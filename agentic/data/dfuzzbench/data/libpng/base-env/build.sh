#!/bin/bash -eu

# Copyright 2017-2018 Glenn Randers-Pehrson
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Revisions by Glenn Randers-Pehrson, 2017:
# 1. Build only the library, not the tools (changed "make -j$(nproc) all" to
#     "make -j$(nproc) libpng16.la").
# 2. Disabled WARNING and WRITE options in pnglibconf.dfa.
# 3. Build zlib alongside libpng
################################################################################

# Disable logging via library build configuration control.
cat scripts/pnglibconf.dfa | \
  sed -e "s/option STDIO/option STDIO disabled/" \
      -e "s/option WARNING /option WARNING disabled/" \
      -e "s/option WRITE enables WRITE_INT_FUNCTIONS/option WRITE disabled/" \
> scripts/pnglibconf.dfa.temp
mv scripts/pnglibconf.dfa.temp scripts/pnglibconf.dfa

export CFLAGS="$CFLAGS -fprofile-instr-generate -fcoverage-mapping"
export CXXFLAGS="$CXXFLAGS -fprofile-instr-generate -fcoverage-mapping"

# build the libpng library.
autoreconf -f -i
./configure --with-libpng-prefix=OSS_FUZZ_
make -j$(nproc) clean
make -j$(nproc) libpng16.la

# build fuzzer_instrumented.
$CXX $CXXFLAGS -std=c++11 -I. \
     $SRC/testbed/instrumented_fuzzer.cc \
     -o $OUT/instrumented_fuzzer \
     -lFuzzingEngine .libs/libpng16.a -lz

# add seed corpus.
find $SRC/testbed -name "*.png" | grep -v crashers | \
     xargs zip $OUT/instrumented_fuzzer_seed_corpus.zip

cp $SRC/testbed/contrib/oss-fuzz/*.dict $OUT/instrumented_fuzzer.dict
cp $SRC/testbed/contrib/oss-fuzz/*.options $OUT/instrumented_fuzzer.options