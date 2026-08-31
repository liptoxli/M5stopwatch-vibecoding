# StopWatch BLE Bridge

StopWatch BLE Bridge is the macOS companion app for the M5 StopWatch Codex firmware.

Current version: **v1.3.6**. Full version history: [CHANGELOG.md](CHANGELOG.md).

It provides local functions including:

- Connect to `M5Codex-*` over Bluetooth.
- Sync configurable key bindings and input mode to firmware.
- Optionally observe Typeless recording/processing state and sync it back to the device.
- Optionally read local Codex auth and push weekly quota snapshots to the device.
- Persist a daily weekly-quota baseline using an 08:00 local-time boundary.
- Sync the local Codex unread-task count and recent four-hour activity summary.
- Stream the StopWatch microphone as 16 kHz IMA-ADPCM and feed a macOS virtual input named `M5 StopWatch Mic`.

Real keyboard input is sent by the device firmware through BLE HID. The bridge app does not inject Typeless, WeChat IME, Enter, or Clear Input keystrokes into macOS. No account credentials are bundled in the app. Codex quota sync is optional and reads the current user's local `~/.codex/auth.json` only when enabled.

## User Setup

1. Flash the M5 StopWatch firmware and enable Bluetooth in device settings.
2. On macOS, pair the device named `M5Codex-XX` in Bluetooth settings. The suffix is stable per device and is derived from the device MAC address.
3. Install the bridge app:

   ```bash
   tools/typeless_bridge/install_launch_agent.sh
   ```

4. Grant Accessibility permission:

   `System Settings -> Privacy & Security -> Accessibility -> StopWatch BLE Bridge`

5. Open the menu bar M5 device icon.
6. Enable `M5 StopWatch Mic` when the StopWatch microphone should become the macOS default input. Disable it to stop the BLE stream and restore the previous input device.
7. Open `Settings...` and choose:

   - Left key: default `F19`.
   - Right key: default `Return`.
   - Right key long press: default `Command+Return` for Codex guide/follow-up insertion.
   - Quota refresh interval: default `300` seconds.
   - Codex quota push: enabled by default, requires local Codex login.
   - Typeless shortcut sync: disabled by default. Enable only when using Typeless and you want the app to write the left key into Typeless local settings.

8. Configure the same left/right shortcuts in your input app if needed. This can be Typeless, Tencent Input, or any other app that accepts normal keyboard shortcuts.

## Virtual microphone

Virtual microphone mode is off by default. When enabled, the Bridge:

1. Selects `M5 StopWatch Mic` as the macOS default input.
2. Subscribes to the firmware's BLE audio service.
3. Keeps the microphone armed while idle and sends `Start` only when voice input begins; the device captures in 20 ms frames.
4. Decodes 4-bit IMA-ADPCM to 16-bit, 16 kHz mono PCM and writes it to the hidden driver output.
5. Sends `Stop` after voice input ends. Disabling the mode also restores the previous macOS input device.

Bridge v1.1.2 also monitors BLE packet arrival, ADPCM decoding and the Core Audio render callback independently. If macOS stops the virtual output engine while the BLE stream remains connected, the Bridge rebuilds only that output engine and keeps the device connection and original-input restore state intact.

### Output routing safety (v1.3.6)

Audio is sent through an explicit Core Audio HAL output unit pinned to
`M5StopWatchMic_2_UID`, not a default-output audio engine. The visible input is
`M5StopWatchMic_UID`; the existing BlackHole fallback is limited to the virtual
`BlackHole2ch_UID` device. Physical speakers and arbitrary renamed devices are
rejected before an output unit starts.

The output starts muted and verifies the device after initialization and start.
The final render callback verifies the actual device before and after conversion;
an unexpected device or failed query produces silence. Device loss, route changes,
and sample-rate changes latch the mute until the output is rebuilt. The standard
Apple AUConverter handles 16 kHz mono to the virtual device's current rate without
changing the system speaker selection or device sample rate.

Recording preflight and health checks include route identity. If the route fails
during a Typeless recording, that dictation is interrupted and the user is asked
to retry; recovery clears buffered audio and never automatically resumes the
interrupted dictation. Logs report the expected UID and actual device ID.

Regression tests (hardware tests require the installed M5 driver):

```bash
bash tools/typeless_bridge/tests/run_audio_route_tests.sh
bash tools/typeless_bridge/tests/run_audio_route_tests.sh --hardware --loopback
# Read-only audit of an installed Bridge process after its mic is enabled:
bash tools/typeless_bridge/tests/run_audio_route_tests.sh --audit-pid <bridge-pid>
```

The loopback test sends only a synthetic signal to the M5 virtual output and
measures its virtual input RMS. It neither records a physical microphone nor
writes an audio file; physical output devices are not permitted.
macOS may still require microphone permission for the **test program** to read
that virtual input. An SSH session can receive silence when this access is
unavailable; use a logged-in desktop session and explicitly approve the test
only if desired. Do not treat an authorization-blocked loopback as proof of a
broken output route, and do not modify the TCC database to run this test.
The PID audit exits 2 when there is no active output yet (for example, waiting
for the StopWatch to connect), and 1 for a non-allowed output; these are distinct.

Validation boundary (2026-08-31): the Mac mini passed 16 kHz source → 48 kHz
virtual output → virtual input loopback, repeated output reconstruction, and
fault-induced silence. Keep the virtual device at its existing **48 kHz** setting.
The optional diagnostic `--hardware --rates` temporarily changes only the virtual
device rate and restores it on completion/error. It currently reproduces silence
after a live switch to 16 kHz on the tested system, despite a valid output route;
this is **not a passing regression test**. Cross-rate live switching remains an
open compatibility issue; the app does not force device-rate changes. The BLE
microphone stream itself remains 16 kHz and needs no change.

Each BLE packet carries 320 decoded samples (20 ms): 14 bytes of protocol header plus 160 bytes of ADPCM. The steady wire rate is about 8.7 KB/s, while decoded PCM is 32 KB/s. No WAV container or recording file is used.

The product installer adds the Core Audio HAL driver. Driver installation requires macOS administrator authorization and restarts Core Audio.

The custom virtual driver is a separate GPLv3 component based on BlackHole. See [`virtual_mic_driver/README.md`](virtual_mic_driver/README.md) and the repository `THIRD_PARTY_NOTICES.md`.

## Multi-Mac Pairing

The firmware stores up to three bonded macOS hosts. For a Mac mini and iMac in different locations, pair the same `M5Codex-XX` device once on each Mac. After both bonds are stored, the device should reconnect to the nearby paired Mac without deleting and re-pairing Bluetooth.

Only one Mac can be connected at a time. If two paired Macs are in the same room, quit the bridge app or turn off Bluetooth on the Mac that should not own the device.

## Recommended Defaults

- Left action: `F19`
- Codex confirm: `Return`
- Codex guide/follow-up: `Command+Return`
- Quota refresh: `300` seconds
- LaunchAgent: start at login, do not force-restart after user quits

F19 is preferred because it is a normal HID key rather than a pure modifier. The device sends it directly through BLE HID, so behavior stays consistent even if the bridge app is not running. Users can map F19 to Typeless, Tencent Input, WeChat IME, or another local voice-input tool.

Available key bindings in the menu app:

```text
F13, F14, F15, F16, F17, F18, F19, F20, Return, Space, Tab, Escape
```

## Menu Bar Status

The menu bar app shows:

- BLE connection status.
- Voice state detected through Accessibility when Typeless is available.
- Codex quota push status.
- Virtual microphone off / waiting / streaming status.
- Last local error.
- Current left/right key bindings.
- Current quota refresh interval.

## Optional Typeless Integration

The bridge works without Typeless. In that mode, it only syncs configuration and quota/status data; the device remains usable because firmware sends configured keyboard keys through BLE HID.

When Typeless is installed and Accessibility permission is granted, the bridge can detect recording / processing / sent states and push a simple voice-state animation to the device. If `Optional: sync Typeless shortcut to left key` is enabled, the app writes the selected left key into Typeless local settings and creates a backup:

```text
~/Library/Application Support/Typeless/app-settings.json.stopwatch-bridge.bak
```

For other input apps, leave Typeless shortcut sync disabled and configure shortcuts inside that app manually.

### Input behavior

The left key toggles voice input. In Typeless mode, the right key follows the same primary-key interaction while recording, processing, or recovering from an interruption, so it cannot submit text before the recognition result can be reviewed. Once Typeless returns to idle/ready, a right-key tap confirms/sends through firmware BLE HID; holding it for two seconds sends the configured long-press action, `Command+Return` by default. This interaction policy is shared by both StopWatch UI themes. The bridge app observes device events only to update status on the StopWatch screen. It does not restore focus, queue Enter, or simulate keyboard events on macOS.

If Accessibility is unavailable, the app reports `bridge_limited` to the device. The device still sends its configured BLE HID keys, but Typeless state detection is limited.

## Codex Quota Auth

Quota sync is optional.

When enabled, the bridge reads:

```text
~/.codex/auth.json
```

and calls:

```text
https://chatgpt.com/backend-api/wham/usage
```

If the user is not logged in locally, quota push fails but the BLE keyboard and Typeless controls still work.

Codex currently exposes only a weekly quota window. The bridge ignores retired
5-hour windows and sends only `codex.weekly`.

### Daily 08:00 tracking

The first successful weekly quota sample in each local-time period from 08:00
through the next day at 07:59 establishes the daily baseline. State is persisted
at:

```text
~/Library/Application Support/M5StopWatch/StopWatchBleBridge/codex-weekly-daily.json
```

Each BLE quota panel includes:

```json
{
  "codex": {
    "weekly": {
      "left_pct": 85,
      "daily_tracking": {
        "boundary_hour_local": 8,
        "day_key": "2026-07-16",
        "period_start": "2026-07-16T08:00:00+08:00",
        "day_start_left_pct": 91,
        "segment_start_left_pct": 91,
        "current_left_pct": 85,
        "used_since_start_pct_points": 6,
        "reset_count": 0,
        "updated_at": "2026-07-16T14:30:00+08:00"
      }
    }
  }
}
```

Quota decreases are accumulated as daily usage. If the weekly quota resets and
the remaining percentage jumps upward, the jump is not counted as negative
usage; `reset_count` increments and `segment_start_left_pct` starts a new
visual segment. `day_start_left_pct` remains the original daily marker.

To use quota sync:

1. Install Codex locally.
2. Log in through the official Codex/OpenAI flow.
3. Enable quota push in the bridge menu.

Do not ask users to paste tokens into the app.

## Build

```bash
tools/typeless_bridge/build_stopwatch_ble_bridge.sh
```

## Package Release

```bash
tools/typeless_bridge/package_release.sh 1.2.0
```

This creates:

```text
dist/StopWatch-BLE-Bridge-1.2.0-macOS-arm64.zip
dist/StopWatch-BLE-Bridge-1.2.0-macOS-arm64.zip.sha256
```

The release package contains only the app bundle. It does not install the LaunchAgent or start the app automatically.

To build the product installer that installs the app, login LaunchAgent, and
`M5 StopWatch Mic` Core Audio driver together:

```bash
tools/typeless_bridge/package_product_installer.sh 1.2.0
```

The `.pkg` requires administrator authorization because it writes the audio
driver under `/Library/Audio/Plug-Ins/HAL`. Distribution outside your own Mac
requires a Developer ID Installer signature and notarization. The build also
emits the pinned BlackHole corresponding-source archive required by GPLv3.

## Input Mode

The menu bar app can switch the primary device button between `Typeless` and `WeChat IME`.
The Mac helper syncs the selected mode plus primary, confirm, and shake bindings to firmware.
Firmware persists the last synced bindings and sends BLE HID keys directly without needing the helper; helper-only features are Typeless state detection, quota sync, and configuration UI.
In `WeChat IME` mode, primary down/up holds and releases Right Option from firmware HID by default, and confirm sends the configured right key.
The default shake fallback is `Command+A` followed by `Backspace` for clearing the current input.

## Install

```bash
tools/typeless_bridge/install_launch_agent.sh
```

This installs:

- App: `~/Applications/StopWatch BLE Bridge.app`
- LaunchAgent: `~/Library/LaunchAgents/dev.vibecoding.stopwatch-ble-bridge.plist`
- Config: `~/Library/Application Support/M5StopWatch/StopWatchBleBridge/config.json`
- Log: `~/Library/Logs/stopwatch-ble-bridge.log`

The LaunchAgent starts the app at login. `KeepAlive` is disabled, so quitting the menu-bar app is respected.

For stable macOS Accessibility permission across app updates, create a local signing identity once:

```bash
tools/typeless_bridge/create_local_codesign_identity.sh
tools/typeless_bridge/install_launch_agent.sh
```

The installer uses `M5StopWatch Local Code Signing` automatically when it exists. After granting Accessibility once for that signed app, later rebuilds should keep the same authorization. If no local identity exists, the installer skips signing by default. Set `SIGN_BRIDGE_APP=1` only when you explicitly want ad-hoc signing; ad-hoc signatures can invalidate Accessibility permission after each rebuild.

## Uninstall

```bash
tools/typeless_bridge/uninstall_launch_agent.sh
```

Then remove `StopWatch BLE Bridge` from Accessibility permissions if desired.

## Field Tests

Manual BLE state writes:

```bash
tools/typeless_bridge/run_stopwatch_ble_bridge.sh --active --once
tools/typeless_bridge/run_stopwatch_ble_bridge.sh --idle --once
tools/typeless_bridge/run_stopwatch_ble_bridge.sh --status "processing" --once
tools/typeless_bridge/run_stopwatch_ble_bridge.sh --once
```

Expected device behavior:

- `--active --once`: voice waveform appears.
- `--idle --once`: voice waveform disappears.
- `--status "processing" --once`: processing waveform appears.
- `--once`: bridge reads Typeless through Accessibility and writes the detected state.

## Troubleshooting

### Connected but button events do nothing

Check the bridge log:

```bash
tail -n 120 ~/Library/Logs/stopwatch-ble-bridge.log
```

If the log contains:

```text
Event subscription failed
```

macOS may have cached an older BLE GATT table. Forget the old Bluetooth device and pair the current `M5Codex-XX` device again.

If the log contains:

```text
Accessibility unavailable
```

the bridge can still keep the device connected and firmware HID input continues to work, but Typeless state detection requires Accessibility permission. Use the menu item `Open Accessibility Settings`, enable `StopWatch BLE Bridge`, then restart the bridge app.

### Enter timing after voice recognition

During recording or recognition, the right key is routed to the same interaction as the primary key and does not send Enter. Enter is available only after Typeless returns to idle/ready, leaving the recognized text visible for manual review before submission.

## Developer Notes

The firmware exposes two BLE surfaces under the same device name:

- Standard BLE HID keyboard.
- Custom BLE GATT bridge:
  - Service: `ABCD0000-E819-B394-6344-2A2F31424C45`
  - Event notify: `ABCD0001-E819-B394-6344-2A2F31424C45`
  - Status write/read: `ABCD0002-E819-B394-6344-2A2F31424C45`
  - Panel/quota write: `ABCD0003-E819-B394-6344-2A2F31424C45`

Firmware devices advertise as `M5Codex-XX`, where `XX` is a stable two-letter suffix derived from the device MAC address. The bridge scans the `M5Codex-` prefix and still accepts legacy development names `M5Codex-HID4` and `M5Codex-HID5`.

The bridge should remain a local companion app. Do not route Typeless state or Codex auth through a public relay.
