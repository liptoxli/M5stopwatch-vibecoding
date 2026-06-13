/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_codex.h"
#include <apps/common/status_bar/status_bar.h>
#include <assets/assets.h>
#include <hal/ble_bridge.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>

using namespace mooncake;

namespace {

constexpr uint32_t kIdleViewFrameMs = 50;
constexpr uint32_t kActiveViewFrameMs = 33;

}  // namespace

AppCodex::AppCodex()
{
    setAppInfo().name = "Codex";
    setAppInfo().icon = (void*)&icon_codex;
}

void AppCodex::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodex::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    _applied_quota_sequence = 0;
    _applied_ble_status_sequence = 0;
    _applied_host_voice_sequence = ble_bridge::host_voice_sequence();
    _applied_host_panel_sequence = ble_bridge::host_panel_sequence();
    _applied_ble_connected = ble_bridge::is_connected();
    _voice_active = false;
    _applied_voice_active = false;
    _voice_mode = view::CodexView::VoiceMode::Idle;
    _applied_voice_mode = view::CodexView::VoiceMode::Idle;
    _voice_mode_since_ms = GetHAL().millis();
    _last_view_update_ms = 0;
    _last_battery_check_ms = GetHAL().millis();
    _quota_client.start();

    LvglLockGuard lock;

    _view = std::make_unique<view::CodexView>();
    _view->init(lv_screen_active());
    view::create_status_bar(0x0B2030, 0x7DD8FF, true);
}

void AppCodex::onRunning()
{
    if (_key_manager) {
        const auto key_event = _key_manager->update();
        if (key_event == input::KeyEvent::GoHome ||
            (GetHAL().btnA.isPressed() && GetHAL().btnB.isPressed())) {
            close();
            return;
        }
    }

    handleBluetoothKeys();
    if (_view && _view->consumeClearInputRequest()) {
        _voice_active = false;
        _voice_mode = view::CodexView::VoiceMode::Idle;
        _voice_mode_since_ms = GetHAL().millis();
        ble_bridge::send_shake_action();
    }

    const uint32_t host_voice_sequence = ble_bridge::host_voice_sequence();
    if (ble_bridge::host_voice_valid() && host_voice_sequence != _applied_host_voice_sequence) {
        const auto previous_mode = _voice_mode;
        _voice_active = ble_bridge::host_voice_active();
        switch (ble_bridge::host_voice_phase()) {
        case ble_bridge::VoicePhase::Processing:
            _voice_mode = view::CodexView::VoiceMode::Processing;
            break;
        case ble_bridge::VoicePhase::Recording:
            _voice_mode = view::CodexView::VoiceMode::Recording;
            break;
        case ble_bridge::VoicePhase::Idle:
        default:
            _voice_mode = view::CodexView::VoiceMode::Idle;
            break;
        }
        if (_voice_mode != previous_mode) {
            _voice_mode_since_ms = GetHAL().millis();
        }
        _applied_host_voice_sequence = host_voice_sequence;
        mclog::tagDebug(getAppInfo().name, "Typeless host voice correction: {}", _voice_active);
    }

    const uint32_t now = GetHAL().millis();
    if (_voice_mode == view::CodexView::VoiceMode::Processing &&
        now - _voice_mode_since_ms > 5000) {
        _voice_active = false;
        _voice_mode = view::CodexView::VoiceMode::Idle;
        _voice_mode_since_ms = now;
        mclog::tagDebug(getAppInfo().name, "Typeless local processing timeout");
    }

    const uint32_t host_panel_sequence = ble_bridge::host_panel_sequence();
    if (ble_bridge::host_panel_valid() && host_panel_sequence != _applied_host_panel_sequence) {
        if (_quota_client.ingestPanelJson(ble_bridge::host_panel_json(), false, "ble")) {
            mclog::tagDebug(getAppInfo().name, "Codex panel ingested from BLE: seq={}", host_panel_sequence);
        }
        _applied_host_panel_sequence = host_panel_sequence;
    }

    const auto quota_snapshot = _quota_client.snapshot();
    bool view_state_changed = false;

    LvglLockGuard lock;

    if (_view) {
        if (quota_snapshot.sequence != 0 && quota_snapshot.sequence != _applied_quota_sequence) {
            _view->applyQuota(quota_snapshot.fiveHourLeftPct,
                              quota_snapshot.weeklyLeftPct,
                              quota_snapshot.fiveHourUsage,
                              quota_snapshot.weeklyUsage,
                              quota_snapshot.valid,
                              quota_snapshot.wifiConnected,
                              quota_snapshot.processing,
                              quota_snapshot.message);
            _applied_quota_sequence = quota_snapshot.sequence;
            view_state_changed = true;
        }

        const bool ble_connected = ble_bridge::is_connected();
        const uint32_t ble_status_sequence = ble_bridge::host_status_sequence();
        if (ble_connected != _applied_ble_connected || ble_status_sequence != _applied_ble_status_sequence) {
            _view->applyBleState(ble_connected,
                                 ble_bridge::host_status_text(),
                                 ble_status_sequence != _applied_ble_status_sequence);
            _applied_ble_connected = ble_connected;
            _applied_ble_status_sequence = ble_status_sequence;
            view_state_changed = true;
        }
        if (_voice_active != _applied_voice_active || _voice_mode != _applied_voice_mode) {
            _view->setVoiceMode(_voice_mode);
            _applied_voice_active = _voice_active;
            _applied_voice_mode = _voice_mode;
            view_state_changed = true;
        }

        const uint32_t frame_ms = _voice_mode == view::CodexView::VoiceMode::Idle ? kIdleViewFrameMs : kActiveViewFrameMs;
        if (view_state_changed || now - _last_view_update_ms >= frame_ms) {
            _view->update();
            _last_view_update_ms = now;
        }
    }
    updateBatteryStatusBar(now);
    view::update_status_bar();
}

void AppCodex::updateBatteryStatusBar(uint32_t now)
{
    if (!view::is_status_bar_created()) {
        return;
    }

    if (now - _last_battery_check_ms < 1000) {
        return;
    }
    _last_battery_check_ms = now;

    if (GetHAL().getBatteryLevel() < 20) {
        view::show_status_bar(5000, true);
    }
}

void AppCodex::handleBluetoothKeys()
{
    auto& hal = GetHAL();

    if (hal.btnA.wasPressed()) {
        _voice_active = true;
        _voice_mode = view::CodexView::VoiceMode::Recording;
        _voice_mode_since_ms = GetHAL().millis();
        mclog::tagDebug(getAppInfo().name, "BLE key A: primary input down");
        ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Down);
    }

    if (hal.btnA.wasReleased()) {
        if (_voice_active) {
            _voice_active = false;
            _voice_mode = view::CodexView::VoiceMode::Idle;
            _voice_mode_since_ms = GetHAL().millis();
        }
        mclog::tagDebug(getAppInfo().name, "BLE key A: primary input up");
        ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Up);
    }

    if (hal.btnB.wasPressed()) {
        if (_voice_active) {
            _voice_active = false;
            _voice_mode = view::CodexView::VoiceMode::Idle;
            _voice_mode_since_ms = GetHAL().millis();
            mclog::tagDebug(getAppInfo().name, "BLE key B: Enter tap while Typeless voice is active");
        } else {
            mclog::tagDebug(getAppInfo().name, "BLE key B: Enter tap");
        }
        ble_bridge::send_codex_enter();
    }
}

void AppCodex::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_voice_active) {
        ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Up);
    }
    _key_manager.reset();
    _voice_active = false;
    _applied_voice_active = false;
    _voice_mode = view::CodexView::VoiceMode::Idle;
    _applied_voice_mode = view::CodexView::VoiceMode::Idle;
    _voice_mode_since_ms = GetHAL().millis();
    _last_battery_check_ms = 0;
    _quota_client.stop();

    LvglLockGuard lock;

    view::destroy_status_bar();
    _view.reset();
}
