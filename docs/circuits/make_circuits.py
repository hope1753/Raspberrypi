"""실습 2~4 회로도(SVG)를 만드는 스크립트.

실행:  py make_circuits.py
결과:  같은 폴더에 lab02_circuit.svg, lab03_circuit.svg, lab04_circuit.svg

핀 번호는 src/lab_0X 코드의 상수와 같다. 코드의 핀을 바꾸면 아래 값도 함께 바꾼다.
"""

from pathlib import Path

OUT = Path(__file__).resolve().parent
FONT = "'Malgun Gothic','Noto Sans KR',sans-serif"
INK = "#1f2937"       # 선과 글자
MUTED = "#6b7280"     # 보조 설명
PANEL = "#f8fafc"     # 패널 배경
BORDER = "#cbd5e1"
V33 = "#dc2626"       # 3.3V
V5 = "#ea580c"        # 5V
SIG = "#2563eb"       # GPIO 신호


class Svg:
    def __init__(self, width, height, title):
        self.w, self.h = width, height
        self.items = [
            f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
            f'<text x="30" y="40" font-size="22" font-weight="700" fill="{INK}">{title}</text>',
        ]

    def add(self, s):
        self.items.append(s)

    # ---- 기본 도형 ----
    def wire(self, *pts, color=INK):
        d = "M" + " L".join(f"{x} {y}" for x, y in pts)
        self.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="2" stroke-linejoin="round"/>')

    def dot(self, x, y):
        self.add(f'<circle cx="{x}" cy="{y}" r="4" fill="{INK}"/>')

    def text(self, x, y, s, size=14, color=INK, anchor="start", weight="400"):
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}">{s}</text>')

    def panel(self, x, y, w, h, title):
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{PANEL}" stroke="{BORDER}"/>')
        self.text(x + 16, y + 28, title, size=16, weight="700")

    # ---- 단자 ----
    def port(self, x, y, label, color=SIG, side="left"):
        """Pi 쪽 단자. side는 글자가 놓이는 방향."""
        self.add(f'<circle cx="{x}" cy="{y}" r="5" fill="#ffffff" stroke="{color}" stroke-width="2"/>')
        if side == "left":
            self.text(x - 12, y + 5, label, color=color, anchor="end", weight="700")
        else:
            self.text(x + 12, y + 5, label, color=color, weight="700")

    def gnd(self, x, y):
        """접지 기호. (x, y)에서 아래로 그린다."""
        self.wire((x, y), (x, y + 12))
        for i, half in enumerate((12, 8, 4)):
            yy = y + 12 + i * 5
            self.add(f'<line x1="{x - half}" y1="{yy}" x2="{x + half}" y2="{yy}" stroke="{INK}" stroke-width="2"/>')
        self.text(x, y + 42, "GND", size=12, color=MUTED, anchor="middle")

    # ---- 부품 (가로 방향, 왼쪽 끝 x에서 시작, 길이를 돌려준다) ----
    def resistor(self, x, y, label, length=70):
        zx = x + 10
        pts = [(x, y), (zx, y)]
        step = (length - 20) / 6
        for i in range(6):
            pts.append((zx + step * (i + 0.5), y - 8 if i % 2 == 0 else y + 8))
        pts += [(x + length - 10, y), (x + length, y)]
        self.wire(*pts)
        self.text(x + length / 2, y - 16, label, size=13, anchor="middle")
        return x + length

    def resistor_v(self, x, y, label, length=70):
        zy = y + 10
        pts = [(x, y), (x, zy)]
        step = (length - 20) / 6
        for i in range(6):
            pts.append((x - 8 if i % 2 == 0 else x + 8, zy + step * (i + 0.5)))
        pts += [(x, y + length - 10), (x, y + length)]
        self.wire(*pts)
        self.text(x + 16, y + length / 2 + 5, label, size=13)
        return y + length

    def led(self, x, y, label, color):
        """가로 LED. 왼쪽이 +(긴 다리)."""
        self.wire((x, y), (x + 14, y))
        self.add(f'<path d="M{x + 14} {y - 12} L{x + 14} {y + 12} L{x + 36} {y} Z" fill="{color}" stroke="{INK}" stroke-width="2" stroke-linejoin="round"/>')
        self.add(f'<line x1="{x + 36}" y1="{y - 12}" x2="{x + 36}" y2="{y + 12}" stroke="{INK}" stroke-width="2"/>')
        self.wire((x + 36, y), (x + 50, y))
        self.text(x + 25, y - 20, label, size=13, anchor="middle")
        return x + 50

    def button(self, x, y, label="버튼"):
        self.wire((x, y), (x + 14, y))
        self.wire((x + 46, y), (x + 60, y))
        self.add(f'<circle cx="{x + 16}" cy="{y}" r="3.5" fill="#ffffff" stroke="{INK}" stroke-width="2"/>')
        self.add(f'<circle cx="{x + 44}" cy="{y}" r="3.5" fill="#ffffff" stroke="{INK}" stroke-width="2"/>')
        self.add(f'<line x1="{x + 14}" y1="{y - 12}" x2="{x + 46}" y2="{y - 12}" stroke="{INK}" stroke-width="2"/>')
        self.add(f'<line x1="{x + 30}" y1="{y - 12}" x2="{x + 30}" y2="{y - 22}" stroke="{INK}" stroke-width="2"/>')
        self.text(x + 30, y + 24, label, size=13, anchor="middle")
        return x + 60

    def chip(self, x, y, w, h, name, left=(), right=()):
        """IC나 모듈. left/right는 (핀 이름, y 위치) 목록이고, 핀 끝 좌표를 돌려준다."""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#ffffff" stroke="{INK}" stroke-width="2"/>')
        self.text(x + w / 2, y - 10, name, size=15, anchor="middle", weight="700")
        ends = {}
        for pin, py in left:
            self.wire((x - 24, py), (x, py))
            self.text(x + 8, py + 5, pin, size=13)
            ends[pin] = (x - 24, py)
        for pin, py in right:
            self.wire((x + w, py), (x + w + 24, py))
            self.text(x + w - 8, py + 5, pin, size=13, anchor="end")
            ends[pin] = (x + w + 24, py)
        return ends

    def save(self, name):
        body = "\n".join(self.items)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}">\n{body}\n</svg>\n')
        (OUT / name).write_text(svg, encoding="utf-8")
        print("wrote", name)


# ---------------------------------------------------------------------------
# 회로 조각 (여러 실습에서 다시 쓴다)
# ---------------------------------------------------------------------------
def led_branch(s, x, y, gpio, label, color):
    """GPIO -> 330옴 -> LED -> GND"""
    s.port(x, y, gpio)
    end = s.resistor(x + 30, y, "330Ω")
    s.wire((x + 5, y), (x + 30, y))
    end = s.led(end + 10, y, label, color)
    s.wire((end - 60, y), (end - 50, y))
    s.wire((end, y), (end + 30, y))
    s.gnd(end + 30, y)


def button_branch(s, x, y, gpio, label="버튼"):
    """3.3V -> 10k옴 -> (GPIO) -> 버튼 -> GND"""
    s.port(x, y, "3.3V", color=V33)
    s.wire((x + 5, y), (x + 30, y))
    end = s.resistor(x + 30, y, "10kΩ 풀업")
    node = end + 30
    s.wire((end, y), (node + 30, y))
    s.dot(node, y)
    s.wire((node, y), (node, y + 46))
    s.port(node, y + 51, gpio, side="right")
    end = s.button(node + 30, y, label)
    s.wire((end, y), (end + 30, y))
    s.gnd(end + 30, y)


def dht11(s, x, y):
    """DHT11: DATA -> GPIO4, 10k 풀업"""
    pins = s.chip(x, y, 110, 150, "DHT11", right=[("VCC", y + 25), ("DATA", y + 90), ("GND", y + 128)])
    vx, vy = pins["VCC"]
    dx, dy = pins["DATA"]
    gx, gy = pins["GND"]
    rail = vx + 60
    s.wire((vx, vy), (rail + 160, vy), color=V33)
    s.port(rail + 165, vy, "3.3V", color=V33, side="right")
    s.dot(rail, vy)
    bottom = s.resistor_v(rail, vy, "10kΩ 풀업", length=dy - vy)
    s.dot(rail, dy)
    s.wire((dx, dy), (rail + 160, dy))
    s.port(rail + 165, dy, "GPIO4 (핀 7)", side="right")
    s.wire((gx, gy), (gx + 30, gy))
    s.gnd(gx + 30, gy)
    s.text(x, y + 190, "모듈형(3핀)은 풀업 저항이 기판에 들어 있다.", size=12, color=MUTED)
    return bottom


def cds_mcp3008(s, x, y):
    """CdS 분압 -> MCP3008 CH0 -> SPI"""
    # 분압 회로 (세로)
    s.port(x, y, "3.3V", color=V33, side="right")
    s.wire((x, y + 5), (x, y + 20), color=V33)
    mid = s.resistor_v(x, y + 20, "CdS", length=70)
    s.wire((x, mid), (x, mid + 30))
    s.dot(x, mid + 15)
    low = s.resistor_v(x, mid + 30, "10kΩ", length=70)
    s.gnd(x, low)
    # MCP3008
    cx, cy = x + 150, y + 20
    rows = [cy + 24 + i * 26 for i in range(8)]
    pins = s.chip(cx, cy, 150, 228, "MCP3008",
                  left=[("CH0", mid + 15)],
                  right=[("VDD", rows[0]), ("VREF", rows[1]), ("AGND", rows[2]), ("CLK", rows[3]),
                         ("DOUT", rows[4]), ("DIN", rows[5]), ("CS", rows[6]), ("DGND", rows[7])])
    s.wire((x, mid + 15), pins["CH0"])
    labels = {
        "VDD": ("3.3V", V33), "VREF": ("3.3V", V33), "AGND": ("GND", INK),
        "CLK": ("GPIO11 SCLK (핀 23)", SIG), "DOUT": ("GPIO9 MISO (핀 21)", SIG),
        "DIN": ("GPIO10 MOSI (핀 19)", SIG), "CS": ("GPIO8 CE0 (핀 24)", SIG), "DGND": ("GND", INK),
    }
    for pin, (label, color) in labels.items():
        px, py = pins[pin]
        s.wire((px, py), (px + 30, py), color=color if color != INK else INK)
        s.port(px + 35, py, label, color=color, side="right")
    s.text(x - 20, cy + 268, "밝을수록 CdS 저항이 줄어 CH0 전압이 올라간다.", size=12, color=MUTED)


def hcsr04(s, x, y, buzzer=True):
    """HC-SR04: ECHO는 1k/2k 분압을 거쳐 GPIO24로"""
    pins = s.chip(x, y, 120, 150, "HC-SR04",
                  right=[("VCC", y + 24), ("TRIG", y + 58), ("ECHO", y + 92), ("GND", y + 126)])
    vx, vy = pins["VCC"]
    s.wire((vx, vy), (vx + 250, vy), color=V5)
    s.port(vx + 255, vy, "5V (핀 2)", color=V5, side="right")
    tx, ty = pins["TRIG"]
    s.wire((tx, ty), (tx + 250, ty))
    s.port(tx + 255, ty, "GPIO23 (핀 16)", side="right")
    ex, ey = pins["ECHO"]
    end = s.resistor(ex, ey, "1kΩ")
    node = end + 40
    s.wire((end, ey), (ex + 250, ey))
    s.dot(node, ey)
    s.port(ex + 255, ey, "GPIO24 (핀 18)", side="right")
    low = s.resistor_v(node, ey, "2kΩ", length=70)
    s.gnd(node, low)
    gx, gy = pins["GND"]
    s.wire((gx, gy), (gx + 40, gy), (gx + 40, gy + 30))
    s.gnd(gx + 40, gy + 30)
    s.text(x, y + 232, "ECHO는 5V 신호라서 1kΩ/2kΩ 분압으로 약 3.3V로 낮춘다.", size=12, color=MUTED)
    if buzzer:
        by = y + 290
        s.port(x + 40, by, "GPIO18 (핀 12)")
        s.wire((x + 45, by), (x + 90, by))
        s.add(f'<rect x="{x + 90}" y="{by - 18}" width="70" height="36" rx="6" fill="#ffffff" stroke="{INK}" stroke-width="2"/>')
        s.text(x + 125, by + 5, "부저", size=13, anchor="middle")
        s.wire((x + 160, by), (x + 200, by))
        s.gnd(x + 200, by)
        s.text(x + 230, by + 5, "10cm 이내면 울림", size=12, color=MUTED)


# ---------------------------------------------------------------------------
# 실습별 그림
# ---------------------------------------------------------------------------
def lab02():
    s = Svg(1000, 760, "실습 2 회로 구성 - LED와 스위치, PWM, 상태 기계")
    s.panel(30, 60, 940, 300, "2-A 버튼 토글 / 2-B PWM 밝기 조절")
    led_branch(s, 160, 130, "GPIO17 (핀 11)", "LED", "#facc15")
    button_branch(s, 160, 230, "GPIO23 (핀 16) · 2-A")
    button_branch(s, 600, 230, "GPIO24 (핀 18) · 2-B")
    s.text(46, 340, "버튼은 풀업 회로다. 평소에는 HIGH, 누르면 GND와 연결되어 LOW가 된다.", size=12, color=MUTED)

    s.panel(30, 380, 940, 350, "2-C 신호등 상태 기계 (과제 3: 노란 LED 추가)")
    led_branch(s, 160, 450, "GPIO22 (핀 15)", "빨강 · WALK", "#ef4444")
    led_branch(s, 160, 530, "GPIO17 (핀 11)", "노랑 · WARN", "#facc15")
    led_branch(s, 160, 610, "GPIO27 (핀 13)", "초록 · CAR GO", "#22c55e")
    button_branch(s, 600, 530, "GPIO23 (핀 16)", "보행자 버튼")
    s.text(46, 712, "LED마다 330Ω 저항을 직렬로 연결해 전류를 제한한다.", size=12, color=MUTED)
    s.save("lab02_circuit.svg")


def lab03():
    s = Svg(1000, 1240, "실습 3 회로 구성 - 온습도·조도·거리 센서")
    s.panel(30, 60, 940, 290, "3-A DHT11 온습도 (단일선 비트뱅잉)")
    dht11(s, 120, 130)
    s.panel(30, 370, 940, 400, "3-B CdS 조도 + MCP3008 (SPI)")
    cds_mcp3008(s, 130, 430)
    s.panel(30, 790, 940, 420, "3-C HC-SR04 거리 (펄스 폭 측정) + 부저")
    hcsr04(s, 120, 860, buzzer=True)
    s.save("lab03_circuit.svg")


def lab04():
    s = Svg(1000, 1175, "실습 4 회로 구성 - 센서 통합 웹 서버")
    s.text(30, 66, "실습 3의 센서 3종을 한 브레드보드에 함께 연결한다. 3.3V와 GND는 공통이고, 부저는 사용하지 않는다.", size=13, color=MUTED)
    s.panel(30, 85, 940, 290, "DHT11 → 온도, 습도 채널")
    dht11(s, 120, 155)
    s.panel(30, 395, 940, 400, "CdS + MCP3008 → 조도 채널")
    cds_mcp3008(s, 130, 455)
    s.panel(30, 815, 940, 330, "HC-SR04 → 거리 채널")
    hcsr04(s, 120, 885, buzzer=False)
    s.save("lab04_circuit.svg")


def block(s, x, y, w, h, title, lines=(), fill="#ffffff"):
    """제목과 설명 줄이 있는 장치 상자"""
    s.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{INK}" stroke-width="2"/>')
    s.text(x + w / 2, y + 30, title, size=16, anchor="middle", weight="700")
    for i, line in enumerate(lines):
        s.text(x + w / 2, y + 56 + i * 20, line, size=13, color=MUTED, anchor="middle")


def lab05():
    s = Svg(1000, 560, "실습 5 회로 구성 - 오디오 시각화와 녹음")
    s.text(30, 66, "GPIO 배선이 없다. 웹캠에 들어 있는 마이크를 USB로 연결하고, 리눅스 ALSA로 소리를 읽는다.", size=13, color=MUTED)

    s.panel(30, 85, 940, 270, "녹음 경로 (visualizer, wavrec)")
    block(s, 70, 140, 220, 160, "로지텍 C270", ["USB 웹캠", "내장 마이크 (모노)", "카메라는 이 실습에서", "사용하지 않음"])
    block(s, 390, 140, 220, 160, "라즈베리파이 4", ["USB 2.0 포트 (검은색)", "ALSA 드라이버", "snd-usb-audio"])
    block(s, 710, 140, 220, 160, "실습 프로그램", ["plughw:3,0", "16kHz · 16비트 · 모노", "visualizer → dBFS 막대", "wavrec → record.wav"])
    s.wire((290, 220), (390, 220), color=SIG)
    s.text(340, 210, "USB", size=13, color=SIG, anchor="middle", weight="700")
    s.wire((610, 220), (710, 220), color=SIG)
    s.text(660, 210, "ALSA", size=13, color=SIG, anchor="middle", weight="700")
    s.text(50, 334, "장치 번호(카드 3, 장치 0)는 arecord -l로 확인한다. USB를 다시 꽂으면 바뀔 수 있다.", size=12, color=MUTED)

    s.panel(30, 375, 940, 160, "재생 경로 (aplay, 둘 중 하나)")
    block(s, 70, 420, 400, 90, "Pi 3.5mm 이어폰 단자 → 스피커", ["aplay -D plughw:0,0 record.wav"], fill=PANEL)
    block(s, 530, 420, 400, 90, "노트북으로 복사해서 재생", ["scp로 가져온 뒤 Windows에서 재생"], fill=PANEL)
    s.save("lab05_circuit.svg")


if __name__ == "__main__":
    lab02()
    lab03()
    lab04()
    lab05()
