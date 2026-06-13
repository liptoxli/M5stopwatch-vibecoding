/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_width  = 466;
constexpr int _panel_height = 466;
constexpr int _ok_width     = 333;
constexpr int _ok_height    = 103;

constexpr uint32_t _bg_color      = 0x000000;
constexpr uint32_t _time_color    = 0xFFFFFF;
constexpr uint32_t _ok_color      = 0x4AD78C;
constexpr uint32_t _ok_text_color = 0x0F5831;

std::string format_alarm_time(const model::AlarmClock::Time24& time)
{
    return fmt::format("{:02d}:{:02d}", time.hour, time.minute);
}

}  // namespace

void AlarmTriggerView::init(const model::AlarmClock::Time24& time)
{
    _is_confirmed = false;

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_width, _panel_height);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _panel->moveForeground();

    _icon = std::make_unique<Image>(_panel->get());
    _icon->setSrc(&alarm_icon);
    _icon->align(LV_ALIGN_TOP_LEFT, 172, 90);
    _icon->moveForeground();

    _time_label = std::make_unique<Label>(_panel->get());
    _time_label->setText(format_alarm_time(time));
    _time_label->setTextFont(&CommissionerMedium108);
    _time_label->setTextColor(lv_color_hex(_time_color));
    _time_label->align(LV_ALIGN_CENTER, 0, -48);
    _time_label->moveForeground();

    _ok_button = std::make_unique<Button>(_panel->get());
    _ok_button->setSize(_ok_width, _ok_height);
    _ok_button->setBgColor(lv_color_hex(_ok_color));
    _ok_button->setBorderWidth(0);
    _ok_button->setShadowWidth(0);
    _ok_button->setRadius(LV_RADIUS_CIRCLE);
    _ok_button->align(LV_ALIGN_CENTER, 0, 101);
    _ok_button->label().setText("OK");
    _ok_button->label().setTextFont(&lv_font_montserrat_28);
    _ok_button->label().setTextColor(lv_color_hex(_ok_text_color));
    _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _ok_button->onClick().connect([this]() { _is_confirmed = true; });
    _ok_button->moveForeground();
}