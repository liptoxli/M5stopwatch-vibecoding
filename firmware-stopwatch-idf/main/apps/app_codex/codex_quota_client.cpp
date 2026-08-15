/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "codex_quota_client.h"
#include "codex_config.h"
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <wifi_manager.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

namespace codex {
namespace {

constexpr const char* kTag = "CodexQuota";
constexpr const char* kQuotaSettingsNs = "codex";
constexpr const char* kSystemSettingsNs = "system";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr char kWifiQuotaFallbackKey[] = "wifi_q_fback";
constexpr char kWeekSegmentStartKey[] = "wk_seg_start";
constexpr char kWeekResetCountKey[] = "wk_reset_cnt";
constexpr char kWeekPeriodStartKey[] = "wk_period";
constexpr char kContextPressureKey[] = "ctx_pressure";
constexpr char kCompactThresholdKey[] = "cmp_threshold";
constexpr char kTotalTokensLabelKey[] = "tokens_label";

constexpr size_t kNvsMaxKeyLength = 15;
static_assert(sizeof(kWifiQuotaFallbackKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kWeekSegmentStartKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kWeekResetCountKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kWeekPeriodStartKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kContextPressureKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kCompactThresholdKey) - 1 <= kNvsMaxKeyLength);
static_assert(sizeof(kTotalTokensLabelKey) - 1 <= kNvsMaxKeyLength);

int normalizedPercent(JsonVariantConst value)
{
    if (value.isNull()) {
        return -1;
    }

    int pct = -1;
    if (value.is<int>()) {
        pct = value.as<int>();
    } else if (value.is<float>()) {
        pct = static_cast<int>(value.as<float>() + 0.5f);
    } else if (value.is<const char*>()) {
        pct = std::atoi(value.as<const char*>());
    }
    return pct < 0 ? -1 : std::clamp(pct, 0, 100);
}

std::string jsonString(JsonVariantConst value, const char* fallback)
{
    if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        return text && text[0] ? text : (fallback ? fallback : "");
    }
    return fallback ? fallback : "";
}

bool jsonBool(JsonVariantConst value, bool fallback = false)
{
    if (value.is<bool>()) {
        return value.as<bool>();
    }
    if (value.is<int>()) {
        return value.as<int>() != 0;
    }
    if (value.is<const char*>()) {
        std::string text = value.as<const char*>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text == "true" || text == "1" || text == "yes" || text == "processing" || text == "running";
    }
    return fallback;
}

float jsonFloat(JsonVariantConst value, float fallback = 0.0f)
{
    if (value.isNull()) {
        return fallback;
    }
    if (value.is<float>()) {
        return value.as<float>();
    }
    if (value.is<int>()) {
        return static_cast<float>(value.as<int>());
    }
    if (value.is<const char*>()) {
        char* end = nullptr;
        const char* text = value.as<const char*>();
        if (!text || !text[0]) {
            return fallback;
        }
        const float parsed = std::strtof(text, &end);
        return end && *end == '\0' ? parsed : fallback;
    }
    return fallback;
}

bool isProcessingState(const std::string& state)
{
    std::string text = state;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text == "processing" || text == "running" || text == "thinking" ||
           text == "waiting" || text == "listening" || text == "busy";
}

bool systemTimeLooksValid()
{
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    return timeinfo != nullptr && (timeinfo->tm_year + 1900) >= 2026;
}

bool snapshotHasQuota(const QuotaSnapshot& snapshot)
{
    return snapshot.weeklyLeftPct >= 0 && snapshot.weeklyLeftPct <= 100;
}

JsonVariantConst firstObject(JsonVariantConst root, const char* a, const char* b = nullptr, const char* c = nullptr)
{
    JsonVariantConst node = root[a];
    if (!b || node.isNull()) {
        return node;
    }
    node = node[b];
    if (!c || node.isNull()) {
        return node;
    }
    return node[c];
}

int quotaLeftPct(JsonVariantConst quota)
{
    int pct = normalizedPercent(quota["left_pct"]);
    if (pct >= 0) {
        return pct;
    }
    pct = normalizedPercent(quota["remaining_pct"]);
    if (pct >= 0) {
        return pct;
    }
    return normalizedPercent(quota["left_percent"]);
}

int64_t jsonInt64(JsonVariantConst value, int64_t fallback = 0)
{
    if (value.isNull()) {
        return fallback;
    }
    if (value.is<int64_t>()) {
        return value.as<int64_t>();
    }
    if (value.is<int>()) {
        return value.as<int>();
    }
    if (value.is<const char*>()) {
        char* end = nullptr;
        const char* text = value.as<const char*>();
        if (!text || !text[0]) {
            return fallback;
        }
        const int64_t parsed = std::strtoll(text, &end, 10);
        return end && *end == '\0' ? parsed : fallback;
    }
    return fallback;
}

bool applyPanelTime(JsonVariantConst root)
{
    const int64_t epoch = jsonInt64(root["server_epoch"], 0);
    if (epoch < 1600000000) {
        return false;
    }

    const std::time_t now = std::time(nullptr);
    if (systemTimeLooksValid() && std::llabs(static_cast<long long>(now - epoch)) <= 2) {
        return true;
    }

    const struct timeval tv = {
        .tv_sec  = static_cast<time_t>(epoch),
        .tv_usec = 0,
    };
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(kTag, "BLE panel time apply failed");
        return false;
    }
    GetHAL().syncSystemTimeToRtc();
    ESP_LOGI(kTag, "BLE panel time synced epoch=%lld", static_cast<long long>(epoch));
    return true;
}

}  // namespace

void QuotaClient::start()
{
    if (_running.load() || _task_handle != nullptr) {
        return;
    }

    auto cached = loadCachedSnapshot();
    if (cached.valid) {
        publish(std::move(cached));
    }

    Settings system_settings(kSystemSettingsNs, false);
    Settings codex_settings(kQuotaSettingsNs, false);
    const bool wifi_enabled = system_settings.GetBool(kWifiEnabledKey, codex_config::kDefaultWifiEnabled);
    const bool fallback_enabled = codex_settings.GetBool(kWifiQuotaFallbackKey,
                                                         codex_config::kDefaultWifiQuotaFallbackEnabled);
    if (!wifi_enabled || !fallback_enabled) {
        ESP_LOGI(kTag, "Wi-Fi quota fallback disabled");
        return;
    }

    _running.store(true);
    if (xTaskCreate(taskEntry, "codex_quota", codex_config::kQuotaTaskStackBytes, this, 4, &_task_handle) != pdPASS) {
        _running.store(false);
        _task_handle = nullptr;
    }
}

void QuotaClient::stop()
{
    _running.store(false);
}

QuotaSnapshot QuotaClient::snapshot()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _snapshot;
}

bool QuotaClient::ingestPanelJson(const std::string& body, bool wifiConnected, const char* source)
{
    QuotaSnapshot snapshot;
    snapshot.wifiConnected = wifiConnected;
    if (!parsePanelJson(body, snapshot)) {
        ESP_LOGW(kTag, "ignore invalid panel payload from %s", source ? source : "external");
        return false;
    }
    if (!snapshot.valid) {
        ESP_LOGI(kTag,
                 "ignore non-quota panel payload from %s status=%s",
                 source ? source : "external",
                 snapshot.status.c_str());
        return false;
    }

    saveCachedSnapshot(snapshot);
    publish(std::move(snapshot));
    return true;
}

void QuotaClient::taskEntry(void* arg)
{
    auto* client = static_cast<QuotaClient*>(arg);
    client->task();
    client->_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void QuotaClient::task()
{
    GetHAL().delay(codex_config::kQuotaFirstPollDelayMs);

    while (_running.load()) {
        publish(fetchOnce());

        const uint32_t start = GetHAL().millis();
        while (_running.load() && GetHAL().millis() - start < codex_config::kQuotaPollMs) {
            GetHAL().delay(250);
        }
    }
}

void QuotaClient::publish(QuotaSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    snapshot.sequence = ++_sequence;
    _snapshot         = std::move(snapshot);
}

QuotaSnapshot QuotaClient::fetchOnce()
{
    const bool stop_station_after_fetch = ensureWifiStarted();
    auto finish = [&](QuotaSnapshot result) {
        finishWifiCycle(stop_station_after_fetch);
        result.wifiConnected = WifiManager::GetInstance().IsConnected();
        return result;
    };

    QuotaSnapshot snapshot;
    auto& wifi = WifiManager::GetInstance();
    snapshot.wifiConnected = wifi.IsConnected();
    if (!snapshot.wifiConnected) {
        return finish(fallbackSnapshot(false, "offline", "Wi-Fi offline"));
    }

    ensureTimeSynced();

    if (fetchUrl(codex_config::kPrimaryPanelUrl, snapshot)) {
        if (snapshot.valid) {
            saveCachedSnapshot(snapshot);
            return finish(snapshot);
        }
        return finish(fallbackSnapshot(true, snapshot.status.c_str(), snapshot.message.c_str()));
    }
    if (fetchUrl(codex_config::kCompatPanelUrl, snapshot)) {
        if (snapshot.valid) {
            saveCachedSnapshot(snapshot);
            return finish(snapshot);
        }
        return finish(fallbackSnapshot(true, snapshot.status.c_str(), snapshot.message.c_str()));
    }
    return finish(fallbackSnapshot(true, "error", "Quota fetch failed"));
}

bool QuotaClient::ensureWifiStarted()
{
    Settings settings(kSystemSettingsNs, false);
    if (!settings.GetBool(kWifiEnabledKey, codex_config::kDefaultWifiEnabled)) {
        return false;
    }

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig config;
        config.ssid_prefix = codex_config::kWifiPortalPrefix;
        config.language = codex_config::kLanguage;
        wifi.Initialize(config);
    }
    const bool already_connected = wifi.IsConnected();
    bool started_by_client = false;
    if (!already_connected && !_station_requested) {
        wifi.StartStation();
        _station_requested = true;
        started_by_client = true;
    }

    if (!already_connected) {
        waitForWifiConnected(codex_config::kWifiConnectWaitMs);
    }

    if (wifi.IsConnected()) {
        _station_requested = false;
    }

    return started_by_client && wifi.IsConnected();
}

bool QuotaClient::waitForWifiConnected(uint32_t timeoutMs)
{
    auto& wifi = WifiManager::GetInstance();
    const uint32_t start = GetHAL().millis();
    while (_running.load() && !wifi.IsConnected() && GetHAL().millis() - start < timeoutMs) {
        GetHAL().delay(250);
    }
    return wifi.IsConnected();
}

void QuotaClient::finishWifiCycle(bool stopStation)
{
    if (!stopStation) {
        return;
    }

    WifiManager::GetInstance().StopStation();
    _station_requested = false;
}

void QuotaClient::ensureTimeSynced()
{
    if (systemTimeLooksValid() || _time_sync_attempted) {
        return;
    }

    _time_sync_attempted = true;
    setenv("TZ", "CST-8", 1);
    tzset();

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
    } else {
        esp_sntp_restart();
    }

    for (int i = 0; i < 40 && _running.load(); ++i) {
        if (systemTimeLooksValid()) {
            ESP_LOGI(kTag, "SNTP time synced");
            GetHAL().syncSystemTimeToRtc();
            return;
        }
        GetHAL().delay(250);
    }

    ESP_LOGW(kTag, "SNTP time sync timeout");
}

bool QuotaClient::fetchUrl(const char* url, QuotaSnapshot& snapshot)
{
    ESP_LOGI(kTag,
             "GET %s free=%u internal=%u",
             url,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

    esp_http_client_config_t config = {};
    config.url                     = url;
    config.timeout_ms              = codex_config::kHttpTimeoutMs;
    config.crt_bundle_attach       = esp_crt_bundle_attach;
    config.keep_alive_enable       = false;
    config.buffer_size             = codex_config::kHttpRxBufferBytes;
    config.buffer_size_tx          = codex_config::kHttpTxBufferBytes;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(kTag, "http client init failed: %s", url);
        return false;
    }

    bool ok = false;
    const esp_err_t open_result = esp_http_client_open(client, 0);
    if (open_result == ESP_OK) {
        const int content_length = esp_http_client_fetch_headers(client);
        std::string body;
        if (content_length > 0) {
            body.reserve(std::min(content_length, codex_config::kMaxPanelBodyBytes));
        }

        char buffer[512];
        while (static_cast<int>(body.size()) < codex_config::kMaxPanelBodyBytes) {
            const int read = esp_http_client_read(client, buffer, sizeof(buffer));
            if (read <= 0) {
                break;
            }
            body.append(buffer, read);
        }

        const int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300 && !body.empty()) {
            ok = parsePanelJson(body, snapshot);
            ESP_LOGI(kTag, "GET %s status=%d body=%d parsed=%d", url, status, static_cast<int>(body.size()), ok);
        } else {
            ESP_LOGW(kTag,
                     "GET %s failed, status=%d len=%d body=%d",
                     url,
                     status,
                     content_length,
                     static_cast<int>(body.size()));
        }
    } else {
        ESP_LOGW(kTag, "GET %s open failed: %s", url, esp_err_to_name(open_result));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool QuotaClient::parsePanelJson(const std::string& body, QuotaSnapshot& snapshot)
{
    JsonDocument doc;
    const auto error = deserializeJson(doc, body);
    if (error) {
        ESP_LOGW(kTag, "panel json parse failed: %s", error.c_str());
        return false;
    }

    JsonVariantConst root = doc.as<JsonVariantConst>();
    applyPanelTime(root);

    JsonVariantConst codex = doc["codex"];
    if (codex.isNull()) {
        codex = firstObject(root, "data", "codex");
    }
    if (codex.isNull()) {
        codex = firstObject(root, "dashboard", "codex");
    }
    if (codex.isNull()) {
        codex = firstObject(root, "data", "dashboard", "codex");
    }
    if (codex.isNull()) {
        ESP_LOGW(kTag, "panel json missing codex block");
        return false;
    }

    snapshot.status          = jsonString(codex["status"], "unknown");
    snapshot.valid           = jsonBool(codex["valid"], snapshot.status == "ok") && snapshot.status != "error";
    snapshot.source          = jsonString(codex["source"], snapshot.source.c_str());
    snapshot.updatedAt       = jsonString(codex["updated_at"], snapshot.updatedAt.c_str());
    snapshot.stale           = jsonBool(codex["stale"], false);
    JsonVariantConst host    = codex["host"];
    if (!host.isNull()) {
        snapshot.hostName = jsonString(host["name"], snapshot.hostName.c_str());
    }
    snapshot.weeklyLeftPct = snapshot.valid ? quotaLeftPct(codex["weekly"]) : -1;
    if (snapshot.weeklyLeftPct < 0 && snapshot.valid) {
        snapshot.weeklyLeftPct = normalizedPercent(codex["weekly_left_pct"]);
    }
    snapshot.weeklyUsage = jsonString(codex["weekly"]["reset_in"], nullptr);
    if (snapshot.weeklyUsage.empty()) {
        snapshot.weeklyUsage = jsonString(codex["weekly_usage"], "--");
    }
    JsonVariantConst dailyTracking = codex["weekly"]["daily_tracking"];
    if (!dailyTracking.isNull()) {
        snapshot.weeklyDayStartLeftPct = normalizedPercent(dailyTracking["day_start_left_pct"]);
        snapshot.weeklySegmentStartLeftPct = normalizedPercent(dailyTracking["segment_start_left_pct"]);
        snapshot.weeklyTodayUsedPctPoints = static_cast<int>(
            std::max<int64_t>(0, jsonInt64(dailyTracking["used_since_start_pct_points"], 0)));
        snapshot.weeklyResetCount = static_cast<int>(
            std::max<int64_t>(0, jsonInt64(dailyTracking["reset_count"], 0)));
        snapshot.weeklyTrackingPeriodStart = jsonString(dailyTracking["period_start"], "");
    }
    snapshot.petState        = jsonString(codex["pet_state"], jsonString(codex["state"], "idle").c_str());

    JsonVariantConst pet = doc["pet"];
    if (pet.isNull()) {
        pet = doc["dashboard"]["pet"];
    }
    if (pet.isNull()) {
        pet = doc["data"]["pet"];
    }
    if (!pet.isNull()) {
        snapshot.petState = jsonString(pet["state"], snapshot.petState.c_str());
        if (snapshot.petState == "idle") {
            snapshot.petState = jsonString(pet["status"], snapshot.petState.c_str());
        }
    }
    snapshot.processing = jsonBool(codex["processing"], false) || jsonBool(pet["processing"], false) ||
                          isProcessingState(snapshot.petState);

    JsonVariantConst home = codex["openwatcher_home"];
    if (!home.isNull()) {
        snapshot.sessionTitle        = jsonString(home["session_title"], snapshot.sessionTitle.c_str());
        snapshot.contextLabel        = jsonString(home["context_label"], snapshot.contextLabel.c_str());
        snapshot.contextPressurePct  = normalizedPercent(home["context_pressure_pct"]);
        snapshot.compactThresholdPct = normalizedPercent(home["compact_threshold_pct"]);
        snapshot.compactWarning      = jsonBool(home["compact_warning"], false);
        snapshot.totalTokensLabel    = jsonString(home["total_tokens_label"], snapshot.totalTokensLabel.c_str());
        snapshot.modelLabel          = jsonString(home["model_label"], snapshot.modelLabel.c_str());
        snapshot.reasoningLabel      = jsonString(home["reasoning_label"], snapshot.reasoningLabel.c_str());
        snapshot.activityLabel       = jsonString(home["activity_label"], snapshot.activityLabel.c_str());
        snapshot.activityLive        = jsonBool(home["activity_live"], false);
        JsonArrayConst buckets       = home["activity_buckets"].as<JsonArrayConst>();
        size_t index = 0;
        for (JsonVariantConst bucket : buckets) {
            if (index >= snapshot.activityBuckets.size()) {
                break;
            }
            snapshot.activityBuckets[index] = std::clamp(jsonFloat(bucket, 0.0f), 0.0f, 1.0f);
            ++index;
        }
    }

    if (snapshot.valid) {
        snapshot.message = jsonString(codex["message"], snapshot.processing ? "Codex processing" : "Quota synced");
    } else {
        snapshot.message = jsonString(codex["message"], "Quota invalid");
        ESP_LOGW(kTag, "codex quota invalid, status=%s", snapshot.status.c_str());
    }

    return true;
}

QuotaSnapshot QuotaClient::loadCachedSnapshot()
{
    Settings settings(kQuotaSettingsNs, false);
    if (!settings.GetBool("quota_cached", false)) {
        return {};
    }

    QuotaSnapshot snapshot;
    snapshot.valid           = true;
    snapshot.cached          = true;
    snapshot.weeklyLeftPct   = settings.GetInt("week_pct", -1);
    snapshot.weeklyUsage     = settings.GetString("week_usage", "--");
    snapshot.weeklyDayStartLeftPct = settings.GetInt("week_day_start", -1);
    snapshot.weeklySegmentStartLeftPct = settings.GetInt(kWeekSegmentStartKey, -1);
    snapshot.weeklyTodayUsedPctPoints = settings.GetInt("week_today_used", 0);
    snapshot.weeklyResetCount = settings.GetInt(kWeekResetCountKey, 0);
    snapshot.weeklyTrackingPeriodStart = settings.GetString(kWeekPeriodStartKey, "");
    snapshot.status          = settings.GetString("status", "cached");
    snapshot.petState        = settings.GetString("pet_state", "idle");
    snapshot.processing      = false;
    snapshot.message         = "Cached quota";
    snapshot.source          = settings.GetString("source", "");
    snapshot.updatedAt       = settings.GetString("updated_at", "");
    snapshot.stale           = true;
    snapshot.hostName        = settings.GetString("host_name", "");
    snapshot.sessionTitle    = settings.GetString("session_title", "Codex Ready");
    snapshot.contextLabel    = settings.GetString("context_label", "-- / --");
    snapshot.contextPressurePct = settings.GetInt(kContextPressureKey, 0);
    snapshot.compactThresholdPct = settings.GetInt(kCompactThresholdKey, -1);
    snapshot.compactWarning  = settings.GetBool("compact_warning", false);
    snapshot.totalTokensLabel = settings.GetString(kTotalTokensLabelKey, "--");
    snapshot.modelLabel      = settings.GetString("model_label", "--");
    snapshot.reasoningLabel  = settings.GetString("reasoning_label", "--");
    snapshot.activityLabel   = settings.GetString("activity_label", "--");
    snapshot.activityLive    = false;
    for (size_t i = 0; i < snapshot.activityBuckets.size(); ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "act_%02u", static_cast<unsigned>(i));
        snapshot.activityBuckets[i] = static_cast<float>(settings.GetInt(key, 0)) / 100.0f;
    }

    if (!snapshotHasQuota(snapshot)) {
        return {};
    }
    return snapshot;
}

void QuotaClient::saveCachedSnapshot(const QuotaSnapshot& snapshot)
{
    if (!snapshot.valid || !snapshotHasQuota(snapshot)) {
        return;
    }

    Settings settings(kQuotaSettingsNs, true);
    settings.SetBool("quota_cached", true);
    settings.SetInt("week_pct", snapshot.weeklyLeftPct);
    settings.SetString("week_usage", snapshot.weeklyUsage);
    settings.SetInt("week_day_start", snapshot.weeklyDayStartLeftPct);
    settings.SetInt(kWeekSegmentStartKey, snapshot.weeklySegmentStartLeftPct);
    settings.SetInt("week_today_used", snapshot.weeklyTodayUsedPctPoints);
    settings.SetInt(kWeekResetCountKey, snapshot.weeklyResetCount);
    settings.SetString(kWeekPeriodStartKey, snapshot.weeklyTrackingPeriodStart);
    settings.SetString("status", snapshot.status);
    settings.SetString("pet_state", snapshot.petState);
    settings.SetString("source", snapshot.source);
    settings.SetString("updated_at", snapshot.updatedAt);
    settings.SetString("host_name", snapshot.hostName);
    settings.SetString("session_title", snapshot.sessionTitle);
    settings.SetString("context_label", snapshot.contextLabel);
    settings.SetInt(kContextPressureKey, snapshot.contextPressurePct);
    settings.SetInt(kCompactThresholdKey, snapshot.compactThresholdPct);
    settings.SetBool("compact_warning", snapshot.compactWarning);
    settings.SetString(kTotalTokensLabelKey, snapshot.totalTokensLabel);
    settings.SetString("model_label", snapshot.modelLabel);
    settings.SetString("reasoning_label", snapshot.reasoningLabel);
    settings.SetString("activity_label", snapshot.activityLabel);
    for (size_t i = 0; i < snapshot.activityBuckets.size(); ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "act_%02u", static_cast<unsigned>(i));
        settings.SetInt(key, static_cast<int>(std::clamp(snapshot.activityBuckets[i], 0.0f, 1.0f) * 100.0f + 0.5f));
    }
}

QuotaSnapshot QuotaClient::fallbackSnapshot(bool wifiConnected, const char* status, const char* message)
{
    auto cached = loadCachedSnapshot();
    if (cached.valid) {
        cached.wifiConnected = wifiConnected;
        cached.status        = status;
        cached.message       = message;
        cached.cached        = true;
        return cached;
    }

    QuotaSnapshot snapshot;
    snapshot.wifiConnected = wifiConnected;
    snapshot.status        = status;
    snapshot.message       = message;
    return snapshot;
}

}  // namespace codex
