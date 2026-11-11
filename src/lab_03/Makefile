# =============================================================================
# 실습 3 빌드 파일 (라즈베리파이에서 직접 빌드할 때 사용)
#
# 처음 한 번:  bash ../setup_pi.sh                 (libgpiod v2 준비)
#             sudo apt install -y liblgpio-dev     (초음파 센서용 lgpio)
#
#   make           세 프로그램(dht, light, distance)을 모두 빌드
#   make dht       3-A DHT11 온습도만 빌드        (libgpiod 필요)
#   make light     3-B CdS 조도만 빌드            (추가 라이브러리 없음)
#   make distance  3-C HC-SR04 거리만 빌드        (libgpiod + lgpio 필요)
#   make clean     빌드 결과 지우기
#
# 소스를 고친 뒤 make를 다시 실행하면, 바뀐 파일만 다시 빌드한다.
# =============================================================================

CXX      ?= g++
CXXFLAGS := -std=c++14 -Wall -Wextra

# setup_pi.sh가 libgpiod v2를 /usr/local에 설치했다면 그쪽을 먼저 찾는다.
# (export로 넘기면 make 4.3 이하에서는 $(shell)에 전달되지 않아서 명령 앞에 직접 붙인다.)
PKG_CONFIG    := PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$$PKG_CONFIG_PATH pkg-config
GPIOD_VERSION := $(shell $(PKG_CONFIG) --modversion libgpiod 2>/dev/null)
GPIOD_CFLAGS  := $(shell $(PKG_CONFIG) --cflags libgpiod 2>/dev/null)
GPIOD_LIBS    := $(shell $(PKG_CONFIG) --libs libgpiod 2>/dev/null)

GPIO_HEADER := ../common/gpio.hpp
STOP_HEADER := ../common/signal_stop.hpp

PROGRAMS := dht light distance

all: $(PROGRAMS)

# 3-A: DHT11은 Gpio 클래스(libgpiod)로 비트뱅잉한다.
dht: 00_dht11_read.cc $(GPIO_HEADER) $(STOP_HEADER) | check-gpiod
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS)

# 3-B: SPI는 리눅스 커널 기능(/dev/spidev0.0)만 써서 링크할 라이브러리가 없다.
light: 01_cds_light.cc $(STOP_HEADER)
	$(CXX) $(CXXFLAGS) $< -o $@

# 3-C: 초음파는 lgpio로, 부저는 Gpio 클래스(libgpiod)로 제어해서 둘 다 필요하다.
#      (슬라이드에는 -llgpio만 있지만 그러면 링크 에러가 난다.)
distance: 02_distance.cc $(GPIO_HEADER) $(STOP_HEADER) | check-gpiod check-lgpio
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS) -llgpio

# 필요한 라이브러리가 없으면 알아보기 어려운 에러 대신 한 줄로 알려 주고 멈춘다.
check-gpiod:
	@case "$(GPIOD_VERSION)" in \
	  2.*) echo "libgpiod $(GPIOD_VERSION) 사용" ;; \
	  *) echo "libgpiod v2가 필요합니다 (현재: $(or $(GPIOD_VERSION),없음)). 먼저 bash ../setup_pi.sh 를 실행하세요."; exit 1 ;; \
	esac

check-lgpio:
	@echo '#include <lgpio.h>' | $(CXX) -E -x c++ - >/dev/null 2>&1 || \
	  { echo "lgpio가 필요합니다. sudo apt install -y liblgpio-dev 를 실행하세요."; exit 1; }

clean:
	rm -f $(PROGRAMS)

.PHONY: all check-gpiod check-lgpio clean
