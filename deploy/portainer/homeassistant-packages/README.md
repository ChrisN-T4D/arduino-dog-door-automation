# Home Assistant packages (dog door)

## `dog_door_frigate_schedules.yaml`

Implements:

- **mmWave** sustained motion (~2 s) → optional **Frigate dog** occupancy → calls the ESP bridge **open** script.
- **Schedule** helper `Dog door — allow auto-open without dog`: when active, **Frigate dog is not required** (still respects block schedule + optional person guard + cooldown).
- **Schedule** helper `Dog door — block automatic opens only`: when active, this **automation does not fire** — manual **`button.press` / dashboard tiles are unchanged.**

### Install

1. On the HA host create `packages` inside your HA config directory (often `/config/packages`).
2. Copy `dog_door_frigate_schedules.yaml` into that folder.
3. In `configuration.yaml` ensure packages are loaded:

   ```yaml
   homeassistant:
     packages: !include_dir_named packages/
   ```

4. Restart Home Assistant.
5. In **Settings → Devices & services → Helpers**, open both **Schedule** helpers and add recurring time slots (**allow without dog** = optional Frigate; **block automatic** = this automation cannot open the door — manual **`button.press`** still works).
6. **Developer tools → States** — search `mmwave` and `dog_door_cam` — set:
   - `binary_sensor.` line tagged **CHANGEME** for mmWave motion.
   - `button.` in script `dog_door_press_bridge_open_button` if your bridge entity differs.
   - If your Frigate camera name isn’t `dog_door_cam`, rename the camera in `frigate/config.yml` or edit the two `binary_sensor.dog_door_cam_*` lines in YAML.

### Frigate

Use the Compose profile **`frigate`**, RTSP URLs in `deploy/portainer/frigate/config.yml`.  
Home Assistant integration URL: **`http://frigate:5000`** (same Docker stack network as HA).