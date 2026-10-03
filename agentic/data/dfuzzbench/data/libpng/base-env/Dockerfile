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
################################################################################

FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && \
    apt-get install -y make autoconf automake libtool zlib1g-dev

RUN git clone --depth 1 https://github.com/madler/zlib.git
RUN git clone --depth 1 https://github.com/pnggroup/libpng.git testbed
# RUN cp libpng/contrib/oss-fuzz/build.sh $SRC
WORKDIR testbed

COPY target.patch build.sh $SRC/
COPY fuzzer.cc $SRC/testbed/original_fuzzer.cc
COPY fuzzer.cc $SRC/testbed/instrumented_fuzzer.cc
RUN echo "instrumented_fuzzer.cc\noriginal_fuzzer.cc\n" > $SRC/testbed/.debugreadonly

RUN git fetch --depth=1 origin c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003
RUN git reset --hard c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003

RUN git apply $SRC/target.patch
