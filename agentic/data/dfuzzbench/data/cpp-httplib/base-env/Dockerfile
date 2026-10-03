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

FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y make autoconf automake libtool

# Install brotli.
RUN apt-get install -y libbrotli-dev

# Install zlib.
RUN apt-get update && apt-get install -y zlib1g-dev

RUN git clone --depth 1 https://github.com/yhirose/cpp-httplib.git testbed

WORKDIR testbed
COPY build.sh target.patch $SRC/
COPY fuzzer.cc $SRC/testbed/original_fuzzer.cc
COPY fuzzer.cc $SRC/testbed/instrumented_fuzzer.cc
RUN echo "instrumented_fuzzer.cc\noriginal_fuzzer.cc\n" > $SRC/testbed/.debugreadonly

RUN git fetch --depth=1 origin 4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d
RUN git reset --hard 4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d

RUN git apply $SRC/target.patch