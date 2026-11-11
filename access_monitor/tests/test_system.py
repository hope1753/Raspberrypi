"""장치 없이 PC에서 돌릴 수 있는 시험.

실행: access_monitor 폴더에서  python -m unittest discover -s tests -v
"""

import sys
import tempfile
import threading
import types
import unittest
from datetime import date, datetime
from pathlib import Path
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import config
import database
import hardware
import tracker
import vision
from monitor import AccessMonitor
from vision import Detection


class TempStorage(unittest.TestCase):
    """시험마다 임시 폴더의 DB와 스냅샷을 쓴다. 진짜 데이터는 건드리지 않는다."""

    def setUp(self):
        self._dir = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(self._dir.cleanup)
        root = Path(self._dir.name)
        for name, value in (("DB_PATH", root / "t.db"), ("SNAPSHOT_DIR", root / "snap")):
            patcher = mock.patch.object(config, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        database.init_db()


class DatabaseTest(TempStorage):
    def test_counts_and_order(self):
        database.add_event("left", 1, 2, "a.jpg", when=datetime(2026, 11, 1, 9, 0, 0))
        database.add_event("right", 3, 4, None, when=datetime(2026, 11, 2, 9, 30, 0))
        database.add_event("center", 5, 6, "c.jpg", when=datetime(2026, 11, 2, 14, 0, 0))

        events = database.recent_events()
        self.assertEqual([e["zone"] for e in events], ["center", "right", "left"])
        self.assertEqual(database.count_on(date(2026, 11, 2)), 2)

        hourly = database.hourly_counts(date(2026, 11, 2))
        self.assertEqual((len(hourly), hourly[9], hourly[14], sum(hourly)), (24, 1, 1, 2))

        daily = database.daily_counts(3, today=date(2026, 11, 3))
        self.assertEqual([d["count"] for d in daily], [1, 2, 0])
        self.assertEqual(daily[0]["date"], "2026-11-01")


class TrackerTest(unittest.TestCase):
    def setUp(self):
        self.calls = []
        servos = types.SimpleNamespace(
            set_angles=lambda pan, tilt: self.calls.append((pan, tilt)),
            release=lambda: self.calls.append("release"),
        )
        self.tracker = tracker.PanTiltTracker(servos)

    def test_deadzone_does_not_move(self):
        edge = config.DEADZONE_PIXELS
        self.assertFalse(self.tracker.update(320 + edge, 240 - edge, 640, 480))
        self.assertEqual(self.calls, [])

    def test_step_is_proportional(self):
        self.assertAlmostEqual(tracker.axis_step(100), 100 * config.KP_DEG_PER_PIXEL)
        self.assertAlmostEqual(tracker.axis_step(-100), -100 * config.KP_DEG_PER_PIXEL)

    def test_step_is_limited(self):
        self.assertEqual(tracker.axis_step(100000), config.MAX_STEP_DEG)
        self.assertEqual(tracker.axis_step(-100000), -config.MAX_STEP_DEG)

    def test_angles_stay_inside_limits(self):
        for _ in range(100):
            self.tracker.update(640, 480, 640, 480)
        self.assertIn(self.tracker.pan, config.PAN_LIMITS)
        self.assertIn(self.tracker.tilt, config.TILT_LIMITS)

    def test_pan_direction_follows_setting(self):
        self.tracker.update(600, 240, 640, 480)
        expected = config.HOME_ANGLE + tracker.axis_step(280) * config.PAN_DIRECTION
        self.assertAlmostEqual(self.tracker.pan, expected)
        self.assertEqual(self.tracker.tilt, config.HOME_ANGLE)

    def test_go_home_releases(self):
        self.tracker.update(600, 240, 640, 480)
        self.tracker.go_home(settle_seconds=0)
        self.assertEqual(self.calls[-2:], [(config.HOME_ANGLE, config.HOME_ANGLE), "release"])


class ServoTest(unittest.TestCase):
    def test_pulse_range(self):
        self.assertEqual(hardware.angle_to_pulse(0), config.SERVO_MIN_PULSE_US)
        self.assertEqual(hardware.angle_to_pulse(180), config.SERVO_MAX_PULSE_US)

    def test_pigpio_calls(self):
        fake_pi = mock.Mock(connected=True)
        with mock.patch.dict(sys.modules, {"pigpio": types.SimpleNamespace(pi=lambda: fake_pi)}):
            servos = hardware.PigpioServos(pan_pin=12, tilt_pin=13)
        servos.set_angles(0, 180)
        fake_pi.set_servo_pulsewidth.assert_any_call(12, config.SERVO_MIN_PULSE_US)
        fake_pi.set_servo_pulsewidth.assert_any_call(13, config.SERVO_MAX_PULSE_US)
        servos.close()
        fake_pi.set_servo_pulsewidth.assert_any_call(12, 0)
        fake_pi.stop.assert_called_once()

    def test_missing_pigpiod_falls_back(self):
        fake_pi = mock.Mock(connected=False)
        with mock.patch.dict(sys.modules, {"pigpio": types.SimpleNamespace(pi=lambda: fake_pi)}):
            self.assertIsInstance(hardware.create_servos(), hardware.NullServos)


def frame_with_block(x):
    """검은 배경에 흰 사각형이 x 위치에 있는 프레임."""
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    frame[150:330, x:x + 100] = 255
    return frame


class VisionTest(unittest.TestCase):
    def test_motion_filter(self):
        motion = vision.MotionFilter(learn_frames=5)
        still = frame_with_block(50)
        for _ in range(5):
            self.assertFalse(motion.has_motion(still))  # 학습 중
        self.assertFalse(any(motion.has_motion(still) for _ in range(5)))  # 정지 화면
        self.assertTrue(motion.has_motion(frame_with_block(400)))  # 물체가 이동함

    def test_zone(self):
        self.assertEqual([vision.zone_of(x, 600) for x in (10, 300, 590)], ["left", "center", "right"])

    def test_largest_person_is_chosen(self):
        people = [Detection(0, 0, 10, 10), Detection(5, 5, 100, 100)]
        self.assertEqual(vision._largest(people).w, 100)
        self.assertIsNone(vision._largest([]))

    def test_falls_back_to_haar_without_model(self):
        with mock.patch.object(config, "MOBILENET_WEIGHTS", Path("없는파일.caffemodel")):
            detector = vision.create_person_detector()
        self.assertIsInstance(detector, vision.HaarPersonDetector)
        self.assertIsNone(detector.detect(np.zeros((480, 640, 3), dtype=np.uint8)))


class FakeCamera:
    def __init__(self, opens=True, frames=None):
        self.opens = opens
        self.frames = frames  # None이면 무한히 프레임을 준다.
        self.opened = 0
        self.closed = 0

    def open(self):
        self.opened += 1
        return self.opens

    def read(self):
        if self.frames is not None:
            if not self.frames:
                return None
            self.frames -= 1
        return np.zeros((480, 640, 3), dtype=np.uint8)

    def close(self):
        self.closed += 1


class FakeMotion:
    def __init__(self, result=True):
        self.result = result
        self.resets = 0

    def reset(self):
        self.resets += 1

    def has_motion(self, frame):
        return self.result


class FakeDetector:
    """정해진 횟수만큼 사람을 돌려주고, 그 뒤로는 없다고 답한다."""

    def __init__(self, hits, person=Detection(500, 100, 100, 200)):
        self.hits = hits
        self.person = person
        self.calls = 0

    def detect(self, frame):
        self.calls += 1
        if self.hits > 0:
            self.hits -= 1
            return self.person
        return None


class MonitorTest(TempStorage):
    def setUp(self):
        super().setUp()
        for name, value in (("DETECT_WINDOW_SECONDS", 0.1), ("TRACK_LOST_SECONDS", 0.05), ("MAX_EVENT_SECONDS", 0.3)):
            patcher = mock.patch.object(config, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.buzzer = mock.Mock()
        self.display = mock.Mock()
        self.tracker = mock.Mock()
        self.tracker.update.return_value = True
        self.stop = threading.Event()

    def build(self, camera, motion, detector):
        return AccessMonitor(mock.Mock(), camera, motion, detector, self.tracker, self.buzzer, self.display, self.stop)

    def statuses(self):
        return [call.args[0] for call in self.display.show.call_args_list]

    def test_person_creates_one_event_and_tracks(self):
        camera = FakeCamera()
        motion = FakeMotion()
        detected = self.build(camera, motion, FakeDetector(hits=5)).handle_trigger()

        self.assertTrue(detected)
        self.assertEqual(motion.resets, 1)
        self.buzzer.alert.assert_called_once()
        events = database.recent_events()
        self.assertEqual(len(events), 1)  # 여러 프레임에 잡혀도 이벤트는 하나다.
        self.assertEqual((events[0]["zone"], events[0]["center_x"], events[0]["center_y"]), ("right", 550, 200))
        self.assertTrue((config.SNAPSHOT_DIR / events[0]["snapshot"]).exists())
        self.assertEqual(self.tracker.update.call_count, 4)  # 첫 검출 뒤 남은 4번을 추적한다.
        self.tracker.go_home.assert_called_once()
        self.assertEqual(camera.closed, 1)
        self.assertEqual(self.statuses(), ["CHECKING", "ALERT", "NORMAL"])

    def test_no_person_returns_to_idle(self):
        camera = FakeCamera()
        detected = self.build(camera, FakeMotion(), FakeDetector(hits=0)).handle_trigger()

        self.assertFalse(detected)
        self.buzzer.alert.assert_not_called()
        self.assertEqual(database.recent_events(), [])
        self.tracker.go_home.assert_not_called()
        self.assertEqual(camera.closed, 1)
        self.assertEqual(self.statuses(), ["CHECKING", "NORMAL"])

    def test_detector_skipped_without_motion(self):
        detector = FakeDetector(hits=5)
        self.assertFalse(self.build(FakeCamera(), FakeMotion(False), detector).handle_trigger())
        self.assertEqual(detector.calls, 0)

    def test_detector_runs_when_filter_disabled(self):
        detector = FakeDetector(hits=1)
        with mock.patch.object(config, "USE_MOTION_FILTER", False):
            self.assertTrue(self.build(FakeCamera(), FakeMotion(False), detector).handle_trigger())

    def test_camera_failure_is_not_fatal(self):
        camera = FakeCamera(opens=False)
        self.assertFalse(self.build(camera, FakeMotion(), FakeDetector(hits=1)).handle_trigger())
        self.assertEqual(self.statuses(), ["CHECKING", "NORMAL"])

    def test_camera_dies_mid_event(self):
        camera = FakeCamera(frames=1)
        self.assertTrue(self.build(camera, FakeMotion(), FakeDetector(hits=9)).handle_trigger())
        self.assertEqual(camera.closed, 1)
        self.assertEqual(len(database.recent_events()), 1)

    def test_long_stay_ends_at_max_event_time(self):
        camera = FakeCamera()
        self.assertTrue(self.build(camera, FakeMotion(), FakeDetector(hits=10**9)).handle_trigger())
        self.assertEqual(camera.closed, 1)

    def test_exception_still_closes_camera(self):
        camera = FakeCamera()
        detector = FakeDetector(hits=1)
        detector.detect = mock.Mock(side_effect=RuntimeError("boom"))
        with self.assertRaises(RuntimeError):
            self.build(camera, FakeMotion(), detector).handle_trigger()
        self.assertEqual(camera.closed, 1)

    def test_run_loop_stops(self):
        pir = mock.Mock()
        def waiter(timeout):
            if pir.wait_for_trigger.call_count == 1:
                return True
            self.stop.set()
            return False
        pir.wait_for_trigger.side_effect = waiter
        camera = FakeCamera()
        monitor = AccessMonitor(pir, camera, FakeMotion(), FakeDetector(hits=1), self.tracker, self.buzzer, self.display, self.stop)
        monitor.run()
        self.assertEqual(camera.opened, 1)
        self.assertEqual(len(database.recent_events()), 1)


class DashboardTest(TempStorage):
    def setUp(self):
        super().setUp()
        import dashboard
        self.client = dashboard.app.test_client()

    def test_empty_summary(self):
        data = self.client.get("/api/summary").get_json()
        self.assertEqual((data["today"], data["events"], len(data["hourly"]), len(data["daily"])), (0, [], 24, 7))

    def test_summary_and_snapshot(self):
        config.SNAPSHOT_DIR.mkdir(parents=True)
        (config.SNAPSHOT_DIR / "a.jpg").write_bytes(b"jpeg")
        database.add_event("left", 1, 2, "a.jpg")

        data = self.client.get("/api/summary").get_json()
        self.assertEqual(data["today"], 1)
        self.assertEqual(data["events"][0]["snapshot"], "a.jpg")
        response = self.client.get("/snapshots/a.jpg")
        self.assertEqual(response.data, b"jpeg")
        response.close()

    def test_snapshot_cannot_escape_folder(self):
        config.SNAPSHOT_DIR.mkdir(parents=True)
        self.assertEqual(self.client.get("/snapshots/../t.db").status_code, 404)

    def test_index_page(self):
        response = self.client.get("/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("이벤트 타임라인", response.get_data(as_text=True))


if __name__ == "__main__":
    unittest.main()
