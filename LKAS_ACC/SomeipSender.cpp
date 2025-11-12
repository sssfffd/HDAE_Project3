#include "SomeipSender.h"
#include <iostream>
#include <vector>
#include <cstring>  // C 표준 (memset, memcpy)
#include <unistd.h> // C 표준 (close)
#include <cstdint>  // C 표준 (uint8_t, uint32_t)

// SOME/IP 헤더 상수 (lkas_someip.cpp에서 가져옴)
static const uint16_t CLIENT_ID        = 0x1111;
static const uint8_t  IFACE_VER        = 0x01;
static const uint8_t  PROTO_VER        = 0x01;
static const uint8_t  MSG_TYPE_REQUEST = 0x00;
static const uint8_t  RET_OK           = 0x00;
static const uint16_t SERVICE_ID_COMMON= 0x0100;
static const uint16_t METHOD_ID_MOTOR  = 0x0201;

SomeipSender::SomeipSender(): sock_(-1), session_(1) {}

SomeipSender::~SomeipSender() {
    closeSock();
}

void SomeipSender::closeSock() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool SomeipSender::open_to(const char* self_ip, const char* dst_ip, int dst_port) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { 
        perror("[ERR] SomeipSender: udp socket"); 
        return false; 
    }

    // RPi 자신의 IP로 소켓 바인딩 (lkas_someip.cpp의 로직과 동일)
    sockaddr_in src{}; 
    src.sin_family = AF_INET;
    src.sin_addr.s_addr = inet_addr(self_ip);
    src.sin_port = htons(0); // (아무 포트나 사용)
    if (bind(sock_, (sockaddr*)&src, sizeof(src)) < 0) {
        perror("[ERR] SomeipSender: udp bind"); 
        closeSock(); 
        return false;
    }

    // TC375 목적지 설정
    memset(&dst_, 0, sizeof(dst_));
    dst_.sin_family = AF_INET;
    dst_.sin_addr.s_addr = inet_addr(dst_ip);
    dst_.sin_port = htons(dst_port);
    return true;
}

bool SomeipSender::sendMotor(const std::string& payload) {
    if (sock_ < 0) return false;

    // SOME/IP 헤더 생성 (16 바이트)
    uint32_t msg_id = (uint32_t(SERVICE_ID_COMMON) << 16) | uint32_t(METHOD_ID_MOTOR);
    uint32_t req_id = (uint32_t(CLIENT_ID) << 16) | (session_++ & 0xFFFF);
    uint32_t length = 8 + payload.size(); // 8(헤더일부) + 페이로드
    
    std::vector<uint8_t> pkt(16 + payload.size());

    // Big-Endian 32비트 정수 변환 함수
    auto be32 = [](uint8_t* p, uint32_t v){ 
        p[0]=uint8_t(v>>24); p[1]=uint8_t(v>>16); p[2]=uint8_t(v>>8); p[3]=uint8_t(v); 
    };

    be32(&pkt[0],  msg_id);
    be32(&pkt[4],  length);
    be32(&pkt[8],  req_id);
    pkt[12] = PROTO_VER; 
    pkt[13] = IFACE_VER; 
    pkt[14] = MSG_TYPE_REQUEST; 
    pkt[15] = RET_OK;

    // 페이로드 복사
    if(!payload.empty()) {
        memcpy(&pkt[16], payload.data(), payload.size());
    }

    // UDP 전송
    ssize_t sent = ::sendto(sock_, pkt.data(), pkt.size(), 0, (sockaddr*)&dst_, sizeof(dst_));
    if (sent < 0) { 
        perror("[WARN] SomeipSender: sendto"); 
        return false; 
    }
    return true;
}