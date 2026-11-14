// =============================================================================
// 실습 3 고찰: Busy-Wait(02_distance.cc)와 Alert(03_distance_alert.cc)를 같은 기준으로
// 비교하기 위한 공통 도구
//
//   - 실행 옵션: 측정 간격(ms)과 조용히 모드(-q)
//   - 측정 통계: 성공 횟수, 평균, 표준편차, 최솟값, 최댓값, 실패 횟수
//   - CPU 점유율: 프로그램이 실행된 시간 중 CPU를 실제로 쓴 시간의 비율
//
// 두 프로그램이 이 파일을 함께 쓰므로, 비교 결과의 차이는 측정 방식의 차이에서만 나온다.
// =============================================================================
#ifndef MEP_LAB03_DISTANCE_STATS_HPP_
#define MEP_LAB03_DISTANCE_STATS_HPP_

#include <sys/resource.h>  // getrusage(): 이 프로세스가 쓴 CPU 시간
#include <sys/time.h>      // timeval

#include <cmath>    // sqrt()
#include <cstdio>   // printf()
#include <cstdlib>  // strtol()
#include <cstring>  // strcmp()
#include <ctime>    // clock_gettime()

// 실행 옵션
//   ./distance          0.5초마다 측정하고 매번 출력
//   ./distance 0        쉬지 않고 연속 측정 (CPU 점유율 비교용)
//   ./distance 0 -q     연속 측정하되 매번 출력하지 않음 (출력 자체가 CPU를 쓰므로)
struct RunOptions {
    long interval_us = 500000;  // 측정 간격
    bool quiet = false;         // true면 측정할 때마다 출력하지 않는다.
};

inline RunOptions ParseOptions(int argc, char* argv[]) {
    RunOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-q") == 0) {
            options.quiet = true;
        } else {
            const long ms = std::strtol(argv[i], nullptr, 10);
            if (ms >= 0) options.interval_us = ms * 1000;
        }
    }
    return options;
}

// 지금까지 쓴 CPU 시간(사용자 + 커널)을 초 단위로 돌려준다.
// 프로세스 전체 기준이라 Alert 방식에서 lgpio가 만든 스레드가 쓴 시간도 포함된다.
inline double CpuSeconds() {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}

// 실제로 흐른 시간을 초 단위로 돌려준다.
inline double WallSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 거리 측정 결과를 모아서 통계를 낸다.
class DistanceStats {
public:
    DistanceStats() : cpu_start_(CpuSeconds()), wall_start_(WallSeconds()) {}

    void AddSuccess(double cm) {
        ++count_;
        sum_ += cm;
        sum_sq_ += cm * cm;
        if (count_ == 1 || cm < min_) min_ = cm;
        if (count_ == 1 || cm > max_) max_ = cm;
    }

    void AddFailure() { ++failures_; }

    // Ctrl+C로 끝날 때 호출한다. 보고서 표에 그대로 옮길 수 있는 형태로 출력한다.
    void Print(const char* method) const {
        const double wall = WallSeconds() - wall_start_;
        const double cpu = CpuSeconds() - cpu_start_;
        std::printf("\n===== %s =====\n", method);
        std::printf("  run time      %.1f s\n", wall);
        std::printf("  cpu usage     %.1f %%  (cpu %.2f s / run %.1f s)\n",
                    wall > 0 ? 100.0 * cpu / wall : 0.0, cpu, wall);
        std::printf("  measurements  %d ok, %d failed\n", count_, failures_);
        if (count_ == 0) return;
        const double mean = sum_ / count_;
        // 표준편차: 값들이 평균에서 얼마나 흩어져 있는지. 작을수록 안정적이다.
        const double variance = sum_sq_ / count_ - mean * mean;
        const double stdev = variance > 0 ? std::sqrt(variance) : 0.0;
        std::printf("  mean          %.2f cm\n", mean);
        std::printf("  stdev         %.2f cm\n", stdev);
        std::printf("  min / max     %.2f / %.2f cm  (range %.2f cm)\n", min_, max_, max_ - min_);
    }

private:
    double cpu_start_;
    double wall_start_;
    int count_ = 0;
    int failures_ = 0;
    double sum_ = 0.0;
    double sum_sq_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
};

#endif  // MEP_LAB03_DISTANCE_STATS_HPP_
