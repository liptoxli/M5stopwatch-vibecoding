/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <array>
#include <mutex>
#include <string>

namespace codex {

struct QuotaSnapshot {
    bool valid                 = false;
    bool cached                = false;
    bool wifiConnected         = false;
    bool processing            = false;
    int weeklyLeftPct          = -1;
    std::string weeklyUsage    = "--";
    int weeklyDayStartLeftPct  = -1;
    int weeklySegmentStartLeftPct = -1;
    int weeklyTodayUsedPctPoints = 0;
    int weeklyResetCount       = 0;
    std::string weeklyTrackingPeriodStart = "";
    std::string status         = "idle";
    std::string petState       = "idle";
    std::string message        = "Quota waiting";
    std::string source         = "";
    std::string updatedAt      = "";
    bool stale                 = false;
    std::string hostName       = "";
    std::string sessionTitle   = "Codex Ready";
    std::string contextLabel   = "-- / --";
    int contextPressurePct     = 0;
    int compactThresholdPct    = -1;
    bool compactWarning        = false;
    std::string totalTokensLabel = "--";
    std::string modelLabel     = "--";
    std::string reasoningLabel = "--";
    std::string activityLabel  = "--";
    bool activityLive          = false;
    std::array<float, 24> activityBuckets = {};
    uint32_t sequence          = 0;
};

class QuotaClient {
public:
    void start();
    void stop();
    QuotaSnapshot snapshot();
    bool ingestPanelJson(const std::string& body, bool wifiConnected, const char* source);

private:
    static void taskEntry(void* arg);
    void task();
    void publish(QuotaSnapshot snapshot);
    QuotaSnapshot fetchOnce();
    bool fetchUrl(const char* url, QuotaSnapshot& snapshot);
    bool parsePanelJson(const std::string& body, QuotaSnapshot& snapshot);
    bool ensureWifiStarted();
    bool waitForWifiConnected(uint32_t timeoutMs);
    void finishWifiCycle(bool stopStation);
    void ensureTimeSynced();
    QuotaSnapshot loadCachedSnapshot();
    void saveCachedSnapshot(const QuotaSnapshot& snapshot);
    QuotaSnapshot fallbackSnapshot(bool wifiConnected, const char* status, const char* message);

    std::mutex _mutex;
    QuotaSnapshot _snapshot;
    TaskHandle_t _task_handle = nullptr;
    std::atomic_bool _running{false};
    bool _station_requested   = false;
    bool _time_sync_attempted = false;
    uint32_t _sequence        = 0;
};

}  // namespace codex
