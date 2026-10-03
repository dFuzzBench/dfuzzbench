FROM local/dfuzzbench-aflgo:base
# Install exiv2 deps manually (avoid install_dependencies.sh which may override LLVM)
RUN apt-get update && apt-get install -y \
    libexpat1-dev zlib1g-dev libssh-dev libcurl4-openssl-dev \
    libgtest-dev libgmock-dev libxml2-utils \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/Exiv2/exiv2 /src/exiv2
WORKDIR /src/exiv2
RUN git fetch --depth=1 origin 66c3cda1835248df8ece24464b4546314d0233a7 \
    && git reset --hard 66c3cda1835248df8ece24464b4546314d0233a7
# Seed corpus from test data
RUN mkdir -p /seeds && for f in $(find test/data -type f -size -20k 2>/dev/null | head -100); do \
    cp "$f" /seeds/ 2>/dev/null || true; done
RUN mkdir -p /out && cp fuzz/exiv2.dict /out/fuzzer_instrumented.dict 2>/dev/null || true
COPY harness.cpp /src/harness.cpp
COPY build_project.sh /src/build_project.sh
