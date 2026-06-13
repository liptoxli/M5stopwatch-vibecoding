#!/usr/bin/env bash
set -euo pipefail

PLIST="$HOME/Library/LaunchAgents/dev.vibecoding.stopwatch-ble-bridge.plist"
APP_DIR="$HOME/Applications/StopWatch BLE Bridge.app"

launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
rm -f "$PLIST"
rm -rf "$APP_DIR"
