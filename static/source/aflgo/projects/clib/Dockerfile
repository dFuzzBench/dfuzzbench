FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y libcurl4-gnutls-dev && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/clibs/clib /src/clib
WORKDIR /src/clib
RUN git fetch --depth=1 origin 6d96e53349784087d1f0c13aca2c9b6cdf3f4c4c \
    && git reset --hard 6d96e53349784087d1f0c13aca2c9b6cdf3f4c4c
RUN mkdir -p /seeds
COPY harness.c /src/harness.c
COPY build_project.sh /src/build_project.sh
