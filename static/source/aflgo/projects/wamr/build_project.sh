#!/bin/bash -eu
# AFLGo build script for wamr.

cd /src/wamr/tests/fuzz/wasm-mutator-fuzz/
cp /src/harness.cc wasm_mutator_fuzz.cc

rm -rf build_loader
cmake -S . -B build_loader \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_AR="$(which llvm-ar)" -DCMAKE_RANLIB="$(which llvm-ranlib)" \
    -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS:-} $AFL_DRIVER -Wl,--allow-multiple-definition"
cmake --build build_loader -j$(nproc)

cp ./build_loader/wasm_mutator_fuzz $OUT/fuzzer_instrumented
