/*
 * Project-level configuration for the Codex StopWatch app.
 *
 * Keep product defaults here so firmware behavior is not scattered across
 * UI, BLE, Wi-Fi, and quota workers.
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace codex_config {

constexpr const char* kDeviceId = "m5stack-stopwatch";
constexpr const char* kBleDeviceNamePrefix = "M5Codex-";
constexpr const char* kWifiPortalPrefix = "M5StopWatch";
constexpr const char* kLanguage = "zh-CN";

constexpr const char* kPrimaryPanelUrl =
    "https://example.com/v1/device/panel?device_id=m5stack-stopwatch";
constexpr const char* kCompatPanelUrl =
    "https://example.com/api/rlcd/panel?device_id=m5stack-stopwatch";

constexpr uint32_t kQuotaPollMs = 10 * 60 * 1000;
constexpr uint32_t kQuotaFirstPollDelayMs = 1500;
constexpr uint32_t kWifiConnectWaitMs = 12 * 1000;
constexpr int kHttpTimeoutMs = 8000;
constexpr int kMaxPanelBodyBytes = 20 * 1024;
constexpr int kHttpRxBufferBytes = 1024;
constexpr int kHttpTxBufferBytes = 512;
constexpr uint32_t kQuotaTaskStackBytes = 12 * 1024;
constexpr bool kDefaultWifiEnabled = false;
constexpr bool kDefaultWifiQuotaFallbackEnabled = false;
constexpr bool kEnableAudioOutput = true;
constexpr bool kEnableAudioInput = false;
constexpr bool kEnableAudioCodecInputPath = true;

constexpr float kTiltFilterAlpha = 0.12f;
constexpr float kTiltMaxOffset = 12.0f;
constexpr float kShakeMotionFloor = 220.0f;
constexpr float kShakeThreshold = 460.0f;
constexpr uint32_t kShakeCooldownMs = 1500;

constexpr uint32_t kBleKeyTapMs = 100;
constexpr uint32_t kBlePairingPasskey = 0;
constexpr uint32_t kBleBatteryUpdateMs = 60 * 1000;
constexpr uint32_t kBleCompanionReadyWindowMs = 30 * 1000;

struct WifiProfile {
    const char* ssid;
    const char* password;
    bool preferDefault;
};

constexpr WifiProfile kKnownWifiProfiles[] = {
    {"YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD", true},
};

constexpr size_t kKnownWifiProfileCount = sizeof(kKnownWifiProfiles) / sizeof(kKnownWifiProfiles[0]);

}  // namespace codex_config
