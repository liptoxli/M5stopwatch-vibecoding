#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/private/tmp}/m5-pairing-reset-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror "$SCRIPT_DIR/pairing_reset_policy_tests.cpp" -o "$TEST_DIR/pairing-reset-tests"
"$TEST_DIR/pairing-reset-tests"
