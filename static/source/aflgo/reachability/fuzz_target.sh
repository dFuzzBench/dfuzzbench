#!/bin/bash -e
# AFLGo in-container runner.
# Usage: fuzz_target.sh <project> <target_file> <target_line> <max_time> [cooling_time]
#
# Expects:
#   /src/<project>/  - project source (clean, no patch applied)
#   /src/target.patch - modified target patch (exit(0) -> abort())
#   /src/build_project.sh - project-specific build commands
#
# Outputs:
#   RESULT:tte=<seconds>,build_time=<seconds>,total_iters=<execs>,status=<hit|timeout|build_failed>

PROJECT="$1"
TARGET_FILE="$2"
TARGET_LINE="$3"
MAX_TIME="${4:-1814400}"  # 3 weeks default
COOLING="${5:-0}"         # 0 = auto (75% of max_time)

if [ "$COOLING" -eq 0 ] 2>/dev/null; then
    COOLING=$((MAX_TIME * 3 / 4))
fi

export AFLGO=/aflgo
# Resolve symlinks so find and gen_distance_fast.py work correctly
SRC_DIR="$(readlink -f /src/$PROJECT)"
TMP_DIR="/tmp/aflgo-tmp"
OUT_DIR="/out"
SEEDS_DIR="/seeds"
AFL_OUT="/afl-out"

# Export OUT and SRC so build_project.sh can use them
export OUT="$OUT_DIR"
export SRC="/src"

mkdir -p "$TMP_DIR" "$OUT_DIR" "$SEEDS_DIR" "$AFL_OUT"

echo "=== AFLGo Target Runner ==="
echo "Project: $PROJECT"
echo "Target: $TARGET_FILE:$TARGET_LINE"
echo "Max time: ${MAX_TIME}s"
echo "Cooling: ${COOLING}s"

START=$(date +%s)

# ---- Step 1: Apply target patch ----
echo "[STAGE] Applying target patch..."
cd "$SRC_DIR"
git checkout -- . 2>/dev/null || true
git clean -fd 2>/dev/null || true

if [ -f /src/target.patch ]; then
    git apply /src/target.patch || { echo "RESULT:tte=0,build_time=0,total_iters=0,status=patch_failed"; exit 0; }
fi

# ---- Step 2: Create BBtargets.txt ----
echo "$TARGET_FILE:$TARGET_LINE" > "$TMP_DIR/BBtargets.txt"
echo "[INFO] BBtargets.txt: $(cat $TMP_DIR/BBtargets.txt)"

# ---- Step 3: Compile AFL driver ----
# Build two versions: LTO for stage 1 (gold linker), regular for stage 2
echo "[STAGE] Compiling AFL driver..."
$AFLGO/instrument/aflgo-clang -flto -c /aflgo/afl_driver.c -o /src/afl_driver_lto.o || \
    clang-11 -flto -c /aflgo/afl_driver.c -o /src/afl_driver_lto.o || \
    { echo "RESULT:tte=0,build_time=0,total_iters=0,status=build_failed"; exit 0; }
$AFLGO/instrument/aflgo-clang -c /aflgo/afl_driver.c -o /src/afl_driver.o || \
    clang-11 -c /aflgo/afl_driver.c -o /src/afl_driver.o || \
    { echo "RESULT:tte=0,build_time=0,total_iters=0,status=build_failed"; exit 0; }

# ---- Step 4: Stage 1 - Build with CG/CFG extraction ----
echo "[STAGE] Stage 1: Building with CG/CFG extraction..."
export CC="$AFLGO/instrument/aflgo-clang"
export CXX="$AFLGO/instrument/aflgo-clang++"
export AFL_DRIVER="/src/afl_driver_lto.o"
export CFLAGS="-targets=$TMP_DIR/BBtargets.txt -outdir=$TMP_DIR -flto -fuse-ld=gold -Wl,-plugin-opt=save-temps"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-flto -fuse-ld=gold -Wl,-plugin-opt=save-temps"
# LTO produces bitcode objects — standard ar/ranlib can't index them
export AR=llvm-ar
export RANLIB=llvm-ranlib
export NM=llvm-nm
export STAGE=1

# For stage 1, output binary to SRC_DIR so .bc files end up there for gen_distance_fast.py
export OUT="$SRC_DIR"

cd "$SRC_DIR"
if ! bash /src/build_project.sh; then
    echo "RESULT:tte=0,build_time=0,total_iters=0,status=build_failed_stage1"
    exit 0
fi

echo "[INFO] TMP_DIR contents:"
ls "$TMP_DIR/" 2>/dev/null | head -10

# Find directory containing the fuzzer's .bc files (may be in build subdir for cmake projects)
# Prefer fuzzer_instrumented.bc, then any non-test .bc file
BC_DIR="$SRC_DIR"
BC_FILE=$(find "$SRC_DIR" -name "fuzzer_instrumented.0.0.preopt.bc" -not -path "*/CMakeFiles/*" 2>/dev/null | head -1)
if [ -z "$BC_FILE" ]; then
    # Exclude common test/configure artifacts (a.out, *_test)
    BC_FILE=$(find "$SRC_DIR" -name "*.0.0.preopt.bc" -not -path "*/CMakeFiles/*" \
        -not -name "a.out.*" -not -name "*_test.*" -not -name "*CompilerId*" 2>/dev/null | head -1)
fi
if [ -z "$BC_FILE" ]; then
    BC_FILE=$(find "$SRC_DIR" -name "*.0.0.preopt.bc" -not -path "*/CMakeFiles/*" 2>/dev/null | head -1)
fi
if [ -n "$BC_FILE" ]; then
    BC_DIR=$(dirname "$BC_FILE")
fi
echo "[INFO] .bc files directory: $BC_DIR"
ls "$BC_DIR"/*.bc 2>/dev/null | head -5 || echo "(no .bc files found)"

# ---- Step 5: Compute distances ----
echo "[STAGE] Computing distances..."
cd "$SRC_DIR"

# Clean up BBnames/BBcalls
if [ -f "$TMP_DIR/BBnames.txt" ]; then
    cat "$TMP_DIR/BBnames.txt" | rev | cut -d: -f2- | rev | sort | uniq > "$TMP_DIR/BBnames2.txt" && \
        mv "$TMP_DIR/BBnames2.txt" "$TMP_DIR/BBnames.txt"
fi
if [ -f "$TMP_DIR/BBcalls.txt" ]; then
    # Fix malformed lines: AFLGo's parallel instrumentation can concatenate entries
    # without newlines, producing lines with >2 comma-separated fields.
    # Also remove lines with empty BB name (leading comma).
    awk -F',' 'NF==2 && $1!=""' "$TMP_DIR/BBcalls.txt" | sort | uniq > "$TMP_DIR/BBcalls2.txt" && \
        mv "$TMP_DIR/BBcalls2.txt" "$TMP_DIR/BBcalls.txt"
fi

# Generate distance file
# gen_distance_fast.py expects .bc files in binaries_directory
# Note: gen_distance_fast.py may return non-zero even when distance.cfg.txt is produced
# (e.g. assertion errors in distance.bin for some CFGs). Check the file, not the exit code.
# Try with the actual binary name first, then fuzzer_instrumented, then without name
if [ -n "$BC_FILE" ]; then
    BC_NAME=$(basename "$BC_FILE" | sed 's/\.0\.0\.preopt\.bc$//')
    echo "[INFO] Trying distance with binary name: $BC_NAME"
    python3 "$AFLGO/distance/gen_distance_fast.py" "$BC_DIR" "$TMP_DIR" "$BC_NAME" 2>&1 || true
fi

# Fallback: gen_distance_fast.py defaults to the C++ distance.bin, which SIGILLs
# (illegal instruction, exit 132) on CPUs lacking the ISA it was built for, e.g.
# an AVX2-only host. The pure-python calculator (--python-only) is
# slower but ISA-portable and yields identical distances; it resumes from the
# state the C++ attempt left behind (the call-graph .dot is reused, not rebuilt).
if [ ! -f "$TMP_DIR/distance.cfg.txt" ] || [ ! -s "$TMP_DIR/distance.cfg.txt" ]; then
    echo "[WARN] Fast distance calc failed; retrying with --python-only (fuzzer_instrumented name)..."
    python3 "$AFLGO/distance/gen_distance_fast.py" --python-only "$BC_DIR" "$TMP_DIR" fuzzer_instrumented 2>&1 || true
fi

if [ ! -f "$TMP_DIR/distance.cfg.txt" ] || [ ! -s "$TMP_DIR/distance.cfg.txt" ]; then
    echo "[WARN] Trying --python-only without fuzzer name..."
    python3 "$AFLGO/distance/gen_distance_fast.py" --python-only "$BC_DIR" "$TMP_DIR" 2>&1 || true
fi

if [ ! -f "$TMP_DIR/distance.cfg.txt" ]; then
    echo "RESULT:tte=0,build_time=0,total_iters=0,status=distance_failed"
    exit 0
fi
# Empty distance file is valid (target is trivially reachable) — create a dummy entry if needed
if [ ! -s "$TMP_DIR/distance.cfg.txt" ]; then
    echo "[WARN] Distance file is empty (target trivially reachable), adding dummy entry"
    echo "DUMMY_BB,1000" > "$TMP_DIR/distance.cfg.txt"
fi

echo "[INFO] Distance file generated: $(wc -l < $TMP_DIR/distance.cfg.txt) entries"

# ---- Step 6: Stage 2 - Rebuild with distance instrumentation ----
echo "[STAGE] Stage 2: Rebuilding with distance instrumentation..."
export STAGE=2
export AFL_DRIVER="/src/afl_driver.o"
export CFLAGS="-distance=$TMP_DIR/distance.cfg.txt"
export CXXFLAGS="$CFLAGS"
export LDFLAGS=""
export OUT="$OUT_DIR"

cd "$SRC_DIR"
git checkout -- . 2>/dev/null || true
# Use -fdx to also remove gitignored build artifacts (e.g. .libs/ with LTO bitcode from stage 1)
git clean -fdx 2>/dev/null || true

# Re-apply patch
if [ -f /src/target.patch ]; then
    git apply /src/target.patch || { echo "RESULT:tte=0,build_time=0,total_iters=0,status=patch_failed_stage2"; exit 0; }
fi

if ! bash /src/build_project.sh; then
    echo "RESULT:tte=0,build_time=0,total_iters=0,status=build_failed_stage2"
    exit 0
fi

if [ ! -f "$OUT_DIR/fuzzer_instrumented" ]; then
    echo "RESULT:tte=0,build_time=0,total_iters=0,status=build_failed_no_binary"
    exit 0
fi

# ---- Step 7: Prepare seeds ----
if [ -f "$OUT_DIR/fuzzer_instrumented_seed_corpus.zip" ]; then
    cd "$SEEDS_DIR" && unzip -o "$OUT_DIR/fuzzer_instrumented_seed_corpus.zip" 2>/dev/null || true
fi
# Ensure at least one seed exists
if [ -z "$(ls -A $SEEDS_DIR 2>/dev/null)" ]; then
    echo "AAAA" > "$SEEDS_DIR/default_seed"
fi

# ---- Step 8: Run afl-fuzz ----
echo "[STAGE] Running afl-fuzz..."
BUILD_END=$(date +%s)
BUILD_TIME=$((BUILD_END - START))
echo "[INFO] Build completed in ${BUILD_TIME}s"

# AFL settings
export AFL_NO_UI=1
export AFL_SKIP_CPUFREQ=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_NO_AFFINITY=1

# Dictionary if available
DICT_FLAG=""
DICT_FILE=$(ls "$OUT_DIR"/*.dict 2>/dev/null | head -1)
[ -n "${DICT_FILE:-}" ] && DICT_FLAG="-x $DICT_FILE"

# Start afl-fuzz in background
$AFLGO/afl-2.57b/afl-fuzz -m none -z exp -c "${COOLING}s" \
    -i "$SEEDS_DIR" -o "$AFL_OUT" \
    $DICT_FLAG \
    "$OUT_DIR/fuzzer_instrumented" @@ &
AFL_PID=$!

echo "[INFO] afl-fuzz started (PID=$AFL_PID)"

# ---- Step 9: Monitor for crash (target hit) ----
FUZZ_START=$(date +%s)
TTE=0
STATUS="timeout"

# Verifies a single input file crashes the binary via the injected target abort.
# A crash counts only if exit code is 134 (SIGABRT) AND stderr contains the
# "HIT TARGET" sentinel emitted by target.patch right before abort(). Any other
# SIGABRT (assert, UBSan, ASan, OOM, library abort) is rejected as off-target.
# Returns 0 on verified hit, 1 otherwise. Stderr captured to $2.
verify_hit() {
    local input="$1" err_log="$2"
    local rc=0
    # `|| rc=$?` captures the timeout's exit status without tripping `set -e`
    # on non-zero (which any crash signal would produce).
    timeout 10 "$OUT_DIR/fuzzer_instrumented" "$input" >/dev/null 2>"$err_log" || rc=$?
    if [ "$rc" -eq 134 ] && grep -q "HIT TARGET" "$err_log"; then
        return 0
    fi
    return 1
}

while true; do
    # Check if afl-fuzz is still running
    if ! kill -0 $AFL_PID 2>/dev/null; then
        echo "[WARN] afl-fuzz exited unexpectedly"
        # AFL aborts when a seed itself crashes (target reached during dry-run).
        # Re-run each seed; require the HIT TARGET sentinel to count as a hit,
        # not just any SIGABRT (off-target asserts/UBSan/ASan must not register).
        for seed in "$SEEDS_DIR"/*; do
            [ -f "$seed" ] || continue
            if verify_hit "$seed" /tmp/verify.err; then
                STATUS="hit"
                TTE=0
                TOTAL_ITERS=0
                echo "[HIT] Seed $(basename "$seed") triggers target abort with HIT TARGET marker"
                break
            fi
        done
        if [ "$STATUS" != "hit" ]; then
            STATUS="afl_died"
        fi
        break
    fi

    # Check for crashes in both possible directory structures.
    # NOTE: a previous version used `ls ... | head -1 > /dev/null 2>&1` whose
    # exit code is `head`'s — always 0 — making the check always succeed and
    # producing 100% false-positive "hit" reports.
    shopt -s nullglob
    crashes=( "$AFL_OUT"/*/crashes/id:* "$AFL_OUT"/crashes/id:* )
    shopt -u nullglob
    if [ ${#crashes[@]} -gt 0 ]; then
        # Re-execute each crash input outside afl-fuzz to verify the abort came
        # from the patched target line (HIT TARGET in stderr). Generic SIGABRTs
        # produced by libfuzzer wrappers, ASan, library asserts etc. would
        # otherwise produce false positives — see the wamr aot_loader cluster.
        verified=0
        for crash in "${crashes[@]}"; do
            if verify_hit "$crash" /tmp/verify.err; then
                verified=1
                break
            fi
        done
        if [ $verified -eq 1 ]; then
            NOW=$(date +%s)
            TTE=$((NOW - FUZZ_START))
            STATUS="hit"
            echo "[HIT] Verified target reached after ${TTE}s of fuzzing (${BUILD_TIME}s build + ${TTE}s fuzz)"
            # Capture stats BEFORE killing — afl-fuzz may truncate fuzzer_stats during shutdown.
            TOTAL_ITERS=$(cat "$AFL_OUT"/fuzzer_stats "$AFL_OUT"/*/fuzzer_stats 2>/dev/null | awk '/^execs_done/ {print $3; exit}')
            kill $AFL_PID 2>/dev/null || true
            break
        else
            # Crash files present but none hit the target line. Don't spam the
            # log every 5s — only print once per new crash count.
            if [ "${LAST_UNVERIFIED:-0}" -lt ${#crashes[@]} ]; then
                echo "[WARN] ${#crashes[@]} crash file(s) present but none reach HIT TARGET; continuing"
                LAST_UNVERIFIED=${#crashes[@]}
            fi
        fi
    fi

    # Check timeout
    NOW=$(date +%s)
    ELAPSED=$((NOW - FUZZ_START))
    if [ $ELAPSED -ge $MAX_TIME ]; then
        echo "[TIMEOUT] Max time ${MAX_TIME}s reached"
        TOTAL_ITERS=$(cat "$AFL_OUT"/fuzzer_stats "$AFL_OUT"/*/fuzzer_stats 2>/dev/null | awk '/^execs_done/ {print $3; exit}')
        kill $AFL_PID 2>/dev/null || true
        break
    fi

    # Progress every 60s
    if [ $((ELAPSED % 60)) -lt 6 ]; then
        EXECS=$(cat "$AFL_OUT"/fuzzer_stats "$AFL_OUT"/*/fuzzer_stats 2>/dev/null | awk '/^execs_done/ {print $3; exit}')
        echo "PROGRESS:elapsed=${ELAPSED}s,execs=${EXECS:-?}"
    fi

    sleep 5
done

# TOTAL_ITERS was captured before kill in the hit/timeout branches; fall back to a final
# read for the unexpected-exit branch and default to 0 if nothing was captured.
if [ -z "${TOTAL_ITERS:-}" ]; then
    TOTAL_ITERS=$(cat "$AFL_OUT"/fuzzer_stats "$AFL_OUT"/*/fuzzer_stats 2>/dev/null | awk '/^execs_done/ {print $3; exit}')
fi
TOTAL_ITERS=${TOTAL_ITERS:-0}

# Preserve crash inputs so they can be retroactively re-verified after the
# container is removed. Orchestrator (aflgo_baseline.py) does `docker cp` of
# this path before docker rm.
shopt -s nullglob
all_crashes=( "$AFL_OUT"/*/crashes/id:* "$AFL_OUT"/crashes/id:* )
shopt -u nullglob
if [ ${#all_crashes[@]} -gt 0 ]; then
    mkdir -p /tmp/crashes-export
    cp -a "${all_crashes[@]}" /tmp/crashes-export/ 2>/dev/null || true
    tar -czf /tmp/crashes-export.tar.gz -C /tmp crashes-export 2>/dev/null || true
    echo "[INFO] Preserved ${#all_crashes[@]} crash input(s) at /tmp/crashes-export.tar.gz"
fi

echo "RESULT:tte=$TTE,build_time=$BUILD_TIME,total_iters=$TOTAL_ITERS,status=$STATUS"
