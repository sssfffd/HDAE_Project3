#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <vector>

// --- [CAN 추가] ---
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
// -----------------


// --- 1. 설정 (⭐ 중요) ---
const float INPUT_WIDTH = 320.0;
const float INPUT_HEIGHT = 320.0;
const float CONFIDENCE_THRESHOLD = 0.5; // (임계값은 0.5로 다시 낮춤, 테스트용)
const float SCORE_THRESHOLD = 0.5;
const float NMS_THRESHOLD = 0.45;

std::vector<std::string> class_names = {"Box", "Sign_A", "Sign_B"}; 

// --- [CAN 추가] ---
const uint32_t CAN_ID_OBSTACLE = 0x300;
const uint8_t DATA_ID_BOX = 0x01;
const uint8_t DATA_ID_SIGN_A = 0x02;
const uint8_t DATA_ID_SIGN_B = 0x03;
// -----------------


// (draw_label, format_yolo 함수는 이전과 동일)
void draw_label(cv::Mat& input_image, std::string label, int left, int top) {
    int baseLine;
    cv::Size label_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
    top = cv::max(top, label_size.height);
    cv::Point tlc = cv::Point(left, top);
    cv::Point brc = cv::Point(left + label_size.width, top + label_size.height + baseLine);
    cv::rectangle(input_image, tlc, brc, cv::Scalar(0, 0, 255), cv::FILLED);
    cv::putText(input_image, label, cv::Point(left, top + label_size.height), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
}

cv::Mat format_yolo(const cv::Mat &source) {
    int col = source.cols;
    int row = source.rows;
    int _max = MAX(col, row);
    cv::Mat result = cv::Mat::zeros(_max, _max, CV_8UC3);
    source.copyTo(result(cv::Rect(0, 0, col, row)));
    cv::resize(result, result, cv::Size(INPUT_WIDTH, INPUT_HEIGHT));
    return result;
}

// (setup_can_socket 함수는 이전과 동일)
int setup_can_socket() {
    int s; 
    struct sockaddr_can addr;
    struct ifreq ifr;
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        std::cerr << "오류: CAN 소켓 생성 실패" << std::endl;
        return -1;
    }
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "오류: 'can0' 인터페이스를 찾을 수 없습니다." << std::endl;
        close(s);
        return -1;
    }
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        std::cerr << "오류: CAN 소켓 바인딩 실패" << std::endl;
        close(s);
        return -1;
    }
    std::cout << "'can0' 소켓 준비 완료!" << std::endl;
    return s;
}

// --- [CAN 수정됨] ---
// CAN 프레임 전송 함수 (클래스 ID 1바이트 + 너비 2바이트 = 총 3바이트 전송)
void send_can_frame(int s, uint32_t can_id, uint8_t class_data, int width_data) {
    struct can_frame frame;
    frame.can_id = can_id;
    frame.can_dlc = 3; // Data Length (3바이트)
    
    // 1. 클래스 ID (1바이트)
    frame.data[0] = class_data;
    
    // 2. 너비 값 (int를 2바이트 uint16_t로 변환)
    uint16_t width = (uint16_t)width_data;
    frame.data[1] = (width >> 8) & 0xFF; // 상위 바이트 (High Byte)
    frame.data[2] = width & 0xFF;        // 하위 바이트 (Low Byte)

    if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        std::cerr << "오류: CAN 프레임 전송 실패" << std::endl;
    }
}
// -----------------


int main() {
    // (카메라, 모델 로드, CAN 소켓 설정은 동일)
    std::string pipeline = "libcamerasrc ! video/x-raw,width=640,height=480 ! videoconvert ! appsink";
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened()) { std::cerr << "오류: GStreamer" << std::endl; return -1; }

    std::string model_path = "./best.onnx"; 
    cv::dnn::Net net;
    try { net = cv::dnn::readNetFromONNX(model_path); }
    catch (cv::Exception& e) { std::cerr << "오류: ONNX" << e.what() << std::endl; return -1; }
    std::cout << "'best.onnx' 모델 로드 성공!" << std::endl;

    int can_socket = setup_can_socket();
    if (can_socket < 0) { std::cerr << "CAN 통신을 시작할 수 없습니다." << std::endl; return -1; }

    cv::Mat frame, blob;
    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    cv::TickMeter tm; // FPS용

    while (true) {
        tm.start(); 
        cap.read(frame); 
        if (frame.empty()) break;

        // (전처리, 추론, 후처리(인덱싱)는 동일)
        cv::Mat input_image = format_yolo(frame);
        cv::dnn::blobFromImage(input_image, blob, 1./255., cv::Size(INPUT_WIDTH, INPUT_HEIGHT), cv::Scalar(), true, false);
        net.setInput(blob);
        std::vector<cv::Mat> outputs;
        net.forward(outputs, net.getUnconnectedOutLayersNames());
        const int num_detections = outputs[0].size[2]; 
        float *data = (float *)outputs[0].data;
        float *cx_data = data;
        float *cy_data = data + num_detections;
        float *w_data = data + 2 * num_detections;
        float *h_data = data + 3 * num_detections;
        //float x_factor = frame.cols / INPUT_WIDTH;
        //float y_factor = frame.rows / INPUT_HEIGHT;
	//
	float max_dim = (float)MAX(frame.cols, frame.rows); // 640
        float x_factor = max_dim / INPUT_WIDTH; // 640 / 320 = 2.0
        float y_factor = max_dim / INPUT_HEIGHT; // 640 / 320 = 2.0

        class_ids.clear();
        confidences.clear();
        boxes.clear();

        for (int i = 0; i < num_detections; ++i) {
            float max_conf = 0.0;
            int class_id = -1;
            for (int j = 0; j < class_names.size(); ++j) {
                float score = data[(4 + j) * num_detections + i];
                if (score > max_conf) {
                    max_conf = score;
                    class_id = j;
                }
            }
            if (max_conf > CONFIDENCE_THRESHOLD) {
                confidences.push_back(max_conf);
                class_ids.push_back(class_id); 
                float cx = cx_data[i];
                float cy = cy_data[i];
                float w =  w_data[i];
                float h =  h_data[i];
                int left = (int)((cx - 0.5 * w) * x_factor);
                int top = (int)((cy - 0.5 * h) * y_factor);
                int width = (int)(w * x_factor);
                int height = (int)(h * y_factor); 
                boxes.push_back(cv::Rect(left, top, width, height));
            }
        }
        std::vector<int> indices;
        cv::dnn::NMSBoxes(boxes, confidences, SCORE_THRESHOLD, NMS_THRESHOLD, indices);

        // --- 8. 결과 그리기 및 CAN 전송 (⭐ 수정됨) ---
        for (int idx : indices) {
            int class_id = class_ids[idx];
            if (class_id >= class_names.size()) continue; 

            std::string class_name = class_names[class_id];
            
            uint8_t can_data_id = 0; // 보낼 CAN 데이터 ID (클래스)

            // 1. 우리가 찾는 클래스인지 확인
            if (class_name == "Box") {
                can_data_id = DATA_ID_BOX;
            } else if (class_name == "Sign_A") {
                can_data_id = DATA_ID_SIGN_A;
            } else if (class_name == "Sign_B") {
                can_data_id = DATA_ID_SIGN_B;
            }

            // 2. 찾는 클래스가 맞다면 (can_data_id가 0이 아님)
            if (can_data_id != 0) {
                cv::Rect box = boxes[idx];
                
                // 2-1. 화면에 그리기
                std::string label = cv::format("%.2f", confidences[idx]); 
                label = class_name + ": " + label;
                cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
                draw_label(frame, label, box.x, box.y);

                // 2-2. 터미널에 너비(width) 출력 (요청 사항 1)
                std::cout << "탐지됨: " << class_name 
                          << ", X간격(너비): " << box.width << " 픽셀" << std::endl;

                // 2-3. CAN으로 클래스 ID와 너비(width) 전송 (요청 사항 2)
                send_can_frame(can_socket, CAN_ID_OBSTACLE, can_data_id, box.width);
            }
        }

        // (FPS 표시 코드)
        tm.stop();
        double fps = tm.getFPS();
        std::string fps_text = cv::format("FPS: %.2f", fps);
        cv::putText(frame, fps_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);

        cv::imshow("YOLOv8 C++ (GStreamer + CAN)", frame);

        if (cv::waitKey(1) == 'q') {
            break; 
        }
    }

    close(can_socket); 
    cap.release();
    cv::destroyAllWindows();
    return 0;
}
