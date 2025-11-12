#pragma once

#include <string>
#include <linux/can.h> // canid_t를 사용하기 위해 포함
#include <atomic>      

// 1. 반환 타입을 위한 구조체 정의
struct CanData {
    int distance_mm = -1;       
    bool obstacle_detected = false; 
    bool sign_turn_right = false; // ★ 우회전 표지판 감지 플래그
};

class TofCanReader {
public:
    TofCanReader();
    ~TofCanReader();

    bool open(const std::string& interface_name);
    void close();

    /**
     * @brief CAN 버스의 모든 메시지(ToF, Obstacle, Sign)를 읽고 파싱합니다. (논-블로킹)
     * @return CanData 구조체
     */
    CanData readMessages(); 

private:
    int m_sock_fd;
    canid_t m_tof_can_id;
    canid_t m_obstacle_can_id; 
};