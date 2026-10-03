#!/bin/bash -eu
# AFLGo build script for libpng.

cd /src/libpng

# Disable logging/write options
cat scripts/pnglibconf.dfa | \
  sed -e "s/option STDIO/option STDIO disabled/" \
      -e "s/option WARNING /option WARNING disabled/" \
      -e "s/option WRITE enables WRITE_INT_FUNCTIONS/option WRITE disabled/" \
> scripts/pnglibconf.dfa.temp
mv scripts/pnglibconf.dfa.temp scripts/pnglibconf.dfa

autoreconf -f -i
# Pass AR/RANLIB/NM so autoconf+libtool use LTO-compatible tools
./configure CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" \
    AR="$AR" RANLIB="$RANLIB" NM="$NM" \
    --with-libpng-prefix=OSS_FUZZ_
make -j$(nproc) clean 2>/dev/null || true
make -j$(nproc) libpng16.la

$CXX $CXXFLAGS ${LDFLAGS:-} -std=c++11 -I. /src/harness.cc \
    -o $OUT/fuzzer_instrumented \
    $AFL_DRIVER .libs/libpng16.a -lz
