from ultralytics import YOLO

# 1. 'yolov8n' 모델을 가져옵니다. (이 부분이 'n'을 선택하는 지점)
model = YOLO('yolov8n.pt')

# 2. 1단계에서 압축 푼 'data.yaml'로 학습(튜닝)을 시킵니다.
#    (만약 data.yaml이 pj3 바로 안이 아니라 하위 폴더에 있다면
#     'dataset/data.yaml'처럼 경로를 정확히 수정하세요)
model.train(data='Detection.v1i.yolov8/data.yaml', 
            epochs=100,  # 100번 반복 (빠른 테스트용으론 30~50)
            imgsz=320) # Pi 테스트와 동일하게 320

print("--- 튜닝(학습) 완료! 'best.pt' 파일 생성됨 ---")