import atexit
import json
import os
import re
import tempfile
from pathlib import Path

from debug_gym.gym.terminal import Terminal, DockerTerminal
from debug_gym.gym.utils import make_file_matcher
from debug_gym.logger import DebugGymLogger


class Workspace:

    def __init__(self, terminal: Terminal, logger: DebugGymLogger | None = None):
        self._tempdir = None
        self.working_dir = None
        self.logger = logger or DebugGymLogger("debug-gym")
        self.terminal = terminal

    def cleanup(self):
        if self.working_dir == Path("/testbed"):
            self.working_dir = None 
        if self._tempdir:
            self._tempdir.cleanup()
            self._tempdir = None

    def reset(
        self,
        readonly_patterns: list[str] | None = None,
        ignore_patterns: list[str] | None = None,
    ):
        self.cleanup()
        if self.working_dir is None:
            self.working_dir = Path("/testbed")
        if type(self.terminal) is Terminal:
            self._tempdir = tempfile.TemporaryDirectory(prefix="DebugGym-")
            atexit.register(self._tempdir.cleanup)
            self.working_dir = Path(self._tempdir.name).resolve()

        self.logger.debug(f"Working directory: {self.working_dir}")
        self.terminal.working_dir = str(self.working_dir)
        self.setup_file_filters(readonly_patterns, ignore_patterns)

    def setup_file_filters(
        self,
        readonly_patterns: list[str] | None = None,
        ignore_patterns: list[str] | None = None,
    ):
        """Indexes files and subdir in the working
        directory, applying ignore and readonly patterns."""
        self._is_readonly_func = lambda f: False
        self._is_ignored_func = lambda f: False

        readonly_patterns = readonly_patterns or []
        ignore_patterns = ignore_patterns or []

        # Ignore debug gym hidden files
        ignore_patterns += [".debugignore", ".debugreadonly"]

        ignore_patterns += (
            self.read_file(".gitignore").splitlines()
            if self.has_file(".gitignore")
            else []
        )
        ignore_patterns += (
            self.read_file(".debugignore").splitlines()
            if self.has_file(".debugignore")
            else []
        )

        readonly_patterns += (
            self.read_file(".debugreadonly").splitlines()
            if self.has_file(".debugreadonly")
            else []
        )

        # create a matcher function for ignored files, .debugignore has precedence over .gitignore
        self._is_ignored_func = make_file_matcher(
            base_dir=self.working_dir,
            pattern_files=[],
            patterns=ignore_patterns,
        )

        # create a matcher function for readonly files
        self._is_readonly_func = make_file_matcher(
            base_dir=self.working_dir,
            pattern_files=[],
            patterns=readonly_patterns,
        )

    def copy_content(self, src: str | Path, target: str | Path | None = None):
        """Copy files contained in src to a target directory."""
        src = Path(src).resolve()
        target = Path(target or self.working_dir).resolve()
        self.terminal.copy_content(src, target)

    def resolve_path(self, filepath: str | Path, raises=False) -> Path:
        """Convert a relative filepath to absolute based on the working_dir.
        If the path is already absolute, it is returned as is.
        If raises is True, raises FileNotFoundError if the file does not exist,
        is not in the working directory or is ignored by the ignore patterns.
        If raises is False, returns the absolute path regardless of the file existence.
        """
        abs_filepath = Path(filepath)
        if not abs_filepath.is_absolute():
            abs_filepath = Path(self.working_dir) / abs_filepath
        abs_filepath_str = str(abs_filepath)

        if raises and abs_filepath != self.working_dir:
            # Check if file exists, is within working_dir and is not ignored.
            check_cmd = (
                f'abs_path=$(realpath "{abs_filepath_str}"); '
                f'test -e "$abs_path" && [[ "$abs_path" == {self.working_dir}* ]]'
            )
            success, result = self.terminal.run(
                f"{check_cmd} && echo OK || echo MISSING"
            )
            if result.strip() != "OK" or self._is_ignored_func(abs_filepath):
                raise FileNotFoundError(
                    f"`{filepath}` does not exist or is not in "
                    f"the working directory `{self.working_dir}`."
                )

        return Path(abs_filepath_str)

    def read_file(self, filepath: str) -> str:
        """Reads a file from the working directory.
        Raises value error if the file does not exist"""
        abs_filepath = self.resolve_path(filepath, raises=True)
        success, output = self.terminal.run(
            f"cat {abs_filepath}", raises=True, strip_output=False
        )
        return output

    def write_file(self, filepath: str, content: str):
        """Writes `content` to `filepath` exactly as-is, preserving any trailing newlines."""
        abs_filepath = self.resolve_path(filepath)

        # In the following command we:
        # - use a single-quoted heredoc (cat <<'nDEBUGGYM_EOF' ... nDEBUGGYM_EOF) so the heredoc body is taken literally (no shell expansion)
        # - append a sentinel character DEBUGGYM_DEL inside the heredoc so we can detect/restore trailing newlines later
        # - capture the heredoc output into shell variable CONTENT since command substitution strips trailing newlines
        # - "${CONTENT%DEBUGGYM_DEL}" removes the trailing sentinel DEBUGGYM_DEL (restoring the original trailing-newline state)
        # - echo -n writes the result without adding an extra newline
        cmd = f"CONTENT=$(cat <<'DEBUGGYM_EOF'\n{content}DEBUGGYM_DEL\nDEBUGGYM_EOF\n); echo -n \"${{CONTENT%DEBUGGYM_DEL}}\" > {abs_filepath}"
        self.terminal.run(cmd, raises=True)

    def directory_tree(self, root: str | Path = None, max_depth: int = 1):
        root = self.resolve_path(root or self.working_dir, raises=True)
        # Use the terminal to run a bash command to list files
        tree_cmd = f"tree --charset=ASCII --noreport -a -v -F -f -L {max_depth} {root} "
        success, output = self.terminal.run(tree_cmd)
        # TODO: Check the possible failure reason
        if not success:
            msg = f"Failed to run tree command: {output}"
            self.logger.error(msg)
            return msg

        first, *rest = output.splitlines()
        lines = [first]
        for line in rest:
            assert "-- " in line
            prefix, path = line.split("-- ", 1)
            prefix += "-- "

            if self._is_ignored_func(path):
                continue

            lines.append(f"{prefix}{os.path.basename(path.rstrip('/'))}")

            if path.endswith("/"):
                # i.e. a directory
                lines[-1] += "/"

            if self._is_readonly_func(path):
                lines[-1] += " (read-only)"

        output = "\n".join(lines)

        # To maintain backward compatibility with previous version of debug-gym.
        output = output.replace("`", "|").replace("    ", "  ")
        return output

    def is_editable(self, filepath):
        return not self._is_readonly_func(self.resolve_path(filepath, raises=True))

    def display_files(self, dir_tree_depth: int = 1) -> str:
        msg = (
            "Listing files in the current working directory."
            " (read-only) indicates read-only files."
            f" Max depth: {str(dir_tree_depth)}.\n"
        )
        msg += self.directory_tree(max_depth=dir_tree_depth)
        return msg

    def has_file(self, filepath: str) -> bool:
        """Checks if a file exists in the working directory.
        Shortcut for `resolve_path` with raises=True.
        """
        try:
            self.resolve_path(filepath, raises=True)
            return True
        except FileNotFoundError:
            return False

    def replace_harness_inputs(self, harness_path: str, inputs: list[str]) -> str | None:
        # First replace the harness with the original content, i.e., with the 'EXAMPLE_INPUT' strings
        self.logger.debug(f"Replacing inputs in harness {harness_path} with {inputs}")
        mismatched_msg = None
        original_harness_path = harness_path.replace('instrumented_', 'original_')

        assert self.has_file(original_harness_path), f"original harness {original_harness_path} not found"

        original_content = self.read_file(original_harness_path)
        required_inputs = original_content.count('EXAMPLE_INPUT')
        self.logger.debug(f"Original harness {original_harness_path} requires {required_inputs} input(s)")
        if required_inputs != len(inputs):
            mismatched_msg = (
                f"The original harness {original_harness_path} requires {required_inputs} input(s), since it contains {required_inputs} 'EXAMPLE_INPUT' placeholders, "
                f"but {len(inputs)} inputs were provided. Make sure to provide the correct number of inputs."
            )
            self.logger.warning(mismatched_msg)
        self.write_file(harness_path, original_content)
        # Then replace the 'EXAMPLE_INPUT' strings with the new inputs
        for i, input_str in enumerate(inputs):
            input_str = input_str.replace('\n', '\\n').replace('"', '\\"')
            self.replace_content(harness_path, f'EXAMPLE_INPUT_{i+1}', input_str)
        
        return mismatched_msg

    def replace_content(self, file_path: str, old_str: str, new_str: str):
        self.logger.debug(
            f"Replacing content in {file_path}: '{old_str}' -> '{new_str}'"
        )
        old_content = self.read_file(file_path)
        abs_filepath = self.resolve_path(file_path, raises=True)
        new_content = old_content.replace(old_str, new_str)
        self.write_file(abs_filepath, new_content)
        return True

    def extract_input(self, response: str) -> list[str]:
        pattern = r"```(.*?)```"
        match = re.search(pattern, response, re.DOTALL)
        if match:
            input_content = match.group(1).strip()
            return input_content.split("\n\n")
        else:
            return None

    def validate_reachability(self, harness_name: str, project_name: str) -> tuple[bool, str, dict | None]:
        if harness_name.endswith(".py"):
        # Define all relevant paths inside the container
            testbed_dir = "/src/testbed"
            harness_path = f"{testbed_dir}/{harness_name}"
            rcfile_path = f"{testbed_dir}/fuzzer_coveragerc"
            coverage_data_path = f"{testbed_dir}/fuzzer_coverage"
            coverage_json_path = f"{testbed_dir}/fuzzer_coverage.json"

            # Step 1: Create the .coveragerc file to ensure coverage data is saved on timeout
            self.logger.debug(f"Creating coverage config at {rcfile_path}")
            coveragerc_content = f"[run]\nsigterm = True\ndata_file = {coverage_data_path}\n"
            self.terminal.run(f"echo -e '{coveragerc_content}' > {rcfile_path}", raises=True)

            # Step 2: Define the command to run the harness with coverage
            run_cmd = f"timeout --foreground -k 30 30 python3 -m coverage run --rcfile={rcfile_path} {harness_path}"

            # Step 3: Execute the harness with a 40-second timeout
            self.logger.info(f"Running harness to check reachability (40s timeout)...")
            self.logger.debug(f"Executing command: {run_cmd}")
            # The output of the run is captured by the run command itself
            success, exec_output = self.terminal.run(run_cmd, timeout=40, raises=False)
            self.logger.debug(f"Harness execution result:\n\tsuccess: {success}\n\toutput: {exec_output}")

            if "HIT TARGET" in exec_output:
                self.logger.info("Target line was reached during harness execution.")
                return True, exec_output, None

            # Step 4: Check if the coverage data file was created and is not empty
            # The shell command '[ -s <file> ]' exits with 0 if the file exists and is not empty
            check_cmd = f"[ -s {coverage_data_path} ]"
            self.logger.info("Checking for coverage data...")
            coverage_found, _ = self.terminal.run(check_cmd, raises=False)
            coverage_json = None
            if coverage_found:
                self.logger.info("Coverage data found. Generating JSON report.")
                # Step 5: If data exists, generate the JSON report with a 30s timeout
                report_cmd = f"timeout 30 python3 -m coverage json --data-file={coverage_data_path} -o {coverage_json_path}"
                success, output = self.terminal.run(report_cmd, timeout=30, raises=False)
                json_data = self.read_file(coverage_json_path)
                coverage_json = json.loads(json_data)
            else:
                self.logger.warning("No coverage data found. The target is not reached.")

            return False, exec_output, coverage_json

        else:
            harness_path = Path(str(self.logger.log_dir.resolve()).rstrip('/') + '/out/instrumented_fuzzer')
            harness_path.unlink(missing_ok=True)
            fuzzing_env = {
                "FUZZING_ENGINE": "libfuzzer",
                "SANITIZER": "address",
                "ARCHITECTURE": "x86_64",
                "PROJECT_NAME": project_name,
                "HELPER": "True",
                "FUZZING_LANGUAGE": "c"
            }

            self.logger.debug(f"fuzzing_env: {fuzzing_env}")
            self.terminal.env_vars.update(fuzzing_env)
            success, compile_output = self.terminal.run("compile", timeout=60, raises=False)
            self.logger.debug(f"Harness compilation result:\n\tsuccess: {success}\n\toutput: {compile_output}")
            if not success:
                self.logger.error("Compilation failed.")
                return False, compile_output, None

            # --- 2. Spin up the Runner Container ---
            self.logger.info("Starting base-runner container for evaluation...")
            
            # Use the host path where your binaries are located
            host_binary_path = str(self.logger.log_dir.resolve()).rstrip('/') + '/out'
            fuzzing_env["RUN_FUZZER_MODE"] = "interactive"
            fuzzing_env["LLVM_PROFILE_FILE"] = "/out/coverage.profraw"
            
            # Update the specific binary name if it differs from 'instrumented_fuzzer'
            # Based on your bash script: $OUT/fuzzer_instrumented
            # Based on your python code: instrumented_fuzzer
            binary_name = "instrumented_fuzzer" 
            binary_path = f"/out/{binary_name}"
            self.logger.debug(f"fuzzing_env: {fuzzing_env}")
            self.logger.debug(f"host_binary_path: {host_binary_path}")

            runner_terminal = DockerTerminal(
                base_image="gcr.io/oss-fuzz-base/base-runner",
                host_out_dir=host_binary_path,
                env_vars=fuzzing_env,
                logger=self.logger
            )

            coverage_json = None
            eval_output = ""
            target_reached = False

            try:
                # --- 3. Execute the binary directly (Replicating the Bash 'else' block) ---
                self.logger.info(f"Running binary '{binary_name}' for coverage generation...")
                
                # Bash: $OUT/fuzzer_instrumented -runs=30 > ...
                # We run this directly instead of via 'run_fuzzer_new' to ensure strict control over flags
                run_cmd = f"LLVM_PROFILE_FILE=/out/coverage.profraw {binary_path} -runs=30"
                
                # Run with timeout (e.g., 60s as per your bash script)
                success, output = runner_terminal.run(run_cmd, timeout=60, raises=False)

                # Save execution log
                eval_logfile = os.path.join(host_binary_path, 'fuzzer_output.log')
                with open(eval_logfile, 'w', encoding='utf-8') as f:
                    f.write(output)
                eval_output = output

                if "HIT TARGET" in output:
                    target_reached = True

                # --- 4. Process Coverage Data (llvm-profdata) ---
                # Bash: llvm-profdata merge -j=1 -sparse coverage.profraw -o coverage.profdata
                self.logger.info("Merging coverage profiles...")
                merge_cmd = "llvm-profdata merge -j=1 -sparse /out/coverage.profraw -o /out/coverage.profdata"
                runner_terminal.run(merge_cmd, timeout=30, raises=False)

                # Cleanup raw profile (Bash: rm coverage.profraw)
                runner_terminal.run("rm /out/coverage.profraw", raises=False)

                # --- 5. Export Coverage to JSON (llvm-cov) ---
                # Bash: if [ -s /out/coverage.profdata ]; then ...
                check_cmd = "[ -s /out/coverage.profdata ]"
                data_found, _ = runner_terminal.run(check_cmd, raises=False)

                if data_found:
                    self.logger.info("Coverage profile found. Exporting to JSON...")
                    
                    # Bash: llvm-cov export -instr-profile=... -object=... -path-equivalence=...
                    # Note: We output to coverage.json inside the container (/out/coverage.json), 
                    # which maps to host_binary_path/coverage.json
                    export_cmd = (
                        f"llvm-cov export -instr-profile=/out/coverage.profdata "
                        f"-object={binary_path} "
                        f"-path-equivalence=/src,/out "
                        f"'-ignore-filename-regex=.*src/libfuzzer/.*' "
                        f"-format=text > /out/coverage.json"
                    )
                    
                    runner_terminal.run(export_cmd, timeout=30, raises=False)

                    # --- 6. Read JSON from Host ---
                    coverage_json_path = os.path.join(host_binary_path, "coverage.json")
                    if os.path.exists(coverage_json_path):
                        try:
                            with open(coverage_json_path, 'r', encoding='utf-8') as f:
                                coverage_json = json.load(f)
                        except json.JSONDecodeError as e:
                            self.logger.error(f"Failed to parse coverage JSON: {e}")
                    else:
                        self.logger.warning("coverage.json was not created on host.")
                else:
                    self.logger.warning("No coverage.profdata found inside container.")

                return target_reached, eval_output, coverage_json

            except Exception as e:
                self.logger.error(f"Error during runner execution: {e}")
                return target_reached, eval_output, None
            
            finally:
                self.logger.debug("Closing runner terminal...")
                runner_terminal.close()

    def instrument_comments_for_lines(self, filename: str, covered_lines: list[int]):
        self.logger.debug(f"[instrument_comments_for_lines] filename: {filename}\tlines: {covered_lines}")
        abs_filepath = self.resolve_path(filename)
        is_python = filename.endswith(".py")
        
        # sed_marker: Use raw strings for C++ to ensure backslashes are passed literally to sed
        # C++ result:  \/\/ COVERED  (Escaped for regex)
        # Python result:  # COVERED
        sed_marker = " # COVERED" if is_python else r" \/\/ COVERED"
        
        def ends_with_continuation(line: str) -> bool:
            return line.rstrip().endswith("\\")

        def contain_triple_quote(line: str) -> bool:
            return line.count("'''") == 1 or line.count('"""') == 1

        def is_unclosed_block_comment(line: str) -> bool:
            return "/*" in line and "*/" not in line

        def skip_instrumentation(line: str, index: int) -> bool:
            if ends_with_continuation(line):
                self.logger.warning(f"Line {index+1} in {filename} ends with continuation '\\'. Skipping.")
                return True

            if is_python:
                if contain_triple_quote(line):
                    self.logger.warning(f"Line {index+1} in {filename} has triple quotes. Skipping.")
                    return True
            else:
                if is_unclosed_block_comment(line):
                    self.logger.warning(f"Line {index+1} in {filename} starts unclosed block comment. Skipping.")
                    return True
                
                # Skip preprocessor directives (optional but safer)
                if line.strip().startswith("#") and not line.strip().startswith("#include"):
                    pass 

            return False

        try:
            content = self.read_file(filename)
        except Exception as e:
            self.logger.error(f"Failed to read file {filename}: {e}")
            return

        lines = content.splitlines()
        lines_to_instrument = []

        # Calculate valid lines to mark
        for line_num in covered_lines:
            idx = line_num - 1 # 0-based index
            if idx < 0 or idx >= len(lines):
                self.logger.warning(f"Line {line_num} is out of range for file {filename}")
                continue
                
            original_line = lines[idx]
            if not skip_instrumentation(original_line, idx):
                lines_to_instrument.append(line_num)

        if not lines_to_instrument:
            return
        
        # 1. Clean existing markers to prevent duplicates (Idempotency)
        # We use '|' as delimiter to avoid conflict with '/' in C comments.
        clean_cmd = f"sed -i 's|{sed_marker}||g' {abs_filepath}"
        self.terminal.run(clean_cmd, raises=True)

        # 2. Batch write new markers
        BATCH_SIZE = 500 
        for i in range(0, len(lines_to_instrument), BATCH_SIZE):
            batch = lines_to_instrument[i : i + BATCH_SIZE]
            # Build sed script: "10s/$/ MARKER/; 20s/$/ MARKER/"
            sed_cmds = [f"{n}s/$/{sed_marker}/" for n in batch]
            sed_script = ";".join(sed_cmds)
            
            # Run batch
            full_cmd = f"sed -i '{sed_script}' {abs_filepath}"
            self.terminal.run(full_cmd, raises=True)

        self.logger.info(f"Instrumented {len(lines_to_instrument)} lines in {filename}")

    def instrument_coverage(self, coverage_json: dict, harness_name: str):
        # This function remains largely the same, just calling the updated method
        # if coverage_json is None:
        #     self.logger.warning("[instrument_coverage] coverage json is empty")
        #     return
        assert coverage_json is not None, "[instrument_coverage] coverage json is empty"
        if harness_name.endswith(".py"):
            for filename, file_coverage in coverage_json.get("files", {}).items():
                executed_lines = file_coverage.get("executed_lines", [])
                self.instrument_comments_for_lines(filename, executed_lines)
        else:
            if "data" not in coverage_json or not coverage_json["data"]:
                self.logger.warning("No data found in coverage JSON.")
                return

            files_data = coverage_json["data"][0]["files"]
            for file_info in files_data:
                container_filename = file_info["filename"]
                # Filter out irrelevant files
                if "fuzzer_instrumented" in container_filename or "/src/testbed/" not in container_filename:
                    continue
                self.logger.debug(f"container_filename: {container_filename}")
                
                # Extract Covered Lines
                covered_lines = set()
                segments = file_info.get("segments", [])
                for seg in segments:
                    line_num = seg[0]
                    exec_count = seg[2]
                    if exec_count > 0:
                        covered_lines.add(line_num)
                if covered_lines:
                    # convert set to sorted list for cleaner processing
                    sorted_lines = sorted(list(covered_lines))
                    self.instrument_comments_for_lines(container_filename, sorted_lines)