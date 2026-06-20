# Bridge Setup

The bridge is a small Python server that runs near Home Assistant. It keeps your Home Assistant token on your computer and gives the Flipper a compact URL it can call through the FlipperHTTP WiFi dev board.

## 1. Create a Home Assistant token

1. Open Home Assistant in a browser.
2. Click your profile picture or username.
3. Scroll to Long-lived access tokens.
4. Create a token named `FlipperHA`.
5. Copy it once. Home Assistant will not show it again.

## 2. Configure `.env`

Copy `.env.example` to `.env` in the repo root:

```powershell
Copy-Item .env.example .env
```

Edit `.env`:

```text
HA_BASE_URL=http://homeassistant.local:8123
HA_TOKEN=your-long-lived-access-token
FLIPPERHA_BRIDGE_KEY=make-up-a-long-random-shared-key
```

`HA_BASE_URL` can also be an IP address, for example `http://192.168.1.25:8123`.

## 3. Run the bridge on Windows

From the repo root:

```powershell
python bridge\ha_state_bridge.py --env .env --host 127.0.0.1 --port 8765
```

Expected startup output:

```text
serving=http://127.0.0.1:8765/v1/states?k=<redacted>
ha_url=http://homeassistant.local:8123
command_log=...\data\state\flipperha_bridge_commands.jsonl
```

Check local bridge health:

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8765/healthz
```

Check Home Assistant auth and connectivity:

```powershell
$key = (Select-String .env -Pattern '^FLIPPERHA_BRIDGE_KEY=(.+)$').Matches.Groups[1].Value
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/setup/check?k=$key"
```

Expected response:

```text
ok | ha=homeassistant.local:8123 | states=123 | builtins=0
```

Optional catalog check for Thermostat onboarding:

```powershell
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/catalog/climate/0?k=$key"
```

A working Home Assistant instance with at least one `climate.*` entity returns a compact list the Flipper can browse.

## 4. Run the bridge on macOS or Linux

From the repo root:

```bash
cp .env.example .env
python3 bridge/ha_state_bridge.py --env .env --host 127.0.0.1 --port 8765
```

Health check:

```bash
curl 'http://127.0.0.1:8765/healthz'
```

Setup check:

```bash
KEY=$(grep '^FLIPPERHA_BRIDGE_KEY=' .env | cut -d= -f2-)
curl "http://127.0.0.1:8765/v1/setup/check?k=$KEY"
```

## 5. Make the bridge reachable from the Flipper

The FlipperHTTP WiFi dev board needs to reach the bridge URL configured in `bridge.cfg`. See `docs/app-config.md` for the SD-card file location.

### Option A: Cloudflare Tunnel

Use this when your Flipper and computer are not on the same simple network, or when HTTPS is required.

```powershell
cloudflared tunnel --url http://127.0.0.1:8765
```

Cloudflare prints a temporary `https://...trycloudflare.com` URL. Put that URL plus `/v1` in `bridge.cfg`:

```text
url=https://your-tunnel.trycloudflare.com/v1
key=same-value-as-FLIPPERHA_BRIDGE_KEY
```

### Option B: Local Network URL

Use this when the FlipperHTTP dev board can reach your computer directly on your LAN.

Run the bridge on all interfaces:

```powershell
python bridge\ha_state_bridge.py --env .env --host 0.0.0.0 --port 8765
```

Find your computer's LAN IP, then configure `bridge.cfg`:

```text
url=http://192.168.1.50:8765/v1
key=same-value-as-FLIPPERHA_BRIDGE_KEY
```

Do not expose this local HTTP URL to the internet.

## Troubleshooting

- `Setup needed`: copy `examples/bridge.example.cfg` to `/ext/apps_data/flipperha/bridge.cfg`, then set `url` and `key`.
- `403 forbidden`: the Flipper key and `FLIPPERHA_BRIDGE_KEY` do not match.
- `ha http 401`: the Home Assistant token is missing, expired, or copied incorrectly.
- `ha error URLError`: the bridge computer cannot reach `HA_BASE_URL`.
- `not found`: check that the Flipper URL ends in `/v1` or that the app build uses the current path normalization.
- Thermostat list is empty: confirm Home Assistant has a `climate.*` entity and the bridge catalog check above returns it.
- Flipper shows `no board`: the FlipperHTTP dev board is not connected or the UART session is busy.

For a fuller symptom guide, see [Troubleshooting](troubleshooting.md).
