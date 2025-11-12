from ultralytics import YOLO

# 1. 1단계에서 테스트한 'best.pt' 모델 경로
MODEL_PATH = 'runs/detect/train5/weights/best.pt'

# 모델 로드
model = YOLO(MODEL_PATH)

# 2. ONNX 포맷으로 변환 (imgsz는 학습 때와 동일하게)
model.export(format='onnx', imgsz=320)

print(f"--- ONNX 변환 완료! ---")
print(f"{MODEL_PATH}와 같은 폴더에 'best.onnx' 파일이 생성되었습니다.")