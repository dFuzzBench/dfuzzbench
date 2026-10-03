#!/bin/bash -eu
# AFLGo build script for exiv2.

cd /src/exiv2
# Copy harness into the expected location
cp /src/harness.cpp fuzz/fuzz-read-print-write.cpp

rm -rf build
mkdir -p build && cd build
cmake \
    -DEXIV2_ENABLE_PNG=ON \
    -DEXIV2_ENABLE_WEBREADY=ON \
    -DEXIV2_ENABLE_CURL=OFF \
    -DEXIV2_ENABLE_BMFF=ON \
    -DEXIV2_TEAM_WARNINGS_AS_ERRORS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS -fno-sanitize=float-divide-by-zero" \
    -DCMAKE_AR="$(which llvm-ar)" -DCMAKE_RANLIB="$(which llvm-ranlib)" \
    -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS:-}" \
    -DEXIV2_BUILD_FUZZ_TESTS=ON \
    -DEXIV2_TEAM_OSS_FUZZ=ON \
    -DLIB_FUZZING_ENGINE="$AFL_DRIVER" \
    -DEXIV2_ENABLE_INIH=OFF \
    -DEXIV2_ENABLE_BROTLI=OFF \
    ..
make -j$(nproc)

cp ./bin/fuzz-read-print-write $OUT/fuzzer_instrumented
