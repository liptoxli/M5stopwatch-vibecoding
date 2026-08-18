#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"
bridge_version="$(tr -d '[:space:]' < "$repo_root/tools/typeless_bridge/VERSION")"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "VERSION must use MAJOR.MINOR.PATCH, got: $version" >&2
    exit 1
fi

if [[ ! "$bridge_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "tools/typeless_bridge/VERSION must use MAJOR.MINOR.PATCH, got: $bridge_version" >&2
    exit 1
fi

expected_firmware_version="$(tr -d '[:space:]' < "$repo_root/firmware-stopwatch-idf/version.txt")"
if [[ "$expected_firmware_version" != "$version" ]]; then
    echo "firmware-stopwatch-idf/version.txt does not match VERSION" >&2
    exit 1
fi

grep -Fq "FirmwareVersion = \"V$version\"" \
    "$repo_root/firmware-stopwatch-idf/main/apps/common/common.h"
grep -Fq "firmware-v$version-" "$repo_root/README.md"
grep -Fq "固件 v$version / macOS Bridge v$bridge_version" "$repo_root/README.md"
grep -Fq "## [$version]" "$repo_root/CHANGELOG.md"
grep -Fq "## [$bridge_version]" "$repo_root/tools/typeless_bridge/CHANGELOG.md"

echo "Version metadata is consistent: firmware v$version, Bridge v$bridge_version"
