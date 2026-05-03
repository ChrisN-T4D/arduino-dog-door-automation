"""FastAPI web UI + serial heartbeat + commands."""

from __future__ import annotations

import asyncio
import logging
import re
import secrets
import threading
from collections import deque
from datetime import datetime
from pathlib import Path

import serial
from fastapi import Depends, FastAPI, Form, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from fastapi.templating import Jinja2Templates
from starlette.status import HTTP_303_SEE_OTHER

from dog_door_pi import config
from dog_door_pi.db import add_rule, delete_rule, init_db, list_rules
from dog_door_pi.scheduler import exit_allowed_at
from dog_door_pi.serial_client import SerialBridge

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

BASE_DIR = Path(__file__).resolve().parent
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))

security = HTTPBasic(auto_error=False)

def _fastapi_kwargs():
    base = {"title": "Dog Door Pi", "version": "0.1.0"}
    if not config.DEVELOPMENT:
        base.update({"docs_url": None, "redoc_url": None, "openapi_url": None})
    return base


app = FastAPI(**_fastapi_kwargs())

# Shared runtime (filled in lifespan)
class AppRuntime:
    bridge: SerialBridge | None = None
    door_state: str = "unknown"
    recent_lines: deque[str] = deque(maxlen=80)
    serial_ok: bool = False


runtime = AppRuntime()
_runtime_lock = threading.Lock()

# Arduino sends STATE:CLOSED | MOVING_CLOSED | MOVING_OPEN | OPEN (see dog_door.ino).
_STATE_RE = re.compile(r"^\s*STATE:\s*(\S+)", re.IGNORECASE)

_STATE_LABELS = {
    "CLOSED": "Closed",
    "MOVING_CLOSED": "Closing",
    "MOVING_OPEN": "Opening",
    "OPEN": "Open",
    "unknown": "Unknown",
}


def _door_state_label(raw: str) -> str:
    key = (raw or "").strip().upper()
    return _STATE_LABELS.get(key, raw or "unknown")


def _on_serial_line(line: str) -> None:
    with _runtime_lock:
        runtime.recent_lines.append(line)
        m = _STATE_RE.match(line.strip())
        if m:
            raw = m.group(1).upper()
            runtime.door_state = raw
            logger.debug("Arduino STATE -> %s", raw)


async def _heartbeat_loop() -> None:
    while True:
        try:
            allowed = exit_allowed_at()
            cmd = "EXIT_ALLOWED" if allowed else "EXIT_DENIED"
            if runtime.bridge:
                runtime.bridge.send_line(cmd)
                logger.debug("Heartbeat: %s", cmd)
        except Exception as e:
            logger.warning("Heartbeat failed: %s", e)
        await asyncio.sleep(config.HEARTBEAT_INTERVAL_SEC)


def _password_matches(provided: str, expected: str) -> bool:
    """Timing-safe comparison for HTTP Basic password."""
    if not expected.strip():
        # Only allowed when validate_web_config permitted empty PW (DOG_DOOR_DEVELOPMENT=1).
        return config.DEVELOPMENT
    try:
        return secrets.compare_digest(
            provided.encode("utf-8"),
            expected.strip().encode("utf-8"),
        )
    except Exception:
        return False


def _require_auth(credentials: HTTPBasicCredentials | None = Depends(security)) -> None:
    if credentials is None:
        raise HTTPException(
            status_code=401,
            detail="Authentication required",
            headers={"WWW-Authenticate": "Basic"},
        )
    if not _password_matches(credentials.password, config.WEB_PASSWORD.strip()):
        raise HTTPException(
            status_code=401,
            detail="Invalid password",
            headers={"WWW-Authenticate": "Basic"},
        )


@app.on_event("startup")
async def startup() -> None:
    config.validate_web_config()
    init_db()
    runtime.bridge = SerialBridge(
        config.SERIAL_PORT,
        config.SERIAL_BAUD,
        on_line=_on_serial_line,
    )
    try:
        runtime.bridge.open()
        runtime.serial_ok = True
        logger.info("Serial opened: %s", config.SERIAL_PORT)
        # Prime Arduino heartbeat immediately
        allowed = exit_allowed_at()
        runtime.bridge.send_line("EXIT_ALLOWED" if allowed else "EXIT_DENIED")
    except serial.SerialException as e:
        logger.warning("Could not open serial (%s): %s", config.SERIAL_PORT, e)
        runtime.serial_ok = False
    asyncio.create_task(_heartbeat_loop())


@app.on_event("shutdown")
async def shutdown() -> None:
    if runtime.bridge:
        runtime.bridge.close()


@app.get("/", response_class=HTMLResponse)
async def index(request: Request, _: None = Depends(_require_auth)):
    rules = list_rules()
    exit_ok = exit_allowed_at()
    with _runtime_lock:
        door_raw = runtime.door_state
        recent = list(runtime.recent_lines)
    # Starlette 0.40+ expects (request, name, context); older two-arg form breaks Jinja.
    return templates.TemplateResponse(
        request,
        "index.html",
        {
            "door_state": door_raw,
            "door_state_label": _door_state_label(door_raw),
            "exit_allowed": exit_ok,
            "serial_ok": runtime.serial_ok,
            "serial_port": config.SERIAL_PORT,
            "rules": rules,
            "recent": recent,
            "now": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        },
    )


@app.get("/api/status")
async def api_status(_: None = Depends(_require_auth)):
    with _runtime_lock:
        door_raw = runtime.door_state
    return JSONResponse(
        {
            "door_state": door_raw,
            "door_state_label": _door_state_label(door_raw),
            "exit_allowed_schedule": exit_allowed_at(),
            "serial_ok": runtime.serial_ok,
            "serial_port": config.SERIAL_PORT,
            "time": datetime.now().isoformat(timespec="seconds"),
        }
    )


@app.post("/action/open")
async def action_open(_: None = Depends(_require_auth)):
    if runtime.bridge and runtime.serial_ok:
        runtime.bridge.send_line("CMD_OPEN")
    return RedirectResponse("/", status_code=HTTP_303_SEE_OTHER)


@app.post("/action/close")
async def action_close(_: None = Depends(_require_auth)):
    if runtime.bridge and runtime.serial_ok:
        runtime.bridge.send_line("CMD_CLOSE")
    return RedirectResponse("/", status_code=HTTP_303_SEE_OTHER)


def _time_to_minutes(t: str) -> int:
    parts = t.strip().split(":")
    if len(parts) != 2:
        raise ValueError("bad time")
    h, m = int(parts[0]), int(parts[1])
    return max(0, min(1439, h * 60 + m))


@app.post("/rules/add")
async def rules_add(
    _: None = Depends(_require_auth),
    label: str = Form(""),
    action: str = Form(...),
    start_time: str = Form(...),
    end_time: str = Form(...),
    day_0: str | None = Form(None),
    day_1: str | None = Form(None),
    day_2: str | None = Form(None),
    day_3: str | None = Form(None),
    day_4: str | None = Form(None),
    day_5: str | None = Form(None),
    day_6: str | None = Form(None),
):
    if action not in ("allow", "block"):
        raise HTTPException(400, "invalid action")
    days_list = []
    for i, v in enumerate([day_0, day_1, day_2, day_3, day_4, day_5, day_6]):
        if v == "on":
            days_list.append(str(i))
    if not days_list:
        raise HTTPException(400, "select at least one day")
    days_csv = ",".join(days_list)
    try:
        sm = _time_to_minutes(start_time)
        em = _time_to_minutes(end_time)
    except ValueError:
        raise HTTPException(400, "invalid time format (use HH:MM)")
    add_rule(label.strip(), action, days_csv, sm, em)
    return RedirectResponse("/", status_code=HTTP_303_SEE_OTHER)


@app.post("/rules/{rule_id}/delete")
async def rules_delete(rule_id: int, _: None = Depends(_require_auth)):
    delete_rule(rule_id)
    return RedirectResponse("/", status_code=HTTP_303_SEE_OTHER)


def run() -> None:
    import uvicorn

    uvicorn.run(
        "dog_door_pi.main:app",
        host=config.HTTP_HOST,
        port=config.HTTP_PORT,
        reload=False,
    )


if __name__ == "__main__":
    run()
