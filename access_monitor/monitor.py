"""시스템 전체의 흐름을 제어하는 모듈.

    [대기] --PIR 감지--> [웹캠 켜기] --모션 있음--> [사람 검출]
      ^                                                |
      |                 사람 없음(시간 초과)            |
      +<-----------------------------------------------+
      |                                                | 사람 있음
      |                                                v
      +<---사람이 떠남--- [추적] <--- [버저, OLED, 스냅샷, DB 저장]

핵심: 대기 중에는 웹캠과 영상 처리를 모두 꺼 두고 PIR 신호만 기다린다.
그래서 평소에는 CPU를 거의 쓰지 않는다.
"""

import logging
import time
from datetime import datetime

import cv2

import config
import database
from vision import center_of, draw_detection, zone_of

log = logging.getLogger(__name__)

STATUS_NORMAL = "NORMAL"
STATUS_CHECKING = "CHECKING"
STATUS_ALERT = "ALERT"


class AccessMonitor:
    """부품들을 받아서 정해진 순서대로 움직이는 지휘자 역할의 클래스."""

    def __init__(self, pir, camera, motion_filter, detector, tracker, buzzer, display, stop_event):
        self._pir = pir
        self._camera = camera
        self._motion = motion_filter
        self._detector = detector
        self._tracker = tracker
        self._buzzer = buzzer
        self._display = display
        self._stop = stop_event

    def wait_for_warmup(self, seconds):
        """PIR 센서가 안정될 때까지 기다린 뒤, 그동안의 오감지를 지운다."""
        log.info("PIR 안정화 대기 중: %d초", seconds)
        self._display.show("WARMUP", database.count_on())
        self._stop.wait(seconds)
        self._pir.clear()

    def run(self):
        """메인 루프. 종료 요청이 올 때까지 대기와 이벤트 처리를 반복한다."""
        self._show(STATUS_NORMAL)
        log.info("대기 중: PIR 감지를 기다립니다")
        while not self._stop.is_set():
            # 짧게 나눠서 기다리는 이유는 Ctrl+C를 눌렀을 때 바로 끝내기 위해서다.
            if self._pir.wait_for_trigger(config.TRIGGER_POLL_SECONDS):
                log.info("PIR 감지: 웹캠을 켭니다")
                self.handle_trigger()
                log.info("대기 중: PIR 감지를 기다립니다")

    def handle_trigger(self):
        """PIR 감지 1회를 처리한다. 사람이 검출됐으면 True를 돌려준다."""
        self._show(STATUS_CHECKING)
        if not self._camera.open():
            self._show(STATUS_NORMAL)
            return False

        self._motion.reset()  # 지난번 배경은 조명이 달라졌을 수 있으니 새로 배운다.
        moved = False
        try:
            frame, person = self._look_for_person()
            if person is None:
                log.info("사람 없음: 대기로 돌아갑니다")
                return False
            self._raise_alert(frame, person)
            moved = self._follow_person()
            return True
        finally:
            # 중간에 에러가 나도 웹캠은 반드시 끄고, 서보도 원위치로 돌린다.
            self._camera.close()
            if moved:
                self._tracker.go_home()
            self._show(STATUS_NORMAL)

    def _look_for_person(self):
        """정해진 시간 동안 사람을 찾는다. 찾으면 (프레임, 검출 결과), 못 찾으면 (None, None)."""
        deadline = time.monotonic() + config.DETECT_WINDOW_SECONDS
        while time.monotonic() < deadline and not self._stop.is_set():
            frame = self._camera.read()
            if frame is None:
                log.warning("프레임을 읽을 수 없습니다")
                break

            # 1차 필터: 움직임이 있는 프레임만 무거운 사람 검출기로 넘긴다.
            if config.USE_MOTION_FILTER and not self._motion.has_motion(frame):
                continue

            person = self._detector.detect(frame)
            if person is not None:
                return frame, person
        return None, None

    def _raise_alert(self, frame, person):
        """사람이 검출됐을 때: 버저, 스냅샷, DB 저장, 화면 표시."""
        self._buzzer.alert()

        center_x, center_y = center_of(person)
        zone = zone_of(center_x, frame.shape[1])
        now = datetime.now()
        snapshot = self._save_snapshot(frame, person, now)
        event_id = database.add_event(zone, center_x, center_y, snapshot, when=now)

        log.info("사람 검출: 이벤트 #%d, 위치=%s, 스냅샷=%s", event_id, zone, snapshot)
        self._show(STATUS_ALERT)

    def _save_snapshot(self, frame, person, when):
        """스냅샷을 저장하고 파일 이름을 돌려준다. 실패해도 이벤트 기록은 남기도록 None을 돌려준다."""
        config.SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
        name = when.strftime("%Y%m%d_%H%M%S_%f") + ".jpg"
        if cv2.imwrite(str(config.SNAPSHOT_DIR / name), draw_detection(frame, person)):
            return name
        log.error("스냅샷을 저장할 수 없습니다: %s", name)
        return None

    def _follow_person(self):
        """사람이 떠날 때까지 따라간다. 서보가 한 번이라도 움직였으면 True.

        같은 사람이 있는 동안에는 여기 머물기 때문에 한 사람이 이벤트 하나로 기록된다.
        카메라가 움직이면 화면 전체가 달라져 MOG2가 쓸모없어지므로 사람 검출만 사용한다.
        """
        moved = False
        started = time.monotonic()
        last_seen = started
        while not self._stop.is_set():
            now = time.monotonic()
            if now - last_seen > config.TRACK_LOST_SECONDS:
                log.info("사람이 떠났습니다")
                break
            if now - started > config.MAX_EVENT_SECONDS:
                log.info("이벤트 최대 시간을 넘어 종료합니다")
                break

            frame = self._camera.read()
            if frame is None:
                break

            person = self._detector.detect(frame)
            if person is None:
                continue

            last_seen = time.monotonic()
            center_x, center_y = center_of(person)
            height, width = frame.shape[:2]
            if self._tracker.update(center_x, center_y, width, height):
                moved = True
        return moved

    def _show(self, status):
        self._display.show(status, database.count_on())
