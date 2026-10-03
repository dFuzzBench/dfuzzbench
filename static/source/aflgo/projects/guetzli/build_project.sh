#!/bin/bash -eu
# AFLGo build script for guetzli.

cd /src/guetzli
make clean 2>/dev/null || true
# Pass AR=llvm-ar so the Makefile archives with LTO-compatible tool
make -j$(nproc) guetzli_static CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" AR="$AR"
# Ensure archive is indexed for LTO
$RANLIB bin/Release/libguetzli_static.a 2>/dev/null || true

$CXX $CXXFLAGS ${LDFLAGS:-} -std=c++11 -I. /src/harness.cc \
    $AFL_DRIVER bin/Release/libguetzli_static.a \
    -o $OUT/fuzzer_instrumented
