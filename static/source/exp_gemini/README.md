# Gemini sweep driver (`exp_gemini/`)

A driver for single-turn input generation with Gemini 3.1 Pro: it asks the model for an input that reaches a
target line, many rounds per target, and verifies every input. It runs the experiments listed under
[Runs](#runs) (prompts without the project's source, as is or with every identifier renamed; a random-input
baseline; T6 with retrieved source context; the knowledge-cutoff comparison) and the
[similarity check of generated inputs against reference PoCs](#similarity-of-generated-inputs-to-reference-pocs).
It calls the artifact's own code for everything that defines a result: the prompt
(`evaluation_project.preprocess_dir` + `llm_fuzz_integration.build_prompt` / `build_no_context_prompt`,
`evaluation_arvo.build_arvo_prompt`), response parsing (`process_response`), harness instrumentation, the Docker
build/compile/run of `docker_utils`, and `evaluation_arvo.run_arvo_container` for T6. Around that it adds rate
limiting, retries, resumable state, and one verification per unique (target, input), shared by all rounds and
runs. The docstring of `sweep.py` has the details.

| file | role |
|---|---|
| `sweep.py` | the driver: `run`, `status`, `report` |
| `run.sh` | starts (or resumes) `sweep.py run` in the background |
| `jobqueue.py` | runs one model's jobs back to back (optional) |
| `poc_overlap.py` | the PoC-similarity check: byte similarity of the generated T6 inputs to the reference PoCs |

## Requirements

- Python 3.10 with `static/requirements.txt` (`numpy` is for `poc_overlap.py`, `google-genai` only for the optional
  `--include-thoughts`).
- Docker. T1–T5 builds start from `gcr.io/oss-fuzz-base/base-builder*`, and verification runs in
  `gcr.io/oss-fuzz-base/base-runner` with the artifact's helpers (`bash docker-utils/base-images/all.sh`, see
  `static/README.md`). T6 needs the eval images `local/dfuzzbench-arvo:<id>` (`python build_arvo_docker.py`).
  The knowledge-cutoff set needs its own eval images (see `static/data/post-cutoff/README.md` and
  `exp_cutoff/README.md`).
- `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) in the environment. The driver drops it from its environment at
  start-up, so no child process sees it. `--random-input` and `--fake-response` runs need no key.

Run everything from `static/source`. `run.sh` uses `$PYTHON` (default `python3`).

## Outputs

Everything goes to `static/exp_results/<name>/` (git-ignored; `DFUZZBENCH_RESULTS` moves the root):

| file | content |
|---|---|
| `responses.jsonl` | one line per answered round: response text, token usage, latency, parse outcome, input hash |
| `attempts.jsonl` | responses without a parsable input that were asked again (`--retry-unanswered`) |
| `verifications.jsonl` | one line per unique (target, input): hit, reproduced (T6), status |
| `api_events.jsonl`, `alerts.log`, `status.jsonl`, `driver.log` | API failures and refusals, anything that needs a look, one status line per minute |
| `harnesses/`, `inputs/`, `logs/` | the instrumented harness (T1–T5) or input file (T6) and the truncated output of every verification |
| `csv/`, `summary.md` | written by `report`: per-project CSVs in the artifact's format and a summary with mean pass@5 |
| `prompts/` | the copies of the T1–T5 target dirs that the prompts are read from (the artifact's `preprocess_dir`) |

`report` uses the verdicts of every run under the results root, since a verdict depends only on the target and
the input. Its reproduce columns leave out the CVEs listed in `POC_UNREPRODUCIBLE` (`sweep.py`), those whose
own PoC reaches the target but cannot reproduce the crash in the CVE's eval image; `summary.md` names them. The
reach columns count every CVE. The per-target base images `local/dfuzzbench-<project>:vbase-*` stay for later runs.
A T1–T5 prompt with source context (realistic, oracle, bm25) lists the context files in directory order, which
depends on the file system; the prompts without source and all T6 prompts do not depend on it.
`python exp_gemini/sweep.py status --name <name>` shows progress; a run resumes where it stopped when started
again with the same arguments. One SIGTERM drains a run, a second stops it.

## Runs

Every run below uses Gemini 3.1 Pro and 20 rounds per target unless noted. Start each with
`exp_gemini/run.sh --name <name> <options>` and summarize it with `python exp_gemini/sweep.py report --name <name>`.

| experiment | `--name` | options |
|---|---|---|
| T6 with retrieved source context; each input checked for reaching the target line and reproducing the crash | `t6-realistic` | `--models gemini-3.1-pro-preview --setting t6_realistic --rounds 20 --verify-workers 4 --min-verify-workers 2` |
| T1–T5 without the project's source | `no-source` | `--models gemini-3.1-pro-preview --setting no_context --rounds 20 --max-inflight 128 --verify-workers 6 --min-verify-workers 4 --retry-unanswered --max-requests-per-target 200` |
| T6 without the project's source | `t6-no-source` | `--models gemini-3.1-pro-preview --setting t6_no_context --rounds 20 --verify-workers 4 --min-verify-workers 2 --retry-unanswered --max-requests-per-target 200` |
| T1–T5 without the project's source, identifiers renamed | `perturbed` | `--models gemini-3.1-pro-preview --setting no_context_placeholder --rounds 20 --max-inflight 128 --verify-workers 6 --min-verify-workers 4 --retry-unanswered --max-requests-per-target 200` |
| T6 without the project's source, identifiers renamed | `t6-perturbed` | `--models gemini-3.1-pro-preview --setting t6_no_context_placeholder --rounds 20 --verify-workers 4 --min-verify-workers 2 --retry-unanswered --max-requests-per-target 200` |
| T1–T5 random-input baseline (no API) | `random` | `--models random --random-input --setting realistic --rounds 5 --rpm 100000 --tpm 1e9 --max-inflight 8 --verify-workers 4 --min-verify-workers 2 --max-verify-workers 6` |
| T6 random-input baseline (no API) | `t6-random` | `--models random --random-input --setting t6_realistic --rounds 20 --rpm 100000 --tpm 1e9 --max-inflight 8 --verify-workers 4 --min-verify-workers 2 --max-verify-workers 6` |
| knowledge cutoff: the T6 protocol on the CVEs reported after the model's training cutoff | `post-cutoff` | `--models gemini-3.1-pro-preview --setting t6_realistic --benchmark ../data/post-cutoff/benchmark.json --rounds 20 --verify-workers 4 --min-verify-workers 2` |

Notes on these runs:

- **Prompt settings.** `t6_realistic` is the T6 prompt of `evaluation_arvo.py` with its retrieved source context.
  `no_context` / `t6_no_context` give the target's file, line and function and the harness, but none of the
  project's source (the T1–T5 prompt also names the project). `no_context_placeholder` /
  `t6_no_context_placeholder` are the same prompt with every identifier (the project, the target's file path and
  function, the harness file and all names in the harness) replaced by a numbered placeholder such as `func1` or
  `var2`; the renamings are in `static/data/obfuscated-placeholder/`.
- **Retry protocol.** With `--retry-unanswered`, a response without an input the artifact can parse (a refusal
  in text, an empty answer, a recitation stop) is kept in `attempts.jsonl` and its round is asked again, up to
  `--max-requests-per-target` responses per target. Responses the API withholds (blocked prompt, filtered answer)
  are always asked again, until 40 refusals without any answer or 200 refusals in total. An input of the wrong
  shape counts as a miss. `--seed-from <run> ...` starts a run from the responses of earlier runs of the same
  prompts: their answered rounds are kept, their unanswered ones are asked again.
- **Random.** `--random-input` answers every round with a fresh 10-character alphanumeric string, sent through
  the same parser and input channel as a model answer; no model is called. The strings are made as in the
  artifact's own `--random` mode (`evaluation_multi_projects_conc.py --random`, see `static/README.md`).
- **Metric.** A target's pass@5 is the unbiased estimate `1 - C(n-c, 5) / C(n, 5)` over its n resolved rounds,
  c of them hits (with fewer than 5 rounds: whether any round hit). `summary.md` of a T1–T5 run has its mean
  over the targets per tier and harness language (Python, C/C++). For T6 the report has, per project and for
  `all`, the mean `reached pass@5` (the input reaches the target line) and `reproduced pass@5` (it also
  reproduces the CVE's crash).
- **Knowledge-cutoff comparison.** Compares CVEs reported before Gemini 3.1 Pro's training cutoff with CVEs of
  the same projects reported after it, under the same protocol. The before-cutoff group is the assimp and opensc
  CVEs of T6, so it comes from the `t6-realistic` run:
  `python exp_gemini/sweep.py report --name t6-realistic --projects assimp opensc`. The after-cutoff group is
  the 20 cases of `static/data/post-cutoff/` (not part of the benchmark; see its README), evaluated by the
  `post-cutoff` run with exactly the T6 protocol. Compare the `reached pass@5` and `reproduced pass@5` columns of
  the `assimp`, `opensc` and `all` rows of the two reports: per CVE the unbiased pass@5 over its 20 rounds,
  averaged over the group's CVEs.
- **Retrieved source context on T1–T5.** These runs use the artifact's evaluation scripts (`static/README.md`).
  `--setting realistic` (or `oracle`, `bm25`) runs the same prompt and verdict through this driver.
- `jobqueue.py` chains one model's runs, e.g.
  `python exp_gemini/jobqueue.py --model gemini-3.1-pro-preview --after t6-realistic --job t6-no-source:t6_no_context --job no-source:no_context --extra "--retry-unanswered --max-requests-per-target 200"`.
  It starts each job through `run.sh` with the per-kind options of the table.

## Similarity of generated inputs to reference PoCs

Measures how close the generated T6 inputs are to the CVEs' public reference PoCs, to tell inputs that copy a
known PoC from inputs derived from the code:

```bash
python exp_gemini/poc_overlap.py --runs t6-realistic t6-no-source t6-perturbed t6-random --claude-runs claude
```

`--claude-runs` takes `exp_claude` run names (or run directories) and adds every proposal of their T6 episodes.
The reference PoCs are the `poc` fields of `static/data/target-arvo/benchmark.json` (`--benchmark` takes several
jsons and pools their cases); no Docker is needed. Writes `static/exp_results/poc_overlap.{md,csv}`: per run, the
exact and near-duplicate (similarity >= 0.9) reproductions of PoCs longer than 8 bytes, and the median byte edit
similarity of the inputs to their own CVE's PoC next to a chance level (their best similarity to the other PoCs
of the same fuzz target). A `poc` field holds the PoC's bytes as a latin-1 string, one character per byte, so a
PoC's size in bytes is the length of its field.

## Smoke test

One round of Gemini 3.1 Pro on one T6 CVE, in the foreground (needs `local/dfuzzbench-arvo:42474468`), then its
report:

```bash
python exp_gemini/sweep.py run --name test --models gemini-3.1-pro-preview --setting t6_realistic \
    --targets 42474468 --rounds 1 --startup-delay 0
python exp_gemini/sweep.py report --name test
```

Without an API key, `--models random --random-input` in place of `--models gemini-3.1-pro-preview` exercises the
same pipeline.
