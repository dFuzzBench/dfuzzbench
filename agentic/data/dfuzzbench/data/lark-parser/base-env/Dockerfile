# Copyright 2023 Google LLC
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
##########################################################################
FROM gcr.io/oss-fuzz-base/base-builder-python
RUN pip3 install --upgrade pip
RUN git clone https://github.com/lark-parser/lark testbed

COPY build.sh target.patch $SRC/
COPY fuzzer.py $SRC/testbed/original_fuzzer.py
COPY fuzzer.py $SRC/testbed/instrumented_fuzzer.py
RUN echo "instrumented_fuzzer.py\noriginal_fuzzer.py\n" > $SRC/testbed/.debugreadonly

WORKDIR $SRC/testbed

RUN git fetch --depth=1 origin 24f19a35f376b9320d53f4d987793fb8b1765f37
RUN git reset --hard 24f19a35f376b9320d53f4d987793fb8b1765f37

RUN git apply $SRC/target.patch
