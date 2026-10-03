FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y autoconf automake libtool zlib1g-dev && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/madler/zlib.git /src/zlib
RUN git clone --depth 1 https://github.com/pnggroup/libpng.git /src/libpng
WORKDIR /src/libpng
RUN git fetch --depth=1 origin c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003 \
    && git reset --hard c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003
# Seed corpus
RUN mkdir -p /seeds && find /src/libpng -name "*.png" | grep -v crashers | head -100 | \
    while read f; do cp "$f" /seeds/ 2>/dev/null || true; done
RUN mkdir -p /out && cp /src/libpng/contrib/oss-fuzz/*.dict /out/fuzzer_instrumented.dict
COPY harness.cc /src/harness.cc
COPY build_project.sh /src/build_project.sh
