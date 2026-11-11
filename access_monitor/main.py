"""감지 프로그램의 시작 파일.

실제 장치로 실행:     python main.py
장치 없이 PC에서 시험: python main.py --sim
영상 파일로 시험:     python main.py --sim --video 영상.mp4
자동으로 5초마다 감지: python main.py --sim --trigger-every 5
"""

import argparse
import logging
import signal
import threading

import config
import database
import hardware
from monitor import AccessMonitor
from tracker import PanTiltTracker
from vision import Camera, MotionFilter, create_person_detector


def parse_args():
    parser = argparse.ArgumentParser(description="출입 감지 및 추적 시스템")
    parser.add_argument("--sim", action="store_true", help="PIR, 버저, OLED, 서보 없이 실행")
    parser.add_argument("--video", help="웹캠 대신 사용할 영상 파일")
    parser.add_argument("--trigger-every", type=float, help="--sim에서 Enter 대신 N초마다 자동 감지")
    return parser.parse_args()


def main():
    args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    database.init_db()

    # 부품을 만든다. --sim이면 가짜 부품을 쓴다.
    if args.sim:
        pir = hardware.SimPir(args.trigger_every)
        buzzer = hardware.NullBuzzer()
        display = hardware.NullDisplay()
        servos = hardware.NullServos()
        warmup = 0
    else:
        pir = hardware.PirSensor()
        buzzer = hardware.ActiveBuzzer()
        display = hardware.create_display()
        servos = hardware.create_servos()
        warmup = config.PIR_WARMUP_SECONDS

    # Ctrl+C(SIGINT)나 종료 신호(SIGTERM)를 받으면 이 깃발을 올려서 루프를 끝낸다.
    stop_event = threading.Event()
    for signal_number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signal_number, lambda *_: stop_event.set())

    monitor = AccessMonitor(
        pir=pir,
        camera=Camera(video_path=args.video),
        motion_filter=MotionFilter(),
        detector=create_person_detector(),
        tracker=PanTiltTracker(servos),
        buzzer=buzzer,
        display=display,
        stop_event=stop_event,
    )

    try:
        monitor.wait_for_warmup(warmup)
        monitor.run()
    finally:
        # 어떤 이유로 끝나든 장치를 안전한 상태로 돌려놓는다.
        for device in (servos, buzzer, display, pir):
            device.close()
        logging.info("종료했습니다")


if __name__ == "__main__":
    main()
