#include "TofCanReader.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <cerrno>

TofCanReader::TofCanReader() : m_sock_fd(-1) {
    m_tof_can_id = 0x200;
    m_obstacle_can_id = 0x300; 
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
    // (기존과 동일)
    m_sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_sock_fd < 0) {
        perror("[ERR] TofCanReader: socket 생성 실패");
        return false;
    }
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
    int flags = fcntl(m_sock_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(m_sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[ERR] TofCanReader: fcntl(O_NONBLOCK) 실패");
        close();
        return false;
    }
    return true;
}

CanData TofCanReader::readMessages() {
    CanData result_data;
    result_data.obstacle_detected = false; 
    result_data.sign_turn_right = false; // ★ 플래그 초기화
    result_data.distance_mm = -1;

    struct can_frame frame;
    int nbytes;
    int dist_mm = -1; 

    while ((nbytes = read(m_sock_fd, &frame, sizeof(struct can_frame))) > 0) {
        
        if (nbytes < sizeof(struct can_frame)) {
            std::cerr << "[WARN] TofCanReader: 불완전한 CAN 프레임 수신" << std::endl;
            continue;
        }

        // 1. ToF 센서 ID 확인
        if (frame.can_id == m_tof_can_id) {
            if (frame.can_dlc >= 2) {
                dist_mm = frame.data[0] | (frame.data[1] << 8);
            }
        }
        // 2. 장애물/표지판 ID 확인
        else if (frame.can_id == m_obstacle_can_id) {
            if (frame.can_dlc >= 1) {
                if (frame.data[0] == 0x01) {
                    result_data.obstacle_detected = true; 
                } 
                else if (frame.data[0] == 0x02) { // ★ 우회전 신호
                    result_data.sign_turn_right = true;
                }
            }
        }
    }

    result_data.distance_mm = dist_mm;
    
    return result_data;
}