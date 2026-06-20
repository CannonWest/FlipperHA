# WiFi Dev Board Notes

The app uses the official Flipper Zero WiFi Developer Board with FlipperHTTP as the network path between the native FAP and the local Home Assistant bridge.

## Transport Shape

```text
Flipper app -> UART -> WiFi Dev Board running FlipperHTTP -> bridge /v1 endpoints -> Home Assistant
```

The bridge keeps the Home Assistant bearer token off the Flipper. The Flipper only stores a bridge URL and a shared bridge key in `/ext/apps_data/flipperha/bridge.cfg`.

## FlipperHTTP Notes

- Serial commands are sent over the board UART at 115200 baud.
- Commands should end with CRLF.
- The app pings the board before bridge GET requests.
- Long Home Assistant bearer-token requests should not be sent from the Flipper. Use the bridge endpoints instead.
- If the app shows `no board`, check that the WiFi Developer Board is attached, running FlipperHTTP, and not held open by another serial client.

## Bridge URL Notes

Cloudflare Tunnel is the easiest HTTPS path while testing. Local LAN URLs can also work when the WiFi Developer Board can reach the bridge computer directly.

Example `bridge.cfg` values:

```text
url=https://your-tunnel.example.com/v1
key=same-value-as-FLIPPERHA_BRIDGE_KEY
```

or:

```text
url=http://192.168.1.50:8765/v1
key=same-value-as-FLIPPERHA_BRIDGE_KEY
```

Do not expose a local HTTP bridge directly to the internet. Prefer an access-controlled tunnel or local-only setup.

## Native App Behavior

- Controller state refresh uses `/v1/states` once rows exist.
- Add uses `/v1/catalog/<category>/<offset>` to browse Home Assistant entities.
- Switch and on/off light rows use `/v1/entity/toggle/<entity_id>`.
- Dimmable light rows use `/v1/entity/dim/<entity_id>/<percent>`.
- Routine rows use `/v1/entity/run/<entity_id>`.
- Thermostat starts setup-gated, browses `/v1/catalog/climate/0`, saves the selected `climate.*` entity, refreshes `/v1/thermostat/state/<entity_id>`, and applies changes with `/v1/thermostat/apply/<entity_id>/<target>/<mode>/<fan>`.
