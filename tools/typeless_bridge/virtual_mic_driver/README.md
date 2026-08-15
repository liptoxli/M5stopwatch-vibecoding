# M5 StopWatch Mic Core Audio driver

This directory builds the virtual microphone installed by StopWatch BLE Bridge.
It exposes a visible input-only device named `M5 StopWatch Mic` and a hidden
output-only mirror used by the Bridge.

The driver is a customized build of
[ExistentialAudio/BlackHole](https://github.com/ExistentialAudio/BlackHole) at
commit `ffcb74433fbcf8c8ca5c736677c1a4864384dc09`. BlackHole and this customized
driver component are GPL-3.0 licensed. They are kept separate from the MIT
licensed StopWatch firmware and Bridge application. See `THIRD_PARTY_NOTICES.md`.

Build locally:

```bash
DRIVER_CODESIGN_IDENTITY="M5StopWatch Local Code Signing" \
  tools/typeless_bridge/virtual_mic_driver/build_driver.sh
```

Installing a Core Audio HAL driver requires administrator authorization. The
product `.pkg` performs that installation and restarts Core Audio.
