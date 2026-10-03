# AFLGo baseline (directed greybox fuzzing)

A directed greybox fuzzing baseline: AFLGo (AFL 2.57b based, upstream commit
`fa125da`) fuzzes each target with distance guidance toward its target line,
with no LLM involved. It runs in two tracks, each with its own launcher and
README:

| Track | What it does | Targets | Metric | Start here |
|---|---|---|---|---|
| **A — reachability** | directed fuzzing toward each benchmark target line | the C/C++ T1–T5 targets (`data/target-latest`; AFLGo has no Python support) | target line reached (`HIT TARGET`) | [`reachability/`](reachability/README.md) |
| **B — vulnerability** | directed fuzzing toward the target line of each known CVE, then CVE reproduction check | 50 T6 ARVO CVEs (`data/target-arvo/benchmark.json`) | reached, and independently reproduced (location-verified) | [`vulnerability/`](vulnerability/README.md) |

Both tracks count a target as reached when replaying an AFL crash prints the
`HIT TARGET` sentinel that the target patch places at the target line.

All commands run from `static/`. Requirements: Docker, any `python3` (stdlib
only), `tmux` for full runs; no API keys. Images are built locally under the
`local/` namespace; the launchers never pull or push (`PULL=1` / `--push` opt
in). Building them fetches `ubuntu:20.04` (Track A) and the public
`n132/arvo:<cve>-vul` images (Track B) from Docker Hub.

```bash
# Track A: build the 10 project images, then a one-target smoke
bash source/aflgo/build_images.sh
NO_TMUX=1 PARALLEL=1 MAX_TIME=600 EXTRA_ARGS="--project cmark --target src:blocks.c:644" \
  bash source/aflgo/reachability/run_track_a.sh
# Track B: three image-build steps, then run_track_b.sh — see vulnerability/README.md
```

## Budget
Both launchers take the fuzzing budget per target in seconds from `MAX_TIME`
(default 518400), and AFLGo's exploitation phase starts after 75% of it. See
each track's README for the other sizing knobs.

## Outputs
Everything goes to `results/aflgo/` under `static/` (git-ignored; override with
`AFLGO_RESULTS_DIR`): `track_a/` holds the per-target CSV, crash tarballs and
logs, `track_b/` the build, eval and audit CSVs, crash snapshots and watchdog
logs.

## Layout
- `Dockerfile.base`, `build_images.sh`, `afl_driver.c`, `projects/<project>/`
  (Dockerfile, `build_project.sh`, harness): the Track A images.
- `patches/`: modified copies of two AFLGo `fa125da` files (Apache-2.0) and the
  script that applies them to the Track B phase-0 container.
- `reachability/`, `vulnerability/`: the two tracks.

## Cleanup (only between runs)
The tracks name their containers `aflgo_*` (Track A runs, Track B image
builds), `vuln-<cve>-container-6d`, `arvo_aflgo_source` and `arvo_aflgo_phase0`
(Track B), all prefixed with `$AFLGO_CONTAINER_PREFIX` when set. Remove only those:
```bash
docker ps -a --format '{{.Names}}' \
  | grep -E "^${AFLGO_CONTAINER_PREFIX:-}(aflgo_|vuln-[0-9]+-container-6d\$|arvo_aflgo_(source|phase0)\$)" \
  | xargs -r docker rm -f
```
