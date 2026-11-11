"""이벤트를 보여 주는 Flask 웹 대시보드.

실행: python dashboard.py
접속: http://<Pi의 IP 주소>:5000

주소 목록:
    /                    대시보드 화면
    /api/summary         화면이 5초마다 가져가는 통계와 타임라인 (JSON)
    /snapshots/<파일명>  스냅샷 사진
"""

from flask import Flask, jsonify, render_template, send_from_directory

import config
import database

app = Flask(__name__)


@app.route("/")
def index():
    # 화면의 뼈대만 보낸다. 실제 데이터는 페이지가 /api/summary에서 가져간다.
    return render_template("index.html")


@app.route("/api/summary")
def api_summary():
    return jsonify(
        today=database.count_on(),
        hourly=database.hourly_counts(),
        daily=database.daily_counts(7),
        events=database.recent_events(30),
    )


@app.route("/snapshots/<path:filename>")
def snapshot(filename):
    # send_from_directory는 ../ 같은 경로로 폴더 밖 파일을 가져가는 것을 막아 준다.
    return send_from_directory(config.SNAPSHOT_DIR, filename)


if __name__ == "__main__":
    database.init_db()
    # debug=True는 외부에서 코드를 실행할 수 있어 위험하므로 켜지 않는다.
    app.run(host=config.DASHBOARD_HOST, port=config.DASHBOARD_PORT, debug=False)
