# Agent instructions

This repository contains an ESP-IDF firmware, a macOS BLE companion, and a Core Audio virtual microphone. Read `docs/AGENT_DEVELOPMENT_GUIDE.md` before changing behavior.

## Working rules

- Preserve the three independent paths: standard BLE HID, Codex Vendor HID, and BLE microphone/Companion.
- Both Classic / Pet and OpenWatcher V2 must use the same A/B voice state machine.
- Design every StopWatch screen for the real 466×466 circular AMOLED. Keep primary confirmation actions near the vertical center, not the clipped corners.
- Do not replace native Codex Micro Agent, Encoder, or Radial events with a virtual mouse or keyboard unless the user explicitly requests it.
- Agent light assignment controls display only. Transport readiness is authoritative for whether an Agent action can be sent.
- A recording interruption is an error for the current utterance. Stop and ask the user to record again; do not silently splice a missing middle section.
- Keep Wi-Fi disabled by default unless a requested feature requires it. Prefer the existing BLE source of truth for live state.
- Do not change the BLE audio format, HID Report Map, partition table, VID/PID, or bond schema as a side effect of a UI task.

## Privacy

- Never commit Wi-Fi credentials, access tokens, account IDs, signing identities, or user-specific absolute paths.
- Keep public `codex_config.h` values sanitized with `example.com` and `YOUR_WIFI_*` placeholders.
- The Bridge may read local Codex authentication for optional quota sync, but must never log or package its content.

## Build and verification

Firmware requires ESP-IDF v5.5.4:

```bash
cd firmware-stopwatch-idf
python3 ./fetch_repos.py
idf.py build
```

Use `idf.py app-flash` only when the installed partition layout is already compatible. Use a full `idf.py flash` after partition or Bootloader changes. Resolve the exact `/dev/cu.usbmodem*` port before flashing.

Build the Bridge with:

```bash
tools/typeless_bridge/build_stopwatch_ble_bridge.sh
```

Before release, run:

```bash
tools/check_version.sh
```

For touch/HID changes, verify short-touch cancellation, one long-press commit, all four Agent slots, reasoning in both directions, Radial neutral-on-release, A/B voice input, BLE reconnect, and virtual microphone health.

## Git safety

- Inspect `git status` before editing and preserve unrelated user changes.
- Stage only named files. Do not use `git add .` or `git add -A`.
- Do not commit, tag, push, or create a release unless the user explicitly asks for that action.
