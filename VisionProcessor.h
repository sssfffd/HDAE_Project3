#pragma once

#include <opencv2/opencv.hpp>

// LKAS 처리 결과를 담을 구조체
struct LKASResult {
    int drive_mode = 0;   // -1 (좌), 0 (직진), 1 (우)
    bool line_found = false; // 차선 감지 여부
    double error = 0.0;    // -1.0 ~ +1.0 사이의 오차
    int center_x = 0;     // 이미지 상의 차선 중심 x좌표
};

class VisionProcessor {
public:
    VisionProcessor();

    /**
     * @brief GUI (트랙바, 윈도우)를 초기화합니다.
     * @param headless true이면 GUI를 생성하지 않음
     */
    void init_gui(bool headless);

    /**
     * @brief 한 프레임을 받아 LKAS 로직을 처리합니다.
     * @param frame 카메라 원본 프레임
     * @return LKASResult 구조체
     */
    LKASResult processFrame(cv::Mat& frame);

    /**
     * @brief 시각화 이미지(vis)에 LKAS 정보를 그립니다.
     * @param vis 시각화할 Mat 객체 (frame.clone())
     * @param result processFrame에서 반환된 결과
     */
    void visualize(cv::Mat& vis, const LKASResult& result);

private:
    bool m_headless;
    
    // LKAS 파라미터 (lkas_someip.cpp에서 가져옴)
    int m_hmin, m_hmax, m_smin, m_smax, m_vmin, m_vmax;
    double m_alpha;
    double m_deadband;
    double m_kp;
    
    // EMA 필터링을 위한 내부 변수
    double m_ema_error; 
};