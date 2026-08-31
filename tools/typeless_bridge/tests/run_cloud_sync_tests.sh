#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/private/tmp}/m5-cloud-sync-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
cp "$SCRIPT_DIR/../stopwatch_cloud_sync.swift" "$SCRIPT_DIR/cloud_sync_tests.swift" "$TEST_DIR/"
swiftc -target arm64-apple-macosx14.0 \
  -module-cache-path "${TMPDIR:-/private/tmp}/stopwatch-ble-bridge-module-cache" \
  "$TEST_DIR/stopwatch_cloud_sync.swift" "$TEST_DIR/cloud_sync_tests.swift" \
  -o "$TEST_DIR/cloud-sync-tests"
"$TEST_DIR/cloud-sync-tests"
