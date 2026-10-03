#!/bin/bash -eu
# AFLGo build script for clib.

cd /src/clib
make clean 2>/dev/null || true
make -j$(nproc) CC="$CC" CXX="$CXX" CFLAGS="-std=c99 -Ideps -Wall -Wno-unused-function -U__STRICT_ANSI__ $CFLAGS" CXXFLAGS="$CXXFLAGS" AR="$AR"

# Rename conflicting main functions
sed 's/int main(int argc/int main2(int argc/g' -i ./src/clib-search.c
sed 's/int main(int argc/int main2(int argc/g' -i ./src/clib-configure.c

find . -name "*.o" -not -name "*.lto.o" -exec $AR rcs fuzz_lib.a {} \;
$RANLIB fuzz_lib.a 2>/dev/null || true

$CC $CFLAGS -Wno-unused-function -U__STRICT_ANSI__ \
    -DHAVE_PTHREADS=1 -pthread \
    -c src/common/clib-cache.c src/clib-configure.c \
    src/common/clib-settings.c src/common/clib-package.c \
    /src/harness.c -I./asprintf -I./deps/ -I./deps/asprintf

$CXX $CXXFLAGS ${LDFLAGS:-} $AFL_DRIVER harness.o \
    -o $OUT/fuzzer_instrumented clib-cache.o clib-configure.o clib-settings.o clib-package.o \
    -I./deps/asprintf -I./deps -I./asprintf \
    fuzz_lib.a -L/usr/lib/x86_64-linux-gnu -lcurl -lpthread
