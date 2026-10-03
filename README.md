# dFuzzBench

dFuzzBench measures **reachability**: whether a large language model (LLM) or an agent can construct an input
whose execution covers a given target line of a real program. This repository contains the benchmark, the study
datasets and the code of every experiment. It contains no run results, logs, figures or tables.

The benchmark has two parts:

- **T1–T5**: target lines in 15 OSS-Fuzz applications (Python, C, C++). Each line is graded into one of five tiers
  by its median first-hit time (FHT) in undirected libFuzzer campaigns: T1 ≈ 1 minute, T2 ≈ 1 hour, T3 ≈ 1 day,
  T4 ≈ 1 week. T5 lines were reached by no campaign but were checked by hand to be reachable. A target counts as
  reached when the run prints the `HIT TARGET` marker that the target's patch puts on the line.
- **T6**: 50 CVEs from [ARVO](https://github.com/n132/ARVO) in 5 C/C++ applications. Each CVE is scored twice:
  reached (the vulnerable line runs) and reproduced (the CVE's sanitizer report appears in the same run).

The experiments evaluate single-turn prompting with three static contexts (Oracle, BM25 and Realistic, within a
100K-token budget). They also evaluate two agents: a reach agent built on
[debug-gym](https://github.com/microsoft/debug-gym), and Claude Code. AFLGo serves as a directed-fuzzing
baseline. Controls test for memorization: prompts without source code, the same prompts with renamed
identifiers, a random-input baseline, CVEs reported after the model's knowledge cutoff, and newly written code.

## Benchmark data and study datasets

| Path | Contents | In the benchmark |
|---|---|---|
| [`static/data/target-latest/`](static/data/README.md#t1t5-target-latest) | T1–T5: the target lines of 15 applications, one directory per tier, with the contexts of each target | **yes** |
| [`static/data/target-arvo/`](static/data/README.md#t6-target-arvo) | T6: the 50 ARVO CVEs (`benchmark.json` and the contexts of each CVE) | **yes** |
| [`static/data/post-cutoff/`](static/data/post-cutoff/README.md) | 20 assimp and opensc cases reported since January 2025: the post group of the knowledge-cutoff comparison | no |
| [`static/data/target-vibe/`](static/data/README.md#vibe-coding-benchmark-target-vibe) | the vibe-coding micro-benchmark: 53 targets in newly written features of bleach, html5lib-python and rich | no |
| [`static/data/obfuscated-placeholder/`](static/data/README.md#no-source-perturbed-obfuscated-placeholder) | the renamed identifiers of the No-source (perturbed) prompts, for T1–T5 and T6 | no (prompt data) |

[`static/data/README.md`](static/data/README.md) describes the formats, the tiers and the directory layout. The
reach agent reads the benchmark as two datasets that it builds locally: T1–T5 from its copy of the targets, T6 from
`static/data/target-arvo/benchmark.json` (see [below](#agent-datasets)).

### The post-cutoff case set

The knowledge-cutoff comparison checks whether Gemini 3.1 Pro does worse on CVEs reported after its training
data ends. Its knowledge cutoff is January 2025, and every T6 CVE predates it. The comparison therefore uses two
groups:

- **pre:** the 20 assimp and opensc CVEs of T6 (part of the benchmark);
- **post:** 20 assimp and opensc cases (10 each) reported on or after 2025-01-01. They live in
  `static/data/post-cutoff/`, in the T6 format. They are **not part of dFuzzBench** and are used only for
  this comparison.

**How the 20 post cases were selected.**

- **11 from ARVO.** These are all the usable ARVO cases of the two projects whose OSS-Fuzz issue was created
  on or after 2025-01-01. One opensc case was excluded because it crashes inside the sanitizer runtime.
- **9 from upstream fix commits** (ids `gh-<fix commit>`). These are fixes that add the crashing input to the
  repository:
  - opensc: the three such fixes of 2026-03-25;
  - assimp: the first six of 18 candidate fixes, tried in a seeded random order, that reproduce with a sanitizer
    report in a project frame.

  The vulnerable version is the fix's parent, built in the project's newest ARVO image. The PoC is the input
  that the fix adds.

A case is kept only if its reference PoC reaches the marker and reproduces the sanitizer report in the case's
evaluation image. From `static/source`, this dry run checks every case ([Smoke tests](#smoke-tests) checks one):

```bash
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --dry_run
```

[`static/data/post-cutoff/README.md`](static/data/post-cutoff/README.md) has the full rules. Each case's entry in
`static/data/post-cutoff/benchmark.json` records its id (`cve_id`), `source`, `report_date`, `fix_commit` and
`fix_pr` (fix-commit cases), `fuzz_target`, `target_line`, `target_function`, `cve_pattern` and `poc`.

**How to evaluate them.** Both groups use the T6 protocol:

- Gemini 3.1 Pro with the realistic context;
- 20 single-turn rounds per case;
- per case, pass@5 of reaching and pass@5 of reproducing (see [Metrics](#metrics));
- a group's score is the mean over its cases, for assimp, opensc and both together.

The knowledge-cutoff row of the [experiment table](#experiments) gives the commands.

## Repository layout

```
static/                      single-turn evaluation, benchmark data, and the other experiment drivers
  data/                      the benchmark and the study datasets (table above)
  source/                    run from here: evaluation_project.py and evaluation_multi_projects_conc.py (T1-T5, vibe),
                             evaluation_arvo.py (T6, post-cutoff), build_arvo_docker.py, pass_at_k.py, const.py, ...
    exp_gemini/              sweep driver of the Gemini 3.1 Pro memorization-control and knowledge-cutoff runs,
                             and the PoC-similarity check
    exp_claude/              Claude Code as the agent on T1–T6
    exp_reach/               effort study: Claude Code on 50 CyberGym tasks, every turn labelled by activity
    exp_cutoff/              source images of the 9 fix-commit cases of the post-cutoff set
    aflgo/                   directed greybox fuzzing baseline with AFLGo
    fuzzing_coverage/, retrieve_context/, misc/    benchmark construction: FHT tiers, contexts, token counts
  docker-utils/              Docker helpers (a pip package) and the OSS-Fuzz base images
  requirements.txt
agentic/                     the reach agent (a fork of debug-gym, MIT); its T6 setting ships as arvo.patch
```

Each directory with experiments has its own README with the exact options:
[`static/`](static/README.md), [`exp_gemini/`](static/source/exp_gemini/README.md),
[`exp_claude/`](static/source/exp_claude/README.md), [`exp_reach/`](static/source/exp_reach/README.md),
[`exp_cutoff/`](static/source/exp_cutoff/README.md), [`aflgo/`](static/source/aflgo/README.md) and
[`agentic/`](agentic/README.md).

## Prerequisites

- **Linux and Docker** 20.10 or newer, usable without sudo. The T6 evaluation images are built from the public
  `n132/arvo:<id>-vul` images, which take several GB each.
- **Python 3.10+** for `static/`, and **Python 3.12** for `agentic/`. Use separate environments.
- **Network access** to GitHub (the harness images clone the projects at pinned commits), Docker Hub
  (`n132/arvo:*`, and `ubuntu:20.04` for AFLGo), `gcr.io/oss-fuzz-base` and the model APIs.
- **Credentials.** The experiments read them from environment variables only:

  | Variable | Used for |
  |---|---|
  | `GEMINI_API_KEY` (`static/` also accepts `GOOGLE_API_KEY`) | Gemini models in `static/`, `exp_gemini/`, the labelling of `exp_reach/` and `misc/count_token.py`. `agentic/llm.yaml` reads `GEMINI_API_KEY` |
  | `OPENAI_API_KEY` | GPT-5.2 and GPT-4o (single-turn evaluation) |
  | `VLLM_BASE_URL` (default `http://localhost:8000/v1`), optionally `VLLM_API_KEY` | Qwen3-30B-A3B-Thinking, served by vLLM (recipe in [`static/README.md`](static/README.md#setup)) |
  | `CLAUDE_CODE_OAUTH_TOKEN` or `ANTHROPIC_API_KEY`, with `claude` on `PATH` | the Claude Code runs (`exp_claude/`, `exp_reach/`) |

  The `--llm_model` ids of the single-turn evaluation are `gemini-3.1-pro-preview`, `gemini-3-flash-preview`,
  `gemini-2.5-pro`, `gpt-5.2-2025-12-11`, `gpt-4o-2024-11-20` and `Qwen3-30B-A3B-Thinking-2507-FP8`. The Gemini
  API no longer serves `gemini-2.5-pro`, so requests with that id fail.

Setup of `static/`, from the repository root (Python 3.10+):

```bash
cd static
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt && pip install -e docker-utils/
bash docker-utils/base-images/all.sh     # once: the runner image with run_fuzzer_new
cd source                                 # every static/ command below runs from static/source
```

Setup of `agentic/`, from the repository root, in a separate Python 3.12 environment:

```bash
cd agentic && pip install -e . && export LLM_CONFIG_FILE_PATH=$PWD/llm.yaml
python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset   # the T1–T5 dataset the configs load
```

## Experiments

Commands run from `static/source` unless the row says otherwise. The README linked in the first column documents
every option. `<model>` is one of the single-turn model ids above. `<mode>` is `oracle`, `bm25` or `realistic`.
[Metrics](#metrics) says how each experiment is scored and [Outputs](#outputs) where its results go.

| Experiment | What it does | Command | Needs |
|---|---|---|---|
| **Effort study** ([`exp_reach/`](static/source/exp_reach/README.md)) | Claude Code reproduces 50 known CyberGym crashes, one episode per task; a Gemini 3.1 Pro judge labels every turn with its activity, to measure how much of an agent's effort goes into reaching the vulnerable code | `python exp_reach/batch_cybergym.py --candidates exp_reach/data/corpus_cybergym50.json --out ../exp_results/exp_reach/corpus-rebuilt.json --workers 2`<br>`exp_reach/start_pilot.sh --name cybergym --corpus exp_reach/data/corpus_cybergym50.json --concurrency 5`<br>`python exp_reach/label_turns.py ../exp_results/exp_reach/cybergym --workers 32` | Claude Code credentials; `GEMINI_API_KEY` for the labels; the 50 `n132/arvo:<id>-vul` images (about 200 GB) |
| **Benchmark construction** ([`static/`](static/README.md#benchmark-construction)) | grades target lines into tiers by first-hit time in undirected fuzzing campaigns, builds the Oracle, BM25 and Realistic contexts, counts tokens and builds the T6 evaluation images | `fuzzing_coverage/` (FHT), `retrieve_context/` (contexts), `python misc/count_token.py <source dir>` (project source tokens), `python build_arvo_docker.py` (T6 images) | the FHT campaigns need an extended OSS-Fuzz checkout that is not shipped; the tier bucketing, the T5 reachability checks and parts of the T1–T5 Realistic and Oracle contexts are manual steps without a script; `GEMINI_API_KEY` for token counts |
| **Single-turn evaluation** ([`static/`](static/README.md#retrieved-context)) | single-turn input generation with a static context: 6 LLMs × 3 contexts on T1–T6, 50 rounds per target, one prompt and one input per round | `python evaluation_multi_projects_conc.py --retrieval_mode <mode> --rounds 50 --llm_model <model>`<br>`python build_arvo_docker.py`<br>`python evaluation_arvo.py --retrieval_mode <mode> --rounds 50 --llm_model <model>`<br>`python pass_at_k.py <result CSVs>` | the model's key; OSS-Fuzz base images; the T6 eval images `local/dfuzzbench-arvo:<id>` |
| **Single-turn reference for the agents** | scores the Gemini 3.1 Pro realistic single-turn runs at pass@50: 50 independent attempts per target, as many model calls as an agent episode may make | `python pass_at_k.py -k 50 <the Gemini 3.1 Pro realistic CSVs of the single-turn evaluation>` | the single-turn evaluation's runs |
| **Reach agent** ([`agentic/`](agentic/README.md)) | multi-turn agent built on debug-gym (Gemini 3.1 Pro): reads code, runs a debugger and proposes up to 15 inputs per episode, with hit/miss and coverage feedback; T1–T5 and T6 | from `agentic/`: `python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset`<br>`bash scripts/build_dfuzzbench_image.sh scripts/config_dfuzzbench_gemini_py.yaml` (and `..._c.yaml`)<br>`python scripts/run.py scripts/config_dfuzzbench_gemini_py.yaml --agent reach_agent -n 3` (and `..._c.yaml`), five times<br>T6: from the repository root `git apply --directory=agentic agentic/arvo.patch` (`git apply arvo.patch` inside `agentic/` silently skips every file), then from `agentic/` `python data/arvo/construct.py --out-dir data/arvo/dataset` and `python scripts/run.py scripts/config_arvo_gemini.yaml --agent reach_agent` | `GEMINI_API_KEY`; the `local/dfuzzbench:<tag>` images (built by the script); `n132/arvo` images for T6 |
| **Claude Code agent** ([`exp_claude/`](static/source/exp_claude/README.md)) | Claude Code as an off-the-shelf agent with read-only access to the source and a `propose_input` tool, under the reach agent's budgets and feedback; one episode per target on T1–T5 and T6 | `exp_claude/start.sh --name claude --all --concurrency 5`<br>`python exp_claude/run_claude.py report --name claude` | Claude Code credentials; the T6 eval images |
| **AFLGo** ([`aflgo/`](static/source/aflgo/README.md)) | directed greybox fuzzing baseline with AFLGo, towards each C/C++ T1–T5 target line and each T6 CVE | from `static/`: `bash source/aflgo/build_images.sh`, then `bash source/aflgo/reachability/run_track_a.sh`<br>T6: the build and run steps of [`vulnerability/`](static/source/aflgo/vulnerability/README.md)<br>`MAX_TIME=<seconds>` sets the fuzzing budget per target | no key; locally built AFLGo images; `ubuntu:20.04` and `n132/arvo` images |
| **Realistic reference for the memorization controls** | the Gemini 3.1 Pro realistic single-turn runs, the setting that the no-source, perturbed and random runs are compared with | the Gemini 3.1 Pro realistic runs of the single-turn evaluation, `pass_at_k.py` | as the single-turn evaluation |
| **No-source and No-source (perturbed)** ([`exp_gemini/`](static/source/exp_gemini/README.md#runs)) | memorization control: Gemini 3.1 Pro gets the harness, the project name and the target location but no project source; the perturbed variant also renames every identifier to a numbered placeholder; T1–T6 | `exp_gemini/run.sh --name <name> --models gemini-3.1-pro-preview --setting <setting> --rounds 20 --retry-unanswered --max-requests-per-target 200 ...` with `<name>` / `<setting>` = `no-source` / `no_context`, `perturbed` / `no_context_placeholder`, `t6-no-source` / `t6_no_context` and `t6-perturbed` / `t6_no_context_placeholder`<br>`python exp_gemini/sweep.py report --name <name>` | `GEMINI_API_KEY`; the T6 eval images |
| **Random baseline** | each round's input is a fresh random 10-character alphanumeric string instead of a model answer, to show how many targets an arbitrary input reaches | T1–T5: `python evaluation_multi_projects_conc.py --random --retrieval_mode realistic --rounds 5`, or with the sweep driver `exp_gemini/run.sh --name random --models random --random-input --setting realistic --rounds 5 --rpm 100000 --tpm 1e9 ...`<br>T6: `exp_gemini/run.sh --name t6-random --models random --random-input --setting t6_realistic --rounds 20 --rpm 100000 --tpm 1e9 ...` | no key; the T6 eval images |
| **Knowledge-cutoff comparison** ([`post-cutoff/`](static/data/post-cutoff/README.md), [`exp_cutoff/`](static/source/exp_cutoff/README.md)) | Gemini 3.1 Pro with the realistic context on the assimp and opensc CVEs of T6 (pre) and on 20 cases of the same projects reported after its training cutoff (post; [above](#the-post-cutoff-case-set)) | pre: `exp_gemini/run.sh --name t6-realistic --models gemini-3.1-pro-preview --setting t6_realistic --rounds 20 ...`, then `python exp_gemini/sweep.py report --name t6-realistic --projects assimp opensc`<br>post: `python exp_cutoff/build_sources.py`<br>`python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --compile-timeout 3600`<br>`exp_gemini/run.sh --name post-cutoff --models gemini-3.1-pro-preview --setting t6_realistic --benchmark ../data/post-cutoff/benchmark.json --rounds 20 ...`<br>`python exp_gemini/sweep.py report --name post-cutoff` | `GEMINI_API_KEY`; the T6 and post-cutoff eval images. The 9 `gh-*` source images are built from `n132/arvo:470183468-vul`, `n132/arvo:467161860-vul` and GitHub |
| **PoC similarity** ([`exp_gemini/`](static/source/exp_gemini/README.md#similarity-of-generated-inputs-to-reference-pocs)) | similarity of the generated T6 inputs (Gemini runs and Claude Code proposals) to the reference PoCs, to detect recalled PoCs | `python exp_gemini/poc_overlap.py --runs t6-realistic t6-no-source t6-perturbed t6-random --claude-runs claude` | the outputs of those T6 runs; no key, no Docker |
| **Vibe-coding benchmark** ([`static/`](static/README.md#vibe-coding-targets)) | Gemini 3.1 Pro with the realistic context on 53 targets in newly written features of bleach, html5lib-python and rich (written with SWE-agent and GPT-5.2, so absent from training data), compared with the original targets of the same projects | `python evaluation_multi_projects_conc.py --vibe --retrieval_mode realistic --rounds 20 --llm_model gemini-3.1-pro-preview`<br>original targets: the same command with `--projects bleach html5lib-python rich` in place of `--vibe` | `GEMINI_API_KEY` |

`evaluation_arvo.py`, `build_arvo_docker.py` and `exp_gemini/sweep.py` take `--benchmark <json>` (default: T6,
`static/data/target-arvo/benchmark.json`). Each case's contexts and patch are read from
`<json dir>/<project>/<setting>/<id>/`, so the same commands run T6 and the post-cutoff set.
`exp_cutoff/build_sources.py` reads `static/data/post-cutoff/benchmark.json` by default. The plain
evaluators count a refused or unparsable answer as a miss. The sweep driver `exp_gemini/`, which the
no-source, perturbed, random and knowledge-cutoff rows above use, asks again when the API withholds a response
and, with `--retry-unanswered`, when an answer has no parsable input (see its README).

## Metrics

- **pass@k** (the single-turn experiments, including the random baseline). A target with n rounds, c of which
  reached it, has pass@k = 1 − C(n−c, k) / C(n, k): the unbiased estimate of the chance that k of its inputs,
  drawn without replacement, include one that reaches it. A round that ended without a parsable input counts as
  a miss. A tier's or group's pass@k is the mean over its targets. `python pass_at_k.py -k <k> <CSVs>` (default
  k = 1 and 5) computes it from the result CSVs: per tier for Python, C/C++ and all on T1–T5, and for reaching
  and reproducing on T6 and the post-cutoff set. `exp_gemini/sweep.py report` writes CSVs in the same format
  and a `summary.md` with the mean pass@5.
- **Reach agent.** A target is a success when the harness prints `HIT TARGET` (T1–T5), or when the `arvo run`
  output contains the CVE's sanitizer report and its file:line (T6). On T1–T5 the success rate is the share of
  targets with `"success": true` in one trial (run), as mean and sample standard deviation over the five trials;
  on T6 it is the share of the 50 CVEs reproduced.
- **Claude Code agent.** `run_claude.py report` gives the share of targets reached per tier and harness
  language, and for T6 the CVEs reached (`hit`) and also reproduced (`reprod`).
- **AFLGo.** A target is reached when replaying an AFL crash prints `HIT TARGET`. A T6 CVE counts as reproduced
  (`reproduced_verified`) when a crash shows the CVE's sanitizer report with its stack in the CVE's
  `target_function`. The rates are reached or reproduced targets over all targets scored.
- **Effort study.** Each turn gets one activity label. `label_turns.py` reports the share of turns, output
  tokens and thinking tokens spent on reachability, with bootstrap 95% confidence intervals, and the per-task
  mean and pooled share of thinking tokens per activity.
- **PoC similarity.** Per run: the exact and near-duplicate (byte similarity ≥ 0.9) copies of reference PoCs
  longer than 8 bytes, and the median byte edit similarity of the inputs to their own CVE's PoC next to a
  chance level (their best similarity to the other PoCs of the same fuzz target).

## Smoke tests

The commands below run from `static/source` after the setup above. These need no API key:

```bash
# T1-T5: a random input on a target that almost any input reaches (the cmark harness reads its first 8 bytes as
# options); builds the image, compiles and runs the harness.
python evaluation_project.py --project_dir ../data/target-latest/cmark/realistic \
    --targets easy/src:blocks.c:144 --random --rounds 1 --llm_model random

# T6: the CVE's own PoC (the `poc` field of the benchmark json) instead of a model.
python build_arvo_docker.py --cve 42490094        # pulls n132/arvo:42490094-vul, builds local/dfuzzbench-arvo:42490094
python evaluation_arvo.py --cve 42490094 --dry_run

# The post-cutoff study set: one ARVO case with its own PoC.
python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --cve 389339262
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --cve 389339262 --dry_run

# The sweep driver end to end, with a random input instead of a model.
python exp_gemini/sweep.py run --name test-random --models random --random-input --setting t6_realistic \
    --targets 42490094 --rounds 1 --startup-delay 0 --rpm 100000 --tpm 1e9
python exp_gemini/sweep.py report --name test-random
```

Other keyless checks:

- `python exp_claude/run_claude.py run --name check --targets cmark::src:blocks.c:144 --dry-run` checks the Claude
  Code setup without calling Claude.
- `cd agentic && pip install -e '.[dev]' && pytest` runs the agent's unit tests.
- AFLGo has a one-target smoke run in [`aflgo/README.md`](static/source/aflgo/README.md).

Checks that call Gemini need `GEMINI_API_KEY`:

```bash
# T1-T5: the realistic prompt of the same target, one request to Gemini 3 Flash
python evaluation_project.py --project_dir ../data/target-latest/cmark/realistic \
    --targets easy/src:blocks.c:144 --rounds 1 --llm_model gemini-3-flash-preview
# one post-cutoff case through the realistic T6 prompt and its evaluation image
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --cve 389339262 --rounds 1 \
    --llm_model gemini-3-flash-preview
# one round of the sweep driver with Gemini 3.1 Pro on a T6 CVE
python build_arvo_docker.py --cve 42474468
python exp_gemini/sweep.py run --name test --models gemini-3.1-pro-preview --setting t6_realistic \
    --targets 42474468 --rounds 1 --startup-delay 0
# from agentic/, in its own environment: the reach agent on one target with gemini-3.5-flash (builds the T1-T5
# dataset and one image)
python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset
bash scripts/build_dfuzzbench_image.sh && python scripts/run.py scripts/config_dfuzzbench.yaml --agent reach_agent
```

## Outputs

Every run writes to a git-ignored directory, which is created on first use:

| Directory | Written by |
|---|---|
| `static/results/<dataset>/` | `evaluation_*.py` (`--results_dir` moves it); `<dataset>` is `target-latest`, `target-arvo`, `target-vibe` or `post-cutoff` |
| `static/results/aflgo/` | AFLGo Track A and Track B (`AFLGO_RESULTS_DIR` moves it) |
| `static/exp_results/` | `exp_gemini/`, `exp_claude/`, `exp_reach/` (`DFUZZBENCH_RESULTS` moves it) |
| `static/out/` | the per-run `/out` dirs of the harness builds, removed after each round |
| `agentic/exps/` | reach agent runs |
| `agentic/data/dfuzzbench/dataset/` | the reach agent's T1–T5 dataset (`build_dataset.py --out-dir`) |
| `agentic/data/arvo/dataset/` | the reach agent's T6 dataset (`construct.py --out-dir`, with `arvo.patch` applied) |

## Agent datasets

The reach agent reads its targets from a local dataset directory (`<dir>/test.jsonl`, one row per target), which
the env option `dataset_id` of its configs names relative to `agentic/`. Both datasets are built from the data in
this repository, before the first run, with these commands run from `agentic/`
([details](agentic/README.md#build-the-datasets)):

| Setting | Dataset | Built by |
|---|---|---|
| T1–T5 | `agentic/data/dfuzzbench/dataset/`: the 319 targets, 143 Python and 176 C/C++, from `agentic/data/dfuzzbench/data/` | `python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset` |
| T6 | `agentic/data/arvo/dataset/`: the 50 ARVO CVEs, from `static/data/target-arvo/benchmark.json` | `python data/arvo/construct.py --out-dir data/arvo/dataset`, after applying `agentic/arvo.patch`, which adds the script |

Both directories are git-ignored. A run stops at start-up if its dataset is missing; rebuild a dataset after
changing the data it is built from.

## Docker images

Images built by the artifact use the `local/` namespace. They are never pulled, and nothing is pushed unless you
ask (`--push`):

- `local/dfuzzbench-<project>:<run id>`: per-run T1–T5 harness images, removed after each round;
- `local/dfuzzbench-<project>:vbase-*`: the sweep's base images, kept for later runs;
- `local/dfuzzbench-arvo:<id>`: T6, post-cutoff and effort-study evaluation images;
- `local/arvo-extra:<id>-vul`: post-cutoff fix-commit sources;
- `local/dfuzzbench:<tag>`: the agent's images;
- `local/dfuzzbench-aflgo:*`, `local/arvo-aflgo-base`, `local/dfuzzbench-arvo-aflgo:<id>`: AFLGo.

`DFUZZ_DOCKER_NS` changes the namespace of the T6 and post-cutoff evaluation images, `<ns>/dfuzzbench-arvo:<id>`
(the `exp_reach/` pipeline uses `local/`).
Public images are pulled when missing: `gcr.io/oss-fuzz-base/base-builder[-python]`, `n132/arvo:<id>-vul` and
`ubuntu:20.04`. `gcr.io/oss-fuzz-base/base-runner` must be the one that `all.sh` builds locally.
`DFUZZ_CONTAINER_PREFIX` (`static/`; `exp_reach/` names its containers `reach-*`, `reach_oracle_*` and `mk_*`),
`AFLGO_CONTAINER_PREFIX` (AFLGo) and `DEBUG_GYM_CONTAINER_PREFIX` (agent) prefix container names.
