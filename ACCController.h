#pragma once

class ACCController {
public:
    /**
     * @brief ACC 로직을 위한 파라미터 초기화
     */
    ACCController();

    /**
     * @brief ToF 센서 거리(m)를 기반으로 TC375에 보낼 '기본 속도'(0~100)를 계산
     * @param frontDistance_m 전방 물체와의 거리 (미터 단위)
     * @return 0~100 사이의 모터 속도 명령
     */
    int computeBaseSpeed(double frontDistance_m);

private:
    /**
     * @brief 계산된 물리 속도(m/s)를 모터 명령(0~100)으로 변환
     */
    int scaleSpeedToCommand(double speed_ms);

    // --- 1. 물리 단위 파라미터 (튜닝 필요) ---
    double m_maxSpeed_ms;           // 최대 속도 (m/s)
    double m_baseSpeed_ms;          // 기본 속도 (m/s)
    double m_stopDistance_m;        // 비상 정지 거리 (m)
    double m_targetSafeDistance_m;  // 목표 안전 거리 (m)
    double m_acc_Kp;                // 비례 게인 (P-Controller)

    // --- 2. 모터 명령 변환 파라미터 (튜닝 필요) ---
    int m_speedCmd_Max;        // TC375의 최대 속도 명령 (예: 100)
    int m_speedCmd_Min_Run;    // 모터가 실제로 돌기 시작하는 최소 명령 (예: 40)
};