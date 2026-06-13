#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALLED_BIN="$HOME/Applications/StopWatch BLE Bridge.app/Contents/MacOS/stopwatch-ble-bridge"

if [[ -x "$INSTALLED_BIN" ]]; then
  exec "$INSTALLED_BIN" "$@"
fi

echo "StopWatch BLE bridge is not installed." >&2
echo "Run: $SCRIPT_DIR/install_launch_agent.sh" >&2
exit 1
