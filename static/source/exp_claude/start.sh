#!/bin/bash
# Start (or resume) a Claude Code agentic run in the background.
#
#   exp_claude/start.sh --name test --targets cmark::src:blocks.c:144 wasm3::42495613 --concurrency 2
#   exp_claude/start.sh --name claude --all
#   python exp_claude/run_claude.py status --name test
#
# Claude Code authenticates with CLAUDE_CODE_OAUTH_TOKEN (a long-lived token made by `claude setup-token`) or
# ANTHROPIC_API_KEY, taken from the caller's environment -- never from a file, a command line or a log. The driver
# hands it only to the claude processes. PYTHON selects the interpreter (default: python3 on PATH), CLAUDE_BIN the
# claude binary (default: claude on PATH), DFUZZBENCH_RESULTS the results root (default: static/exp_results).
set -euo pipefail
cd "$(dirname "$0")/.."    # static/source of this checkout
PYTHON="${PYTHON:-python3}"
RESULTS="${DFUZZBENCH_RESULTS:-$(cd .. && pwd)/exp_results}"

NAME="" prev=""
for arg in "$@"; do [ "$prev" = "--name" ] && NAME="$arg"; prev="$arg"; done
[ -n "$NAME" ] || { echo "usage: $0 --name NAME [run_claude.py run options]" >&2; exit 2; }
if [ -z "${CLAUDE_CODE_OAUTH_TOKEN:-}" ] && [ -z "${ANTHROPIC_API_KEY:-}" ]; then
    echo "set CLAUDE_CODE_OAUTH_TOKEN (claude setup-token) or ANTHROPIC_API_KEY in the environment" >&2; exit 1
fi
RUN_DIR="$RESULTS/exp_claude/$NAME"
mkdir -p "$RUN_DIR"
DFUZZBENCH_RESULTS="$RESULTS" env -u GEMINI_API_KEY -u GOOGLE_API_KEY \
    nohup "$PYTHON" -u exp_claude/run_claude.py run "$@" >> "$RUN_DIR/stdout.log" 2>&1 &
echo "started pid $! -> $RUN_DIR/"
