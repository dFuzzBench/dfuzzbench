# Context retrieval (benchmark construction)

This directory builds the **retrieved code context** that single-turn input generation gives the model for each
target — the `oracle/`, `bm25/` and `realistic/` directories (plus the instrumentation `target.patch`) shipped
under [`../../data/`](../../data). Every context fits a uniform **100K-token** budget (`tiktoken` `cl100k_base`).

> **You do not need to run any of this to use the benchmark.** The constructed contexts are already included under
> `static/data/target-*/<project>/{oracle,bm25,realistic,callgraph}/`. The code is here for transparency and to
> rebuild or extend the benchmark.

## The three static settings

| Setting       | How the context is selected |
|---------------|------------------------------|
| **Oracle**    | the source files executed on the shortest path to the target in the fuzzing campaigns of `../fuzzing_coverage/` |
| **BM25**      | sparse retrieval over the repository; `retrieve_new.py` queries with the target file, the target function and the 20 lines before and after the target line |
| **Realistic** | the files of a static call-graph path from the fuzzed API to the target function, filled up with BM25 results (BM25 alone when no path is found) |

## Files

| File                   | Role |
|------------------------|------|
| `retrieve_new.py`      | Clones each project at its pinned commit, inserts the `HIT TARGET` marker at the target line, writes `target.patch` and retrieves context under the 100K-token budget. Default (`--target_set default`): the BM25 context of the T1–T5 targets, written to `<output_root>/<project>/<level>/<target>/` (the layout of a project's `bm25/` dir; `--output_root` defaults to `static/results/retrieve_context/benchmark_data`, and the clones go to `--base_work_dir`, default `static/results/retrieve_context/work_dir`). `--target_set vibe_feature_only` / `vibe_overall`: the same on top of the vibe-coded feature patch, which it reads from `--vibe_patch_dir <dir>/<project>/patches/{feature,overall}.patch` (not shipped; each shipped vibe `target.patch` already contains the feature). `--arvo <benchmark json>`: for every CVE, `bm25/`, `realistic/` (the json's `call_paths` first, then BM25) and `oracle/` (a copy of `realistic/`) under `<output_root>/<project>/`. |
| `analyze_callgraph.py` | Finds all call paths from the harness entry to the target function in a static call graph: `python analyze_callgraph.py callgraph.json <src node> <dest node>` for a `code2flow` JSON (Python), `--language cpp --src_root <src>` for the text output of LLVM's print-callgraph pass (C/C++). The basis of the T1–T5 Realistic context. |
| `build_bench.py`       | Oracle context of one target: `python build_bench.py --github_link <repo> --commit_hash <sha> --target_filepath <path> --target_line <n> --data_dir <out>` instruments the target, writes `target.patch` and copies the target file; the other files on the path were added by hand. |

T1–T5 Realistic and Oracle contexts were therefore assembled partly by hand (call paths from
`analyze_callgraph.py`, Oracle files beyond the target file); the T6 ones are fully scripted (`--arvo`). Some
Python call paths come from the PyCG call graph instead, and some were completed by hand where the call goes
through a dispatch that the call graphs miss (the parser phases and tokenizer states of html5lib, which bleach
vendors, and rich's `__rich_console__` protocol). The call-graph contexts of bleach and of lark-parser each use
the same files for all their targets, apart from the marker in the target file.

## Dependencies & inputs

`bm25s` and `tiktoken` (in `static/requirements.txt`), `git` and network access to clone the projects, and for
call graphs `code2flow` (Python) or LLVM `opt` (C/C++). None of this is on the evaluation or smoke-test path.

## Output layout (already shipped under `static/data/`)

```
data/target-<latest|vibe>/<project>/
├── base-env/      # OSS-Fuzz fuzz harness + Dockerfile
├── oracle/        # Oracle context     <level>/<target>/
├── bm25/          # BM25 context       <level>/<target>/
├── realistic/     # Realistic context  <level>/<target>/
└── callgraph/     # call-graph artifacts used to build realistic/
data/target-arvo/<project>/{oracle,bm25,realistic}/<cve_id>/   (T6; data/post-cutoff/ has the same layout)
```
