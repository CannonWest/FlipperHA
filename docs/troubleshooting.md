# Troubleshooting

Start with the smallest path that should work, then move outward: Home Assistant from the bridge computer, bridge from the bridge computer, bridge from the WiFi Developer Board, then the Flipper app.

## Fast Checks

From the bridge computer:

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8765/healthz
$key = (Select-String .env -Pattern '^FLIPPERHA_BRIDGE_KEY=(.+)$').Matches.Groups[1].Value
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/setup/check?k=$key"
```

From the Flipper app:

- Open Controller -> Info.
- Confirm HA Link is not `Setup needed`.
- Confirm `Cfg:file` if you are using SD-card config.
- Try Refresh after adding at least one Controller row.

## Flipper Shows `Setup needed`

The app did not load a usable bridge URL and key.

Check:

- `bridge.cfg` exists at `/ext/apps_data/flipperha/bridge.cfg`.
- The file has `url=` and `key=` lines.
- `url=` ends in `/v1`.
- `key=` matches `FLIPPERHA_BRIDGE_KEY` in `.env`.
- Restart the app after changing SD-card config.

## Flipper Shows `no board`

The app could not talk to FlipperHTTP over the WiFi Developer Board UART.

Check:

- The WiFi Developer Board is attached firmly.
- The board is running FlipperHTTP.
- qFlipper, Flipper Lab, or another serial tool is not holding the COM port.
- Reboot the WiFi Developer Board and reopen the app.

## Flipper Shows `get timeout`

FlipperHTTP accepted the request path, but the WiFi Developer Board did not return a completed HTTP response.

Common causes:

- The board cannot reach the bridge URL.
- The bridge is bound to `127.0.0.1` while the board is trying to reach it over the LAN.
- Windows Firewall is blocking the bridge port.
- A Cloudflare Tunnel URL changed after restarting `cloudflared`.
- `bridge.cfg` has the wrong scheme, host, port, or missing `/v1`.

For LAN testing, run the bridge on all interfaces:

```powershell
python bridge\ha_state_bridge.py --env .env --host 0.0.0.0 --port 8765
```

Then use a LAN URL in `bridge.cfg`, for example:

```text
url=http://192.168.1.50:8765/v1
key=same-value-as-FLIPPERHA_BRIDGE_KEY
```

## Flipper Shows `403 forbidden`

The bridge is reachable, but the key is wrong.

Check:

- `.env` has the intended `FLIPPERHA_BRIDGE_KEY`.
- `/ext/apps_data/flipperha/bridge.cfg` has the same value in `key=`.
- There are no extra spaces before or after the key.

## Flipper Shows `ha http 401`

Home Assistant rejected the bridge token.

Check:

- `HA_TOKEN` is a long-lived access token.
- The token was copied fully.
- The token was not deleted from Home Assistant.
- The bridge process was restarted after editing `.env`.

## Flipper Shows `ha error URLError`

The bridge could not reach Home Assistant.

Check:

- `HA_BASE_URL` is reachable from the bridge computer.
- The URL includes the scheme, such as `http://` or `https://`.
- Home Assistant is online.
- DNS names like `homeassistant.local` resolve on the bridge computer. If not, use the Home Assistant IP address.

## Flipper Shows `not found`

Usually the app and bridge URL shape do not match.

Check:

- `bridge.cfg` should point to the bridge base ending in `/v1`, not to a specific endpoint.
- Example: `url=https://your-tunnel.example.com/v1`.
- Rebuild or reinstall the current app if you are testing an older FAP.

## Add Menu Is Empty

Check the bridge catalog directly:

```powershell
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/catalog/switches/0?k=$key"
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/catalog/lights/0?k=$key"
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/catalog/routines/0?k=$key"
```

If these are empty, Home Assistant may not have entities in those domains, or the token may not have access to them.

## Thermostat List Is Empty

Check the climate catalog:

```powershell
Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8765/v1/catalog/climate/0?k=$key"
```

The Thermostat setup screen only lists Home Assistant entities whose IDs start with `climate.`.

## Cloudflare Tunnel Works Locally But Not On Flipper

Check:

- The tunnel process is still running.
- The exact printed HTTPS URL is in `bridge.cfg`.
- The URL ends in `/v1`.
- The key is copied exactly.
- Any Cloudflare Access policy allows the WiFi Developer Board request path.

For a temporary tunnel, restarting `cloudflared` usually creates a new URL. Update `bridge.cfg` after every restart.

## Where To Look For Logs

- Bridge console: startup, Home Assistant errors, and command results.
- `data/state/flipperha_bridge_commands.jsonl`: bridge command log, if enabled.
- Flipper CLI `log`: native app logs while the app is running.
- qFlipper desktop logs: file transfer, firmware, USB, and RPC context.
