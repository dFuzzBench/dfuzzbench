FROM local/dfuzzbench-aflgo:base
RUN apt-get update && apt-get install -y automake autoconf libtool pkg-config \
    python3-docutils python3-sphinx libedit-dev libpcre2-dev libncurses-dev \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/varnishcache/varnish-cache /src/varnish-cache \
    && ln -s /src/varnish-cache /src/varnish
WORKDIR /src/varnish-cache
RUN git fetch --depth=1 origin 41059cf6815d0f366a5145fdb69d310e37e6d380 \
    && git reset --hard 41059cf6815d0f366a5145fdb69d310e37e6d380
RUN mkdir -p /seeds
COPY harness.c /src/harness.c
COPY build_project.sh /src/build_project.sh
