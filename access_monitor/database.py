"""이벤트를 SQLite 파일에 저장하고 읽는 모듈.

감지 프로그램(main.py)과 대시보드(dashboard.py)는 서로 다른 프로그램이지만
같은 DB 파일을 함께 쓴다. 감지 쪽이 쓰고, 대시보드 쪽이 읽는 구조다.
"""

import sqlite3
from contextlib import closing
from datetime import datetime, timedelta

import config

_SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp  TEXT NOT NULL,   -- 발생 시각, 예: 2026-11-02T14:30:05
    zone       TEXT NOT NULL,   -- 화면 속 위치: left, center, right
    center_x   INTEGER NOT NULL,-- 사람 중심의 x 좌표(픽셀)
    center_y   INTEGER NOT NULL,-- 사람 중심의 y 좌표(픽셀)
    snapshot   TEXT             -- 스냅샷 파일 이름. 저장에 실패하면 비어 있다.
);
CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events (timestamp);
"""


def _connect(db_path=None):
    # timeout: 다른 프로그램이 쓰는 중이면 바로 실패하지 않고 최대 5초 기다린다.
    conn = sqlite3.connect(str(db_path or config.DB_PATH), timeout=5)
    conn.row_factory = sqlite3.Row  # 결과를 row["zone"]처럼 이름으로 꺼낼 수 있게 한다.
    return conn


def init_db(db_path=None):
    """테이블이 없으면 만든다. 이미 있으면 아무 일도 하지 않는다."""
    with closing(_connect(db_path)) as conn, conn:
        # WAL 모드: 한쪽이 쓰는 동안에도 다른 쪽이 읽을 수 있다.
        conn.execute("PRAGMA journal_mode=WAL")
        conn.executescript(_SCHEMA)


def add_event(zone, center_x, center_y, snapshot, when=None, db_path=None):
    """이벤트 한 건을 저장하고 새 id를 돌려준다."""
    when = when or datetime.now()
    with closing(_connect(db_path)) as conn, conn:
        # 값을 ? 자리에 넘기면 SQLite가 안전하게 처리해 준다.
        cursor = conn.execute(
            "INSERT INTO events (timestamp, zone, center_x, center_y, snapshot) "
            "VALUES (?, ?, ?, ?, ?)",
            (when.isoformat(timespec="seconds"), zone, center_x, center_y, snapshot),
        )
        return cursor.lastrowid


def recent_events(limit=50, db_path=None):
    """최근 이벤트를 새로운 것부터 돌려준다. (타임라인용)"""
    with closing(_connect(db_path)) as conn:
        rows = conn.execute(
            "SELECT * FROM events ORDER BY timestamp DESC, id DESC LIMIT ?", (limit,)
        ).fetchall()
    return [dict(row) for row in rows]


def count_on(day=None, db_path=None):
    """특정 날짜(기본: 오늘)의 이벤트 수."""
    day = day or datetime.now().date()
    with closing(_connect(db_path)) as conn:
        row = conn.execute(
            "SELECT COUNT(*) AS n FROM events WHERE substr(timestamp, 1, 10) = ?",
            (day.isoformat(),),
        ).fetchone()
    return row["n"]


def hourly_counts(day=None, db_path=None):
    """특정 날짜의 시간대별 이벤트 수를 24칸 리스트로 돌려준다. (0시~23시)"""
    day = day or datetime.now().date()
    counts = [0] * 24
    with closing(_connect(db_path)) as conn:
        rows = conn.execute(
            "SELECT CAST(substr(timestamp, 12, 2) AS INTEGER) AS hour, COUNT(*) AS n "
            "FROM events WHERE substr(timestamp, 1, 10) = ? GROUP BY hour",
            (day.isoformat(),),
        ).fetchall()
    for row in rows:
        counts[row["hour"]] = row["n"]
    return counts


def daily_counts(days=7, today=None, db_path=None):
    """최근 며칠의 일별 이벤트 수를 오래된 날짜부터 돌려준다.

    이벤트가 없는 날도 0으로 채워서, 그래프에 빠지는 날이 없게 한다.
    """
    today = today or datetime.now().date()
    dates = [today - timedelta(days=offset) for offset in range(days - 1, -1, -1)]
    with closing(_connect(db_path)) as conn:
        rows = conn.execute(
            "SELECT substr(timestamp, 1, 10) AS day, COUNT(*) AS n FROM events "
            "WHERE substr(timestamp, 1, 10) >= ? GROUP BY day",
            (dates[0].isoformat(),),
        ).fetchall()
    found = {row["day"]: row["n"] for row in rows}
    return [{"date": d.isoformat(), "count": found.get(d.isoformat(), 0)} for d in dates]
