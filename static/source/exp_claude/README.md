# Claude Code as the agent (`exp_claude/`)

Claude Code as an off-the-shelf coding agent that searches for an input reaching each target line: one
`claude -p` session per target on the T1–T5 targets and the 50 T6 CVEs, with the budgets and feedback of the
reach agent (`agentic/`) but Claude Code's own agent loop and tools. A target counts as hit when any proposal of
its episode reaches the target line; for T6 the report also counts the CVEs whose crash was reproduced.

| | reach agent (`agentic/`, debug-gym) | Claude Code (this directory) |
|---|---|---|
| read code | view / listdir / grep | Read / Grep / Glob, confined to the source tree (`--restricted`) |
| run code | pdb / lldb | nothing: no shell, no debugger, no web |
| retrieval aids | get_context, get_callpaths | none |
| verify | propose_input, 15 per episode | `propose_input` MCP tool, 15 per episode, counted by the tool |
| budget | 50 inference calls | `--max-turns 50` |
| feedback | hit / miss, harness output, COVERED comments | the same, COVERED comments written into the agent's copy of the tree |
| model | Gemini 3.1 Pro | `claude-opus-5`, effort high |

The verdict is the Gemini sweep's verifier (`exp_gemini/sweep.py`: the artifact's harness instrumentation, the
same per-target base images and `-runs=100`, `evaluation_arvo` for T6), so a hit means the same as in the sweeps.

| file | role |
|---|---|
| `run_claude.py` | the driver: exports each target's source tree from its image, writes the prompts, runs the episodes, `status`, `report`, `--dry-run` |
| `propose_mcp.py` | the `propose_input` tool, a stdio MCP server started per episode |
| `verify.py` | verification (compile and run at a low CPU weight, a coverage build for the COVERED markers) |
| `tracer.py` | line coverage of Python harnesses, run inside the build container |
| `start.sh` | starts (or resumes) a run in the background |

## Requirements

- Claude Code (`claude` on PATH, or `CLAUDE_BIN`), in a version that accepts the flags in
  `run_claude.py:claude_command` (`--restricted`, `--effort`, `--strict-mcp-config`, `--max-turns`).
- Credentials in the environment: `CLAUDE_CODE_OAUTH_TOKEN` (from `claude setup-token`, uses a Claude
  subscription) or `ANTHROPIC_API_KEY`. Only the claude processes receive it; the MCP server and Docker do not.
  Claude Code runs with its own config dir (`DFUZZ_CLAUDE_CONFIG_DIR`, default
  `static/exp_results/exp_claude/config`), so the user's own CLAUDE.md, memory, settings, hooks and plugins never
  reach the agent (`CLAUDE_CODE_DISABLE_CLAUDE_MDS=1` as well).
- Python and Docker as for `exp_gemini/`: the T1–T5 base images are built at the first verification of a
  target, and T6 needs the eval images `local/dfuzzbench-arvo:<id>` (`python build_arvo_docker.py`).

## Commands

From `static/source`:

```bash
python exp_claude/run_claude.py run --name claude --all --dry-run   # check the setup; no Claude call, no build
exp_claude/start.sh --name claude --all --concurrency 5               # the whole experiment: T1-T5, then T6
python exp_claude/run_claude.py status --name claude
python exp_claude/run_claude.py report --name claude                  # hit rate per tier and language, per target
```

`--tiers easy medium ...` or `--t6` select a part, `--targets <project>::<target>` single targets (T6:
`<project>::<id>`). `--dry-run` reports the claude binary and its version, whether credentials are set (never
their value), the targets, the command line of an episode, the tools the `propose_input` server lists, and
missing images; it writes each episode's prompts and MCP config under `<run>/dry-run/`.

The defaults are the experiment's settings: `--model claude-opus-5 --effort high --max-turns 50
--max-proposals 15 --runs 100`, one episode per target. `--episode-timeout` sets a wall-clock limit per episode,
a safety net only. An episode whose reply exceeds Claude Code's output token cap is resumed once with its
remaining turns; a proposal whose verification fails for an infrastructure reason is not counted. The
PoC-similarity check takes the T6 proposals of a run with `python exp_gemini/poc_overlap.py --claude-runs claude`.

`report` gives the hit rate (hit targets / targets, one episode per target) per tier, the T1–T5 hits per
harness language (Python, C/C++), and each target's outcome. For T6 it counts the CVEs whose target line was
reached (`hit`) and those whose crash was also reproduced (`reprod`).

## Outputs

Under `static/exp_results/exp_claude/<name>/` (`DFUZZBENCH_RESULTS` moves the root): `control.json` (edit
`concurrency`, `verify_slots`, `pause`, `hold` while it runs; re-read every few seconds), `driver.log`,
`status.json`, `episodes/<slug>/` (`episode.json`, `system_prompt.txt`, `first_message.txt`, `transcript.jsonl`
= Claude Code's stream-json, `proposals.jsonl`, `state.json`, `result.json`), `work/<slug>/` (the agent's tree with
its COVERED markers) and `scratch/`. One SIGTERM lets the running episodes finish and starts no new one; a
restart resumes unfinished episodes with the turns they have left. When the usage limit is hit the run pauses
and retries later.

## Prompts

`system_prompt.txt` is debug-gym's reach-agent prompt without the debugger, `get_context`, `get_callpaths` and
`update_plan` steps, plus a Budget section (the agent is told the 15/50 limits) and the artifact's description of
how the input enters the harness (`const.HARNESS_PROMPTS`). The first user message is the harness source, as in
debug-gym. The target line given is the line of the `HIT TARGET` print in the patched tree (the benchmark's
line + 2 for C/C++, as in the single-turn prompts).
