# Migration Notes

These notes are for people moving from an older private or hardcoded build to the public add-your-own setup.

## What Changed

- The public Controller starts blank.
- Built-in private action rows are disabled by default.
- Bridge URL and key are read from `/ext/apps_data/flipperha/bridge.cfg`.
- Thermostat is no longer hardcoded. Choose a `climate.*` entity on-device or seed it in config.
- Controller rows added on-device are stored on the Flipper SD card.

## Files To Back Up

Before replacing an older build, back up these files from the Flipper SD card if they exist:

```text
/ext/apps_data/flipperha/bridge.cfg
/ext/apps_data/flipperha/thermostat.cfg
/ext/apps_data/flipperha/controller_entries.bin
/ext/apps_data/flipperha/controller_order.bin
```

## Moving From Compile-Time Config

Older developer builds may have used `flipper_app/src/ha_remote_local.h` for bridge config.

For public-style setup, move those values into SD-card config:

```text
url=https://your-bridge.example.com/v1
key=your-bridge-key
```

Then remove `ha_remote_local.h` or leave it as a fallback only. SD-card config wins when present.

## Moving Controller Rows

There is no hand-written migration format yet for `controller_entries.bin`. The safest public path is:

1. Install the public build.
2. Configure `bridge.cfg`.
3. Open Controller -> Add.
4. Add switches, lights, dimmers, and routines from the live Home Assistant catalog.
5. Use Reorder to arrange rows.

## Moving Thermostat

Open Thermostat and press OK on the setup screen. Select the Home Assistant `climate.*` entity you want to control.

You can also pre-seed it in either config file:

```text
thermostat=climate.your_thermostat
```

or:

```text
entity_id=climate.your_thermostat
```

## Legacy BLE Fallback

The public path uses the WiFi Developer Board and bridge. BLE action dispatch is retained only as an advanced fallback. If you enable built-in action examples at compile time, use `home-assistant/flipperha-dispatch.yaml` as a generic starting point and replace every example entity with your own.
