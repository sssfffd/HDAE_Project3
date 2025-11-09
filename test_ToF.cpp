/**
 * @file test_tof_can.cpp
 * @brief SocketCAN을 이용해 ToF 센서 데이터를 읽어옵니다. (ID 0x200 수정)
 *
 * [수정 사항]
 * 1. TOFSEN_CAN_ID를 0x401에서 0x200으로 변경 (사용자 로그 기반)
 * 2. 파싱 로직(data[0], data[1])은 그대로 유지 (로그 기반)
 * 3. fcntl.h, cerrno 포함 (논-블로킹 설정 및 에러 확인용)
 * 4. socket을 O_NONBLOCK (논-블로킹) 모드로 설정
 * 5. read()가 -1을 반환할 때, EAGAIN/EWOULDBLOCK 에러는 데이터가 없는 정상이므로 무시
 * 6. 루프 마지막에 sleep(10ms)을 추가하여 CPU 사용률 100% 방지 및 Ctrl+C 반응
 *
 * [컴파일 방법]
 * g++ -o test_tof_can test_tof_can.cpp -std=c++17
 *
 * [★★★ 실행 전 설정 ★★★]
 * 1. RS485 CAN HAT에 맞는 드라이버가 설치되어 있어야 합니다.
 * 2. Tofsen 센서의 Baudrate에 맞게 CAN 인터페이스를 활성화해야 합니다.
 * (예: 250kbps일 경우)
 * sudo ip link set can0 type can bitrate 250000
 * sudo ip link set up can0
 *
 * [실행 방법]
 * ./test_tof_can
 */

#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <cstring>      // C 표준 (memset, strerror)
#include <unistd.h>     // C 표준 (close, read)
#include <sys/socket.h> // C 표준 (socket)
#include <sys/ioctl.h>  // C 표준 (ioctl)
#include <net/if.h>     // C 표준 (ifreq)

#include <linux/can.h>      // SocketCAN 헤더
#include <linux/can/raw.h>  // SocketCAN 헤더

#include <fcntl.h>      // ★★★ O_NONBLOCK 설정을 위해 추가
#include <cerrno>       // ★★★ errno (EAGAIN, EWOULDBLOCK)를 위해 추가

using namespace std;

// --- 시스템 종료 플래그 ---
static atomic<bool> g_running{true};
static void on_sigint(int){ g_running.store(false); cerr << "\n[SYS] 종료 신호 수신...\n"; }

// ★★★ (수정) Tofsen 센서 CAN ID (로그에서 0x200 확인) ★★★
static const canid_t TOFSEN_CAN_ID = 0x200; 


int main() {
    signal(SIGINT, on_sigint);

    int sock_fd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    // --- 1. SocketCAN 소켓 생성 ---
    sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock_fd < 0) {
        perror("[ERR] SocketCAN: socket 생성 실패");
        return 1;
    }

    // --- 2. 'can0' 인터페이스에 소켓 바인딩 ---
    // (이 코드는 "can0" 인터페이스가 존재한다고 가정합니다)
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("[ERR] SocketCAN: 'can0' 인터페이스를 찾을 수 없습니다. (HAT 드라이버 로드 및 'sudo ip link...' 명령어 실행 필요)");
        ::close(sock_fd);
        return 1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERR] SocketCAN: bind 실패");
        ::close(sock_fd);
        return 1;
    }

    // --- 3. ★★★ 소켓을 논-블로킹 모드로 변경 ★★★
    int flags = fcntl(sock_fd, F_GETFL, 0); // 기존 플래그 읽기
    if (flags < 0) {
        perror("[ERR] fcntl(F_GETFL) 실패");
        ::close(sock_fd);
        return 1;
    }
    // 기존 플래그에 O_NONBLOCK 플래그 추가
    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[ERR] fcntl(F_SETFL, O_NONBLOCK) 실패");
        ::close(sock_fd);
        return 1;
    }

    cout << "[INFO] SocketCAN 'can0' (논-블로킹) 준비완료." << endl;
    cout << "CAN 버스에서 데이터를 수신 대기합니다... (Ctrl+C로 종료)" << endl;

    // --- 4. ★★★ 메인 루프: 논-블로킹 Read ★★★
    while (g_running.load()) { // g_running이 false가 되면 루프 탈출
        
        struct can_frame frame; // CAN 데이터 패킷
        
        // (A) 논-블로킹 읽기 시도
        int nbytes = read(sock_fd, &frame, sizeof(struct can_frame));

        if (nbytes < 0) {
            // --- 논-블로킹 에러 처리 ---
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // (이것은 정상입니다) 
                // "읽을 데이터가 아직 없음"을 의미합니다.
                // 루프가 멈추지 않고 계속 돌 수 있습니다.
            } else {
                // (이것은 실제 에러입니다) 
                // 예: Network is down
                perror("[WARN] CAN read 에러");
                // 에러가 발생해도 계속 시도 (Ctrl+C로 종료 가능)
            }

        } else if (nbytes < sizeof(struct can_frame)) {
            // (에러) 불완전한 프레임 수신
            cerr << "[WARN] 불완전한 CAN 프레임 수신" << endl;
        
        } else {
            // ★★★ (정상) 데이터 수신 성공 ★★★
            cout << "[CAN RX] ID: 0x" << hex << frame.can_id 
                 << "  DLC: " << (int)frame.can_dlc << "  Data: ";
            
            for (int i = 0; i < frame.can_dlc; i++) {
                printf("%02X ", frame.data[i]);
            }
            cout << dec << endl;

            // (C) Tofsen 데이터 파싱 (ID: 0x200)
            if (frame.can_id == TOFSEN_CAN_ID) {
                
                // (파싱 로직: 첫 2바이트를 LSB/MSB 16비트 정수로 읽음)
                // (로그 '2C 00' -> 0x002C -> 44)
                if (frame.can_dlc >= 2) {
                    int dist_mm = frame.data[0] | (frame.data[1] << 8);
                    
                    // ★★★ 이제 이 줄이 출력될 것입니다 ★★★
                    cout << "  -> [TOF] 거리: " << dist_mm << " mm" << endl;
                }
            }
        }
        
        // ★★★ CPU 100% 사용을 막고, 종료 신호에 반응할 시간을 줌 ★★★
        // 이 sleep 덕분에 g_running 플래그를 확인할 여유가 생깁니다.
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    // --- 5. 종료 처리 ---
    // (Ctrl+C를 누르면 루프를 탈출하고 이 코드가 실행됩니다)
    ::close(sock_fd);
    cout << "[INFO] SocketCAN 닫힘." << endl;

    return 0;
}