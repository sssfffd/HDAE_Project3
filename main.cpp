#include <iostream>
#include <atomic>
#include <csignal>
#include <string>
#include <chrono>
#include <thread>
#include <opencv2/opencv.hpp>
#include <algorithm>

#include "SomeipSender.h"
#include "VisionProcessor.h"
#include "ACCController.h"
#include "TofCanReader.h"

using namespace std;
using namespace cv;

// ===== 설정 (기존과 동일) =====
static const char* PI_IP = "192.168.137.39";
static const char* CTRL_IP = "192.168.2.30";
static const int CTRL_PORT = 30509;
static const int CAM_INDEX = 0;
static const int WIDTH = 320, HEIGHT = 240;
static const int TX_PERIOD_MS = 100;
static const int TURN_DELTA = 16;

static atomic<bool> g_running{true};
static void on_sigint(int){ g_running.store(false); cerr << "\n[SYS] SIGINT\n"; }

static string build_motor_cmd(int drive_mode, int base_speed) {
    int Rspd=0, Lspd=0;
    int Rdir=1, Ldir=1;
    if (drive_mode == 0) { Rspd = base_speed; Lspd = base_speed; }
    else if (drive_mode < 0) { Rspd = base_speed + TURN_DELTA; Lspd = 0; }
    else { Rspd = 0; Lspd = base_speed + TURN_DELTA; }
    Rspd = max(0, min(100, Rspd)); Lspd = max(0, min(100, Lspd));
    return to_string(Rspd)+";"+to_string(Lspd)+";"+to_string(Rdir)+";"+to_string(Ldir);
}

// ==========================================================
// ===== 1. 상태(State) 정의 =====
// ==========================================================
enum VehicleState {
    STATE_LANE_FOLLOWING  // 기본: 차선 유지 및 ACC 주행
};

// (디버깅용) 상태 이름을 문자열로
const char* STATE_NAMES[] = {
    "LANE_FOLLOWING"
};
// ==========================================================


int main(int argc, char** argv) {
    signal(SIGINT, on_sigint);
    bool headless = (argc > 1 && string(argv[1]) == "--headless");
    
    SomeipSender tx;
    VisionProcessor lkas_module;
    ACCController acc_module;
    VideoCapture cap;
    TofCanReader tof_reader;

    // ----- 초기화 (기존과 동일) -----
    if (!tx.open_to(PI_IP, CTRL_IP, CTRL_PORT)) { cerr << "[ERR] SOME/IP fail\n"; return 1; }
    cout << "[INFO] SOME/IP Ready\n";

    cout << "[INFO] Opening Camera " << CAM_INDEX << "..." << endl;
    cap.open(CAM_INDEX, CAP_V4L2);
    if (!cap.isOpened()) {
        cerr << "[ERR] Camera open FAILED!\n";
        return 1;
    }
    cout << "[INFO] Camera opened successfully.\n";

    if (!tof_reader.open("can0")) { cerr << "[ERR] CAN fail\n"; return 1; }
    cout << "[INFO] CAN Ready\n";

    lkas_module.init_gui(headless);
    // -------------------------------

    Mat frame, vis; // 'vis'는 기존 코드와 동일하게 비어있는 상태로 선언
    auto lastTxTime = chrono::steady_clock::now();
    int last_drive_mode = 0;
    int last_base_speed = 0;
    int last_good_tof_mm = 5000;
    int fail_count = 0;
    
    // ===== 2. 초기 상태 지정 =====
    VehicleState currentState = STATE_LANE_FOLLOWING;
    LKASResult lkas; 

    cout << "[INFO] Starting Main Loop...\n";

    while (g_running.load()) {
        // (A) 센서 읽기 (모든 상태에서 공통으로 필요)
        if (!cap.read(frame) || frame.empty()) {
            fail_count++;
            if (fail_count % 10 == 0) {
                cerr << "[WARN] Camera frame EMPTY! (Retrying... " << fail_count << ")\r" << flush;
            }
            this_thread::sleep_for(chrono::milliseconds(10));
            continue; 
        }
        if (fail_count > 0) { cerr << "\n[INFO] Camera recovered!\n"; fail_count = 0; }

        int tof_mm = tof_reader.getDistanceMm();
        if (tof_mm >= 0) last_good_tof_mm = tof_mm;
        double current_distance_m = last_good_tof_mm / 1000.0;

        // ==========================================================
        // ===== 3. 상태 머신(State Machine) =====
        // ==========================================================
        switch (currentState) {
            
            case STATE_LANE_FOLLOWING: {
                // 1. LKAS 로직 수행
                lkas = lkas_module.processFrame(frame);
                
                // 2. ACC 로직 수행
                int base_speed = acc_module.computeBaseSpeed(current_distance_m);

                // 3. 결정값 업데이트 (기존 main.cpp 로직과 동일)
                if (lkas.line_found) {
                    last_drive_mode = lkas.drive_mode;
                }
                last_base_speed = base_speed;
                
                // 4. (미래) 상태 전이 조건 검사...
                break;
            }
            // (향후 다른 상태 추가...)
        }
        // ==========================================================


        // (C) 전송 (기존과 동일)
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - lastTxTime).count() >= TX_PERIOD_MS) {
            string payload = build_motor_cmd(last_drive_mode, last_base_speed);
            tx.sendMotor(payload);
            lastTxTime = now;
            
            // ★ 로그는 기존 포맷 유지
            cout << "[RUN] LKAS:" << last_drive_mode << " ACC:" << last_base_speed 
                 << " Dist:" << last_good_tof_mm << "mm\r" << flush;
        }

        // (D) 시각화 (기존 코드 로직과 100% 동일)
        if (!headless) {
            // 'vis'는 여전히 비어있지만, 기존 코드와 동일하게 visualize 호출
            lkas_module.visualize(vis, lkas);
            imshow("view", vis);
            if (waitKey(1) == 27) break;
        }
    }

    tx.sendMotor("0;0;1;1");
    cout << "\n[SYS] Stopped.\n";
    return 0;
}