# Arduino dog door automation

|Ecosystem| Path | Purpose|
|-----------|------|--------|
| **Arduino (ELEGOO Uno)** | [`dog_door/`](dog_door/) | Firmware: BTS7960 motor, USB serial host commands (`CMD_OPEN` / `CMD_CLOSE`). Main sketch: [`dog_door/dog_door.ino`](dog_door/dog_door.ino). |
| **Raspberry Pi (optional)** | [`pi/`](pi/) | FastAPI app: USB serial bridge + web UI + SQLite schedule reference. Setup: [`pi/README.md`](pi/README.md). |
| **ESP32 + LD2410C** | [`esphome/`](esphome/) | ESPHome mmWave presence for Home Assistant. See [`esphome/dog_door_mmwave.yaml`](esphome/dog_door_mmwave.yaml). |
| **Home Assistant (Docker)** | [`deploy/portainer/`](deploy/portainer/) | Example Compose for Portainer: HA, MQTT (optional), Eufy Security WS bridge. |

## Architecture (target)

- **Detection**: LD2410C on ESP32 → Home Assistant (ESPHome or MQTT).
- **Coordination**: Home Assistant automations (schedules, Eufy cameras, cooldowns) call either the Pi **`POST /action/open`** or an ESPHome **`uart.write`** to the Uno (Pi-less path).
- **Actuation**: Uno runs the door motor; opens only on **`CMD_OPEN`** from USB serial (Pi) or future UART from ESP32.

## Clone (e.g. on the Raspberry Pi)

```bash
git clone https://github.com/ChrisN-T4D/arduino-dog-door-automation.git
cd arduino-dog-door-automation
```

Follow **`pi/README.md`** for venv, `.env`, and systemd on the Pi.

## Home Assistant on the server (Portainer / Traefik)

Copy [`deploy/portainer/docker-compose.yml`](deploy/portainer/docker-compose.yml) into a Portainer stack and set variables from [`deploy/portainer/.env.example`](deploy/portainer/.env.example). See [`deploy/portainer/README.md`](deploy/portainer/README.md) for Traefik labels, MQTT, and the [Eufy Security](https://github.com/fuatakgun/eufy_security) HACS integration pointing at the **`eufy-security-ws`** container.

## Configuration notes

- Copy **`pi/env.example`** → **`pi/.env`** on the Pi (not committed). Use a **strong `DOG_DOOR_PASSWORD`**; the Pi app will not start with a missing or trivial password unless **`DOG_DOOR_DEVELOPMENT=1`** (dev only). See **`pi/README.md` → Security**.
- SQLite DB is created under **`pi/data/`** at runtime (ignored by git).
- **Do not** expose the Pi web UI or HA door actions to the public internet without **VPN/HTTPS**; they can open the door.
