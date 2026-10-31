// =============================================================================
// 실습 2-B: 버튼으로 LED 밝기를 조절하는 프로그램 (소프트웨어 PWM)
//
// [PWM이란?]
//   GPIO 핀은 켜짐(HIGH)과 꺼짐(LOW) 두 가지만 할 수 있어서, 원래는 "반쯤 밝게"가
//   불가능하다. 그런데 눈이 알아채지 못할 만큼 빠르게 깜빡이면 눈에는 평균 밝기로
//   보인다. 이렇게 켜져 있는 시간의 비율로 밝기를 만드는 방법이 PWM이다.
//
// [듀티비(duty cycle)]
//   한 주기 중 켜져 있는 시간의 비율이다.
//     듀티비  25% : ■□□□  -> 어둡게 보임
//     듀티비  75% : ■■■□  -> 밝게 보임
//     듀티비 100% : ■■■■  -> 가장 밝음
//
// [회로]
//   - LED : GPIO17 -> 330옴 저항 -> LED -> GND
//   - 버튼: GPIO24 -> 버튼 -> GND, 그리고 GPIO24와 3.3V 사이에 10k옴 풀업 저항
//     (2-A와 달리 버튼 2번인 GPIO24를 쓴다.)
//
// [동작]
//   버튼을 누를 때마다 밝기가 0 -> 25 -> 50 -> 75 -> 100 -> 0 % 순서로 바뀐다.
//
// [빌드] (src/lab_02 폴더에서, Pi에 libgpiod v2가 있을 때)
//   g++ -std=c++14 -Wall -Wextra 01_pwn_dimmer.cc -o pwm -lgpiod
// =============================================================================

#include <unistd.h>  // usleep(): 프로그램을 잠깐 쉬게 하는 함수

#include <cstdio>  // printf(): 화면에 글자를 출력하는 함수
#include <ctime>   // clock_gettime(): 현재 시각을 읽는 함수

#include "../common/gpio.hpp"         // GPIO 핀을 쉽게 쓰게 해 주는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr unsigned int kLedPin = 17;     // LED가 연결된 핀 (BCM 17)
    constexpr unsigned int kButtonPin = 24;  // 버튼 2번이 연결된 핀 (BCM 24)
    constexpr long kSettleMs = 50;           // 디바운스 시간 (2-A와 같은 규칙)

    // 한 주기를 100칸으로 나누고, 한 칸을 100마이크로초로 잡았다.
    // 그러면 한 주기는 100 x 100us = 10ms이고, 1초에 약 100번 반복된다(100Hz).
    // 실제로는 GPIO 호출과 운영체제 스케줄링 때문에 이보다 조금 더 느리다.
    constexpr int kPwmSteps = 100;
    constexpr int kStepUs = 100;

    // 10칸마다(약 1ms마다) 한 번씩 버튼을 확인한다.
    constexpr int kSampleSteps = 10;

    // 버튼을 한 번 누를 때 올라가는 밝기다. (0, 25, 50, 75, 100 퍼센트)
    constexpr int kBrightnessStep = 25;

    // 현재 시각을 밀리초(1/1000초) 단위로 돌려준다.
    long NowMillis() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
    }

    // 이번 칸(step)에서 LED를 켜야 하는지 알려 준다.
    // 예) 칸이 100개이고 듀티비가 25%이면 0~24번 칸은 켜고, 나머지는 끈다.
    // 슬라이드는 step < duty_percent 인데, 칸이 100개일 때만 맞는 식이다.
    // 과제 2처럼 kPwmSteps를 바꿔도 밝기가 맞도록 칸 수를 함께 계산했다.
    // (칸이 100개이면 슬라이드의 식과 결과가 완전히 같다.)
    bool PwmLevel(int step, int duty_percent) {
        return step * 100 < duty_percent * kPwmSteps;
    }

    // 다음 밝기를 계산한다. 100 다음에는 다시 0으로 돌아간다.
    //   (100 + 25) % 125 = 0  <- %는 나머지 연산
    // 이 함수와 PwmLevel은 하드웨어를 건드리지 않아서 PC에서도 시험할 수 있다.
    int NextBrightness(int duty_percent) {
        return (duty_percent + kBrightnessStep) % (100 + kBrightnessStep);
    }
}  // namespace

int main() {
    // 핀을 사용하겠다고 요청한다. LED는 꺼진 상태로 시작한다.
    Gpio led(kLedPin, Gpio::kOutputLow);
    Gpio button(kButtonPin, Gpio::kInputPullUp);

    if (!InstallStopHandler() || !led.ok() || !button.ok()) {
        std::fprintf(stderr, "cannot claim GPIO%u/%u\n", kLedPin, kButtonPin);
        return 1;
    }

    int duty = 0;                 // 현재 밝기(%). 꺼진 상태에서 시작한다.
    bool armed = true;            // 다음 누름을 받을 준비가 되어 있는가?
    long last_low = NowMillis();  // 버튼이 마지막으로 눌려 있던 시각

    std::printf("Press the button on GPIO%u. Ctrl-C to stop\n", kButtonPin);
    std::fflush(stdout);

    while (!StopRequested()) {
        // 이 for 문 한 바퀴가 PWM의 "한 주기"다.
        for (int step = 0; step < kPwmSteps; ++step) {
            // 이번 칸에 맞게 LED를 켜거나 끈다.
            if (!led.Set(PwmLevel(step, duty) ? 1 : 0)) return 1;

            // 버튼도 이 반복문 "안에서" 확인한다.
            // 그래야 버튼을 확인하는 동안에도 LED 밝기가 흔들리지 않는다.
            if (step % kSampleSteps == 0) {
                const long now = NowMillis();
                const int level = button.Get();

                if (level == 0) {  // 버튼이 눌려 있다 (풀업이라 눌리면 0)
                    last_low = now;
                    if (armed) {
                        armed = false;  // 손을 뗄 때까지 다시 받지 않는다.
                        duty = NextBrightness(duty);
                        std::printf("brightness %d%%\n", duty);
                        std::fflush(stdout);
                    }
                } else if (now - last_low >= kSettleMs) {
                    armed = true;  // 50ms 동안 떼어져 있었으니 다시 장전한다.
                }
            }

            usleep(kStepUs);  // 한 칸의 길이만큼 기다린다.
        }
    }

    led.Set(0);  // 끝내기 전에 LED를 끈다.
    return 0;
}
