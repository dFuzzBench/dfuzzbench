#!/usr/bin/env bash
# Track A (reachability) launcher: AFLGo directed fuzzing on the C/C++
# T1-T5 targets. Derives static/ from this script's location and launches
# aflgo_baseline.py with dated logging. Outputs go to $AFLGO_RESULTS_DIR/track_a
# (default: the git-ignored static/results/aflgo/track_a).
#
# Default: spawns two tmux sessions (aflgoA campaign + aflgoAmon hourly monitor)
# and returns, so an SSH session can drop safely:
#   bash source/aflgo/reachability/run_track_a.sh
# Foreground (no tmux, no monitor) — for smokes:
#   NO_TMUX=1 bash source/aflgo/reachability/run_track_a.sh
#
# Env overrides:
#   PYTHON=/path/to/python3   # default: first python3/python on PATH
#   MAX_TIME=518400           # fuzz budget per target, seconds (default 6 days);
#                             #   fuzz_target.sh sets exploitation switch = 75%
#   PARALLEL=176              # concurrent targets (see note)
#   MEM_CAP_MB=2048           # hard per-container --memory cap, MB (== --memory-swap,
#                             #   no swap). Empty = auto: 0.8*RAM/PARALLEL, floor 2048.
#                             #   Pair with a low PARALLEL on small hosts to prevent a
#                             #   host OOM from the concurrent in-container LTO builds.
#   CPUSET=26-31              # pin every container to these host cores (docker
#                             #   --cpuset-cpus); also throttles the in-container
#                             #   build (nproc honors the mask).
#   EXCLUDE=1                 # skip the T5 ("unreachable") targets
#   IMAGE_REPO=<repo>         # image repo (default local/dfuzzbench-aflgo, built by
#                             #   source/aflgo/build_images.sh)
#   PULL=1                    # docker pull the images first (default 0: local images)
#   NO_TMUX=1                 # run campaign in foreground, no monitor
#   EXTRA_ARGS="--project cmark --difficulty easy"   # scope a smoke run
#   AFLGO_RESULTS_DIR=<dir>   # output root (default static/results/aflgo)
#   AFLGO_CONTAINER_PREFIX=<p> # extra prefix for container names (co-located runs)
#
# PARALLEL note: 176 runs every target at once → ~MAX_TIME wall clock, but on an
# N-core host that is (176/N)x CPU oversubscription. On a normal host set
# PARALLEL ≈ nproc; targets then run in waves, each still getting its full MAX_TIME.
set -euo pipefail

REACH_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REPO_ROOT="$(cd "$REACH_DIR/../../.." && pwd)"   # source/aflgo/reachability -> static/
PY="${PYTHON:-$(command -v python3 || command -v python || true)}"
[ -n "$PY" ] || { echo "ERROR: no python3/python on PATH; set PYTHON=/path/to/python3" >&2; exit 1; }

MAX_TIME="${MAX_TIME:-518400}"
PARALLEL="${PARALLEL:-176}"
IMAGE_REPO="${IMAGE_REPO:-local/dfuzzbench-aflgo}"
PULL="${PULL:-0}"
MEM_CAP_MB="${MEM_CAP_MB:-}"   # empty => aflgo_baseline.py auto-sizes from RAM/PARALLEL
RESULTS_ROOT="$(realpath -m "${AFLGO_RESULTS_DIR:-$REPO_ROOT/results/aflgo}")"
OUT_DIR="$RESULTS_ROOT/track_a"
# C/C++ projects only (AFLGo doesn't support Python). Mirrors AFLGO_PROJECTS in
# aflgo_baseline.py. The :base image is build-only and NOT needed to run.
PROJECTS=(cmark libpng exiv2 guetzli md4c cpp-httplib varnish wamr clib libbpf)
if [[ " ${EXTRA_ARGS:-} " =~ --project(=|[[:space:]]+)([^[:space:]]+) ]]; then
  PROJECTS=("${BASH_REMATCH[2]}")
fi

EXCLUDE_FLAG=""; MON_EXCLUDE_FLAG=""
if [ -n "${EXCLUDE:-}" ]; then
  EXCLUDE_FLAG="--exclude_unreachable"          # aflgo_baseline.py (underscore)
  MON_EXCLUDE_FLAG="--exclude-unreachable"      # track_a_status.py (hyphen)
fi
MEM_CAP_FLAG=""; [ -n "${MEM_CAP_MB:-}" ] && MEM_CAP_FLAG="--mem_cap_mb $MEM_CAP_MB"
CPUSET_FLAG="";  [ -n "${CPUSET:-}" ]     && CPUSET_FLAG="--cpuset $CPUSET"

echo "== Track A == py=$PY static=$REPO_ROOT max_time=${MAX_TIME}s parallel=$PARALLEL mem_cap=${MEM_CAP_MB:-auto} ${CPUSET_FLAG} image=$IMAGE_REPO ${EXCLUDE_FLAG}"

if [ "$PULL" != "0" ]; then
  echo "Pulling ${#PROJECTS[@]} project images from $IMAGE_REPO..."
  for p in "${PROJECTS[@]}"; do
    echo "  pull $IMAGE_REPO:$p"; docker pull "$IMAGE_REPO:$p"
  done
fi
for p in "${PROJECTS[@]}"; do
  docker image inspect "$IMAGE_REPO:$p" >/dev/null 2>&1 || {
    echo "ERROR: image $IMAGE_REPO:$p not found; build it with: bash source/aflgo/build_images.sh $p" >&2; exit 1; }
done

mkdir -p "$OUT_DIR"
LOG="$OUT_DIR/run-$(date +%F)-trackA.log"
echo "Logging to $LOG  (CSV: $OUT_DIR/aflgo-baseline-<ts>.csv)"

BASE_ARGS="--max_time $MAX_TIME --parallel $PARALLEL $EXCLUDE_FLAG $MEM_CAP_FLAG $CPUSET_FLAG ${EXTRA_ARGS:-}"
export PYTHONPATH="$REPO_ROOT/source" IMAGE_REPO AFLGO_RESULTS_DIR="$RESULTS_ROOT" \
       AFLGO_CONTAINER_PREFIX="${AFLGO_CONTAINER_PREFIX:-}"

# Foreground (no monitor) — for smokes / small hosts.
if [ -n "${NO_TMUX:-}" ]; then
  echo "Running campaign in FOREGROUND (monitor NOT started)..."
  set +e
  "$PY" -u "$REACH_DIR/aflgo_baseline.py" $BASE_ARGS 2>&1 | tee -a "$LOG"
  rc=${PIPESTATUS[0]}; set -e
  exit "$rc"
fi

# A tmux session inherits the tmux server's environment, not ours: pass it explicitly.
RUN_ENV="PYTHONPATH='$PYTHONPATH' IMAGE_REPO='$IMAGE_REPO' AFLGO_RESULTS_DIR='$AFLGO_RESULTS_DIR' AFLGO_CONTAINER_PREFIX='$AFLGO_CONTAINER_PREFIX'"

# Default: campaign + an hourly progress monitor under tmux (analog of Track B's
# arvofuzz/arvowd), then return so an SSH session can drop safely.
command -v tmux >/dev/null || { echo "ERROR: tmux not found; install it or run with NO_TMUX=1" >&2; exit 1; }
CAMPAIGN="env $RUN_ENV '$PY' -u '$REACH_DIR/aflgo_baseline.py' $BASE_ARGS 2>&1 | tee -a '$LOG'"
MON="env $RUN_ENV '$PY' -u '$REACH_DIR/track_a_status.py' --loop --interval 3600 $MON_EXCLUDE_FLAG"
tmux new -d -s aflgoA    "$CAMPAIGN"
tmux new -d -s aflgoAmon "$MON"
echo "Launched tmux sessions:"
echo "  aflgoA     — AFLGo campaign (${PARALLEL}-way, ${MAX_TIME}s/target)"
echo "  aflgoAmon  — hourly progress monitor -> $OUT_DIR/aflgo-baseline-progress.csv"
echo "Monitor:  tmux attach -t aflgoAmon   |   $PY $REACH_DIR/track_a_status.py"
echo "Log:      tail -f $LOG"
