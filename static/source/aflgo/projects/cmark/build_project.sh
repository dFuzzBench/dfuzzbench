#!/bin/bash -eu
# AFLGo build script for cmark.
# Called by fuzz_target.sh with CC/CXX/CFLAGS/CXXFLAGS/AFL_DRIVER/OUT set.

cd /src/cmark
rm -rf build
mkdir -p build && cd build
cmake ../ -DCMAKE_C_COMPILER="$CC" -DCMAKE_C_FLAGS="$CFLAGS" \
          -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
          -DCMAKE_AR="$(which llvm-ar)" -DCMAKE_RANLIB="$(which llvm-ranlib)"
make -j$(nproc) cmark_static
cd ..

$CC $CFLAGS -Isrc -Ibuild/src -c /src/harness.c -o /src/harness.o
$CXX $CXXFLAGS $LDFLAGS /src/harness.o build/src/libcmark.a $AFL_DRIVER -o $OUT/fuzzer_instrumented
