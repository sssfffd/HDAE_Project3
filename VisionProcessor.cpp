#include "VisionProcessor.h"
#include <iostream>

using namespace cv;
using namespace std;

VisionProcessor::VisionProcessor() : 
    m_headless(false),
    m_hmin(0), m_hmax(179), 
    m_smin(0), m_smax(80), 
    m_vmin(200), m_vmax(255),
    m_alpha(0.30),
    m_deadband(0.05),
    m_kp(0.6),
    m_ema_error(0.0) 
{
    // lkas_someip.cpp의 파라미터와 동일하게 초기화
}

void VisionProcessor::init_gui(bool headless) {
    m_headless = headless;
    if (!m_headless) {
        // LKAS HSV 값 조절용 트랙바 생성
        namedWindow("LKAS Trackbars", WINDOW_AUTOSIZE);
        createTrackbar("H min","LKAS Trackbars",&m_hmin,179);
        createTrackbar("H max","LKAS Trackbars",&m_hmax,179);
        createTrackbar("S min","LKAS Trackbars",&m_smin,255);
        createTrackbar("S max","LKAS Trackbars",&m_smax,255);
        createTrackbar("V min","LKAS Trackbars",&m_vmin,255);
        createTrackbar("V max","LKAS Trackbars",&m_vmax,255);
        
        // 메인 뷰와 마스크 윈도우 생성
        namedWindow("view", WINDOW_AUTOSIZE);
        namedWindow("mask(roi)", WINDOW_AUTOSIZE);
    }
}

LKASResult VisionProcessor::processFrame(Mat& frame) {
    
    LKASResult result;
    
    // 1. ROI: 하단 50%
    int y0 = frame.rows * 0.5;
    Rect roiRect(0, y0, frame.cols, frame.rows - y0);
    
    // 2. HSV & Threshold
    static Mat hsv, mask, roiMask; // (static으로 선언하여 매번 생성 방지)
    cvtColor(frame, hsv, COLOR_BGR2HSV);
    inRange(hsv, Scalar(m_hmin, m_smin, m_vmin), 
                 Scalar(m_hmax, m_smax, m_vmax), mask);

    // 3. ROI 마스크 및 노이즈 제거
    roiMask = mask(roiRect).clone(); // ROI 영역만 복사
    morphologyEx(roiMask, roiMask, MORPH_OPEN,  getStructuringElement(MORPH_RECT, Size(3,3)));
    morphologyEx(roiMask, roiMask, MORPH_CLOSE, getStructuringElement(MORPH_RECT, Size(5,5)));

    // 4. 중심점(Centroid) → 오차 계산
    Moments m = moments(roiMask, true);
    result.line_found = (m.m00 > 1e3); // (픽셀이 1000개 이상일 때만 유효)
    
    double cx = result.line_found ? (m.m10 / m.m00) : (frame.cols / 2.0);
    result.center_x = (int)cx;
    
    // -1.0 ~ +1.0 사이의 오차
    double e = (cx - (frame.cols / 2.0)) / (frame.cols / 2.0);
    
    // 5. EMA 필터 (오차 스무딩)
    m_ema_error = m_alpha * e + (1.0 - m_alpha) * m_ema_error;
    result.error = m_ema_error;

    // 6. 판단 (Decision)
    int decided = 0;
    if (result.line_found && fabs(m_ema_error) >= m_deadband) {
        decided = (m_ema_error > 0.0) ? 1 : -1; // (오른쪽: 1, 왼쪽: -1)
    }
    
    // 차선을 찾았을 때만 조향 모드 갱신
    if (result.line_found) {
        result.drive_mode = decided;
    } else {
        // 차선을 잃으면 기존 모드를 유지 (이 결정은 main.cpp가 함)
        result.drive_mode = 0; // (일단 0으로 리포트)
    }

    if (!m_headless) {
        imshow("mask(roi)", roiMask);
    }
    
    return result;
}

void VisionProcessor::visualize(Mat& vis, const LKASResult& result) {
    if (m_headless) return;
    
    // ROI 영역 표시
    Rect roiRect(0, vis.rows * 0.5, vis.cols, vis.rows * 0.5);
    rectangle(vis, roiRect, Scalar(0,255,255), 1);
    
    // 중앙선 표시
    line(vis, Point(vis.cols/2, 0), Point(vis.cols/2, vis.rows), Scalar(0,255,0), 1);
    
    // 차선 중심점 표시
    if (result.line_found) {
        Point p(result.center_x, (int)(vis.rows * 0.75)); // ROI의 중앙
        circle(vis, p, 4, Scalar(0,0,255), -1);
    }

    // 상태 텍스트 표시
    const char* action = "STRAIGHT";
    if (result.drive_mode > 0) action = "STEER RIGHT";
    else if (result.drive_mode < 0) action = "STEER LEFT";
    
    putText(vis, action, Point(8,18), FONT_HERSHEY_SIMPLEX, 0.55, Scalar(0,255,255), 2);
    putText(vis, format("mode=%d  e=%.3f", result.drive_mode, result.error),
            Point(8,40), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,255,0), 1);
    
    // (ACC 시각화는 main.cpp에서 별도 처리)
}