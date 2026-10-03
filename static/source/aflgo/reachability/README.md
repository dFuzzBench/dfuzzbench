# Track A — AFLGo on the C/C++ T1–T5 targets

For every C/C++ target, `aflgo_baseline.py` starts one container from
`local/dfuzzbench-aflgo:<project>`, copies in the target's `target.patch` (with
`exit(0)` turned into `abort()`) and `fuzz_target.sh`. The runner applies the
patch, builds the harness twice with AFLGo (stage 1: call graph, CFGs and
distances to the target line; stage 2: the distance-instrumented binary) and
runs `afl-fuzz -z exp -c <75% of MAX_TIME>` until a crash replay prints
`HIT TARGET` (`status=hit`) or `MAX_TIME` runs out.

Targets: every entry of `TARGETS` in `source/const.py` for the 10 C/C++
projects, in the tiers `easy`, `medium`, `hard`, `extreme_hard` and
`unreachable` (T1–T5); AFLGo cannot run the Python targets. Patches:
`data/target-latest/<project>/{bm25,realistic,oracle}/<tier>/<key>/target.patch`.

All commands run from `static/`. Requirements: Docker, any `python3` (stdlib
only), `tmux` for the full run. No API keys.

## 1. Build the images (once)
```bash
bash source/aflgo/build_images.sh          # local/dfuzzbench-aflgo:base + the 10 project images
bash source/aflgo/build_images.sh cmark    # base + a single project
```
The builds download Ubuntu 20.04, LLVM 11 (apt.llvm.org), AFLGo at commit
`fa125da` and every project at its pinned commit; each image is about 4 GB.
Runs need network access too: every libbpf target clones elfutils from
sourceware.org while it builds.

## 2. Run
```bash
# smoke: one cmark target in the foreground, 10-minute fuzzing cap
NO_TMUX=1 PARALLEL=1 MAX_TIME=600 EXTRA_ARGS="--project cmark --target src:blocks.c:644" \
  bash source/aflgo/reachability/run_track_a.sh

# all targets: tmux sessions aflgoA (campaign) and aflgoAmon (hourly monitor)
PARALLEL=$(nproc) bash source/aflgo/reachability/run_track_a.sh

# status at any time
python3 source/aflgo/reachability/track_a_status.py

# the monitor never exits; stop it once aflgoA has finished (before a new launch)
tmux kill-session -t '=aflgoAmon'
```
`EXTRA_ARGS` scopes a run (`--project`, `--difficulty`, `--target`), e.g. to
re-run the targets left after an interruption; every finished target is already
in the CSV.

## Budget and sizing
- `MAX_TIME` sets the fuzzing budget per target in seconds (default 518400);
  AFLGo switches to exploitation after 75% of it. `aflgo_baseline.py` run
  directly (without the launcher) takes it as `--max_time` (default 1814400).
- A target stops at its first verified hit, so wall clock is roughly
  Σ(build + fuzz time) / `PARALLEL`. The default `PARALLEL` starts every target
  at once, each container with its own LTO build, which oversubscribes the CPU
  and RAM of a typical host. Use `PARALLEL ≈ nproc` instead (targets then run
  in waves, each with its full `MAX_TIME`); on a small host, lower `PARALLEL`
  further and set `MEM_CAP_MB`.
- If the C++ `distance.bin` crashes with an illegal instruction on the host CPU,
  `fuzz_target.sh` retries with AFLGo's pure-Python distance calculator.

## Outputs (`results/aflgo/track_a/`, or `$AFLGO_RESULTS_DIR/track_a/`)
- `aflgo-baseline-<ts>.csv`: one row per target, written as each finishes:
  `project,target_key,difficulty,filepath,line,function,round,tte_seconds,build_time_seconds,total_iters,status`.
  A target is reached iff `status=hit`; the per-tier reach rate is hits / targets.
  Other statuses: `timeout`, `distance_failed`, `build_failed*`, `patch_failed*`,
  `afl_died`, `no_patch`, `container_failed`, `error`.
- `crashes/<project>/<key>_r<round>.tar.gz`: the AFL crash inputs of a target.
- `run-<date>-trackA.log`, `aflgo-baseline-progress.csv` (hourly monitor rows).

## Knobs (env)
| Var | Meaning |
|---|---|
| `MAX_TIME` | per-target fuzzing cap in seconds (default 518400) |
| `PARALLEL` | concurrent containers (default: every target at once) |
| `MEM_CAP_MB` | hard per-container `--memory` (= `--memory-swap`); empty = 0.8·RAM/`PARALLEL`, floor 2048 |
| `CPUSET` | pin all containers to these cores (`--cpuset-cpus`), e.g. `26-31` |
| `EXCLUDE=1` | skip the T5 (`unreachable`) targets |
| `EXTRA_ARGS` | passed to `aflgo_baseline.py` (`--project`, `--difficulty`, `--target`, `--rounds`) |
| `NO_TMUX=1` | run in the foreground without the monitor |
| `IMAGE_REPO` | image repo (default `local/dfuzzbench-aflgo`) |
| `PULL=1` | `docker pull` the images first (default 0: use local images) |
| `AFLGO_RESULTS_DIR` | output root (default `results/aflgo`) |
| `AFLGO_CONTAINER_PREFIX` | extra prefix for the `aflgo_*` container names |

## Seeds and dictionaries
Each image bakes its seeds into `/seeds` (cmark spec tests, libpng and exiv2 test
files, AFL's JPEG testcases for guetzli, md4c's seed corpus, generated wasm
modules for wamr, libbpf's OSS-Fuzz seed corpus; the other projects start from
`AAAA`) and its dictionary into `/out/*.dict` (cmark, libpng, exiv2, md4c).
