// =============================================================================
// 실습 2-A: 버튼을 누를 때마다 LED를 켜고 끄는 프로그램
//
// [배우는 것]
//   - 디지털 출력: 핀을 HIGH(3.3V) 또는 LOW(0V)로 만들어 LED를 켜고 끈다.
//   - 디지털 입력: 핀의 전압을 읽어서 버튼이 눌렸는지 알아낸다.
//   - 디바운스: 버튼을 한 번 눌렀는데 여러 번 눌린 것처럼 읽히는 문제를 막는다.
//
// [회로]
//   - LED : GPIO17 -> 330옴 저항 -> LED -> GND
//   - 버튼: GPIO23 -> 버튼 -> GND, 그리고 GPIO23과 3.3V 사이에 10k옴 풀업 저항
//
// [풀업이란?]
//   저항으로 핀을 3.3V 쪽에 살짝 연결해 두는 방식이다.
//   그래서 평소에는 HIGH(1)이고, 버튼을 누르면 GND와 연결되어 LOW(0)가 된다.
//   즉, "누르면 1"이 아니라 "누르면 0"이다. 처음에 가장 많이 헷갈리는 부분이다.
//
// [빌드] (src/lab_02 폴더에서, Pi에 libgpiod v2가 있을 때)
//   g++ -std=c++14 -Wall -Wextra 00_led_button.cc -o led -lgpiod
// =============================================================================

#include <unistd.h>  // usleep(): 프로그램을 잠깐 쉬게 하는 함수

#include <cstdio>  // printf(): 화면에 글자를 출력하는 함수
#include <ctime>   // clock_gettime(): 현재 시각을 읽는 함수

#include "../common/gpio.hpp"         // GPIO 핀을 쉽게 쓰게 해 주는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

// 이 파일 안에서만 쓰는 상수와 함수를 모아 두는 곳이다.
namespace {
    // constexpr은 "절대 바뀌지 않는 값"이라는 뜻이다.
    // 핀 번호는 BCM 번호다. 보드에 적힌 물리적인 핀 순서(1~40번)와 다르니 주의!
    constexpr unsigned int kLedPin = 17;     // LED가 연결된 핀 (BCM 17)
    constexpr unsigned int kButtonPin = 23;  // 버튼이 연결된 핀 (BCM 23)

    // 버튼을 확인하는 간격이다. 1000마이크로초 = 1밀리초 = 0.001초.
    // 사람이 버튼을 누르는 시간보다 훨씬 짧아서 놓칠 일이 없다.
    constexpr int kPollIntervalUs = 1000;

    // 버튼에서 손을 뗀 뒤, 이 시간(50밀리초) 동안 계속 HIGH여야
    // "다음 누름을 받을 준비가 됐다"고 판단한다.
    // 과제 1: 이 값을 0으로 바꾸면 한 번 눌러도 여러 번 토글되는 것을 볼 수 있다.
    constexpr long kSettleMs = 50;

    // 현재 시각을 밀리초(1/1000초) 단위로 돌려준다.
    // CLOCK_MONOTONIC은 항상 앞으로만 가는 시계라서 시간 간격을 재기에 알맞다.
    long NowMillis() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
    }
}  // namespace

int main() {
    // 1단계: 핀을 사용하겠다고 운영체제에 요청한다.
    //   led   : 출력으로 쓰고, 처음에는 LOW(꺼짐)로 시작한다.
    //   button: 입력으로 쓰고, 내부 풀업도 함께 켠다.
    // 프로그램이 끝나면 Gpio의 소멸자가 핀을 자동으로 돌려준다.
    Gpio led(kLedPin, Gpio::kOutputLow);
    Gpio button(kButtonPin, Gpio::kInputPullUp);

    // 2단계: 준비가 잘 됐는지 확인한다. 하나라도 실패하면 에러를 출력하고 끝낸다.
    if (!InstallStopHandler() || !led.ok() || !button.ok()) {
        std::fprintf(stderr, "cannot claim GPIO%u/%u\n", kLedPin, kButtonPin);
        return 1;
    }

    // 3단계: 프로그램이 기억해야 할 값들을 준비한다.
    bool led_is_on = false;       // 지금 LED가 켜져 있는가?
    bool armed = true;            // 다음 누름을 받을 준비가 되어 있는가? (장전 상태)
    long last_low = NowMillis();  // 버튼이 마지막으로 눌려 있던(LOW) 시각

    std::printf("Press the button on GPIO%u. Ctrl-C to stop\n", kButtonPin);
    std::fflush(stdout);

    // 4단계: Ctrl+C를 누를 때까지 계속 반복한다.
    while (!StopRequested()) {
        const int level = button.Get();  // 버튼 핀의 값 읽기: 0, 1, 실패하면 -1
        if (level < 0) return 1;
        const long now = NowMillis();

        // [디바운스 규칙]
        // 버튼 접점은 붙는 순간 몇 밀리초 동안 붙었다 떨어졌다를 반복한다(바운싱).
        // 그래서 "누름은 바로 처리하고, 손을 떼고 50ms가 지나기 전에는 다시 받지 않는다."
        if (level == 0) {  // 버튼이 눌려 있다 (풀업이라 눌리면 0)
            last_low = now;  // 눌려 있는 동안 시각을 계속 갱신한다.

            // armed가 true일 때만 처리한다. 바운싱으로 또 0이 읽혀도 무시된다.
            if (armed) {
                armed = false;           // 손을 뗄 때까지 다시 받지 않도록 잠근다.
                led_is_on = !led_is_on;  // 켜져 있으면 끄고, 꺼져 있으면 켠다(토글).

                if (!led.Set(led_is_on ? 1 : 0)) return 1;  // 실제로 LED에 반영
                std::printf("LED %s\n", led_is_on ? "on" : "off");
                std::fflush(stdout);  // 글자가 화면에 바로 나오도록 밀어낸다.
            }
        } else if (now - last_low >= kSettleMs) {
            // 버튼이 떼어진 지 50ms가 넘었다. 이제 진짜로 뗀 것이니 다시 장전한다.
            armed = true;
        }

        usleep(kPollIntervalUs);  // 잠깐 쉬어서 CPU를 100% 쓰지 않게 한다.
    }

    // 5단계: 끝내기 전에 LED를 끈다. 핀은 Gpio 소멸자가 자동으로 돌려준다.
    led.Set(0);
    return 0;
}
