// =============================================================================
// 실습 4: 실습 3의 센서 3종을 하나의 클래스로 묶은 파일
//
// 실습 3에서는 센서마다 main()이 따로 있었다. 여기서는 각 센서를 읽는 부분만
// 가져와서 Sensors 클래스 하나에 넣었다. 그래서 프로그램 하나가 세 센서를 함께 다룬다.
//
//   센서      | 통신 방식        | 연결 핀
//   ----------+------------------+-------------------------------------------
//   DHT11     | 단일선 비트뱅잉  | DATA -> GPIO4 (10k옴 풀업)
//   CdS       | SPI (MCP3008)    | SPI0 (CE0), MCP3008 CH0
//   HC-SR04   | 펄스 폭 측정     | TRIG -> GPIO23, ECHO -> 1k/2k 분압 -> GPIO24
//
// [실패 격리]
//   한 센서가 연결되지 않았거나 고장 나도 다른 센서는 계속 읽혀야 한다.
//   그래서 준비에 실패해도 프로그램을 끝내지 않고, 그 센서의 Read 함수만 false를 돌려준다.
// =============================================================================
#ifndef MEP_LAB04_SENSORS_HPP_
#define MEP_LAB04_SENSORS_HPP_

#include <fcntl.h>             // open(): SPI 장치 파일 열기
#include <lgpio.h>             // lgpio: 초음파 센서의 정밀한 시간 측정
#include <linux/spi/spidev.h>  // 리눅스 SPI 드라이버 구조체와 명령
#include <sys/ioctl.h>         // ioctl(): 드라이버에 명령 보내기
#include <unistd.h>            // usleep(), close()

#include <cstdint>  // std::uint8_t 같은 정해진 크기의 정수
#include <cstring>  // memset()
#include <ctime>    // clock_gettime()

#include "../common/gpio.hpp"  // DHT11에 쓰는 Gpio 클래스 (libgpiod)

// 실습 3에서 가져온 함수들. 이름이 겹치지 않도록 lab3라는 이름 공간에 넣었다.
namespace lab3 {

// ---------------------------------------------------------------------------
// 실습 3-A: DHT11 (00_dht11_read.cc)
// ---------------------------------------------------------------------------
constexpr unsigned int kDhtPin = 4;     // 데이터 선 (BCM 4)
constexpr long kDhtTimeoutUs = 200;     // 이 시간 안에 신호가 안 바뀌면 포기한다.
constexpr long kBitThresholdUs = 50;    // HIGH가 이보다 길면 1, 짧으면 0
constexpr int kBitCount = 40;           // 받을 비트 수 (5바이트)
constexpr int kStartLowUs = 20000;      // 시작 신호 20ms
constexpr long kDhtMinIntervalUs = 2000000;  // DHT11은 2초보다 빨리 읽을 수 없다.

// 현재 시각을 마이크로초 단위로 돌려준다.
inline long NowMicros() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

// 핀이 level이 될 때까지 쉬지 않고 기다린다(바쁜 대기). 걸린 시간 또는 -1.
inline long WaitFor(const Gpio& line, int level, long timeout_us) {
    const long start = NowMicros();
    for (;;) {
        const int value = line.Get();
        if (value < 0) return -1;
        const long elapsed = NowMicros() - start;
        if (value == level) return elapsed;
        if (elapsed > timeout_us) return -1;
    }
}

// 시작 신호 -> 응답 신호 -> 40비트 수신 -> 체크섬 검사 순서로 한 번 읽는다.
inline bool ReadDhtOnce(Gpio* line, int* humidity, int* temperature) {
    if (!line->Reconfigure(Gpio::kOutputLow)) return false;  // 시작 신호: 20ms LOW
    usleep(kStartLowUs);
    if (!line->Reconfigure(Gpio::kInput)) return false;      // 이제 센서의 말을 듣는다.

    // 응답 신호: LOW -> HIGH -> LOW
    if (WaitFor(*line, 0, kDhtTimeoutUs) < 0) return false;
    if (WaitFor(*line, 1, kDhtTimeoutUs) < 0) return false;
    if (WaitFor(*line, 0, kDhtTimeoutUs) < 0) return false;

    // 40비트: HIGH가 유지된 시간으로 0과 1을 구별한다.
    std::uint8_t data[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < kBitCount; ++i) {
        if (WaitFor(*line, 1, kDhtTimeoutUs) < 0) return false;
        const long high_us = WaitFor(*line, 0, kDhtTimeoutUs);
        if (high_us < 0) return false;
        data[i / 8] = static_cast<std::uint8_t>((data[i / 8] << 1) | (high_us > kBitThresholdUs ? 1 : 0));
    }

    // 체크섬이 맞지 않으면 잘못 받은 것이니 버린다.
    if (static_cast<std::uint8_t>(data[0] + data[1] + data[2] + data[3]) != data[4]) return false;
    *humidity = data[0];
    *temperature = data[2];
    return true;
}

// ---------------------------------------------------------------------------
// 실습 3-B: CdS + MCP3008 (01_cds_light.cc)
// ---------------------------------------------------------------------------
constexpr const char* kSpiDev = "/dev/spidev0.0";  // SPI 버스 0, CE0
constexpr unsigned int kAdcChannel = 0;            // CdS 회로가 연결된 채널
constexpr int kAdcMax = 1023;                      // 10비트 ADC 최댓값
constexpr float kVref = 3.3f;                      // ADC 기준 전압
constexpr std::uint32_t kSpeedHz = 1350000;        // SPI 속도 (MCP3008 안전 범위)

// SPI 모드 0, 8비트, 1.35MHz로 설정한다.
inline bool ConfigureSpi(int fd) {
    const std::uint8_t mode = SPI_MODE_0;
    const std::uint8_t bits = 8;
    return ::ioctl(fd, SPI_IOC_WR_MODE, &mode) >= 0 &&
           ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) >= 0 &&
           ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &kSpeedHz) >= 0;
}

// MCP3008의 한 채널을 읽어 0~1023을 돌려준다. 실패하면 음수.
inline int ReadAdc(int fd, unsigned int channel) {
    const std::uint8_t tx[3] = {0x01, static_cast<std::uint8_t>(0x80 | (channel << 4)), 0x00};
    std::uint8_t rx[3] = {0, 0, 0};
    spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = reinterpret_cast<std::uintptr_t>(tx);
    tr.rx_buf = reinterpret_cast<std::uintptr_t>(rx);
    tr.len = 3;
    tr.speed_hz = kSpeedHz;
    tr.bits_per_word = 8;
    if (::ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) return -1;
    if ((rx[1] & 0x04) != 0) return -2;  // 널 비트가 1이면 칩이 응답하지 않은 것이다.
    return ((rx[1] & 0x03) << 8) | rx[2];
}

// ADC 값(0~1023)을 전압(0~3.3V)으로 바꾼다.
inline float AdcToVoltage(int raw) {
    return (static_cast<float>(raw) / static_cast<float>(kAdcMax)) * kVref;
}

// ---------------------------------------------------------------------------
// 실습 3-C: HC-SR04 (02_distance.cc)
// ---------------------------------------------------------------------------
constexpr int kChipDevice = 0;            // /dev/gpiochip0
constexpr int kTrigPin = 23;              // TRIG (BCM 23)
constexpr int kEchoPin = 24;              // ECHO (BCM 24), 분압 회로 필수
constexpr long kStartTimeoutUs = 60000;   // ECHO가 HIGH가 되기를 기다리는 최대 시간
constexpr long kEchoTimeoutUs = 30000;    // ECHO가 LOW가 되기를 기다리는 최대 시간
constexpr float kSpeedCmPerUs = 0.0343f;  // 20도 공기 중 음속

// lgpio로 핀이 level이 될 때까지 기다린다. lguTimestamp()는 나노초 단위다.
inline long WaitFor(int handle, int gpio, int level, long timeout_us) {
    const std::uint64_t start = lguTimestamp();
    for (;;) {
        const int value = lgGpioRead(handle, gpio);
        if (value < 0) return -1;
        const long elapsed = static_cast<long>((lguTimestamp() - start) / 1000);
        if (value == level) return elapsed;
        if (elapsed > timeout_us) return -1;
    }
}

// TRIG에 10us 펄스를 보내고, ECHO가 HIGH인 시간을 돌려준다. 실패하면 -1.
inline long MeasureEchoUs(int handle) {
    if (lgGpioRead(handle, kEchoPin) != 0) return -1;
    if (lgGpioWrite(handle, kTrigPin, 1) != LG_OKAY) return -1;
    lguSleep(0.00001);
    if (lgGpioWrite(handle, kTrigPin, 0) != LG_OKAY) return -1;
    if (WaitFor(handle, kEchoPin, 1, kStartTimeoutUs) < 0) return -1;
    return WaitFor(handle, kEchoPin, 0, kEchoTimeoutUs);
}

// 왕복 시간을 편도 거리(cm)로 바꾼다.
inline float ToCm(long echo_us) {
    return (static_cast<float>(echo_us) * kSpeedCmPerUs) / 2.0f;
}

}  // namespace lab3

// =============================================================================
// 세 센서를 하나로 묶은 클래스
// =============================================================================
class Sensors {
public:
    // 세 센서를 각각 준비한다. 하나가 실패해도 나머지는 그대로 쓸 수 있다.
    Sensors()
        : dht_(lab3::kDhtPin, Gpio::kInput),
          spi_fd_(::open(lab3::kSpiDev, O_RDWR)),
          lg_handle_(lgGpiochipOpen(lab3::kChipDevice)) {
        if (spi_fd_ >= 0 && !lab3::ConfigureSpi(spi_fd_)) {
            ::close(spi_fd_);
            spi_fd_ = -1;
        }
        lg_ok_ = lg_handle_ >= 0 &&
                 lgGpioClaimOutput(lg_handle_, 0, lab3::kTrigPin, 0) == LG_OKAY &&
                 lgGpioClaimInput(lg_handle_, 0, lab3::kEchoPin) == LG_OKAY;
    }

    // 소멸자: 열었던 장치를 모두 돌려준다. DHT11 핀은 Gpio가 알아서 돌려준다.
    ~Sensors() {
        if (spi_fd_ >= 0) ::close(spi_fd_);
        if (lg_handle_ >= 0) {
            lgGpioFree(lg_handle_, lab3::kEchoPin);
            lgGpioFree(lg_handle_, lab3::kTrigPin);
            lgGpiochipClose(lg_handle_);
        }
    }

    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    // 온도와 습도를 읽는다.
    // DHT11은 2초보다 자주 읽으면 안 되므로, 2초가 지나지 않았으면 센서를 건드리지 않고
    // 지난번 결과(캐시)를 그대로 돌려준다. 2초가 지났으면 실제로 읽고 캐시를 바꾼다.
    bool ReadDht(float* temperature, float* humidity) {
        const long now = lab3::NowMicros();
        if (!dht_attempted_ || now - dht_last_us_ >= lab3::kDhtMinIntervalUs) {
            dht_attempted_ = true;
            dht_last_us_ = now;
            int h = 0;
            int t = 0;
            dht_ok_ = dht_.ok() && lab3::ReadDhtOnce(&dht_, &h, &t);
            if (dht_ok_) {
                dht_temperature_ = static_cast<float>(t);
                dht_humidity_ = static_cast<float>(h);
            }
        }
        *temperature = dht_temperature_;
        *humidity = dht_humidity_;
        return dht_ok_;
    }

    // 조도 센서 전압(V)을 읽는다.
    bool ReadLight(float* volts) {
        if (spi_fd_ < 0) return false;
        const int raw = lab3::ReadAdc(spi_fd_, lab3::kAdcChannel);
        if (raw < 0) return false;
        *volts = lab3::AdcToVoltage(raw);
        return true;
    }

    // 거리(cm)를 읽는다.
    bool ReadDistance(float* cm) {
        if (!lg_ok_) return false;
        const long echo_us = lab3::MeasureEchoUs(lg_handle_);
        if (echo_us < 0) return false;
        *cm = lab3::ToCm(echo_us);
        return true;
    }

private:
    Gpio dht_;
    int spi_fd_;
    int lg_handle_;
    bool lg_ok_ = false;

    // DHT11 캐시
    bool dht_attempted_ = false;  // 한 번이라도 읽어 봤는가?
    long dht_last_us_ = 0;        // 마지막으로 실제로 읽은 시각
    bool dht_ok_ = false;         // 마지막 읽기가 성공했는가?
    float dht_temperature_ = 0.0f;
    float dht_humidity_ = 0.0f;
};

#endif  // MEP_LAB04_SENSORS_HPP_
