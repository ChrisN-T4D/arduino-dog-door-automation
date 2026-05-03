# Arduino dog door automation

|Ecosystem| Path | Purpose|
|-----------|------|--------|
| **Arduino (ELEGOO Uno)** | [`dog_door/`](dog_door/) | Firmware: RFID (RDM6300), BTS7960 motor, optional Pi serial policy. Main sketch: [`dog_door/dog_door.ino`](dog_door/dog_door.ino). |
| **Raspberry Pi 3B** | [`pi/`](pi/) | Python FastAPI app: USB serial to Uno, schedules, web UI. Setup: [`pi/README.md`](pi/README.md). |

## Clone (e.g. on the Raspberry Pi)

This repo syncs to GitHub **`ChrisN-T4D/arduino-dog-door-automation`** (`origin`). Clone with:

```bash
git clone https://github.com/ChrisN-T4D/arduino-dog-door-automation.git
cd arduino-dog-door-automation
```

Then follow **`pi/README.md`** for venv, `.env`, and `uvicorn` / systemd.

## Configuration notes

- Copy **`pi/env.example`** → **`pi/.env`** on the Pi (not committed; listed in `.gitignore`).
- SQLite DB is created under **`pi/data/`** at runtime (ignored by git).
