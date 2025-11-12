import cv2
import os

# ------------------- 1. 설정 -------------------
# (필수) 변환할 동영상 파일 이름
VIDEO_SOURCE = "Vid6.mp4" 

# (필수) 이미지 파일을 저장할 폴더 이름
OUTPUT_FOLDER = "collected_images"

# (선택) 몇 프레임마다 1장씩 저장할지 (예: 30 = 1초에 1장)
FRAME_SKIP = 5
# -----------------------------------------------

# 저장할 폴더가 없으면 새로 만들기
if not os.path.exists(OUTPUT_FOLDER):
    os.makedirs(OUTPUT_FOLDER)
    print(f"폴더 생성: {OUTPUT_FOLDER}")

# 동영상 파일 열기
cap = cv2.VideoCapture(VIDEO_SOURCE)

if not cap.isOpened():
    print(f"오류: '{VIDEO_SOURCE}' 파일을 열 수 없습니다.")
    exit()

print("프레임 추출을 시작합니다. (Ctrl+C로 중단)")

count = 0
saved_count = 0

while True:
    # 동영상에서 프레임 1장 읽기
    ret, frame = cap.read()

    # 동영상이 끝났거나, 프레임을 못 읽으면 종료
    if not ret:
        break

    # FRAME_SKIP (예: 30) 프레임마다 1번씩만 실행
    if count % FRAME_SKIP == 0:
        # 저장할 파일 이름 (예: collected_images/frame_0000.jpg)
        filename = f"{OUTPUT_FOLDER}/frame_{saved_count:04d}.jpg"
        
        # 이미지 파일로 저장
        cv2.imwrite(filename, frame)
        
        print(f"저장됨: {filename}")
        saved_count += 1
    
    count += 1

# 자원 해제
cap.release()
print("\n--- 작업 완료 ---")
print(f"총 {saved_count}개의 이미지가 '{OUTPUT_FOLDER}' 폴더에 저장되었습니다.")