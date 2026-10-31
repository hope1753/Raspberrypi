// =============================================================================
// 실습 3-B: CdS 조도 센서로 밝기 읽기 (SPI + MCP3008 ADC)
//
// [왜 ADC가 필요할까?]
//   라즈베리파이 GPIO는 0과 1만 읽을 수 있어서 "1.65V" 같은 전압 크기를 모른다.
//   그래서 전압을 숫자로 바꿔 주는 외부 칩(ADC, MCP3008)을 쓴다.
//   MCP3008은 10비트 ADC라서 0V~3.3V를 0~1023의 숫자로 바꿔 준다.
//
// [CdS와 분압 회로]
//   CdS는 밝을수록 저항이 작아지는 부품이다. 다음처럼 저항과 직렬로 연결한다.
//     3.3V -- [CdS] -- 측정 지점 -- [10k옴] -- GND
//   밝아지면 CdS 저항이 줄어들어 측정 지점의 전압이 올라간다.
//     전압 V = 3.3V x 10k / (CdS 저항 + 10k)
//
//     밝기         | CdS 저항  | 전압    | ADC 값
//     -------------+-----------+---------+-------
//     어두움(암실) | 약 1M옴   | 약 0.03V| 약 10
//     실내 조명    | 약 10k옴  | 약 1.65V| 약 512
//     밝음(직사광) | 약 1k옴   | 약 3.0V | 약 930
//
// [SPI란?]
//   클록 선에 맞춰 데이터를 주고받는 통신 방식이다. 시간 맞추기는 전용 하드웨어와
//   리눅스 커널 드라이버가 해 주므로, 프로그램은 보낼 바이트와 받은 바이트만 다룬다.
//
// [회로]
//   측정 지점 -> MCP3008 CH0, Pi의 SPI0 핀(SCLK, MOSI, MISO, CE0)을 MCP3008에 연결
//   MCP3008의 VDD와 VREF -> 3.3V
//
// [실행 전 준비] Pi에서 SPI를 켜야 한다: sudo raspi-config -> Interface Options -> SPI
//
// [빌드] (src/lab_03 폴더에서) 커널 기능만 써서 추가 라이브러리가 필요 없다.
//   g++ -std=c++14 -Wall -Wextra 01_cds_light.cc -o light
// =============================================================================

#include <fcntl.h>              // open(): 장치 파일을 여는 함수
#include <linux/spi/spidev.h>   // 리눅스 SPI 드라이버에 쓰는 구조체와 명령들
#include <sys/ioctl.h>          // ioctl(): 드라이버에 명령을 보내는 함수
#include <unistd.h>             // usleep(), close()

#include <cstdint>  // std::uint8_t 같은 정해진 크기의 정수
#include <cstdio>   // printf(): 화면에 글자를 출력하는 함수
#include <cstring>  // memset(): 메모리를 0으로 채우는 함수

#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr const char* kSpiDev = "/dev/spidev0.0";  // SPI 버스 0번, CE0 핀에 연결된 장치
    constexpr unsigned int kChannel = 0;               // CdS 회로가 연결된 MCP3008 채널
    constexpr int kAdcMax = 1023;                      // 10비트 ADC의 최댓값
    constexpr float kVref = 3.3f;                      // ADC의 기준 전압
    constexpr float kFixedOhm = 10000.0f;              // 분압 회로의 고정 저항 10k옴

    // SPI 클록 속도다. MCP3008의 최대 속도는 전압마다 다르다.
    // (5V에서 3.6MHz, 2.7V에서 1.35MHz) 3.3V 수치는 데이터시트에 없어서
    // 안전한 2.7V 기준인 1.35MHz를 쓴다.
    constexpr std::uint32_t kSpeedHz = 1350000;

    constexpr int kIntervalUs = 500000;  // 측정 간격: 0.5초
}  // namespace

// MCP3008의 한 채널을 읽어서 0~1023 값을 돌려준다. 통신 실패는 -1, 응답이 없으면 -2.
//
// [주고받는 3바이트] SPI는 보내는 동안 동시에 받기도 한다.
//   보냄: 0x01(시작 비트) | 0x80+채널번호<<4(단일 입력, 채널 선택) | 0x00(값을 받기 위한 빈 클록)
//   받음: 의미 없는 값    | 하위 3비트에 널 비트와 결과 상위 2비트 | 결과 하위 8비트
int ReadChannel(int fd, unsigned int channel) {
    const std::uint8_t tx[3] = {0x01, static_cast<std::uint8_t>(0x80 | (channel << 4)), 0x00};
    std::uint8_t rx[3] = {0, 0, 0};

    // 드라이버에 넘겨줄 전송 정보를 채운다. 먼저 전체를 0으로 깨끗이 지운다.
    spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = reinterpret_cast<std::uintptr_t>(tx);  // 보낼 데이터가 있는 곳
    tr.rx_buf = reinterpret_cast<std::uintptr_t>(rx);  // 받은 데이터를 넣을 곳
    tr.len = 3;                                        // 3바이트를 주고받는다.
    tr.speed_hz = kSpeedHz;
    tr.bits_per_word = 8;

    if (::ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) return -1;

    // 널 비트는 정상이라면 항상 0이다. 1이면 칩이 응답하지 않은 것이다.
    // 이 검사가 없으면 연결이 끊겨도 값이 1023으로 읽혀서 "아주 밝음"으로 착각한다.
    if ((rx[1] & 0x04) != 0) return -2;

    // 상위 2비트(rx[1])를 왼쪽으로 8칸 밀고 하위 8비트(rx[2])를 붙여 10비트 값을 만든다.
    return ((rx[1] & 0x03) << 8) | rx[2];
}

// ADC 값(0~1023)을 전압(0~3.3V)으로 바꾼다.
float AdcToVoltage(int raw) {
    return (static_cast<float>(raw) / static_cast<float>(kAdcMax)) * kVref;
}

// 전압으로부터 CdS의 저항을 계산한다. (위의 분압 공식을 거꾸로 푼 식이다.)
// 전압이 0V나 3.3V이면 0으로 나누거나 저항이 0이 되어 계산할 수 없으니 -1을 돌려준다.
float CdsOhm(float volts) {
    if (volts <= 0.0f || volts >= kVref) return -1.0f;
    return kFixedOhm * (kVref - volts) / volts;
}

// 전압을 사람이 읽기 쉬운 밝기 단계로 바꾼다. (과제 3에서 확인한다.)
const char* Describe(float volts) {
    if (volts < 0.5f) return "dark";
    if (volts < 1.5f) return "shaded";
    if (volts < 2.5f) return "bright";
    return "very bright";
}

// SPI 통신 설정을 한다. 모드 0, 한 번에 8비트, 최대 속도 1.35MHz.
// MCP3008은 모드 0과 3을 지원하며, 여기서는 모드 0을 쓴다.
bool Configure(int fd) {
    const std::uint8_t mode = SPI_MODE_0;
    const std::uint8_t bits = 8;
    return ::ioctl(fd, SPI_IOC_WR_MODE, &mode) >= 0 &&
           ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) >= 0 &&
           ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &kSpeedHz) >= 0;
}

int main() {
    // 리눅스에서는 SPI 장치도 파일처럼 열어서 쓴다.
    const int fd = ::open(kSpiDev, O_RDWR);
    if (fd < 0 || !Configure(fd) || !InstallStopHandler()) {
        std::fprintf(stderr, "cannot open %s -- enable SPI with raspi-config\n", kSpiDev);
        return 1;
    }

    while (!StopRequested()) {
        const int raw = ReadChannel(fd, kChannel);

        if (raw < 0) {
            std::printf("no reply -- check VDD, MISO->DOUT, CE0->CS and the notch\n");
        } else {
            const float volts = AdcToVoltage(raw);
            std::printf("raw=%4d  V=%.3f  R=%.0f ohm  %s\n", raw, volts, CdsOhm(volts), Describe(volts));
        }
        std::fflush(stdout);

        usleep(kIntervalUs);
    }

    return ::close(fd) == 0 ? 0 : 1;  // 장치 파일을 닫고 끝낸다.
}
