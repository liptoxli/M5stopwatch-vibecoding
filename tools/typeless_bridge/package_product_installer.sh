#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="${1:-$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION")}"
WORK_DIR="${TMPDIR:-/private/tmp}/m5-stopwatch-product-installer-$VERSION"
PACKAGE_ROOT="$WORK_DIR/root"
PACKAGE_SCRIPTS="$WORK_DIR/scripts"
OUTPUT="$ROOT_DIR/dist/M5-StopWatch-Bridge-$VERSION-macOS-arm64.pkg"
APP_SOURCE="${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-release-$VERSION/StopWatch-BLE-Bridge-$VERSION-macOS-arm64/StopWatch BLE Bridge.app"

"$SCRIPT_DIR/package_release.sh" "$VERSION" >/dev/null
DRIVER_SIGN_IDENTITY="${DRIVER_CODESIGN_IDENTITY:-}"
if [[ -z "$DRIVER_SIGN_IDENTITY" ]] && \
   security find-identity -v -p codesigning | grep -Fq "M5StopWatch Local Code Signing"; then
  DRIVER_SIGN_IDENTITY="M5StopWatch Local Code Signing"
fi
if [[ -n "$DRIVER_SIGN_IDENTITY" ]]; then
  DRIVER_CODESIGN_IDENTITY="$DRIVER_SIGN_IDENTITY" \
    "$SCRIPT_DIR/virtual_mic_driver/build_driver.sh" "$WORK_DIR/driver" >/dev/null
else
  "$SCRIPT_DIR/virtual_mic_driver/build_driver.sh" "$WORK_DIR/driver" >/dev/null
fi

rm -rf "$WORK_DIR/root" "$WORK_DIR/scripts" "$OUTPUT"
mkdir -p "$PACKAGE_ROOT/Applications"
mkdir -p "$PACKAGE_ROOT/Library/Audio/Plug-Ins/HAL"
mkdir -p "$PACKAGE_ROOT/Library/LaunchAgents"
mkdir -p "$PACKAGE_SCRIPTS"

cp -R -X "$APP_SOURCE" "$PACKAGE_ROOT/Applications/StopWatch BLE Bridge.app"
cp -R -X "$WORK_DIR/driver/M5StopWatchMic.driver" "$PACKAGE_ROOT/Library/Audio/Plug-Ins/HAL/"
cp -X "$SCRIPT_DIR/product_launch_agent.plist" "$PACKAGE_ROOT/Library/LaunchAgents/dev.vibecoding.stopwatch-ble-bridge.plist"
cp -X "$SCRIPT_DIR/virtual_mic_driver/postinstall" "$PACKAGE_SCRIPTS/postinstall"
chmod 755 "$PACKAGE_SCRIPTS/postinstall"
/usr/bin/xattr -cr "$PACKAGE_ROOT" 2>/dev/null || true

PKG_ARGS=(
  --root "$PACKAGE_ROOT"
  --scripts "$PACKAGE_SCRIPTS"
  --install-location /
  --identifier dev.vibecoding.m5-stopwatch-bridge.pkg
  --version "$VERSION"
)
if [[ -n "${INSTALLER_SIGN_IDENTITY:-}" ]]; then
  PKG_ARGS+=(--sign "$INSTALLER_SIGN_IDENTITY")
fi
COPYFILE_DISABLE=1 pkgbuild "${PKG_ARGS[@]}" "$OUTPUT"

SOURCE_ARCHIVE="$ROOT_DIR/dist/M5-StopWatch-Mic-BlackHole-source-ffcb744.tar.gz"
git -C "${BLACKHOLE_SOURCE_DIR:-${TMPDIR:-/private/tmp}/m5-stopwatch-blackhole-source}" \
  archive --format=tar.gz --prefix=BlackHole-ffcb744/ \
  -o "$SOURCE_ARCHIVE" ffcb74433fbcf8c8ca5c736677c1a4864384dc09

/usr/bin/shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
/usr/bin/shasum -a 256 "$SOURCE_ARCHIVE" > "$SOURCE_ARCHIVE.sha256"
printf '%s\n' "$OUTPUT"
cat "$OUTPUT.sha256"
printf '%s\n' "$SOURCE_ARCHIVE"
cat "$SOURCE_ARCHIVE.sha256"
