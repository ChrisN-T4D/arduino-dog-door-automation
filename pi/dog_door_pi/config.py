"""Environment-driven configuration."""

import os
import sys
from pathlib import Path


def _env(key: str, default: str | None = None) -> str | None:
    v = os.environ.get(key)
    if v is None or v.strip() == "":
        return default
    return v.strip()


# Serial device (Pi): often /dev/ttyACM0 (Uno) or /dev/ttyUSB0
SERIAL_PORT = _env("DOG_DOOR_SERIAL", "/dev/ttyACM0")
SERIAL_BAUD = int(_env("DOG_DOOR_BAUD", "9600") or "9600")

# Web / auth — no default password; see validate_web_config()
HTTP_HOST = _env("DOG_DOOR_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(_env("DOG_DOOR_HTTP_PORT", "8080") or "8080")
WEB_PASSWORD = _env("DOG_DOOR_PASSWORD") or ""

# Set DOG_DOOR_DEVELOPMENT=1 only on trusted dev machines (enables /docs, relaxes password rules).
DEVELOPMENT = _env("DOG_DOOR_DEVELOPMENT", "0") == "1"

# SQLite path (under pi/ by default)
DATA_DIR = Path(_env("DOG_DOOR_DATA_DIR", str(Path(__file__).resolve().parent.parent / "data")))
DB_PATH = DATA_DIR / "dog_door.db"

_WEAK_PASSWORDS = frozenset(
    {
        "changeme",
        "password",
        "password123",
        "12345678",
        "admin",
        "dogdoor",
    }
)


def validate_web_config() -> None:
    """Refuse to run with missing or trivial web passwords (unless DEVELOPMENT)."""
    pw = WEB_PASSWORD.strip()
    if not pw:
        if DEVELOPMENT:
            print(
                "WARNING: DOG_DOOR_PASSWORD is empty; web UI is wide open. "
                "Set DOG_DOOR_PASSWORD before production.",
                file=sys.stderr,
            )
            return
        print(
            "ERROR: Set DOG_DOOR_PASSWORD (HTTP Basic password for the web UI). "
            "See pi/env.example.",
            file=sys.stderr,
        )
        sys.exit(1)
    if DEVELOPMENT:
        return
    if pw.lower() in _WEAK_PASSWORDS:
        print(
            "ERROR: DOG_DOOR_PASSWORD is a known weak/default string. Choose a unique password.",
            file=sys.stderr,
        )
        sys.exit(1)
    if len(pw) < 10:
        print(
            "ERROR: DOG_DOOR_PASSWORD must be at least 10 characters "
            "(or set DOG_DOOR_DEVELOPMENT=1 for short passwords — dev only).",
            file=sys.stderr,
        )
        sys.exit(1)
