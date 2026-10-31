// =============================================================================
// 실습 3-A: DHT11 온습도 센서 읽기 (단일선 비트뱅잉)
//
// [비트뱅잉이란?]
//   SPI 같은 전용 통신 회로 없이, CPU가 프로그램으로 핀을 직접 조작하고 시간을 재서
//   통신하는 방법이다. DHT11은 표준 통신 규격이 아니라서 이 방법을 쓴다.
//
// [DHT11과 대화하는 순서]
//   데이터 선이 1개뿐이라서 Pi와 센서가 번갈아 가며 사용한다.
//     1) 시작 신호: Pi가 선을 20ms 동안 LOW로 만든다. ("측정해 줘!")
//     2) 응답 신호: 센서가 LOW -> HIGH -> LOW 로 대답한다. ("알겠어!")
//     3) 데이터   : 센서가 40비트(5바이트)를 보낸다.
//     4) 체크섬   : 앞의 4바이트를 더한 값이 5번째 바이트와 같은지 검사한다.
//
// [0과 1을 구별하는 방법]
//   전압 높이가 아니라 "HIGH가 얼마나 오래 유지되는가"로 구별한다.
//     약 27마이크로초 HIGH -> 0
//     약 70마이크로초 HIGH -> 1
//   그래서 중간값인 50마이크로초를 기준으로 판단한다.
//
// [회로]
//   DHT11 DATA -> GPIO4, VCC -> 3.3V, GND -> GND
//   DATA와 3.3V 사이에 10k옴 풀업 저항이 필요하다. (모듈에는 보통 달려 있다.)
//
// [빌드] (src/lab_03 폴더에서, Pi에 libgpiod v2가 있을 때)
//   g++ -std=c++14 -Wall -Wextra 00_dht11_read.cc -o dht -lgpiod
// =============================================================================

#include <unistd.h>  // usleep(): 프로그램을 잠깐 쉬게 하는 함수

#include <cstdint>  // std::uint8_t: 0~255를 담는 1바이트 정수
#include <cstdio>   // printf(): 화면에 글자를 출력하는 함수
#include <ctime>    // clock_gettime(): 현재 시각을 읽는 함수

#include "../common/gpio.hpp"         // GPIO 핀을 쉽게 쓰게 해 주는 Gpio 클래스
#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr unsigned int kDataPin = 4;  // 데이터 선이 연결된 핀 (BCM 4)
    constexpr long kTimeoutUs = 200;      // 이 시간 안에 신호가 안 바뀌면 포기한다.
    constexpr long kBitThresholdUs = 50;  // HIGH가 이보다 길면 1, 짧으면 0 (과제 2에서 변경)
    constexpr int kBitCount = 40;         // 받을 비트 수 (5바이트 x 8비트)
    constexpr int kStartLowUs = 20000;    // 시작 신호 길이: 20ms
    constexpr int kIntervalUs = 2000000;  // 측정 간격: 2초 (센서가 이보다 빨리 읽을 수 없다.)
}  // namespace

// 현재 시각을 마이크로초(1/1000000초) 단위로 돌려준다.
long NowMicros() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

// 핀이 원하는 값(level)이 될 때까지 기다리고, 걸린 시간을 돌려준다.
// 시간이 초과되거나 읽기에 실패하면 -1을 돌려준다.
//
// [바쁜 대기(busy-wait)]
// 쉬지 않고 핀을 계속 읽는 방식이다. CPU를 많이 쓰지만 마이크로초 단위를 재려면
// 필요하다. usleep()으로 쉬면 그사이에 신호가 지나가 버린다.
long WaitFor(const Gpio& line, int level, long timeout_us) {
    const long start = NowMicros();
    for (;;) {
        const int value = line.Get();
        if (value < 0) return -1;
        const long elapsed = NowMicros() - start;
        if (value == level) return elapsed;
        if (elapsed > timeout_us) return -1;
    }
}

// 센서를 한 번 읽는다. 성공하면 true를 돌려주고 습도와 온도를 채워 준다.
bool ReadOnce(Gpio* line, int* humidity, int* temperature) {
    // 1) 시작 신호: 핀을 출력으로 바꿔 20ms 동안 LOW로 만든다.
    if (!line->Reconfigure(Gpio::kOutputLow)) return false;
    usleep(kStartLowUs);

    // 핀을 입력으로 바꾼다. 이제부터는 선을 놓고 센서의 말을 듣는다.
    if (!line->Reconfigure(Gpio::kInput)) return false;

    // 2) 응답 신호: LOW -> HIGH -> LOW 순서로 바뀌는지 차례로 확인한다.
    if (WaitFor(*line, 0, kTimeoutUs) < 0) return false;
    if (WaitFor(*line, 1, kTimeoutUs) < 0) return false;
    if (WaitFor(*line, 0, kTimeoutUs) < 0) return false;

    // 3) 데이터: 40비트를 하나씩 받는다.
    std::uint8_t data[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < kBitCount; ++i) {
        // HIGH가 시작될 때까지 기다린다.
        if (WaitFor(*line, 1, kTimeoutUs) < 0) return false;

        // HIGH가 끝날 때까지 걸린 시간이 곧 비트의 값이다.
        const long high_us = WaitFor(*line, 0, kTimeoutUs);
        if (high_us < 0) return false;
        const int bit = high_us > kBitThresholdUs ? 1 : 0;

        // i / 8 은 몇 번째 바이트인지를 뜻한다. (0~7번 비트는 0번 바이트)
        // 기존 값을 왼쪽으로 한 칸 밀고, 비어 있는 맨 오른쪽에 새 비트를 넣는다.
        data[i / 8] = static_cast<std::uint8_t>((data[i / 8] << 1) | bit);
    }

    // 4) 체크섬: 앞의 4바이트를 더한 값이 5번째 바이트와 다르면 잘못 받은 것이니 버린다.
    // static_cast로 1바이트로 잘라서 비교한다. 합이 255를 넘을 수 있기 때문이다.
    if (static_cast<std::uint8_t>(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return false;
    }

    // data[0]: 습도 정수, data[1]: 습도 소수, data[2]: 온도 정수, data[3]: 온도 소수
    // DHT11은 소수 부분이 항상 0이라서 정수 부분만 쓴다.
    *humidity = data[0];
    *temperature = data[2];
    return true;
}

int main() {
    // 평소에는 입력 상태로 둔다. 풀업 저항 덕분에 선은 HIGH로 유지된다.
    Gpio line(kDataPin, Gpio::kInput);
    if (!InstallStopHandler() || !line.ok()) return 1;

    std::printf("DHT11 on GPIO%u. Ctrl-C to stop\n", kDataPin);

    while (!StopRequested()) {
        int humidity = 0;
        int temperature = 0;

        if (ReadOnce(&line, &humidity, &temperature)) {
            std::printf("%d C  %d %%\n", temperature, humidity);
        } else {
            // 비트뱅잉은 운영체제가 잠깐 다른 일을 하면 시간이 어긋나서 가끔 실패할 수 있다.
            std::printf("read failed -- check the pull-up and the 20 ms pulse\n");
        }
        std::fflush(stdout);

        usleep(kIntervalUs);  // 다음 측정까지 2초 기다린다.
    }
    return 0;
}
