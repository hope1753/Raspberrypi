#syntax=docker/dockerfile:1
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# 1. arm64 아키텍처 추가
RUN dpkg --add-architecture arm64

# 2. Ubuntu 24.04 (deb822 포맷) 저장소 분리 설정
# amd64 패키지는 archive.ubuntu.com에서, arm64 패키지는 ports.ubuntu.com에서 다운로드하도록 지정
RUN sed -i 's/Types: deb/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources && \
    echo "" >> /etc/apt/sources.list.d/ubuntu.sources && \
    echo "Types: deb" >> /etc/apt/sources.list.d/ubuntu.sources && \
    echo "URIs: http://ports.ubuntu.com/ubuntu-ports/" >> /etc/apt/sources.list.d/ubuntu.sources && \
    echo "Suites: noble noble-updates noble-backports noble-security" >> /etc/apt/sources.list.d/ubuntu.sources && \
    echo "Components: main restricted universe multiverse" >> /etc/apt/sources.list.d/ubuntu.sources && \
    echo "Architectures: arm64" >> /etc/apt/sources.list.d/ubuntu.sources

# 3. 빌드 도구 및 라즈베리파이용(arm64) 블루투스 라이브러리 설치
RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    cmake \
    file \
    g++-aarch64-linux-gnu \
    make \
    ninja-build \
    pkg-config \
    libbluetooth-dev:arm64 \
    curl \
    ca-certificates \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# 4. libgpiod v2 (arm64) 소스 빌드
# Ubuntu 24.04 패키지는 v1(1.6.3)이라 v2 API를 쓰는 src/common/gpio.hpp를 컴파일할 수 없음.
# 정적 라이브러리로 링크하므로 Pi에 설치된 libgpiod 버전과 무관하게 실행됨.
ARG LIBGPIOD_VERSION=2.2.1
RUN curl -fsSL https://mirrors.edge.kernel.org/pub/software/libs/libgpiod/libgpiod-${LIBGPIOD_VERSION}.tar.xz | tar -xJ -C /tmp && \
    cd /tmp/libgpiod-${LIBGPIOD_VERSION} && \
    ./configure --host=aarch64-linux-gnu --prefix=/usr/aarch64-linux-gnu \
        --enable-static --disable-shared \
        ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes && \
    make -j"$(nproc)" install && \
    rm -rf /tmp/libgpiod-${LIBGPIOD_VERSION}

WORKDIR /src

# 5. 크로스 컴파일 실행 명령어
CMD cmake -S /src -B /out \
    -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /out --parallel