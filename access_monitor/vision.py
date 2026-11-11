"""웹캠 읽기, 모션 필터, 사람 검출을 담당하는 모듈."""

import logging
import sys
from collections import namedtuple
from pathlib import Path

import cv2

import config

log = logging.getLogger(__name__)

# 사람 한 명의 검출 결과. x, y는 사각형 왼쪽 위 모서리, w, h는 너비와 높이다.
Detection = namedtuple("Detection", "x y w h")


def center_of(detection):
    """사각형의 중심 좌표를 돌려준다."""
    return detection.x + detection.w // 2, detection.y + detection.h // 2


def zone_of(center_x, frame_width):
    """화면을 세 칸으로 나눠 사람이 어느 쪽에 있는지 돌려준다."""
    if center_x < frame_width / 3:
        return "left"
    if center_x < frame_width * 2 / 3:
        return "center"
    return "right"


class Camera:
    """필요할 때만 켜고 끄는 웹캠. video_path를 주면 영상 파일로 시험할 수 있다."""

    def __init__(self, index=config.CAMERA_INDEX, video_path=None):
        self._source = video_path if video_path else index
        self._is_file = bool(video_path)
        self._capture = None

    def open(self):
        """웹캠을 켠다. 성공하면 True."""
        # 리눅스에서는 V4L2를 직접 지정해야 C270 설정이 잘 적용된다.
        if not self._is_file and sys.platform.startswith("linux"):
            self._capture = cv2.VideoCapture(self._source, cv2.CAP_V4L2)
        else:
            self._capture = cv2.VideoCapture(self._source)

        if not self._capture.isOpened():
            log.error("웹캠을 열 수 없습니다: %s", self._source)
            self.close()
            return False

        if not self._is_file:
            self._capture.set(cv2.CAP_PROP_FRAME_WIDTH, config.FRAME_WIDTH)
            self._capture.set(cv2.CAP_PROP_FRAME_HEIGHT, config.FRAME_HEIGHT)
            # 버퍼를 1장으로 줄인다. 안 그러면 추적할 때 과거 화면을 보고 움직이게 된다.
            self._capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            # 켠 직후에는 자동 노출로 화면이 출렁이니 몇 장을 버린다.
            for _ in range(config.CAMERA_WARMUP_FRAMES):
                self._capture.read()
        return True

    def read(self):
        """프레임 한 장을 돌려준다. 실패하면 None."""
        if self._capture is None:
            return None
        ok, frame = self._capture.read()
        return frame if ok else None

    def close(self):
        if self._capture is not None:
            self._capture.release()
            self._capture = None


class MotionFilter:
    """MOG2 배경차분으로 화면에 움직임이 있는지 판단한다.

    MOG2는 프레임을 계속 보면서 "늘 그대로인 부분(배경)"을 배우고,
    배경과 다른 부분을 움직임으로 표시한다.

    웹캠을 켤 때마다 reset()으로 새로 배운다. 이때 배경에 사람이 살짝 섞여 들어가도,
    입구를 지나가는 사람은 계속 위치가 바뀌므로 움직임으로 잡힌다.
    """

    def __init__(self, learn_frames=config.MOTION_LEARN_FRAMES, min_ratio=config.MOTION_MIN_RATIO):
        self._learn_frames = learn_frames
        self._min_ratio = min_ratio
        self.reset()

    def reset(self):
        self._subtractor = cv2.createBackgroundSubtractorMOG2(history=100, detectShadows=True)
        self._seen = 0

    def has_motion(self, frame):
        """이번 프레임에 움직임이 있으면 True."""
        # 작게 줄이고 흐리게 만들면 계산이 빨라지고 자잘한 노이즈도 줄어든다.
        small = cv2.GaussianBlur(cv2.resize(frame, (320, 240)), (5, 5), 0)
        mask = self._subtractor.apply(small)
        self._seen += 1
        if self._seen <= self._learn_frames:
            return False  # 아직 배경을 배우는 중이다.

        # MOG2는 그림자를 회색(127), 움직임을 흰색(255)으로 표시한다. 흰색만 남긴다.
        _, mask = cv2.threshold(mask, 200, 255, cv2.THRESH_BINARY)
        ratio = cv2.countNonZero(mask) / mask.size
        return ratio >= self._min_ratio


class MobileNetPersonDetector:
    """MobileNet-SSD로 사람을 찾는다. Haar보다 느리지만 오검출이 적다."""

    _PERSON_CLASS_ID = 15  # 이 모델이 학습한 20종류 중 사람의 번호
    _INPUT_SIZE = (300, 300)

    def __init__(self, prototxt, weights, min_confidence=config.PERSON_MIN_CONFIDENCE):
        self._net = cv2.dnn.readNetFromCaffe(str(prototxt), str(weights))
        self._min_confidence = min_confidence

    def detect(self, frame):
        height, width = frame.shape[:2]
        # 모델이 원하는 크기와 값 범위로 바꾼다. 이 숫자들은 모델이 정해 둔 값이다.
        blob = cv2.dnn.blobFromImage(cv2.resize(frame, self._INPUT_SIZE), 0.007843, self._INPUT_SIZE, 127.5)
        self._net.setInput(blob)
        output = self._net.forward()

        people = []
        for i in range(output.shape[2]):
            confidence = float(output[0, 0, i, 2])
            class_id = int(output[0, 0, i, 1])
            if class_id != self._PERSON_CLASS_ID or confidence < self._min_confidence:
                continue
            # 좌표가 0~1 비율로 나오므로 실제 픽셀로 바꾸고 화면 밖으로 나간 부분은 자른다.
            x1 = max(0, int(output[0, 0, i, 3] * width))
            y1 = max(0, int(output[0, 0, i, 4] * height))
            x2 = min(width, int(output[0, 0, i, 5] * width))
            y2 = min(height, int(output[0, 0, i, 6] * height))
            if x2 > x1 and y2 > y1:
                people.append(Detection(x1, y1, x2 - x1, y2 - y1))
        return _largest(people)


class HaarPersonDetector:
    """OpenCV에 포함된 Haar cascade로 사람의 전신을 찾는다. 모델 파일을 받을 필요가 없다."""

    def __init__(self):
        self._cascade = cv2.CascadeClassifier(str(_find_haar_file()))
        if self._cascade.empty():
            raise RuntimeError("Haar cascade 파일을 읽을 수 없습니다")

    def detect(self, frame):
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        found = self._cascade.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=4, minSize=(60, 120))
        return _largest([Detection(*map(int, box)) for box in found])


def _find_haar_file():
    name = "haarcascade_fullbody.xml"
    # pip로 설치하면 cv2.data에 경로가 있고, apt로 설치하면 아래 폴더에 있다.
    if hasattr(cv2, "data"):
        return Path(cv2.data.haarcascades) / name
    return Path("/usr/share/opencv4/haarcascades") / name


def _largest(people):
    """여러 명이 잡히면 가장 크게 보이는(가장 가까운) 사람을 고른다. 없으면 None."""
    return max(people, key=lambda p: p.w * p.h, default=None)


def create_person_detector():
    """모델 파일이 있으면 MobileNet-SSD를, 없으면 Haar cascade를 사용한다."""
    if config.MOBILENET_PROTOTXT.exists() and config.MOBILENET_WEIGHTS.exists():
        log.info("사람 검출: MobileNet-SSD")
        return MobileNetPersonDetector(config.MOBILENET_PROTOTXT, config.MOBILENET_WEIGHTS)
    log.warning("MobileNet-SSD 모델 파일이 없어 Haar cascade를 사용합니다 (%s)", config.MODEL_DIR)
    return HaarPersonDetector()


def draw_detection(frame, detection):
    """사람 위치에 초록색 사각형을 그린 복사본을 돌려준다. (스냅샷용)"""
    marked = frame.copy()
    top_left = (detection.x, detection.y)
    bottom_right = (detection.x + detection.w, detection.y + detection.h)
    cv2.rectangle(marked, top_left, bottom_right, (0, 255, 0), 2)
    return marked
