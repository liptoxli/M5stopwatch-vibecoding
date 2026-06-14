#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_BIN="$SCRIPT_DIR/.build/stopwatch-ble-bridge"
APP_DIR="$HOME/Applications/StopWatch BLE Bridge.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
BIN="$MACOS_DIR/stopwatch-ble-bridge"
PLIST="$HOME/Library/LaunchAgents/dev.vibecoding.stopwatch-ble-bridge.plist"
WATCHDOG_PLIST="$HOME/Library/LaunchAgents/dev.vibecoding.stopwatch-ble-bridge.watchdog.plist"
WATCHDOG="$HOME/Library/Application Support/M5StopWatch/StopWatchBleBridge/watchdog.sh"
LOG="$HOME/Library/Logs/stopwatch-ble-bridge.log"
SIGN_IDENTITY="${BRIDGE_CODESIGN_IDENTITY:-M5StopWatch Local Code Signing}"

"$SCRIPT_DIR/build_stopwatch_ble_bridge.sh" >/dev/null
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl bootout "gui/$(id -u)" "$WATCHDOG_PLIST" 2>/dev/null || true
/usr/bin/pkill -x "stopwatch-ble-bridge" 2>/dev/null || true
rm -f "$WATCHDOG_PLIST" "$WATCHDOG"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
cp "$BUILD_BIN" "$BIN"
chmod 755 "$BIN"

ICONSET="$RESOURCES_DIR/StopWatchBridge.iconset"
rm -rf "$ICONSET" "$RESOURCES_DIR/StopWatchBridge.icns"
mkdir -p "$ICONSET"
/usr/bin/swiftc "$SCRIPT_DIR/generate_bridge_icon.swift" -o /tmp/generate_stopwatch_bridge_icon
/tmp/generate_stopwatch_bridge_icon "$ICONSET" >/dev/null
/usr/bin/iconutil -c icns "$ICONSET" -o "$RESOURCES_DIR/StopWatchBridge.icns"

cat > "$CONTENTS_DIR/Info.plist" <<'INFO_PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleDisplayName</key>
  <string>StopWatch BLE Bridge</string>
  <key>CFBundleExecutable</key>
  <string>stopwatch-ble-bridge</string>
  <key>CFBundleIconFile</key>
  <string>StopWatchBridge</string>
  <key>CFBundleIconName</key>
  <string>StopWatchBridge</string>
  <key>CFBundleIdentifier</key>
  <string>dev.vibecoding.stopwatch-ble-bridge</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>StopWatch BLE Bridge</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0</string>
  <key>CFBundleVersion</key>
  <string>1</string>
  <key>LSUIElement</key>
  <true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>Connects to the M5 StopWatch over Bluetooth to sync Codex and Typeless state.</string>
</dict>
</plist>
INFO_PLIST

if security find-identity -v -p codesigning | grep -Fq "$SIGN_IDENTITY"; then
  /usr/bin/xattr -cr "$APP_DIR" 2>/dev/null || true
  codesign --force --deep --sign "$SIGN_IDENTITY" "$APP_DIR" >/dev/null
  echo "Signed app with stable local identity: $SIGN_IDENTITY"
elif [[ "${SIGN_BRIDGE_APP:-0}" == "1" ]]; then
  /usr/bin/xattr -cr "$APP_DIR" 2>/dev/null || true
  codesign --force --deep --sign - "$APP_DIR" >/dev/null
  echo "Signed app with ad-hoc identity. Accessibility permission may need to be re-approved after each build."
else
  echo "Skipping codesign. Run tools/typeless_bridge/create_local_codesign_identity.sh once for stable Accessibility permission across updates."
fi

cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>dev.vibecoding.stopwatch-ble-bridge</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/open</string>
    <string>-W</string>
    <string>-gj</string>
    <string>$APP_DIR</string>
  </array>
  <key>LimitLoadToSessionType</key>
  <string>Aqua</string>
  <key>ProcessType</key>
  <string>Interactive</string>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <false/>
  <key>StandardOutPath</key>
  <string>$LOG</string>
  <key>StandardErrorPath</key>
  <string>$HOME/Library/Logs/stopwatch-ble-bridge.err.log</string>
</dict>
</plist>
PLIST

launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "$PLIST"
