#!/bin/bash
# Build dfuzzbench per-target Docker images locally, tagged exactly as the dfuzzbench
# environment expects (local/dfuzzbench:<tag>), from the files under data/dfuzzbench/data/.
#
# An image is the project testbed with the target line instrumented (the target's target.patch
# injects `print("HIT TARGET"); exit(0)` at that line). It builds
# `FROM gcr.io/oss-fuzz-base/base-builder[-python]` and clones the project from GitHub.
# Targets of one project share all layers but the last few, so building many is cheap.
#
# Usage (run from the agentic/ root):
#   bash scripts/build_dfuzzbench_image.sh                                  # the target of config_dfuzzbench.yaml
#   bash scripts/build_dfuzzbench_image.sh "<project>::<target>" ...        # specific target ids
#   bash scripts/build_dfuzzbench_image.sh scripts/config_dfuzzbench_gemini_py.yaml  # every target of a config
set -euo pipefail
cd "$(dirname "$0")/.."          # -> agentic/

[ $# -gt 0 ] || set -- scripts/config_dfuzzbench.yaml

TARGET_IDS=()
for arg in "$@"; do
    case "$arg" in
        *.yaml|*.yml)
            # A command substitution (unlike `< <(...)`) stops the script if python3 fails.
            ids_text=$(python3 -c 'import sys, yaml; p = yaml.safe_load(open(sys.argv[1]))["base"]["problems"]; assert isinstance(p, list), "base.problems must be a list of target ids"; print("\n".join(p))' "$arg")
            mapfile -t ids <<< "$ids_text"
            TARGET_IDS+=("${ids[@]}") ;;
        *::*) TARGET_IDS+=("$arg") ;;
        *) echo "ERROR: '$arg' is neither a target id (<project>::<target>) nor a config file"; exit 1 ;;
    esac
done

build_one() {
    local TARGET_ID="$1"
    local PROJECT="${TARGET_ID%%::*}"
    local TARGET="${TARGET_ID#*::}"     # part after '::', e.g. bleach:_vendor:html5lib:_tokenizer.py:1434

    # image tag: same transform the environment uses (debug_gym/gym/envs/dfuzz_bench.py)
    local TAG
    TAG=$(python3 -c 'import sys; t=sys.argv[1]; print(t.lower().replace("::",".").replace(":",".").replace("._",".0_").replace("_.","_0."))' "$TARGET_ID")

    local BASE_ENV="data/dfuzzbench/data/$PROJECT/base-env"
    [ -d "$BASE_ENV" ] || { echo "ERROR: no base-env for project '$PROJECT' ($BASE_ENV)"; return 1; }
    local TGT_DIR
    TGT_DIR=$(find "data/dfuzzbench/data/$PROJECT/realistic" -mindepth 2 -maxdepth 2 -type d -name "$TARGET" 2>/dev/null | head -1)
    [ -n "${TGT_DIR:-}" ] && [ -f "$TGT_DIR/target.patch" ] || { echo "ERROR: no target.patch found for '$TARGET_ID'"; return 1; }

    local CTX
    CTX=$(mktemp -d)
    cp -r "$BASE_ENV"/. "$CTX"/
    cp "$TGT_DIR/target.patch" "$CTX"/target.patch

    echo "Building  local/dfuzzbench:$TAG"
    echo "  project = $PROJECT"
    echo "  patch   = $TGT_DIR/target.patch"
    local rc=0
    docker build -t "local/dfuzzbench:$TAG" "$CTX" || rc=$?
    rm -rf "$CTX"
    return $rc
}

FAILED=()
for id in "${TARGET_IDS[@]}"; do
    build_one "$id" || FAILED+=("$id")
done

echo
echo "Built $(( ${#TARGET_IDS[@]} - ${#FAILED[@]} ))/${#TARGET_IDS[@]} image(s) as local/dfuzzbench:<tag>."
if [ ${#FAILED[@]} -gt 0 ]; then
    printf 'FAILED: %s\n' "${FAILED[@]}"
    exit 1
fi
