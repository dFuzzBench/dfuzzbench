#!/bin/bash -eu
# Copyright 2020 Google Inc.
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

# Build fuzz targets specified  in test/Makefile.
cp $SRC/testbed/instrumented_fuzzer.cc test/fuzzing/server_fuzzer.cc
cd test/fuzzing && make -j$(nproc) server_fuzzer

mv server_fuzzer instrumented_fuzzer

# Copy the fuzzer executables, zip-ed corpora, option and dictionary files to $OUT.
find . -name 'instrumented_fuzzer' -exec cp -v '{}' $OUT ';'          # Copy fuzz-target.
find . -name '*.dict' -exec cp -v '{}' $OUT ';'     # Copy dictionaries.
find . -name '*_seed_corpus.zip' -exec cp -v '{}' $OUT ';' # Copy seed corpora.