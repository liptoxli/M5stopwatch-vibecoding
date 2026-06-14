#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="${1:-1.0.0}"
DIST_DIR="$ROOT_DIR/dist"
WORK_DIR="${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-release-$VERSION"
STAGE_DIR="$WORK_DIR/StopWatch-BLE-Bridge-$VERSION-macOS-arm64"
APP_DIR="$STAGE_DIR/StopWatch BLE Bridge.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
BUILD_BIN="$SCRIPT_DIR/.build/stopwatch-ble-bridge"
BIN="$MACOS_DIR/stopwatch-ble-bridge"
ZIP_PATH="$DIST_DIR/StopWatch-BLE-Bridge-$VERSION-macOS-arm64.zip"
TMP_ZIP_PATH="$WORK_DIR/StopWatch-BLE-Bridge-$VERSION-macOS-arm64.zip"
SIGN_IDENTITY="${BRIDGE_CODESIGN_IDENTITY:-M5StopWatch Local Code Signing}"

"$SCRIPT_DIR/build_stopwatch_ble_bridge.sh" >/dev/null

rm -rf "$WORK_DIR" "$ZIP_PATH" "$ZIP_PATH.sha256"
mkdir -p "$WORK_DIR" "$DIST_DIR"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
cp "$BUILD_BIN" "$BIN"
chmod 755 "$BIN"

ICONSET="$RESOURCES_DIR/StopWatchBridge.iconset"
rm -rf "$ICONSET" "$RESOURCES_DIR/StopWatchBridge.icns"
mkdir -p "$ICONSET"
/usr/bin/swiftc "$SCRIPT_DIR/generate_bridge_icon.swift" -o "$DIST_DIR/generate_stopwatch_bridge_icon"
"$DIST_DIR/generate_stopwatch_bridge_icon" "$ICONSET" >/dev/null
/usr/bin/iconutil -c icns "$ICONSET" -o "$RESOURCES_DIR/StopWatchBridge.icns"
rm -rf "$ICONSET" "$DIST_DIR/generate_stopwatch_bridge_icon"

cat > "$CONTENTS_DIR/Info.plist" <<INFO_PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>zh_CN</string>
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
  <string>$VERSION</string>
  <key>CFBundleVersion</key>
  <string>$VERSION</string>
  <key>LSMinimumSystemVersion</key>
  <string>14.0</string>
  <key>LSUIElement</key>
  <true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>用于通过蓝牙连接 M5 StopWatch，同步 Codex 额度、输入状态和按键配置。</string>
</dict>
</plist>
INFO_PLIST

if security find-identity -v -p codesigning | grep -Fq "$SIGN_IDENTITY"; then
  /usr/bin/xattr -cr "$APP_DIR" 2>/dev/null || true
  codesign --force --deep --sign "$SIGN_IDENTITY" "$APP_DIR" >/dev/null
  echo "Signed release app with stable local identity: $SIGN_IDENTITY"
elif [[ "${SIGN_BRIDGE_APP:-0}" == "1" ]]; then
  /usr/bin/xattr -cr "$APP_DIR" 2>/dev/null || true
  codesign --force --deep --sign - "$APP_DIR" >/dev/null
  echo "Signed release app with ad-hoc identity. Accessibility permission may need to be re-approved after each build."
else
  echo "Skipping codesign. Run tools/typeless_bridge/create_local_codesign_identity.sh once for stable Accessibility permission across updates."
fi

/usr/bin/ditto -c -k --noextattr --noqtn --keepParent "$APP_DIR" "$TMP_ZIP_PATH"
cp "$TMP_ZIP_PATH" "$ZIP_PATH"
/usr/bin/shasum -a 256 "$ZIP_PATH" > "$ZIP_PATH.sha256"

echo "$ZIP_PATH"
cat "$ZIP_PATH.sha256"
