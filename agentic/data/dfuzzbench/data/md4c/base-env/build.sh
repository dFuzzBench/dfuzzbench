#!/bin/bash -eu
# Copyright 2021 Google LLC
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
################################################################################

export CFLAGS="$CFLAGS -fprofile-instr-generate -fcoverage-mapping"
export CXXFLAGS="$CXXFLAGS -fprofile-instr-generate -fcoverage-mapping"

# Copy seed corpus and dictionary. Copy (not move) and reuse the build dir: the agentic env
# re-runs this script for every proposed input.
cp $SRC/{*.zip,*.dict} $OUT

mkdir -p build && cd build
cmake ../ -DBUILD_SHARED_LIBS=OFF
make
$CC $CFLAGS -c $SRC/testbed/instrumented_fuzzer.c -I../src
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE instrumented_fuzzer.o -o $OUT/instrumented_fuzzer \
    ./src/libmd4c-html.a ./src/libmd4c.a