"""Environment-driven configuration."""

import os
from pathlib import Path


def _env(key: str, default: str | None = None) -> str | None:
    v = os.environ.get(key)
    if v is None or v.strip() == "":
        return default
    return v.strip()


# Serial device (Pi): often /dev/ttyACM0 (Uno) or /dev/ttyUSB0
SERIAL_PORT = _env("DOG_DOOR_SERIAL", "/dev/ttyACM0")
SERIAL_BAUD = int(_env("DOG_DOOR_BAUD", "9600") or "9600")

# Heartbeat to Arduino (must be < Uno HEARTBEAT_TIMEOUT_MS / 1000, typically 35s)
HEARTBEAT_INTERVAL_SEC = float(_env("DOG_DOOR_HEARTBEAT_SEC", "25") or "25")

# Web / auth
HTTP_HOST = _env("DOG_DOOR_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(_env("DOG_DOOR_HTTP_PORT", "8080") or "8080")
WEB_PASSWORD = _env("DOG_DOOR_PASSWORD", "changeme") or "changeme"

# SQLite path (under pi/ by default)
DATA_DIR = Path(_env("DOG_DOOR_DATA_DIR", str(Path(__file__).resolve().parent.parent / "data")))
DB_PATH = DATA_DIR / "dog_door.db"
