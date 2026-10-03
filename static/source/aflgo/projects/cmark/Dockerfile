FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y python3 && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/commonmark/cmark.git /src/cmark
WORKDIR /src/cmark
RUN git fetch --depth=1 origin 9d74662f4b12f0c7c2f610231a47348fe8b7b619 \
    && git reset --hard 9d74662f4b12f0c7c2f610231a47348fe8b7b619
# Seed corpus
RUN mkdir -p /seeds && python3 test/spec_tests.py --fuzz-corpus /seeds --spec test/spec.txt 2>/dev/null || true
RUN python3 test/spec_tests.py --fuzz-corpus /seeds --spec test/regression.txt 2>/dev/null || true
RUN mkdir -p /out && cp fuzz/dictionary /out/cmark.dict
COPY harness.c /src/harness.c
COPY build_project.sh /src/build_project.sh
