#!/bin/bash
# Start (or resume) a run of the effort study in the background.
#
#   exp_reach/start_pilot.sh --name cybergym --corpus exp_reach/data/corpus_cybergym50.json --concurrency 5
#   python exp_reach/run_pilot.py status --name cybergym
#
# Claude Code authenticates with CLAUDE_CODE_OAUTH_TOKEN (`claude setup-token`) or ANTHROPIC_API_KEY from the
# caller's environment; the driver hands it only to the host claude processes, so the agent (no native tools, a
# --network none container) never sees it. PYTHON selects the interpreter (default: python3 on PATH), CLAUDE_BIN the
# claude binary, DFUZZBENCH_RESULTS the results root (default: static/exp_results).
set -euo pipefail
cd "$(dirname "$0")/.."                                   # static/source of this checkout
PYTHON="${PYTHON:-python3}"
RESULTS="${DFUZZBENCH_RESULTS:-$(cd .. && pwd)/exp_results}"

NAME="" prev=""
for arg in "$@"; do [ "$prev" = "--name" ] && NAME="$arg"; prev="$arg"; done
[ -n "$NAME" ] || { echo "usage: $0 --name NAME --corpus JSON [options]" >&2; exit 2; }
if [ -z "${CLAUDE_CODE_OAUTH_TOKEN:-}" ] && [ -z "${ANTHROPIC_API_KEY:-}" ]; then
    echo "set CLAUDE_CODE_OAUTH_TOKEN (claude setup-token) or ANTHROPIC_API_KEY in the environment" >&2; exit 1
fi
RUN_DIR="$RESULTS/exp_reach/$NAME"
mkdir -p "$RUN_DIR"
DFUZZBENCH_RESULTS="$RESULTS" env -u GEMINI_API_KEY -u GOOGLE_API_KEY \
    nohup "$PYTHON" -u exp_reach/run_pilot.py run "$@" >> "$RUN_DIR/stdout.log" 2>&1 &
echo "started pid $! -> $RUN_DIR/"
