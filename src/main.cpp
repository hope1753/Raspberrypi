#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

constexpr int PORT_CHANNEL = 1;      // RFCOMM 채널 번호
constexpr size_t MAX_CLIENTS = 2;    // 1:1 채팅을 위한 최대 클라이언트 수

std::vector<int> client_sockets;
std::mutex clients_mutex;
std::mutex cout_mutex;               // 여러 스레드의 로그 출력이 섞이지 않도록 보호

void log(const std::string& line) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << line << std::endl;
}

// 부분 전송(partial write)까지 처리하여 전체 데이터를 보낸다.
// MSG_NOSIGNAL: 끊긴 소켓에 써도 SIGPIPE로 프로세스가 죽지 않도록 함
bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// 자신을 제외한 다른 클라이언트에게 메시지 중계 (Relay)
void relay(int from_fd, const std::string& msg) {
    std::vector<int> targets;
    {
        // 락은 대상 목록 복사에만 사용하고, blocking 전송은 락 밖에서 수행
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (int sock : client_sockets) {
            if (sock != from_fd) targets.push_back(sock);
        }
    }
    for (int sock : targets) {
        send_all(sock, msg);
    }
}

// 연결된 클라이언트의 메시지를 수신하여 상대방에게 전달하는 스레드 함수
void handle_client(int client_fd, int client_id) {
    char buffer[1024];
    std::string pending;  // 스트림이므로 '\n' 단위로 메시지를 잘라 처리
    log("[Server] Client " + std::to_string(client_id) + " connected.");

    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read < 0 && errno == EINTR) continue;
        if (bytes_read <= 0) {
            log("[Server] Client " + std::to_string(client_id) + " disconnected.");
            break;
        }

        pending.append(buffer, static_cast<size_t>(bytes_read));

        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            log("[Client " + std::to_string(client_id) + "]: " + line);
            relay(client_fd, "[Peer " + std::to_string(client_id) + "]: " + line + "\n");
        }
    }

    // 목록에서 먼저 제거한 뒤 close (fd 번호 재사용으로 인한 경쟁 상태 방지)
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        client_sockets.erase(std::remove(client_sockets.begin(), client_sockets.end(), client_fd),
                             client_sockets.end());
    }
    close(client_fd);
}

int main() {
    // 끊긴 소켓에 write 시 SIGPIPE로 서버 전체가 종료되는 것을 방지
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };

    // 1. 블루투스 RFCOMM 소켓 생성
    int server_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // 2. 소켓 바인딩 (모든 로컬 어댑터)
    bdaddr_t any_addr = {{0, 0, 0, 0, 0, 0}};  // BDADDR_ANY
    loc_addr.rc_family = AF_BLUETOOTH;
    bacpy(&loc_addr.rc_bdaddr, &any_addr);
    loc_addr.rc_channel = static_cast<uint8_t>(PORT_CHANNEL);

    if (bind(server_fd, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // 3. 수신 대기 (Listen)
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }
    log("[Bluetooth Relay Server] Waiting for connections on RFCOMM channel " +
        std::to_string(PORT_CHANNEL) + "...");

    int client_counter = 1;

    // 4. 클라이언트 접속 수락 루프
    while (true) {
        socklen_t opt = sizeof(rem_addr);  // accept가 값을 바꾸므로 매번 초기화
        int client_fd = accept(server_fd, (struct sockaddr *)&rem_addr, &opt);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        char rem_addr_str[18] = { 0 };
        ba2str(&rem_addr.rc_bdaddr, rem_addr_str);

        {
            // 1:1 채팅이므로 최대 접속 수를 직접 제한
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (client_sockets.size() >= MAX_CLIENTS) {
                send_all(client_fd, "[Server] Room is full.\n");
                close(client_fd);
                log(std::string("[Server] Rejected connection from ") + rem_addr_str + " (room full)");
                continue;
            }
            client_sockets.push_back(client_fd);
        }
        log(std::string("[Server] Accepted connection from ") + rem_addr_str);

        std::thread(handle_client, client_fd, client_counter++).detach();
    }

    close(server_fd);
    return 0;
}
