/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "ble_bridge.h"
#include "utils/settings/settings.h"
#include <mooncake_log.h>

namespace {

static constexpr const char* _bluetooth_enabled_key = "bt_enabled";
static const std::string_view _tag                  = "HAL-Bluetooth";

}  // namespace

void Hal::setBluetoothEnabled(bool enabled, bool saveToSettings)
{
    if (_bluetooth_enabled != enabled) {
        mclog::tagInfo(_tag, "set bluetooth enabled: {}", enabled);
    }
    _bluetooth_enabled = enabled;
    ble_bridge::set_enabled(enabled);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetBool(_bluetooth_enabled_key, enabled);
    }
}

bool Hal::getBluetoothEnabled(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _bluetooth_enabled = settings.GetBool(_bluetooth_enabled_key, true);
        ble_bridge::set_enabled(_bluetooth_enabled);
    }

    return _bluetooth_enabled;
}
