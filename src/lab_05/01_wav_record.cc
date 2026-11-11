// =============================================================================
// 실습 5-B: Ctrl+C를 누를 때까지 마이크 소리를 WAV 파일로 녹음하는 프로그램
//
// [WAV 파일의 구조]
//   맨 앞 44바이트는 "이 파일이 어떤 소리인지" 적어 둔 머리말(헤더)이고,
//   그 뒤에는 마이크에서 읽은 표본이 그대로 이어진다.
//
//   위치  크기  내용              이 실습의 값
//   0     4     "RIFF"
//   4     4     RIFF 크기         36 + 데이터 크기  (녹음이 끝나야 알 수 있음)
//   8     4     "WAVE"
//   12    4     "fmt "            (끝에 공백 한 칸 포함)
//   16    4     fmt 크기          16
//   20    2     오디오 형식       1 (PCM)
//   22    2     채널 수           1
//   24    4     표본화 주파수     16000
//   28    4     초당 바이트 수    32000 (주파수 x 2바이트)
//   32    2     프레임 크기       2 (채널 수 x 16비트 / 8)
//   34    2     표본 비트 수      16
//   36    4     "data"
//   40    4     데이터 크기       표본 바이트 수  (녹음이 끝나야 알 수 있음)
//   44    ...   표본들
//
// [크기를 나중에 채우는 이유]
//   녹음을 시작할 때는 언제 끝날지 모르니 크기를 알 수 없다. 그래서 처음에는 0으로 써 두고,
//   Ctrl+C로 녹음이 끝나면 파일 맨 앞으로 돌아가서 실제 크기로 다시 쓴다.
//   kill -9처럼 강제로 끝내면 이 단계가 실행되지 않아 0초짜리 파일이 된다.
//
// [실행]
//   ./wavrec plughw:3,0            <- 16kHz로 record.wav에 녹음
//   ./wavrec plughw:3,0 8000       <- 과제 2: 8kHz로 녹음
//   ./wavrec plughw:3,0 44100      <- 과제 2: 44.1kHz로 녹음
//   aplay record.wav               <- 녹음한 파일 재생
//
// [빌드] (src/lab_05 폴더에서)
//   make wavrec
// =============================================================================

#include <alsa/asoundlib.h>  // ALSA: 리눅스 표준 오디오 라이브러리

#include <cerrno>   // EPIPE: 오버런을 뜻하는 에러 번호
#include <cstdint>  // std::uint8_t, std::uint16_t, std::uint32_t
#include <cstdio>   // fopen(), fwrite(), fseek(), printf()
#include <cstdlib>  // strtoul(): 글자를 숫자로 바꾸기

#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr const char* kDevice = "plughw:1,0";  // 기본 장치. arecord -l로 확인해서 바꾼다.
    constexpr const char* kOutPath = "record.wav"; // 저장할 파일 이름
    constexpr unsigned int kRate = 16000;          // 기본 표본화 주파수 (두 번째 인자로 바꿀 수 있다)
    constexpr unsigned int kLatencyUs = 100000;    // 드라이버가 모아 두는 양: 100ms
    constexpr snd_pcm_uframes_t kPeriod = 1024;    // 한 번에 읽는 표본 수
    constexpr std::uint16_t kChannels = 1;         // 모노
    constexpr std::uint16_t kBits = 16;            // 표본 하나당 16비트
    constexpr std::uint32_t kHeaderBytes = 44;     // WAV 헤더 크기
}  // namespace

// WAV 파일 안의 숫자는 모두 리틀 엔디언(작은 자리 바이트가 먼저)으로 써야 한다.
// 정수를 메모리 그대로 쓰면 컴퓨터 종류에 따라 순서가 뒤집힐 수 있어서,
// 비트 연산으로 한 바이트씩 잘라 작은 자리부터 직접 쓴다.
//   예) 16000 = 0x3E80  ->  80 3E 순서로 기록
bool WriteU16(std::FILE* file, std::uint16_t value) {
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                   static_cast<std::uint8_t>(value >> 8)};
    return std::fwrite(bytes, 1, 2, file) == 2;
}

bool WriteU32(std::FILE* file, std::uint32_t value) {
    const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(value & 0xFF),
                                   static_cast<std::uint8_t>((value >> 8) & 0xFF),
                                   static_cast<std::uint8_t>((value >> 16) & 0xFF),
                                   static_cast<std::uint8_t>(value >> 24)};
    return std::fwrite(bytes, 1, 4, file) == 4;
}

// 44바이트 헤더를 쓴다. 위의 표 순서 그대로다.
// && 로 이어져 있어서, 중간에 하나라도 실패하면 뒤는 실행하지 않고 false가 된다.
bool WriteHeader(std::FILE* file, unsigned int rate, std::uint32_t data_bytes) {
    const std::uint16_t align = static_cast<std::uint16_t>(kChannels * kBits / 8);  // 프레임 크기 2바이트
    return std::fwrite("RIFF", 1, 4, file) == 4 &&
           WriteU32(file, kHeaderBytes - 8 + data_bytes) &&  // 앞의 8바이트("RIFF"+크기)를 뺀 나머지
           std::fwrite("WAVEfmt ", 1, 8, file) == 8 &&
           WriteU32(file, 16) &&                           // fmt 부분의 크기
           WriteU16(file, 1) &&                            // 1 = PCM (압축 없음)
           WriteU16(file, kChannels) &&
           WriteU32(file, rate) &&
           WriteU32(file, rate * align) &&                 // 초당 바이트 수
           WriteU16(file, align) &&
           WriteU16(file, kBits) &&
           std::fwrite("data", 1, 4, file) == 4 &&
           WriteU32(file, data_bytes);
}

int main(int argc, char* argv[]) {
    const char* device = argc > 1 ? argv[1] : kDevice;

    // 과제 2: 두 번째 인자로 표본화 주파수를 바꿀 수 있다. 없거나 잘못된 값이면 16000.
    unsigned int rate = kRate;
    if (argc > 2) {
        const unsigned long requested = std::strtoul(argv[2], nullptr, 10);
        if (requested >= 1000 && requested <= 192000) rate = static_cast<unsigned int>(requested);
    }

    // 1) 녹음 장치를 열고 형식을 설정한다. (5-A와 같다)
    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (rc >= 0) {
        rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                kChannels, rate, 1, kLatencyUs);
    }

    // 2) 파일을 만들고, 크기를 0으로 둔 임시 헤더를 먼저 쓴다.
    //    헤더를 쓰고 나면 파일의 쓰기 위치가 44바이트 뒤가 되어, 표본이 바로 이어진다.
    std::FILE* file = std::fopen(kOutPath, "wb");  // wb: 바이너리 쓰기 모드
    if (rc < 0 || file == nullptr || !InstallStopHandler() || !WriteHeader(file, rate, 0)) {
        std::fprintf(stderr, "cannot start on %s: %s\n", device, snd_strerror(rc));
        if (file != nullptr) std::fclose(file);
        if (pcm != nullptr) snd_pcm_close(pcm);
        return 1;
    }

    std::int16_t samples[kPeriod];
    std::uint32_t data_bytes = 0;  // 지금까지 쓴 표본 바이트 수
    std::printf("recording %s at %u Hz. Ctrl-C to finish\n", kOutPath, rate);

    // 3) Ctrl+C를 누를 때까지 읽어서 파일에 이어 쓴다.
    while (!StopRequested()) {
        const snd_pcm_sframes_t got = snd_pcm_readi(pcm, samples, kPeriod);
        if (got < 0) {
            if (got == -EPIPE) {
                // 과제 5: Ctrl+Z로 멈췄다가 fg로 재개하면 여기서 오버런이 보인다.
                std::printf("\n[overrun] samples were dropped, recovering\n");
            }
            if (snd_pcm_recover(pcm, static_cast<int>(got), 1) < 0) {
                // 복구할 수 없는 에러. 원인을 알려 주지 않으면 그냥 꺼진 것처럼 보이므로 출력한다.
                std::fprintf(stderr, "\nread failed: %s\n", snd_strerror(static_cast<int>(got)));
                break;
            }
            continue;
        }

        // ALSA가 이미 16비트 리틀 엔디언으로 주므로 표본은 그대로 쓰면 된다.
        const std::size_t bytes = static_cast<std::size_t>(got) * sizeof(std::int16_t);
        if (std::fwrite(samples, 1, bytes, file) != bytes) break;  // 디스크가 꽉 차면 멈춘다.
        data_bytes += static_cast<std::uint32_t>(bytes);

        // 녹음 시간 = 바이트 수 / (초당 표본 수 x 2바이트)
        std::printf("\r%.1f s recorded", static_cast<double>(data_bytes) / (rate * 2.0));
        std::fflush(stdout);
    }

    // 4) 파일 맨 앞으로 돌아가서, 0으로 써 두었던 두 크기를 실제 값으로 다시 쓴다.
    const bool patched = std::fseek(file, 0, SEEK_SET) == 0 && WriteHeader(file, rate, data_bytes);
    std::fclose(file);
    snd_pcm_close(pcm);
    std::printf("\nwrote %s (%u bytes)\n", kOutPath, data_bytes + kHeaderBytes);
    return patched ? 0 : 1;
}
