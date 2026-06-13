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
#include <mutex>
#include <string>

namespace codex {

struct QuotaSnapshot {
    bool valid                 = false;
    bool cached                = false;
    bool wifiConnected         = false;
    bool processing            = false;
    int fiveHourLeftPct        = -1;
    int weeklyLeftPct          = -1;
    std::string fiveHourUsage  = "--";
    std::string weeklyUsage    = "--";
    std::string status         = "idle";
    std::string petState       = "idle";
    std::string message        = "Quota waiting";
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
