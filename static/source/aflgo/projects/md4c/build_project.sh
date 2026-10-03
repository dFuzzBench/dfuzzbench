#!/bin/bash -eu
# AFLGo build script for md4c.

cd /src/md4c
rm -rf build
mkdir -p build && cd build
cmake ../ -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_AR="$(which llvm-ar)" -DCMAKE_RANLIB="$(which llvm-ranlib)"
make -j$(nproc)

$CC $CFLAGS -c /src/harness.c -I../src -o /src/harness.o
$CXX $CXXFLAGS ${LDFLAGS:-} /src/harness.o ./src/libmd4c-html.a ./src/libmd4c.a \
    $AFL_DRIVER -o $OUT/fuzzer_instrumented
