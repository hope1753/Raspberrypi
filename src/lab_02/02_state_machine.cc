// =============================================================================
// 실습 2-C: 상태 기계로 만드는 보행자 신호등
//
// [상태 기계란?]
//   프로그램이 "지금 어떤 상태인가"를 하나의 변수로 기억하고, 정해진 규칙에 따라
//   다음 상태로 넘어가게 만드는 방법이다. 시간과 버튼이 섞인 동작도
//   플래그 변수를 여러 개 쓰지 않고 깔끔하게 정리할 수 있다.
//
// [이 프로그램의 상태]
//   상태     | 지속  | 빨강 LED | 초록 LED | 다음 상태
//   ---------+-------+----------+----------+----------
//   CAR GO   | 10초  | 꺼짐     | 켜짐     | WARN     (차량 통행)
//   WARN     |  3초  | 꺼짐     | 깜빡임   | WALK     (곧 바뀜 경고)
//   WALK     |  8초  | 켜짐     | 꺼짐     | CAR GO   (보행자 횡단)
//
// [보행자 버튼 규칙]
//   - 버튼은 CAR GO 구간만 앞당긴다. 누른 순간부터 최대 2초 뒤에 초록이 끝난다.
//     남은 시간이 이미 2초 이하이면 그대로 둔다.
//   - WALK 중에 누르면 요청을 기억했다가 다음 CAR GO에 적용한다.
//   - WARN 중에 누르면 곧 WALK가 오므로 이미 처리된 것으로 본다.
//   - 열 번 눌러도 한 번 누른 것과 같고, 다시 눌러도 2초가 늘어나지 않는다.
//
// [회로]
//   - 빨강 LED: GPIO17 -> 330옴 저항 -> LED -> GND
//   - 초록 LED: GPIO27 -> 330옴 저항 -> LED -> GND
//   - 버튼    : GPIO23 -> 버튼 -> GND, 그리고 GPIO23과 3.3V 사이에 10k옴 풀업 저항
//
// [빌드] (src/lab_02 폴더에서, Pi에 libgpiod v2가 있을 때)
//   g++ -std=c++14 -Wall -Wextra 02_state_machine.cc -o traffic -lgpiod
// =============================================================================

#include <unistd.h>  // usleep(): 프로그램을 잠깐 쉬게 하는 함수

#include <cstdio>  // printf(): 화면에 글자를 출력하는 함수
#include <ctime>   // clock_gettime(): 현재 시각을 읽는 함수

#include "../common/gpio.hpp"         // GPIO 핀을 쉽게 쓰게 해 주는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr unsigned int kRedPin = 17;     // 빨강 LED (BCM 17)
    constexpr unsigned int kGreenPin = 27;   // 초록 LED (BCM 27)
    constexpr unsigned int kButtonPin = 23;  // 보행자 버튼 (BCM 23)

    constexpr int kPollIntervalUs = 1000;  // 1밀리초마다 한 번씩 확인한다.
    constexpr long kSettleMs = 50;         // 디바운스 시간 (2-A와 같은 규칙)
    constexpr long kGraceMs = 2000;        // 버튼을 누른 뒤 초록을 유지할 최대 시간
    constexpr long kBlinkMs = 250;         // WARN에서 초록이 켜지고 꺼지는 간격

    // 상태에 이름을 붙인다. 내부적으로 kCarGo=0, kWarn=1, kWalk=2가 된다.
    // 맨 끝의 kStateCount는 값이 3이 되어 "상태가 모두 몇 개인지"를 나타낸다.
    enum State {
        kCarGo,
        kWarn,
        kWalk,
        kStateCount
    };

    // 상태 번호를 칸 번호로 써서 지속 시간과 이름을 찾는 표다.
    // 예) kDurationMs[kWarn]은 3000, kNames[kWarn]은 "WARN"이다.
    constexpr long kDurationMs[kStateCount] = {10000, 3000, 8000};
    const char* const kNames[kStateCount] = {"CAR GO", "WARN", "WALK"};

    // 현재 시각을 밀리초(1/1000초) 단위로 돌려준다.
    long NowMillis() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
    }

    // LED를 어떻게 켤지는 "상태"만 보고 결정한다.
    // 이렇게 하면 LED가 상태 순서와 어긋나는 일이 생기지 않는다.
    bool RedOn(int state) { return state == kWalk; }

    // 초록은 CAR GO에서 켜지고, WARN에서는 blink 값에 따라 깜빡인다.
    bool GreenOn(int state, bool blink) {
        return state == kCarGo || (state == kWarn && blink);
    }
}  // namespace

int main() {
    // 핀을 사용하겠다고 요청한다. LED는 모두 꺼진 상태로 시작한다.
    Gpio red(kRedPin, Gpio::kOutputLow);
    Gpio green(kGreenPin, Gpio::kOutputLow);
    Gpio button(kButtonPin, Gpio::kInputPullUp);

    if (!InstallStopHandler() || !red.ok() || !green.ok() || !button.ok()) {
        std::fprintf(stderr, "cannot claim GPIO%u/%u/%u\n", kRedPin, kGreenPin, kButtonPin);
        return 1;
    }

    int state = kCarGo;                                // 현재 상태
    long deadline = NowMillis() + kDurationMs[state];  // 이 시각이 되면 다음 상태로
    bool requested = false;                            // 보행자 요청이 걸려 있는가?
    bool armed = true;                                 // 다음 누름을 받을 준비가 됐는가?
    long last_low = NowMillis();                       // 버튼이 마지막으로 눌려 있던 시각

    std::printf("-> %s\n", kNames[state]);
    std::fflush(stdout);

    while (!StopRequested()) {
        const long now = NowMillis();

        // 1) 버튼 읽기 (2-A와 같은 디바운스 규칙)
        const int level = button.Get();
        if (level < 0) break;
        if (level == 0) {  // 버튼이 눌려 있다 (풀업이라 눌리면 0)
            last_low = now;
            if (armed) {
                armed = false;
                requested = true;  // 보행자 요청을 기억해 둔다.
                // 과제 4: 어느 상태에서 눌렀는지 로그로 확인할 수 있다.
                std::printf("button pressed during %s\n", kNames[state]);
                std::fflush(stdout);
            }
        } else if (now - last_low >= kSettleMs) {
            armed = true;
        }

        // 2) 이벤트 전이: 요청이 있으면 CAR GO의 끝나는 시각을 앞당긴다.
        //    끝나는 시각은 앞으로만 당겨지고 뒤로 밀리지 않는다. 그래서 여러 번
        //    눌러도 2초가 늘어나지 않고, 한 번 누른 것과 같다.
        if (requested && state == kCarGo && now + kGraceMs < deadline) {
            deadline = now + kGraceMs;
        }

        // 3) 시간 전이: 끝나는 시각이 지났으면 다음 상태로 넘어간다.
        //    반드시 2)번 뒤에 있어야 한다. 순서가 바뀌면 요청이 한 회차 늦게 반영된다.
        if (now >= deadline) {
            // % 연산으로 CAR GO -> WARN -> WALK -> CAR GO 순서가 반복된다.
            state = (state + 1) % kStateCount;
            deadline = now + kDurationMs[state];

            // WALK가 왔다는 건 횡단 요청이 처리됐다는 뜻이니 요청을 지운다.
            if (state == kWalk) requested = false;

            std::printf("-> %s\n", kNames[state]);
            std::fflush(stdout);
        }

        // 4) 출력: 현재 상태에 맞게 LED를 켠다.
        //    시각을 250ms로 나눈 몫이 짝수일 때와 홀수일 때를 번갈아 깜빡임을 만든다.
        const bool blink = (now / kBlinkMs) % 2 == 0;
        if (!red.Set(RedOn(state) ? 1 : 0) || !green.Set(GreenOn(state, blink) ? 1 : 0)) {
            break;
        }

        usleep(kPollIntervalUs);
    }

    // 끝내기 전에 LED를 모두 끈다.
    red.Set(0);
    green.Set(0);
    return 0;
}
