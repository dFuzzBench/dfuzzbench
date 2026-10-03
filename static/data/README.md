# Benchmark data

This directory holds the benchmark data that the experiments read: targets, contexts, patches and harnesses. The
evaluation code in `../source/` reads them as they are. Runs write their outputs to `static/results/` (the
evaluation scripts) or `static/exp_results/` (`exp_gemini`, `exp_claude`, `exp_reach`). Git ignores any
`exec-env-*` working directory that a run leaves in this directory.

| Directory | Contents | Used by |
|---|---|---|
| [`target-latest/`](#t1t5-target-latest) | T1–T5: reachability targets in 15 applications, graded into 5 tiers | the T1–T5 experiments: retrieved context, no-source controls, random inputs, the AFLGo baseline; the original targets of the vibe-coding comparison |
| [`target-arvo/`](#t6-target-arvo) | T6: 50 ARVO CVEs in 5 applications | the T6 experiments: retrieved context, no-source controls, random inputs, the AFLGo baseline; the pre-cutoff group of the knowledge-cutoff comparison; the reference PoCs of the PoC-similarity check |
| [`target-vibe/`](#vibe-coding-benchmark-target-vibe) | the vibe-coding benchmark: 53 targets in newly written features of 3 Python applications | the vibe-coding comparison: targets in code no model saw in training |
| [`obfuscated-placeholder/`](#no-source-perturbed-obfuscated-placeholder) | the identifier renamings of the No-source (perturbed) prompts, for T1–T5 and T6 | the No-source (perturbed) control: no project source and every identifier renamed |
| [`post-cutoff/`](post-cutoff/README.md) | 20 assimp and opensc cases reported since January 2025, Gemini 3.1 Pro's knowledge cutoff. **Not part of the benchmark** | only the knowledge-cutoff comparison (its post-cutoff group) |

dFuzzBench consists of T1–T5 (`target-latest/`) and T6 (`target-arvo/`). `../source/const.py` lists the target
lines, functions and sanitizer patterns:

- `TARGETS` for T1–T5
- `ARVO_TARGETS` for T6
- `VIBE_TARGETS` for the vibe targets

The post-cutoff cases are not in `const.py`. The evaluation takes their target line, function and sanitizer
pattern from their entries in `post-cutoff/benchmark.json`.

## T1–T5: `target-latest/`

```
target-latest/<application>/
  base-env/                      Dockerfile, build.sh and the OSS-Fuzz harness fuzzer.{py,c,cc,cpp}
  oracle/ bm25/ realistic/       one tree per context setting:
    <tier>/<target>/               the retrieved source files and target.patch
  callgraph/<tier>/<target>/     the call-graph context of the targets that have a static call-graph path
```

- **Tiers.** A target's tier comes from its median first-hit time (FHT): how long undirected libFuzzer campaigns
  take to first execute the line (see `../source/fuzzing_coverage/`). The directory names map to the
  tiers as follows:

  | Directory | Tier | Median first-hit time |
  |---|---|---|
  | `easy` | T1 | about 1 minute |
  | `medium` | T2 | about 1 hour |
  | `hard` | T3 | about 1 day |
  | `extreme_hard` | T4 | about 1 week |
  | `unreachable` | T5 | not reached by any campaign, but checked by hand to be reachable |

- **Target names.** A target is named by its file path, with `:` in place of `/`, followed by its line. For
  example, `src:common:clib-package.c:1705` is line 1705 of `src/common/clib-package.c`. Context files are
  named the same way.
- **`target.patch`.** Each round applies this patch to the pinned checkout. It puts the marker on the target line:
  a `HIT TARGET` print followed by `exit(0)`. Python targets use `print`. C and C++ targets use
  `fprintf(stderr, ...)`, and the patch also adds `#include <stdio.h>` and `<stdlib.h>` (`<cstdlib>` in `.cc`
  and `.cpp` files) at the top of the file. A round reaches the target when the run prints `HIT TARGET`.
- **The line the prompt names** is the target's line, plus 2 in C and C++ files for the two `#include` lines. It
  is the marker's line in the patched file, with two exceptions where the target line cannot hold a statement:
  in bleach `bleach:_vendor:html5lib:html5parser.py:1779` the line is an `else:` and the marker opens its block
  (line 1780); in varnish `bin:varnishd:cache:cache_esi_parse.c:536` the line continues a call and the marker
  sits before that call (line 537; the prompt names 538).
- **`base-env/`.** The Dockerfile starts from `gcr.io/oss-fuzz-base/base-builder` or `base-builder-python`. It
  clones the application from GitHub and resets it to the commit the benchmark was built on. `build.sh` and the
  harness are adapted from the application's OSS-Fuzz project; the evaluation splices the model's input into the
  harness.
- **Contexts.** The retrieved context of every target fits a 100K-token budget (tiktoken `cl100k_base`).
  - `oracle/` holds the source files executed on the shortest path to the target in the fuzzing campaigns.
  - `bm25/` holds files retrieved by BM25 from the target-line information.
  - `realistic/` uses a static call-graph path to the target where one exists, and BM25 retrieval otherwise. For
    the targets with a call-graph path, it is identical to `callgraph/`.

The 15 applications, by language:

| Language | Applications |
|---|---|
| Python | lark-parser, bleach, html5lib-python, filesystem_spec, rich |
| C | wamr, md4c, libbpf, varnish, clib, cmark |
| C++ | cpp-httplib, libpng, guetzli, exiv2 |

wamr is listed under C, the language of its sources, but its harness is `fuzzer.cc`. An application need not have targets in every tier.
`TARGETS` in `../source/const.py` maps each application, tier directory and target name to the target's file
path, line and function.

## T6: `target-arvo/`

```
target-arvo/
  benchmark.json                              the 50 CVEs: assimp, openh264, opensc, serenity, wasm3 (10 each)
  <project>/{bm25,oracle,realistic}/<cve_id>/   the context files and target.patch of each CVE
```

Each `benchmark.json` entry has these fields:

| Field | Meaning |
|---|---|
| `cve_id` | the ARVO / OSS-Fuzz issue id |
| `project_name`, `repo_link` | the application and its repository |
| `commit_id` | the vulnerable commit in the ARVO image |
| `fuzz_target` | the fuzz target |
| `harness_code` | the harness source |
| `target_line`, `target_function` | the first stack frame inside the project in the CVE's sanitizer report |
| `cve_pattern` | the sanitizer report that counts as reproduction, e.g. `AddressSanitizer: heap-buffer-overflow` |
| `poc` | the reference PoC: its bytes as a latin-1 string |
| `trigger_command` | ARVO's reproduction command |

`call_paths` and `realistic_context` are construction records; the prompts are built from the context
directories.

`../source/build_arvo_docker.py` builds each evaluation image, `local/dfuzzbench-arvo:<cve_id>`. It applies
`<project>/bm25/<cve_id>/target.patch` to `n132/arvo:<cve_id>-vul` and compiles. A T6 patch only prints
`HIT TARGET` at the target line and does not exit, so the crash can still follow. It adds the same two
`#include` lines, so the prompt names line `target_line + 2` of the patched file. A round **reaches** the target
when the run prints `HIT TARGET`, and **reproduces** the CVE when the same run also shows `cve_pattern`.

Things to know:

- `oracle/` is identical to `realistic/` for every T6 CVE.
- The marker is on line `target_line + 2` of the patched file, the line the prompt names, except for assimp
  42486096, wasm3 42495801 and opensc 42477340 and 42479512, where it sits one line above it.
- The evaluation takes a T6 CVE's target from `ARVO_TARGETS`. Those entries equal the json's `target_line`,
  `target_function` and `cve_pattern`, except that serenity 42492610's file is `AK/Format.h` there and
  `Meta/Lagom/build/../../../AK/Format.h` in the json; the prompt names `AK/Format.h`.
- opensc 42479513's target line is in its harness, `src/tests/fuzzing/fuzz_pkcs15_reader.c`. The prompt shows that
  file as the harness source, so its context directories hold no copy of it.

## Vibe-coding benchmark: `target-vibe/`

```
target-vibe/<application>/
  base-env/                      the same as target-latest/<application>/base-env/
  realistic/<tier>/<target>/     the context and target.patch
```

These targets sit in feature code written for the benchmark, so no model saw it in training; running them next
to the original targets of the same applications shows whether the model does as well on unseen code. Each
`target.patch` adds the new feature code and the marker to the pinned checkout. The features were generated with
SWE-agent and GPT-5.2. Only the realistic context exists for these targets, and they are run with `--vibe` (see
`static/README.md`).

The 53 targets are in bleach, html5lib-python and rich, under the tier directories `easy`, `medium` and
`unreachable` (T1, T2 and T5). `VIBE_TARGETS` in `../source/const.py` lists them in the same form as `TARGETS`.

## No-source (perturbed): `obfuscated-placeholder/`

```
obfuscated-placeholder/
  target-latest/<application>/{prompt.json, harness.<ext>}   the 15 applications, covering all their targets
  target-arvo/<cve_id>/{prompt.json, harness.cc}             the 50 T6 CVEs
```

`--no_context placeholder` reads these files. In the No-source (perturbed) prompt, every project-specific
identifier is replaced by a numbered placeholder: `project1`, `dir1/file2.c`, `prog1`, `func1`, `Type1`,
`CONST1`, `module1` or `var1`. This covers the project name, the target's path and function, the harness file
name and every name in the harness. Language keywords, the standard library, the fuzzing-engine API and literals
are kept.

- `harness.<ext>` is the renamed harness.
- `prompt.json` holds the renamed project, harness name, input example and per-target path and function. It also
  lists every replaced identifier (`renamed`, `renamed_paths`).

The model's input is still run through the original harness and project.

`../source/obfuscate_no_context.py` writes these files. By default it writes to
`static/results/obfuscated-placeholder/`, to compare with the shipped files;
`--out_dir ../data/obfuscated-placeholder` replaces them. Placeholders are numbered per project in the order of
the targets in `TARGETS` (T1–T5) or of the cases given (`--benchmark` takes several jsons), so the numbering
depends on which targets and cases are in the input. `--keep ../data/obfuscated-placeholder` keeps every
placeholder of the shipped files and numbers only names they lack; it reproduces the shipped files, and adding a
target this way leaves the prompts of the other targets unchanged.
