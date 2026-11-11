// =============================================================================
// 실습 4: 센서 통합 웹 서버
//
// [하는 일]
//   실습 3의 센서 3종(DHT11, CdS, HC-SR04)을 한 프로그램에서 읽고,
//   그 값을 웹 브라우저에서 볼 수 있게 HTTP로 제공한다.
//     http://<Pi 주소>:8080/     -> 대시보드 화면 (1초마다 자동 갱신)
//     http://<Pi 주소>:8080/api  -> JSON 데이터
//
// [왜 스레드를 두 개 쓸까?]
//   센서는 0.5초마다 읽어야 하고, 브라우저 요청은 언제 올지 모른다.
//   하나의 루프로 둘 다 하면, 센서를 읽는 동안(수십 ms) 브라우저가 기다려야 하고
//   브라우저를 처리하는 동안 DHT11의 마이크로초 타이밍이 흐트러진다.
//   그래서 일을 나눈다.
//     수집 스레드: 0.5초마다 센서를 읽어 공유 상태에 쓴다. (생산자)
//     메인 스레드: 브라우저 요청이 오면 공유 상태를 읽어 답한다. (소비자)
//
// [과제 2] JSON에 수집 시각(timestamp)을 추가했다.
//
// [빌드] (src/lab_04 폴더에서)
//   make
// =============================================================================

#include <unistd.h>  // close()

#include <chrono>   // std::chrono::milliseconds
#include <cstdio>   // printf(), snprintf()
#include <cstring>  // strlen()
#include <ctime>    // time(), localtime_r(), strftime()
#include <mutex>    // std::mutex, std::lock_guard
#include <thread>   // std::thread

#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능
#include "http.hpp"
#include "sensors.hpp"

namespace {
    constexpr int kPort = 8080;            // 웹 서버 포트
    constexpr int kCollectMs = 500;        // 센서 수집 주기: 0.5초
    constexpr int kAcceptTimeoutMs = 300;  // 접속을 기다리다 Ctrl+C를 확인하는 간격

    // 채널(측정값) 4개의 이름과 단위. 센서를 늘릴 때는 이 표에 한 줄씩 추가하면 된다.
    constexpr int kChannels = 4;
    const char* const kNames[kChannels] = {"temperature", "humidity", "light", "distance"};
    const char* const kUnits[kChannels] = {"C", "%", "V", "cm"};
}  // namespace

// =============================================================================
// 두 스레드가 함께 쓰는 공유 상태
//
// 수집 스레드가 쓰고, 메인 스레드가 읽는다. 둘이 동시에 건드리면 값이 섞일 수 있어서
// 모든 접근을 뮤텍스(mutex)로 보호한다.
//
// [왜 필드마다 std::atomic을 쓰지 않을까?]
//   atomic은 값 "하나"를 안전하게 바꿔 줄 뿐이다. 수집 스레드가 온도를 새 값으로 쓴 직후
//   멈추고 웹 스레드가 읽으면, 새 온도와 지난번 거리가 섞인 결과를 보게 된다.
//   네 값과 시각이 "한 묶음으로 함께" 바뀌어야 하므로, 전체를 하나의 임계 구역으로 묶는다.
// =============================================================================
class Snapshot {
public:
    // 새로 수집한 값 전체를 한 번에 바꾼다.
    void Replace(const float* values, const bool* valid, std::time_t collected_at) {
        std::lock_guard<std::mutex> guard(mutex_);  // 이 블록이 끝나면 자동으로 잠금이 풀린다.
        for (int i = 0; i < kChannels; ++i) {
            values_[i] = values[i];
            valid_[i] = valid[i];
        }
        collected_at_ = collected_at;
    }

    // 현재 값 전체를 복사해 간다. 복사만 하고 바로 잠금을 풀어서 수집 스레드를 오래 막지 않는다.
    void Read(float* values, bool* valid, std::time_t* collected_at) const {
        std::lock_guard<std::mutex> guard(mutex_);
        for (int i = 0; i < kChannels; ++i) {
            values[i] = values_[i];
            valid[i] = valid_[i];
        }
        *collected_at = collected_at_;
    }

private:
    mutable std::mutex mutex_;  // const 함수(Read)에서도 잠글 수 있도록 mutable
    float values_[kChannels] = {0.0f};
    bool valid_[kChannels] = {false};
    std::time_t collected_at_ = 0;  // 0이면 아직 한 번도 수집하지 않은 것이다.
};

// 센서를 한 바퀴 읽어 공유 상태에 넣는다.
// 센서 하나가 실패해도 다른 센서 값은 그대로 살아 있도록, 결과를 따로따로 기록한다.
void CollectOnce(Sensors* sensors, Snapshot* out) {
    float values[kChannels] = {0.0f};
    bool valid[kChannels] = {false};  // 온도, 습도, 조도, 거리
    valid[0] = sensors->ReadDht(&values[0], &values[1]);
    valid[1] = valid[0];  // DHT11은 한 번 읽을 때 온도와 습도를 함께 준다.
    valid[2] = sensors->ReadLight(&values[2]);
    valid[3] = sensors->ReadDistance(&values[3]);
    out->Replace(values, valid, std::time(nullptr));
}

// 수집 시각을 "2026-11-09T15:30:05" 형태의 글자로 바꾼다. 아직 수집 전이면 null.
void FormatTime(std::time_t when, char* out, size_t size) {
    std::tm local;
    if (when == 0 || localtime_r(&when, &local) == nullptr ||
        std::strftime(out, size, "\"%Y-%m-%dT%H:%M:%S\"", &local) == 0) {
        std::snprintf(out, size, "null");
    }
}

// 공유 상태를 JSON 글자로 만든다. 만든 길이를 돌려주고, 버퍼가 모자라면 -1.
//   {"timestamp":"2026-11-09T15:30:05","sensors":[{"name":"temperature",...}, ...]}
int BuildJson(char* out, size_t size, const float* values, const bool* valid, std::time_t collected_at) {
    char timestamp[32];
    FormatTime(collected_at, timestamp, sizeof(timestamp));

    int n = std::snprintf(out, size, "{\"timestamp\":%s,\"sensors\":[", timestamp);
    for (int i = 0; i < kChannels && n > 0 && static_cast<size_t>(n) < size; ++i) {
        n += std::snprintf(out + n, size - static_cast<size_t>(n),
                           "%s{\"name\":\"%s\",\"unit\":\"%s\",\"value\":%.2f,\"ok\":%s}",
                           i == 0 ? "" : ",", kNames[i], kUnits[i],
                           static_cast<double>(values[i]), valid[i] ? "true" : "false");
    }
    if (n > 0 && static_cast<size_t>(n) < size) {
        n += std::snprintf(out + n, size - static_cast<size_t>(n), "]}");
    }
    // snprintf는 "쓰고 싶었던 길이"를 돌려준다. 확인하지 않으면 버퍼에 실제로 들어간 것보다
    // 긴 Content-Length를 보내게 되므로, 넘쳤으면 -1로 실패를 알린다.
    return (n < 0 || static_cast<size_t>(n) >= size) ? -1 : n;
}

int main() {
    Sensors sensors;
    Http server(kPort);
    if (!InstallStopHandler() || !server.ok()) {
        std::fprintf(stderr, "cannot listen on port %d\n", kPort);
        return 1;
    }

    Snapshot snapshot;

    // 수집 스레드를 띄운다. 이 람다 함수가 별도의 흐름으로 동시에 실행된다.
    // [&sensors, &snapshot]은 main의 두 변수를 복사하지 않고 그대로 함께 쓰겠다는 뜻이다.
    std::thread collector([&sensors, &snapshot]() {
        while (!StopRequested()) {
            CollectOnce(&sensors, &snapshot);
            std::this_thread::sleep_for(std::chrono::milliseconds(kCollectMs));
        }
    });

    std::printf("dashboard on http://<pi-address>:%d/  (JSON at /api)\n", kPort);
    std::fflush(stdout);

    // 메인 스레드는 브라우저 요청을 처리한다.
    while (!StopRequested()) {
        const int client = server.Accept(kAcceptTimeoutMs);
        if (client < 0) continue;  // 아무도 안 왔으면 다시 기다린다.

        if (Http::Wants(client, "/api")) {
            float values[kChannels];
            bool valid[kChannels];
            std::time_t collected_at = 0;
            char body[1024];
            snapshot.Read(values, valid, &collected_at);
            const int length = BuildJson(body, sizeof(body), values, valid, collected_at);
            if (length > 0) Http::Send(client, "application/json", body, length);
        } else {
            Http::Send(client, "text/html", kDashboard, static_cast<int>(std::strlen(kDashboard)));
        }
        ::close(client);
    }

    // 수집 스레드가 끝날 때까지 기다린다. 기다리지 않고 main이 끝나면
    // sensors와 snapshot이 먼저 사라져서, 수집 스레드가 없는 메모리를 건드리게 된다.
    collector.join();
    std::printf("stopped\n");
    return 0;
}
