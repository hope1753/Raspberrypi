# =============================================================================
# 실습 5 빌드 파일 (라즈베리파이에서 직접 빌드할 때 사용)
#
# 처음 한 번:  sudo apt install -y libasound2-dev alsa-utils
#             (ALSA 개발 파일, 그리고 arecord/aplay/alsamixer 명령)
#
#   make             두 프로그램(visualizer, wavrec)을 모두 빌드
#   make visualizer  5-A 레벨 미터만 빌드
#   make wavrec      5-B WAV 녹음만 빌드
#   make clean       빌드 결과 지우기
#
# 이 실습은 GPIO를 쓰지 않아서 libgpiod가 필요 없다.
# =============================================================================

CXX      ?= g++
CXXFLAGS := -std=c++14 -Wall -Wextra

STOP_HEADER := ../common/signal_stop.hpp
PROGRAMS := visualizer wavrec

all: $(PROGRAMS)

# -lasound: ALSA 라이브러리를 링크한다.
visualizer: 00_audio_visualizer.cc $(STOP_HEADER) | check-alsa
	$(CXX) $(CXXFLAGS) $< -o $@ -lasound

wavrec: 01_wav_record.cc $(STOP_HEADER) | check-alsa
	$(CXX) $(CXXFLAGS) $< -o $@ -lasound

# ALSA 개발 파일이 없으면 알아보기 어려운 에러 대신 한 줄로 알려 주고 멈춘다.
check-alsa:
	@echo '#include <alsa/asoundlib.h>' | $(CXX) -E -x c++ - >/dev/null 2>&1 || \
	  { echo "ALSA 개발 파일이 필요합니다. sudo apt install -y libasound2-dev 를 실행하세요."; exit 1; }

clean:
	rm -f $(PROGRAMS)

.PHONY: all check-alsa clean
