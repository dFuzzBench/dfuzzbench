#!/bin/bash
# Apply the AFLGo toolchain patches that the ARVO-AFLGo build needs. Run this
# against a fresh aflgo source tree (or the local/arvo-aflgo-base source
# container) so the per-CVE builds — which copy /src/aflgo verbatim via
# build_aflgo_arvo.py:install_aflgo_via_copy — inherit the fixes.
#
#   A1  distance/gen_distance_fast.py
#       Make CFG distance step-2 fault-tolerant: one unreadable CFG (a
#       heavily-templated C++ name whose cfg.<mangled>.dot exceeds the 255-char
#       filename limit, so the pass skipped writing it) must not abort the
#       whole step. Unlocks serenity FuzzJs/FuzzShell + assimp distance.
#
#   A2  instrument/aflgo-pass.so.cc
#       Add cl::ZeroOrMore to the -distance/-targets/-outdir cl::opt so a
#       Makefile that consumes both $(CFLAGS) and $(CXXFLAGS) on a single
#       compile (openh264: `$(CXX) $(CFLAGS) $(CXXFLAGS)`) doesn't trip LLVM's
#       "may only occur zero or one times". Requires recompiling the pass.
#
# Usage:
#   In-image (operates on /src/aflgo):     ./apply_aflgo_patches.sh
#   Against a running container:           ./apply_aflgo_patches.sh <container>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CONTAINER="${1:-}"

# The pass MUST be rebuilt with a clean CFLAGS/CXXFLAGS. The ARVO/OSS-Fuzz base
# exports CXXFLAGS containing -stdlib=libc++, and instrument/Makefile uses
# `CXXFLAGS ?=` so that env value wins -> the .so picks up the libc++
# std::string ABI (std::__1) and fails to load against the libstdc++-built host
# clang ("undefined symbol ..._ZNK4llvm10ModulePass17createPrinterPass...").
# Clearing the env restores the Makefile's libstdc++ default.
REBUILD='cd /src/aflgo/instrument && rm -f aflgo-pass.so && env -u CFLAGS -u CXXFLAGS -u CPPFLAGS make aflgo-pass.so'

if [[ -z "$CONTAINER" ]]; then
  cp "$HERE/gen_distance_fast.py" /src/aflgo/distance/gen_distance_fast.py
  cp "$HERE/aflgo-pass.so.cc"     /src/aflgo/instrument/aflgo-pass.so.cc
  bash -c "$REBUILD"
  echo "[apply_aflgo_patches] done (local /src/aflgo)"
else
  docker cp "$HERE/gen_distance_fast.py" "$CONTAINER:/src/aflgo/distance/gen_distance_fast.py"
  docker cp "$HERE/aflgo-pass.so.cc"     "$CONTAINER:/src/aflgo/instrument/aflgo-pass.so.cc"
  docker exec "$CONTAINER" bash -lc "$REBUILD"
  echo "[apply_aflgo_patches] done (container $CONTAINER)"
fi
