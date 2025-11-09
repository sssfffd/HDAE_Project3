#include "ACCController.h"
#include <iostream>
#include <algorithm> // for std::max, std::min

ACCController::ACCController() {
    // --- 1. 물리 단위 파라미터 (튜닝) ---
    m_maxSpeed_ms = 1.2;      // [1.5m/s일 때 -> 명령 100]
    m_baseSpeed_ms = 1.0;     // [1.0m/s일 때 -> 명령 80] (목표 달성!)
    m_stopDistance_m = 0.4;   // (안전 강화) 정지 거리를 30cm -> 40cm로 약간 늘림
    m_targetSafeDistance_m = 1.0; // (안전 강화) 목표 안전 거리를 80cm -> 1m로 늘림
    m_acc_Kp = 0.6;           // (부드럽게) 속도 변화 반응성을 0.8 -> 0.6으로 낮춤

    // --- 2. 모터 명령 변환 파라미터 ---
    m_speedCmd_Max = 90;
    m_speedCmd_Min_Run = 40;  // 40 이하는 모터가 안 돈다고 가정
}

int ACCController::scaleSpeedToCommand(double speed_ms) {
    if (speed_ms <= 0.0) return 0;
    if (speed_ms >= m_maxSpeed_ms) return m_speedCmd_Max;

    // 선형 변환: (speed_ms / 1.5) * 60 + 40
    double ratio = speed_ms / m_maxSpeed_ms;
    int cmd = m_speedCmd_Min_Run + (int)(ratio * (m_speedCmd_Max - m_speedCmd_Min_Run));
    
    return std::max(m_speedCmd_Min_Run, std::min(m_speedCmd_Max, cmd));
}

int ACCController::computeBaseSpeed(double frontDistance_m) {
    if (frontDistance_m < m_stopDistance_m) return 0; // 비상 정지

    double error = frontDistance_m - m_targetSafeDistance_m;
    double targetSpeed_ms = m_baseSpeed_ms + (m_acc_Kp * error);

    // 클램핑
    targetSpeed_ms = std::max(0.0, std::min(m_maxSpeed_ms, targetSpeed_ms));

    return scaleSpeedToCommand(targetSpeed_ms);
}