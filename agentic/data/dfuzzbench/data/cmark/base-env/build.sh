#!/bin/bash -eu
# Copyright 2017 Google Inc.
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

mkdir -p build
cd build
cmake ../
make cmark_static
cd ..

$CC $CFLAGS -Isrc -Ibuild/src -c $SRC/testbed/instrumented_fuzzer.c -o instrumented_fuzzer.o
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE instrumented_fuzzer.o build/src/libcmark.a -o $OUT/instrumented_fuzzer
cp $SRC/*.options $OUT/
cp fuzz/dictionary $OUT/cmark.dict

mkdir -p corpus
python3 test/spec_tests.py --fuzz-corpus corpus --spec test/spec.txt
python3 test/spec_tests.py --fuzz-corpus corpus --spec test/regression.txt
python3 test/spec_tests.py --fuzz-corpus corpus --spec test/smart_punct.txt
zip -j $OUT/fuzzer_instrumented_seed_corpus.zip corpus/*
