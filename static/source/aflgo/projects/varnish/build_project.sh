#!/bin/bash -eu
# AFLGo build script for varnish.

cd /src/varnish-cache
cp /src/harness.c bin/varnishd/fuzzers/esi_parse_fuzzer.c

./autogen.sh
# Pass AR/RANLIB/NM for LTO-compatible archive tools
export LIB_FUZZING_ENGINE="$AFL_DRIVER"
./configure CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" \
    AR="$AR" RANLIB="$RANLIB" NM="$NM" \
    --enable-oss-fuzz PCRE2_LIBS=-l:libpcre2-8.a \
    LIB_FUZZING_ENGINE="$AFL_DRIVER"
make -j2 -C include/
make -j2 -C lib/libvarnish/
make -j2 -C lib/libvgz/
make -j2 -C lib/libvsc/
make -j2 -C bin/varnishd/ esi_parse_fuzzer

cp bin/varnishd/*_fuzzer $OUT/fuzzer_instrumented
