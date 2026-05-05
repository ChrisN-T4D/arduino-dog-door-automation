# Home Assistant stack (Portainer)

Use this folder as a **Git-type stack** in Portainer (point the stack at this repo path) **or** paste `docker-compose.yml` into a **Web editor** stack.

## Prerequisites

1. **Host paths**: Set **`HA_CONFIG_PATH`** in the stack environment to an absolute path on the server (example: `/srv/homeassistant`). If unset, Compose uses the named volume **`ha_config`** (data lives in Docker’s volume store).
2. **Traefik**: Create the external network once if it does not exist: **`docker network create traefik_proxy`**. Set **`TRAEFIK_DOMAIN_SUFFIX`** to your public DNS zone (the part after the hostname, e.g. `example.com`). The router hostname is **`${HA_SUBDOMAIN:-ha}.${TRAEFIK_DOMAIN_SUFFIX}`** (default subdomain `ha`). Match **`TRAEFIK_CERT_RESOLVER`** to your ACME resolver name (default **`le`**). Home Assistant’s **`configuration.yaml`** should set **`http`** external URL to the same HTTPS URL Traefik serves.
3. **Eufy (optional)**: Set stack env **`COMPOSE_PROFILES=eufy`**, then **`EUFY_USERNAME`** and **`EUFY_PASSWORD`**. The **`eufy-security-ws`** service listens on port **3000** inside **`dog_door_ha`**. Without the profile, Home Assistant and Mosquitto still start. For local station discovery, upstream docs prefer **`network_mode: host`** on that image; many installs still work via cloud—see [eufy-security-ws Docker](https://github.com/bropat/eufy-security-ws/blob/master/docs/docker.md).
4. **MQTT**: Home Assistant → **Settings → Devices & Services → Add integration → MQTT**. Broker: **`mosquitto`**, port **1883**, no TLS (internal network only). Tighten **`mosquitto.conf`** before exposing the broker.

WebSockets for the HA UI should work through Traefik’s HTTP router to port **8123**.

## Eufy in Home Assistant

Install **[HACS](https://hacs.xyz/)**, then the **[Eufy Security](https://github.com/fuatakgun/eufy_security)** integration. Configure the WebSocket URL to reach **`eufy-security-ws:3000`** from the HA container (Docker DNS hostname **`eufy-security-ws`**).

## Dog door automation

From HA, call your Raspberry Pi (or other bridge) with **`rest_command`** → **`POST https://…/action/open`** with HTTP Basic auth, **or** drive an ESPHome **`uart.write`** if you use the Pi-less path. Do not expose **`/action/open`** on the public internet.
