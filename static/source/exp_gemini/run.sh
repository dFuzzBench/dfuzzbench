#!/bin/bash
# Start (or resume) a sweep in the background. The API key is taken from GEMINI_API_KEY (or GOOGLE_API_KEY) in
# the caller's environment; the driver removes it from its environment at start-up, so docker and other child
# processes never see it. --random-input and --fake-response runs need no key.
#
#   exp_gemini/run.sh --name no-source --models gemini-3.1-pro-preview --setting no_context ...
#   python exp_gemini/sweep.py status --name no-source
#
# PYTHON selects the interpreter (default: python3 on PATH); DFUZZBENCH_RESULTS the results root
# (default: static/exp_results).
set -euo pipefail
cd "$(dirname "$0")/.."    # static/source of this checkout
PYTHON="${PYTHON:-python3}"
RESULTS="${DFUZZBENCH_RESULTS:-$(cd .. && pwd)/exp_results}"

NAME="" prev="" offline=""
for arg in "$@"; do
    [ "$prev" = "--name" ] && NAME="$arg"
    case "$arg" in --random-input|--fake-response) offline=1 ;; esac
    prev="$arg"
done
[ -n "$NAME" ] || { echo "usage: $0 --name NAME --models M [M ...] [sweep.py run options]" >&2; exit 2; }
if pgrep -f "exp_gemini/sweep.py run --name $NAME( |$)" >/dev/null; then
    echo "a sweep named $NAME is already running" >&2; exit 1
fi
if [ -z "$offline" ] && [ -z "${GEMINI_API_KEY:-}" ] && [ -z "${GOOGLE_API_KEY:-}" ]; then
    echo "set GEMINI_API_KEY (or GOOGLE_API_KEY) in the environment" >&2; exit 1
fi
mkdir -p "$RESULTS/$NAME"
DFUZZBENCH_RESULTS="$RESULTS" nohup "$PYTHON" -u exp_gemini/sweep.py run "$@" >> "$RESULTS/$NAME/stdout.log" 2>&1 &
echo "started pid $! -> $RESULTS/$NAME/"
