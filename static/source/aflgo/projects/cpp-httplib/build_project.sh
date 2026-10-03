#!/bin/bash -eu
# AFLGo build script for cpp-httplib.

cd /src/cpp-httplib
cp /src/harness.cc test/fuzzing/server_fuzzer.cc

# Build directly instead of using Makefile (which has hardcoded paths)
cd test/fuzzing
$CXX $CXXFLAGS ${LDFLAGS:-} -I../.. -DCPPHTTPLIB_ZLIB_SUPPORT \
    -o server_fuzzer server_fuzzer.cc \
    -lz $AFL_DRIVER -pthread -lanl

cp server_fuzzer $OUT/fuzzer_instrumented
