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
#include <hal/utils/settings/settings.h>
#include <mooncake.h>
#include <mooncake_log.h>

using namespace mooncake;

namespace {

constexpr uint32_t kStatusBarFrameMs = 50;
constexpr const char* kCodexSettingsNs = "codex";
constexpr const char* kThemeKey = "theme";
constexpr const char* kThemeOpenWatcherV2 = "openwatcher_v2";

view::CodexView::ThemeMode load_codex_theme_mode()
{
    Settings settings(kCodexSettingsNs, false);
    const std::string theme = settings.GetString(kThemeKey, "official_v1");
    if (theme == kThemeOpenWatcherV2) {
        return view::CodexView::ThemeMode::OpenWatcherV2;
    }
    return view::CodexView::ThemeMode::OfficialV1;
}

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
    // Re-apply the current host state after the app opens. The Bridge can
    // reconnect and deliver an interruption while the launcher is still on
    // screen, and that latched fault must not be mistaken for an old update.
    _applied_host_voice_sequence = 0;
    _applied_host_unread_sequence = 0;
    _applied_host_panel_sequence = ble_bridge::host_panel_sequence();
    _applied_ble_connected = ble_bridge::is_connected();
    _voice_active = false;
    _applied_voice_active = false;
    _confirm_long_sent = false;
    _confirm_routes_to_primary = false;
    _voice_session_interrupted = false;
    _voice_mode = view::CodexView::VoiceMode::Idle;
    _applied_voice_mode = view::CodexView::VoiceMode::Idle;
    _voice_mode_since_ms = GetHAL().millis();
    _fault_first_vibration_at_ms = 0;
    _fault_second_vibration_at_ms = 0;
    _last_view_update_ms = 0;
    _last_status_bar_update_ms = 0;
    _last_battery_check_ms = GetHAL().millis();
    _quota_client.start();

    LvglLockGuard lock;

    _view = std::make_unique<view::CodexView>();
    _view->init(lv_screen_active(), load_codex_theme_mode());
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
    const uint32_t now = GetHAL().millis();
    if (ble_bridge::voice_session_interrupted()) {
        enterVoiceInterrupted(now);
    }
    if (_fault_first_vibration_at_ms != 0 &&
        static_cast<int32_t>(now - _fault_first_vibration_at_ms) >= 0) {
        GetHAL().vibrate(160, 100);
        _fault_first_vibration_at_ms = 0;
        mclog::tagInfo(getAppInfo().name, "Voice interruption haptic 1/2");
    } else if (_fault_second_vibration_at_ms != 0 &&
        static_cast<int32_t>(now - _fault_second_vibration_at_ms) >= 0) {
        GetHAL().vibrate(160, 100);
        _fault_second_vibration_at_ms = 0;
        mclog::tagInfo(getAppInfo().name, "Voice interruption haptic 2/2");
    }
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
        case ble_bridge::VoicePhase::Interrupted:
            enterVoiceInterrupted(now);
            break;
        case ble_bridge::VoicePhase::Processing:
            if (!_voice_session_interrupted) {
                _voice_mode = view::CodexView::VoiceMode::Processing;
            }
            break;
        case ble_bridge::VoicePhase::Recording:
            if (!_voice_session_interrupted) {
                _voice_mode = view::CodexView::VoiceMode::Recording;
            }
            break;
        case ble_bridge::VoicePhase::Idle:
        default:
            if (!_voice_session_interrupted) {
                _voice_mode = view::CodexView::VoiceMode::Idle;
            }
            break;
        }
        if (_voice_mode != previous_mode) {
            _voice_mode_since_ms = GetHAL().millis();
        }
        if (_voice_session_interrupted) {
            _voice_active = false;
        }
        ble_bridge::set_voice_capture_active(_voice_mode == view::CodexView::VoiceMode::Recording);
        _applied_host_voice_sequence = host_voice_sequence;
        mclog::tagDebug(getAppInfo().name, "Typeless host voice correction: {}", _voice_active);
    }

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
    const bool quota_changed = quota_snapshot.sequence != 0 &&
                               quota_snapshot.sequence != _applied_quota_sequence;
    const bool ble_connected = ble_bridge::is_connected();
    const uint32_t ble_status_sequence = ble_bridge::host_status_sequence();
    const bool ble_changed = ble_connected != _applied_ble_connected ||
                             ble_status_sequence != _applied_ble_status_sequence;
    const bool voice_changed = _voice_active != _applied_voice_active ||
                               _voice_mode != _applied_voice_mode;
    const uint32_t host_unread_sequence = ble_bridge::host_codex_unread_sequence();
    const bool unread_changed = ble_bridge::host_codex_unread_valid() &&
                                host_unread_sequence != _applied_host_unread_sequence;
    const uint32_t frame_ms = _view ? _view->frameIntervalMs() : 100;
    const bool view_due = _view && now - _last_view_update_ms >= frame_ms;
    const bool status_bar_due = now - _last_status_bar_update_ms >= kStatusBarFrameMs;

    if (!quota_changed && !ble_changed && !voice_changed && !unread_changed && !view_due && !status_bar_due) {
        return;
    }

    bool view_state_changed = false;

    LvglLockGuard lock;

    if (_view) {
        if (quota_changed) {
            _view->applySnapshot(quota_snapshot);
            _applied_quota_sequence = quota_snapshot.sequence;
            view_state_changed = true;
        }

        if (ble_changed) {
            _view->applyBleState(ble_connected,
                                 ble_bridge::host_status_text(),
                                 ble_status_sequence != _applied_ble_status_sequence);
            _applied_ble_connected = ble_connected;
            _applied_ble_status_sequence = ble_status_sequence;
            view_state_changed = true;
        }
        if (voice_changed) {
            _view->setVoiceMode(_voice_mode);
            _applied_voice_active = _voice_active;
            _applied_voice_mode = _voice_mode;
            view_state_changed = true;
        }
        if (unread_changed) {
            _view->setUnreadTaskCount(ble_bridge::host_codex_unread_count());
            _applied_host_unread_sequence = host_unread_sequence;
            view_state_changed = true;
        }

        if (view_state_changed || view_due) {
            _view->update();
            _last_view_update_ms = now;
        }
    }
    if (status_bar_due) {
        updateBatteryStatusBar(now);
        view::update_status_bar();
        _last_status_bar_update_ms = now;
    }
}

void AppCodex::enterVoiceInterrupted(uint32_t now)
{
    if (_voice_session_interrupted) {
        return;
    }
    _voice_session_interrupted = true;
    _voice_active = false;
    _voice_mode = view::CodexView::VoiceMode::Interrupted;
    _voice_mode_since_ms = now;
    ble_bridge::set_voice_capture_active(false);
    _fault_first_vibration_at_ms = now + 300;
    _fault_second_vibration_at_ms = now + 760;
    mclog::tagInfo(getAppInfo().name, "Voice session interrupted; waiting for retry");
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

bool AppCodex::shouldRouteConfirmAsPrimary() const
{
    if (!ble_bridge::is_typeless_input_mode()) {
        return false;
    }
    const bool host_voice_busy = ble_bridge::host_voice_valid() &&
                                 ble_bridge::host_voice_phase() != ble_bridge::VoicePhase::Idle;
    return _voice_session_interrupted ||
           ble_bridge::voice_session_interrupted() ||
           host_voice_busy ||
           _voice_active ||
           _voice_mode != view::CodexView::VoiceMode::Idle;
}

void AppCodex::handlePrimaryInputDown(const char* sourceKey)
{
    const uint32_t now = GetHAL().millis();
    const bool typeless_mode = ble_bridge::is_typeless_input_mode();
    if (_voice_session_interrupted) {
        if (!ble_bridge::is_connected()) {
            GetHAL().vibrate(90, 120);
            mclog::tagDebug(getAppInfo().name, "Voice retry waiting for BLE via {}", sourceKey);
            return;
        }
        _voice_session_interrupted = false;
        _fault_first_vibration_at_ms = 0;
        _fault_second_vibration_at_ms = 0;
        ble_bridge::clear_voice_session_interruption();
        _voice_active = false;
        mclog::tagInfo(getAppInfo().name, "Voice interruption cleared by {} retry", sourceKey);
    }
    const bool starting_voice = !typeless_mode || !_voice_active;
    _primary_input_down_ms = now;
    if (typeless_mode && _voice_active) {
        _voice_mode = view::CodexView::VoiceMode::Processing;
    } else {
        _voice_active = true;
        _voice_mode = view::CodexView::VoiceMode::Recording;
    }
    _voice_mode_since_ms = now;
    ble_bridge::set_voice_capture_active(starting_voice);
    mclog::tagDebug(getAppInfo().name, "BLE key {}: primary input down", sourceKey);
    ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Down);
}

void AppCodex::handlePrimaryInputUp(const char* sourceKey)
{
    const bool typeless_mode = ble_bridge::is_typeless_input_mode();
    _primary_input_down_ms = 0;
    if (!typeless_mode && _voice_active) {
        _voice_active = false;
        _voice_mode = view::CodexView::VoiceMode::Idle;
        _voice_mode_since_ms = GetHAL().millis();
        ble_bridge::set_voice_capture_active(false);
    }
    mclog::tagDebug(getAppInfo().name, "BLE key {}: primary input up", sourceKey);
    ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Up);
}

void AppCodex::handleBluetoothKeys()
{
    auto& hal = GetHAL();

    if (hal.btnA.wasPressed()) {
        handlePrimaryInputDown("A");
    }

    if (hal.btnA.wasReleased()) {
        handlePrimaryInputUp("A");
    }

    if (hal.btnB.wasPressed()) {
        _confirm_long_sent = false;
        // Interaction policy shared by both UI themes: while Typeless is busy,
        // B is a second primary key. Confirm actions are available only at idle.
        _confirm_routes_to_primary = shouldRouteConfirmAsPrimary();
        if (_confirm_routes_to_primary) {
            mclog::tagDebug(getAppInfo().name, "BLE key B: routed to primary interaction");
            handlePrimaryInputDown("B");
        } else {
            mclog::tagDebug(getAppInfo().name, "BLE key B: confirm down");
        }
    }

    if (hal.btnB.isPressed() && !_confirm_long_sent && hal.btnB.pressedFor(kConfirmLongPressMs)) {
        if (!_confirm_routes_to_primary && shouldRouteConfirmAsPrimary()) {
            _confirm_routes_to_primary = true;
            mclog::tagDebug(getAppInfo().name, "BLE key B: late-routed to primary interaction");
            handlePrimaryInputDown("B");
        }
        if (_confirm_routes_to_primary) {
            return;
        }
        _confirm_long_sent = true;
        if (_voice_active) {
            _voice_active = false;
            _voice_mode = view::CodexView::VoiceMode::Idle;
            _voice_mode_since_ms = GetHAL().millis();
        }
        ble_bridge::set_voice_capture_active(false);
        mclog::tagDebug(getAppInfo().name, "BLE key B: confirm long press");
        ble_bridge::send_confirm_long_press();
    }

    if (hal.btnB.wasReleased()) {
        if (_confirm_routes_to_primary) {
            handlePrimaryInputUp("B");
            _confirm_routes_to_primary = false;
            mclog::tagDebug(getAppInfo().name, "BLE key B: primary interaction complete");
            return;
        }
        if (_confirm_long_sent) {
            _confirm_long_sent = false;
            mclog::tagDebug(getAppInfo().name, "BLE key B: confirm release after long press");
            return;
        }
        if (shouldRouteConfirmAsPrimary()) {
            mclog::tagDebug(getAppInfo().name, "BLE key B: release safety-routed to primary interaction");
            handlePrimaryInputDown("B");
            handlePrimaryInputUp("B");
            return;
        }
        if (_voice_active) {
            _voice_active = false;
            _voice_mode = view::CodexView::VoiceMode::Idle;
            _voice_mode_since_ms = GetHAL().millis();
            mclog::tagDebug(getAppInfo().name, "BLE key B: Enter tap while Typeless voice is active");
        } else {
            mclog::tagDebug(getAppInfo().name, "BLE key B: Enter tap");
        }
        ble_bridge::set_voice_capture_active(false);
        ble_bridge::send_codex_enter();
    }
}

void AppCodex::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_voice_active || _confirm_routes_to_primary) {
        ble_bridge::send_typeless_option(ble_bridge::ButtonAction::Up);
    }
    ble_bridge::set_voice_capture_active(false);
    _key_manager.reset();
    _voice_active = false;
    _applied_voice_active = false;
    _confirm_routes_to_primary = false;
    _voice_mode = view::CodexView::VoiceMode::Idle;
    _applied_voice_mode = view::CodexView::VoiceMode::Idle;
    _voice_mode_since_ms = GetHAL().millis();
    _fault_first_vibration_at_ms = 0;
    _fault_second_vibration_at_ms = 0;
    ble_bridge::clear_voice_session_interruption();
    _last_battery_check_ms = 0;
    _last_status_bar_update_ms = 0;
    _quota_client.stop();

    LvglLockGuard lock;

    view::destroy_status_bar();
    _view.reset();
}
