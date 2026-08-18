/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "../codex_config.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <ctime>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr uint32_t kColorClock   = 0xEEF2FF;
constexpr uint32_t kColorWeek    = 0x78DFF5;
constexpr uint32_t kColorToday   = 0x397FA6;
constexpr uint32_t kColorTrack   = 0x1D3442;
constexpr uint32_t kColorWeekText = 0xA5EDFF;
constexpr uint32_t kColorTodayText = 0x64A9CA;
constexpr uint32_t kColorText    = 0xEEF2FF;
constexpr uint32_t kColorTextDim = 0x8C96B3;
constexpr uint32_t kColorPetBody = 0x172847;
constexpr uint32_t kColorPetFace = 0xD9FBFF;
constexpr uint32_t kOwBackground = 0x05070B;
constexpr uint32_t kOwPanel      = 0x10161F;
constexpr uint32_t kOwPanelAlt   = 0x121C29;
constexpr uint32_t kOwBlue       = 0x35B8FF;
constexpr uint32_t kOwGreen      = 0x55F36A;
constexpr uint32_t kOwToday      = 0xFF7438;
constexpr uint32_t kOwAmber      = 0xFFC542;
constexpr uint32_t kOwTeal       = 0x2DF1D3;
constexpr uint32_t kOwSoftText   = 0xADB9CC;
constexpr uint32_t kOwTrack      = 0x1E2A3C;
constexpr uint32_t kOwStatusIdle = 0x7895B2;
constexpr uint32_t kOwAlert      = 0xFF405C;
constexpr uint32_t kPetTouchEffectMs = 1050;
constexpr uint32_t kPetIdleFrameMs = 50;
constexpr uint32_t kPetActiveFrameMs = 33;

constexpr int kScreenSize = 466;
constexpr int kScreenCenter = 233;
constexpr int kScreenRadius = 233;
constexpr int kQuotaRadius = 219;
constexpr int kQuotaWidth = 18;
constexpr int kQuotaEndpointLabelWidth = 90;
constexpr int kQuotaEndpointLabelInset = 10;
constexpr int kQuotaLeftLabelX = kScreenCenter - kQuotaRadius + kQuotaEndpointLabelInset;
constexpr int kQuotaRightLabelX = kScreenCenter + kQuotaRadius - kQuotaEndpointLabelInset - kQuotaEndpointLabelWidth;
constexpr int kQuotaCaptionY = kScreenCenter - 38;
constexpr int kQuotaValueY = kScreenCenter - 16;
constexpr int kQuotaGradientHalfAngle = 3;

void setLabelTextIfChanged(Label& label, std::string_view text)
{
    const char* current = label.getText();
    if (current != nullptr && text == current) {
        return;
    }
    label.setText(text);
}

template <typename WidgetT>
void setHiddenIfChanged(WidgetT& widget, bool hidden)
{
    const bool currently_hidden = lv_obj_has_flag(widget.get(), LV_OBJ_FLAG_HIDDEN);
    if (currently_hidden != hidden) {
        widget.setHidden(hidden);
    }
}

uint32_t blendRgb(uint32_t from, uint32_t to, float amount)
{
    const float t = std::clamp(amount, 0.0f, 1.0f);
    const auto blend_channel = [t](uint32_t a, uint32_t b) {
        return static_cast<uint32_t>(std::lround(static_cast<float>(a) +
                                                 (static_cast<float>(b) - static_cast<float>(a)) * t));
    };
    const uint32_t red = blend_channel((from >> 16) & 0xFF, (to >> 16) & 0xFF);
    const uint32_t green = blend_channel((from >> 8) & 0xFF, (to >> 8) & 0xFF);
    const uint32_t blue = blend_channel(from & 0xFF, to & 0xFF);
    return (red << 16) | (green << 8) | blue;
}

constexpr int square(int value)
{
    return value * value;
}

constexpr bool pointInsideRoundDisplay(int x, int y)
{
    return square(x - kScreenCenter) + square(y - kScreenCenter) <= square(kScreenRadius);
}

constexpr bool rectInsideRoundDisplay(int x, int y, int width, int height)
{
    return pointInsideRoundDisplay(x, y) &&
           pointInsideRoundDisplay(x + width - 1, y) &&
           pointInsideRoundDisplay(x, y + height - 1) &&
           pointInsideRoundDisplay(x + width - 1, y + height - 1);
}

static_assert(kQuotaRadius + kQuotaWidth / 2 <= kScreenRadius - 5,
              "Quota arc must stay inside the AMOLED safe radius");
static_assert(rectInsideRoundDisplay(kQuotaLeftLabelX, kQuotaCaptionY, kQuotaEndpointLabelWidth, 60),
              "Today quota labels must stay inside the round display");
static_assert(rectInsideRoundDisplay(kQuotaRightLabelX, kQuotaCaptionY, kQuotaEndpointLabelWidth, 60),
              "Remaining quota labels must stay inside the round display");
static_assert(rectInsideRoundDisplay(168, 394, 130, 28),
              "Quota refresh label must stay inside the round display");

void setup_clock_digit_panel(Container& panel, int x)
{
    panel.align(LV_ALIGN_TOP_MID, x, 8);
    panel.setSize(66, 54);
    panel.setRadius(22);
    panel.setBorderWidth(0);
    panel.setBgColor(lv_color_hex(0x0A1B26));
    panel.setBgOpa(210);
    panel.setShadowWidth(8);
    panel.setShadowColor(lv_color_hex(0x17465B));
    panel.setShadowOpa(45);
    panel.setPaddingAll(0);
    panel.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

void setup_clock_flow(NumberFlow& flow)
{
    flow.minDigits = 2;
    flow.setAlign(LV_ALIGN_CENTER);
    flow.setPos(0, -1);
    flow.setTextFont(&lv_font_montserrat_36);
    flow.setTextColor(lv_color_hex(kColorClock));
    flow.init();
}

float clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

uint32_t blend_color(uint32_t from, uint32_t to, float amount)
{
    const float t = clamp01(amount);
    const uint8_t fr = static_cast<uint8_t>((from >> 16) & 0xFF);
    const uint8_t fg = static_cast<uint8_t>((from >> 8) & 0xFF);
    const uint8_t fb = static_cast<uint8_t>(from & 0xFF);
    const uint8_t tr = static_cast<uint8_t>((to >> 16) & 0xFF);
    const uint8_t tg = static_cast<uint8_t>((to >> 8) & 0xFF);
    const uint8_t tb = static_cast<uint8_t>(to & 0xFF);
    const auto mix = [t](uint8_t a, uint8_t b) -> uint8_t {
        return static_cast<uint8_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t));
    };
    return (static_cast<uint32_t>(mix(fr, tr)) << 16) |
           (static_cast<uint32_t>(mix(fg, tg)) << 8) |
           static_cast<uint32_t>(mix(fb, tb));
}

uint32_t quota_semantic_color(float fraction)
{
    const float value = clamp01(fraction);
    if (value < 0.34f) {
        return blend_color(0xFF4A3A, kOwAmber, value / 0.34f);
    }
    if (value < 0.68f) {
        return blend_color(kOwAmber, 0xBDEB38, (value - 0.34f) / 0.34f);
    }
    return blend_color(0xBDEB38, kOwGreen, (value - 0.68f) / 0.32f);
}

uint32_t activity_heat_color(float activity)
{
    const float value = clamp01(activity);
    if (value <= 0.001f) {
        return 0x142236;
    }
    if (value < 0.10f) {
        return 0x18364A;
    }
    if (value < 0.25f) {
        return 0x174B63;
    }
    if (value < 0.45f) {
        return 0x176078;
    }
    if (value < 0.65f) {
        return 0x1591A2;
    }
    if (value < 0.82f) {
        return 0x18B7B6;
    }
    return kOwTeal;
}

template <size_t N>
const char* pick_phrase(const std::array<const char*, N>& phrases, uint32_t salt)
{
    static_assert(N > 0);
    return phrases[(salt / 97U) % N];
}

template <size_t N>
const void* pick_asset(const std::array<const void*, N>& assets, uint32_t salt)
{
    static_assert(N > 0);
    return assets[(salt / 97U) % N];
}

const void* pick_idle_asset(uint32_t salt)
{
    static constexpr std::array<const void*, 3> kAssets = {
        &codex_pet_msg_idle_0,
        &codex_pet_msg_idle_1,
        &codex_pet_msg_idle_2,
    };
    return pick_asset(kAssets, salt);
}

const void* pick_touch_asset(uint32_t salt)
{
    static constexpr std::array<const void*, 4> kAssets = {
        &codex_pet_msg_touch_0,
        &codex_pet_msg_touch_1,
        &codex_pet_msg_touch_2,
        &codex_pet_msg_touch_3,
    };
    return pick_asset(kAssets, salt);
}

const void* pick_processing_asset(uint32_t salt)
{
    static constexpr std::array<const void*, 4> kAssets = {
        &codex_pet_msg_processing_0,
        &codex_pet_msg_processing_1,
        &codex_pet_msg_processing_2,
        &codex_pet_msg_processing_3,
    };
    return pick_asset(kAssets, salt);
}

const void* pick_key_asset(uint32_t salt)
{
    static constexpr std::array<const void*, 3> kAssets = {
        &codex_pet_msg_key_0,
        &codex_pet_msg_key_1,
        &codex_pet_msg_key_2,
    };
    return pick_asset(kAssets, salt);
}

const void* pick_error_asset(uint32_t salt)
{
    static constexpr std::array<const void*, 3> kAssets = {
        &codex_pet_msg_error_0,
        &codex_pet_msg_error_1,
        &codex_pet_msg_error_2,
    };
    return pick_asset(kAssets, salt);
}

const char* pick_idle_phrase(uint32_t salt)
{
    static constexpr std::array<const char*, 6> kPhrases = {
        "Ready",
        "Still here",
        "Tiny focus",
        "Tap when ready",
        "Quota watch",
        "Waiting..."
    };
    return pick_phrase(kPhrases, salt);
}

}  // namespace

void CodexView::init(lv_obj_t* parent, ThemeMode themeMode)
{
    _theme_mode = themeMode;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent,
                              lv_color_hex(_theme_mode == ThemeMode::OpenWatcherV2 ? kOwBackground : 0x000000),
                              LV_PART_MAIN);

    _state.weekly   = {1.0f, 1.0f, "--"};
    _state.message  = "Waiting...";

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_theme_mode == ThemeMode::OpenWatcherV2 ? kOwBackground : 0x000000));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        initOpenWatcherV2();
    } else {
        initFlipClock();
        initSemicircleQuota();
        initPet();
    }

    _message_label = std::make_unique<Label>(_panel->get());
    _message_label->setText(_state.message.c_str());
    _message_label->setTextFont(_theme_mode == ThemeMode::OpenWatcherV2
                                    ? &lv_font_montserrat_18
                                    : &lv_font_montserrat_24);
    _message_label->setTextColor(lv_color_hex(_theme_mode == ThemeMode::OpenWatcherV2
                                                  ? kOwSoftText
                                                  : kColorText));
    _message_label->setWidth(260);
    _message_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _message_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _message_label->align(LV_ALIGN_TOP_MID, 0, _theme_mode == ThemeMode::OpenWatcherV2 ? 354 : 327);

    _message_image = std::make_unique<Image>(_panel->get());
    _message_image->setSrc(&codex_pet_msg_idle_0);
    _message_image->align(LV_ALIGN_TOP_MID, 0, _theme_mode == ThemeMode::OpenWatcherV2 ? 272 : 323);
    _message_image->setHidden(_theme_mode == ThemeMode::OpenWatcherV2);
    _message_image->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _message_image_active = _theme_mode != ThemeMode::OpenWatcherV2;
    if (_message_label) {
        _message_label->setHidden(_theme_mode != ThemeMode::OpenWatcherV2);
    }

    initVoiceWaveform();

    _ble_dot = std::make_unique<Container>(_panel->get());
    _ble_dot->setSize(8, 8);
    _ble_dot->setRadius(LV_RADIUS_CIRCLE);
    _ble_dot->setBorderWidth(0);
    _ble_dot->setBgColor(lv_color_hex(0x2F79FF));
    _ble_dot->align(_theme_mode == ThemeMode::OpenWatcherV2 ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID,
                    -7,
                    _theme_mode == ThemeMode::OpenWatcherV2 ? -22 : 71);

    _wifi_dot = std::make_unique<Container>(_panel->get());
    _wifi_dot->setSize(8, 8);
    _wifi_dot->setRadius(LV_RADIUS_CIRCLE);
    _wifi_dot->setBorderWidth(0);
    _wifi_dot->setBgColor(lv_color_hex(0x38E07D));
    _wifi_dot->align(_theme_mode == ThemeMode::OpenWatcherV2 ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID,
                     7,
                     _theme_mode == ThemeMode::OpenWatcherV2 ? -22 : 71);

    updateConnectionDots();
    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        updateOpenWatcherV2Labels();
    } else {
        updateSemicircleQuota();
    }
    GetHAL().updateImuData();
}

void CodexView::update()
{
    if (_theme_mode != ThemeMode::OpenWatcherV2) {
        updateFlipClock();
    }

    const uint32_t now = GetHAL().millis();
    if (_state.messageExpiresAtMs != 0 && now > _state.messageExpiresAtMs) {
        _state.messageExpiresAtMs = 0;
        if (_theme_mode == ThemeMode::OpenWatcherV2) {
            _state.message.clear();
            if (_message_label) {
                _message_label->setText("");
                _message_label->setHidden(true);
            }
        } else {
            const bool voice_visible = _voice_mode != VoiceMode::Idle;
            if (_message_image_active) {
                setIdleMessageImage(++_message_phrase_counter + now);
            } else {
                _state.message = pick_idle_phrase(++_message_phrase_counter + now);
            }
            if (_message_label && !_message_image_active) {
                _message_label->setHidden(voice_visible);
                _message_label->setText(_state.message.c_str());
            }
        }
    }

    updateMotionInput();

    if (_theme_mode != ThemeMode::OpenWatcherV2) {
        const bool voice_animated = _voice_mode == VoiceMode::Recording ||
                                    _voice_mode == VoiceMode::Processing;
        const bool pet_active = voice_animated ||
                                _state.processing ||
                                _pet_pressed ||
                                _pet_anim != PetAnim::Idle ||
                                now < _pet_effect_until_tick ||
                                _shake_energy > 0.0f;
        const uint32_t pet_frame_ms = pet_active ? kPetActiveFrameMs : kPetIdleFrameMs;
        if (_last_pet_update_tick == 0 || now - _last_pet_update_tick >= pet_frame_ms || _pet_pressed) {
            _last_pet_update_tick = now;
            updatePet();
        }
    }
    updateVoiceWaveform();
}

void CodexView::applyQuota(int weeklyLeftPct,
                           const std::string& weeklyResetLabel,
                           bool valid,
                           bool wifiConnected,
                           bool processing,
                           const std::string& message)
{
    codex::QuotaSnapshot snapshot;
    snapshot.weeklyLeftPct = weeklyLeftPct;
    snapshot.weeklyUsage = weeklyResetLabel;
    snapshot.valid = valid;
    snapshot.wifiConnected = wifiConnected;
    snapshot.processing = processing;
    snapshot.message = message;
    applySnapshot(snapshot);
}

void CodexView::applySnapshot(const codex::QuotaSnapshot& snapshot)
{
    const int previous_week_pct = static_cast<int>(std::lround(remainingRatio(_state.weekly) * 100.0f));
    const auto previous_activity_buckets = _state.activityBuckets;

    _state.wifiConnected = snapshot.wifiConnected;
    _state.processing = snapshot.processing;
    _state.source = snapshot.source;
    _state.updatedAt = snapshot.updatedAt;
    _state.stale = snapshot.stale || snapshot.cached;
    _state.hostName = snapshot.hostName;
    _state.sessionTitle = snapshot.sessionTitle.empty() ? "Codex Ready" : snapshot.sessionTitle;
    _state.contextLabel = snapshot.contextLabel.empty() ? "-- / --" : snapshot.contextLabel;
    _state.contextPressurePct = std::clamp(snapshot.contextPressurePct, 0, 100);
    _state.compactThresholdPct = snapshot.compactThresholdPct;
    _state.compactWarning = snapshot.compactWarning;
    _state.totalTokensLabel = snapshot.totalTokensLabel.empty() ? "--" : snapshot.totalTokensLabel;
    _state.modelLabel = snapshot.modelLabel.empty() ? "--" : snapshot.modelLabel;
    _state.reasoningLabel = snapshot.reasoningLabel.empty() ? "--" : snapshot.reasoningLabel;
    _state.activityLabel = snapshot.activityLabel.empty() ? "--" : snapshot.activityLabel;
    _state.activityLive = snapshot.activityLive;
    _state.activityBuckets = snapshot.activityBuckets;
    _state.weeklyDayStartLeftPct = snapshot.weeklyDayStartLeftPct;
    _state.weeklySegmentStartLeftPct = snapshot.weeklySegmentStartLeftPct;
    _state.weeklyTodayUsedPctPoints = snapshot.weeklyTodayUsedPctPoints;
    _state.weeklyResetCount = snapshot.weeklyResetCount;
    _state.weeklyTrackingPeriodStart = snapshot.weeklyTrackingPeriodStart;
    _last_quota_update_tick = GetHAL().millis();

    if (snapshot.valid) {
        _state.weekly.used = 100.0f - static_cast<float>(std::clamp(snapshot.weeklyLeftPct, 0, 100));
        _state.weekly.limit = 100.0f;
        _state.weekly.resetInLabel = snapshot.weeklyUsage.empty() ? "--" : snapshot.weeklyUsage;
    } else {
        _state.weekly.used = 100.0f;
        _state.weekly.limit = 100.0f;
        _state.weekly.resetInLabel = "--";
    }

    const int updated_week_pct = static_cast<int>(std::lround(remainingRatio(_state.weekly) * 100.0f));
    const bool v2_canvas_changed = previous_week_pct != updated_week_pct ||
                                   previous_activity_buckets != _state.activityBuckets;

    updateConnectionDots();
    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        updateOpenWatcherV2Labels();
        if (v2_canvas_changed && _v2_canvas) {
            lv_obj_invalidate(_v2_canvas->get());
        }
    } else {
        updateSemicircleQuota();
    }
    setMessageText(snapshot.message);
    if (_state.processing) {
        setMessageImage(pick_processing_asset(++_message_phrase_counter + GetHAL().millis()), 2400);
    }
}

void CodexView::applyBleState(bool connected, const std::string& hostMessage, bool hostMessageChanged)
{
    if (_state.bleConnected != connected) {
        _state.bleConnected = connected;
        updateConnectionDots();
        updateOpenWatcherV2Labels();
    }

    if (hostMessageChanged && !hostMessage.empty()) {
        if (hostMessage == "Typeless recording" ||
            hostMessage == "Typeless processing" ||
            hostMessage == "Typeless idle" ||
            hostMessage == "Bridge ready") {
            if (hostMessage == "Bridge ready" &&
                (_state.message == "Bridge unavailable" ||
                 _state.message == "App unavailable" ||
                 _state.message == "Host state unknown")) {
                setMessageText(pick_idle_phrase(++_message_phrase_counter + GetHAL().millis()));
            }
            return;
        }
        std::string message = hostMessage;
        if (hostMessage == "BLE connected") {
            message = "BLE connected";
        } else if (hostMessage == "BLE paired") {
            message = "BLE paired";
        } else if (hostMessage == "BLE disconnected") {
            message = "BLE disconnected";
        } else if (hostMessage == "BLE ready") {
            message = "BLE ready";
        } else if (hostMessage == "Key sent") {
            setMessageImage(pick_key_asset(++_message_phrase_counter + GetHAL().millis()), 1500);
            return;
        } else if (hostMessage == "Key send failed") {
            setMessageImage(pick_error_asset(++_message_phrase_counter + GetHAL().millis()), 1800);
            return;
        } else if (hostMessage == "Typeless request sent") {
            setMessageImage(pick_key_asset(++_message_phrase_counter + GetHAL().millis()), 1500);
            return;
        } else if (hostMessage == "Bridge fallback key") {
            setMessageImage(pick_key_asset(++_message_phrase_counter + GetHAL().millis()), 1500);
            return;
        }
        setMessageText(message);
    }
}

void CodexView::setUnreadTaskCount(int count)
{
    _state.unreadStateValid = true;
    _state.unreadTaskCount = std::max(count, 0);
    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        updateOpenWatcherV2Labels();
    }
}

void CodexView::setVoiceActive(bool active)
{
    setVoiceMode(active ? VoiceMode::Recording : VoiceMode::Idle);
}

void CodexView::setVoiceMode(VoiceMode mode)
{
    if (_voice_mode == mode) {
        return;
    }

    _voice_mode = mode;
    const bool visible = mode == VoiceMode::Recording || mode == VoiceMode::Processing;
    if (_voice_waveform) {
        _voice_waveform->setHidden(!visible);
    }
    if (mode == VoiceMode::Interrupted) {
        _message_image_active = false;
        _state.message = "MIC LOST - PRESS A";
        _state.messageExpiresAtMs = 0;
        if (_message_label) {
            _message_label->setText(_state.message.c_str());
            _message_label->setTextColor(lv_color_hex(kOwAmber));
            _message_label->setHidden(_theme_mode == ThemeMode::OpenWatcherV2);
        }
        if (_message_image) {
            _message_image->setHidden(true);
        }
    } else {
        if (_message_label) {
            if (_state.message == "MIC LOST - PRESS A") {
                _state.message.clear();
                _message_label->setText("");
            }
            _message_label->setTextColor(lv_color_hex(_theme_mode == ThemeMode::OpenWatcherV2
                                                          ? kOwSoftText
                                                          : kColorText));
            _message_label->setHidden(visible || _message_image_active);
        }
        if (_message_image) {
            _message_image->setHidden(visible || !_message_image_active);
        }
    }
    if (_v2_sync_indicator) {
        setHiddenIfChanged(*_v2_sync_indicator, visible || mode == VoiceMode::Interrupted);
    }
    updateOpenWatcherV2Labels();
}

uint32_t CodexView::frameIntervalMs() const
{
    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        return 100;
    }
    return (_voice_mode == VoiceMode::Recording || _voice_mode == VoiceMode::Processing)
        ? kPetActiveFrameMs
        : kPetIdleFrameMs;
}

bool CodexView::consumeClearInputRequest()
{
    if (!_clear_input_requested) {
        return false;
    }
    _clear_input_requested = false;
    return true;
}

void CodexView::initFlipClock()
{
    _clock_panel = std::make_unique<Container>(_panel->get());
    _clock_panel->align(LV_ALIGN_TOP_MID, 0, 0);
    _clock_panel->setSize(154, 66);
    _clock_panel->setBgOpa(LV_OPA_TRANSP);
    _clock_panel->setBorderWidth(0);
    _clock_panel->setPaddingAll(0);
    _clock_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _clock_hour_panel = std::make_unique<Container>(_clock_panel->get());
    setup_clock_digit_panel(*_clock_hour_panel, -40);
    _clock_hour_flow = std::make_unique<NumberFlow>(_clock_hour_panel->get());
    setup_clock_flow(*_clock_hour_flow);

    _clock_minute_panel = std::make_unique<Container>(_clock_panel->get());
    setup_clock_digit_panel(*_clock_minute_panel, 40);
    _clock_minute_flow = std::make_unique<NumberFlow>(_clock_minute_panel->get());
    setup_clock_flow(*_clock_minute_flow);

    _clock_colon = std::make_unique<Label>(_clock_panel->get());
    _clock_colon->setText(":");
    _clock_colon->setTextFont(&lv_font_montserrat_36);
    _clock_colon->setTextColor(lv_color_hex(kColorClock));
    _clock_colon->align(LV_ALIGN_TOP_MID, 0, 12);

    updateFlipClock(true);
}

void CodexView::updateFlipClock(bool force)
{
    if (_clock_hour_flow) {
        _clock_hour_flow->update();
    }
    if (_clock_minute_flow) {
        _clock_minute_flow->update();
    }

    const uint32_t now_ms = GetHAL().millis();
    if (!force && now_ms - _last_clock_tick <= 1000) {
        return;
    }
    _last_clock_tick = now_ms;

    std::time_t now    = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    if (localTime == nullptr) {
        return;
    }

    if (_clock_hour_flow) {
        _clock_hour_flow->setValue(localTime->tm_hour);
    }
    if (_clock_minute_flow) {
        _clock_minute_flow->setValue(localTime->tm_min);
    }
}

void CodexView::initSemicircleQuota()
{
    _semicircle_quota.canvas = std::make_unique<Container>(_panel->get());
    _semicircle_quota.canvas->setSize(kScreenSize, kScreenSize);
    _semicircle_quota.canvas->align(LV_ALIGN_CENTER, 0, 0);
    _semicircle_quota.canvas->setBgOpa(LV_OPA_TRANSP);
    _semicircle_quota.canvas->setBorderWidth(0);
    _semicircle_quota.canvas->setPaddingAll(0);
    _semicircle_quota.canvas->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _semicircle_quota.canvas->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_semicircle_quota.canvas->get(),
                        &CodexView::drawSemicircleQuotaEvent,
                        LV_EVENT_DRAW_MAIN_BEGIN,
                        this);

    auto setup_caption = [](Label& label, const char* text, uint32_t color, int x) {
        label.setText(text);
        label.setTextFont(&lv_font_montserrat_18);
        label.setTextColor(lv_color_hex(color));
        label.setWidth(kQuotaEndpointLabelWidth);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.align(LV_ALIGN_TOP_LEFT, x, kQuotaCaptionY);
    };

    auto setup_value = [](Label& label, uint32_t color, int x) {
        label.setText("--%");
        label.setTextFont(&MontserratSemiBold26);
        label.setTextColor(lv_color_hex(color));
        label.setWidth(kQuotaEndpointLabelWidth);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.align(LV_ALIGN_TOP_LEFT, x, kQuotaValueY);
    };

    _semicircle_quota.todayCaption = std::make_unique<Label>(_panel->get());
    setup_caption(*_semicircle_quota.todayCaption, "TODAY", kColorTodayText, kQuotaLeftLabelX);
    _semicircle_quota.todayValue = std::make_unique<Label>(_panel->get());
    setup_value(*_semicircle_quota.todayValue, kColorTodayText, kQuotaLeftLabelX);

    _semicircle_quota.remainingCaption = std::make_unique<Label>(_panel->get());
    setup_caption(*_semicircle_quota.remainingCaption, "LEFT", kColorWeekText, kQuotaRightLabelX);
    _semicircle_quota.remainingValue = std::make_unique<Label>(_panel->get());
    setup_value(*_semicircle_quota.remainingValue, kColorWeekText, kQuotaRightLabelX);

    _semicircle_quota.resetLabel = std::make_unique<Label>(_panel->get());
    _semicircle_quota.resetLabel->setText("--");
    _semicircle_quota.resetLabel->setTextFont(&lv_font_montserrat_22);
    _semicircle_quota.resetLabel->setTextColor(lv_color_hex(kColorTextDim));
    _semicircle_quota.resetLabel->setWidth(130);
    _semicircle_quota.resetLabel->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _semicircle_quota.resetLabel->align(LV_ALIGN_TOP_LEFT, 168, 394);
}

void CodexView::updateSemicircleQuota()
{
    const int remaining = static_cast<int>(std::lround(remainingRatio(_state.weekly) * 100.0f));
    const bool has_daily_baseline = _state.weeklyDayStartLeftPct >= 0;
    const int today_used = std::max(0, _state.weeklyTodayUsedPctPoints);

    if (_semicircle_quota.remainingValue) {
        char value[8];
        std::snprintf(value, sizeof(value), "%d%%", remaining);
        _semicircle_quota.remainingValue->setText(value);
    }
    if (_semicircle_quota.todayValue) {
        char value[16];
        if (has_daily_baseline) {
            std::snprintf(value, sizeof(value), "%d%%", today_used);
        } else {
            std::snprintf(value, sizeof(value), "--%%");
        }
        _semicircle_quota.todayValue->setText(value);
    }
    if (_semicircle_quota.resetLabel) {
        _semicircle_quota.resetLabel->setText(_state.weekly.resetInLabel.c_str());
    }
    if (_semicircle_quota.canvas) {
        lv_obj_invalidate(_semicircle_quota.canvas->get());
    }
}

void CodexView::drawSemicircleQuotaEvent(lv_event_t* event)
{
    auto* view = static_cast<CodexView*>(lv_event_get_user_data(event));
    if (view == nullptr || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    view->drawSemicircleQuota(layer, coords);
}

void CodexView::drawSemicircleQuota(lv_layer_t* layer, const lv_area_t& coords)
{
    const float remaining = remainingRatio(_state.weekly);
    const float today = _state.weeklyDayStartLeftPct >= 0
        ? clamp01(static_cast<float>(_state.weeklyTodayUsedPctPoints) / 100.0f)
        : 0.0f;
    const float today_end = std::min(1.0f, remaining + today);

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = {
        static_cast<lv_coord_t>(coords.x1 + kScreenCenter),
        static_cast<lv_coord_t>(coords.y1 + kScreenCenter),
    };
    arc.radius = kQuotaRadius;
    arc.width = kQuotaWidth;
    arc.rounded = 1;
    arc.start_angle = 0;
    arc.end_angle = 180;
    arc.color = lv_color_hex(kColorTrack);
    arc.opa = 230;
    lv_draw_arc(layer, &arc);

    const int remaining_end_angle = static_cast<int>(std::lround(180.0f * remaining));
    if (remaining_end_angle > 0) {
        arc.start_angle = 0;
        arc.end_angle = remaining_end_angle;
        arc.color = lv_color_hex(kColorWeek);
        arc.opa = 245;
        lv_draw_arc(layer, &arc);
    }

    const int today_end_angle = static_cast<int>(std::lround(180.0f * today_end));
    if (today_end_angle > remaining_end_angle) {
        arc.start_angle = remaining_end_angle;
        arc.end_angle = today_end_angle;
        arc.color = lv_color_hex(kColorToday);
        arc.opa = 245;
        lv_draw_arc(layer, &arc);
    }

    const auto draw_gradient_boundary = [&](int boundary_angle,
                                            int left_available,
                                            int right_available,
                                            uint32_t left_color,
                                            uint32_t right_color) {
        const int half_angle = std::min({kQuotaGradientHalfAngle, left_available, right_available});
        if (half_angle <= 0) {
            return;
        }

        const int start_angle = boundary_angle - half_angle;
        const int end_angle = boundary_angle + half_angle;
        arc.rounded = 0;
        arc.opa = 245;
        for (int angle = start_angle; angle < end_angle; ++angle) {
            const float mix = static_cast<float>(angle - start_angle) /
                              static_cast<float>(end_angle - start_angle);
            arc.start_angle = angle;
            arc.end_angle = angle + 1;
            arc.color = lv_color_hex(blendRgb(left_color, right_color, mix));
            lv_draw_arc(layer, &arc);
        }
    };

    const bool has_today_segment = today_end_angle > remaining_end_angle;
    const int next_boundary = has_today_segment ? today_end_angle : 180;
    draw_gradient_boundary(remaining_end_angle,
                           remaining_end_angle,
                           next_boundary - remaining_end_angle,
                           kColorWeek,
                           has_today_segment ? kColorToday : kColorTrack);

    if (has_today_segment && today_end_angle < 180) {
        draw_gradient_boundary(today_end_angle,
                               today_end_angle - remaining_end_angle,
                               180 - today_end_angle,
                               kColorToday,
                               kColorTrack);
    }
}

void CodexView::initOpenWatcherV2()
{
    _v2_canvas = std::make_unique<Container>(_panel->get());
    _v2_canvas->setSize(466, 466);
    _v2_canvas->align(LV_ALIGN_CENTER, 0, 0);
    _v2_canvas->setBgOpa(LV_OPA_TRANSP);
    _v2_canvas->setBorderWidth(0);
    _v2_canvas->setPaddingAll(0);
    _v2_canvas->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_v2_canvas->get(), &CodexView::drawOpenWatcherV2Event, LV_EVENT_DRAW_MAIN_BEGIN, this);

    _v2_title_label = std::make_unique<Label>(_panel->get());
    _v2_title_label->setText("Codex Ready");
    _v2_title_label->setTextFont(&lv_font_montserrat_24);
    _v2_title_label->setTextColor(lv_color_hex(kOwBlue));
    _v2_title_label->setWidth(280);
    _v2_title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _v2_title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _v2_title_label->align(LV_ALIGN_TOP_MID, 0, 40);

    _v2_left_caption_label = std::make_unique<Label>(_panel->get());
    _v2_left_caption_label->setText("LEFT");
    _v2_left_caption_label->setTextFont(&lv_font_montserrat_20);
    _v2_left_caption_label->setTextColor(lv_color_hex(kOwSoftText));
    _v2_left_caption_label->setWidth(120);
    _v2_left_caption_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _v2_left_caption_label->align(LV_ALIGN_TOP_MID, 0, 80);

    _v2_remaining_value_label = std::make_unique<Label>(_panel->get());
    _v2_remaining_value_label->setText("--");
    _v2_remaining_value_label->setTextFont(&CommissionerMedium64);
    _v2_remaining_value_label->setTextColor(lv_color_hex(kOwGreen));
    _v2_remaining_value_label->setWidth(155);
    _v2_remaining_value_label->setTextAlign(LV_TEXT_ALIGN_RIGHT);
    _v2_remaining_value_label->align(LV_ALIGN_TOP_LEFT, 130, 107);

    _v2_remaining_unit_label = std::make_unique<Label>(_panel->get());
    _v2_remaining_unit_label->setText("%");
    _v2_remaining_unit_label->setTextFont(&lv_font_montserrat_48);
    _v2_remaining_unit_label->setTextColor(lv_color_hex(kOwGreen));
    _v2_remaining_unit_label->setWidth(70);
    _v2_remaining_unit_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _v2_remaining_unit_label->align(LV_ALIGN_TOP_LEFT, 285, 139);

    _v2_today_caption_label = std::make_unique<Label>(_panel->get());
    _v2_today_caption_label->setText("TODAY");
    _v2_today_caption_label->setTextFont(&lv_font_montserrat_18);
    _v2_today_caption_label->setTextColor(lv_color_hex(kOwToday));
    _v2_today_caption_label->setWidth(100);
    _v2_today_caption_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _v2_today_caption_label->align(LV_ALIGN_TOP_LEFT, 58, 122);

    _v2_today_value_label = std::make_unique<Label>(_panel->get());
    _v2_today_value_label->setText("--%");
    _v2_today_value_label->setTextFont(&lv_font_montserrat_36);
    _v2_today_value_label->setTextColor(lv_color_hex(kOwToday));
    _v2_today_value_label->setWidth(110);
    _v2_today_value_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _v2_today_value_label->align(LV_ALIGN_TOP_LEFT, 55, 145);

    _v2_status_label = std::make_unique<Label>(_panel->get());
    _v2_status_label->setText("");
    _v2_status_label->setTextFont(&lv_font_montserrat_22);
    _v2_status_label->setTextColor(lv_color_hex(kOwBlue));
    _v2_status_label->setWidth(280);
    _v2_status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _v2_status_label->align(LV_ALIGN_TOP_MID, 0, 199);
    _v2_status_label->setHidden(true);

    _v2_meta_left_label = std::make_unique<Label>(_panel->get());
    _v2_meta_left_label->setText("BLE --");
    _v2_meta_left_label->setTextFont(&lv_font_montserrat_18);
    _v2_meta_left_label->setTextColor(lv_color_hex(kOwSoftText));
    _v2_meta_left_label->setWidth(150);
    _v2_meta_left_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _v2_meta_left_label->align(LV_ALIGN_TOP_LEFT, 46, 232);

    _v2_meta_right_label = std::make_unique<Label>(_panel->get());
    _v2_meta_right_label->setText("WIFI --");
    _v2_meta_right_label->setTextFont(&lv_font_montserrat_18);
    _v2_meta_right_label->setTextColor(lv_color_hex(kOwSoftText));
    _v2_meta_right_label->setWidth(150);
    _v2_meta_right_label->setTextAlign(LV_TEXT_ALIGN_RIGHT);
    _v2_meta_right_label->align(LV_ALIGN_TOP_RIGHT, -46, 232);

    _v2_sync_indicator = std::make_unique<Container>(_panel->get());
    _v2_sync_indicator->setSize(140, 14);
    _v2_sync_indicator->align(LV_ALIGN_TOP_MID, 0, 378);
    _v2_sync_indicator->setBgOpa(LV_OPA_TRANSP);
    _v2_sync_indicator->setBorderWidth(0);
    _v2_sync_indicator->setPaddingAll(0);
    _v2_sync_indicator->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _v2_sync_left_line = std::make_unique<Container>(_v2_sync_indicator->get());
    _v2_sync_left_line->setSize(45, 1);
    _v2_sync_left_line->align(LV_ALIGN_LEFT_MID, 0, 0);
    _v2_sync_left_line->setBgColor(lv_color_hex(0xA7F000));
    _v2_sync_left_line->setBgOpa(210);
    _v2_sync_left_line->setBorderWidth(0);

    _v2_sync_right_line = std::make_unique<Container>(_v2_sync_indicator->get());
    _v2_sync_right_line->setSize(45, 1);
    _v2_sync_right_line->align(LV_ALIGN_RIGHT_MID, 0, 0);
    _v2_sync_right_line->setBgColor(lv_color_hex(0xA7F000));
    _v2_sync_right_line->setBgOpa(210);
    _v2_sync_right_line->setBorderWidth(0);

    _v2_sync_dot = std::make_unique<Container>(_v2_sync_indicator->get());
    _v2_sync_dot->setSize(7, 7);
    _v2_sync_dot->align(LV_ALIGN_CENTER, 0, 0);
    _v2_sync_dot->setRadius(LV_RADIUS_CIRCLE);
    _v2_sync_dot->setBgColor(lv_color_hex(0xC9FF53));
    _v2_sync_dot->setBgOpa(LV_OPA_COVER);
    _v2_sync_dot->setBorderWidth(0);
}

void CodexView::updateOpenWatcherV2Labels()
{
    if (_theme_mode != ThemeMode::OpenWatcherV2) {
        return;
    }

    const int week_pct = static_cast<int>(std::lround(remainingRatio(_state.weekly) * 100.0f));
    if (_v2_title_label) {
        std::string title = _state.sessionTitle;
        uint32_t color = kOwStatusIdle;
        if (_state.unreadStateValid) {
            if (_state.unreadTaskCount <= 0) {
                title = "Codex Clear";
                color = kOwBlue;
            } else {
                const std::string count = _state.unreadTaskCount > 9
                                              ? "9+"
                                              : std::to_string(_state.unreadTaskCount);
                title = "Codex " + count + " Unread";
                if (_state.unreadTaskCount == 1) {
                    color = kOwAmber;
                } else if (_state.unreadTaskCount == 2) {
                    color = kOwToday;
                } else {
                    color = kOwAlert;
                }
            }
        }
        setLabelTextIfChanged(*_v2_title_label, title);
        _v2_title_label->setTextColor(lv_color_hex(color));
    }
    if (_v2_remaining_value_label) {
        const std::string remaining = std::to_string(week_pct);
        setLabelTextIfChanged(*_v2_remaining_value_label, remaining);
    }
    if (_v2_today_value_label) {
        const bool today_valid = _state.weeklyDayStartLeftPct >= 0;
        const int today_pct = std::clamp(_state.weeklyTodayUsedPctPoints, 0, 100);
        const std::string today = today_valid ? (std::to_string(today_pct) + "%") : "--%";
        setLabelTextIfChanged(*_v2_today_value_label, today);
    }
    if (_v2_status_label) {
        const bool interrupted = _voice_mode == VoiceMode::Interrupted;
        const bool recording = _voice_mode == VoiceMode::Recording;
        const bool processing = _voice_mode == VoiceMode::Processing || _state.processing;
        const bool show_status = interrupted || _state.compactWarning || recording || processing;
        const char* text = interrupted ? "MIC LOST - PRESS A"
                                       : (_state.compactWarning ? "COMPACT SOON"
                                                                : (recording ? "LISTENING" : "PROCESSING"));
        const uint32_t color = interrupted ? kOwToday
                                           : (_state.compactWarning ? kOwAmber
                                                                    : (recording ? kOwGreen : (processing ? kOwAmber : kOwBlue)));
        const char* current = _v2_status_label->getText();
        if (current == nullptr || std::string_view(current) != text) {
            _v2_status_label->setText(text);
            _v2_status_label->setTextColor(lv_color_hex(color));
        }
        setHiddenIfChanged(*_v2_status_label, !show_status);
    }
    if (_v2_meta_left_label) {
        const std::string reset = _state.weekly.resetInLabel.empty() ? "--" : _state.weekly.resetInLabel;
        const std::string meta = "RESET " + reset;
        setLabelTextIfChanged(*_v2_meta_left_label, meta);
    }
    if (_v2_meta_right_label) {
        const std::string activity = "4H -> NOW";
        const char* current = _v2_meta_right_label->getText();
        if (current == nullptr || std::string_view(current) != activity) {
            _v2_meta_right_label->setText(activity.c_str());
            _v2_meta_right_label->setTextColor(lv_color_hex(_state.activityLive ? kOwGreen : kOwSoftText));
        }
    }
}

void CodexView::drawOpenWatcherV2Event(lv_event_t* event)
{
    auto* view = static_cast<CodexView*>(lv_event_get_user_data(event));
    if (view == nullptr) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    view->drawOpenWatcherV2(layer, coords);
}

void CodexView::drawOpenWatcherV2(lv_layer_t* layer, const lv_area_t& coords)
{
    const int width = coords.x2 - coords.x1 + 1;
    const int height = coords.y2 - coords.y1 + 1;
    const lv_point_t center = {
        static_cast<lv_coord_t>(coords.x1 + width / 2),
        static_cast<lv_coord_t>(coords.y1 + height / 2),
    };

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = center;
    arc.rounded = 1;

    const float week = remainingRatio(_state.weekly);

    arc.radius = 223;
    arc.width = 7;
    arc.color = lv_color_hex(kOwTrack);
    arc.opa = 170;
    arc.start_angle = 180;
    arc.end_angle = 360;
    lv_draw_arc(layer, &arc);

    constexpr int kTopSegments = 36;
    for (int i = 0; i < kTopSegments; ++i) {
        const float start = static_cast<float>(i) / static_cast<float>(kTopSegments);
        const float end = static_cast<float>(i + 1) / static_cast<float>(kTopSegments);
        if (start >= week) {
            break;
        }
        const float active_end = std::min(end, week);
        const int start_deg = 180 + static_cast<int>(std::lround(180.0f * start));
        const int end_deg = 180 + static_cast<int>(std::lround(180.0f * active_end)) - 1;
        if (end_deg <= start_deg) {
            continue;
        }
        arc.color = lv_color_hex(quota_semantic_color((start + active_end) * 0.5f));
        arc.opa = 235;
        arc.start_angle = start_deg;
        arc.end_angle = end_deg;
        lv_draw_arc(layer, &arc);
    }

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);

    auto draw_line = [&](int x1, int y1, int x2, int y2, uint32_t color, int line_width, lv_opa_t opa) {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.p1 = {static_cast<lv_value_precise_t>(coords.x1 + x1), static_cast<lv_value_precise_t>(coords.y1 + y1)};
        line.p2 = {static_cast<lv_value_precise_t>(coords.x1 + x2), static_cast<lv_value_precise_t>(coords.y1 + y2)};
        line.color = lv_color_hex(color);
        line.width = line_width;
        line.opa = opa;
        line.round_start = 1;
        line.round_end = 1;
        lv_draw_line(layer, &line);
    };

    draw_line(50, 181, 122, 181, kOwToday, 1, 230);
    draw_line(122, 181, 147, 205, kOwToday, 1, 230);
    draw_line(147, 205, 168, 205, kOwToday, 1, 230);

    lv_area_t today_dot = {
        static_cast<lv_coord_t>(coords.x1 + 165),
        static_cast<lv_coord_t>(coords.y1 + 202),
        static_cast<lv_coord_t>(coords.x1 + 171),
        static_cast<lv_coord_t>(coords.y1 + 208),
    };
    rect.radius = LV_RADIUS_CIRCLE;
    rect.bg_color = lv_color_hex(kOwToday);
    rect.bg_opa = 255;
    rect.border_width = 0;
    lv_draw_rect(layer, &rect, &today_dot);

    auto draw_divider = [&](int y) {
        draw_line(44, y, 204, y, 0x314052, 1, 210);
        draw_line(262, y, 422, y, 0x314052, 1, 210);
        draw_line(218, y, 227, y, 0x596CFF, 2, 245);
        draw_line(232, y, 241, y, 0x596CFF, 2, 245);
        draw_line(246, y, 255, y, 0x596CFF, 2, 245);
    };
    draw_divider(222);
    draw_divider(340);

    constexpr int kCells = 24;
    constexpr int kCellSize = 24;
    constexpr int kCellGap = 6;
    const int cell_x = coords.x1 + 56;
    const int cell_y = coords.y1 + 263;

    for (int i = 0; i < kCells; ++i) {
        const float activity = _state.activityBuckets[i];
        const int col = i % 12;
        const int row = i / 12;
        const int x = cell_x + col * (kCellSize + kCellGap);
        const int y = cell_y + row * (kCellSize + kCellGap);

        lv_area_t area = {
            static_cast<lv_coord_t>(x),
            static_cast<lv_coord_t>(y),
            static_cast<lv_coord_t>(x + kCellSize - 1),
            static_cast<lv_coord_t>(y + kCellSize - 1),
        };
        rect.radius = 4;
        rect.bg_color = lv_color_hex(activity_heat_color(activity));
        rect.bg_opa = 255;
        rect.border_width = 1;
        rect.border_color = lv_color_hex(0x145484);
        rect.border_opa = 150;
        lv_draw_rect(layer, &rect, &area);
    }

}

void CodexView::initPet()
{
    _pet_hit_area = std::make_unique<Container>(_panel->get());
    _pet_hit_area->setSize(220, 210);
    _pet_hit_area->align(LV_ALIGN_TOP_MID, 0, 118);
    _pet_hit_area->setBgOpa(LV_OPA_TRANSP);
    _pet_hit_area->setBorderWidth(0);
    _pet_hit_area->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _pet_hit_area->onClick().connect([this]() {
        _pet_pressed = true;
        setMessageImage(pick_touch_asset(++_message_phrase_counter + GetHAL().millis()), 2200);
        GetHAL().vibrate(30, 80);
    });

    _pet_image = std::make_unique<Image>(_pet_hit_area->get());
    _pet_image->setSrc(&codex_pet_idle0);
    _pet_image->align(LV_ALIGN_CENTER, 0, 2);
    _pet_image->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _pet_current_src = &codex_pet_idle0;
    const uint32_t now = GetHAL().millis();
    _next_blink_tick = now + 4200;
    _next_idle_action_tick = now + 2600;
}

void CodexView::updatePet()
{
    const uint32_t now = GetHAL().millis();
    const float t      = static_cast<float>(now % 3600) / 3600.0f;
    const float voicePulse = _voice_mode == VoiceMode::Idle ? 0.0f
        : (0.5f + 0.5f * std::sin(static_cast<float>(now % 760) / 760.0f * 6.283185f));
    const float effectPulse = now < _pet_effect_until_tick
        ? (0.5f + 0.5f * std::sin(static_cast<float>(now % 260) / 260.0f * 6.283185f))
        : 0.0f;
    const bool touchAnimating = _pet_anim == PetAnim::Touch;
    const int touchLift = touchAnimating
        ? static_cast<int>((1.0f - std::min(1.0f, static_cast<float>(now - _pet_anim_start_tick) / 520.0f)) * -7.0f)
        : 0;
    const int yOffset  = static_cast<int>(std::sin(t * 6.283185f) * 4.0f - voicePulse * 3.0f - effectPulse * 5.0f) + touchLift;
    const int tiltX     = static_cast<int>(_tilt_x * codex_config::kTiltMaxOffset);
    const int tiltY     = static_cast<int>(_tilt_y * codex_config::kTiltMaxOffset);
    const int shakeX    = static_cast<int>(std::sin(static_cast<float>(now % 220) / 220.0f * 6.283185f) * _shake_energy * 11.0f);

    if (_pet_hit_area) {
        const int baseY = _theme_mode == ThemeMode::OpenWatcherV2 ? 124 : 118;
        _pet_hit_area->align(LV_ALIGN_TOP_MID, tiltX + shakeX, baseY + yOffset + tiltY);
    }
    if (_pet_face) {
        _pet_face->align(LV_ALIGN_CENTER, -4 + static_cast<int>(_tilt_x * 4.0f),
                         -1 + static_cast<int>(_tilt_y * 3.0f));
    }

    if (_pet_pressed) {
        _pet_pressed = false;
        _pet_anim = PetAnim::Touch;
        _pet_anim_start_tick = now;
        _pet_effect_until_tick = now + kPetTouchEffectMs;
        _shake_energy = std::max(_shake_energy, 0.25f);
        if (_pet_image) {
            _pet_image->setOpa(220);
        }
        if (_pet_screen) {
            _pet_screen->setBgColor(lv_color_hex(0x08243A));
        }
        _last_idle_tick = now;
        _next_idle_action_tick = now + 3600;
        return;
    }

    if (_pet_anim == PetAnim::Idle && now >= _next_blink_tick) {
        _pet_anim = PetAnim::Blink;
        _pet_anim_start_tick = now;
        _next_blink_tick = now + 4300 + ((now / 97) % 3600);
    }
    if (_pet_anim == PetAnim::Idle &&
        _voice_mode == VoiceMode::Idle &&
        !_state.processing &&
        now >= _next_idle_action_tick) {
        _idle_action_kind = static_cast<uint8_t>((now / 977U) % 2U);
        _pet_anim = _idle_action_kind == 0 ? PetAnim::LookAround : PetAnim::Stretch;
        _pet_anim_start_tick = now;
        _next_idle_action_tick = now + 5200 + ((now / 113U) % 4300U);
    }

    const uint32_t anim_elapsed = now - _pet_anim_start_tick;
    const void* frame = &codex_pet_idle0;
    if ((_state.processing || _voice_mode == VoiceMode::Processing) && _pet_anim != PetAnim::Touch) {
        const uint32_t phase = (now / 135) % 8;
        frame = phase == 0 ? static_cast<const void*>(&codex_pet_processing0)
                            : (phase == 1 ? static_cast<const void*>(&codex_pet_processing1)
                                          : (phase == 2 ? static_cast<const void*>(&codex_pet_processing2)
                                                        : (phase == 3 ? static_cast<const void*>(&codex_pet_processing3)
                                                                      : (phase == 4 ? static_cast<const void*>(&codex_pet_processing2)
                                                                                    : static_cast<const void*>(&codex_pet_processing1)))));
    } else switch (_pet_anim) {
    case PetAnim::Touch:
        if (anim_elapsed < 90) {
            frame = &codex_pet_touch0;
        } else if (anim_elapsed < 230) {
            frame = &codex_pet_touch1;
        } else if (anim_elapsed < 430) {
            frame = &codex_pet_touch2;
        } else if (anim_elapsed < 720) {
            frame = &codex_pet_touch3;
        } else {
            frame = &codex_pet_idle1;
        }
        if (anim_elapsed > 920) {
            _pet_anim = PetAnim::Idle;
            _pet_anim_start_tick = now;
            _next_blink_tick = now + 2600;
            _next_idle_action_tick = now + 4200;
        }
        break;
    case PetAnim::LookAround:
        if (anim_elapsed < 130) {
            frame = &codex_pet_idle1;
        } else if (anim_elapsed < 310) {
            frame = &codex_pet_idle2;
        } else if (anim_elapsed < 500) {
            frame = &codex_pet_idle1;
        } else if (anim_elapsed < 700) {
            frame = &codex_pet_idle3;
        } else {
            frame = &codex_pet_idle0;
        }
        if (anim_elapsed > 900) {
            _pet_anim = PetAnim::Idle;
            _pet_anim_start_tick = now;
        }
        break;
    case PetAnim::Stretch:
        if (anim_elapsed < 120) {
            frame = &codex_pet_idle0;
        } else if (anim_elapsed < 300) {
            frame = &codex_pet_idle4;
        } else if (anim_elapsed < 520) {
            frame = &codex_pet_idle5;
        } else if (anim_elapsed < 760) {
            frame = &codex_pet_idle4;
        } else {
            frame = &codex_pet_idle1;
        }
        if (anim_elapsed > 1020) {
            _pet_anim = PetAnim::Idle;
            _pet_anim_start_tick = now;
            _next_blink_tick = now + 2100;
        }
        break;
    case PetAnim::Blink:
        if (anim_elapsed < 110) {
            frame = &codex_pet_blink0;
        } else if (anim_elapsed < 230) {
            frame = &codex_pet_blink1;
        } else if (anim_elapsed < 340) {
            frame = &codex_pet_blink0;
        } else {
            _pet_anim = PetAnim::Idle;
            _pet_anim_start_tick = now;
            _next_blink_tick = now + 4200 + (now % 3200);
            frame = &codex_pet_idle0;
        }
        break;
    case PetAnim::Idle:
    default:
        switch ((now / 820) % 24) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
            frame = &codex_pet_idle0;
            break;
        case 6:
        case 7:
            frame = &codex_pet_idle1;
            break;
        case 9:
            frame = &codex_pet_idle2;
            break;
        case 10:
            frame = &codex_pet_idle0;
            break;
        case 12:
            frame = &codex_pet_idle3;
            break;
        case 13:
            frame = &codex_pet_idle0;
            break;
        case 15:
        case 16:
            frame = &codex_pet_idle5;
            break;
        case 19:
            frame = &codex_pet_idle4;
            break;
        default:
            frame = &codex_pet_idle1;
            break;
        }
        break;
    }
    setPetFrame(frame);
    _shake_energy *= 0.90f;
    if (_shake_energy < 0.03f) {
        _shake_energy = 0.0f;
    }

    if (now - _last_idle_tick > 500) {
        _last_idle_tick = now;
        if (_pet_image) {
            const int opa = now < _pet_effect_until_tick ? 230 + static_cast<int>(effectPulse * 25.0f) : 255;
            _pet_image->setOpa(static_cast<lv_opa_t>(std::clamp(opa, 0, 255)));
        }
        if (_pet_screen) {
            _pet_screen->setBgColor(lv_color_hex(0x020A11));
        }
        if (_pet_face) {
            if (_pet_anim == PetAnim::Touch) {
                _pet_face->setText(">_<");
            } else if (_pet_anim == PetAnim::LookAround) {
                _pet_face->setText("> ");
            } else if (_pet_anim == PetAnim::Stretch) {
                _pet_face->setText("^_");
            } else if (_voice_mode == VoiceMode::Interrupted) {
                _pet_face->setText("!!");
            } else if (_voice_mode != VoiceMode::Idle) {
                _pet_face->setText(">_");
            } else {
                _pet_face->setText(((now / 5000) % 2) == 0 ? ">_" : "> ");
            }
        }
    }
}

void CodexView::initVoiceWaveform()
{
    _voice_waveform = std::make_unique<Container>(_panel->get());
    _voice_waveform->setSize(188, 42);
    _voice_waveform->align(LV_ALIGN_TOP_MID, 0, _theme_mode == ThemeMode::OpenWatcherV2 ? 354 : 319);
    _voice_waveform->setBgOpa(LV_OPA_TRANSP);
    _voice_waveform->setBorderWidth(0);
    _voice_waveform->setPaddingAll(0);
    _voice_waveform->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _voice_waveform->setHidden(true);

    constexpr int kBarCount = 9;
    constexpr int kBarWidth = 8;
    constexpr int kGap = 12;
    constexpr int kStartX = 42;
    for (int i = 0; i < kBarCount; ++i) {
        auto bar = std::make_unique<Container>(_voice_waveform->get());
        bar->setSize(kBarWidth, 10);
        bar->setRadius(5);
        bar->setBorderWidth(0);
        bar->setBgColor(i % 2 == 0 ? lv_color_hex(kOwBlue) : lv_color_hex(kColorWeek));
        bar->setBgOpa(220);
        bar->setShadowWidth(0);
        bar->align(LV_ALIGN_TOP_LEFT, kStartX + i * kGap, 16);
        bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _voice_bars[i] = std::move(bar);
    }
}

void CodexView::updateVoiceWaveform()
{
    if ((_voice_mode != VoiceMode::Recording && _voice_mode != VoiceMode::Processing) ||
        !_voice_waveform || lv_obj_has_flag(_voice_waveform->get(), LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    constexpr int kMaxHeight = 36;
    constexpr int kMinHeight = 8;
    constexpr int kCenterY = 21;
    const uint32_t now = GetHAL().millis();
    for (size_t i = 0; i < _voice_bars.size(); ++i) {
        if (!_voice_bars[i]) {
            continue;
        }
        float wave = 0.0f;
        if (_voice_mode == VoiceMode::Processing) {
            const float cursor = static_cast<float>((now / 90) % 18) / 2.0f;
            const float distance = std::fabs(cursor - static_cast<float>(i));
            wave = std::max(0.12f, 1.0f - distance * 0.32f);
        } else {
            const float phase = static_cast<float>((now / 65 + i * 3) % 18) / 18.0f;
            wave = 0.5f + 0.5f * std::sin((phase * 6.283185f) + static_cast<float>(i) * 0.78f);
        }
        const int height = kMinHeight + static_cast<int>(std::lround((kMaxHeight - kMinHeight) * wave));
        const int opacity = 150 + static_cast<int>(wave * 90.0f);
        if (_voice_bar_heights[i] == height && _voice_bar_opacities[i] == opacity) {
            continue;
        }
        _voice_bar_heights[i] = height;
        _voice_bar_opacities[i] = opacity;
        _voice_bars[i]->setSize(8, height);
        _voice_bars[i]->align(LV_ALIGN_TOP_LEFT, 42 + static_cast<int>(i) * 12, kCenterY - height / 2);
        _voice_bars[i]->setBgOpa(opacity);
    }
}

void CodexView::setPetFrame(const void* src)
{
    if (!_pet_image || src == nullptr || src == _pet_current_src) {
        return;
    }
    _pet_image->setSrc(src);
    _pet_current_src = src;
}

void CodexView::updateMotionInput()
{
    const uint32_t now = GetHAL().millis();
    if (now - _last_imu_update_tick < 50) {
        return;
    }
    _last_imu_update_tick = now;

    GetHAL().updateImuData();
    const auto& imu = GetHAL().getImuData();

    const float targetTiltX = std::clamp(imu.accelX, -1.0f, 1.0f);
    const float targetTiltY = std::clamp(imu.accelY, -1.0f, 1.0f);
    _tilt_x += (targetTiltX - _tilt_x) * codex_config::kTiltFilterAlpha;
    _tilt_y += (targetTiltY - _tilt_y) * codex_config::kTiltFilterAlpha;

    const float gyroMotion = std::sqrt(imu.gyroX * imu.gyroX + imu.gyroY * imu.gyroY + imu.gyroZ * imu.gyroZ);
    if (gyroMotion > codex_config::kShakeMotionFloor) {
        const float normalized = std::clamp((gyroMotion - codex_config::kShakeMotionFloor) / (codex_config::kShakeThreshold * 1.4f), 0.0f, 1.0f);
        _shake_energy = std::max(_shake_energy, normalized);
    }
    if (gyroMotion > codex_config::kShakeThreshold) {
        _shake_trigger_count = std::min<uint8_t>(_shake_trigger_count + 1, 3);
    } else if (gyroMotion < codex_config::kShakeThreshold * 0.65f) {
        _shake_trigger_count = 0;
    }

    if (_shake_trigger_count >= 2 && now - _last_shake_tick > codex_config::kShakeCooldownMs) {
        _last_shake_tick = now;
        _shake_trigger_count = 0;
        _clear_input_requested = true;
        _pet_effect_until_tick = now + 420;
        _shake_energy = 1.0f;
        setMessageImage(&codex_pet_msg_touch_1, 1300);
        GetHAL().vibrate(55, 140);
    }
}

void CodexView::setMessage(const char* message, uint32_t ttlMs)
{
    _message_image_active      = false;
    _state.message              = message;
    _state.messageExpiresAtMs   = GetHAL().millis() + ttlMs;
    const bool voice_visible = _voice_mode != VoiceMode::Idle;
    if (_message_image) {
        _message_image->setHidden(true);
    }
    if (_message_label) {
        _message_label->setHidden(voice_visible);
        _message_label->setText(_state.message.c_str());
    }
}

void CodexView::setMessageImage(const void* src, uint32_t ttlMs)
{
    if (!src || !_message_image) {
        return;
    }

    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        _message_image_active = false;
        _message_image->setHidden(true);
        return;
    }

    _message_image_active = true;
    _state.messageExpiresAtMs = ttlMs == 0 ? 0 : GetHAL().millis() + ttlMs;
    _message_image->setSrc(src);
    _message_image->setHidden(_voice_mode != VoiceMode::Idle);
    if (_message_label) {
        _message_label->setHidden(true);
    }
}

void CodexView::setMessageText(const std::string& message)
{
    if (_theme_mode == ThemeMode::OpenWatcherV2) {
        _message_image_active = false;
        if (_message_image) {
            _message_image->setHidden(true);
        }
        if (message.empty() || message == "Ready") {
            _state.message.clear();
            _state.messageExpiresAtMs = 0;
            if (_message_label) {
                _message_label->setText("");
                _message_label->setHidden(true);
            }
            return;
        }

        _state.message = message;
        _state.messageExpiresAtMs = GetHAL().millis() + 2200;
        if (_message_label) {
            _message_label->setText(_state.message.c_str());
            _message_label->setHidden(_voice_mode != VoiceMode::Idle);
        }
        return;
    }

    if (message.empty() || message == "Ready") {
        setIdleMessageImage(++_message_phrase_counter + GetHAL().millis());
        return;
    }

    _message_image_active = false;
    if (_message_image) {
        _message_image->setHidden(true);
    }
    _state.message = message;
    const bool voice_visible = _voice_mode != VoiceMode::Idle;
    _state.messageExpiresAtMs = 0;
    if (_message_label) {
        _message_label->setHidden(voice_visible);
        _message_label->setText(_state.message.c_str());
    }
}

void CodexView::setIdleMessageImage(uint32_t salt)
{
    _state.message = pick_idle_phrase(salt);
    setMessageImage(pick_idle_asset(salt), 0);
}

void CodexView::updateConnectionDots()
{
    if (_ble_dot) {
        _ble_dot->setOpa(_state.bleConnected ? 255 : 90);
    }
    if (_wifi_dot) {
        _wifi_dot->setOpa(_state.wifiConnected ? 255 : 90);
    }
}

float CodexView::remainingRatio(const QuotaSlot& slot) const
{
    if (slot.limit <= 0.0f) {
        return 0.0f;
    }
    return clamp01((slot.limit - slot.used) / slot.limit);
}
