"""Evaluate schedule windows at the current local time (reference for UI / Home Assistant migration)."""

from __future__ import annotations

from datetime import datetime

from dog_door_pi.db import ScheduleRule, list_rules


def _minutes_since_midnight(dt: datetime) -> int:
    return dt.hour * 60 + dt.minute


def _parse_days(days_csv: str) -> set[int]:
    parts = [p.strip() for p in days_csv.split(",") if p.strip() != ""]
    return {int(p) for p in parts}


def _in_time_window(now_m: int, start_m: int, end_m: int) -> bool:
    """Inclusive start, exclusive end on same day; if end_m < start_m, window crosses midnight."""
    if start_m <= end_m:
        return start_m <= now_m < end_m
    return now_m >= start_m or now_m < end_m


def exit_allowed_at(when: datetime | None = None) -> bool:
    """
    Policy:
    - Any matching **block** rule → deny.
    - If there is at least one **allow** rule in the DB: require matching at least one **allow** rule.
    - If there are no **allow** rules: after blocks, allow.
    """
    when = when or datetime.now()
    rules = list_rules()
    now_m = _minutes_since_midnight(when)
    dow = when.weekday()  # Monday=0 .. Sunday=6

    allow_rules = [r for r in rules if r.action == "allow"]
    block_rules = [r for r in rules if r.action == "block"]

    for r in block_rules:
        days = _parse_days(r.days)
        if dow not in days:
            continue
        if _in_time_window(now_m, r.start_minutes, r.end_minutes):
            return False

    if not allow_rules:
        return True

    for r in allow_rules:
        days = _parse_days(r.days)
        if dow not in days:
            continue
        if _in_time_window(now_m, r.start_minutes, r.end_minutes):
            return True

    return False
