/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "watch_face.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <array>
#include <ctime>
#include <cstdio>

using namespace uitk;
using namespace uitk::lvgl_cpp;

using namespace view;

namespace {

constexpr int _hour_x_offset   = -117;
constexpr int _minute_x_offset = 0;
constexpr int _second_x_offset = 117;
constexpr int _flow_y_offset   = -22;
constexpr int _date_y_offset   = 76;
constexpr int _mask_size       = 466;
constexpr int _panel_width     = 100;
constexpr int _panel_height    = 132;
constexpr int _panel_radius    = 42;

struct Theme {
    uint32_t backgroundColor;
    uint32_t panelColor;
    uint32_t timeColor;
    uint32_t dateColor;
};

constexpr std::array<const char*, 7> _weekday_map = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

constexpr std::array<Theme, 10> _themes = {
    Theme{0x000000, 0x1A1919, 0xFFFFFF, 0xA0A0A0},  //
    Theme{0xEEEEEE, 0xE4E3E3, 0x484848, 0x525252},  //
    Theme{0xC2EFEB, 0xB1E1DD, 0x566E6C, 0x6A938F},  //
    Theme{0xEEE0CB, 0xD9CAB2, 0x6A5E4D, 0x948063},  //
    Theme{0xB1CC74, 0xA4BD6B, 0x4B5E20, 0x668323},  //
    Theme{0x8A89C0, 0x8180B4, 0xDFDEFF, 0xCBCBFF},  //
    Theme{0xF26419, 0xF6722C, 0xFFEFE6, 0xFFD3BC},  //
    Theme{0xF2D7EE, 0xE9C9E5, 0x83627E, 0xA4789D},  //
    Theme{0x048A81, 0x05968C, 0xB1FFFA, 0x70CFC9},  //
    Theme{0x9DB4C0, 0x92A8B3, 0xECF8FF, 0xDCF3FF},  //
};

void setup_panel(Container& panel, int x_offset, int y_offset)
{
    panel.align(LV_ALIGN_CENTER, x_offset, y_offset);
    panel.setSize(_panel_width, _panel_height);
    panel.setRadius(_panel_radius);
    panel.setBorderWidth(0);
    panel.setOutlineWidth(0);
    panel.setShadowWidth(0);
    panel.setPaddingAll(0);
    panel.setBgOpa(LV_OPA_COVER);
    panel.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

void setup_number_flow(NumberFlow& number_flow)
{
    number_flow.minDigits = 2;
    number_flow.setAlign(LV_ALIGN_CENTER);
    number_flow.setPos(0, 0);
    number_flow.setTextFont(&CommissionerMedium64);
    number_flow.setTextColor(lv_color_hex(0xFFFFFF));
    number_flow.init();
}

}  // namespace

void WatchFaceNumberFlow::onCreate(lv_obj_t* parent)
{
    _theme_index = 0;

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

    _hour_panel = std::make_unique<Container>(parent);
    setup_panel(*_hour_panel, _hour_x_offset, _flow_y_offset);
    _hour_flow = std::make_unique<NumberFlow>(_hour_panel->get());
    setup_number_flow(*_hour_flow);

    _minute_panel = std::make_unique<Container>(parent);
    setup_panel(*_minute_panel, _minute_x_offset, _flow_y_offset);
    _minute_flow = std::make_unique<NumberFlow>(_minute_panel->get());
    setup_number_flow(*_minute_flow);

    _second_panel = std::make_unique<Container>(parent);
    setup_panel(*_second_panel, _second_x_offset, _flow_y_offset);
    _second_flow = std::make_unique<NumberFlow>(_second_panel->get());
    setup_number_flow(*_second_flow);

    _date_label = std::make_unique<Label>(parent);
    _date_label->align(LV_ALIGN_CENTER, 0, _date_y_offset);
    _date_label->setTextFont(&lv_font_montserrat_24);
    _date_label->setTextColor(lv_color_hex(0xA0A0A0));
    _date_label->setBgOpa(LV_OPA_TRANSP);
    _date_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _click_mask = std::make_unique<Container>(parent);
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_mask_size, _mask_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycle_theme(); });
    _click_mask->moveForeground();

    apply_theme();

    _last_tick = GetHAL().millis();
    update();
}

void WatchFaceNumberFlow::onUpdate()
{
    if (_hour_flow) {
        _hour_flow->update();
    }
    if (_minute_flow) {
        _minute_flow->update();
    }
    if (_second_flow) {
        _second_flow->update();
    }

    if (GetHAL().millis() - _last_tick > 1000) {
        _last_tick = GetHAL().millis();
        update();
    }
}

void WatchFaceNumberFlow::onDestroy()
{
    _click_mask.reset();
    _date_label.reset();
    _second_flow.reset();
    _minute_flow.reset();
    _hour_flow.reset();
    _second_panel.reset();
    _minute_panel.reset();
    _hour_panel.reset();
    _background_panel.reset();
}

void WatchFaceNumberFlow::update()
{
    std::time_t now    = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    if (localTime == nullptr) {
        return;
    }

    if (_hour_flow) {
        _hour_flow->setValue(localTime->tm_hour);
    }
    if (_minute_flow) {
        _minute_flow->setValue(localTime->tm_min);
    }
    if (_second_flow) {
        _second_flow->setValue(localTime->tm_sec);
    }

    if (_date_label && localTime->tm_wday >= 0 && localTime->tm_wday < static_cast<int>(_weekday_map.size())) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %s", localTime->tm_year + 1900, localTime->tm_mon + 1,
                      localTime->tm_mday, _weekday_map[localTime->tm_wday]);
        _date_label->setText(buffer);
    }
}

void WatchFaceNumberFlow::cycle_theme()
{
    _theme_index++;
    if (_theme_index >= static_cast<int>(_themes.size())) {
        _theme_index = 0;
    }

    apply_theme();
}

void WatchFaceNumberFlow::apply_theme()
{
    const Theme& theme = _themes[_theme_index];

    if (_background_panel) {
        _background_panel->setBgColor(lv_color_hex(theme.backgroundColor));
    }

    for (auto* panel : {_hour_panel.get(), _minute_panel.get(), _second_panel.get()}) {
        if (panel) {
            panel->setBgColor(lv_color_hex(theme.panelColor));
        }
    }

    for (auto* flow : {_hour_flow.get(), _minute_flow.get(), _second_flow.get()}) {
        if (flow) {
            flow->setDigitColor(lv_color_hex(theme.timeColor));
        }
    }

    if (_date_label) {
        _date_label->setTextColor(lv_color_hex(theme.dateColor));
    }
}
