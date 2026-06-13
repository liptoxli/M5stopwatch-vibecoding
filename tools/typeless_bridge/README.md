# StopWatch BLE Bridge

StopWatch BLE Bridge is the macOS companion app for the M5 StopWatch Codex firmware.

It provides four local functions:

- Connect to `M5Codex-*` over Bluetooth.
- Sync configurable key bindings and input mode to firmware.
- Optionally observe Typeless recording/processing state and sync it back to the device.
- Optionally read local Codex auth and push quota snapshots to the device.

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
6. Open `Settings...` and choose:

   - Left key: default `F19`.
   - Right key: default `Return`.
   - Quota refresh interval: default `300` seconds.
   - Codex quota push: enabled by default, requires local Codex login.
   - Typeless shortcut sync: disabled by default. Enable only when using Typeless and you want the app to write the left key into Typeless local settings.

7. Configure the same left/right shortcuts in your input app if needed. This can be Typeless, Tencent Input, or any other app that accepts normal keyboard shortcuts.

## Recommended Defaults

- Left action: `F19`
- Codex confirm: `Return`
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

### Input boundary

The left key toggles voice input and the right key confirms/sends through firmware BLE HID. The bridge app observes device events only to update status on the StopWatch screen. It does not restore focus, queue Enter, or simulate keyboard events on macOS.

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

For open-source users, document this as:

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
tools/typeless_bridge/package_release.sh 1.0.0
```

This creates:

```text
dist/StopWatch-BLE-Bridge-1.0.0-macOS-arm64.zip
dist/StopWatch-BLE-Bridge-1.0.0-macOS-arm64.zip.sha256
```

The release package contains only the app bundle. It does not install the LaunchAgent or start the app automatically.

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

The installer skips ad-hoc codesigning by default to reduce macOS Accessibility permission invalidation during development. Set `SIGN_BRIDGE_APP=1` before running the installer if you explicitly want to sign the app bundle.

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

Enter is sent by the device firmware when the right key is pressed. If the input app has not finished inserting recognized text, wait for the device processing indicator to clear before pressing confirm.

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
