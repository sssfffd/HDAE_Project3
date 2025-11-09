// acc_test.cpp : USB Webcam 전용(V4L2) + OpenCV + SocketCAN + GUI 튠
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <iomanip>

// ===== SocketCAN =====
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ===================== 사용자 조정 영역 =====================
#define CAN_IFACE        "can0"
#define CAN_ID_MOTOR     0x151
#define CAN_DLC_MOTOR    4       // [L,R, dirFlags, 0]
#define DUTY_MIN_TURN    80
#define DUTY_MIN_MOVE    30
#define DUTY_MAX         250

static float KP_DEFAULT     = 0.9f;
static int   BASE_DUTY_FWD  = 100;
static int   PIVOT_BIAS     = 10;
// ===========================================================

struct Options {
    bool headless=false;
    std::string device="/dev/video0";
    int width=640, height=480, fps=30;
} g_opt;

static bool g_running=true;
void sigintHandler(int){ g_running=false; }

// HSV/Trackbar
struct HSVRange { int hmin=0,smin=0,vmin=200, hmax=179,smax=60,vmax=255; } g_hsv;
static const char* WIN_TRACK="LKAS Trackbars";
static const char* WIN_VIEW ="View";
static const char* WIN_MASK ="Mask";

int TB_KP        = (int)(KP_DEFAULT*100); // 0..300
int TB_BASE      = BASE_DUTY_FWD;         // 0..250
int TB_MIN_TURN  = DUTY_MIN_TURN;         // 0..250

void on_trackbar(int, void*){}
static inline int clampi(int v,int lo,int hi){ return std::max(lo,std::min(hi,v)); }

static inline cv::Rect clampROI(const cv::Rect& r, const cv::Size& sz){
    cv::Rect B(0,0,sz.width,sz.height);
    cv::Rect out = r & B;
    if(out.width<=0 || out.height<=0) return cv::Rect();
    return out;
}

void createTrackbars(){
    cv::namedWindow(WIN_TRACK, cv::WINDOW_NORMAL);
    cv::createTrackbar("H min", WIN_TRACK, &g_hsv.hmin, 179, on_trackbar);
    cv::createTrackbar("H max", WIN_TRACK, &g_hsv.hmax, 179, on_trackbar);
    cv::createTrackbar("S min", WIN_TRACK, &g_hsv.smin, 255, on_trackbar);
    cv::createTrackbar("S max", WIN_TRACK, &g_hsv.smax, 255, on_trackbar);
    cv::createTrackbar("V min", WIN_TRACK, &g_hsv.vmin, 255, on_trackbar);
    cv::createTrackbar("V max", WIN_TRACK, &g_hsv.vmax, 255, on_trackbar);
    cv::createTrackbar("Kp x100",  WIN_TRACK, &TB_KP, 300, on_trackbar);
    cv::createTrackbar("BaseDuty", WIN_TRACK, &TB_BASE, 250, on_trackbar);
    cv::createTrackbar("MinTurn",  WIN_TRACK, &TB_MIN_TURN, 250, on_trackbar);
    cv::resizeWindow(WIN_TRACK, 460, 280);
}

// ===== SocketCAN =====
struct CanCtx{ int fd=-1; bool ok() const { return fd>=0; } };
CanCtx can_open(const char* ifname){
    CanCtx c;
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(s<0){ perror("socket"); return c; }
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if(ioctl(s, SIOCGIFINDEX, &ifr)<0){ perror("ioctl"); close(s); return c; }
    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))<0){ perror("bind"); close(s); return c; }
    c.fd=s; return c;
}
bool can_send_motor(const CanCtx& c, uint8_t L, uint8_t R, uint8_t dirFlags){
    if(!c.ok()) return false;
    struct can_frame fr{};
    fr.can_id = CAN_ID_MOTOR;
    fr.can_dlc = CAN_DLC_MOTOR;
    fr.data[0]=L; fr.data[1]=R; fr.data[2]=dirFlags; fr.data[3]=0;
    int n=write(c.fd,&fr,sizeof(fr));
    return n==(int)sizeof(fr);
}

// ===== 간단 LKAS =====
struct LKASOut{ float err=0.f; int cx=-1; bool valid=false; };

LKASOut center_from_histogram(const cv::Mat& bin){
    LKASOut o; if(bin.empty()||bin.type()!=CV_8UC1) return o;
    cv::Mat proj; cv::reduce(bin, proj, 0, cv::REDUCE_SUM, CV_32SC1);
    double mx; cv::Point p; cv::minMaxLoc(proj,nullptr,&mx,nullptr,&p);
    if(mx<=0) return o;
    o.cx=p.x;
    o.err = ((float)o.cx - (bin.cols*0.5f)) / (bin.cols*0.5f); // -1..+1
    o.valid=true; return o;
}

struct MotorCmd{ uint8_t L=0,R=0; uint8_t dir=0; }; // dir bit0=L(1=rev), bit1=R(1=rev)

MotorCmd compute_motor_from_err(float err, int base_duty, int min_turn){
    MotorCmd m{};
    float Kp = TB_KP/100.0f;
    float steer = Kp*err;
    float a = std::clamp(std::abs(steer), 0.0f, 1.0f);

    int d_straight = clampi(base_duty, DUTY_MIN_MOVE, DUTY_MAX);
    int d_turn = clampi((int)(a*170)+min_turn, min_turn, DUTY_MAX);

    if(steer > 0.05f){
        m.R = clampi(d_straight + PIVOT_BIAS, 0, DUTY_MAX);
        m.L = d_turn;
        m.dir = 0x01; // L reverse
    } else if(steer < -0.05f){
        m.L = clampi(d_straight + PIVOT_BIAS, 0, DUTY_MAX);
        m.R = d_turn;
        m.dir = 0x02; // R reverse
    } else {
        m.L = d_straight; m.R = d_straight; m.dir=0x00;
    }
    return m;
}

// ===== 웹캠 오픈(V4L2 전용, FOURCC 자동협상) =====
static bool set_fourcc(cv::VideoCapture& cap, char a,char b,char c,char d){
#if CV_VERSION_MAJOR>=4
    int fourcc = cv::VideoWriter::fourcc(a,b,c,d);
#else
    int fourcc = CV_FOURCC(a,b,c,d);
#endif
    return cap.set(cv::CAP_PROP_FOURCC, fourcc);
}

bool probe_frame(cv::VideoCapture& cap, cv::Mat& out){
    for(int i=0;i<10;i++){
        if(!cap.read(out) || out.empty()){ cv::waitKey(10); continue; }
        return true;
    }
    return false;
}

cv::VideoCapture open_webcam_v4l2(std::string& info){
    cv::VideoCapture cap;
    // 장치 열기
    cap.open(g_opt.device, cv::CAP_V4L2);
    if(!cap.isOpened()){ info="open fail"; return cap; }

    cap.set(cv::CAP_PROP_FRAME_WIDTH , g_opt.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, g_opt.height);
    cap.set(cv::CAP_PROP_FPS        , g_opt.fps);

    // 우선 MJPG 시도 → 실패 시 YUYV
    set_fourcc(cap,'M','J','P','G');
    cv::Mat test;
    if(probe_frame(cap, test)){
        info = "V4L2 MJPG";
        return cap;
    }

    // MJPG 안 나오면 YUYV
    set_fourcc(cap,'Y','U','Y','V');
    test.release();
    if(probe_frame(cap, test)){
        info = "V4L2 YUYV";
        return cap;
    }

    // FOURCC 설정 빼고 재시도(디폴트)
    cap.release();
    cap.open(g_opt.device, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH , g_opt.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, g_opt.height);
    cap.set(cv::CAP_PROP_FPS        , g_opt.fps);
    test.release();
    if(probe_frame(cap, test)){
        info = "V4L2 default";
        return cap;
    }

    info="V4L2 no-frames";
    return cap;
}

int main(int argc, char** argv){
    std::signal(SIGINT, sigintHandler);

    // CLI
    for(int i=1;i<argc;++i){
        std::string a(argv[i]);
        if(a=="--headless") g_opt.headless=true;
        else if(a=="--device" && i+1<argc) g_opt.device=argv[++i];
        else if(a=="--size" && i+1<argc){ int w=0,h=0; std::sscanf(argv[++i],"%dx%d",&w,&h); if(w>0&&h>0){g_opt.width=w; g_opt.height=h;} }
        else if(a=="--fps" && i+1<argc){ g_opt.fps=std::max(1, std::atoi(argv[++i])); }
    }

    std::cout<<"[INFO] SOME/IP Ready\n";
    std::cout<<"[INFO] Opening Camera(V4L2): "<<g_opt.device<<" ...\n";
    std::string backend;
    cv::VideoCapture cap = open_webcam_v4l2(backend);
    if(!cap.isOpened()){
        std::cerr<<"[ERR] Camera open failed: "<<backend<<"\n";
        return 2;
    }
    std::cout<<"[INFO] Camera opened successfully. ("<<backend<<")\n";

    std::cout<<"[INFO] CAN Ready\n";
    CanCtx can = can_open(CAN_IFACE);
    if(!can.ok()) std::cerr<<"[WARN] CAN open failed on "<<CAN_IFACE<<". RUN WITHOUT CAN.\n";

    if(!g_opt.headless){
        createTrackbars();
        cv::namedWindow(WIN_VIEW, cv::WINDOW_NORMAL);
        cv::namedWindow(WIN_MASK, cv::WINDOW_NORMAL);
        cv::resizeWindow(WIN_VIEW,  800, 600);
        cv::resizeWindow(WIN_MASK,  640, 240);
    }

    std::cout<<"[INFO] Starting Main Loop...\n";

    while(g_running){
        cv::Mat frame;
        if(!cap.read(frame) || frame.empty()){
            std::cerr<<"[ERR] empty frame; retry\n";
            cv::waitKey(10);
            continue;
        }

        // ROI: 하단 40%
        cv::Rect roi_full(0, (int)(frame.rows*0.60), frame.cols, (int)(frame.rows*0.40));
        cv::Rect roi = clampROI(roi_full, frame.size());
        cv::Mat view = (roi.area()>0)? frame(roi) : frame;

        // 트랙바 값 갱신
        if(!g_opt.headless){
            g_hsv.hmin = cv::getTrackbarPos("H min", WIN_TRACK);
            g_hsv.hmax = cv::getTrackbarPos("H max", WIN_TRACK);
            g_hsv.smin = cv::getTrackbarPos("S min", WIN_TRACK);
            g_hsv.smax = cv::getTrackbarPos("S max", WIN_TRACK);
            g_hsv.vmin = cv::getTrackbarPos("V min", WIN_TRACK);
            g_hsv.vmax = cv::getTrackbarPos("V max", WIN_TRACK);
        }

        // HSV Threshold
        cv::Mat hsv, mask;
        cv::cvtColor(view, hsv, cv::COLOR_BGR2HSV);
        cv::inRange(hsv,
            cv::Scalar(g_hsv.hmin, g_hsv.smin, g_hsv.vmin),
            cv::Scalar(g_hsv.hmax, g_hsv.smax, g_hsv.vmax),
            mask);

        // 약간의 노이즈 제거
        static cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, {5,5});
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, k);

        // 중심 추정
        LKASOut lk = center_from_histogram(mask);

        // 조향/모터
        if(lk.valid){
            MotorCmd cmd = compute_motor_from_err(lk.err, TB_BASE, TB_MIN_TURN);
            if(can.ok()){
                if(!can_send_motor(can, cmd.L, cmd.R, cmd.dir)){
                    std::cerr<<"[WARN] CAN send fail\n";
                }
            }
            if(!g_opt.headless){
                cv::line(view, {view.cols/2,0},{view.cols/2,view.rows-1},{0,255,0},1);
                cv::line(view, {lk.cx,0},{lk.cx,view.rows-1},{255,0,255},1);
                std::ostringstream os;
                os<<"err="<<std::fixed<<std::setprecision(2)<<lk.err
                  <<"  L="<<(int)cmd.L<<((cmd.dir&0x01)?"(R)":"(F)")
                  <<"  R="<<(int)cmd.R<<((cmd.dir&0x02)?"(R)":"(F)");
                cv::putText(view, os.str(), {10,25}, cv::FONT_HERSHEY_SIMPLEX, 0.7, {0,255,255}, 2);
            }
        } else {
            if(can.ok()) can_send_motor(can, 0, 0, 0x00);
            if(!g_opt.headless){
                cv::putText(view, "LINE LOST", {10,25}, cv::FONT_HERSHEY_SIMPLEX, 0.8, {0,0,255}, 2);
            }
        }

        // GUI
        if(!g_opt.headless){
            cv::imshow(WIN_VIEW, frame);
            cv::imshow(WIN_MASK, mask);
            int key = cv::waitKey(1);
            if(key==27) break;
        } else {
            cv::waitKey(1);
        }
    }
    return 0;
}

