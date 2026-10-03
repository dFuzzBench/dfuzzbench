#!/bin/bash -eu
# AFLGo build script for libbpf.
# elfutils is built with standard clang (no AFLGo/LTO) to avoid compatibility issues.
# Only libbpf + harness use AFLGo instrumentation.

cd /src/libbpf

# Build elfutils dependency with standard compiler (no LTO)
rm -rf elfutils
git clone https://sourceware.org/git/elfutils.git
(
    cd elfutils
    git checkout 67a187d4c1790058fc7fd218317851cb68bb087c

    sed -i 's/^\(NO_UNDEFINED=\).*/\1/' configure.ac
    sed -i 's/^\(ZDEFS_LDFLAGS=\).*/\1/' configure.ac

    autoreconf -i -f
    ./configure --enable-maintainer-mode --disable-debuginfod --disable-libdebuginfod \
        --disable-demangler --without-bzlib --without-lzma --without-zstd \
        CC=clang-11 CFLAGS="-Wno-error" CXX=clang++-11 CXXFLAGS="-Wno-error"

    make -C config -j$(nproc) V=1
    make -C lib -j$(nproc) V=1
    make -C libelf -j$(nproc) V=1
)

# Build libbpf with AFLGo compiler
make -C src BUILD_STATIC_ONLY=y V=1 clean 2>/dev/null || true
make -C src -j$(nproc) CFLAGS="-I$(pwd)/elfutils/libelf $CFLAGS" BUILD_STATIC_ONLY=y V=1 \
    AR="$AR" RANLIB="$RANLIB"

$CC $CFLAGS -Isrc -Iinclude -Iinclude/uapi -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
    -c /src/harness.c -o /src/harness.o
$CXX $CXXFLAGS ${LDFLAGS:-} /src/harness.o src/libbpf.a "$(pwd)/elfutils/libelf/libelf.a" \
    -l:libz.a $AFL_DRIVER -o $OUT/fuzzer_instrumented
