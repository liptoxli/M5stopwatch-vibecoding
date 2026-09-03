#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/private/tmp}/m5-mic-ready-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
swiftc -target arm64-apple-macosx14.0 \
  -module-cache-path "${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-module-cache" \
  "$SCRIPT_DIR/../stopwatch_microphone_readiness.swift" \
  "$SCRIPT_DIR/microphone_readiness_tests.swift" \
  -o "$TEST_DIR/microphone-readiness-tests"
"$TEST_DIR/microphone-readiness-tests"
