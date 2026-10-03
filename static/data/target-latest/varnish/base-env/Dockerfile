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
RUN apt-get update && apt install -y automake autoconf libtool pkg-config python3-docutils python3-sphinx libedit-dev libpcre2-dev libncurses-dev
RUN git clone --depth 1 https://github.com/varnishcache/varnish-cache
COPY target.patch build.sh fuzzer_instrumented.c $SRC
RUN chmod +x $SRC/build.sh && \
    [ -f /usr/local/bin/compile ] && chmod +x /usr/local/bin/compile
WORKDIR $SRC/varnish-cache

RUN git fetch --depth=1 origin 41059cf6815d0f366a5145fdb69d310e37e6d380
RUN git reset --hard 41059cf6815d0f366a5145fdb69d310e37e6d380

RUN git apply $SRC/target.patch