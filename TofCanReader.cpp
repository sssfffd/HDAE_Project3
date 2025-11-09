#include "TofCanReader.h"
#include <iostream>
#include <cstring>      // C 표준 (memset, strerror)
#include <unistd.h>     // C 표준 (close, read)
#include <sys/socket.h> // C 표준 (socket)
#include <sys/ioctl.h>  // C 표준 (ioctl)
#include <net/if.h>     // C 표준 (ifreq)
#include <fcntl.h>      // O_NONBLOCK
#include <cerrno>       // errno, EAGAIN

TofCanReader::TofCanReader() : m_sock_fd(-1) {
    // 우리가 로그에서 확인한 실제 CAN ID
    m_tof_can_id = 0x200; 
}

TofCanReader::~TofCanReader() {
    close();
}

void TofCanReader::close() {
    if (m_sock_fd != -1) {
        ::close(m_sock_fd);
        m_sock_fd = -1;
    }
}

bool TofCanReader::open(const std::string& interface_name) {
    // 1. SocketCAN 소켓 생성
    m_sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_sock_fd < 0) {
        perror("[ERR] TofCanReader: socket 생성 실패");
        return false;
    }

    // 2. CAN 인터페이스에 바인딩
    struct sockaddr_can addr;
    struct ifreq ifr;
    strcpy(ifr.ifr_name, interface_name.c_str());
    if (ioctl(m_sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("[ERR] TofCanReader: 'can0' 인터페이스를 찾을 수 없습니다.");
        close();
        return false;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERR] TofCanReader: bind 실패");
        close();
        return false;
    }

    // 3. 소켓을 논-블로킹 모드로 변경 (main 루프가 멈추지 않도록)
    int flags = fcntl(m_sock_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(m_sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[ERR] TofCanReader: fcntl(O_NONBLOCK) 실패");
        close();
        return false;
    }

    return true;
}

int TofCanReader::getDistanceMm() {
    if (m_sock_fd < 0) return -1;

    struct can_frame frame;
    int nbytes = read(m_sock_fd, &frame, sizeof(struct can_frame));

    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1; // (정상) 데이터가 아직 없음
        }
        perror("[WARN] TofCanReader: CAN read 에러");
        return -1;
    }

    if (nbytes < sizeof(struct can_frame)) {
        std::cerr << "[WARN] TofCanReader: 불완전한 CAN 프레임 수신" << std::endl;
        return -1;
    }

    // --- 수신된 데이터가 ToF 센서 ID와 일치하는지 확인 ---
    if (frame.can_id == m_tof_can_id) {
        // (로그에서 확인한 파싱 로직: data[0] | (data[1] << 8))
        if (frame.can_dlc >= 2) {
            int dist_mm = frame.data[0] | (frame.data[1] << 8);
            return dist_mm; // 거리(mm) 반환!
        }
    }
    
    // (ToF ID가 아니거나, 파싱에 실패함)
    return -1; 
}