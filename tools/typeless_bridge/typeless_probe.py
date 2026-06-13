#!/usr/bin/env python3
"""Probe Typeless local state and Accessibility-visible UI text.

This is a validation tool, not the final BLE bridge.
It avoids private Typeless IPC and only uses local settings, SQLite history,
and macOS Accessibility via osascript/System Events.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import subprocess
import time
from pathlib import Path


HOME = Path.home()
SUPPORT_DIR = HOME / "Library" / "Application Support" / "Typeless"
SETTINGS_PATH = SUPPORT_DIR / "app-settings.json"
DB_PATH = SUPPORT_DIR / "typeless.db"


def load_settings() -> dict:
    if not SETTINGS_PATH.exists():
        return {"error": f"missing {SETTINGS_PATH}"}
    return json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))


def latest_history(limit: int = 5) -> list[dict]:
    if not DB_PATH.exists():
        return [{"error": f"missing {DB_PATH}"}]
    sql = """
        select id, status, mode, created_at, updated_at, duration, audio_local_path
        from history_v2
        order by created_at desc
        limit ?
    """
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        return [dict(row) for row in conn.execute(sql, (limit,))]


def typeless_process_summary() -> str:
    script = (
        'tell application "System Events" to tell process "Typeless" '
        'to get {name, frontmost, visible, count of windows, count of UI elements}'
    )
    return run_osascript(script)


def typeless_ui_dump() -> str:
    script = (
        'tell application "System Events" to tell process "Typeless" '
        'to get {role, name, value, description} of UI elements'
    )
    return run_osascript(script)


def run_osascript(script: str) -> str:
    try:
        completed = subprocess.run(
            ["osascript", "-e", script],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=2.0,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    return completed.stdout.strip()


def print_once() -> None:
    settings = load_settings()
    shortcuts = settings.get("featureShortcutBindings", {})
    print("== Typeless settings ==")
    print(json.dumps(shortcuts, ensure_ascii=False, indent=2))
    print("\n== Typeless process ==")
    print(typeless_process_summary())
    print("\n== Latest history ==")
    print(json.dumps(latest_history(), ensure_ascii=False, indent=2))
    print("\n== Accessibility UI ==")
    print(typeless_ui_dump())


def watch(interval: float) -> None:
    while True:
        print("\n--- probe tick ---")
        print("process:", typeless_process_summary())
        print("latest:", json.dumps(latest_history(1), ensure_ascii=False))
        ui = typeless_ui_dump()
        useful = [line for line in ui.splitlines() if line.strip()]
        print("ui:")
        for line in useful[:80]:
            print(line)
        time.sleep(interval)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--watch", action="store_true", help="poll Typeless state continuously")
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()
    if args.watch:
        watch(args.interval)
    else:
        print_once()


if __name__ == "__main__":
    main()
