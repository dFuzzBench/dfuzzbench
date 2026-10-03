import copy
import re

from debug_gym.gym.entities import Observation
from debug_gym.gym.terminal import ShellSession
from debug_gym.gym.tools.tool import EnvironmentTool
from debug_gym.gym.tools.toolbox import Toolbox


@Toolbox.register()
class LLDBTool(EnvironmentTool):
    name: str = "lldb"
    examples = [
        """lldb(command="b src/main.cpp:42") to set a breakpoint at line 42 in 'src/main.cpp'.""",
        """lldb(command="run") to start the execution (required after initialization).""",
        """lldb(command="c") to continue the execution until the next breakpoint.""",
        """lldb(command="p x") to print the value of the variable x.""",
        """lldb(command="br del 1") to delete breakpoint number 1.""",
    ]
    description = (
        "An interface to the LLDB debugger for C/C++. Send a command to the LLDB terminal."
        + "\nImportant: Unlike PDB, you must issue 'run' or 'r' to start execution after the session loads."
        + "\nWhen setting breakpoints, use `b file_path:line_number`."
        + "\nTo clear breakpoints, use `br del <id>` (check ids with `br list`)."
        + "\nExamples:\n"
        + "\n".join(examples)
    )
    arguments = {
        "command": {
            "type": ["string"],
            "description": "The command to be sent to the LLDB terminal. Should be a valid LLDB command.",
        },
    }

    def __init__(self):
        super().__init__()
        self.current_frame_file = None
        self._session: ShellSession = None

    def __getstate__(self):
        state = self.__dict__.copy()
        for k in ["_session", "current_frame_file"]:
            del state[k]
        return state

    def __setstate__(self, state):
        self.__dict__.update(state)
        self.current_frame_file = None
        self._session = None

    def __deepcopy__(self, memo):
        result = type(self).__new__(self.__class__)
        memo[id(self)] = result
        for k, v in self.__dict__.items():
            if k == "_session":
                setattr(result, k, None)
            elif k == "current_frame_file":
                setattr(result, k, None)
            else:
                setattr(result, k, copy.deepcopy(v, memo))
        return result

    @property
    def lldb_is_running(self):
        return self._session is not None and self._session.is_running

    def interact_with_lldb(self, command: str, timeout: int | None = None) -> str:
        if timeout is None:
            timeout = 60
        try:
            # LLDB prompt is "(lldb)"
            output = self._session.run(command, read_until="(lldb)", timeout=timeout)
        except TimeoutError as e:
            output = f"The command `{command}` has timed out. No breakpoint was triggered. This means the current input forces the program down a path that DOES NOT hit your target line."
            self.close_lldb()

        return output.replace("(lldb)", "").strip()

    def close_lldb(self):
        if self._session:
            self._session.close()
        self.current_frame_file = None

    def start_lldb(self, environment) -> str:
        print(f"[start_lldb] Initializing...")
        self._session = environment.terminal.new_shell_session()
        
        # 1. Start LLDB *without* the binary first.
        # This gives us a clean session.
        cmd = "lldb --no-use-colors"
        initial_output = self._session.start(
            cmd, read_until="(lldb)"
        )

        # 2. Explicitly load the target using 'target create'.
        # This allows us to capture the specific loading error.
        binary_path = "/out/instrumented_fuzzer"
        print(f"[start_lldb] Loading target: {binary_path}")
        
        # We use interact_with_lldb (or manual run) to send the command
        # Note: We can't use self.interact_with_lldb yet if it relies on self.lldb_is_running logic
        # that checks self._session. So we use the session directly here.
        load_cmd = f"target create '{binary_path}'"
        load_output = self._session.run(load_cmd, read_until="(lldb)")
        
        # Clean up the output (remove the prompt)
        load_output = load_output.replace("(lldb)", "").strip()
        print(f"[start_lldb] Load Output: {load_output}")

        # 3. CRITICAL: Check for load failure
        # If the DWARF version is wrong, this is where we catch it.
        if "error:" in load_output or "extraction failed" in load_output:
            print(f"[start_lldb] FATAL: Could not load target. {load_output}")
            # We explicitly close the session so lldb_is_running returns False
            self._session.close()
            self._session = None
            return f"Failed to start LLDB: Target load error: {load_output}"
        
        initial_output += f"\n{load_output}"

        # 4. Restore breakpoints (only if load succeeded)
        if environment.persistent_breakpoints:
            print(f"[start_lldb] Restoring breakpoints")
            print(f"[start_lldb] Number of persistent breakpoints: {len(environment.current_breakpoints_state)}")
            for _, _command in environment.current_breakpoints_state.items():
                self.interact_with_lldb(_command, environment.run_timeout)
            if len(environment.current_breakpoints_state) > 0:
                initial_output += "\nBreakpoints have been restored."

        self.set_current_frame_file(environment)
        return initial_output

    def on_env_reset(self, environment, **kwargs) -> Observation:
        super().on_env_reset(environment, **kwargs)
        obs = self.start_lldb(environment)
        return Observation(self.name, obs)

    def on_rewrite_success(
        self, environment, file, head, tail, length, **kwargs
    ) -> Observation:
        self.breakpoint_modify(environment, file, head, tail, length)
        obs = self.restart_lldb(environment)
        obs = "\nDebugging terminal started:\n" f"{obs}\n"
        return Observation(self.name, obs)

    def restart_lldb(self, environment) -> str:
        self.close_lldb()
        return self.start_lldb(environment)

    def use(self, environment, command: str) -> Observation:
        if command == "":
            return Observation(self.name, "Failure calling lldb:\nEmpty commands are not allowed.")

        _warning = ""
        # LLDB allows multiple commands via 'command source' or similar, but for safety in Gym:
        if ";" in command or "\n" in command:
             # Basic heuristic check, though LLDB commands can be complex
            splits = re.split("\n|;", command)
            if len(splits) > 1:
                command = splits[0].strip()
                _warning += "Multiple commands are not supported. Only the first command will be executed."

        success, output = True, ""
        if not self.lldb_is_running:
            print("[LLDBTool use] lldb is not running, starting lldb...")
            output += self.start_lldb(environment)

        if not self.lldb_is_running:
            return Observation(self.name, f"Failure calling lldb:\n{output}")

        # --- Command Logic ---
        
        # Handle Clear/Delete
        # PDB uses 'cl' or 'clear'. LLDB uses 'breakpoint delete' or 'br del'.
        # If the agent tries to use PDB syntax, we might want to catch it, or just pass it through
        # (lldb has no 'cl' command by default, so it would fail, which is fine for learning).
        if command.startswith("cl ") or command == "clear":
             # Optional: Translate PDB 'cl' to LLDB 'br del' to be friendly?
             # For now, let's assume the agent must learn LLDB syntax, 
             # but we handle 'br del' specifically to update state.
             pass

        # Handle Breakpoint listing
        if command in ["b", "break", "br", "breakpoint list"]:
            success, output = (True, f"Breakpoints:\n{environment.current_breakpoints()}\n")
        
        # Handle clearing all breakpoints
        elif command in ["br del", "breakpoint delete"] and len(command.split()) == 2:
             # Deleting all breakpoints usually prompts for confirmation in LLDB: "About to delete all... [Y/n]"
             # We need to handle that interaction or force it.
             # Use: "breakpoint delete -f" (force)
             real_cmd = "breakpoint delete -f"
             environment.current_breakpoints_state = {}
             out = self.interact_with_lldb(real_cmd, environment.run_timeout)
             success, output = True, "All breakpoints have been cleared."

        # Standard interaction
        else:
            try:
                lldb_out = self.interact_with_lldb(command, environment.run_timeout)
                output += f"LLDB command output:\n{lldb_out}"

                print(f"[use] lldb_out before update_breakpoints: {lldb_out}")
                if self.lldb_is_running and "TimeoutError" not in lldb_out:
                    try:
                        self.update_breakpoints(environment)
                    except Exception as e:
                        print(f"[LLDBTool] Failed to sync breakpoints: {e}")
                else:
                    print("[LLDBTool] Skipping breakpoint sync due to timeout/session death.")

            except Exception as e:
                success = False
                output = str(e)

        # Check for Process Exit
        if "exited with status" in output or "Process" in output and "exited" in output:
             output += "\nProcess exited. Restarting session..."
             # We don't necessarily restart the whole shell, but the process is dead.
             # LLDB keeps the shell open.
             pass 

        if _warning:
            obs = f"{_warning}\n{output.strip()}\n"
        else:
            obs = f"{output.strip()}\n"

        # Add Context (Current Frame)
        if self.lldb_is_running:
            current_frame = self.set_current_frame_file(environment)
            
            # Context Listing (source code)
            # In LLDB, 'source list' or 'l' works.
            list_output = ""
            if environment.auto_list and command.split()[0] not in ["l", "list", "source"]:
                list_output = self.interact_with_lldb("source list -c 10", environment.run_timeout)

            if current_frame:
                obs += f"\nCurrent frame:\n{current_frame}\n"
            if list_output:
                obs += f"\nContext around the current frame:\n{list_output}\n"

        return Observation(self.name, obs)

    def breakpoint_modify(
        self, environment, rewrite_file, rewrite_head, rewrite_tail, new_code_length
    ):
        # handle breakpoints line number changes caused by rewriting
        # this is a wrapper that manages the self.breakpoints_state, which does not reset at each pseudo terminal start
        # self.breakpoints_state is a dict, the keys are "|||".join([file_path, str(line_number)]) and values are breakpoint_command
        if len(environment.current_breakpoints_state) == 0:
            return
        current_breakpoints_state_copy = copy.deepcopy(
            environment.current_breakpoints_state
        )
        rewrite_file = environment.workspace.resolve_path(rewrite_file)
        for _key in environment.current_breakpoints_state.keys():
            _file_path, _line_number = _key.split("|||")
            _file_path = environment.workspace.resolve_path(_file_path)
            if _file_path != rewrite_file:
                # the breakpoints are not in the current file, no need to modify
                continue
            _line_number = int(_line_number)
            if rewrite_head is None:
                # no line number is provided, rewrite the whole code
                # we remove all breakpoints in the current file
                del current_breakpoints_state_copy[_key]
            else:
                # if a breakpoint was set in between the rewritten code, we need to remove it
                if rewrite_head <= _line_number <= rewrite_tail:
                    del current_breakpoints_state_copy[_key]
                # if a breakpoint was set after the rewritten code, we need to move it
                elif _line_number > rewrite_tail:
                    new_line_number = (
                        _line_number
                        + new_code_length
                        - (rewrite_tail - rewrite_head + 1)
                    )
                    new_key = "|||".join([str(_file_path), str(new_line_number)])
                    _new_value = environment.current_breakpoints_state[_key].split(":")
                    _new_value[1] = " ".join(
                        [str(new_line_number), " ".join(_new_value[1].split()[1:])]
                    )
                    current_breakpoints_state_copy[new_key] = ":".join(
                        _new_value
                    ).strip()
                    del current_breakpoints_state_copy[_key]
                # if a breakpoint was set before the rewritten code, we don't need to do anything
                else:
                    pass
        environment.current_breakpoints_state = current_breakpoints_state_copy


    def update_breakpoints(self, environment):
        """Updates internal state by parsing `breakpoint list` output."""
        command = "breakpoint list"
        # Get raw output
        raw_output = self.interact_with_lldb(command, environment.run_timeout)
        
        # DEBUG: Print the raw representation to see hidden \r or \n characters
        # This will prove if the data is actually missing or just hidden.
        print(f"[update_breakpoint] RAW output repr: {repr(raw_output)}")

        new_breakpoints = {}
        
        # Split by newlines (handles \r\n, \n, and \r)
        lines = raw_output.splitlines()
        
        # Regex 1: Standard file/line
        # Matches: 1: file = 'Str.c', line = 44
        # Explanation:
        # file\s*=\s* -> matches "file =" with any spacing
        # ['"]?        -> matches optional quote
        # ([^'",]+)    -> Capture Group 1: The filename (any char except quotes/comma)
        # ['"]?        -> matches optional closing quote
        # .*?          -> minimal match until...
        # line\s*=\s* -> matches "line ="
        # (\d+)        -> Capture Group 2: The line number
        pattern_file_attr = re.compile(r"file\s*=\s*['\"]?([^'\",]+)['\"]?.*?line\s*=\s*(\d+)")

        # Regex 2: The "name" format (Pending breakpoints)
        # Matches: 1: name = 'Str.c:44'
        pattern_name = re.compile(r"name\s*=\s*['\"]?([^'\",]+):(\d+)['\"]?")

        for line in lines:
            line = line.strip()
            
            # Skip empty lines or headers
            if not line or line.startswith("Current breakpoints"):
                continue

            file_path = None
            line_number = None

            # Try Pattern 1 (Standard)
            m1 = pattern_file_attr.search(line)
            if m1:
                file_path, line_number = m1.groups()
            
            # Try Pattern 2 (Pending/Name)
            if not file_path:
                m3 = pattern_name.search(line)
                if m3:
                    file_path, line_number = m3.groups()

            # If we found a match, store it
            if file_path and line_number:
                # Key format: file|||line
                key = "|||".join([file_path, line_number])
                
                # We assume standard breakpoint syntax for restoration
                new_breakpoints[key] = f"b {file_path}:{line_number}"
        
        # Update the environment
        environment.current_breakpoints_state = new_breakpoints
        print(f"[update_breakpoint] Parsed state: {new_breakpoints}")

    def set_current_frame_file(self, environment) -> str | None:
        command = "frame info" 
        # We assume the session is open; checking self.lldb_is_running might be tricky 
        # if you consider 'loaded but not run' as running.
        # Safest is to just try running it.
        try:
            output = self._session.run(command, read_until="(lldb)")
            output = output.replace("(lldb)", "").strip()
        except Exception:
            return None

        # --- FIX: Handle the "Not running" state gracefully ---
        if "Command requires a current process" in output:
            return "Process not started. Use 'run' to start."
        # -----------------------------------------------------

        match = re.search(r" at (.+?):(\d+)", output)
        
        file_path = None
        if match:
            file_path = f"{match.group(1)}({match.group(2)})"

        if self.current_frame_file != file_path:
            self.current_frame_file = file_path
        
        return output