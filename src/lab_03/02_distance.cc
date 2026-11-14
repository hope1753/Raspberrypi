// =============================================================================
// 실습 3-C: HC-SR04 초음파 센서로 거리 재기 (펄스 폭 측정)
//
// [거리를 재는 원리]
//   박쥐처럼 초음파를 쏘고, 물체에 부딪혀 돌아올 때까지 걸린 시간을 잰다.
//     1) TRIG 핀에 10마이크로초짜리 펄스를 보낸다. ("측정 시작!")
//     2) 센서가 초음파를 쏘고, ECHO 핀을 HIGH로 만든다.
//     3) 반사파가 돌아오면 ECHO 핀이 LOW가 된다.
//   즉, ECHO가 HIGH인 시간이 초음파가 왕복한 시간이다.
//     거리(cm) = HIGH 시간(us) x 0.0343(cm/us) / 2
//   0.0343은 20도에서의 음속이고, 왕복 거리이므로 2로 나눈다.
//
// [이 프로그램의 경고 기능]
//   물체가 10cm보다 가까우면 부저를 울린다.
//
// [회로] ※ 주의: ECHO를 GPIO에 바로 연결하면 안 된다!
//   HC-SR04의 ECHO는 5V를 내보낸다. GPIO는 3.3V까지만 견디므로 Pi가 망가질 수 있다.
//   1k옴과 2k옴 저항으로 전압을 약 3.3V로 낮춰서 연결해야 한다.
//     ECHO -- [1k옴] -- GPIO24 -- [2k옴] -- GND
//   - VCC -> 5V, GND -> GND, TRIG -> GPIO23
//   - 능동 부저 -> GPIO18, GND
//
// [빌드] (src/lab_03 폴더에서, Pi에 libgpiod v2와 liblgpio-dev가 있을 때)
//   g++ -std=c++14 -Wall -Wextra 02_distance.cc -o distance -lgpiod -llgpio
//   슬라이드에는 -llgpio만 있지만, 부저에 쓰는 Gpio 클래스가 libgpiod를 쓰므로
//   -lgpiod도 함께 넣어야 링크된다.
// =============================================================================

#include <lgpio.h>   // lgpio: 정밀한 시간 측정에 쓰는 GPIO 라이브러리
#include <unistd.h>  // usleep(): 프로그램을 잠깐 쉬게 하는 함수

#include <cstdint>  // std::uint64_t: 아주 큰 수를 담는 정수
#include <cstdio>   // printf(): 화면에 글자를 출력하는 함수

#include "../common/gpio.hpp"         // 부저에 쓰는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능
#include "distance_stats.hpp"         // 03_distance_alert.cc와 같은 기준의 통계, CPU 점유율

namespace {
    constexpr int kChipDevice = 0;           // GPIO 칩 번호 (/dev/gpiochip0)
    constexpr int kTrigPin = 23;             // TRIG (BCM 23)
    constexpr int kEchoPin = 24;             // ECHO (BCM 24), 1k/2k 분압 회로를 거쳐서 연결
    constexpr unsigned int kBuzzerPin = 18;  // 능동 부저 (BCM 18)
    constexpr float kWarnCm = 10.0f;         // 이보다 가까우면 부저를 울린다.

    constexpr long kStartTimeoutUs = 60000;   // ECHO가 HIGH가 되기를 기다리는 최대 시간
    constexpr long kEchoTimeoutUs = 30000;    // ECHO가 LOW가 되기를 기다리는 최대 시간
    constexpr float kSpeedCmPerUs = 0.0343f;  // 20도 공기 중 음속 (cm/마이크로초)
}  // namespace

// 핀이 원하는 값(level)이 될 때까지 기다리고, 걸린 시간(마이크로초)을 돌려준다.
// 시간이 초과되거나 읽기에 실패하면 -1을 돌려준다.
// 주의: lguTimestamp()는 나노초 단위라서 1000으로 나눠 마이크로초로 바꾼다.
long WaitFor(int handle, int gpio, int level, long timeout_us) {
    const std::uint64_t start = lguTimestamp();
    for (;;) {
        const int value = lgGpioRead(handle, gpio);
        if (value < 0) return -1;
        const long elapsed = static_cast<long>((lguTimestamp() - start) / 1000);
        if (value == level) return elapsed;
        if (elapsed > timeout_us) return -1;
    }
}

// 측정에 실패했을 때 돌려주는 값. 어느 단계에서 실패했는지 구별해서 원인을 찾기 쉽게 한다.
constexpr long kEchoBusy = -1;      // 측정 전부터 ECHO가 HIGH로 붙어 있음
constexpr long kTrigFailed = -2;    // TRIG 핀에 펄스를 보내지 못함
constexpr long kNoEchoStart = -3;   // 60ms를 기다려도 ECHO가 HIGH가 되지 않음
constexpr long kEchoTooLong = -4;   // ECHO가 30ms 넘게 HIGH로 남아 있음 (반사파가 안 돌아옴)

// 실패 값을 사람이 읽을 수 있는 설명으로 바꾼다.
const char* DescribeFailure(long code) {
    switch (code) {
        case kEchoBusy:    return "ECHO is already HIGH before triggering -- check the divider and that nothing else drives GPIO24";
        case kTrigFailed:  return "cannot drive TRIG -- check GPIO23";
        case kNoEchoStart: return "ECHO never went HIGH -- check the 5V feed, TRIG wiring and the divider direction";
        case kEchoTooLong: return "ECHO stayed HIGH too long -- nothing within about 5 m, or the target absorbs sound";
        default:           return "unknown";
    }
}

// 초음파를 한 번 쏘고 ECHO가 HIGH인 시간을 마이크로초로 돌려준다. 실패하면 위의 음수 값.
long MeasureEchoUs(int handle) {
    // 측정 전에는 ECHO가 LOW여야 한다. 이전 측정이 아직 끝나지 않았다면 포기한다.
    if (lgGpioRead(handle, kEchoPin) != 0) return kEchoBusy;

    // TRIG를 10마이크로초 동안 HIGH로 만든다. 센서가 받아들이는 최소 길이다.
    if (lgGpioWrite(handle, kTrigPin, 1) != LG_OKAY) return kTrigFailed;
    lguSleep(0.00001);
    if (lgGpioWrite(handle, kTrigPin, 0) != LG_OKAY) return kTrigFailed;

    // ECHO가 HIGH가 될 때까지 기다린 뒤, 다시 LOW가 될 때까지의 시간을 잰다.
    if (WaitFor(handle, kEchoPin, 1, kStartTimeoutUs) < 0) return kNoEchoStart;
    const long echo_us = WaitFor(handle, kEchoPin, 0, kEchoTimeoutUs);
    return echo_us < 0 ? kEchoTooLong : echo_us;
}

// ECHO 시간을 거리(cm)로 바꾼다. 왕복 시간이므로 2로 나눠 편도 거리를 구한다.
float ToCm(long echo_us) {
    return (static_cast<float>(echo_us) * kSpeedCmPerUs) / 2.0f;
}

// [실행]
//   ./distance          0.5초마다 측정
//   ./distance 0 -q     쉬지 않고 연속 측정, 매번 출력하지 않음 (03_distance_alert.cc와 CPU 비교용)
//   Ctrl+C로 끝내면 평균, 표준편차, CPU 점유율을 출력한다.
int main(int argc, char* argv[]) {
    const RunOptions options = ParseOptions(argc, argv);

    // GPIO 칩을 열고 부저 핀을 준비한다. 부저는 꺼진 상태로 시작한다.
    const int handle = lgGpiochipOpen(kChipDevice);
    Gpio buzzer(kBuzzerPin, Gpio::kOutputLow);
    if (handle < 0 || !buzzer.ok() || !InstallStopHandler()) return 1;

    // TRIG는 출력(처음에는 LOW)으로, ECHO는 입력으로 사용하겠다고 요청한다.
    if (lgGpioClaimOutput(handle, 0, kTrigPin, 0) != LG_OKAY ||
        lgGpioClaimInput(handle, 0, kEchoPin) != LG_OKAY) {
        std::fprintf(stderr, "cannot claim GPIO%d/%d\n", kTrigPin, kEchoPin);
        return 1;
    }

    std::printf("[busy-wait] TRIG=GPIO%d ECHO=GPIO%d buzzer=GPIO%u interval=%ld ms. Ctrl-C to stop\n",
                kTrigPin, kEchoPin, kBuzzerPin, options.interval_us / 1000);

    DistanceStats stats;
    while (!StopRequested()) {
        const long echo_us = MeasureEchoUs(handle);

        // 측정에 성공했고 10cm보다 가까우면 경고한다.
        // 능동 부저는 핀이 HIGH이기만 하면 소리가 나므로 켜고 끄기만 하면 된다.
        const bool warn = echo_us >= 0 && ToCm(echo_us) < kWarnCm;
        if (!buzzer.Set(warn ? 1 : 0)) break;

        if (echo_us < 0) {
            stats.AddFailure();
            if (!options.quiet) std::printf("no echo: %s\n", DescribeFailure(echo_us));
        } else {
            stats.AddSuccess(ToCm(echo_us));
            if (!options.quiet) {
                std::printf("echo=%5ld us -> %6.1f cm %s\n", echo_us, ToCm(echo_us), warn ? "[WARN]" : "");
            }
        }
        std::fflush(stdout);

        // 센서는 직전 측정의 반사파가 사라질 때까지 잠깐 쉬어야 해서, 연속 측정이어도 1ms는 쉰다.
        usleep(options.interval_us > 0 ? options.interval_us : 1000);
    }

    stats.Print("busy-wait");

    // 끝내기 전에 부저를 끄고, 사용한 핀과 칩을 돌려준다.
    buzzer.Set(0);
    lgGpioFree(handle, kEchoPin);
    lgGpioFree(handle, kTrigPin);
    return lgGpiochipClose(handle) == LG_OKAY ? 0 : 1;
}
