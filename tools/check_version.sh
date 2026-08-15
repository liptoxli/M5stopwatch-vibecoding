#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "VERSION must use MAJOR.MINOR.PATCH, got: $version" >&2
    exit 1
fi

expected_firmware_version="$(tr -d '[:space:]' < "$repo_root/firmware-stopwatch-idf/version.txt")"
if [[ "$expected_firmware_version" != "$version" ]]; then
    echo "firmware-stopwatch-idf/version.txt does not match VERSION" >&2
    exit 1
fi

grep -Fq "FirmwareVersion = \"V$version\"" \
    "$repo_root/firmware-stopwatch-idf/main/apps/common/common.h"
grep -Fq "version-v$version-" "$repo_root/README.md"
grep -Fq "当前公开版本是 **v$version**" "$repo_root/README.md"
grep -Fq "## [$version]" "$repo_root/CHANGELOG.md"

echo "Version metadata is consistent: v$version"
