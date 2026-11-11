# =============================================================================
# 실습 2 빌드 파일 (라즈베리파이에서 직접 빌드할 때 사용)
#
# 처음 한 번:  bash ../setup_pi.sh   (libgpiod v2 준비)
#
#   make          세 프로그램(led, pwm, traffic)을 모두 빌드
#   make led      2-A 버튼 LED만 빌드
#   make pwm      2-B PWM 밝기 조절만 빌드
#   make traffic  2-C 신호등 상태 기계만 빌드
#   make clean    빌드 결과 지우기
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

# 모든 프로그램이 함께 쓰는 헤더. 이 파일이 바뀌어도 다시 빌드한다.
COMMON := ../common/gpio.hpp ../common/signal_stop.hpp

PROGRAMS := led pwm traffic

all: $(PROGRAMS)

led: 00_led_button.cc $(COMMON) | check-gpiod
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS)

pwm: 01_pwn_dimmer.cc $(COMMON) | check-gpiod
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS)

traffic: 02_state_machine.cc $(COMMON) | check-gpiod
	$(CXX) $(CXXFLAGS) $(GPIOD_CFLAGS) $< -o $@ $(GPIOD_LIBS)

# gpio.hpp는 libgpiod v2 전용이라, v2가 없으면 알아보기 어려운 컴파일 에러가 수십 줄 나온다.
# 그 전에 원인을 한 줄로 알려 주고 멈춘다.
check-gpiod:
	@case "$(GPIOD_VERSION)" in \
	  2.*) echo "libgpiod $(GPIOD_VERSION) 사용" ;; \
	  *) echo "libgpiod v2가 필요합니다 (현재: $(or $(GPIOD_VERSION),없음)). 먼저 bash ../setup_pi.sh 를 실행하세요."; exit 1 ;; \
	esac

clean:
	rm -f $(PROGRAMS)

.PHONY: all check-gpiod clean
