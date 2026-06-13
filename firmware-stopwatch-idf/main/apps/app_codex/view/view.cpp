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
#include <ctime>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr uint32_t kColorClock   = 0xCDEBF9;
constexpr uint32_t kColor5h      = 0x45D9FF;
constexpr uint32_t kColorWeek    = 0x8FB6FF;
constexpr uint32_t kColorTrack   = 0x25314A;
constexpr uint32_t kColorText    = 0xE8F8FF;
constexpr uint32_t kColorTextDim = 0x7F98A3;
constexpr uint32_t kColorPetBody = 0x172847;
constexpr uint32_t kColorPetFace = 0xD9FBFF;
constexpr uint32_t kPetTouchEffectMs = 1050;
constexpr uint32_t kPetIdleFrameMs = 50;
constexpr uint32_t kPetActiveFrameMs = 33;

void setup_clock_digit_panel(Container& panel, int x)
{
    panel.align(LV_ALIGN_TOP_MID, x, 8);
    panel.setSize(66, 54);
    panel.setRadius(22);
    panel.setBorderWidth(0);
    panel.setBgColor(lv_color_hex(0x0A1B26));
    panel.setBgOpa(210);
    panel.setShadowWidth(12);
    panel.setShadowColor(lv_color_hex(0x17465B));
    panel.setShadowOpa(70);
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

void CodexView::init(lv_obj_t* parent)
{
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), LV_PART_MAIN);

    _state.fiveHour = {1.0f, 1.0f, "--"};
    _state.weekly   = {1.0f, 1.0f, "--"};
    _state.message  = "Waiting...";

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(0x000000));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    initFlipClock();

    initQuotaBar(_bar_5h, "5H", _state.fiveHour.resetInLabel.c_str(), true, lv_color_hex(kColor5h));
    initQuotaBar(_bar_week, "WEEK", _state.weekly.resetInLabel.c_str(), false, lv_color_hex(kColorWeek));

    initPet();

    _message_label = std::make_unique<Label>(_panel->get());
    _message_label->setText(_state.message.c_str());
    _message_label->setTextFont(&lv_font_montserrat_24);
    _message_label->setTextColor(lv_color_hex(kColorText));
    _message_label->setWidth(260);
    _message_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _message_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _message_label->align(LV_ALIGN_TOP_MID, 0, 327);

    _message_image = std::make_unique<Image>(_panel->get());
    _message_image->setSrc(&codex_pet_msg_idle_0);
    _message_image->align(LV_ALIGN_TOP_MID, 0, 323);
    _message_image->setHidden(false);
    _message_image->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _message_image_active = true;
    if (_message_label) {
        _message_label->setHidden(true);
    }

    initVoiceWaveform();

    _ble_dot = std::make_unique<Container>(_panel->get());
    _ble_dot->setSize(10, 10);
    _ble_dot->setRadius(LV_RADIUS_CIRCLE);
    _ble_dot->setBorderWidth(0);
    _ble_dot->setBgColor(lv_color_hex(0x2F79FF));
    _ble_dot->align(LV_ALIGN_BOTTOM_MID, -12, -34);

    _wifi_dot = std::make_unique<Container>(_panel->get());
    _wifi_dot->setSize(10, 10);
    _wifi_dot->setRadius(LV_RADIUS_CIRCLE);
    _wifi_dot->setBorderWidth(0);
    _wifi_dot->setBgColor(lv_color_hex(0x38E07D));
    _wifi_dot->align(LV_ALIGN_BOTTOM_MID, 12, -34);

    updateConnectionDots();
    updateQuotaBar(_bar_5h, _state.fiveHour, true, lv_color_hex(kColor5h), "5H");
    updateQuotaBar(_bar_week, _state.weekly, false, lv_color_hex(kColorWeek), "WEEK");
    GetHAL().updateImuData();
}

void CodexView::update()
{
    updateFlipClock();

    const uint32_t now = GetHAL().millis();
    if (_state.messageExpiresAtMs != 0 && now > _state.messageExpiresAtMs) {
        _state.messageExpiresAtMs = 0;
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

    updateMotionInput();

    const bool pet_active = _voice_mode != VoiceMode::Idle ||
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
    updateVoiceWaveform();
}

void CodexView::applyQuota(int fiveHourLeftPct,
                           int weeklyLeftPct,
                           const std::string& fiveHourResetLabel,
                           const std::string& weeklyResetLabel,
                           bool valid,
                           bool wifiConnected,
                           bool processing,
                           const std::string& message)
{
    _state.wifiConnected = wifiConnected;
    _state.processing = processing;
    _last_quota_update_tick = GetHAL().millis();

    if (valid) {
        _state.fiveHour.used = 100.0f - static_cast<float>(std::clamp(fiveHourLeftPct, 0, 100));
        _state.fiveHour.limit = 100.0f;
        _state.fiveHour.resetInLabel = fiveHourResetLabel.empty() ? "--" : fiveHourResetLabel;
        _state.weekly.used = 100.0f - static_cast<float>(std::clamp(weeklyLeftPct, 0, 100));
        _state.weekly.limit = 100.0f;
        _state.weekly.resetInLabel = weeklyResetLabel.empty() ? "--" : weeklyResetLabel;
    } else {
        _state.fiveHour.used = 100.0f;
        _state.fiveHour.limit = 100.0f;
        _state.fiveHour.resetInLabel = "--";
        _state.weekly.used = 100.0f;
        _state.weekly.limit = 100.0f;
        _state.weekly.resetInLabel = "--";
    }

    updateConnectionDots();
    updateQuotaBar(_bar_5h, _state.fiveHour, true, lv_color_hex(kColor5h), "5H");
    updateQuotaBar(_bar_week, _state.weekly, false, lv_color_hex(kColorWeek), "WEEK");
    setMessageText(message);
    if (_state.processing) {
        setMessageImage(pick_processing_asset(++_message_phrase_counter + GetHAL().millis()), 2400);
    }
}

void CodexView::applyBleState(bool connected, const std::string& hostMessage, bool hostMessageChanged)
{
    if (_state.bleConnected != connected) {
        _state.bleConnected = connected;
        updateConnectionDots();
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
    const bool visible = mode != VoiceMode::Idle;
    if (_voice_waveform) {
        _voice_waveform->setHidden(!visible);
    }
    if (_message_label) {
        _message_label->setHidden(visible || _message_image_active);
    }
    if (_message_image) {
        _message_image->setHidden(visible || !_message_image_active);
    }
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

void CodexView::initQuotaBar(EdgeQuotaBar& bar,
                             const char* title,
                             const char* resetLabel,
                             bool leftSide,
                             lv_color_t color)
{
    const int x = leftSide ? 0 : 352;

    bar.trackImage = std::make_unique<Image>(_panel->get());
    bar.trackImage->setSrc(leftSide ? static_cast<const void*>(&codex_quota_left_track)
                                    : static_cast<const void*>(&codex_quota_right_track));
    bar.trackImage->align(LV_ALIGN_TOP_LEFT, x, 56);
    bar.trackImage->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    bar.fillClip = std::make_unique<Container>(_panel->get());
    bar.fillClip->setSize(118, 1);
    bar.fillClip->align(LV_ALIGN_TOP_LEFT, x, 56);
    bar.fillClip->setBgOpa(LV_OPA_TRANSP);
    bar.fillClip->setBorderWidth(0);
    bar.fillClip->setPaddingAll(0);
    bar.fillClip->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    bar.fillImage = std::make_unique<Image>(bar.fillClip->get());
    bar.fillImage->setSrc(leftSide ? static_cast<const void*>(&codex_quota_left_fill)
                                   : static_cast<const void*>(&codex_quota_right_fill));
    bar.fillImage->align(LV_ALIGN_TOP_LEFT, 0, 0);
    bar.fillImage->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    bar.percentLabel = std::make_unique<Label>(_panel->get());
    bar.percentLabel->setText("--%");
    bar.percentLabel->setTextFont(&MontserratSemiBold26);
    bar.percentLabel->setTextColor(color);
    bar.percentLabel->setWidth(86);
    bar.percentLabel->setTextAlign(LV_TEXT_ALIGN_CENTER);
    bar.percentLabel->align(LV_ALIGN_TOP_LEFT, leftSide ? 104 : 276, 84);

    bar.title = std::make_unique<Label>(_panel->get());
    bar.title->setText(title);
    bar.title->setTextFont(&MontserratSemiBold26);
    bar.title->setTextColor(color);
    bar.title->setWidth(96);
    bar.title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    bar.title->align(LV_ALIGN_TOP_LEFT, leftSide ? 47 : 322, 191);

    bar.underline = std::make_unique<Container>(_panel->get());
    bar.underline->setSize(54, 2);
    bar.underline->setRadius(2);
    bar.underline->setBorderWidth(0);
    bar.underline->setBgColor(color);
    bar.underline->align(LV_ALIGN_TOP_LEFT, leftSide ? 70 : 345, 230);

    bar.resetLabel = std::make_unique<Label>(_panel->get());
    bar.resetLabel->setText(resetLabel);
    bar.resetLabel->setTextFont(&lv_font_montserrat_22);
    bar.resetLabel->setTextColor(lv_color_hex(kColorTextDim));
    bar.resetLabel->setWidth(116);
    bar.resetLabel->setTextAlign(LV_TEXT_ALIGN_CENTER);
    bar.resetLabel->align(LV_ALIGN_TOP_LEFT, leftSide ? 37 : 312, 245);
}

void CodexView::drawQuotaBar(EdgeQuotaBar& bar, bool leftSide, lv_color_t color, float remaining)
{
    if (!bar.fillClip || !bar.fillImage) {
        return;
    }

    constexpr int imageW = 118;
    constexpr int imageH = 330;
    const float remainingClamped = clamp01(remaining);
    const int visibleH = static_cast<int>(std::lround(imageH * remainingClamped));
    const int hiddenTop = imageH - visibleH;

    bar.fillClip->setSize(imageW, std::max(1, visibleH));
    bar.fillClip->align(LV_ALIGN_TOP_LEFT, leftSide ? 0 : 352, 56 + hiddenTop);
    bar.fillClip->setHidden(visibleH <= 0);
    bar.fillImage->align(LV_ALIGN_TOP_LEFT, 0, -hiddenTop);
}

void CodexView::updateQuotaBar(EdgeQuotaBar& bar,
                               const QuotaSlot& slot,
                               bool leftSide,
                               lv_color_t color,
                               const char* title)
{
    if (bar.title) {
        bar.title->setText(title);
    }
    if (bar.resetLabel) {
        bar.resetLabel->setText(slot.resetInLabel.c_str());
    }

    const float ratio = remainingRatio(slot);
    if (bar.percentLabel) {
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::lround(ratio * 100.0f)));
        bar.percentLabel->setText(pct);
    }
    if (std::abs(ratio - bar.renderedRatio) > 0.005f) {
        drawQuotaBar(bar, leftSide, color, ratio);
        bar.renderedRatio = ratio;
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
        _pet_hit_area->align(LV_ALIGN_TOP_MID, tiltX + shakeX, 118 + yOffset + tiltY);
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
    _voice_waveform->align(LV_ALIGN_TOP_MID, 0, 319);
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
        bar->setBgColor(i % 2 == 0 ? lv_color_hex(kColor5h) : lv_color_hex(kColorWeek));
        bar->setBgOpa(220);
        bar->setShadowWidth(8);
        bar->setShadowColor(lv_color_hex(0x386FFF));
        bar->setShadowOpa(70);
        bar->align(LV_ALIGN_TOP_LEFT, kStartX + i * kGap, 16);
        bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _voice_bars[i] = std::move(bar);
    }
}

void CodexView::updateVoiceWaveform()
{
    if (_voice_mode == VoiceMode::Idle || !_voice_waveform || lv_obj_has_flag(_voice_waveform->get(), LV_OBJ_FLAG_HIDDEN)) {
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
        _voice_bars[i]->setSize(8, height);
        _voice_bars[i]->align(LV_ALIGN_TOP_LEFT, 42 + static_cast<int>(i) * 12, kCenterY - height / 2);
        _voice_bars[i]->setBgOpa(150 + static_cast<int>(wave * 90.0f));
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
