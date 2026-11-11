#!/usr/bin/env bash
# =============================================================================
# 라즈베리파이에서 실습 코드를 직접 빌드할 수 있게 준비하는 스크립트
#
# 하는 일:
#   1) 컴파일러(g++), make, pkg-config 설치
#   2) libgpiod 버전 확인
#      - v2가 이미 있으면 그대로 사용한다.
#      - v1(Pi OS Bookworm 기본)뿐이면 v2 소스를 받아 /usr/local에 따로 설치한다.
#        시스템의 v1은 지우지 않으므로 gpiodetect 같은 기존 명령은 그대로 동작한다.
#
# 사용법 (Pi에서, src 폴더 안에서):
#   bash setup_pi.sh
# =============================================================================
set -euo pipefail  # 중간에 하나라도 실패하면 바로 멈춘다.

LIBGPIOD_VERSION=2.2.1
PREFIX=/usr/local

echo "[1/3] 빌드 도구 설치"
sudo apt-get update
sudo apt-get install -y g++ make pkg-config curl xz-utils ca-certificates

# 지금 쓸 수 있는 libgpiod 버전을 확인한다. /usr/local에 설치한 것도 함께 찾는다.
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
current=$(pkg-config --modversion libgpiod 2>/dev/null || echo "없음")
echo "[2/3] 현재 libgpiod: ${current}"

if [[ "${current}" == 2.* ]]; then
    echo "libgpiod v2가 이미 있습니다. 설치를 건너뜁니다."
else
    echo "[3/3] libgpiod ${LIBGPIOD_VERSION} 소스 빌드 (몇 분 걸립니다)"
    work=$(mktemp -d)
    curl -fsSL "https://mirrors.edge.kernel.org/pub/software/libs/libgpiod/libgpiod-${LIBGPIOD_VERSION}.tar.xz" \
        | tar -xJ -C "${work}"
    (
        cd "${work}/libgpiod-${LIBGPIOD_VERSION}"
        # 정적 라이브러리(.a)만 만든다. 만든 프로그램에 라이브러리가 들어가서,
        # 실행할 때 시스템의 v1과 섞일 일이 없다.
        ./configure --prefix="${PREFIX}" --enable-static --disable-shared
        make -j"$(nproc)"
        sudo make install
    )
    rm -rf "${work}"
fi

echo
echo "완료: libgpiod $(pkg-config --modversion libgpiod)"
echo "이제 lab_02 폴더에서 make 를 실행하세요."
