FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y libbrotli-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/yhirose/cpp-httplib.git /src/cpp-httplib
WORKDIR /src/cpp-httplib
RUN git fetch --depth=1 origin 4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d \
    && git reset --hard 4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d
RUN mkdir -p /seeds
COPY harness.cc /src/harness.cc
COPY build_project.sh /src/build_project.sh
