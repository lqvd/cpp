ARG SYSROOT_VERSION=0.8.0
FROM ghcr.io/faasm/cpp-sysroot:${SYSROOT_VERSION}

SHELL ["/bin/bash", "-c"]
ENV CPP_DOCKER="on"
ENV TERM=xterm-256color

ARG CPP_REPO=https://github.com/lqvd/cpp
ARG CPP_BRANCH=main

WORKDIR /

RUN apt update && apt install -y \
    protobuf-compiler \
    libprotobuf-dev \
    libprotoc-dev \
    && rm -rf /var/lib/apt/lists/*

# Replace upstream cpp source with your fork.
RUN cd / \
    && rm -rf /code/cpp \
    && mkdir -p /code \
    && git clone -b ${CPP_BRANCH} ${CPP_REPO} /code/cpp \
    && cd /code/cpp \
    && git submodule update --init -f third-party/protobuf \
    && git submodule update --init -f third-party/protogen-faabric \
    && git submodule update --init -f third-party/faabric \
    && git submodule update --init -f third-party/faasm-clapack \
    && git submodule update --init -f third-party/libffi \
    && git submodule update --init -f third-party/wasi-libc \
    && git submodule update --init -f third-party/zlib

WORKDIR /code/cpp

RUN ./bin/create_venv.sh

RUN source venv/bin/activate \
    && inv libprotobuf.build \
    && inv pb.build-plugin

# Build all the targets
RUN cd /code/cpp \
    && source venv/bin/activate \
    # Build native Faasm libraries (static and shared)
    && inv \
        libfaasrpc --native --shared \
    # Build Faasm WASM libraries for wasm32-wasi-threads target
    && inv \
        libfaasrpc \
    # Build the protobuf
    && inv \
        libprotobuf \
    # Build the protoc plugin
    && inv \
        pb.build-plugin

RUN echo ". /code/cpp/bin/workon.sh" >> ~/.bashrc

CMD ["/bin/bash", "-l"]
