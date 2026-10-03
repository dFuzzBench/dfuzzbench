FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y libpng-dev && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/google/guetzli /src/guetzli
WORKDIR /src/guetzli
RUN git fetch --depth=1 origin 214f2bb42abf5a577c079d00add5d6cc470620d3 \
    && git reset --hard 214f2bb42abf5a577c079d00add5d6cc470620d3
# Seed corpus (download AFL test cases)
RUN mkdir -p /seeds && cd /tmp && \
    curl -sL https://lcamtuf.coredump.cx/afl/demo/afl_testcases.tgz | tar -xz && \
    cp jpeg/full/images/* /seeds/ 2>/dev/null || true && \
    rm -rf /tmp/jpeg /tmp/gif /tmp/png
COPY harness.cc /src/harness.cc
COPY build_project.sh /src/build_project.sh
