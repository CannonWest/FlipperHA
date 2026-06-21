# Release Build Notes

This repo keeps generated Flipper artifacts out of git. Build locally, then attach the `.fap` file to a GitHub Release or another release channel.

## Cutting a Release (every version)

Releasing the FAP is part of the dev loop — **every** `fap_version` bump gets a matching GitHub Release with a freshly built FAP. After bumping `fap_version` in `application.fam`, committing, and pushing to `main`:

```powershell
py tools/release.py
```

It builds the FAP from a `git archive` of `HEAD` (not the working tree), so the public download can never pick up a local `flipper_app/src/ha_remote_local.h` and bake in a private bridge URL/key. It tags `v<fap_version>`, attaches the FAP, and auto-generates notes from the commits. Use `--notes-file NOTES.md` for hand-written notes or `--draft` to stage one for review. (Run it with a Python that has `ufbt` — plain `py` works; a Store-aliased Python does not.)

## Local Build

From the repo root:

```powershell
cd flipper_app
py -m ufbt build
```

The build output prints the generated `flipperha.fap` path. Depending on how `ufbt` is launched, it may appear under a local build/state directory such as `dist/`, `build/`, `.ufbt/`, or `.ufbt-state/`.

To install directly while developing:

```powershell
py -m ufbt launch
```

## Release Checklist

- Build from a clean git tree.
- Confirm `flipper_app/application.fam` has the intended `fap_version`.
- Attach only the generated `.fap` and release notes.
- Do not attach `.env`, `bridge.cfg`, `thermostat.cfg`, command logs, or local Home Assistant data.
- Re-run the private-term scan from this repo before publishing.

## Suggested Release Notes Shape

```text
FlipperHA vX.Y

Highlights
- Controller Add flow for switches, lights, dimmers, and routines.
- Thermostat setup for Home Assistant climate entities.
- SD-card bridge config.

Install
1. Copy flipperha.fap to the Flipper apps folder, or install with ufbt.
2. Run the Python bridge from this repo.
3. Copy bridge.cfg to /ext/apps_data/flipperha/bridge.cfg.
```

## GitHub Actions Notes

A future workflow can build the FAP and upload it as an artifact, but keep the first workflow intentionally small:

- Check out the repo.
- Install Python and uFBT.
- Run `ufbt build` in `flipper_app/`.
- Upload the generated `flipperha.fap`.

Keep secrets out of CI. The app should build with placeholder bridge defaults and read real bridge settings from SD-card config at runtime.
