#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/private/tmp}/m5-voice-gate-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
c++ -std=c++17 "$SCRIPT_DIR/voice_start_gate_tests.cpp" -o "$TEST_DIR/voice-start-gate-tests"
"$TEST_DIR/voice-start-gate-tests"
