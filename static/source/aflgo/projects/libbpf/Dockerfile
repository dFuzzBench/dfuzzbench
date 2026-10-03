FROM local/dfuzzbench-aflgo:base
RUN sed -i -e '/^#\s*deb-src.*\smain\s\+restricted/s/^#//' /etc/apt/sources.list 2>/dev/null || true && \
    apt-get update && \
    apt-get build-dep -y --no-install-recommends libelf-dev 2>/dev/null || true && \
    apt-get install -y --no-install-recommends pkg-config libz-dev && \
    rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/libbpf/libbpf /src/libbpf
WORKDIR /src/libbpf
RUN git fetch --depth=1 origin c5f22aca0f3aa855daa159b2777472b35e721804 \
    && git reset --hard c5f22aca0f3aa855daa159b2777472b35e721804
# Seed corpus
RUN mkdir -p /seeds /out && cp fuzz/bpf-object-fuzzer_seed_corpus.zip /out/ && \
    python3 -m zipfile -e /out/bpf-object-fuzzer_seed_corpus.zip /seeds/
COPY harness.c /src/harness.c
COPY build_project.sh /src/build_project.sh
