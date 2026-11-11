"""PIR 센서, 버저, OLED, 서보모터를 다루는 모듈.

실제 장치용 클래스와, 장치 없이 시험할 때 쓰는 가짜(Sim/Null) 클래스가 함께 있다.
두 쪽의 함수 이름이 같아서 monitor.py는 어느 쪽을 받든 똑같이 동작한다.

라이브러리는 함수 안에서 import한다. 그래야 gpiozero나 pigpio가 없는 PC에서도
--sim 모드로 프로그램을 실행할 수 있다.
"""

import logging
import threading
import time

import config

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# PIR 모션 센서
# ---------------------------------------------------------------------------
class PirSensor:
    """HC-SR501 PIR 센서. 움직임이 감지되는 순간 인터럽트처럼 알려 준다."""

    def __init__(self, pin=config.PIR_PIN):
        from gpiozero import MotionSensor

        self._triggered = threading.Event()
        self._sensor = MotionSensor(pin)
        # 신호가 LOW에서 HIGH로 바뀌는 순간 gpiozero가 이 함수를 자동으로 불러 준다.
        # 메인 프로그램은 잠들어 있다가 Event가 켜지면 깨어나므로 CPU를 거의 쓰지 않는다.
        self._sensor.when_motion = self._triggered.set

    def wait_for_trigger(self, timeout):
        """감지가 있으면 True, timeout 초 안에 없으면 False."""
        fired = self._triggered.wait(timeout)
        self._triggered.clear()
        return fired

    def clear(self):
        """지금까지 쌓인 감지를 지운다. (안정화 시간의 오감지를 버릴 때 사용)"""
        self._triggered.clear()

    def close(self):
        self._sensor.close()


class SimPir:
    """가짜 PIR. Enter 키를 누르거나, 정해진 간격마다 감지된 것으로 처리한다."""

    def __init__(self, every_seconds=None):
        self._triggered = threading.Event()
        target = self._auto_loop if every_seconds else self._keyboard_loop
        self._every = every_seconds
        threading.Thread(target=target, daemon=True).start()

    def _keyboard_loop(self):
        print("[SIM] Enter 키를 누르면 PIR이 감지한 것으로 처리합니다.")
        try:
            while True:
                input()
                self._triggered.set()
        except EOFError:
            pass

    def _auto_loop(self):
        while True:
            time.sleep(self._every)
            self._triggered.set()

    def wait_for_trigger(self, timeout):
        fired = self._triggered.wait(timeout)
        self._triggered.clear()
        return fired

    def clear(self):
        self._triggered.clear()

    def close(self):
        pass


# ---------------------------------------------------------------------------
# 버저
# ---------------------------------------------------------------------------
class ActiveBuzzer:
    """능동 버저. 전기만 주면 소리가 나는 종류다. (수동 버저는 소리가 다르게 난다)"""

    def __init__(self, pin=config.BUZZER_PIN):
        from gpiozero import Buzzer

        self._buzzer = Buzzer(pin)

    def alert(self):
        # background=True: 삑삑 소리가 나는 동안에도 프로그램은 멈추지 않고 계속 돈다.
        self._buzzer.beep(on_time=0.2, off_time=0.2, n=3, background=True)

    def close(self):
        self._buzzer.off()
        self._buzzer.close()


class NullBuzzer:
    def alert(self):
        log.info("[SIM] 삐삐삐 (버저)")

    def close(self):
        pass


# ---------------------------------------------------------------------------
# OLED 디스플레이
# ---------------------------------------------------------------------------
class OledDisplay:
    """SSD1306 OLED (I2C, 주소 0x3C)에 상태와 오늘의 감지 횟수를 표시한다."""

    def __init__(self):
        from luma.core.interface.serial import i2c
        from luma.oled.device import ssd1306

        self._device = ssd1306(i2c(port=1, address=0x3C))

    def show(self, status, today_count):
        from luma.core.render import canvas

        # canvas 안에서 그린 내용이 블록이 끝날 때 화면에 한 번에 나타난다.
        # 기본 글꼴에는 한글이 없어서 영어로 표시한다.
        with canvas(self._device) as draw:
            draw.text((0, 0), "Access Monitor", fill="white")
            draw.text((0, 20), f"Status: {status}", fill="white")
            draw.text((0, 40), f"Today : {today_count}", fill="white")

    def close(self):
        self._device.cleanup()


class NullDisplay:
    """OLED가 없을 때 대신 쓰는 가짜 화면. 상태를 로그로 남긴다."""

    def show(self, status, today_count):
        log.info("[화면] %s, 오늘 %d건", status, today_count)

    def close(self):
        pass


def create_display():
    """OLED를 연결해 본다. OLED는 추가 과제라서 실패해도 프로그램은 계속 돌아간다."""
    try:
        return OledDisplay()
    except Exception as error:  # 라이브러리 없음, I2C 꺼짐, 배선 문제 등 원인이 다양하다.
        log.warning("OLED 없이 실행합니다: %s", error)
        return NullDisplay()


# ---------------------------------------------------------------------------
# 서보모터 2개 (팬, 틸트)
# ---------------------------------------------------------------------------
def angle_to_pulse(angle):
    """각도(0~180도)를 서보 펄스 폭(마이크로초)으로 바꾼다."""
    span = config.SERVO_MAX_PULSE_US - config.SERVO_MIN_PULSE_US
    return int(config.SERVO_MIN_PULSE_US + span * angle / 180.0)


class PigpioServos:
    """pigpio 데몬으로 SG90 2개를 제어한다.

    pigpio는 CPU 대신 DMA 하드웨어로 펄스를 만들어서, 소프트웨어 PWM보다 떨림이 적다.
    먼저 pigpiod가 실행 중이어야 한다: sudo systemctl start pigpiod
    """

    def __init__(self, pan_pin=config.PAN_SERVO_PIN, tilt_pin=config.TILT_SERVO_PIN):
        import pigpio

        self._pi = pigpio.pi()
        if not self._pi.connected:
            raise RuntimeError("pigpiod에 연결할 수 없습니다")
        self._pan_pin = pan_pin
        self._tilt_pin = tilt_pin

    def set_angles(self, pan, tilt):
        self._pi.set_servo_pulsewidth(self._pan_pin, angle_to_pulse(pan))
        self._pi.set_servo_pulsewidth(self._tilt_pin, angle_to_pulse(tilt))

    def release(self):
        # 펄스 폭 0은 "신호 끊기"다. 대기 중 서보의 떨림과 소음이 사라진다.
        self._pi.set_servo_pulsewidth(self._pan_pin, 0)
        self._pi.set_servo_pulsewidth(self._tilt_pin, 0)

    def close(self):
        self.release()
        self._pi.stop()


class NullServos:
    """서보가 없을 때 대신 쓰는 가짜 서보. 움직임을 로그로 남긴다."""

    def set_angles(self, pan, tilt):
        log.debug("[서보] pan=%.1f tilt=%.1f", pan, tilt)

    def release(self):
        pass

    def close(self):
        pass


def create_servos():
    """서보를 연결해 본다. 추가 과제라서 실패해도 기본 기능은 계속 돌아간다."""
    if not config.USE_TRACKING:
        return NullServos()
    try:
        return PigpioServos()
    except Exception as error:
        log.warning("서보 없이 실행합니다: %s", error)
        return NullServos()
