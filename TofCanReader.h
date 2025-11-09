#pragma once

#include <string>
#include <linux/can.h> // canid_t를 사용하기 위해 포함

class TofCanReader {
public:
    TofCanReader();
    ~TofCanReader();

    /**
     * @brief "can0"와 같은 CAN 인터페이스를 엽니다.
     * @param interface_name (예: "can0")
     * @return 성공 시 true
     */
    bool open(const std::string& interface_name);

    /**
     * @brief CAN 소켓을 닫습니다.
     */
    void close();

    /**
     * @brief CAN 버스에서 ToF 센서 데이터를 읽고 파싱합니다. (논-블로킹)
     * @return (성공) 0 이상의 거리 (mm).
     * @return (실패/데이터 없음) -1
     */
    int getDistanceMm();

private:
    int m_sock_fd; // SocketCAN 파일 디스크립터
    canid_t m_tof_can_id; // 우리가 찾은 ToF 센서 ID
};