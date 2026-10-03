FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y build-essential && rm -rf /var/lib/apt/lists/*
# Install wasm-tools (binary only, no LLVM 15 needed)
ARG WASM_TOOLS_VER=1.205.0
RUN cd /opt && wget -q https://github.com/bytecodealliance/wasm-tools/releases/download/v${WASM_TOOLS_VER}/wasm-tools-${WASM_TOOLS_VER}-x86_64-linux.tar.gz \
    && tar -xf wasm-tools-${WASM_TOOLS_VER}-x86_64-linux.tar.gz \
    && mv wasm-tools-${WASM_TOOLS_VER}-x86_64-linux wasm-tools \
    && rm wasm-tools-${WASM_TOOLS_VER}-x86_64-linux.tar.gz \
    && ln -sf /opt/wasm-tools/wasm-tools /usr/local/bin/wasm-tools
RUN git clone --depth 1 https://github.com/bytecodealliance/wasm-micro-runtime /src/wamr
WORKDIR /src/wamr
RUN git fetch --depth=1 origin 0119b17526ae447c5c57784d9deb90c04af51fe5 \
    && git reset --hard 0119b17526ae447c5c57784d9deb90c04af51fe5
# Generate seed corpus (uses wasm-tools smith)
RUN mkdir -p /seeds && cd tests/fuzz/wasm-mutator-fuzz/ && \
    (./smith_wasm.sh 10 2>/dev/null && cp build/CORPUS_DIR/*.wasm /seeds/ 2>/dev/null) || \
    echo "AAAA" > /seeds/default_seed
COPY harness.cc /src/harness.cc
COPY build_project.sh /src/build_project.sh
