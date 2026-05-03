"""SQLite persistence for schedule rules."""

from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path

from dog_door_pi import config


@dataclass
class ScheduleRule:
    id: int | None
    label: str
    action: str  # "allow" | "block"
    days: str  # "0,1,2,3,4,5,6" Monday=0 .. Sunday=6
    start_minutes: int  # 0..1439
    end_minutes: int  # 0..1439; if end < start, window spans midnight


def init_db(path: Path | None = None) -> None:
    path = path or config.DB_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(path) as conn:
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS schedule_rules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                label TEXT NOT NULL DEFAULT '',
                action TEXT NOT NULL CHECK (action IN ('allow', 'block')),
                days TEXT NOT NULL DEFAULT '0,1,2,3,4,5,6',
                start_minutes INTEGER NOT NULL,
                end_minutes INTEGER NOT NULL
            )
            """
        )
        conn.commit()


@contextmanager
def get_conn():
    init_db()
    conn = sqlite3.connect(config.DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
    finally:
        conn.close()


def list_rules() -> list[ScheduleRule]:
    with get_conn() as conn:
        rows = conn.execute(
            "SELECT id, label, action, days, start_minutes, end_minutes "
            "FROM schedule_rules ORDER BY id"
        ).fetchall()
    return [
        ScheduleRule(
            id=r["id"],
            label=r["label"] or "",
            action=r["action"],
            days=r["days"],
            start_minutes=int(r["start_minutes"]),
            end_minutes=int(r["end_minutes"]),
        )
        for r in rows
    ]


def add_rule(
    label: str,
    action: str,
    days: str,
    start_minutes: int,
    end_minutes: int,
) -> int:
    with get_conn() as conn:
        cur = conn.execute(
            "INSERT INTO schedule_rules (label, action, days, start_minutes, end_minutes) "
            "VALUES (?, ?, ?, ?, ?)",
            (label, action, days, start_minutes, end_minutes),
        )
        conn.commit()
        return int(cur.lastrowid)


def delete_rule(rule_id: int) -> None:
    with get_conn() as conn:
        conn.execute("DELETE FROM schedule_rules WHERE id = ?", (rule_id,))
        conn.commit()
