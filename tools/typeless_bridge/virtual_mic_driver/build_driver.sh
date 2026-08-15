#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/.build}"
SOURCE_DIR="${BLACKHOLE_SOURCE_DIR:-${TMPDIR:-/private/tmp}/m5-stopwatch-blackhole-source}"
SOURCE_REPO="https://github.com/ExistentialAudio/BlackHole.git"
SOURCE_COMMIT="ffcb74433fbcf8c8ca5c736677c1a4864384dc09"
DRIVER="$OUTPUT_DIR/M5StopWatchMic.driver"

if [[ ! -f "$SOURCE_DIR/BlackHole/BlackHole.c" ]]; then
  git clone "$SOURCE_REPO" "$SOURCE_DIR"
fi
git -C "$SOURCE_DIR" fetch --depth 1 origin "$SOURCE_COMMIT"
git -C "$SOURCE_DIR" checkout --detach "$SOURCE_COMMIT"

mkdir -p "$DRIVER/Contents/MacOS"
cp "$SCRIPT_DIR/Info.plist" "$DRIVER/Contents/Info.plist"

clang \
  -arch arm64 \
  -bundle \
  -O2 \
  -mmacosx-version-min=14.0 \
  -framework Accelerate \
  -framework CoreAudio \
  -framework CoreFoundation \
  -DkNumber_Of_Channels=2 \
  -DkHas_Driver_Name_Format=false \
  '-DkDriver_Name="M5StopWatchMic"' \
  '-DkPlugIn_BundleID="dev.vibecoding.m5-stopwatch-mic"' \
  '-DkDevice_Name="M5 StopWatch Mic"' \
  -DkDevice_IsHidden=false \
  -DkDevice_HasInput=true \
  -DkDevice_HasOutput=false \
  '-DkDevice2_Name="M5 StopWatch Mic Bridge"' \
  -DkDevice2_IsHidden=true \
  -DkDevice2_HasInput=false \
  -DkDevice2_HasOutput=true \
  '-DkSampleRates=16000,44100,48000' \
  "$SOURCE_DIR/BlackHole/BlackHole.c" \
  -o "$DRIVER/Contents/MacOS/M5StopWatchMic"

chmod 755 "$DRIVER/Contents/MacOS/M5StopWatchMic"
if [[ -n "${DRIVER_CODESIGN_IDENTITY:-}" ]]; then
  codesign --force --deep --sign "$DRIVER_CODESIGN_IDENTITY" "$DRIVER"
else
  codesign --force --deep --sign - "$DRIVER"
fi
codesign --verify --deep --strict "$DRIVER"
printf '%s\n' "$DRIVER"
