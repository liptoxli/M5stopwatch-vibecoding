/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <apps/app_codex/codex_config.h>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <apps/common/audio/audio.h>
#include <hal/utils/settings/settings.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <algorithm>
#include <iterator>

using namespace mooncake;
using namespace smooth_ui_toolkit;

namespace {

void add_ssid_if_missing(SsidManager& ssids, const char* ssid, const char* password)
{
    const auto& saved = ssids.GetSsidList();
    const bool exists = std::any_of(saved.begin(), saved.end(), [&](const auto& item) {
        return item.ssid == ssid;
    });
    if (!exists) {
        ssids.AddSsid(ssid, password);
    }
}

void init_wifi_station()
{
    Settings settings("system", false);
    if (!settings.GetBool("wifi_enabled", codex_config::kDefaultWifiEnabled)) {
        return;
    }

    auto& wifi = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = codex_config::kWifiPortalPrefix;
    config.language = codex_config::kLanguage;
    wifi.Initialize(config);

    auto& ssids = SsidManager::GetInstance();
    for (size_t i = 0; i < codex_config::kKnownWifiProfileCount; ++i) {
        const auto& profile = codex_config::kKnownWifiProfiles[i];
        if (profile.preferDefault) {
            ssids.AddSsid(profile.ssid, profile.password);
        } else {
            add_ssid_if_missing(ssids, profile.ssid, profile.password);
        }
    }
    const auto& saved_ssids = ssids.GetSsidList();
    for (int i = 0; i < saved_ssids.size(); ++i) {
        const bool prefer_default = std::any_of(std::begin(codex_config::kKnownWifiProfiles),
                                                std::end(codex_config::kKnownWifiProfiles),
                                                [&](const auto& profile) {
                                                    return profile.preferDefault && saved_ssids[i].ssid == profile.ssid;
                                                });
        if (prefer_default) {
            ssids.SetDefaultSsid(i);
            break;
        }
    }
}

void init_bluetooth()
{
    const bool enabled = GetHAL().getBluetoothEnabled(true);
    mclog::tagInfo("BLE-Bridge", "startup requested: enabled={}", enabled);
}

}  // namespace

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();
    init_wifi_station();
    init_bluetooth();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps
    GetMooncake().installApp(std::make_unique<AppCodex>());
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppWatchFace>());
    GetMooncake().installApp(std::make_unique<AppLuckyWheel>());
    GetMooncake().installApp(std::make_unique<AppMaze>());
    GetMooncake().installApp(std::make_unique<AppSetup>());
    // GetMooncake().installApp(std::make_unique<AppTemplate>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().updateActivityMonitor();
        if (!GetHAL().isActivitySleeping()) {
            GetMooncake().update();
        }
        // The UI already rate-limits its own animations. A 10 ms scheduler
        // cadence keeps physical-key latency below one frame while allowing
        // the idle task and dynamic-frequency scaling to run longer.
        GetHAL().delay(10);
    }
}
