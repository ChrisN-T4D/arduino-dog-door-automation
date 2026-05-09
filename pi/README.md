# Dog door — Raspberry Pi companion

Python **FastAPI** app that:

- Opens **USB serial** to the Arduino Uno (`/dev/ttyACM0` or `/dev/ttyUSB0`).
- Sends **`CMD_OPEN`** / **`CMD_CLOSE`** when you use the web UI or when **Home Assistant** calls **`POST /action/open`** (e.g. `rest_command`).
- Serves a small **web UI** (HTTP Basic auth): door status, remote open/close, optional SQLite **schedule rules** (reference for migration to Home Assistant).
- Stores rules in **SQLite** under `pi/data/dog_door.db`.

The Uno firmware no longer uses RFID or Pi heartbeats: the door opens **only** on explicit **`CMD_OPEN`**. Policy (time windows, mmWave, Frigate dog/person) belongs in **Home Assistant**.

**Pi setup** below includes **Arduino CLI** so you can **compile and upload** `dog_door.ino` from the Pi over USB (e.g. after `git pull`) without a separate PC.

---

## 1. Flash Raspberry Pi OS onto the SD card (PC)

1. Install **Raspberry Pi Imager**: https://www.raspberrypi.com/software/
2. Insert the **microSD** (8 GB+).
3. Choose **Raspberry Pi OS (64-bit)** or **Lite** (Lite is enough if you use SSH only).
4. Click the **gear icon** (OS customization):
   - Enable **SSH** (password or keys).
   - Set **username / password** (e.g. `pi` or your choice).
   - Optionally set **Wi‑Fi** SSID/password so the Pi joins your LAN on first boot.
   - Set **locale** and **timezone** if you care about schedule times.
5. Write the image, then **eject** the card safely.

---

## 2. First boot and SSH

1. Put the SD card in the **Pi 3B**, connect **Ethernet or Wi‑Fi**, and **5 V power**.
2. Find the Pi’s IP (router DHCP list, or `raspberrypi.local` via mDNS).
3. From your PC:  
   `ssh pi@<IP>`  
   (use the user you configured in Imager.)

---

## 3. System packages, serial access, and Arduino CLI

### 3.1 Base packages

```bash
sudo apt update
sudo apt install -y python3-pip python3-venv git curl
```

### 3.2 Serial permissions (`dialout`)

You need this for **both** the Python app and **arduino-cli** talking to the Uno without `sudo`:

```bash
sudo usermod -aG dialout $USER
```

Log out and SSH back in so the group applies.

### 3.3 Arduino CLI (compile / upload Uno firmware on the Pi)

Install the official CLI into **`~/bin`** and register the **AVR** core for **Arduino Uno**:

```bash
mkdir -p ~/bin
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/bin sh
grep -q 'export PATH="$HOME/bin:$PATH"' ~/.bashrc || echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc
export PATH="$HOME/bin:$PATH"
arduino-cli version
arduino-cli core update-index
arduino-cli core install arduino:avr
```

Upload **`dog_door.ino`** after the repo is on the Pi (Uno on USB, usually **`/dev/ttyACM0`**):

```bash
cd ~/arduino-dog-door-automation/dog_door
arduino-cli compile -b arduino:avr:uno .
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno .
```

Docs: [Arduino CLI installation](https://arduino.github.io/arduino-cli/latest/installation/).

---

## 4. Copy this project onto the Pi

**Option A — Git**

```bash
cd ~
git clone <your-repo-url> arduino-dog-door-automation
```

**Option B — SCP / USB stick**

Copy the `arduino-dog-door-automation` folder so you have:

`/home/pi/arduino-dog-door-automation/pi/`

---

## 5. Python virtualenv and dependencies

```bash
cd ~/arduino-dog-door-automation/pi
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

Requires **Python 3.10+** (current Raspberry Pi OS meets this).

---

## 6. Environment variables

```bash
cp env.example .env
nano .env   # edit password and serial device
```

Important:

- **`DOG_DOOR_PASSWORD`** — web login password (HTTP Basic).
- **`DOG_DOOR_SERIAL`** — usually **`/dev/ttyACM0`** for Arduino Uno over USB; some adapters show **`/dev/ttyUSB0`**. Run `ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null` with the Uno plugged in.
- **systemd `EnvironmentFile=`** only accepts lines like **`KEY=value`**. Do **not** use **`export KEY=value`** in `.env` or systemd will ignore every line and the app will exit with “Set `DOG_DOOR_PASSWORD`”.

Load before starting (manual run; `set -a` exports variables for the child process):

```bash
set -a && source .env && set +a
```

---

## 7. Plug in the Arduino

- **USB A** (Pi) → **USB B** (Uno).
- Uno can keep **12 V** on **Vin** / barrel as in your bench setup.

Find the device node:

```bash
ls -l /dev/ttyACM0
```

If permission denied, confirm **dialout** (step 3).

---

## 8. Run the server (manual test)

From `pi/` with venv activated and `.env` sourced:

```bash
uvicorn dog_door_pi.main:app --host 0.0.0.0 --port 8080
```

Open a browser: **`http://<pi-ip>:8080`** — the browser will ask for **HTTP Basic** login:

- **Username:** anything (e.g. `pi`)
- **Password:** value of **`DOG_DOOR_PASSWORD`**

Use **Open / Close** and confirm the Uno serial protocol (`ACK:CMD_OPEN`, `STATE:*`). Add **schedule rules** only if you still use this DB as a reference.

Stop with **Ctrl+C**.

---

## 9. systemd service (auto-start on boot)

Paths below assume user **`pi`** and project under **`/home/pi/arduino-dog-door-automation`**. Adjust if yours differs.

1. Create **`/home/pi/arduino-dog-door-automation/pi/.env`** (copy from `env.example`, real secrets).

2. Copy the unit file (edit paths inside if needed):

```bash
sudo cp ~/arduino-dog-door-automation/pi/deploy/dog-door.service /etc/systemd/system/dog-door.service
sudo nano /etc/systemd/system/dog-door.service
```

Ensure **`User=`**, **`WorkingDirectory=`**, **`EnvironmentFile=`**, and **`ExecStart=`** match your machine.

3. Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable dog-door
sudo systemctl start dog-door
sudo systemctl status dog-door
```

Logs:

```bash
journalctl -u dog-door -f
```

---

## Schedule logic (short)

SQLite rules drive the **`exit_allowed_schedule`** field in **`GET /api/status`** and the dashboard line “within schedule window”. They **do not** block **`CMD_OPEN`** from the web UI or from Home Assistant — migrate real gating to HA automations.

- **Block** rules: matching windows mark the schedule as “not allowed” in that evaluation.
- **Allow** rules: if **any** allow rule exists, the schedule is “allowed” only inside at least one allow window (after blocks).
- **No allow rules:** after blocks, the schedule evaluates as allowed unless blocked.

Overnight windows are supported (e.g. start **22:00**, end **06:00**).

---

## Security (read this)

- **Set `DOG_DOOR_PASSWORD`** in `pi/.env` to a **strong, unique** value (never commit `.env`). The app **refuses to start** if the password is missing, too short (under 10 characters), or a common default—unless **`DOG_DOOR_DEVELOPMENT=1`** (dev only: relaxes rules, enables `/docs`, can allow empty password with a warning).
- **HTTP Basic** sends the password **base64** (not encrypted). Treat as **LAN-only**; for remote access use **VPN / Tailscale** or **HTTPS** on a reverse proxy, not raw port-forward to **8080** on the public internet.
- The dashboard **“Recent serial lines”** shows Arduino `STATE:` / `BOOT:` lines. Don’t expose the UI beyond people you trust.
- **OpenAPI `/docs`** is **disabled** by default; enable only in dev with **`DOG_DOOR_DEVELOPMENT=1`**.

---

## Troubleshooting

| Issue | What to check |
|--------|----------------|
| App exits: password error | Set **`DOG_DOOR_PASSWORD`** (10+ chars) or use **`DOG_DOOR_DEVELOPMENT=1`** only for local testing. |
| No `/dev/ttyACM0` | Cable, Uno power, `lsusb`, driver on Pi |
| Permission denied on serial | User in **dialout**, re-login |
| Web works but door does not move | Uno powered, motor wiring, **`arduino-cli monitor`** on serial — send **`PING`** → **`PONG`**, **`CMD_OPEN`** when idle → **`ACK:CMD_OPEN`**. |
| HA cannot open door | Pi reachable from HA container (Tailscale/LAN), **`rest_command`** URL and Basic auth correct, **`serial_ok`** true in **`/api/status`**. |

---

## Development on Windows (optional)

From the `pi` folder with Python 3.10+:

```powershell
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
$env:DOG_DOOR_SERIAL="COM3"
$env:DOG_DOOR_PASSWORD="your-dev-password-here"
$env:DOG_DOOR_DEVELOPMENT="1"
uvicorn dog_door_pi.main:app --host 127.0.0.1 --port 8080
```

Use the correct **COM** port for the Uno in Device Manager. **`DOG_DOOR_DEVELOPMENT=1`** relaxes password rules for local testing only.
