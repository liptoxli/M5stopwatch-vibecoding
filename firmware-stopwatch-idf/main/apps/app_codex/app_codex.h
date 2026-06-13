/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "codex_quota_client.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

class AppCodex : public mooncake::AppAbility {
public:
    AppCodex();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void handleBluetoothKeys();
    void updateBatteryStatusBar(uint32_t now);

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::CodexView> _view;
    codex::QuotaClient _quota_client;
    uint32_t _applied_quota_sequence = 0;
    uint32_t _applied_ble_status_sequence = 0;
    uint32_t _applied_host_voice_sequence = 0;
    uint32_t _applied_host_panel_sequence = 0;
    uint32_t _last_view_update_ms = 0;
    uint32_t _last_battery_check_ms = 0;
    bool _applied_ble_connected = false;
    bool _voice_active = false;
    bool _applied_voice_active = false;
    uint32_t _voice_mode_since_ms = 0;
    view::CodexView::VoiceMode _voice_mode = view::CodexView::VoiceMode::Idle;
    view::CodexView::VoiceMode _applied_voice_mode = view::CodexView::VoiceMode::Idle;
};
