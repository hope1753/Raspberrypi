// =============================================================================
// 실습 3 고찰: HC-SR04 거리 측정을 Busy-Wait 대신 Alert(엣지 콜백)로 구현
//
// [02_distance.cc (Busy-Wait)와 무엇이 다른가?]
//   Busy-Wait: ECHO가 바뀔 때까지 프로그램이 쉬지 않고 핀을 계속 읽는다.
//              기다리는 동안 CPU를 계속 쓰고, 시간도 "핀을 읽은 순간"에 잰다.
//   Alert    : "ECHO가 바뀌면 알려 달라"고 lgpio에 등록해 두고 잠든다.
//              ECHO가 HIGH나 LOW로 바뀌는 순간 커널이 시각을 기록하고,
//              lgpio가 그 기록을 콜백 함수로 넘겨준다. 기다리는 동안 CPU를 거의 쓰지 않는다.
//
// [동작 순서]
//   1) 메인: TRIG에 10us 펄스를 보내고, 결과가 올 때까지 잠든다. (조건 변수)
//   2) lgpio 스레드: ECHO가 HIGH가 되면 콜백 호출 -> 시각 기록
//   3) lgpio 스레드: ECHO가 LOW가 되면 콜백 호출 -> 펄스 폭 계산, 메인을 깨움
//   4) 메인: 펄스 폭으로 거리를 계산한다.
//
// [회로] 02_distance.cc와 같다.
//   VCC -> 5V, GND -> GND, TRIG -> GPIO23, 부저 -> GPIO18
//   ECHO -- [1k옴] -- GPIO24 -- [2k옴] -- GND
//
// [실행] 02_distance.cc와 같은 옵션을 쓴다.
//   ./alert          0.5초마다 측정
//   ./alert 0 -q     쉬지 않고 연속 측정, 매번 출력하지 않음 (CPU 점유율 비교용)
//   Ctrl+C로 끝내면 평균, 표준편차, CPU 점유율을 출력한다.
//
// [빌드] (src/lab_03 폴더에서)
//   make alert
// =============================================================================

#include <lgpio.h>   // lgpio: 엣지 콜백(Alert)
#include <unistd.h>  // usleep()

#include <chrono>              // std::chrono::milliseconds
#include <condition_variable>  // std::condition_variable: 결과가 올 때까지 잠들기
#include <cstdint>             // std::uint64_t
#include <cstdio>              // printf()
#include <mutex>               // std::mutex: 콜백 스레드와 메인 스레드가 함께 쓰는 값 보호

#include "../common/gpio.hpp"         // 부저에 쓰는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능
#include "distance_stats.hpp"         // 02_distance.cc와 같은 기준의 통계, CPU 점유율

namespace {
    constexpr int kChipDevice = 0;           // GPIO 칩 번호 (/dev/gpiochip0)
    constexpr int kTrigPin = 23;             // TRIG (BCM 23)
    constexpr int kEchoPin = 24;             // ECHO (BCM 24), 1k/2k 분압 회로를 거쳐서 연결
    constexpr unsigned int kBuzzerPin = 18;  // 능동 부저 (BCM 18)
    constexpr float kWarnCm = 10.0f;         // 이보다 가까우면 부저를 울린다.

    // 02_distance.cc의 두 제한 시간(60ms + 30ms)을 합친 값. 이 안에 결과가 없으면 실패로 본다.
    constexpr int kWaitMs = 90;
    constexpr float kSpeedCmPerUs = 0.0343f;  // 20도 공기 중 음속 (cm/마이크로초)
}  // namespace

// 측정에 실패했을 때 돌려주는 값. 02_distance.cc와 같은 번호를 쓴다.
constexpr long kEchoBusy = -1;      // 측정 전부터 ECHO가 HIGH로 붙어 있음
constexpr long kTrigFailed = -2;    // TRIG 핀에 펄스를 보내지 못함
constexpr long kNoEchoStart = -3;   // 기다려도 ECHO가 HIGH가 되지 않음
constexpr long kEchoTooLong = -4;   // ECHO가 HIGH가 된 뒤 LOW로 돌아오지 않음

const char* DescribeFailure(long code) {
    switch (code) {
        case kEchoBusy:    return "ECHO is already HIGH before triggering -- check the divider and that nothing else drives GPIO24";
        case kTrigFailed:  return "cannot drive TRIG -- check GPIO23";
        case kNoEchoStart: return "ECHO never went HIGH -- check the 5V feed, TRIG wiring and the divider direction";
        case kEchoTooLong: return "ECHO stayed HIGH too long -- nothing within about 5 m, or the target absorbs sound";
        default:           return "unknown";
    }
}

// =============================================================================
// 콜백(lgpio 스레드)과 메인 스레드가 함께 쓰는 값
//
// 콜백은 lgpio가 만든 별도 스레드에서 실행된다. 그래서 두 스레드가 같은 값을 건드리는
// 부분은 모두 뮤텍스로 보호한다. (실습 4의 Snapshot과 같은 이유)
// =============================================================================
struct EchoState {
    std::mutex mutex;
    std::condition_variable done_cv;  // 측정이 끝나면 메인 스레드를 깨운다.
    bool rose = false;                // 이번 측정에서 ECHO가 HIGH가 되었는가?
    bool done = false;                // 이번 측정의 펄스 폭이 나왔는가?
    std::uint64_t rise_ns = 0;        // ECHO가 HIGH가 된 시각 (커널이 기록, 나노초)
    long width_us = 0;                // 펄스 폭 (마이크로초)
};

// ECHO가 바뀔 때마다 lgpio가 부르는 함수. 여러 변화가 한 번에 묶여서 올 수도 있다.
void OnEchoEdge(int num_alerts, lgGpioAlert_p alerts, void* userdata) {
    EchoState* state = static_cast<EchoState*>(userdata);
    std::lock_guard<std::mutex> guard(state->mutex);
    for (int i = 0; i < num_alerts; ++i) {
        const lgGpioReport_t& report = alerts[i].report;
        if (report.gpio != kEchoPin) continue;

        if (report.level == 1) {
            // 올라가는 순간: 커널이 찍은 시각을 기록한다.
            state->rose = true;
            state->rise_ns = report.timestamp;
        } else if (report.level == 0 && state->rose && !state->done) {
            // 내려가는 순간: 두 시각의 차이가 펄스 폭이다.
            state->width_us = static_cast<long>((report.timestamp - state->rise_ns) / 1000);
            state->done = true;
            state->done_cv.notify_one();  // 잠들어 있는 메인 스레드를 깨운다.
        }
        // level이 2인 경우는 lgpio의 감시 시간 초과 알림이라 무시한다.
    }
}

// 초음파를 한 번 쏘고 ECHO 펄스 폭을 마이크로초로 돌려준다. 실패하면 위의 음수 값.
long MeasureEchoUs(int handle, EchoState* state) {
    if (lgGpioRead(handle, kEchoPin) != 0) return kEchoBusy;

    // 이번 측정을 위해 결과를 비운다. (콜백과 겹치지 않게 잠그고 바꾼다.)
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->rose = false;
        state->done = false;
    }

    // TRIG에 10us 펄스를 보낸다. (02_distance.cc와 같다)
    if (lgGpioWrite(handle, kTrigPin, 1) != LG_OKAY) return kTrigFailed;
    lguSleep(0.00001);
    if (lgGpioWrite(handle, kTrigPin, 0) != LG_OKAY) return kTrigFailed;

    // 결과가 올 때까지 잠든다. Busy-Wait와 달리 기다리는 동안 CPU를 쓰지 않는다.
    std::unique_lock<std::mutex> lock(state->mutex);
    const bool finished = state->done_cv.wait_for(lock, std::chrono::milliseconds(kWaitMs),
                                                  [state] { return state->done; });
    if (finished) return state->width_us;
    return state->rose ? kEchoTooLong : kNoEchoStart;
}

float ToCm(long echo_us) {
    return (static_cast<float>(echo_us) * kSpeedCmPerUs) / 2.0f;
}

int main(int argc, char* argv[]) {
    const RunOptions options = ParseOptions(argc, argv);

    const int handle = lgGpiochipOpen(kChipDevice);
    Gpio buzzer(kBuzzerPin, Gpio::kOutputLow);
    if (handle < 0 || !buzzer.ok() || !InstallStopHandler()) return 1;

    // TRIG는 출력으로, ECHO는 "올라가고 내려갈 때 모두 알려 달라"는 Alert 입력으로 요청한다.
    EchoState state;
    if (lgGpioClaimOutput(handle, 0, kTrigPin, 0) != LG_OKAY ||
        lgGpioClaimAlert(handle, 0, LG_BOTH_EDGES, kEchoPin, -1) != LG_OKAY ||
        lgGpioSetAlertsFunc(handle, kEchoPin, OnEchoEdge, &state) != LG_OKAY) {
        std::fprintf(stderr, "cannot claim GPIO%d/%d\n", kTrigPin, kEchoPin);
        return 1;
    }

    std::printf("[alert] TRIG=GPIO%d ECHO=GPIO%d buzzer=GPIO%u interval=%ld ms. Ctrl-C to stop\n",
                kTrigPin, kEchoPin, kBuzzerPin, options.interval_us / 1000);

    DistanceStats stats;
    while (!StopRequested()) {
        const long echo_us = MeasureEchoUs(handle, &state);

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

    stats.Print("alert (edge callback)");

    // 끝내기 전에 부저를 끄고, 사용한 핀과 칩을 돌려준다. ECHO를 돌려주면 Alert도 해제된다.
    buzzer.Set(0);
    lgGpioFree(handle, kEchoPin);
    lgGpioFree(handle, kTrigPin);
    return lgGpiochipClose(handle) == LG_OKAY ? 0 : 1;
}
