// =============================================================================
// 실습 5-A: 마이크 소리 크기를 터미널 막대로 보여 주는 프로그램 (레벨 미터)
//
// [소리가 숫자가 되는 과정 (PCM)]
//   마이크 신호를 1초에 16,000번 재서(표본화) 각각을 -32768 ~ 32767 정수로 바꾼다.
//   이 정수 하나를 "표본(sample)"이라고 한다. 1초에 16,000개 x 2바이트 = 32,000바이트.
//
// [이 프로그램이 하는 일]
//   1) ALSA(리눅스 오디오 시스템)로 마이크를 연다.
//   2) 표본을 1,024개씩(약 64ms 분량) 읽는다.
//   3) 그 묶음의 소리 세기(RMS)를 계산하고 dBFS 단위로 바꾼다.
//   4) 같은 줄에 막대를 다시 그린다. 소리가 클수록 막대가 길어진다.
//
// [실행]
//   arecord -l                    <- 마이크의 카드 번호와 장치 번호 확인
//   ./visualizer plughw:3,0       <- 카드 3, 장치 0이었다면
//
// [빌드] (src/lab_05 폴더에서)
//   make visualizer
// =============================================================================

#include <alsa/asoundlib.h>  // ALSA: 리눅스 표준 오디오 라이브러리

#include <cerrno>   // EPIPE: 오버런을 뜻하는 에러 번호
#include <cmath>    // sqrt(), log10()
#include <cstdint>  // std::int16_t: 16비트 정수 (표본 하나)
#include <cstdio>   // printf()
#include <cstring>  // strcmp()

#include "../common/signal_stop.hpp"  // Ctrl+C를 눌렀을 때 안전하게 끝내는 기능

namespace {
    constexpr const char* kDevice = "plughw:1,0";  // 기본 장치. arecord -l로 확인해서 바꾼다.
    constexpr unsigned int kRate = 16000;          // 1초에 16,000번 측정 (사람 목소리에 충분)
    constexpr unsigned int kLatencyUs = 100000;    // 드라이버가 모아 두는 양: 100ms
    constexpr snd_pcm_uframes_t kPeriod = 1024;    // 한 번에 읽는 표본 수 (약 64ms마다 막대 갱신)
    constexpr int kBarWidth = 50;                  // 막대 최대 길이 (칸)
    constexpr double kFullScale = 32768.0;         // 16비트 표본의 최대 크기
    constexpr double kFloorDb = -60.0;             // 이보다 작은 소리는 조용함(무음)으로 본다.
}  // namespace

// RMS(제곱 평균 제곱근): 소리 묶음의 "평균 세기".
// 소리 파형은 0을 기준으로 위아래로 흔들리기 때문에, 그냥 평균을 내면 +와 -가 서로
// 지워져서 큰 소리도 0에 가깝게 나온다. 그래서 제곱해서 부호를 없앤 뒤 평균을 내고,
// 마지막에 제곱근을 씌워 원래 단위로 돌려놓는다.
double Rms(const std::int16_t* samples, int count) {
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    return std::sqrt(sum / static_cast<double>(count));
}

// RMS를 dBFS(최대 크기 대비 데시벨)로 바꾼다.
//   dBFS = 20 x log10(RMS / 32768)
//   가장 큰 소리가 0dBFS이고, 작을수록 음수가 된다. (예: 최대의 1/100이면 -40dBFS)
// 사람 귀는 소리의 "차이"보다 "비율"에 반응하기 때문에, 그대로 막대를 그리면
// 작은 소리들이 전부 0칸이 된다. 로그(dB)로 바꾸면 귀가 느끼는 변화와 비슷하게 막대가 변한다.
double RmsToDbfs(double rms) {
    if (rms <= 0.0) return kFloorDb;  // log10(0)은 계산할 수 없어서 바로 하한을 돌려준다.
    const double db = 20.0 * std::log10(rms / kFullScale);
    return db < kFloorDb ? kFloorDb : db;
}

// 녹음을 언제 시작할지 정한다.
// snd_pcm_set_params()는 "버퍼가 가득 찰 만큼(1600프레임) 요청받으면 시작"하도록 정한다.
// 그런데 이 프로그램은 한 번에 1024프레임만 요청하기 때문에, 녹음이 시작되지 않은 채로
// 기다리다가 바로 Input/output error가 난다. (Pi + C270에서 실제로 확인한 문제)
// arecord처럼 시작 기준(start_threshold)을 1로 바꿔서, 첫 읽기에서 바로 녹음이 시작되게 한다.
int StartOnFirstRead(snd_pcm_t* pcm) {
    snd_pcm_sw_params_t* sw = nullptr;
    int rc = snd_pcm_sw_params_malloc(&sw);
    if (rc < 0) return rc;
    rc = snd_pcm_sw_params_current(pcm, sw);  // 지금 설정을 가져와서
    if (rc >= 0) rc = snd_pcm_sw_params_set_start_threshold(pcm, sw, 1);  // 시작 기준만 1로 바꾸고
    if (rc >= 0) rc = snd_pcm_sw_params(pcm, sw);  // 다시 적용한다.
    snd_pcm_sw_params_free(sw);
    return rc;
}

int main(int argc, char* argv[]) {
    // 실행할 때 장치 이름을 주면 그것을, 안 주면 기본값을 쓴다.
    const char* device = argc > 1 ? argv[1] : kDevice;

    // 1) 녹음(캡처) 방향으로 장치를 연다. 마지막 0은 "데이터가 찰 때까지 기다리는" 블로킹 모드.
    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);

    // 2) 형식 설정: 16비트 정수(리틀 엔디언), 1채널(모노), 16kHz, 자동 리샘플링 허용, 100ms 버퍼
    if (rc >= 0) {
        rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                1, kRate, 1, kLatencyUs);
    }
    if (rc >= 0) rc = StartOnFirstRead(pcm);  // 첫 읽기에서 바로 녹음이 시작되게 한다.
    if (rc < 0 || !InstallStopHandler()) {
        // snd_strerror()는 ALSA 에러 번호를 사람이 읽을 수 있는 문장으로 바꿔 준다.
        std::fprintf(stderr, "cannot open %s: %s\n", device, snd_strerror(rc));
        std::fprintf(stderr, "run `arecord -l`, then pass the device, e.g. %s plughw:3,0\n", argv[0]);
        if (pcm != nullptr) snd_pcm_close(pcm);
        return 1;
    }

    // 문제 찾기용: ./visualizer plughw:3,0 -v 로 실행하면 실제로 설정된 값을 출력한다.
    // arecord -v 의 출력과 비교하면 두 프로그램의 설정 차이를 볼 수 있다.
    if (argc > 2 && std::strcmp(argv[2], "-v") == 0) {
        snd_output_t* out = nullptr;
        if (snd_output_stdio_attach(&out, stderr, 0) >= 0) {
            snd_pcm_dump(pcm, out);
            snd_output_close(out);
        }
    }

    std::int16_t samples[kPeriod];
    char bar[kBarWidth + 1];
    std::printf("listening on %s at %u Hz. Ctrl-C to stop\n", device, kRate);

    while (!StopRequested()) {
        // 3) 표본 1,024개가 찰 때까지 기다렸다가 읽는다. 돌려주는 값은 "프레임 수"다.
        const snd_pcm_sframes_t got = snd_pcm_readi(pcm, samples, kPeriod);

        // 음수면 문제가 생긴 것이다. 가장 흔한 것은 오버런(-EPIPE):
        // 이 프로그램이 늦게 읽어서 드라이버 버퍼가 넘쳐 표본을 잃어버린 상황이다.
        // 한 번 삐끗했다고 끝내지 않고, 복구한 뒤 계속 듣는다.
        if (got < 0) {
            if (got == -EPIPE) {
                // 과제 5: 오버런이 일어나고 복구되는 것을 눈으로 확인할 수 있게 알려 준다.
                std::printf("\n[overrun] samples were dropped, recovering\n");
            }
            if (snd_pcm_recover(pcm, static_cast<int>(got), 1) < 0) {
                // 복구할 수 없는 에러. 원인을 알려 주지 않으면 그냥 꺼진 것처럼 보이므로 출력한다.
                std::fprintf(stderr, "\nread failed: %s\n", snd_strerror(static_cast<int>(got)));
                break;
            }
            continue;
        }
        if (got == 0) continue;  // 읽은 게 없으면 RMS를 계산할 수 없다. (0으로 나누기 방지)

        // 4) 소리 세기를 막대 길이로 바꾼다. -60dBFS면 0칸, 0dBFS면 50칸.
        const double dbfs = RmsToDbfs(Rms(samples, static_cast<int>(got)));
        const int length = static_cast<int>((dbfs - kFloorDb) / (0.0 - kFloorDb) * kBarWidth);
        for (int i = 0; i < kBarWidth; ++i) bar[i] = i < length ? '#' : ' ';
        bar[kBarWidth] = '\0';

        // \r은 줄을 바꾸지 않고 커서를 맨 앞으로 보낸다. 그래서 같은 줄이 계속 다시 그려진다.
        // 줄바꿈이 없으면 글자가 버퍼에 머무르므로 fflush로 바로 화면에 내보낸다.
        std::printf("\r%6.1f dBFS |%s|", dbfs, bar);
        std::fflush(stdout);
    }

    std::printf("\nstopped\n");
    return snd_pcm_close(pcm) < 0 ? 1 : 0;
}
