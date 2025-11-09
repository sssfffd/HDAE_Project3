#pragma once

#include <string>
#include <sys/socket.h> // sockaddr_in
#include <arpa/inet.h>  // inet_addr

class SomeipSender {
public:
    SomeipSender();
    ~SomeipSender();

    /**
     * @brief RPi IP에서 TC375 IP:Port로 UDP 소켓을 엽니다.
     * @param self_ip RPi의 IP (예: "192.168.2.10")
     * @param dst_ip TC375의 IP (예: "192.168.2.30")
     * @param dst_port TC375의 Port (예: 30509)
     * @return 성공 시 true
     */
    bool open_to(const char* self_ip, const char* dst_ip, int dst_port);

    /**
     * @brief 소켓을 닫습니다.
     */
    void closeSock();

    /**
     * @brief SOME/IP 헤더를 붙여 모터 명령 페이로드를 전송합니다.
     * @param payload "Rspd;Lspd;Rdir;Ldir" 형식의 문자열
     * @return 전송 성공 시 true
     */
    bool sendMotor(const std::string& payload);

private:
    int sock_;
    sockaddr_in dst_{};
    uint32_t session_;
};