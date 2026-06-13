/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_width     = 466;
constexpr int _panel_height    = 466;
constexpr int _selector_width  = 160;
constexpr int _selector_height = 200;
constexpr int _ok_width        = 374;
constexpr int _ok_height       = 130;

constexpr uint32_t _bg_color                = 0x000000;
constexpr uint32_t _label_color             = 0xFFFFFF;
constexpr uint32_t _selector_color          = 0x343434;
constexpr uint32_t _selector_selected_color = 0x696969;
constexpr uint32_t _ok_color                = 0x4AD78C;
constexpr uint32_t _ok_text_color           = 0x0F5831;

std::string build_number_options(int begin, int end)
{
    std::string options;
    for (int value = begin; value <= end; ++value) {
        char buffer[4] = {};
        std::snprintf(buffer, sizeof(buffer), "%02d", value);
        if (!options.empty()) {
            options.push_back('\n');
        }
        options += buffer;
    }
    return options;
}

}  // namespace

void AlarmAddView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_width, _panel_height);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->align(LV_ALIGN_TOP_MID, 0, 75);
    _title_label->setText("Add Alarm");
    _title_label->setTextFont(&MontserratSemiBold26);
    _title_label->setTextColor(lv_color_hex(_label_color));

    const auto hour_options = build_number_options(0, 23);
    _hour_selector          = std::make_unique<Roller>(_panel->get());
    _hour_selector->align(LV_ALIGN_CENTER, -90, -13);
    _hour_selector->setSize(_selector_width, _selector_height);
    _hour_selector->setOptions(hour_options.c_str(), LV_ROLLER_MODE_INFINITE);
    _hour_selector->setVisibleRowCount(5);
    _hour_selector->setSelected(7, LV_ANIM_OFF);
    lv_obj_set_style_radius(_hour_selector->get(), 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_hour_selector->get(), lv_color_hex(_selector_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_hour_selector->get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_hour_selector->get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(_hour_selector->get(), &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(_hour_selector->get(), lv_color_hex(_label_color), LV_PART_MAIN);
    lv_obj_set_style_text_align(_hour_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_hour_selector->get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_hour_selector->get(), lv_color_hex(_selector_selected_color), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(_hour_selector->get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(_hour_selector->get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(_hour_selector->get(), lv_color_hex(_label_color), LV_PART_SELECTED);
    lv_obj_set_style_text_align(_hour_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(_hour_selector->get(), 0, LV_PART_SELECTED);

    const auto minute_options = build_number_options(0, 59);
    _minute_selector          = std::make_unique<Roller>(_panel->get());
    _minute_selector->align(LV_ALIGN_CENTER, 90, -13);
    _minute_selector->setSize(_selector_width, _selector_height);
    _minute_selector->setOptions(minute_options.c_str(), LV_ROLLER_MODE_INFINITE);
    _minute_selector->setVisibleRowCount(5);
    _minute_selector->setSelected(0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_minute_selector->get(), 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_minute_selector->get(), lv_color_hex(_selector_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_minute_selector->get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_minute_selector->get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(_minute_selector->get(), &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(_minute_selector->get(), lv_color_hex(_label_color), LV_PART_MAIN);
    lv_obj_set_style_text_align(_minute_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_minute_selector->get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_minute_selector->get(), lv_color_hex(_selector_selected_color), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(_minute_selector->get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(_minute_selector->get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(_minute_selector->get(), lv_color_hex(_label_color), LV_PART_SELECTED);
    lv_obj_set_style_text_align(_minute_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(_minute_selector->get(), 0, LV_PART_SELECTED);

    _ok_button = std::make_unique<Button>(_panel->get());
    _ok_button->align(LV_ALIGN_CENTER, 0, 175);
    _ok_button->setSize(_ok_width, _ok_height);
    _ok_button->setRadius(77);
    _ok_button->setBorderWidth(0);
    _ok_button->setShadowWidth(0);
    _ok_button->setBgColor(lv_color_hex(_ok_color));
    _ok_button->label().setText("OK");
    _ok_button->label().setTextFont(&lv_font_montserrat_28);
    _ok_button->label().setTextColor(lv_color_hex(_ok_text_color));
    _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _ok_button->onClick().connect([this]() { _is_confirmed = true; });
}

model::AlarmClock::Time24 AlarmAddView::selectedTime() const
{
    model::AlarmClock::Time24 time;
    if (_hour_selector) {
        time.hour = static_cast<uint8_t>(_hour_selector->getSelected());
    }
    if (_minute_selector) {
        time.minute = static_cast<uint8_t>(_minute_selector->getSelected());
    }
    return time;
}