/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "watch_face.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <array>
#include <cmath>
#include <ctime>
#include <cstdio>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace view;

namespace {

constexpr int _mask_size             = 466;
constexpr int _time_y_offset         = -24;
constexpr int _date_y_offset         = 57;
constexpr int _tips_y_offset         = 130;
constexpr int _orbit_ball_size       = 14;
constexpr int _orbit_ball_radius     = 214;
constexpr uint32_t _tips_duration_ms = 3000;
constexpr float _pi                  = 3.14159265358979323846f;

struct Theme {
    uint32_t backgroundColor;
    uint32_t ballColor;
    uint32_t timeColor;
    uint32_t dateColor;
};

constexpr std::array<const char*, 7> _weekday_map = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

constexpr std::array<Theme, 10> _themes = {
    Theme{0x000000, 0xA0A0A0, 0xFFFFFF, 0xA0A0A0},  //
    Theme{0xEEEEEE, 0x525252, 0x484848, 0x525252},  //
    Theme{0xC2EFEB, 0x6A938F, 0x566E6C, 0x6A938F},  //
    Theme{0xEEE0CB, 0x948063, 0x6A5E4D, 0x948063},  //
    Theme{0xB1CC74, 0x668323, 0x4B5E20, 0x668323},  //
    Theme{0x8A89C0, 0xCBCBFF, 0xDFDEFF, 0xCBCBFF},  //
    Theme{0xF26419, 0xFFD3BC, 0xFFEFE6, 0xFFD3BC},  //
    Theme{0xF2D7EE, 0xA4789D, 0x83627E, 0xA4789D},  //
    Theme{0x048A81, 0x70CFC9, 0xB1FFFA, 0x70CFC9},  //
    Theme{0x9DB4C0, 0xDCF3FF, 0xECF8FF, 0xDCF3FF},  //
};

void setup_second_ball(Container& ball)
{
    ball.setSize(_orbit_ball_size, _orbit_ball_size);
    ball.setBgOpa(LV_OPA_COVER);
    ball.setBorderWidth(0);
    ball.setOutlineWidth(0);
    ball.setShadowWidth(0);
    ball.setPaddingAll(0);
    ball.setRadius(LV_RADIUS_CIRCLE);
    ball.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

float unwrap_angle(float previous_unwrapped_angle, float next_angle)
{
    float previous_angle = std::fmod(previous_unwrapped_angle, 360.0f);
    if (previous_angle < 0.0f) {
        previous_angle += 360.0f;
    }

    float delta = std::fmod(next_angle - previous_angle, 360.0f);
    if (delta < -180.0f) {
        delta += 360.0f;
    } else if (delta > 180.0f) {
        delta -= 360.0f;
    }

    return previous_unwrapped_angle + delta;
}

}  // namespace

void WatchFaceSimple::handleLongPressed(lv_event_t* e)
{
    auto* view = static_cast<WatchFaceSimple*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    view->toggle_second_ball();
}

void WatchFaceSimple::onCreate(lv_obj_t* parent)
{
    _theme_index             = 0;
    _orbit_angle_initialized = false;
    _orbit_angle_unwrapped   = 0.0f;

    _orbit_angle_anim.pause();
    _orbit_angle_anim.springOptions().visualDuration = 0.3;
    _orbit_angle_anim.springOptions().bounce         = 0.4;
    _orbit_angle_anim.teleport(0.0f);
    _orbit_angle_anim.play();

    _background_panel = std::make_unique<Container>(parent);
    _background_panel->align(LV_ALIGN_CENTER, 0, 0);
    _background_panel->setSize(_mask_size, _mask_size);
    _background_panel->setRadius(0);
    _background_panel->setBorderWidth(0);
    _background_panel->setOutlineWidth(0);
    _background_panel->setShadowWidth(0);
    _background_panel->setPaddingAll(0);
    _background_panel->setBgOpa(LV_OPA_COVER);
    _background_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _time_label = std::make_unique<Label>(parent);
    _time_label->align(LV_ALIGN_CENTER, 0, _time_y_offset);
    _time_label->setTextFont(&CommissionerMedium108);
    _time_label->setBgOpa(LV_OPA_TRANSP);
    _time_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _date_label = std::make_unique<Label>(parent);
    _date_label->align(LV_ALIGN_CENTER, 0, _date_y_offset);
    _date_label->setTextFont(&lv_font_montserrat_24);
    _date_label->setBgOpa(LV_OPA_TRANSP);
    _date_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _tips_label = std::make_unique<Label>(parent);
    _tips_label->align(LV_ALIGN_CENTER, 0, _tips_y_offset);
    _tips_label->setTextFont(&lv_font_montserrat_20);
    _tips_label->setBgOpa(LV_OPA_TRANSP);
    _tips_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _tips_label->setText("Long press\nto toggle second dot");
    _tips_label->setTextColor(lv_color_hex(0xEAE48D));
    _tips_label->setTextAlign(LV_TEXT_ALIGN_CENTER);

    _second_ball = std::make_unique<Container>(parent);
    setup_second_ball(*_second_ball);

    _click_mask = std::make_unique<Container>(parent);
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_mask_size, _mask_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() {
        if (_suppress_next_click) {
            _suppress_next_click = false;
            return;
        }

        cycle_theme();
    });
    _click_mask->addEventCb(handleLongPressed, LV_EVENT_LONG_PRESSED, this);
    _click_mask->moveForeground();

    apply_theme();

    _last_tick           = 0;
    _show_second_ball    = true;
    _suppress_next_click = false;
    _show_tips           = true;
    _tips_hide_tick      = GetHAL().millis() + _tips_duration_ms;
    update();
}

void WatchFaceSimple::onUpdate()
{
    if (_tips_label && _show_tips && GetHAL().millis() >= _tips_hide_tick) {
        _tips_label->setHidden(true);
        _show_tips = false;
    }

    if (!_orbit_angle_anim.done()) {
        apply_second_ball_state();
    }

    if (GetHAL().millis() - _last_tick > 1000) {
        update();
    }
}

void WatchFaceSimple::onDestroy()
{
    _click_mask.reset();
    _second_ball.reset();
    _tips_label.reset();
    _date_label.reset();
    _time_label.reset();
    _background_panel.reset();
}

void WatchFaceSimple::update()
{
    std::time_t now     = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    if (local_time == nullptr) {
        return;
    }

    _last_tick = GetHAL().millis();

    if (_time_label) {
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local_time->tm_hour, local_time->tm_min);
        _time_label->setText(buffer);
    }

    if (_date_label && local_time->tm_wday >= 0 && local_time->tm_wday < static_cast<int>(_weekday_map.size())) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %s", local_time->tm_year + 1900, local_time->tm_mon + 1,
                      local_time->tm_mday, _weekday_map[local_time->tm_wday]);
        _date_label->setText(buffer);
    }

    setOrbitAngle(static_cast<float>(local_time->tm_sec * 6));
    apply_second_ball_state();
}

void WatchFaceSimple::apply_second_ball_state()
{
    if (_second_ball == nullptr) {
        return;
    }

    _second_ball->setHidden(!_show_second_ball);
    if (!_show_second_ball) {
        return;
    }

    float radians = (static_cast<float>(_orbit_angle_anim) - 90.0f) * _pi / 180.0f;
    int offset_x  = static_cast<int>(std::lround(_orbit_ball_radius * std::cos(radians)));
    int offset_y  = static_cast<int>(std::lround(_orbit_ball_radius * std::sin(radians)));
    _second_ball->align(LV_ALIGN_CENTER, offset_x, offset_y);
}

void WatchFaceSimple::setOrbitAngle(float angle)
{
    if (!_orbit_angle_initialized) {
        _orbit_angle_unwrapped   = angle;
        _orbit_angle_initialized = true;
    } else {
        _orbit_angle_unwrapped = unwrap_angle(_orbit_angle_unwrapped, angle);
    }

    _orbit_angle_anim = _orbit_angle_unwrapped;
}

void WatchFaceSimple::toggle_second_ball()
{
    _show_second_ball    = !_show_second_ball;
    _suppress_next_click = true;
    apply_second_ball_state();
}

void WatchFaceSimple::cycle_theme()
{
    _theme_index++;
    if (_theme_index >= static_cast<int>(_themes.size())) {
        _theme_index = 0;
    }

    apply_theme();
}

void WatchFaceSimple::apply_theme()
{
    const Theme& theme = _themes[_theme_index];

    if (_background_panel) {
        _background_panel->setBgColor(lv_color_hex(theme.backgroundColor));
    }

    if (_time_label) {
        _time_label->setTextColor(lv_color_hex(theme.timeColor));
    }

    if (_date_label) {
        _date_label->setTextColor(lv_color_hex(theme.dateColor));
    }

    if (_second_ball) {
        _second_ball->setBgColor(lv_color_hex(theme.ballColor));
    }
}
