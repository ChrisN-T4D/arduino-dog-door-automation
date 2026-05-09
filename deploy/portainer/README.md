# Home Assistant stack (Portainer)

Use this folder as a **Git-type stack** in Portainer (point the stack at this repo path) **or** paste `docker-compose.yml` into a **Web editor** stack.

## Prerequisites

1. **Host paths**: Set **`HA_CONFIG_PATH`** in the stack environment to an absolute path on the server (example: `/srv/homeassistant`). If unset, Compose uses the named volume **`ha_config`** (data lives in Docker’s volume store).
2. **Traefik**: Create the external network once if it does not exist: **`docker network create traefik_proxy`**. Set **`TRAEFIK_DOMAIN_SUFFIX`** to your public DNS zone (the part after the hostname, e.g. `example.com`). The router hostname is **`${HA_SUBDOMAIN:-ha}.${TRAEFIK_DOMAIN_SUFFIX}`** (default subdomain `ha`). Match **`TRAEFIK_CERT_RESOLVER`** to your ACME resolver name (default **`le`**). Home Assistant’s **`configuration.yaml`** should set **`http`** external URL to the same HTTPS URL Traefik serves.
3. **Frigate (optional)**: Set **`COMPOSE_PROFILES=frigate`**. Beside `docker-compose.yml`, create **`frigate/`** with **`config.yml`** (copy **`frigate/config.yml.example`** → **`config.yml`**, fill RTSP URLs for Eufy or other NAS/RTSP cameras; track **`dog`** and **`person`**). **`config.yml`** enables MQTT to **`mosquitto`** (`topic_prefix: frigate`) so the **HACS Frigate** integration can subscribe to entity topics. Restart the stack profile. HA → **Devices & Services** → **Frigate** integration → **`http://frigate:5000`** (Docker DNS hostname on **`dog_door_ha`**). Complete step **5** so HA’s **MQTT** integration uses the same broker.

4. **Dog door HA logic (schedules + mmWave + Frigate)**: Copy **`homeassistant-packages/dog_door_frigate_schedules.yaml`** into your HA **`/config/packages/`** (see **`HA_CONFIG_PATH`** on the Docker host). Add **`packages: !include_dir_merge_named packages/`** under **`homeassistant:`** in **`configuration.yaml`**. Helpers include two **Schedule** entities: blocks automations entirely vs “open without needing Frigate dog.” See **`homeassistant-packages/README.md`**.
5. **MQTT**: Home Assistant → **Devices & Services → MQTT**. Broker **`mosquitto`**, port **1883**. Tighten **`mosquitto.conf`** before exposing the broker.

WebSockets for the HA UI should work through Traefik’s HTTP router to port **8123**.

### Reverse proxy error in logs (`trusted_proxies`)

If you see:

`A request from a reverse proxy was received from …, but your HTTP integration is not set-up for reverse proxies`

Traefik (e.g. **`172.21.0.x`**) is forwarding `X-Forwarded-For` while Home Assistant still distrusts those headers. Add an **`http:`** block to **`configuration.yaml`** in your HA config directory (the host path behind **`HA_CONFIG_PATH`**).

Copy from **[`homeassistant-http-reverse-proxy-snippet.yaml`](homeassistant-http-reverse-proxy-snippet.yaml)** (merge with any existing **`http:`** keys—do not duplicate the top-level **`http:`** key). The **`172.16.0.0/12`** range covers common Docker bridge subnets including **`172.21.0.3`**.

Then **restart** Home Assistant. See [Home Assistant HTTP / reverse proxies](https://www.home-assistant.io/integrations/http/#reverse-proxies).

**Automated merge (recommended):** on the Docker host, from the folder that contains **`docker-compose.yml`**, run (uses the **`yq`** helper in the compose file — same **`HA_CONFIG_PATH`** volume as HA):

```bash
docker compose --profile fix-proxy-config run --rm ha-proxy-merge
```

Then **restart** the **`homeassistant`** container.

In **Portainer:** duplicate stack → **Replicas / editor** isn’t ideal for one-shots; use the host SSH shell above, or **Add container** → image **`mikefarah/yq:4`** → bind the same **`/config`** volume → **Console** → run the two `yq -i …` commands from [`homeassistant-http-reverse-proxy-snippet.yaml`](homeassistant-http-reverse-proxy-snippet.yaml) against **`/config/configuration.yaml`**.

The **`rich` … `SyntaxWarning`** line in logs is a Python 3.14 dependency warning and is unrelated to Traefik.

## Dog door automation

From HA, trigger the ESP **`button`** “Dog door open” (**`dog_door_uno_bridge.yaml`**) or Pi **`POST /action/open`** (see **`pi/README.md`**). Do not expose unconditional open URLs on the public internet.

**Stacks without a Git checkout:** Compose bind **`./frigate:/config`** resolves next to **`docker-compose.yml`**. Ensure that folder exists on the host Portainer mounts for the stack path.
