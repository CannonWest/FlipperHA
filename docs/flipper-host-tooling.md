# Flipper Host Tooling And Observability

These are public-safe notes for observing the Flipper while developing the FlipperHA app on a desktop machine.

## qFlipper Logs

qFlipper writes timestamped desktop logs under the current user's local app data directory:

```text
%LOCALAPPDATA%\qFlipper\qFlipper-<YYYYMMDD-HHMMSS>.txt
```

These logs are useful for confirming firmware updates, file transfers, and USB serial/RPC activity. They do not capture Home Assistant events or Flipper app runtime logs.

## Serial And CLI Access

- The Flipper enumerates as a USB serial COM port on Windows.
- qFlipper's built-in CLI or any serial terminal can connect to the port.
- Use the Flipper CLI `log` command for device-side runtime logs.
- Close qFlipper before using browser Web Serial tools such as Flipper Lab; only one client can hold the COM port.

## Useful Checks

- Confirm the FAP was copied to the device or launched by `ufbt`.
- Confirm `/ext/apps_data/flipperha/bridge.cfg` exists on the SD card.
- Confirm the WiFi Developer Board is attached and FlipperHTTP responds to `[PING]`.
- Pair device logs with bridge console output when diagnosing `no board`, `get timeout`, or `403 forbidden`.
