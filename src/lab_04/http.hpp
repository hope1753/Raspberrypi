// =============================================================================
// 실습 4: 브라우저와 대화할 수 있는 가장 작은 HTTP 서버
//
// [HTTP 서버가 하는 일]
//   1) 8080번 포트를 열고 브라우저의 접속을 기다린다. (listen)
//   2) 접속이 오면 받아들이고(accept), 요청 내용을 읽는다. (read)
//   3) "HTTP/1.1 200 OK"로 시작하는 답장을 보내고 연결을 끊는다. (write, close)
//   한 번에 한 요청만 처리하고, 연결을 계속 유지하지 않는(keep-alive 없음) 단순한 서버다.
//
// [슬라이드 코드와 다른 점]
//   - 요청을 보내지 않고 가만히 있는 접속 때문에 서버 전체가 멈추지 않도록,
//     읽기에 1초 제한 시간을 걸었다.
//   - 브라우저가 답장을 다 받기 전에 창을 닫아도 프로그램이 죽지 않도록,
//     send()에 MSG_NOSIGNAL을 붙였다. (블루투스 서버에서 겪었던 SIGPIPE 문제와 같다.)
// =============================================================================
#ifndef MEP_LAB04_HTTP_HPP_
#define MEP_LAB04_HTTP_HPP_

#include <arpa/inet.h>   // htonl(), htons(): 네트워크 바이트 순서로 바꾸기
#include <netinet/in.h>  // sockaddr_in: IPv4 주소 구조체
#include <poll.h>        // poll(): 제한 시간을 두고 기다리기
#include <sys/socket.h>  // socket(), bind(), listen(), accept(), send()
#include <sys/time.h>    // timeval: 읽기 제한 시간
#include <unistd.h>      // read(), close()

#include <cstdint>
#include <cstdio>
#include <cstring>

class Http {
public:
    // 포트를 열고 접속을 받을 준비를 한다. 실패하면 ok()가 false가 된다.
    explicit Http(int port) : listener_(::socket(AF_INET, SOCK_STREAM, 0)) {
        if (listener_ < 0) return;

        const int reuse_addr_enabled = 1;
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 모든 네트워크 카드에서 받는다.
        addr.sin_port = htons(static_cast<std::uint16_t>(port));

        // 프로그램을 껐다 바로 켜도 "이미 사용 중인 포트" 에러가 나지 않게 한다.
        if (::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_enabled, sizeof(reuse_addr_enabled)) != 0 ||
            ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listener_, 4) != 0) {  // 대기열에 최대 4명까지 줄 세운다.
            ::close(listener_);
            listener_ = -1;
        }
    }

    ~Http() {
        if (listener_ >= 0) ::close(listener_);
    }

    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    bool ok() const { return listener_ >= 0; }

    // 최대 timeout_ms 동안 접속을 기다린다. 아무도 안 오면 -1.
    // 무한정 기다리지 않는 이유: 그사이 Ctrl+C가 눌렸는지 확인하기 위해서다.
    int Accept(int timeout_ms) const {
        pollfd descriptor;
        descriptor.fd = listener_;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        if (::poll(&descriptor, 1, timeout_ms) <= 0) return -1;
        if ((descriptor.revents & POLLIN) == 0) return -1;

        const int client = ::accept(listener_, nullptr, nullptr);
        if (client >= 0) {
            // 요청을 1초 안에 보내지 않는 접속은 포기한다.
            timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        }
        return client;
    }

    // 요청 첫 줄이 path를 원하는지 확인한다.
    // 예) "GET /api HTTP/1.1" 이면 Wants(client, "/api")가 true.
    static bool Wants(int client, const char* path) {
        char request[512];
        const ssize_t got = ::read(client, request, sizeof(request) - 1);
        if (got <= 0) return false;
        request[got] = '\0';
        return std::strstr(request, path) != nullptr;
    }

    // 머리(헤더)와 몸통(본문)으로 된 HTTP 답장을 보낸다.
    static bool Send(int client, const char* type, const char* body, int length) {
        char head[160];
        const int head_len = std::snprintf(head, sizeof(head),
                                           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                                           "Connection: close\r\n\r\n",
                                           type, length);
        if (head_len < 0 || static_cast<size_t>(head_len) >= sizeof(head)) return false;
        return SendAll(client, head, static_cast<size_t>(head_len)) &&
               SendAll(client, body, static_cast<size_t>(length));
    }

private:
    // 한 번에 다 보내지 못할 수 있어서, 다 보낼 때까지 반복한다.
    static bool SendAll(int fd, const char* data, size_t size) {
        size_t sent = 0;
        while (sent < size) {
            const ssize_t n = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    int listener_;
};

// 브라우저에 보낼 대시보드 화면. 1초마다 /api에서 값을 받아 표를 다시 그린다.
// 실패한 센서는 0 대신 "--"로 빨갛게 표시한다. 맨 아래에 수집 시각(과제 2)을 보여 준다.
const char* const kDashboard =
    "<!doctype html>
        <meta charset=utf-8>
            <title>MEP Sensor Hub</title>"
    "       <style>
                body {
                    font-family:sans-serif; 
                    margin:2rem
                }"
                "td {
                    padding:.4rem 1rem;
                    border-bottom:1px solid #ddd
                }"
                ".bad {
                    color:#c0392b
                }
                #ts {
                    color:#777;
                    margin-top:1rem
                }
            </style>"
    "<h1>Sensor Hub</h1>
        <table id=t>
        </table>
        <div id=ts>
        </div>"
    "   <script>
        setInterval(async()=>{"
    "       const r=await(await fetch('/api')).json();"
    "       document.getElementById('t').innerHTML=r.sensors.map(s=>`<tr><td>${s.name}</td><td class=${s.ok?'':'bad'}>${s.ok?s.value.toFixed(2)+' '+s.unit:'--'}</td></tr>`).join('');document.getElementById('ts').textContent='collected at '+r.timestamp;},1000)
    </script>";

#endif  // MEP_LAB04_HTTP_HPP_
