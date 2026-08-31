#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/private/tmp}/m5-audio-route-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
swiftc -D DEBUG -target arm64-apple-macosx14.0 \
  -module-cache-path "${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-module-cache" \
  "$SCRIPT_DIR/../stopwatch_microphone.swift" "$SCRIPT_DIR/audio_route_tests.swift" \
  -o "$TEST_DIR/audio-route-tests"
"$TEST_DIR/audio-route-tests" "$@"
