#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/.build"
OUT_BIN="$OUT_DIR/stopwatch-ble-bridge"
MODULE_CACHE_DIR="${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-module-cache"

mkdir -p "$OUT_DIR" "$MODULE_CACHE_DIR"
swiftc \
  -target arm64-apple-macosx14.0 \
  -module-cache-path "$MODULE_CACHE_DIR" \
  "$SCRIPT_DIR/stopwatch_ble_bridge.swift" \
  -o "$OUT_BIN"

echo "$OUT_BIN"
