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

FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y make cmake
RUN git clone https://github.com/mity/md4c testbed

# Dictionaries:
RUN git clone --depth 1 https://github.com/google/fuzzing && \
    mv fuzzing/dictionaries/markdown.dict $SRC/fuzz-mdhtml.dict && \
    rm -rf fuzzing

# Seed corpus:
RUN zip -j $SRC/fuzz-mdhtml_seed_corpus.zip $SRC/testbed/test/fuzzers/seed-corpus/*

WORKDIR $SRC/testbed
COPY target.patch build.sh $SRC/
COPY fuzzer.c $SRC/testbed/original_fuzzer.c
COPY fuzzer.c $SRC/testbed/instrumented_fuzzer.c
RUN echo "instrumented_fuzzer.c\noriginal_fuzzer.c\n" > $SRC/testbed/.debugreadonly

RUN git fetch --depth=1 origin 481fbfbdf72daab2912380d62bb5f2187d438408
RUN git reset --hard 481fbfbdf72daab2912380d62bb5f2187d438408

RUN git apply $SRC/target.patch