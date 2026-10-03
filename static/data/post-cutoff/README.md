# Post-cutoff case set

**These 20 cases are not part of dFuzzBench.** They exist only for the knowledge-cutoff comparison, which checks
whether the model reaches and reproduces cases reported after its training cutoff as well as cases reported
before it. Gemini 3.1 Pro's knowledge cutoff is January 2025, and every T6 CVE predates it. The comparison
therefore uses two groups:

- **pre:** the 20 assimp and opensc CVEs of T6, in `../target-arvo/benchmark.json`, reported 2019–2021;
- **post:** these 20 assimp and opensc cases, reported since January 2025.

The benchmark itself, including T6, is the same with or without this directory.

## Contents

| Path | What it is |
|---|---|
| `benchmark.json` | the 20 cases (assimp 10, opensc 10), in the format of `../target-arvo/benchmark.json` |
| `<project>/{bm25,oracle,realistic}/<id>/` | the context files and `target.patch` of each case, built as for T6. The three directories are identical for these cases; the evaluation uses `realistic` |

Each `benchmark.json` entry has the usual T6 fields, plus the following:

| Field | Meaning |
|---|---|
| `source` | `arvo` for 11 cases taken from ARVO, where `cve_id` is the OSS-Fuzz issue id. `fix-commit` for 9 cases built from upstream fix commits, where `cve_id` is `gh-<fix commit>` |
| `report_date` | the date the case was reported (see [Selection](#selection)) |
| `cutoff_group` | always `post` |
| `harness_path` | the harness source inside the image |
| `commit_id` | the vulnerable commit. For `fix-commit` cases, this is the parent of `fix_commit` |
| `source_image`, `base_image` | `fix-commit` only: the source image `local/arvo-extra:<id>-vul` and the ARVO image it is built from |
| `fix_commit`, `fix_pr`, `poc_source` | `fix-commit` only: the fix, its upstream pull request (`null` if GitHub lists none), and `<repository>@<fix commit>:<path>` of the crashing input that the fix adds. The `poc` field holds that input |

`realistic_context` and `call_paths` are copied from the records the cases were built from. The evaluation does
not read them; it builds prompts from the context directories. It takes each case's target from `target_line`,
`target_function` and `cve_pattern`, since `static/source/const.py` (`ARVO_TARGETS`) lists only the T6 CVEs.

The target patches follow the T6 convention. Each patch adds two `#include` lines at the top of the target file
and a `fprintf(stderr, "HIT TARGET\n");  // target` print on the line the prompt names, which is
`target_line + 2` in the patched file. The print does not exit, so the crash can still follow. There are two
exceptions:

- `gh-b675b9b4` prints with `dprintf(2, ...)`. Its harness compiles `pkcs15-crypt.c` with
  `#define stderr stdout` and closes stdout, so a `stderr` print would not appear.
- opensc `416295951` and `421520684` have their target in `src/tests/fuzzing/fuzzer_reader.c`. The retriever
  skips `tests/`, so that file was added to their context directories by hand.

## Selection

**Cutoff.** Gemini 3.1 Pro (`gemini-3.1-pro-preview`) has a knowledge cutoff of January 2025, according to
Google's Gemini 3 developer guide. A case counts as post-cutoff when it was reported on or after 2025-01-01, so
the cutoff month itself counts as post.

**Report date.** For ARVO cases, this is the creation date (UTC) of the OSS-Fuzz issue
(`https://issues.oss-fuzz.com/issues/<cve_id>`). For fix-commit cases, it is the creation date of the upstream
pull request that merged the fix. GitHub lists no pull request for the three opensc fix commits, so their date is
the commit's author date.

**1. ARVO (11 cases).** This group has every usable ARVO case of assimp and opensc reported since 2025-01-01. The
search covered the ARVO-Meta v3.0.0 database and the newer `n132/arvo` images on Docker Hub (ids up to 479896934,
pushed February 2026), which gave assimp 4 cases and opensc 8. One opensc case was excluded: 456977594
(`fuzz_pkcs15_encode`, "Null-dereference READ in ubsan_GetStackTrace") crashes inside the sanitizer runtime, so it
has no project target line. As in T6, the target line is the first stack frame inside the project in ARVO's
sanitizer report.

**2. Upstream fix commits (9 cases).** ARVO has no other assimp or opensc case, and OSS-Fuzz testcases cannot be
downloaded without an account. To bring both projects to 10 post cases, the remaining cases come from fix commits
that add the crashing input to the repository. Each case is built as follows:

- The vulnerable version is the fix commit's parent.
- It is built in the OSS-Fuzz environment of the project's newest ARVO image: `n132/arvo:470183468-vul` for
  assimp, `n132/arvo:467161860-vul` for opensc.
- The build uses AddressSanitizer and the case's fuzz target.
- The PoC is the input that the fix commit adds.
- The target line is chosen by the same rule as for the ARVO cases.

The cases come from two sources:

- **opensc (3):** the three fixes of 2026-03-25 whose commit message says the reporter's fuzzer reproducer was
  added to the corpus: `502d0c01` (`asn1.c`, `fuzz_pkcs15_encode`), `73fe9c9b` (`card-rtecp.c`, `fuzz_pkcs11`) and
  `b675b9b4` (`pkcs15-crypt.c`, `fuzz_pkcs15_crypt`).
- **assimp (6 of 18 candidates):** the candidates are memory-safety fixes from June to September 2026 that each add
  a malformed model. In this list order they are:
  `17ec36b2` (MD2), `7555cc90` (MD5), `bb1aac08` (X), `a47827a2` (LWO), `21bea12f` (CSM), `eb21a7c4` (HMP),
  `fe1965b3` (Q3D), `a472362d` (MDC), `ea118b1c` (ASE), `4229c79f` (MD5), `09883e08` (MD5 camera),
  `f8469990` (IQM), `49fa5d05` (MD5), `50cd1ef3` (3MF), `4073e1fd` (NDO), `f3e5f003` (X), `eb84eec5` (CSM) and
  `d3714526` (PK3).

  The list was shuffled with `random.Random(20261001).shuffle`, which tries them in the order `a47827a2`,
  `d3714526`, `21bea12f`, `49fa5d05`, `17ec36b2`, `eb21a7c4`, `4073e1fd`, and so on. The first six that reproduce
  with a sanitizer report in a project frame were kept, at most one per target file. `d3714526` gave no sanitizer
  report, which leaves `a47827a2` (LWO), `21bea12f` (CSM), `49fa5d05` (MD5), `17ec36b2` (MD2), `eb21a7c4` (an HMP
  model that crashes in the MDL loader) and `4073e1fd` (NDO).

**Validation.** A case is kept only if its reference PoC (the `poc` field) reaches the marker and reproduces the
sanitizer report (`cve_pattern`) in the case's evaluation image. The dry run under
[Building the images](#building-the-images) performs this check.

**Per-case details.** `benchmark.json` records each case: `project_name` and `cve_id`, `source`, `report_date`,
`fix_commit` and `fix_pr` (fix-commit cases), `fuzz_target`, `target_line` (`<file>:<line>`), `target_function`,
`cve_pattern` and `poc` (the reference PoC's bytes as a latin-1 string, one character per byte).

## Building the images

Run these commands from `static/source`. You need Docker and network access to Docker Hub and GitHub. The
fix-commit cases need their source images first: `build_sources.py` starts from the two base images above and
fetches the vulnerable commits from GitHub.

```bash
python exp_cutoff/build_sources.py      # local/arvo-extra:<id>-vul for the 9 gh-* cases (see exp_cutoff/README.md)
python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --compile-timeout 3600
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --dry_run    # each case's own PoC
```

`build_arvo_docker.py` builds the evaluation image `local/dfuzzbench-arvo:<id>` of every case. It starts from
`n132/arvo:<id>-vul`, or from `source_image` where the entry has one, applies
`<project>/bm25/<id>/target.patch` and compiles. The dry run feeds each case's own PoC through its image and
reports, per case, whether the target was reached and the sanitizer report reproduced.

## Evaluating

- The model is Gemini 3.1 Pro (`gemini-3.1-pro-preview`) with the realistic context and the single-turn T6
  prompt.
- Each case gets 20 independent requests. Each answer's input runs once in the case's evaluation image.
- **Reached** means the output contains `HIT TARGET`. **Reproduced** means `HIT TARGET` and the case's sanitizer
  report appear in the same run.
- For each case, pass@5 = 1 − C(20−c, 5) / C(20, 5), computed over the 20 rounds. An answer without a parsable
  input counts as a miss. A group's score is the mean over its cases, per project and over both projects.
- The **pre** group is the 10 assimp and 10 opensc CVEs of T6, evaluated in the same way.

The Gemini sweep driver, `static/source/exp_gemini/sweep.py`, runs this protocol. It re-asks when the API
withholds a response, such as a blocked prompt or a filtered answer.
[`exp_gemini/README.md`](../../source/exp_gemini/README.md) describes its options. From `static/source`, with
`GEMINI_API_KEY` set:

```bash
exp_gemini/run.sh --name post-cutoff --models gemini-3.1-pro-preview --setting t6_realistic \
    --benchmark ../data/post-cutoff/benchmark.json --rounds 20                     # post group
exp_gemini/run.sh --name t6-realistic --models gemini-3.1-pro-preview --setting t6_realistic \
    --rounds 20                                                                    # T6, which holds the pre group
python exp_gemini/sweep.py report --name post-cutoff
python exp_gemini/sweep.py report --name t6-realistic --projects assimp opensc
```

To evaluate only the pre group, add `--projects assimp opensc` to the second run. The `reached pass@5` and
`reproduced pass@5` columns of the `assimp`, `opensc` and `all` rows give each group's scores. You can also use
the artifact's plain evaluator, which counts a withheld response as a miss:

```bash
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --rounds 20 --llm_model gemini-3.1-pro-preview
python evaluation_arvo.py --project assimp --rounds 20 --llm_model gemini-3.1-pro-preview    # pre group
python evaluation_arvo.py --project opensc --rounds 20 --llm_model gemini-3.1-pro-preview
python pass_at_k.py -k 5 ../results/post-cutoff/arvo-assimp-realistic-gemini-3.1-pro-preview-<time>.csv
```

Run `pass_at_k.py` once per project and group, on the CSV of that project and group: `results/post-cutoff/` for
post, `results/target-arvo/` for pre. Pass both projects' CSVs together for the score over both projects.
