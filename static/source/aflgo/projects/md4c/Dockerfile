FROM local/dfuzzbench-aflgo:base
RUN git clone https://github.com/mity/md4c /src/md4c
WORKDIR /src/md4c
RUN git fetch --depth=1 origin 481fbfbdf72daab2912380d62bb5f2187d438408 \
    && git reset --hard 481fbfbdf72daab2912380d62bb5f2187d438408
# Seed corpus + dictionary
RUN mkdir -p /seeds && cp test/fuzzers/seed-corpus/* /seeds/ 2>/dev/null || true
RUN mkdir -p /out && git clone --depth 1 https://github.com/google/fuzzing /tmp/fuzzing && \
    cp /tmp/fuzzing/dictionaries/markdown.dict /out/fuzzer_instrumented.dict && rm -rf /tmp/fuzzing
COPY harness.c /src/harness.c
COPY build_project.sh /src/build_project.sh
