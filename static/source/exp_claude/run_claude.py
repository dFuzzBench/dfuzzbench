"""Claude Code as the agent of the agentic setting: one `claude -p` episode per target, with Read/Grep/Glob on a
host copy of the target's source tree and a `propose_input` MCP tool (propose_mcp.py) as the only way to run
anything. The reach agent's budgets: 50 turns, 15 proposals; its feedback: hit/miss, harness output, COVERED
markers. No shell, no debugger, no web.

    run_claude.py run    --name test --targets cmark::src:blocks.c:144 wasm3::42495613 [--concurrency 2]
    run_claude.py run    --name claude --all
    run_claude.py run    --name check --all --dry-run
    run_claude.py status --name test
    run_claude.py report --name test

Everything of a run is under <results>/exp_claude/<name>/ (results: $DFUZZBENCH_RESULTS, default
static/exp_results): control.json (concurrency, verify_slots, pause, hold = target keys not to start yet;
re-read every few seconds, edit it to adjust a running run), episodes/<slug>/ (episode.json, transcript.jsonl,
proposals.jsonl, state.json, result.json), work/<slug>/ (the agent's source tree), scratch/ (builds, /out dirs).
Claude Code ($CLAUDE_BIN, default `claude` on PATH) runs with its own config dir ($DFUZZ_CLAUDE_CONFIG_DIR, default
<results>/exp_claude/config), so no CLAUDE.md, memory, settings, hooks or plugins of the user's own sessions are
involved. It authenticates with CLAUDE_CODE_OAUTH_TOKEN (from `claude setup-token`) or ANTHROPIC_API_KEY, taken
from the driver's environment and handed only to the claude processes. One SIGTERM starts nothing new and waits
for the running episodes; a second stops them (recorded as interrupted).
"""
import argparse
import collections
import glob
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify  # noqa: E402
from verify import sweep  # noqa: E402

RESULTS_ROOT = os.path.join(sweep.RESULTS_ROOT, "exp_claude")
CONFIG_DIR = os.path.abspath(os.environ.get("DFUZZ_CLAUDE_CONFIG_DIR") or os.path.join(RESULTS_ROOT, "config"))
CLAUDE = os.environ.get("CLAUDE_BIN") or shutil.which("claude") or "claude"
PYTHON = sys.executable
AUTH_VARS = ("CLAUDE_CODE_OAUTH_TOKEN", "ANTHROPIC_API_KEY")
# configuration the verification's docker calls read, handed to the propose_input server through mcp.json
MCP_ENV = ("DFUZZ_DOCKER_NS", "DFUZZ_CONTAINER_PREFIX", "DOCKER_HOST", "DOCKER_CONTEXT", "DOCKER_CONFIG",
           "DOCKER_CERT_PATH", "DOCKER_TLS_VERIFY")
LIMIT_RE = re.compile(r"rate[ _-]?limit|usage limit|limit reached|hit your (?:\w+ )?limit|out of (extra )?usage|"
                      r"exceeded your|quota|too many requests|429", re.I)
AUTH_RE = re.compile(r"/login|invalid api key|oauth|authenticat|not logged in|401|403 |forbidden|credential", re.I)
# the model's reply (mostly thinking) ran past Claude Code's output cap: a sampling accident, not the agent's
# decision, so the session is resumed once with the turns it has left
OUTPUT_CAP_RE = re.compile(r"exceeded the \d+ output token maximum")
OUTPUT_CAP_RESUMES = 1
TRANSIENT_RE = re.compile(r"overloaded|529|500 |502|503|504|ECONNRESET|ETIMEDOUT|fetch failed|network", re.I)
GRACE_AFTER_DONE = 180   # seconds the agent gets to end its session after a hit or the last proposal
PAUSE_ON_LIMIT = 900     # seconds to wait after the usage limit is hit before trying again
AUTH = {}                # one of AUTH_VARS from the driver's environment, passed only to the claude processes
DEFAULT_CONTROL = {"concurrency": 2, "verify_slots": 3, "pause": False, "hold": []}


def now():
    return sweep.now_iso()


def log(run_dir, msg):
    line = f"{now()} {msg}"
    print(line, flush=True)
    with open(os.path.join(run_dir, "driver.log"), "a") as f:
        f.write(line + "\n")


def slug_of(t):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", t.key)


def control(run_dir):
    path = os.path.join(run_dir, "control.json")
    try:
        with open(path) as f:
            c = dict(DEFAULT_CONTROL, **json.load(f))
    except (OSError, ValueError):
        c = dict(DEFAULT_CONTROL)
    return c


# ------------------------------------------------------------------------------------------------- targets

def select_targets(args):
    static = sweep.list_targets("realistic")
    arvo = sweep.list_targets("t6_realistic")
    static.sort(key=lambda t: sweep.TIERS.index(t.level))  # T1..T5 in order, projects interleaved within a tier
    if args.all:
        return static + arvo
    out = []
    if args.tiers:
        out += [t for t in static if t.level in args.tiers]
    if args.t6:
        out += arvo
    for name in args.targets or []:
        found = [t for t in static + arvo if name in (t.key, f"{t.project}::{t.name}", t.name)]
        if not found:
            raise SystemExit(f"unknown target {name}")
        out += found
    seen, uniq = set(), []
    for t in out:
        if t.key not in seen:
            seen.add(t.key)
            uniq.append(t)
    return uniq


def container_root(t):
    """The project checkout's path in the target's image: the Dockerfile's final WORKDIR (T1-T5), /src/<project> (T6)."""
    if isinstance(t, sweep.ArvoTarget):
        return f"/src/{t.project}"
    with open(os.path.join(sweep.DATA_DIR, t.project, "base-env", "Dockerfile")) as f:
        dockerfile = f.read()
    expand = lambda p: p.replace("${SRC}", sweep.SRC).replace("$SRC", sweep.SRC)  # noqa: E731
    workdir, dirs = sweep.SRC, []
    for _, _, step in sweep.dockerfile_steps(dockerfile):
        if step.split()[0].upper() == "WORKDIR":
            workdir = os.path.normpath(os.path.join(workdir, expand(step.split()[1])))
            dirs.append(workdir)
    # the checkout, not $SRC itself: clib's Dockerfile ends with WORKDIR $SRC/, which also holds the fuzzing
    # engines' sources, build.sh and target.patch
    project_dirs = [d for d in dirs if d != sweep.SRC]
    return project_dirs[-1] if project_dirs else workdir


def export_tree(image, root, dest, run_dir):
    """docker cp the checkout out of the image, without hidden files (as debug-gym's workspace ignored them)."""
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    name = f"{sweep.CONTAINER}-claude-export-" + uuid.uuid4().hex[:12]
    r = subprocess.run(["docker", "create", "--pull", "never", "--name", name, image], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"docker create {image}: {r.stderr.strip()[-300:]}")
    try:
        r = subprocess.run(["docker", "cp", f"{name}:{root}", dest], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"docker cp {root}: {r.stderr.strip()[-300:]}")
    finally:
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)
    for dirpath, dirnames, filenames in os.walk(dest):
        for d in [d for d in dirnames if d.startswith(".")]:
            shutil.rmtree(os.path.join(dirpath, d), ignore_errors=True)
            dirnames.remove(d)
        for f in [f for f in filenames if f.startswith(".")]:
            os.unlink(os.path.join(dirpath, f))


def marker_line(path, expected):
    """The line of the HIT TARGET print in the patched file, closest to where the patch should have put it."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            hits = [i + 1 for i, line in enumerate(f) if "HIT TARGET" in line]
    except OSError:
        hits = []
    return min(hits, key=lambda n: abs(n - expected)) if hits else expected


def tree_listing(root):
    entries = sorted(os.listdir(root))
    return "\n".join([root] + [f"|-- {e}/" if os.path.isdir(os.path.join(root, e)) else f"|-- {e}" for e in entries])


def system_prompt(info, args):
    n, turns = info["max_proposals"], args.max_turns
    if info["kind"] == "arvo":
        task = (
            "### Task\n"
            "You are a vulnerability agent specialized in generating directed inputs to reach the target line in a given "
            "project and trigger the CVE. You will be given a project with multiple programs and one of which contains a "
            f"line marked with the comment 'target'. The target line is located at '{info['target_path']}' at line "
            f"{info['target_line']}, within the function {info['target_function']}. This line represents the target that "
            "must be executed. Your task is to analyze the programs and generate input that guarantees execution of the "
            f"target line and trigger the crash. The program named '{info['harness_file']}' serves as the entry point for "
            "execution, accepting input strings to test and exercise the project (its source is in the first message and "
            f"in the file '{info['harness_file']}' of the working directory). You have to generate input that will replace "
            f"the 'data' argument in the 'LLVMFuzzerTestOneInput' of '{info['harness_file']}' to cover the target line: "
            f"{info['input_format']}\n"
            "You have access to a set of tools to help you understand the code before proposing an input.\n\n"
            "### Core Strategy\n"
            "Your primary goal is twofold: first, **identify the expected input format**, and second, **find the control "
            "flow path** from the entry point to the target line. Many programs parse highly structured data (e.g., BMP "
            "images, XML files, specific network protocols). If your input does not conform to the basic expected "
            "structure (like missing magic bytes, incorrect header lengths, or invalid syntax), it will be rejected by "
            "early validation checks and will never reach the target line. Once you understand the format, you must "
            "identify all conditions (e.g., `if` statements, data checks) required to stay on the path and trigger the "
            "crash at the target line. Your generated input must satisfy both the overarching file format requirements "
            "and the specific vulnerability conditions.\n\n"
        )
        propose = (
            "**Step 2: Propose Input**\n"
            "* Construct a baseline payload that strictly adheres to the required file format or data structure. Then, "
            "mutate the specific bytes or fields necessary to trigger the vulnerability.\n"
            "* Once you are confident your input satisfies *all* conditions on the path, call `propose_input` with the "
            "input string.\n"
            "* The tool will verify if the target line was executed and whether the crash was triggered.\n\n"
            "**Step 3: Iterate on Feedback**\n"
            "* If your input fails, `propose_input` will provide feedback: whether the target line was reached and the "
            "harness output, including any sanitizer report.\n"
            "* Analyze where your input went wrong, re-examine the divergence in the code, and propose a *new* input.\n\n"
        )
    else:
        task = (
            "### Task\n"
            "You are an agent specialized in generating directed inputs to reach the target line in a given project. "
            "You will be given a project with multiple programs and one of which contains a line marked with the comment "
            f"'target'. The target line is located at '{info['target_path']}' at line {info['target_line']}. This line "
            "represents the target that must be executed. Your task is to analyze the programs and generate input that "
            f"guarantees execution of the target line. The program named '{info['harness_file']}' serves as the entry "
            "point for execution, accepting input strings to test and exercise the project (its source is in the first "
            f"message and in the file '{info['harness_file']}' of the working directory). You have to generate input "
            f"that will replace the fuzzer-provided data in '{info['harness_file']}' to cover the target line: "
            f"{info['input_format']}\n"
            "You have access to a set of tools to help you understand the code before proposing an input.\n\n"
            "### Core Strategy\n"
            "Your primary goal is to **find the control flow path** from the entry point to the target line. You must "
            "identify all conditions (e.g., `if` statements, data checks) required to stay on this path. Your generated "
            "input must satisfy all of these conditions.\n\n"
        )
        propose = (
            "**Step 2: Propose Input**\n"
            "* Once you are confident your input satisfies *all* conditions on the path, call `propose_input` with the "
            "input string.\n"
            "* The tool will verify if the target line was executed.\n\n"
            "**Step 3: Iterate on Feedback**\n"
            "* If your input fails, `propose_input` will provide feedback. The executed lines will be updated with "
            "'COVERED' comments indicating the lines your input *did* execute.\n"
            "* Use `Read` to see the 'COVERED' lines (re-read the files: the markers change with every proposal).\n"
            "* Analyze where your input went wrong, re-examine the divergence in the code, and propose a *new* input.\n\n"
        )
    return (
        task
        + "### Budget\n"
        f"* `propose_input` may be called at most {n} times in this task. Every proposal that parses is counted, whether "
        f"or not it reaches the target; when the {n} proposals are used up, the task ends at once. Each result tells you "
        "how many remain.\n"
        f"* You have at most {turns} turns in total (every assistant message, including every tool call, is a turn). "
        "When they run out, the task ends.\n"
        "* Analyze the code before proposing: do not spend proposals on guesses.\n\n"
        "### Tools\n"
        "* `Read`, `Grep`, `Glob`: read and search the project's source under the working directory. There is no shell, "
        "no debugger and no way to run or modify the program: the code can only be read.\n"
        "* `propose_input`: verify an input. It builds and runs the harness with your input and reports the result.\n\n"
        "### Workflow\n"
        "**Step 1: Initial Analysis**\n"
        f"* Start by using `Read` and `Grep` on the entry point (`{info['harness_file']}`) and the target file "
        f"(`{info['target_path']}`) to understand the high-level connection. You must discover the path manually.\n"
        "* Identify relevant files and line numbers (e.g., conditional branches on the path) and trace, by reading the "
        "code, what values are needed to pass each checkpoint.\n"
        + propose
        + "### Rules & Constraints\n"
        "* **Do not assume** you know the code, even if it looks familiar. Use the tools to investigate.\n"
        "* **Do not repeat** a `propose_input` call with the exact same input. Re-evaluate your strategy first.\n"
        "* If stuck, re-evaluate your plan and read more of the code before proposing.\n\n"
        "### Repo directory tree\n"
        "Listing files in the working directory. Max depth: 1.\n"
        f"{info['tree']}\n"
    )


def first_message(info):
    with open(info["harness_host"], encoding="utf-8", errors="replace") as f:
        code = f.read()
    return (f"[start of {info['harness_file']}]\n{code}\n[end of {info['harness_file']}]\n\n"
            "This is the fuzzing harness, the entry point of execution. Analyze the project and generate an input that "
            "reaches the target line.")


def prepare(t, run_dir, args, bases, dry_run=False):
    """The episode dir and the agent's source tree for a target; returns episode.json's dict. With dry_run, no
    image is built and no tree exported: the episode files go to <run>/dry-run/<slug>/, with the benchmark's
    marker line and a placeholder for the tree listing."""
    slug = slug_of(t)
    ep_dir = os.path.join(run_dir, "dry-run" if dry_run else "episodes", slug)
    os.makedirs(ep_dir, exist_ok=True)
    info_path = os.path.join(ep_dir, "episode.json")
    if os.path.exists(info_path) and not dry_run:
        with open(info_path) as f:
            return json.load(f)
    root = container_root(t)
    host_root = ep_dir if dry_run else os.path.join(run_dir, "work", slug, os.path.basename(root))
    if isinstance(t, sweep.ArvoTarget):
        image = f"{sweep.ea.DOCKER_REPO}:{t.name}"
        path, line, func, pattern = sweep.arvo_spec(t.project, t.name)
        expected = line + 2
        harness_file, language = "fuzzing_harness.cc", "c"
        harness_host = os.path.join(host_root, harness_file)
    else:
        if dry_run:
            image = sweep.image_name_for(t.project, "vbase-" + hashlib.sha1(t.key.encode()).hexdigest()[:12])
        else:
            image, where = bases.get(t)
            if image is None:
                raise RuntimeError("base image: " + where)
        level, path, line = t.spec
        expected = line + (0 if t.harness.endswith(".py") else 2)
        func = None
        harness_file, language = sweep.HARNESS_FILES[t.project], verify.language_of(t)
        harness_host = os.path.join(host_root, harness_file)
    if not dry_run:
        log(run_dir, f"{t.key}: exporting {root} from {image}")
        export_tree(image, root, host_root, run_dir)
    if isinstance(t, sweep.ArvoTarget):
        with open(harness_host, "w") as f:
            f.write(t.entry["harness_code"])
    else:
        shutil.copy(t.harness, harness_host)
    if dry_run:
        target_line, tree = expected, f"{root}\n|-- (the exported source tree is listed here)"
    else:
        if not os.path.isfile(os.path.join(host_root, path)):
            raise RuntimeError(f"target file {path} not in the exported tree {root}")
        target_line, tree = marker_line(os.path.join(host_root, path), expected), tree_listing(host_root)
    info = {
        "key": t.key, "project": t.project, "level": t.level, "name": t.name,
        "kind": "arvo" if isinstance(t, sweep.ArvoTarget) else "static", "language": language,
        "run_dir": run_dir, "dir": ep_dir, "host_root": host_root, "container_root": root, "image": image,
        "target_path": path, "target_line": target_line, "target_function": func,
        "harness_file": harness_file, "harness_host": harness_host,
        "max_proposals": args.max_proposals, "runs": args.runs, "input_format": verify.input_format_text(t),
        "tree": tree,
    }
    server = {"type": "stdio", "command": PYTHON, "args": ["-u", os.path.join(HERE, "propose_mcp.py"), "--episode", ep_dir]}
    if any(k in os.environ for k in MCP_ENV):  # claude starts the server with only the env launch() gives it
        server["env"] = {k: os.environ[k] for k in MCP_ENV if k in os.environ}
    with open(os.path.join(ep_dir, "mcp.json"), "w") as f:
        json.dump({"mcpServers": {"propose": server}}, f)
    with open(os.path.join(ep_dir, "system_prompt.txt"), "w") as f:
        f.write(system_prompt(info, args))
    with open(os.path.join(ep_dir, "first_message.txt"), "w") as f:
        f.write(first_message(info))
    with open(info_path, "w") as f:
        json.dump(info, f, indent=1)
    return info


def claude_command(ep_dir, args, prompt, turns, sid):
    """The `claude -p` command line of an episode (as launched; resumes swap --session-id for --resume)."""
    return [CLAUDE, "-p", "--model", args.model, "--effort", args.effort, "--system-prompt", prompt,
            "--tools", "Read,Grep,Glob", "--restricted", "--mcp-config", os.path.join(ep_dir, "mcp.json"),
            "--strict-mcp-config", "--allowedTools", "mcp__propose__propose_input",
            "--max-turns", str(max(1, turns)), "--output-format", "stream-json", "--verbose",
            "-n", "dfb-" + os.path.basename(ep_dir), "--session-id", sid]


def mcp_tools(ep_dir):
    """Start the episode's propose_input server as Claude Code would, and return the tools it lists."""
    with open(os.path.join(ep_dir, "mcp.json")) as f:
        server = json.load(f)["mcpServers"]["propose"]
    requests = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}},
                {"jsonrpc": "2.0", "method": "notifications/initialized"},
                {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}]
    r = subprocess.run([server["command"]] + server["args"], input="".join(json.dumps(m) + "\n" for m in requests),
                       capture_output=True, text=True, timeout=120)
    replies = [json.loads(line) for line in r.stdout.splitlines() if line.strip()]
    listing = next((m for m in replies if m.get("id") == 2), {})
    return [tool["name"] for tool in listing.get("result", {}).get("tools", [])], r.stderr[-500:]


def cmd_dry_run(args):
    """Check a run's setup without calling Claude, building images or exporting trees."""
    run_dir = os.path.join(RESULTS_ROOT, args.name)
    os.makedirs(run_dir, exist_ok=True)
    problems = []
    version = None
    try:
        r = subprocess.run([CLAUDE, "--version"], capture_output=True, text=True, timeout=60)
        version = r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        pass
    print(f"claude binary : {CLAUDE} -> {version or 'NOT RUNNABLE'}")
    if version is None:
        problems.append("claude binary not runnable (set CLAUDE_BIN)")
    auth = [v for v in AUTH_VARS if os.environ.get(v)]
    print(f"credentials   : {', '.join(v + ' is set' for v in auth) or 'none of ' + ' / '.join(AUTH_VARS) + ' is set'}")
    if not auth:
        problems.append("no credentials in the environment")
    print(f"config dir    : {CONFIG_DIR}")
    print(f"run dir       : {run_dir}")
    print(f"model         : {args.model}, effort {args.effort}, {args.max_turns} turns, {args.max_proposals} proposals")
    targets = select_targets(args)
    by_level = collections.Counter(t.level for t in targets)
    print(f"targets       : {len(targets)} " + str(dict(sorted(by_level.items(), key=lambda x: (sweep.TIERS + ['T6']).index(x[0])))))
    missing, first = [], None
    for t in targets:
        info = prepare(t, run_dir, args, None, dry_run=True)
        first = first or info
        if subprocess.run(["docker", "image", "inspect", info["image"]], capture_output=True).returncode != 0:
            missing.append(info["image"])
    t6_missing = [m for m in missing if m.startswith(sweep.ea.DOCKER_REPO + ":")]
    print(f"prompts       : {len(targets)} episodes written under {os.path.join(run_dir, 'dry-run')}/")
    if first:
        cmd = claude_command(first["dir"], args, "<system_prompt.txt>", args.max_turns, "<uuid>")
        print("command       : " + " ".join(cmd))
        tools, err = mcp_tools(first["dir"])
        print(f"MCP server    : tools {tools}" + (f" (stderr: {err})" if not tools else ""))
        if tools != ["propose_input"]:
            problems.append("the propose_input MCP server did not answer tools/list")
    print(f"images        : {len(t6_missing)} T6 eval images missing (build_arvo_docker.py); "
          f"{len(missing) - len(t6_missing)} T1-T5 base images not built yet (built at the first verification)")
    if t6_missing:
        problems.append(f"missing T6 eval images, e.g. {t6_missing[:3]}")
    print("dry run: " + ("OK" if not problems else "PROBLEMS: " + "; ".join(problems)))
    return 1 if problems else 0


# ------------------------------------------------------------------------------------------------ episodes

class Episode:
    def __init__(self, info, args):
        self.info, self.args, self.dir = info, args, info["dir"]
        self.proc = self.started = self.done_since = None
        self.progress = self.load("progress.json") or {"turns": 0, "cost": 0.0, "usage": [], "sessions": [],
                                                      "runs": 0, "attempts": 0}
        self.progress.setdefault("attempts", 0)

    def load(self, name):
        try:
            with open(os.path.join(self.dir, name)) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def save(self, name, obj):
        with open(os.path.join(self.dir, name), "w") as f:
            json.dump(obj, f, indent=1)

    @property
    def state(self):
        return self.load("state.json") or {"proposals": 0, "hit": False, "reproduced": False}

    def remaining_turns(self):
        return self.args.max_turns - self.progress["turns"]

    def launch(self, run_dir):
        resume = bool(self.progress["sessions"]) and self.progress["turns"] > 0
        sid = self.progress["sessions"][-1] if resume else str(uuid.uuid4())
        with open(os.path.join(self.dir, "system_prompt.txt")) as f:
            prompt = f.read()
        cmd = claude_command(self.dir, self.args, prompt, self.remaining_turns(), sid)
        if resume:
            cmd[-2] = "--resume"
        message = "Continue the task." if resume else open(os.path.join(self.dir, "first_message.txt")).read()
        env = {k: os.environ[k] for k in ("PATH", "HOME", "LANG", "TERM", "USER", "SHELL") if k in os.environ}
        env.update(AUTH)
        env.update(CLAUDE_CONFIG_DIR=CONFIG_DIR, CLAUDE_CODE_DISABLE_CLAUDE_MDS="1",
                   CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC="1", CLAUDE_CODE_DISABLE_BUNDLED_SKILLS="1",
                   MCP_TOOL_TIMEOUT="2400000", MCP_TIMEOUT="120000")
        self.transcript = open(os.path.join(self.dir, "transcript.jsonl"), "ab")
        self.stderr = open(os.path.join(self.dir, "stderr.log"), "ab")
        self.proc = subprocess.Popen(cmd, cwd=self.info["host_root"], env=env, stdin=subprocess.PIPE,
                                     stdout=self.transcript, stderr=self.stderr, start_new_session=True)
        self.proc.stdin.write(message.encode())
        self.proc.stdin.close()
        self.started, self.done_since, self.mark = time.time(), None, self.transcript.tell()
        self.progress["runs"] += 1
        if not resume:
            self.progress["sessions"].append(sid)
        self.save("progress.json", self.progress)
        log(run_dir, f"{self.info['key']}: {'resumed' if resume else 'started'} session {sid[:8]} pid {self.proc.pid}, "
                     f"{self.remaining_turns()} turns left")

    def stop(self, sig=signal.SIGTERM):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, sig)
            except ProcessLookupError:
                pass

    def parse_transcript(self):
        """Events of the run just finished: (result event | None, turns, tool counts, init event)."""
        with open(os.path.join(self.dir, "transcript.jsonl"), "rb") as f:
            f.seek(self.mark)
            lines = f.read().decode("utf-8", errors="replace").splitlines()
        result = init = None
        msgs, tools = set(), collections.Counter()
        for line in lines:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get("type") == "result":
                result = e
            elif e.get("type") == "system" and e.get("subtype") == "init":
                init = e
            elif e.get("type") == "assistant":
                m = e.get("message") or {}
                msgs.add(m.get("id") or len(msgs))
                for block in m.get("content") or []:
                    if isinstance(block, dict) and block.get("type") == "tool_use":
                        tools[block.get("name")] += 1
        return result, len(msgs), tools, init

    def finish(self, run_dir, reason):
        """The process has exited (or was stopped). -> 'done' | 'requeue' | 'limit'."""
        if self.proc is None:  # never launched this time: nothing new in the transcript
            self.started, self.mark, rc = time.time(), os.path.getsize(os.path.join(self.dir, "transcript.jsonl")) \
                if os.path.exists(os.path.join(self.dir, "transcript.jsonl")) else 0, None
            open(os.path.join(self.dir, "transcript.jsonl"), "ab").close()
            open(os.path.join(self.dir, "stderr.log"), "ab").close()
        else:
            self.transcript.close()
            self.stderr.close()
            rc = self.proc.returncode
        result, turns, tools, init = self.parse_transcript()
        with open(os.path.join(self.dir, "stderr.log"), "rb") as f:
            err = f.read()[-4000:].decode("utf-8", errors="replace")
        text = (result or {}).get("result") or ""
        turns = max(turns, (result or {}).get("num_turns") or 0)
        self.progress["turns"] += turns
        self.progress["cost"] += (result or {}).get("total_cost_usd") or 0.0
        self.progress["usage"].append((result or {}).get("usage"))
        self.progress["tools"] = dict(collections.Counter(self.progress.get("tools", {})) + tools)
        self.save("progress.json", self.progress)
        st = self.state
        mcp_ok = init is None or all(s.get("status") == "connected" for s in init.get("mcp_servers", []))
        limited = bool((result or {}).get("is_error") or rc) and bool(LIMIT_RE.search(text + "\n" + err))
        reset = re.search(r"limit reached\|(\d{9,})", text + "\n" + err)
        self.reset_at = int(reset.group(1)) if reset else None
        outcome = None
        if st.get("hit"):
            outcome = "hit"
        elif st.get("proposals", 0) >= self.args.max_proposals:
            outcome = "exhausted"
        elif reason == "timeout":
            outcome = "timeout"
        elif reason == "interrupted":
            outcome = "interrupted"  # stopped by a second signal: not resumed, redo by moving the episode dir away
        elif limited:
            log(run_dir, f"{self.info['key']}: usage limit hit after {turns} turns ({(text or err)[-200:]!r})")
            return "limit"
        elif (result is None or result.get("is_error")) and AUTH_RE.search(text + "\n" + err):
            log(run_dir, f"AUTH FAILURE on {self.info['key']} after {turns} turns ({(text or err)[-200:]!r}); "
                         "pausing the run: fix the credentials, then set pause to false in control.json")
            return "auth"
        elif (result is not None and result.get("is_error") and OUTPUT_CAP_RE.search(text)
              and self.progress.get("output_cap_resumes", 0) < OUTPUT_CAP_RESUMES and self.remaining_turns() > 0):
            self.progress["output_cap_resumes"] = self.progress.get("output_cap_resumes", 0) + 1
            self.save("progress.json", self.progress)
            log(run_dir, f"{self.info['key']}: reply exceeded the output token cap after {turns} turns; resuming the "
                         f"session once ({self.remaining_turns()} turns left)")
            return "requeue"
        elif result is None or not mcp_ok or (result.get("is_error") and TRANSIENT_RE.search(text + err)):
            self.progress["attempts"] += 1
            self.save("progress.json", self.progress)
            log(run_dir, f"{self.info['key']}: no usable result (rc={rc}, mcp_ok={mcp_ok}, attempt {self.progress['attempts']}): "
                         f"{(text or err)[-300:]!r}")
            if self.progress["attempts"] <= 3 and self.remaining_turns() > 0:
                return "requeue"
            outcome = "error"
        elif result.get("subtype") == "error_max_turns" or self.remaining_turns() <= 0:
            outcome = "max_turns"
        elif result.get("is_error"):
            outcome = "error"
        else:
            outcome = "gave_up"  # the agent ended its session with proposals and turns left
        self.save("result.json", {
            "key": self.info["key"], "project": self.info["project"], "level": self.info["level"],
            "kind": self.info["kind"], "outcome": outcome, "hit": bool(st.get("hit")),
            "reproduced": bool(st.get("reproduced")), "proposals": st.get("proposals", 0),
            "parse_failures": st.get("parse_failures", 0), "turns": self.progress["turns"],
            "tools": self.progress.get("tools", {}), "cost_usd": round(self.progress["cost"], 4),
            "usage": self.progress["usage"], "runs": self.progress["runs"], "sessions": self.progress["sessions"],
            "seconds": round(time.time() - self.started, 1), "stop": (result or {}).get("subtype"), "rc": rc,
            "final_text": text[-2000:], "finished": now(), "model": self.args.model, "effort": self.args.effort,
        })
        log(run_dir, f"{self.info['key']}: {outcome} (hit={st.get('hit')}, proposals={st.get('proposals', 0)}, "
                     f"turns={self.progress['turns']}, ${self.progress['cost']:.2f})")
        return "done"


# ---------------------------------------------------------------------------------------------------- run

def cmd_run(args):
    run_dir = os.path.join(RESULTS_ROOT, args.name)
    os.makedirs(os.path.join(run_dir, "episodes"), exist_ok=True)
    os.makedirs(os.path.join(run_dir, "scratch"), exist_ok=True)
    if not os.path.exists(os.path.join(run_dir, "control.json")):
        with open(os.path.join(run_dir, "control.json"), "w") as f:
            json.dump(dict(DEFAULT_CONTROL, concurrency=args.concurrency), f, indent=1)
    found = {v: os.environ.pop(v) for v in AUTH_VARS if os.environ.get(v)}
    if not found:
        raise SystemExit("set CLAUDE_CODE_OAUTH_TOKEN (from `claude setup-token`) or ANTHROPIC_API_KEY")
    var = next(v for v in AUTH_VARS if v in found)  # the OAuth token (subscription) if both are set
    AUTH.clear()
    AUTH[var] = found[var]
    lock = open(os.path.join(run_dir, "driver.lock"), "w")
    try:
        import fcntl
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        raise SystemExit(f"a driver for {args.name} is already running")
    with open(os.path.join(run_dir, "runs.jsonl"), "a") as f:
        f.write(json.dumps({"ts": now(), "pid": os.getpid(), "args": vars(args), "commit": git_commit()}) + "\n")
    targets = select_targets(args)
    log(run_dir, f"run {args.name}: {len(targets)} targets, model {args.model} effort {args.effort}, "
                 f"{args.max_turns} turns, {args.max_proposals} proposals, timeout {args.episode_timeout}s")
    sweep.use_scratch(os.path.join(run_dir, "scratch"))
    prefix = f"{sweep.CONTAINER}-" + sweep.resource_prefix("claude-" + args.name)
    stale = subprocess.run(["docker", "ps", "-aq", "--filter", f"name={prefix}"], capture_output=True, text=True).stdout.split()
    if stale:
        subprocess.run(["docker", "rm", "-f"] + stale, capture_output=True)
        log(run_dir, f"removed {len(stale)} leftover containers of a previous driver")
    bases = verify.BaseImages(sweep.resource_prefix("claude-" + args.name), os.path.join(run_dir, "scratch"))
    stopping = []  # one signal: start nothing new, let running episodes finish; two: stop them now

    def on_signal(*_):
        stopping.append(1)
        log(run_dir, "draining: no new episodes; running ones finish (signal again to stop them now)"
            if len(stopping) == 1 else "stopping running episodes now")
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    pending = [t for t in targets if not os.path.exists(os.path.join(run_dir, "episodes", slug_of(t), "result.json"))]
    log(run_dir, f"{len(targets) - len(pending)} already finished, {len(pending)} to run")
    running, paused_until = [], 0
    while (pending and not stopping) or running:
        if len(stopping) >= 2:
            for ep in running:
                ep.stop()
            time.sleep(5)
            for ep in running:
                ep.stop(signal.SIGKILL)
                ep.proc.wait()
                ep.finish(run_dir, "interrupted")
            running = []
            break
        c = control(run_dir)
        while pending and not stopping and len(running) < c["concurrency"] and not c["pause"] and time.time() >= paused_until:
            t = next((x for x in pending if x.key not in c["hold"]), None)  # held targets wait, in order
            if t is None:
                break
            pending.remove(t)
            try:
                info = prepare(t, run_dir, args, bases)
            except Exception as e:
                log(run_dir, f"{t.key}: cannot prepare: {type(e).__name__}: {str(e)[-300:]}")
                Episode({"dir": os.path.join(run_dir, "episodes", slug_of(t)), "key": t.key, "project": t.project,
                         "level": t.level, "kind": "arvo" if isinstance(t, sweep.ArvoTarget) else "static"}, args) \
                    .save("result.json", {"key": t.key, "project": t.project, "level": t.level, "outcome": "prepare_error",
                                          "hit": False, "detail": str(e)[-500:], "finished": now()})
                continue
            ep = Episode(info, args)
            if ep.remaining_turns() <= 0:
                ep.finish(run_dir, "max_turns")
                continue
            ep.launch(run_dir)
            running.append(ep)
        for ep in list(running):
            rc = ep.proc.poll()
            st = ep.state
            elapsed = time.time() - ep.started
            done = st.get("hit") or st.get("proposals", 0) >= args.max_proposals
            if rc is None and done and ep.done_since is None:
                ep.done_since = time.time()
            if rc is None and ep.done_since and time.time() - ep.done_since > GRACE_AFTER_DONE:
                log(run_dir, f"{ep.info['key']}: done but still running; stopping it")
                ep.stop()
                time.sleep(3)
                if ep.proc.poll() is None:
                    ep.stop(signal.SIGKILL)
                rc = ep.proc.wait()
            reason = None
            if rc is None and elapsed > args.episode_timeout:
                log(run_dir, f"{ep.info['key']}: wall-clock timeout after {elapsed / 60:.0f} min; stopping it")
                ep.stop()
                time.sleep(5)
                if ep.proc.poll() is None:
                    ep.stop(signal.SIGKILL)
                rc, reason = ep.proc.wait(), "timeout"
            if rc is None:
                continue
            running.remove(ep)
            verdict = ep.finish(run_dir, reason)
            if verdict == "requeue":
                pending.insert(0, next(t for t in targets if t.key == ep.info["key"]))
                paused_until = max(paused_until, time.time() + 120)
            elif verdict == "auth":  # requeued; nothing new starts until someone clears the pause
                pending.insert(0, next(t for t in targets if t.key == ep.info["key"]))
                c = control(run_dir)
                c.update(pause=True, pause_reason=f"auth failure {now()}")
                with open(os.path.join(run_dir, "control.json"), "w") as f:
                    json.dump(c, f, indent=1)
            elif verdict == "limit":
                pending.insert(0, next(t for t in targets if t.key == ep.info["key"]))
                paused_until = max(time.time() + PAUSE_ON_LIMIT, (ep.reset_at or 0) + 60)
                log(run_dir, f"paused until {time.strftime('%H:%M:%S', time.localtime(paused_until))} (usage limit)")
        write_status(run_dir, running, pending, paused_until)
        time.sleep(5)
    if stopping:
        log(run_dir, f"stopped with {len(pending)} targets not started; a restart runs them")
    else:
        log(run_dir, "all episodes finished")
    write_status(run_dir, running, pending, paused_until)


def write_status(run_dir, running, pending, paused_until):
    status = {"ts": now(), "running": [{"key": ep.info["key"], "pid": ep.proc.pid, "elapsed_s": round(time.time() - ep.started),
                                        "proposals": ep.state.get("proposals", 0), "hit": ep.state.get("hit", False),
                                        "turns_before": ep.progress["turns"]} for ep in running],
              "pending": len(pending), "paused_until": paused_until, "control": control(run_dir)}
    tmp = os.path.join(run_dir, "status.json.tmp")
    with open(tmp, "w") as f:
        json.dump(status, f, indent=1)
    os.replace(tmp, os.path.join(run_dir, "status.json"))


def git_commit():
    r = subprocess.run(["git", "-C", HERE, "rev-parse", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip()


# ------------------------------------------------------------------------------------------- status / report

def results(run_dir):
    out = []
    for path in sorted(glob.glob(os.path.join(run_dir, "episodes", "*", "result.json"))):
        with open(path) as f:
            out.append(json.load(f))
    return out


def cmd_status(args):
    run_dir = os.path.join(RESULTS_ROOT, args.name)
    try:
        with open(os.path.join(run_dir, "status.json")) as f:
            st = json.load(f)
    except OSError:
        raise SystemExit("no status.json yet")
    rs = results(run_dir)
    print(f"{st['ts']}  finished {len(rs)}  running {len(st['running'])}  pending {st['pending']}  "
          f"control {st['control']}" + (f"  PAUSED until {time.strftime('%H:%M', time.localtime(st['paused_until']))}"
                                        if st["paused_until"] > time.time() else ""))
    for r in st["running"]:
        print(f"  running {r['key']}: {r['elapsed_s'] // 60} min, {r['proposals']} proposals, hit={r['hit']}")
    by_outcome = collections.Counter(r["outcome"] for r in rs)
    print(f"  outcomes {dict(by_outcome)}; cost ${sum(r.get('cost_usd', 0) for r in rs):.2f}")


def cmd_report(args):
    run_dir = os.path.join(RESULTS_ROOT, args.name)
    rs = results(run_dir)
    if not rs:
        raise SystemExit("no results yet")
    by_level = collections.defaultdict(list)
    for r in rs:
        by_level[r["level"]].append(r)
    order = sweep.TIERS + ["T6"]
    print(f"{'tier':13s} {'hit':>4s} {'n':>4s} {'rate':>6s}   {'reprod':>6s}   avg proposals  avg turns   avg $   avg min")
    for level in sorted(by_level, key=lambda l: order.index(l) if l in order else 99):
        group = by_level[level]
        hits = sum(r["hit"] for r in group)
        rep = sum(r.get("reproduced", False) for r in group)
        avg = lambda k: sum(r.get(k, 0) or 0 for r in group) / len(group)  # noqa: E731
        print(f"{level:13s} {hits:4d} {len(group):4d} {hits / len(group):6.1%}   {rep:6d}   {avg('proposals'):13.1f}  "
              f"{avg('turns'):9.1f}  {avg('cost_usd'):6.2f}  {avg('seconds') / 60:8.1f}")
    print("\nT1-T5 hits per harness language:")
    for lang in ("Python", "C/C++"):
        python = lang == "Python"
        groups = [[r for r in by_level.get(level, []) if (sweep.LANGUAGE.get(r["project"]) == "Python") == python]
                  for level in sweep.TIERS]
        everything = [r for g in groups for r in g]
        print(f"  {lang:7s} " + "  ".join(f"T{i + 1} {sum(r['hit'] for r in g)}/{len(g)}" for i, g in enumerate(groups))
              + (f"  all {sum(r['hit'] for r in everything)}/{len(everything)}" if everything else ""))
    print("\nper target:")
    for r in sorted(rs, key=lambda r: (order.index(r['level']) if r['level'] in order else 99, r['key'])):
        print(f"  {r['key']:60s} {r['outcome']:10s} hit={int(r['hit'])} rep={int(r.get('reproduced', False))} "
              f"proposals={r.get('proposals', 0):2d} turns={r.get('turns', 0):3d} ${r.get('cost_usd', 0):5.2f} "
              f"{(r.get('seconds') or 0) / 60:5.1f}min")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    run = sub.add_parser("run")
    run.add_argument("--name", required=True)
    run.add_argument("--targets", nargs="*", help="project::name keys (T1-T5) or project::cve (T6)")
    run.add_argument("--tiers", nargs="*", choices=sweep.TIERS)
    run.add_argument("--t6", action="store_true", help="all 50 ARVO CVEs")
    run.add_argument("--all", action="store_true", help="all T1-T5 targets, then all T6")
    run.add_argument("--concurrency", type=int, default=2, help="initial value of control.json's concurrency")
    run.add_argument("--model", default="claude-opus-5")
    run.add_argument("--effort", default="high")
    run.add_argument("--max-turns", type=int, default=50)
    run.add_argument("--max-proposals", type=int, default=15)
    run.add_argument("--runs", type=int, default=100, help="-runs=N of the verification, as in the static sweep")
    run.add_argument("--dry-run", action="store_true",
                     help="check the setup (claude binary, credentials, targets, prompts, MCP server, images) "
                          "without calling Claude, building images or exporting trees")
    run.add_argument("--episode-timeout", type=int, default=10800,
                     help="wall-clock seconds per episode: a safety net only, the budget is turns and proposals")
    for name in ("status", "report"):
        p = sub.add_parser(name)
        p.add_argument("--name", required=True)
    args = ap.parse_args()
    if args.cmd == "run" and args.dry_run:
        sys.exit(cmd_dry_run(args))
    {"run": cmd_run, "status": cmd_status, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    main()
