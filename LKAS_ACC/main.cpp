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
static const char* PI_IP = "192.168.137.117";
static const char* CTRL_IP = "192.168.2.30";
static const int CTRL_PORT = 30509;
static const int CAM_INDEX = 0;
static const int WIDTH = 320, HEIGHT = 240;
static const int TX_PERIOD_MS = 100;
static const int TURN_DELTA = 16;

// ===== 회피 및 회전 시간 =====
static const auto AVOID_TIME1_TURN = chrono::milliseconds(700); 
static const auto AVOID_TIME2_STRAIGHT = chrono::milliseconds(550); 
static const auto TURN_RIGHT_TIME = chrono::milliseconds(2500); 

// ===== ToF 기반 장애물 감지 설정 =====
static const double OBSTACLE_THRESHOLD_M = 0.5; 
static const auto OBSTACLE_DETECT_DURATION = chrono::seconds(3); 

// ===== 회피 기동 속도 =====
static const int AVOID_BASE_SPEED = 90;


static atomic<bool> g_running{true};
static void on_sigint(int){ g_running.store(false); cerr << "\n[SYS] SIGINT\n"; }

static string build_motor_cmd(int drive_mode, int base_speed) {
    // (기존과 동일)
    int Rspd=0, Lspd=0;
    int Rdir=1, Ldir=1;
    if (drive_mode == 0) { Rspd = base_speed; Lspd = base_speed; }
    else if (drive_mode < 0) { Rspd = base_speed + TURN_DELTA; Lspd = 0; } // -1: 좌회전
    else { Rspd = 0; Lspd = base_speed + TURN_DELTA; } // 1: 우회전
    Rspd = max(0, min(100, Rspd)); Lspd = max(0, min(100, Lspd));
    return to_string(Rspd)+";"+to_string(Lspd)+";"+to_string(Rdir)+";"+to_string(Ldir);
}

// ==========================================================
// ===== 1. 상태(State) 정의 (기존과 동일) =====
// ==========================================================
enum VehicleState {
    STATE_LANE_FOLLOWING,      
    STATE_AVOID_1_TURN_LEFT,   
    STATE_AVOID_2_STRAIGHT,    
    STATE_AVOID_3_TURN_RIGHT,  
    STATE_WAITING_FOR_TURN_OPENING, 
    STATE_HARD_RIGHT_TURN      
};
const char* STATE_NAMES[] = {
    "LANE_FOLLOWING", "AVOID_1_TURN_LEFT", "AVOID_2_STRAIGHT",
    "AVOID_3_TURN_RIGHT", "WAITING_FOR_TURN_OPENING", "HARD_RIGHT_TURN"
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
    cap.open(CAM_INDEX, CAP_V4L2);
    if (!cap.isOpened()) { cerr << "[ERR] Camera open FAILED!\n"; return 1; }
    cout << "[INFO] Camera opened successfully.\n";
    if (!tof_reader.open("can0")) { cerr << "[ERR] CAN fail\n"; return 1; }
    cout << "[INFO] CAN Ready\n";
    lkas_module.init_gui(headless);
    // -------------------------------

    Mat frame, vis;
    auto lastTxTime = chrono::steady_clock::now();
    int last_drive_mode = 0;
    int last_base_speed = 0;
    int last_good_tof_mm = 5000;
    int fail_count = 0;
    
    // ===== 2. 초기 상태 및 타이머/래치 지정 =====
    VehicleState currentState = STATE_LANE_FOLLOWING;
    LKASResult lkas; 
    auto m_state_timer = chrono::steady_clock::now(); 
    bool sign_turn_latch = false; 
    
    // ToF 장애물 감지용 변수
    bool m_is_obstacle_close = false; 
    auto m_obstacle_timer = chrono::steady_clock::now(); 

    // ★ 신규: 장애물 회피 1회 제한 플래그
    bool has_avoided_obstacle = false;

    cout << "[INFO] Starting Main Loop...\n";

    while (g_running.load()) {
        // (A) 센서 읽기 (기존과 동일)
        if (!cap.read(frame) || frame.empty()) {
            fail_count++;
            if (fail_count % 10 == 0) {
                cerr << "[WARN] Camera frame EMPTY! (Retrying... " << fail_count << ")\r" << flush;
            }
            this_thread::sleep_for(chrono::milliseconds(10));
            continue; 
        }
        if (fail_count > 0) { cerr << "\n[INFO] Camera recovered!\n"; fail_count = 0; }

        CanData can_data = tof_reader.readMessages();
        if (can_data.distance_mm >= 0) {
            last_good_tof_mm = can_data.distance_mm;
        }
        if (can_data.sign_turn_right) { 
            sign_turn_latch = true;
        }
        double current_distance_m = last_good_tof_mm / 1000.0;
        
        auto now = chrono::steady_clock::now(); 
        auto elapsed = now - m_state_timer;     

        // (ToF 장애물 감지 로직 - 기존과 동일)
        if (current_distance_m < OBSTACLE_THRESHOLD_M && current_distance_m > 0.0) {
            if (!m_is_obstacle_close) {
                m_is_obstacle_close = true;
                m_obstacle_timer = now; 
            }
        } else {
            m_is_obstacle_close = false;
        }


        // ==========================================================
        // ===== 3. 상태 머신(State Machine) =====
        // ==========================================================
        switch (currentState) {
            
            case STATE_LANE_FOLLOWING: {
                last_base_speed = acc_module.computeBaseSpeed(current_distance_m);
                lkas = lkas_module.processFrame(frame);
                if (lkas.line_found) {
                    last_drive_mode = lkas.drive_mode;
                }
                
                // ★ 상태 전이 1: 장애물 (1회 제한 조건 추가)
                auto obstacle_elapsed_time = now - m_obstacle_timer;
                if (m_is_obstacle_close && obstacle_elapsed_time >= OBSTACLE_DETECT_DURATION && !has_avoided_obstacle) {
                    cout << "\n[STATE] Obstacle (ToF) DETECTED! (First Time) -> AVOID_1_TURN_LEFT\n";
                    currentState = STATE_AVOID_1_TURN_LEFT;
                    m_state_timer = now;     
                    m_is_obstacle_close = false; // 1회성 트리거
                    has_avoided_obstacle = true; // ★ 1회 제한 플래그 사용
                }
                // 상태 전이 2: 우회전 표지판 (기존과 동일)
                else if (sign_turn_latch) {
                    cout << "\n[STATE] Turn Sign DETECTED! -> WAITING_FOR_TURN_OPENING\n";
                    currentState = STATE_WAITING_FOR_TURN_OPENING;
                    sign_turn_latch = false; 
                }
                break;
            }
            
            // --- 장애물 회피 상태 (★ 버그 수정) ---
            case STATE_AVOID_1_TURN_LEFT: {
                last_base_speed = AVOID_BASE_SPEED;
                last_drive_mode = 1; 
                if (elapsed >= AVOID_TIME1_TURN) {
                    cout << "\n[STATE] Avoid: Left Turn Done -> AVOID_2_STRAIGHT\n";
                    currentState = STATE_AVOID_2_STRAIGHT;
                    m_state_timer = now; 
                }
                break;
            }
            case STATE_AVOID_2_STRAIGHT: {
                last_base_speed = AVOID_BASE_SPEED; 
                last_drive_mode = 0; // 직진
                if (elapsed >= AVOID_TIME2_STRAIGHT) {
                    cout << "\n[STATE] Avoid: Straight Done -> AVOID_3_TURN_RIGHT\n";
                    currentState = STATE_AVOID_3_TURN_RIGHT;
                    m_state_timer = now; 
                }
                break;
            }
            case STATE_AVOID_3_TURN_RIGHT: {
                last_base_speed = AVOID_BASE_SPEED; 
                last_drive_mode = -1; 
                if (elapsed >= AVOID_TIME1_TURN) {
                    cout << "\n[STATE] Avoid: Right Turn Done -> LANE_FOLLOWING\n";
                    currentState = STATE_LANE_FOLLOWING;
                }
                break;
            }

            // --- 우회전 상태 ---
            case STATE_WAITING_FOR_TURN_OPENING: {
                last_base_speed = acc_module.computeBaseSpeed(current_distance_m);
                lkas = lkas_module.processFrame(frame);
                if (lkas.line_found) {
                    last_drive_mode = lkas.drive_mode;
                }

                // ★ 우선순위 1: 장애물 감지 (1회 제한 조건 추가)
                auto obstacle_elapsed_time = now - m_obstacle_timer;
                 if (m_is_obstacle_close && obstacle_elapsed_time >= OBSTACLE_DETECT_DURATION && !has_avoided_obstacle) {
                     cout << "\n[STATE] Obstacle (while waiting)! -> AVOID_1_TURN_LEFT\n";
                     currentState = STATE_AVOID_1_TURN_LEFT;
                     m_state_timer = now;
                     m_is_obstacle_close = false; 
                     sign_turn_latch = false; // (우회전 대기 상태는 취소됨)
                     has_avoided_obstacle = true; // ★ 1회 제한 플래그 사용
                }
                // 우선순위 2: 차선이 사라짐 (기존과 동일)
                else if (!lkas.line_found) {
                    cout << "\n[STATE] Turn Opening DETECTED! -> HARD_RIGHT_TURN\n";
                    currentState = STATE_HARD_RIGHT_TURN;
                    m_state_timer = now; 
                }
                break;
            }
            
            case STATE_HARD_RIGHT_TURN: {
                last_base_speed = AVOID_BASE_SPEED; 
                last_drive_mode = 1; // 우회전
                if (elapsed >= TURN_RIGHT_TIME) {
                    cout << "\n[STATE] Hard Right Turn Done -> LANE_FOLLOWING\n";
                    currentState = STATE_LANE_FOLLOWING;
                }
                break;
            }
        }
        // ==========================================================


        // (C) 전송 (기존과 동일)
        auto tx_now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(tx_now - lastTxTime).count() >= TX_PERIOD_MS) {
            string payload = build_motor_cmd(last_drive_mode, last_base_speed);
            tx.sendMotor(payload);
            lastTxTime = tx_now;
            
            cout << "[RUN] State: " << STATE_NAMES[currentState] 
                 << " Mode:" << last_drive_mode << " ACC:" << last_base_speed 
                 << " Dist:" << last_good_tof_mm << "mm\r" << flush;
        }

        // (D) 시각화 (기존 코드 로직과 100% 동일)
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