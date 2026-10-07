#syntax=docker/dockerfile:1
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    cmake \
    file \
    g++-aarch64-linux-gnu \
    make && rm -rf /var/lib/apt/lists/*

WORKDIR /src

CMD cmake -S /src -B /out \
    -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /out --parallel