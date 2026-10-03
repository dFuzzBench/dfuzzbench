# Reach agent (agentic input generation)

This directory holds the **reach agent**, an agentic alternative to single-turn input generation:
a ReAct agent that gathers its own context by exploring the target project through tools and
proposes inputs until one executes the target line (T1–T5) or reproduces the CVE (T6). It is a
fork of Microsoft's [debug-gym](https://github.com/microsoft/debug-gym) (MIT, see
[`LICENSE`](LICENSE) and [`DEBUG_GYM.md`](DEBUG_GYM.md)); the dfuzzbench environment, the ARVO
environment and the fuzzing tools are our additions.

| | T1–T5 (reachability targets) | T6 (50 ARVO CVEs) |
| :-- | :-- | :-- |
| Dataset (local, built here) | `data/dfuzzbench/dataset/`, built by `data/dfuzzbench/build_dataset.py` from `data/dfuzzbench/data/`: one row per target line, 143 Python and 176 C/C++ | `data/arvo/dataset/`, built by `data/arvo/construct.py` (added by [`arvo.patch`](arvo.patch)) from `static/data/target-arvo/benchmark.json`: one row per CVE |
| Docker image per target | `local/dfuzzbench:<tag>`, built here (no registry) | public `n132/arvo:<id>-vul`, pulled if missing |
| Configs | `scripts/config_dfuzzbench_gemini_py.yaml` (pdb), `scripts/config_dfuzzbench_gemini_c.yaml` (lldb) | `scripts/config_arvo_gemini.yaml`, added by [`arvo.patch`](arvo.patch) |
| Success | the harness prints `HIT TARGET` (instrumented target line) | the `arvo run` output contains the CVE's sanitizer report and its file:line (reproduction) |

Every episode is capped at **50 LLM calls** (`max_steps`) and **15 input proposals**
(`max_propose_steps`); the model is **Gemini 3.1 Pro** (`llm_name: gemini-3.1-pro`). The agent has
the tools `get_context` (the target's realistic context, which the prompt tells it to call first),
`get_callpaths` (which reports that no call path was found, since the configs set
`provide_call_paths: False`; see Notes), `view_code`, `listdir`, `grep`, `update_plan`,
`propose_input`, and `pdb` (Python) or `lldb` (C/C++). The prompt and loop are in
`debug_gym/agents/reach_agent.py`, the environment in `debug_gym/gym/envs/dfuzz_bench.py` (T6:
`debug_gym/gym/envs/arvo.py`, added by `arvo.patch`), and the reachability check (coverage.py for
Python; recompile with llvm-cov and run in `gcr.io/oss-fuzz-base/base-runner` for C/C++) in
`debug_gym/gym/workspace.py`.

## Setup

Requirements: Linux (the debugger tools need a PTY), Docker, Python 3.12, and network access (GitHub
clones during image builds, apt/pip inside the containers, public Docker images, and the Gemini
API). The datasets are built locally from this repository (see below).

```bash
cd agentic
conda create -n debug-gym python=3.12 && conda activate debug-gym
pip install -e .                                # pip install -e '.[dev]' to also run the tests
export GEMINI_API_KEY=...                       # read by llm.yaml; never stored in a file
export LLM_CONFIG_FILE_PATH=$PWD/llm.yaml
```

debug-gym reads the LLM entry named by `llm_name` from `llm_config_file_path` in the run config,
else from `$LLM_CONFIG_FILE_PATH`, else from `~/.config/debug_gym/llm.yaml`.
[`llm.yaml`](llm.yaml) has the entry `gemini-3.1-pro` of the experiment configs
(`gemini-3.1-pro-preview` through Gemini's OpenAI-compatible endpoint, with thought summaries) and
`gemini-3.5-flash` for smoke tests; `api_key: "${GEMINI_API_KEY}"` takes the key from the
environment. See `DEBUG_GYM.md` for the entry format and other providers.

## Build the datasets

Both datasets are built locally from data in this repository, before the first run; nothing is
downloaded. From `agentic/`:

```bash
python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset   # T1–T5
python data/arvo/construct.py --out-dir data/arvo/dataset                   # T6, once arvo.patch is applied (see T6)
```

- T1–T5: `data/dfuzzbench/dataset/test.jsonl`, one row per T1–T5 target, 319 rows, built from
  `data/dfuzzbench/data/` (see Notes).
- T6: `data/arvo/dataset/test.jsonl`, one row per CVE, 50 rows, built from
  `static/data/target-arvo/benchmark.json` by `data/arvo/construct.py`, which `arvo.patch` adds.

A config names its dataset directory with the env option `dataset_id` (`"data/dfuzzbench/dataset"`;
T6: `"data/arvo/dataset"`), relative to the working directory or to `agentic/`. The environment reads
`<dataset_id>/test.jsonl` (one JSON object per line) with `debug_gym/gym/envs/local_dataset.py`.
Both directories are git-ignored. Rebuild a dataset after changing the data it is built from; a run
stops at start-up if its dataset is missing.

## Build the target images (T1–T5)

Each target runs in its own image `local/dfuzzbench:<tag>`: the project's `base-env`
(`data/dfuzzbench/data/<project>/base-env/`, cloned at a pinned commit) plus the target's
`target.patch`, which prints `HIT TARGET` and exits at the target line.

```bash
bash scripts/build_dfuzzbench_image.sh                                          # the smoke-test target
bash scripts/build_dfuzzbench_image.sh "cmark::src:blocks.c:1035"               # one or more target ids
bash scripts/build_dfuzzbench_image.sh scripts/config_dfuzzbench_gemini_py.yaml # every Python target
bash scripts/build_dfuzzbench_image.sh scripts/config_dfuzzbench_gemini_c.yaml  # every C/C++ target
```

The images build `FROM gcr.io/oss-fuzz-base/base-builder[-python]`, and C/C++ proposals are
checked in `gcr.io/oss-fuzz-base/base-runner`; Docker pulls these public images when they are
missing. `scripts/build_images.sh` builds them from `data/dfuzzbench/base-images/` instead
(slow: it compiles LLVM). Targets of one project share all image layers but the last few. A run
stops at start-up if an image is missing; it never pulls `local/` images.

## Smoke test (Gemini, one target, a few steps)

```bash
python scripts/run.py scripts/config_dfuzzbench.yaml --agent reach_agent
```

`config_dfuzzbench.yaml` is `config_dfuzzbench_gemini_py.yaml` restricted to one bleach target,
with `gemini-3.5-flash` and `max_steps: 8`. At the end, the run lists the target under its status:
`resolved` or `unresolved` means that the pipeline works (the target's `trajectory.json` lists the
steps), and `error` that it failed (see the target's `debug_gym.log`). A C/C++ target exercises
the lldb and recompilation path:

```bash
python scripts/run.py scripts/config_dfuzzbench_gemini_c.yaml --agent reach_agent \
    -p "base.problems=['cmark::src:blocks.c:1035']" base.llm_name=gemini-3.5-flash base.max_steps=6
```

## T1–T5: full runs and metrics

```bash
python scripts/run.py scripts/config_dfuzzbench_gemini_py.yaml --agent reach_agent -n 3   # Python targets
python scripts/run.py scripts/config_dfuzzbench_gemini_c.yaml  --agent reach_agent -n 3   # C/C++ targets
```

Python targets need `pdb` and C/C++ targets `lldb`, so the targets are split into these two configs
(143 and 176 targets; `problems: "all"` would mix them). The metrics average five trials: run
each command five times (every run gets a new random uuid). `-n` sets the number of targets
run in parallel. More workers are faster, but a C/C++ proposal is recompiled with a 60 s timeout,
so on a small machine heavy parallelism can turn proposals into compile failures. A target's tier
is the `level` field of the dataset (`easy`, `medium`, `hard`, `extreme_hard`, `unreachable` =
T1–T5).

The metrics are computed from the `trajectory.json` files (see Outputs), separately for the Python
and the C/C++ runs, as follows:

- *Success rate*: per trial (run uuid), the share of targets with `"success": true`; mean and
  sample standard deviation over the five trials.
- *Steps / episode*: the length of the episode's `log` list. Its first entry is the initial
  observation, so this is the number of LLM calls plus one.
- *Prompt (response) tokens / step*: the sum of `token_usage.prompt` (`token_usage.response`) over
  the `prompt_response_pairs` of all `log` entries, divided by the length of `log`. `prompt` counts
  the whole context sent with a call.

The token and step metrics are the mean and sample standard deviation over all episodes of the
five trials.

## T6: the 50 ARVO CVEs

The ARVO setting changes shared files (the agent prompt, the hexadecimal `propose_input`, `lldb`,
`run.py`), so it ships as a patch. Apply it from the repository root, build the T6 dataset, run,
and revert:

```bash
git apply --directory=agentic agentic/arvo.patch        # or, inside agentic/: patch -p1 < arvo.patch
cd agentic
python data/arvo/construct.py --out-dir data/arvo/dataset   # the T6 dataset (once)
python scripts/run.py scripts/config_arvo_gemini.yaml --agent reach_agent
cd .. && git apply -R --directory=agentic agentic/arvo.patch
```

`git apply arvo.patch` run inside `agentic/` of a git checkout silently skips every file; use one of
the two forms above. `-n` runs CVEs in parallel. One CVE as a smoke test:
`-p "base.problems=['42477334-vul']" base.llm_name=gemini-3.5-flash base.max_steps=5`. The patch
also adds the ARVO environment and `data/arvo/construct.py`, which writes the T6 dataset (see Build
the datasets; the dataset stays in place when the patch is reverted). The reach agent covers
exactly these 50 T6 CVEs (the patched prompt holds their target lines). The 20 post-cutoff cases
(`static/data/post-cutoff/`, CVEs reported after the model's training cutoff) are not part of
the benchmark; they are evaluated with single-turn input generation (see `static/`), not with
this agent.

The T6 metric is the reproduction rate: the share of the 50 CVEs whose `trajectory.json` has
`"success": true` (the reproduction check of the table above).

## Outputs

Runs write to `exps/dfuzzbench/<uuid>/` (T6: `exps/arvo/<uuid>/`, git-ignored), with
`experiment_info.jsonl` (config, git commit and diff) and, per target, `trajectory.json` (every
step's action, observation and token usage, and `success`), `debug_gym.log`, `debug_gym.patch`
(`git diff` of the testbed at the end of the episode), `<target>_status.json` and the C/C++ build
directory `out/`. Re-running with `-p base.uuid=<uuid>` resumes a run and skips the finished
targets (`--force-failed` retries the unresolved ones); `run.py` refuses a uuid that another live
run holds or that holds results of a different `llm_name`.

## Notes

- **Call paths.** `get_callpaths` returns the dataset's static-call-graph paths
  (`realistic_call_paths`) only when the env option `provide_call_paths` is `True`. The option
  defaults to `False`, and the configs set it to `False`; the tool then answers "No valid call
  paths identified from the static call graph."
- **T6 scoring.** The ARVO environment scores CVE reproduction only (sanitizer report and target
  file:line in the `arvo run` output); it does not report reaching the target line separately, so
  no code here computes a T6 reach rate for this agent.
- **Rate limiting.** `debug_gym/llms/openai.py` waits 15 s before every LLM call (60 s with
  `arvo.patch`).
- **Containers** are named `debug_gym_<uuid>`; `DEBUG_GYM_CONTAINER_PREFIX` changes the prefix.
- **Data.** `data/dfuzzbench/data/<project>/` holds what the dataset and images are built from:
  `base-env/` (Dockerfile, build.sh, harness with `EXAMPLE_INPUT` placeholders) and, per target,
  `realistic/<tier>/<file:path:line>/` (`target.patch` plus the realistic-context files, taken from
  `static/data/target-latest/<project>/realistic/`; each target line has exactly one tier here).
  `data/dfuzzbench/build_dataset.py` builds the dataset rows from them (`--out-dir` writes the
  dataset that the configs load, `--json-out` the rows as one JSON file). The `realistic_context`
  field joins a target's files in sorted file-name order, so a build is deterministic. The T6 rows
  are the 50 entries of `static/data/target-arvo/benchmark.json` in file order:
  `data/arvo/construct.py` renames their fields to the columns the ARVO environment reads and sets
  `cve_pattern` to the sanitizer report and the target file:line that a reproduction must print.
- **Tests.** `pip install -e '.[dev]' && pytest` from `agentic/` (tests that need Docker are skipped
  when it is not running).
