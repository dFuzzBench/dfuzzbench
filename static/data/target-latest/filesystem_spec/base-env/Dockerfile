#!/usr/bin/python3
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
FROM gcr.io/oss-fuzz-base/base-builder-python
RUN pip3 install --upgrade pip aiohttp requests
RUN git clone https://github.com/fsspec/filesystem_spec filesystem_spec
COPY build.sh fuzzer_instrumented.py target.patch $SRC/
WORKDIR $SRC/filesystem_spec

RUN git fetch --depth=1 origin 3675a7cd163a3d7bc82f83e238ddc967f7b145c8
RUN git reset --hard 3675a7cd163a3d7bc82f83e238ddc967f7b145c8

RUN git apply $SRC/target.patch
