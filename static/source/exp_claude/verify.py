"""Verification for the Claude Code agentic runs.

The verdict is the static sweep's: the artifact's harness instrumentation (llm_fuzz_integration), the same
per-target base images, `compile` + `run_fuzzer_new -runs=N` for T1-T5 and evaluation_arvo's container check for
T6, so a hit here means exactly what a hit in the Gemini sweeps means. On top of that, a miss gets the reach
agent's feedback: the lines the input executed are marked with COVERED comments in the agent's copy of the
source (a host directory the agent reads with Read/Grep/Glob), as debug-gym's propose tool did in the container.

Containers run at a low CPU weight, so verification takes CPU only from jobs at the lowest weight (1), such as
fuzzing campaigns run beside it, and at most `verify_slots` (control.json) verifications run at once across all
episodes.
"""
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "exp_gemini"))
import sweep  # noqa: E402  (the Gemini sweep driver: targets, base images, instrumentation, ARVO check)
import const  # noqa: E402  (artifact: input-format texts)

DOCKER_OPTS = ["--cpu-shares", "10", "--memory", "8g"]  # below the Gemini sweeps' verification (100), above weight 1
COVERAGE_RUN_TIMEOUT = 300
TRACER = os.path.join(HERE, "tracer.py")
MARKER = {"python": " # COVERED", "c": " // COVERED"}


def log(episode_dir, msg):
    with open(os.path.join(episode_dir, "verify.log"), "a") as f:
        f.write(f"{sweep.now_iso()} {msg}\n")


def language_of(t):
    return "python" if isinstance(t, sweep.Target) and t.harness.endswith(".py") else "c"


def target_of(episode):
    """The sweep's Target / ArvoTarget for an episode.json."""
    if episode["kind"] == "arvo":
        return sweep.ArvoTarget(sweep.ARVO_ENTRIES[episode["name"]]["project_name"], episode["name"])
    return sweep.Target(episode["project"], episode["level"], episode["name"], "realistic")


def slots_count(run_dir, default=3):
    try:
        with open(os.path.join(run_dir, "control.json")) as f:
            return max(1, int(json.load(f).get("verify_slots", default)))
    except (OSError, ValueError):
        return default


class Slot:
    """One of N flock-based verification slots shared by every episode of a run (each episode's MCP server
    is its own process). N is re-read from control.json at every acquire."""

    def __init__(self, run_dir):
        self.run_dir, self.dir, self.fh = run_dir, os.path.join(run_dir, "scratch", "slots"), None
        os.makedirs(self.dir, exist_ok=True)

    def __enter__(self):
        while True:
            for i in range(slots_count(self.run_dir)):
                fh = open(os.path.join(self.dir, f"slot-{i}"), "w")
                try:
                    fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    self.fh = fh
                    return self
                except OSError:
                    fh.close()
            time.sleep(2)

    def __exit__(self, *exc):
        fcntl.flock(self.fh, fcntl.LOCK_UN)
        self.fh.close()


class BaseImages(sweep.BaseImages):
    """The sweep's per-target base images, but an image that already exists is used without the `docker build`
    that re-checks its cache: every episode's MCP server is a fresh process, and even a fully cached build is slow
    on a busy Docker disk."""

    def get(self, t):
        if t.key not in self.ready:
            image = sweep.image_name_for(t.project, "vbase-" + hashlib.sha1(t.key.encode()).hexdigest()[:12])
            r = subprocess.run(["docker", "image", "inspect", image], capture_output=True)
            if r.returncode == 0:
                harness = "fuzzer_instrumented" + os.path.splitext(t.harness)[1]
                bdir = os.path.join(self.root, t.project, f"{t.level}-{t.name}")
                split = sweep.split_harness_copy(
                    sweep.harness_copy_last(sweep.copy_build_context(t, bdir), harness), harness)
                if split is not None:
                    self.ready[t.key] = image, split[1]
        return super().get(t)


class Verifier:
    def __init__(self, run_dir, episode):
        self.run_dir, self.episode = run_dir, episode
        self.scratch = os.path.join(run_dir, "scratch")
        sweep.use_scratch(self.scratch)
        self.prefix = sweep.resource_prefix("claude-" + os.path.basename(run_dir))
        self.bases = BaseImages(self.prefix, self.scratch)
        self.t = target_of(episode)
        self.language = language_of(self.t)
        self.results_dir = episode["dir"]

    # ------------------------------------------------------------------------------------------ static T1-T5

    def verify(self, data):
        """data: the artifact's parsed input list (llm_fuzz_integration.process_response). -> record dict."""
        if isinstance(self.t, sweep.ArvoTarget):
            return self.verify_arvo(data)
        err, sha, harness_path = sweep.instrument(self.t, data, self.results_dir)
        if err:
            return {"status": err, "hit": False, "detail": "the input does not fit the harness "
                    "(wrong number of input strings for its consume calls)"}
        record = {"harness_sha": sha, "harness": harness_path}
        vdir = os.path.join(self.scratch, "verify", self.t.project,
                            f"{hashlib.sha1(self.t.key.encode()).hexdigest()[:12]}--{sha[:12]}")
        with Slot(self.run_dir):
            status = None
            for attempt in range(1, 4):
                shutil.rmtree(vdir, ignore_errors=True)
                os.makedirs(vdir)
                harness_file = os.path.join(vdir, "fuzzer_instrumented" + os.path.splitext(self.t.harness)[1])
                shutil.copy(harness_path, harness_file)
                run_id = self.prefix + hashlib.sha1(os.path.abspath(vdir).encode()).hexdigest()[:12]
                stages = {}
                try:
                    previous = status
                    status, hit, detail, data_log, coverage = self.build_and_run(harness_file, run_id, stages)
                finally:
                    sweep.cleanup_run(self.t.project, run_id)
                    sweep.cleanup_run(self.t.project, run_id + "-cov")
                if status == "ok" or (status == "compile_fail" and previous == "compile_fail"):
                    break  # a compile failure seen twice is the input's doing: it counts as a miss
                time.sleep(20 * attempt)
        shutil.rmtree(vdir, ignore_errors=True)
        record.update(status=status, hit=bool(hit), attempts=attempt, detail=detail, stage_s=stages)
        if data_log is not None:
            record["output"] = data_log.decode("utf-8", errors="replace")
        record["coverage"] = coverage
        return record

    def build_and_run(self, harness_file, run_id, stages):
        """sweep.build_and_run with the low-weight docker options and, on a miss, a coverage pass.
        -> (status, hit, detail, log bytes | None, {file: [lines]} | None)"""
        t, project = self.t, self.t.project
        out = sweep.out_dir_for(project, run_id)
        start = time.time()
        image, where = self.bases.get(t)
        stages["build"] = round(time.time() - start, 1)
        if image is None:
            return "infra_fail", False, "docker build: " + where, None, None
        env = ["FUZZING_ENGINE=libfuzzer", "SANITIZER=address", "ARCHITECTURE=x86_64", f"PROJECT_NAME={project}",
               "HELPER=True", f"FUZZING_LANGUAGE={self.language}"]
        docker_run = ["docker", "run", "--privileged", "--rm", "--shm-size=2g", "--platform", "linux/amd64",
                      "--pull", "never"] + DOCKER_OPTS
        mount = ["--mount", f"type=bind,source={harness_file},target={where},readonly"]
        name = f"{sweep.CONTAINER}-{run_id}-compile"
        start = time.time()
        if self.language == "python":
            # the ASan compile as the sweep runs it, then the line tracer on the installed project (coverage)
            cmd = ["bash", "-c", "compile && cd $SRC && COV_ROOT=$COV_ROOT COV_OUT=/out/coverage.json "
                                 f"timeout {COVERAGE_RUN_TIMEOUT} python3 /cov/tracer.py {where} "
                                 "> /out/trace.log 2>&1; true"]
            extra = ["-v", f"{TRACER}:/cov/tracer.py:ro", "-e", f"COV_ROOT={self.episode['container_root']}"]
        else:
            cmd, extra = [], []
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)  # a leftover of an interrupted run
        r = sweep.sh(docker_run + ["--name", name] + sweep._env_to_docker_args(env) + extra +
                     ["-v", f"{out}:/out"] + mount + [image] + cmd, sweep.COMPILE_TIMEOUT, name)
        stages["compile"] = round(time.time() - start, 1)
        if r is None:
            return "infra_fail", False, "compile timed out", None, None
        if r.returncode != 0:
            text = r.stdout[-4000:]
            transient = r.returncode in (125, 137) or any(s in text for s in (
                "Killed", "No space left", "Cannot connect to the Docker daemon", "OCI runtime"))
            return ("infra_fail" if transient else "compile_fail"), False, f"compile rc={r.returncode}: {text[-1500:]}", None, None
        name = f"{sweep.CONTAINER}-{run_id}-run"
        start = time.time()
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)  # a leftover of an interrupted run
        r = sweep.sh(docker_run + ["--name", name] + sweep._env_to_docker_args(env + ["RUN_FUZZER_MODE=interactive"]) +
                     ["-v", f"{out}:/out", sweep.RUNNER_IMAGE, "run_fuzzer_new", "fuzzer_instrumented",
                      f"-runs={self.episode['runs']}"], sweep.RUN_TIMEOUT, name)
        stages["run"] = round(time.time() - start, 1)
        log_path = os.path.join(out, "fuzzer_output.log")
        if not os.path.exists(log_path):
            return "infra_fail", False, "no fuzzer_output.log " + (
                "(timeout)" if r is None else f"(rc={r.returncode}): {r.stdout.strip()[-600:]}"), None, None
        hit, data = sweep.read_log(log_path)
        if not sweep.fuzzer_ran(data):  # the harness never started: not the input's fault, not counted
            last = data.strip().splitlines()[-1][-300:].decode("utf-8", "replace") if data.strip() else "(empty log)"
            return "infra_fail", False, "the fuzzer never ran the input: " + last, data, None
        coverage = None
        if not hit:
            start = time.time()
            try:
                coverage = (self.python_coverage(out) if self.language == "python"
                            else self.c_coverage(image, where, harness_file, run_id, env))
            except Exception as e:  # coverage is feedback, never a verdict: a failure only loses the markers
                log(self.results_dir, f"coverage failed: {type(e).__name__}: {e}")
            stages["coverage"] = round(time.time() - start, 1)
        return "ok", hit, None, data, coverage

    def python_coverage(self, out):
        path = os.path.join(out, "coverage.json")
        if not os.path.exists(path):
            log(self.results_dir, "no coverage.json from the tracer: " + tail(os.path.join(out, "trace.log")))
            return None
        with open(path) as f:
            traced = json.load(f)
        return self.map_to_tree({k: set(v) for k, v in traced.items()})

    def c_coverage(self, image, where, harness_file, run_id, env):
        """A second build with SANITIZER=coverage, run once in base-runner, llvm-cov export -> lines per file."""
        out = sweep.out_dir_for(self.t.project, run_id + "-cov")
        env = [e for e in env if not e.startswith("SANITIZER=")] + ["SANITIZER=coverage"]
        docker_run = ["docker", "run", "--privileged", "--rm", "--shm-size=2g", "--platform", "linux/amd64",
                      "--pull", "never"] + DOCKER_OPTS
        name = f"{sweep.CONTAINER}-{run_id}-cov-compile"
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)  # a leftover of an interrupted run
        r = sweep.sh(docker_run + ["--name", name] + sweep._env_to_docker_args(env) + ["-v", f"{out}:/out", "--mount",
                     f"type=bind,source={harness_file},target={where},readonly", image], sweep.COMPILE_TIMEOUT, name)
        if r is None or r.returncode != 0:
            log(self.results_dir, "coverage build failed: " + (r.stdout[-800:] if r else "timeout"))
            return None
        name = f"{sweep.CONTAINER}-{run_id}-cov-run"
        script = (f"cd /out && LLVM_PROFILE_FILE=/out/cov.profraw timeout {COVERAGE_RUN_TIMEOUT} ./fuzzer_instrumented "
                  "-runs=1 > /out/cov_run.log 2>&1; llvm-profdata merge -sparse /out/cov.profraw -o /out/cov.profdata "
                  "&& llvm-cov export -instr-profile=/out/cov.profdata -object=/out/fuzzer_instrumented "
                  "-ignore-filename-regex='.*src/libfuzzer/.*' -format=text > /out/coverage.json")
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)  # a leftover of an interrupted run
        r = sweep.sh(docker_run + ["--name", name] + sweep._env_to_docker_args(env + ["RUN_FUZZER_MODE=interactive"]) +
                     ["-v", f"{out}:/out", sweep.RUNNER_IMAGE, "bash", "-c", script], COVERAGE_RUN_TIMEOUT + 120, name)
        path = os.path.join(out, "coverage.json")
        if r is None or not os.path.exists(path):
            log(self.results_dir, "coverage run failed: " + (r.stdout[-800:] if r else "timeout"))
            return None
        with open(path) as f:
            cov = json.load(f)
        lines = {}
        for file_info in cov.get("data", [{}])[0].get("files", []):
            for seg in file_info.get("segments", []):
                if seg[2] > 0:  # a segment with an execution count: its line ran
                    lines.setdefault(file_info["filename"], set()).add(seg[0])
        return self.map_to_tree(lines)

    def map_to_tree(self, lines_by_container_path):
        """{container path: lines} -> {path relative to the agent's tree: sorted lines}. A file is mapped by
        the longest path suffix that exists in the tree (pip-installed copies live under site-packages)."""
        root, out = self.episode["host_root"], {}
        for path, lines in lines_by_container_path.items():
            parts = path.strip("/").split("/")
            rel = None
            for n in range(1, min(len(parts), 8) + 1):
                cand = "/".join(parts[-n:])
                if os.path.isfile(os.path.join(root, cand)):
                    rel = cand
            if rel and "fuzzer_instrumented" not in rel:
                out[rel] = sorted(set(out.get(rel, [])) | set(lines))
        return out

    # ------------------------------------------------------------------------------------------------ T6

    def verify_arvo(self, data):
        blob = sweep.arvo_input(data)
        sha, path = sweep.store_arvo_input(self.t, data, self.results_dir)
        pattern = sweep.arvo_spec(self.t.project, self.t.name)[3]
        tag = f"{sweep.arvo_tag('claude-' + os.path.basename(self.run_dir))}-{sha[:12]}"
        record = {"harness_sha": sha, "input": path}
        with Slot(self.run_dir):
            for attempt in range(1, 4):
                start = time.time()
                hit, reproduced, output = sweep.ea.run_arvo_container(self.t.name, blob, pattern, timeout=30, tag=tag)
                stages = {"run": round(time.time() - start, 1)}
                status = "infra_fail" if output == "" or output.startswith("docker cp failed:") else "ok"
                if status == "ok":
                    break
                time.sleep(20 * attempt)
        record.update(status=status, hit=bool(hit), reproduced=bool(reproduced), attempts=attempt, stage_s=stages,
                      detail=None if status == "ok" else (output or "container did not start")[-1500:],
                      output=output, coverage=None, cve_pattern=pattern)
        return record


# ------------------------------------------------------------------------------------------- COVERED markers

def strip_markers(path, language):
    marker = MARKER[language]
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    new = text.replace(marker + "\n", "\n")
    if new.endswith(marker):
        new = new[:-len(marker)]
    if new != text:
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(new)


def annotate(root, coverage, language, previously):
    """Mark `coverage` ({rel path: lines}) in the tree at `root` with trailing COVERED comments, after removing
    the markers of the previous proposal (`previously`: rel paths). Returns the rel paths marked now.
    The skip rules are debug-gym's: a continuation line, a line with one triple quote (Python), a line that
    opens a block comment without closing it (C)."""
    marker = MARKER[language]
    for rel in set(previously) | set(coverage):
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            strip_markers(path, language)
    marked = []
    for rel, lines in coverage.items():
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            content = f.read().split("\n")
        n = 0
        for line_no in lines:
            i = line_no - 1
            if i < 0 or i >= len(content):
                continue
            line = content[i]
            if line.rstrip().endswith("\\"):
                continue
            if language == "python" and (line.count("'''") == 1 or line.count('"""') == 1):
                continue
            if language == "c" and "/*" in line and "*/" not in line:
                continue
            content[i] = line + marker
            n += 1
        if n:
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write("\n".join(content))
            marked.append(rel)
    return marked


def tail(path, n=1500):
    try:
        with open(path, "rb") as f:
            return f.read()[-n:].decode("utf-8", errors="replace")
    except OSError:
        return ""


def input_format_text(t):
    """The artifact's own description of how the input is put into the harness (const.HARNESS_PROMPTS)."""
    if isinstance(t, sweep.ArvoTarget):
        return const.ARVO_HARNESS_PROMPTS[t.project][1]
    return const.HARNESS_PROMPTS[t.project][1]


def parse_input(text):
    """The agent's proposal -> the artifact's input list, or None. Accepts a ``` block or bare text."""
    text = text.strip("\n")
    if "```" not in text:
        text = "```\n" + text + "\n```"
    elif not re.search(r"```\n", text):
        text = text.replace("```", "```\n", 1)
    data, code = sweep.lfi.process_response(text)
    return data if code == sweep.lfi.ErrorCode.SUCCESS else None
