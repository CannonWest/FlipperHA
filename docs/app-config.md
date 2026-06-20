# Flipper App Config

The public app reads bridge connection settings from the Flipper SD card on launch.

## File Location

Copy `examples/bridge.example.cfg` to:

```text
/ext/apps_data/flipperha/bridge.cfg
```

When browsing the SD card over USB or qFlipper, create the folder if it does not exist yet:

```text
apps_data/flipperha/bridge.cfg
```

## File Format

```text
url=https://your-bridge.example.com/v1
key=change-this-bridge-key
# Optional: pre-seed Thermostat instead of choosing it on-device.
# thermostat=climate.your_thermostat
```

- `url` must be reachable by the FlipperHTTP WiFi dev board and should end in `/v1`.
- `key` must match `FLIPPERHA_BRIDGE_KEY` in the bridge `.env` file.
- `thermostat` is optional. If present, it must be a Home Assistant `climate.*` entity.
- Blank lines and lines starting with `#` are ignored.

## On-Device Status

Open Controller, then the Info button on the left rail. Until both values are configured, HA Link shows `Setup needed` and bridge commands return `setup needed` without touching UART.

When config is loaded from SD card, HA Link shows `Cfg:file`. If a developer build uses `flipper_app/src/ha_remote_local.h` instead, it shows `Cfg:build`.

## Thermostat Setup

Open Thermostat. If no thermostat is configured, the app shows `Thermostat Setup`. Press OK to browse Home Assistant `climate.*` entities from the bridge, then select the thermostat to save it on the SD card.

The app writes the selected entity to:

```text
/ext/apps_data/flipperha/thermostat.cfg
```

Manual format:

```text
entity_id=climate.your_thermostat
```

Thermostat refresh also fetches the supported HVAC modes and fan modes from Home Assistant. If a thermostat has no fan mode support, the fan button is shown as unavailable and save commands omit fan changes.

## Developer Fallback

For firmware developers who prefer compile-time config, copy `examples/ha_remote_local.example.h` to `flipper_app/src/ha_remote_local.h` and rebuild. The SD-card `bridge.cfg` takes precedence when present.
