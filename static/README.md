# Static retrieved context (single-turn evaluation)

The model receives a **target line** plus a fixed, **retrieved** code context (or, in the no-source controls, no
project source at all) and emits a fuzzing input in **one shot**. The input is spliced into the project's
OSS-Fuzz harness, which is built and run in Docker. A target is **reached** when the run prints `HIT TARGET` (the marker
the target's `target.patch` puts on the target line); a T6 CVE is **reproduced** when the same run also shows the
CVE's sanitizer report (`cve_pattern`, e.g. `AddressSanitizer: heap-buffer-overflow`).

This directory holds the single-turn experiments and the tools that built the benchmark:

| Experiment | What it does |
|------------|--------------|
| [Retrieved context](#retrieved-context) | single-turn input generation with retrieved context: every model with each context (Oracle, BM25, Realistic) on T1–T6 |
| [No-source controls](#no-source-controls) | the same task with no project source in the prompt, then also with every identifier renamed, and random inputs with no model |
| [Knowledge cutoff](#knowledge-cutoff) | assimp and opensc CVEs reported before Gemini 3.1 Pro's knowledge cutoff against cases reported after it |
| [Vibe-coding targets](#vibe-coding-targets) | targets in feature code written for the benchmark, which no model saw in training, next to the original targets of the same applications |
| [PoC similarity](#sweep-driver-and-poc-similarity) | byte similarity of the generated T6 inputs to the reference PoCs |

## Layout

| Path | What it is |
|------|------------|
| `data/target-latest/` | T1–T5: 15 applications (Python, C, C++), targets graded into tiers by first-hit time (`easy`, `medium`, `hard`, `extreme_hard`, `unreachable` = T1–T5) |
| `data/target-arvo/` | T6: 50 ARVO CVEs of 5 applications (`benchmark.json`), scored on reach and reproduce |
| `data/target-vibe/` | the vibe-coding benchmark: 53 targets in newly written features of bleach, html5lib-python and rich |
| `data/post-cutoff/` | 20 assimp and opensc cases reported since January 2025 (Gemini 3.1 Pro's knowledge cutoff; 11 from ARVO, 9 from upstream fix commits), used only by the knowledge-cutoff comparison; **not part of the benchmark**, see [its README](data/post-cutoff/README.md) |
| `data/obfuscated-placeholder/` | the renamed identifiers of the No-source (perturbed) prompts |
| `source/` | the code; the entry points below are run from `static/source/` |
| `results/`, `exp_results/` | what runs write (git-ignored; created on first use): the evaluation scripts and AFLGo write to `results/`, the drivers in `source/exp_*` to `exp_results/` |

Each target ships its contexts as sibling directories, `oracle/`, `bm25/` and `realistic/` (plus the
`callgraph/` used to build `realistic/`), next to a `base-env/` with the harness, `build.sh` and `Dockerfile`.
The retrieved context of every target fits a uniform 100K-token budget, counted with tiktoken's `cl100k_base`; a
model's own tokenizer can count more.

## Setup

```bash
cd static
python3 -m venv .venv && . .venv/bin/activate     # Python 3.10+
pip install -r requirements.txt
pip install -e docker-utils/                      # or: export PYTHONPATH="$PWD/source:$PWD/docker-utils"
bash docker-utils/base-images/all.sh              # once: gcr.io/oss-fuzz-base/base-runner + run_fuzzer_new
cd source
```

- **Docker** must be usable by your user. T1–T5 images build `FROM gcr.io/oss-fuzz-base/base-builder` or
  `base-builder-python` (pulled on first use) and clone the projects from GitHub at pinned commits (helper
  repositories such as zlib for libpng at their latest commit); T6 images are built from the public
  `n132/arvo:<id>-vul` images (several GB each). The Dockerfiles use the `:latest` builder images, so a new pull
  can bring a newer toolchain.
- **The runner image matters.** Verification runs `run_fuzzer_new` in `gcr.io/oss-fuzz-base/base-runner`; the
  stock OSS-Fuzz image lacks it, so build it with `all.sh` first. Locally built images (the runner, the per-run
  harness images, `local/dfuzzbench-arvo:<id>`) are never pulled and nothing is pushed.
- **API keys come from the environment only**:

  | Models (`--llm_model`) | Provider | Environment |
  |---|---|---|
  | `gemini-3.1-pro-preview`, `gemini-3-flash-preview`, `gemini-2.5-pro` | Google | `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) |
  | `gpt-5.2-2025-12-11`, `gpt-4o-2024-11-20` (aliases `gpt-5.2`, `gpt-4o`) | OpenAI | `OPENAI_API_KEY` |
  | `Qwen3-30B-A3B-Thinking-2507-FP8` | a local vLLM server | `VLLM_BASE_URL` (default `http://localhost:8000/v1`), `VLLM_API_KEY` if the server has one |

  Qwen is requested as `Qwen/Qwen3-30B-A3B-Thinking-2507-FP8` through vLLM's OpenAI-compatible API, so serve it
  under that name with a context length that holds the prompts (the retrieved context alone may fill the
  100K-token budget), for example
  `vllm serve Qwen/Qwen3-30B-A3B-Thinking-2507-FP8 --port 8000 --tensor-parallel-size 2 --max-model-len 131072`.
  The Gemini API no longer serves `gemini-2.5-pro`, so requests with that id fail.

## Smoke tests

No API key needed:

```bash
# T1–T5, random input, one target that almost any input reaches (the cmark harness reads its first 8 bytes as
# options): image build, compile, run
python evaluation_project.py --project_dir ../data/target-latest/cmark/realistic \
    --targets easy/src:blocks.c:144 --random --rounds 1 --llm_model random

# T6, the CVE's known PoC (the `poc` field of benchmark.json) instead of a model
python build_arvo_docker.py --cve 42490094      # pulls n132/arvo:42490094-vul (~7 GB), builds local/dfuzzbench-arvo:42490094
python evaluation_arvo.py --cve 42490094 --dry_run

# T6, random input
python evaluation_arvo.py --cve 42490094 --random --rounds 2
```

One Gemini request (the realistic prompt of the same target, to Gemini 3 Flash), then the same build and run;
needs `GEMINI_API_KEY` (an answer without a fenced input is counted as a `pattern_unmatch_err`):

```bash
python evaluation_project.py --project_dir ../data/target-latest/cmark/realistic \
    --targets easy/src:blocks.c:144 --rounds 1 --llm_model gemini-3-flash-preview
```

## Running the experiments

`evaluation_project.py` evaluates one project (`--project_dir <data>/<project>/<setting>`, optionally
`--targets <level>/<target dir> ...`); `evaluation_multi_projects_conc.py` runs it for every project in parallel
(`--projects` to choose). `evaluation_arvo.py` evaluates the CVEs of a benchmark json (`--project`, `--cve` to
choose). Every round waits 15 s before its API request; a T1–T5 round then rebuilds the target's image and runs
the harness on the input for up to 600 s, a T6 round runs it in the CVE's prebuilt image for up to 30 s. Results
go to `results/` (see [Outputs](#outputs)).

**Metric.** `pass_at_k.py` reads the result CSVs. For a target with c hits in n rounds, pass@k =
1 − C(n−c, k) / C(n, k): the chance that at least one of k inputs drawn without replacement from the n reaches
the target (the unbiased estimator of Chen et al.). A tier's pass@k is the mean over its targets. Rounds that end
before an input runs count as misses. For T6 it computes the same for reaching the target and for reproducing
the CVE.

### Retrieved context

Single-turn input generation with retrieved context. Each target gets 50 independent rounds. Run it for each
model of the table above and each `--retrieval_mode` in `oracle`, `bm25`, `realistic`:

```bash
# T1–T5
python evaluation_multi_projects_conc.py --retrieval_mode realistic --rounds 50 --llm_model gemini-3.1-pro-preview
python pass_at_k.py ../results/target-latest/*-realistic-gemini-3.1-pro-preview-*.csv   # one CSV per project, each with its own <time>

# T6: build the 50 images once, then reach and reproduce
python build_arvo_docker.py
python evaluation_arvo.py --retrieval_mode realistic --rounds 50 --llm_model gemini-3.1-pro-preview
python pass_at_k.py ../results/target-arvo/arvo-*-realistic-gemini-3.1-pro-preview-<time>.csv
```

`pass_at_k.py -k 1,5` (default) prints pass@1 and pass@5 per tier for Python, C/C++ and all; for T6 it prints
reached and reproduced. `pass_at_k.py -k 50` gives pass@50 of the same 50 rounds, the single-turn baseline
that the agent experiments are compared with.

### No-source controls

These runs test whether hits depend on the project source. They give the model no project source (No-source),
then also rename every project identifier to a numbered placeholder (No-source (perturbed)), and replace the
model with random strings (Random). Each is scored with pass@k like the retrieved-context runs.

```bash
# No-source: the harness, project name and target location, no project source
python evaluation_multi_projects_conc.py --no_context --rounds 20 --llm_model gemini-3.1-pro-preview
python evaluation_arvo.py --no_context --rounds 20 --llm_model gemini-3.1-pro-preview
# No-source (perturbed): the same prompt with every identifier renamed to a numbered placeholder
python evaluation_multi_projects_conc.py --no_context placeholder --rounds 20 --llm_model gemini-3.1-pro-preview
python evaluation_arvo.py --no_context placeholder --rounds 20 --llm_model gemini-3.1-pro-preview
# Random: a fresh random 10-character alphanumeric string per round, no model
python evaluation_multi_projects_conc.py --random --retrieval_mode realistic --rounds 5
python evaluation_arvo.py --random --rounds 20
```

With `--no_context` or `--random`, `--retrieval_mode` (default `realistic`) only selects the target list.
`obfuscate_no_context.py` regenerates the renamings that `--no_context placeholder` reads from
`data/obfuscated-placeholder/`; it writes them to `results/obfuscated-placeholder/` unless given `--out_dir`, and
`--keep ../data/obfuscated-placeholder` reproduces the shipped files (see its docstring).

### Knowledge cutoff

Compares the 20 assimp and opensc CVEs of T6 (all reported before Gemini 3.1 Pro's January 2025 knowledge
cutoff) with 20 assimp and opensc cases reported since, to see whether the model does as well on cases it cannot
have seen in training. The latter live in `data/post-cutoff/`; they are **not part of dFuzzBench**, and
[`data/post-cutoff/README.md`](data/post-cutoff/README.md) explains how they were selected and how 9 of them (the
`gh-*` cases) get their source images. Both groups use the T6 protocol
(realistic context, 20 rounds, mean pass@5 over the group):

```bash
python exp_cutoff/build_sources.py                                                    # the 9 gh-* source images
python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --compile-timeout 3600
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --rounds 20 --llm_model gemini-3.1-pro-preview
python evaluation_arvo.py --project assimp --rounds 20 --llm_model gemini-3.1-pro-preview   # pre group
python evaluation_arvo.py --project opensc --rounds 20 --llm_model gemini-3.1-pro-preview
python pass_at_k.py -k 5 ../results/post-cutoff/arvo-assimp-realistic-gemini-3.1-pro-preview-<time>.csv   # post, assimp
python pass_at_k.py -k 5 ../results/target-arvo/arvo-assimp-realistic-gemini-3.1-pro-preview-<time>.csv   # pre, assimp
```

Give `pass_at_k.py` both projects' CSVs for the score over assimp and opensc together.

`--benchmark` takes any json of this format; each CVE's contexts and patch are read from
`<json dir>/<project>/<setting>/<cve_id>/`.

### Vibe-coding targets

Targets in feature code written for the benchmark (generated with SWE-agent and GPT-5.2, see
[`data/README.md`](data/README.md)), run next to the original targets of the same three applications, to see
whether the model does as well on code it cannot have seen in training:

```bash
python evaluation_multi_projects_conc.py --vibe --retrieval_mode realistic --rounds 20 --llm_model gemini-3.1-pro-preview
python evaluation_multi_projects_conc.py --projects bleach html5lib-python rich \
    --retrieval_mode realistic --rounds 20 --llm_model gemini-3.1-pro-preview     # the original targets
```

### Sweep driver and PoC similarity

The commands in the [top-level README](../README.md) run the Gemini 3.1 Pro no-source controls (No-source and
No-source (perturbed) on T1–T6, Random on T6) and the knowledge-cutoff comparison through the sweep driver in
[`source/exp_gemini/`](source/exp_gemini/README.md). The driver calls this code for the prompts, the response
parsing, the harness instrumentation and the verification, and adds rate limiting, the retry protocol for
refused or unanswered requests (the commands above count such a round as a miss) and resumable state. It also
runs the PoC-similarity check, which measures how close the generated T6 inputs are, byte for byte, to the
reference PoCs (the `poc` fields of `benchmark.json`). Its README lists the command of each of these experiments.

### Benchmark construction

Not needed to use the benchmark: what these tools build is shipped in `data/`.

- **Target tiers (first-hit time):** [`source/fuzzing_coverage/`](source/fuzzing_coverage/README.md).
- **Contexts (Oracle / BM25 / Realistic):** [`source/retrieve_context/`](source/retrieve_context/README.md).
- **Token count of an application's source:** `python misc/count_token.py <project source dir>` (Gemini
  `count_tokens`, needs `GEMINI_API_KEY`).
- **T6 images:** `build_arvo_docker.py` applies each CVE's `target.patch` to `n132/arvo:<id>-vul`, runs
  `arvo compile` and commits `local/dfuzzbench-arvo:<id>` (`--push` to push it; `DFUZZ_DOCKER_NS` changes the
  `local` namespace).
- **No-source (perturbed) renamings:** `obfuscate_no_context.py`.

### Other experiments

- **Directed greybox fuzzing baseline with AFLGo:** [`source/aflgo/`](source/aflgo/README.md).
- **Reach agent episodes** (a tool-using agent that explores the project and proposes up to 15 inputs per
  target): [`../agentic/`](../agentic/README.md).
- **Claude Code as the agent**, with the same budgets and feedback: [`source/exp_claude/`](source/exp_claude/README.md).
- **Where a coding agent's effort goes** (Claude Code on CyberGym crash-reproduction tasks, every step labelled
  with its activity): [`source/exp_reach/`](source/exp_reach/README.md).

## Outputs

Everything goes under `results/<dataset>/` (`--results_dir` to change; dataset = `target-latest`, `target-vibe`,
`target-arvo` or `post-cutoff`):

| File | Content |
|------|---------|
| `<project>-<setting>-<model>-<time>.csv` | T1–T5: `<level>,<target>,<hits>/<rounds> (<errors>),<API latencies>` per target |
| `<project>-<setting>-<model>-<time>.txt` | the same, one line per target as it finishes |
| `<project>/exec-env-<setting>-<model>-<time>/<level>-<target>/` | per round: the instrumented harness and the fuzzer log (first 256 KB and last 64 KB) |
| `arvo-<project>-<setting>-<model>-<time>.csv` | ARVO: `<project>,<cve>,<hits>/<rounds> (<reproduced>),(<errors>),<API latencies>` |
| `logs/<project>/<cve>-<setting>-<model>-round<n>-<time>.log` | ARVO: the model's answer and the program output of each round |

`<setting>` is the retrieval mode, `no_context` or `no_context_placeholder`; `<model>` is `random` or `dry_run`
for the keyless modes. Rounds that end before an input runs (API error, refusal, no parsable input) are listed
under `<errors>` and count as misses. Runs are independent: several models or settings can run at the same time
on the same targets (each target of a run builds its own image tag, `local/dfuzzbench-<project>:<run id>`, and
`/out` dir under `static/out/runs/`, both removed after every round). `DFUZZ_CONTAINER_PREFIX=<name>` gives every
container a name starting with `<name>`. AFLGo writes to `results/aflgo/{track_a,track_b}/` (see
[`source/aflgo/README.md`](source/aflgo/README.md)); the drivers in `source/exp_*` write to `exp_results/`.

## Notes on the code

- A T1–T5 prompt with source context lists the context files in directory order (`os.listdir` of the run's
  exec-env copy), which depends on the file system. The No-source prompts and the T6 prompts (whose files are
  sorted) do not depend on it.
- That copy also holds the files of the application's `base-env/`. `build_prompt` skips only names containing
  `.patch`, `.log`, `.json`, `.sh`, `fuzzer.` or `fuzzer_instrumented`, so these prompts also show the
  application's `Dockerfile`.
- A Python input is spliced into the harness as a string literal that escapes quotes but not backslashes
  (`llm_fuzz_integration.to_string_literal`), so an input can end the literal early and run code of its own. A
  round counts as a hit whenever `HIT TARGET` appears in the run's output.
