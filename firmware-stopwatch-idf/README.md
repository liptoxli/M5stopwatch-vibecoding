# M5 StopWatch Vibe Coding Firmware

ESP-IDF firmware for the M5Stack StopWatch. It extends the upstream UserDemo with the Codex status UI, two UI themes, BLE HID input, real-time BLE microphone, Codex Micro compatible Agent/Encoder/Radial controls, quota/activity panels and power management.

For the module map, touch parameters, BLE protocols and safe secondary-development workflow, read [Agent and secondary-development guide](../docs/AGENT_DEVELOPMENT_GUIDE.md).

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

Use `idf.py app-flash` only when the device already has a compatible Bootloader and partition table. Firmware v0.10.0 changes the HID layout; after upgrading from v0.9.x, remove the old `M5Codex-*` entry from macOS Bluetooth settings and pair it again once.
