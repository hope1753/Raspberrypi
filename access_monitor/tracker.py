"""팬틸트 서보로 사람을 따라가는 비례 제어 모듈.

[비례 제어(P 제어)란?]
목표에서 많이 벗어나 있으면 많이, 조금 벗어나 있으면 조금 움직이는 방법이다.

    오차 = 사람 중심 좌표 - 화면 중앙 좌표
    이동 각도 = 오차 x KP

[데드존이란?]
사람이 화면 중앙 근처에 있으면 일부러 움직이지 않는 구간이다.
이 구간이 없으면 아주 작은 오차에도 서보가 계속 움직여서 카메라가 덜덜 떨린다.

이 파일은 서보를 직접 다루지 않고 계산만 하므로 장치 없이도 시험할 수 있다.
"""

import time

import config


def clamp(value, low, high):
    """value가 low~high 범위를 벗어나지 않게 자른다."""
    return max(low, min(high, value))


def axis_step(error_pixels):
    """오차(픽셀)로부터 이번에 움직일 각도를 계산한다. 한 축에 대한 계산이다."""
    if abs(error_pixels) <= config.DEADZONE_PIXELS:
        return 0.0  # 데드존 안이면 움직이지 않는다.
    step = error_pixels * config.KP_DEG_PER_PIXEL
    # 한 번에 너무 크게 움직이면 사람이 화면 밖으로 나가거나 서보에 무리가 간다.
    return clamp(step, -config.MAX_STEP_DEG, config.MAX_STEP_DEG)


class PanTiltTracker:
    def __init__(self, servos):
        self._servos = servos
        self.pan = config.HOME_ANGLE
        self.tilt = config.HOME_ANGLE

    def update(self, center_x, center_y, frame_width, frame_height):
        """사람 중심 좌표를 받아 서보를 움직인다. 실제로 움직였으면 True."""
        pan_step = axis_step(center_x - frame_width / 2) * config.PAN_DIRECTION
        tilt_step = axis_step(center_y - frame_height / 2) * config.TILT_DIRECTION
        if pan_step == 0.0 and tilt_step == 0.0:
            return False

        self.pan = clamp(self.pan + pan_step, *config.PAN_LIMITS)
        self.tilt = clamp(self.tilt + tilt_step, *config.TILT_LIMITS)
        self._servos.set_angles(self.pan, self.tilt)
        return True

    def go_home(self, settle_seconds=0.5):
        """정면으로 돌아간 뒤 신호를 끊는다. (대기 중 떨림과 소음 방지)"""
        self.pan = config.HOME_ANGLE
        self.tilt = config.HOME_ANGLE
        self._servos.set_angles(self.pan, self.tilt)
        time.sleep(settle_seconds)  # 서보가 도착할 시간을 준다.
        self._servos.release()
