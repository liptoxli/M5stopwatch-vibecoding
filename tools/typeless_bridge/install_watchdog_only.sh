#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$HOME/Applications/StopWatch BLE Bridge.app"
BIN="$APP_DIR/Contents/MacOS/stopwatch-ble-bridge"
WATCHDOG="$HOME/Library/Application Support/Liptox/StopWatchBleBridge/watchdog.sh"
WATCHDOG_PLIST="$HOME/Library/LaunchAgents/com.liptox.stopwatch-ble-bridge.watchdog.plist"
LOG="$HOME/Library/Logs/stopwatch-ble-bridge.log"

mkdir -p "$(dirname "$WATCHDOG")"
cat > "$WATCHDOG" <<WATCHDOG_SH
#!/usr/bin/env bash
set -euo pipefail

APP="$APP_DIR"
BIN="$BIN"
LOG="$LOG"

if [[ ! -x "\$BIN" ]]; then
  exit 0
fi

if ! /usr/bin/pgrep -x "stopwatch-ble-bridge" >/dev/null; then
  /bin/echo "\$(/bin/date '+%Y-%m-%d %H:%M:%S') restarting StopWatch BLE Bridge" >> "\$LOG"
  /usr/bin/open -gj "\$APP"
fi
WATCHDOG_SH
chmod 755 "$WATCHDOG"

cat > "$WATCHDOG_PLIST" <<WATCHDOG_PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.liptox.stopwatch-ble-bridge.watchdog</string>
  <key>ProgramArguments</key>
  <array>
    <string>$WATCHDOG</string>
  </array>
  <key>LimitLoadToSessionType</key>
  <string>Aqua</string>
  <key>RunAtLoad</key>
  <true/>
  <key>StartInterval</key>
  <integer>60</integer>
  <key>StandardOutPath</key>
  <string>$LOG</string>
  <key>StandardErrorPath</key>
  <string>$HOME/Library/Logs/stopwatch-ble-bridge.err.log</string>
</dict>
</plist>
WATCHDOG_PLIST

launchctl bootout "gui/$(id -u)" "$WATCHDOG_PLIST" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$WATCHDOG_PLIST"
echo "$WATCHDOG_PLIST"
