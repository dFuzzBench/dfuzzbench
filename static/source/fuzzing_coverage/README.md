# Fuzzing coverage / first-hit time (benchmark construction)

This directory measures the **first-hit time (FHT)** used to select target lines and grade them into the
difficulty tiers `T1`–`T5`. The FHT of a line is how long an **undirected** coverage-guided fuzzer (libFuzzer)
takes to first execute it.

> **You do not need to run any of this to use the benchmark.** The tiers are baked into the shipped data: the
> `easy` / `medium` / `hard` / `extreme_hard` / `unreachable` directories (T1–T5) under
> `static/data/target-*/<project>/<setting>/`. The code is here for transparency; no FHT outputs are shipped.

## Pipeline

Run steps 1 and 2 once per campaign, then step 3 over all campaigns, from this directory:

1. `incremental_fuzzing.py --oss_fuzz_dir <oss-fuzz>` runs the undirected libFuzzer campaign of each project
   (`--projects`, default: the 15 benchmark applications; `--seconds` sets the campaign length in seconds) and
   keeps periodic coverage snapshots.
2. `corpus_coverage.py --oss_fuzz_dir <oss-fuzz> --out_dir <campaign dir>` writes the coverage report of every
   snapshot and turns them into `<campaign dir>/<project>_coverage.json` (`{fuzzer: {minute: newly covered
   lines}}`), using `coverage.py` (coverage.py JSON reports for Python, `llvm-cov` exports for C/C++). Give each
   campaign its own `--out_dir` (default `static/results/fht/coverage`).
3. `median.py --reports <campaign dir> [<campaign dir> ...]` gives each line the FHT of every campaign that
   reached it and the median of those, written as `<project>_median.json`, `<project>_median_final.json` (lines
   grouped by median FHT) and `<project>_executed_lines.json` into `--out_dir` (default `static/results/fht`).

Steps 1 and 2 call `infra/helper.py coverage-new` and `infra/helper.py corpus-coverage`, two commands that
upstream OSS-Fuzz's `helper.py` does not have; the modified OSS-Fuzz checkout is not part of this artifact. The
checkout can also be given as `$OSS_FUZZ_DIR`. Bucketing lines into tiers (median FHT around 1 min / 1 h /
1 day / 1 week with a 5% margin for T1–T4, lines no campaign reached for T5) and the manual reachability check of
every T5 target were done by hand; there is no script for them.

## Outputs

The scripts write their outputs under `static/results/fht/` (git-ignored) unless `--out_dir` says otherwise. The
campaign data of steps 1 and 2 (corpus snapshots, coverage reports) stays in the OSS-Fuzz checkout; `coverage.py`
reads the reports from `<oss-fuzz>/build/out/<project>/`.

| File                            | Written by | Contents |
|---------------------------------|------------|----------|
| `<project>_coverage.json`       | step 2 (`coverage/`, one dir per campaign) | `{ fuzzer: { minute: { "newly_covered_lines": { source_file: [lines] }, "newly_covered_lines_count", "percent_covered", "timestamp" } } }` |
| `<project>_executed_lines.json` | step 3 | `{ source_file: [executed line numbers] }` |
| `<project>_median.json`         | step 3 | `{ source_file: { line: { "FHT": [per-campaign minutes], "median": <value> } } }` |
| `<project>_median_final.json`   | step 3 | `{ median: { source_file: [lines] } }`, the lines grouped by their median FHT |

## Dependencies

An OSS-Fuzz checkout with the two helper commands above and its coverage builds, Docker, and
`beautifulsoup4` (in `static/requirements.txt`).
