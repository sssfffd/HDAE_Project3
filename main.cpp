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

// ===== 설정 =====
static const char* PI_IP   = "10.50.122.160";
static const char* CTRL_IP = "192.168.2.30"; // (※ 주의: 이 IP가 맞는지 다시 확인하세요!)
static const int CTRL_PORT = 30509;
static const int CAM_INDEX = 0;              // (혹시 안 되면 -1 로 바꿔보세요)
static const int WIDTH = 320, HEIGHT = 240;
static const int TX_PERIOD_MS = 100;
static const int TURN_DELTA = 16; 

static atomic<bool> g_running{true};
static void on_sigint(int){ g_running.store(false); cerr << "\n[SYS] SIGINT\n"; }

static string build_motor_cmd(int drive_mode, int base_speed) {
    static const int STRAIGHT_CAP = 45; // upper
    int Rspd=0, Lspd=0;
    int Rdir=1, Ldir=1;
    if (drive_mode == 0) {
	   int capped = std::min(base_speed, STRAIGHT_CAP); 
	    Rspd = base_speed; 
	    Lspd = base_speed;

    }
    else if (drive_mode < 0) { Rspd = base_speed + TURN_DELTA; Lspd = 0; }
    else { Rspd = 0; Lspd = base_speed + TURN_DELTA; }
    Rspd = max(0, min(100, Rspd)); Lspd = max(0, min(100, Lspd));
    return to_string(Rspd)+";"+to_string(Lspd)+";"+to_string(Rdir)+";"+to_string(Ldir);
}

int main(int argc, char** argv) {
    signal(SIGINT, on_sigint);
    bool headless = (argc > 1 && string(argv[1]) == "--headless");
    
    SomeipSender tx;
    VisionProcessor lkas_module;
    ACCController acc_module;
    VideoCapture cap;
    TofCanReader tof_reader;

    if (!tx.open_to(PI_IP, CTRL_IP, CTRL_PORT)) { cerr << "[ERR] SOME/IP fail\n"; return 1; }
    cout << "[INFO] SOME/IP Ready\n";

    // 카메라 열기 시도 (옵션 제거하고 기본으로 시도)
    cout << "[INFO] Opening Camera " << CAM_INDEX << "..." << endl;
    cap.open(CAM_INDEX, CAP_V4L2); // ★ V4L2 백엔드 강제 지정
    if (!cap.isOpened()) {
        cerr << "[ERR] Camera open FAILED! (Check cable or try CAM_INDEX = -1)\n";
        return 1;
    }
    // (해상도 설정은 일단 생략하고 기본값으로 테스트)
    cout << "[INFO] Camera opened successfully.\n";

    if (!tof_reader.open("can0")) { cerr << "[ERR] CAN fail\n"; return 1; }
    cout << "[INFO] CAN Ready\n";

    lkas_module.init_gui(headless);

    Mat frame, vis;
    auto lastTxTime = chrono::steady_clock::now();
    int last_drive_mode = 0;
    int last_base_speed = 0;
    int last_good_tof_mm = 5000;
    int fail_count = 0; // ★ 디버깅용 실패 카운터

    cout << "[INFO] Starting Main Loop...\n";

    while (g_running.load()) {
        // (A) 카메라 읽기
        if (!cap.read(frame) || frame.empty()) {
            fail_count++;
            if (fail_count % 10 == 0) { // 10번 연속 실패할 때마다 출력
                cerr << "[WARN] Camera frame EMPTY! (Retrying... " << fail_count << ")\r" << flush;
            }
            this_thread::sleep_for(chrono::milliseconds(10));
            continue; 
        }
        if (fail_count > 0) { cerr << "\n[INFO] Camera recovered!\n"; fail_count = 0; }

        // (B) 센서 & 제어
        int tof_mm = tof_reader.getDistanceMm();
        if (tof_mm >= 0) last_good_tof_mm = tof_mm;
        
        LKASResult lkas = lkas_module.processFrame(frame);
        int base_speed = acc_module.computeBaseSpeed(last_good_tof_mm / 1000.0);

        if (lkas.line_found) last_drive_mode = lkas.drive_mode;
        last_base_speed = base_speed;

        // (C) 전송
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - lastTxTime).count() >= TX_PERIOD_MS) {
            string payload = build_motor_cmd(last_drive_mode, last_base_speed);
            tx.sendMotor(payload);
            lastTxTime = now;
            // ★ 정상 작동 확인을 위한 로그
            cout << "[RUN] LKAS:" << last_drive_mode << " ACC:" << last_base_speed 
                 << " Dist:" << last_good_tof_mm << "mm\r" << flush;
        }

        // (D) 시각화
        if (!headless) {
            lkas_module.visualize(vis, lkas);
            imshow("view", vis);
            if (waitKey(1) == 27) break;
        }
    }

    tx.sendMotor("0;0;1;1");
    cout << "\n[SYS] Stopped.\n";
    return 0;
}
