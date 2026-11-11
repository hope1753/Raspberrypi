# 출입 감지 및 추적 시스템

PIR 센서가 움직임을 감지하면 웹캠을 켜서 사람인지 확인하고, 사람이면 버저, OLED, DB 기록, 팬틸트 추적을 수행한다. 기록은 웹 대시보드에서 볼 수 있다.

## 파일 구성

| 파일 | 역할 |
|---|---|
| `main.py` | 감지 프로그램 시작 파일 |
| `monitor.py` | 전체 흐름 제어 (대기 → 검사 → 경고 → 추적) |
| `vision.py` | 웹캠, MOG2 모션 필터, 사람 검출 |
| `tracker.py` | 팬틸트 비례 제어 계산 |
| `hardware.py` | PIR, 버저, OLED, 서보 제어 |
| `database.py` | SQLite 저장과 조회 |
| `dashboard.py`, `templates/` | Flask 대시보드 |
| `config.py` | 핀 번호와 모든 설정값 |
| `tests/` | 장치 없이 돌리는 시험 |

## 배선

| 부품 | 연결 |
|---|---|
| HC-SR501 | VCC → 5V, GND → GND, OUT → GPIO17 |
| 능동 버저 | + → GPIO27, - → GND |
| SSD1306 OLED | VCC → 3.3V, GND → GND, SDA → GPIO2, SCL → GPIO3 |
| 팬 서보 SG90 | 신호 → GPIO12, 전원 → 별도 5V |
| 틸트 서보 SG90 | 신호 → GPIO13, 전원 → 별도 5V |
| 로지텍 C270 | USB |

주의할 점:

- 서보 전원은 Pi의 5V 핀에서 뽑지 말고 별도 5V를 쓴다. 서보가 움직일 때 전류가 튀어 Pi가 재부팅될 수 있다.
- 별도 전원의 GND와 Pi의 GND는 반드시 서로 연결한다.
- HC-SR501의 OUT 전압이 3.3V인지 처음에 멀티미터로 확인한다.

## 라즈베리파이 설치

```bash
sudo apt update
sudo apt install -y python3-venv python3-opencv python3-flask python3-gpiozero python3-lgpio python3-pigpio pigpio python3-pil i2c-tools
sudo raspi-config nonint do_i2c 0
sudo systemctl enable --now pigpiod
```

`pigpio` 패키지가 없다고 나오면 Pi OS 버전 문제일 수 있다. 서보는 추가 과제이므로 없어도 기본 기능은 동작한다.

```bash
cd access_monitor
python3 -m venv --system-site-packages .venv
source .venv/bin/activate
pip install luma.oled
```

OLED가 연결됐는지 확인한다. 표에 `3c`가 보이면 정상이다.

```bash
i2cdetect -y 1
```

## 실행

터미널 두 개에서 각각 실행한다.

```bash
python main.py
```

```bash
python dashboard.py
```

대시보드 주소는 `http://<Pi의 IP 주소>:5000` 이다. 로그인 기능이 없어서 같은 네트워크의 누구나 스냅샷을 볼 수 있다. 막으려면 `config.py`의 `DASHBOARD_HOST`를 `127.0.0.1`로 바꾼다.

## 장치 없이 시험

```bash
pip install -r requirements.txt
python -m unittest discover -s tests -v
```

PC의 웹캠으로 전체 흐름을 볼 수도 있다. Enter 키가 PIR 감지를 대신한다.

```bash
python main.py --sim
```

## 사람 검출 모델

`models/` 폴더에 `MobileNetSSD_deploy.prototxt`와 `MobileNetSSD_deploy.caffemodel`을 넣으면 MobileNet-SSD를 사용한다. 파일이 없으면 OpenCV에 포함된 Haar cascade(전신)를 자동으로 사용한다. Haar는 전신이 화면에 다 들어와야 잘 잡히고 오검출이 많은 편이라, 시연 전에 MobileNet-SSD로 바꾸는 것을 권장한다.

## 설치 후 조정할 값 (`config.py`)

| 증상 | 조정 |
|---|---|
| 카메라가 사람 반대쪽으로 돈다 | `PAN_DIRECTION`, `TILT_DIRECTION` 부호 변경 |
| 카메라가 좌우로 흔들린다 | `KP_DEG_PER_PIXEL` 감소, `DEADZONE_PIXELS` 증가 |
| 추적이 느리다 | `KP_DEG_PER_PIXEL` 증가 |
| 사람이 지나갔는데 놓친다 | `CAMERA_WARMUP_FRAMES`, `MOTION_LEARN_FRAMES` 감소, 또는 `USE_MOTION_FILTER = False` |
| 서보 끝에서 소리가 난다 | `SERVO_MIN_PULSE_US`, `SERVO_MAX_PULSE_US` 범위 축소 |
