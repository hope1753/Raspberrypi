# =============================================================================
# 실습 4 빌드 파일 (라즈베리파이에서 직접 빌드할 때 사용)
#
# 처음 한 번:  bash ../setup_pi.sh   (libgpiod v2 준비)
#             sudo apt install -y liblgpio-dev   (초음파 센서용 lgpio)
#
#   make        센서 통합 웹 서버(hub) 빌드
#   make clean  빌드 결과 지우기
# =============================================================================

CXX      ?= g++
CXXFLAGS := -std=c++14 -Wall -Wextra

# setup_pi.sh가 libgpiod v2를 /usr/local에 설치했다면 그쪽을 먼저 찾는다.
# (export로 넘기면 make 4.3 이하에서는 $(shell)에 전달되지 않아서 명령 앞에 직접 붙인다.)
PKG_CONFIG    := PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$$PKG_CONFIG_PATH pkg-config
GPIOD_VERSION := $(shell $(PKG_CONFIG) --modversion libgpiod 2>/dev/null)
GPIOD_CFLAGS  := $(shell $(PKG_CONFIG) --cflags libgpiod 2>/dev/null)
GPIOD_LIBS    := $(shell $(PKG_CONFIG) --libs libgpiod 2>/dev/null)

HEADERS := sensors.hpp http.hpp ../common/gpio.hpp ../common/signal_stop.hpp

all: hub

# -pthread: std::thread를 쓰기 위한 옵션
hub: 00_sensor_hub.cc $(HEADERS) | check-libs
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS) -llgpio -pthread

# 필요한 라이브러리가 없으면 알아보기 어려운 에러 대신 한 줄로 알려 주고 멈춘다.
check-libs:
	@case "$(GPIOD_VERSION)" in \
	  2.*) echo "libgpiod $(GPIOD_VERSION) 사용" ;; \
	  *) echo "libgpiod v2가 필요합니다 (현재: $(or $(GPIOD_VERSION),없음)). 먼저 bash ../setup_pi.sh 를 실행하세요."; exit 1 ;; \
	esac
	@echo '#include <lgpio.h>' | $(CXX) -E -x c++ - >/dev/null 2>&1 || \
	  { echo "lgpio가 필요합니다. sudo apt install -y liblgpio-dev 를 실행하세요."; exit 1; }

clean:
	rm -f hub

.PHONY: all check-libs clean
