# Arduino dog door automation

|Ecosystem| Path | Purpose|
|-----------|------|--------|
| **Arduino (ELEGOO Uno)** | [`dog_door/`](dog_door/) | Firmware: BTS7960 motor; USB/UART lines: `CMD_OPEN` / `CMD_CLOSE` / `PING` / `GET_STATE` / `SET_*_MS=` (timings in EEPROM). Sketch: [`dog_door/dog_door.ino`](dog_door/dog_door.ino). |
| **Raspberry Pi (optional)** | [`pi/`](pi/) | FastAPI app: USB serial bridge + web UI + SQLite schedule reference. Setup: [`pi/README.md`](pi/README.md). |
| **ESP32 + LD2410C** | [`esphome/`](esphome/) | ESPHome mmWave (distance, gate tuning, engineering mode) + [`esphome/dog_door_uno_bridge.yaml`](esphome/dog_door_uno_bridge.yaml) for Uno UART. |
| **Home Assistant (Docker)** | [`deploy/portainer/`](deploy/portainer/) | Example Compose for Portainer: HA, MQTT (optional), Frigate (RTSP dog/person detection). See [`deploy/portainer/homeassistant-packages/`](deploy/portainer/homeassistant-packages/). |

## Architecture (target)

- **Detection**: LD2410C on ESP32 → Home Assistant (ESPHome or MQTT).
- **Coordination**: Home Assistant automations (schedules, Frigate RTSP/NAS streams, cooldowns, mmWave) trigger the ESP bridge **`button`** or Pi **`POST /action/open`**.
- **Actuation**: Uno runs the door motor; opens only on **`CMD_OPEN`** from USB serial (Pi) or UART from the ESP bridge (`dog_door_uno_bridge.yaml`).

## Clone (e.g. on the Raspberry Pi)

```bash
git clone https://github.com/ChrisN-T4D/arduino-dog-door-automation.git
cd arduino-dog-door-automation
```

Follow **`pi/README.md`** for venv, `.env`, and systemd on the Pi.

## Home Assistant on the server (Portainer / Traefik)

Copy [`deploy/portainer/docker-compose.yml`](deploy/portainer/docker-compose.yml) into a Portainer stack and set variables from [`deploy/portainer/.env.example`](deploy/portainer/.env.example). Enable optional Frigate (`COMPOSE_PROFILES=frigate`), add RTSP in [`deploy/portainer/frigate/config.yml.example`](deploy/portainer/frigate/config.yml.example), and optionally install HA package **[`deploy/portainer/homeassistant-packages/dog_door_frigate_schedules.yaml`](deploy/portainer/homeassistant-packages/dog_door_frigate_schedules.yaml)**. Full detail: **[`deploy/portainer/README.md`](deploy/portainer/README.md)** and **[`deploy/portainer/homeassistant-packages/README.md`](deploy/portainer/homeassistant-packages/README.md)**.

## Configuration notes

- Copy **`pi/env.example`** → **`pi/.env`** on the Pi (not committed). Use a **strong `DOG_DOOR_PASSWORD`**; the Pi app will not start with a missing or trivial password unless **`DOG_DOOR_DEVELOPMENT=1`** (dev only). See **`pi/README.md` → Security**.
- SQLite DB is created under **`pi/data/`** at runtime (ignored by git).
- **Do not** expose the Pi web UI or HA door actions to the public internet without **VPN/HTTPS**; they can open the door.
