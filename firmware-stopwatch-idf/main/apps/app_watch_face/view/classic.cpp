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
#include <string>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _dial_center          = 233;
constexpr int _mask_size            = 466;
constexpr int _tick_gap             = 9;
constexpr int _hour_tick_length     = 28;
constexpr int _minute_tick_length   = 14;
constexpr int _tick_width           = 2;
constexpr int _dial_size            = 358;
constexpr int _time_label_y_offset  = 104;
constexpr int _week_label_y_offset  = 138;
constexpr int _date_panel_width     = 48;
constexpr int _date_panel_height    = 34;
constexpr int _date_panel_x_offset  = 125;
constexpr int _hour_hand_width      = 126;
constexpr int _hour_hand_height     = 14;
constexpr int _hour_anchor_offset   = 12;
constexpr int _minute_hand_width    = 190;
constexpr int _minute_hand_height   = 8;
constexpr int _minute_anchor_offset = 19;
constexpr int _second_hand_width    = 240;
constexpr int _second_hand_height   = 14;
constexpr int _second_anchor_offset = 29;

constexpr uint32_t _dial_color        = 0x1B1B1B;
constexpr uint32_t _date_panel_color  = 0x101010;
constexpr uint32_t _hour_tick_color   = 0x6E6E6E;
constexpr uint32_t _minute_tick_color = 0x454545;
constexpr uint32_t _text_color        = 0xFFFFFF;
constexpr int _display_mode_count     = 3;

constexpr std::array<const char*, 7> _weekday_map = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

void setup_hand(Image& hand, const lv_image_dsc_t& image, int width, int height, int anchor_offset)
{
    hand.setSrc(&image);
    hand.setPos(_dial_center - anchor_offset, _dial_center - height / 2);
    hand.setPivot(anchor_offset, height / 2);
    hand.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

void set_hand_rotation(Image& hand, double degrees)
{
    hand.setRotation(static_cast<int32_t>(std::round(degrees * 10.0)));
}

void draw_ticks(Canvas& canvas)
{
    const double pi  = 3.14159265358979323846;
    int outer_radius = _dial_center - _tick_gap;

    canvas.createBuffer(_mask_size, _mask_size, LV_COLOR_FORMAT_RGB565);
    canvas.fillBg(lv_color_black(), LV_OPA_COVER);
    canvas.startDrawing();

    for (int tick_index = 0; tick_index < 60; ++tick_index) {
        bool is_hour_tick     = tick_index % 5 == 0;
        int tick_length       = is_hour_tick ? _hour_tick_length : _minute_tick_length;
        lv_color_t tick_color = is_hour_tick ? lv_color_hex(_hour_tick_color) : lv_color_hex(_minute_tick_color);

        double angle     = (-90.0 + tick_index * 6.0) * pi / 180.0;
        int inner_radius = outer_radius - tick_length;

        int x1 = static_cast<int>(std::lround(_dial_center + inner_radius * std::cos(angle)));
        int y1 = static_cast<int>(std::lround(_dial_center + inner_radius * std::sin(angle)));
        int x2 = static_cast<int>(std::lround(_dial_center + outer_radius * std::cos(angle)));
        int y2 = static_cast<int>(std::lround(_dial_center + outer_radius * std::sin(angle)));

        canvas.drawLine(x1, y1, x2, y2, _tick_width, tick_color);
    }

    canvas.finishDrawing();
}

std::string replace_zero_with_o(std::string text)
{
    for (char& ch : text) {
        if (ch == '0') {
            ch = 'O';
        }
    }

    return text;
}

}  // namespace

void WatchFaceClassic::onCreate(lv_obj_t* parent)
{
    _display_mode = 0;

    _ticks = std::make_unique<Canvas>(parent);
    _ticks->align(LV_ALIGN_CENTER, 0, 0);
    draw_ticks(*_ticks);
    _ticks->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _sub_panel = std::make_unique<Container>(parent);
    _sub_panel->align(LV_ALIGN_CENTER, 0, 0);
    _sub_panel->setSize(_dial_size, _dial_size);
    _sub_panel->setBgColor(lv_color_hex(_dial_color));
    _sub_panel->setBgOpa(LV_OPA_COVER);
    _sub_panel->setRadius(LV_RADIUS_CIRCLE);
    _sub_panel->setBorderWidth(0);
    _sub_panel->setOutlineWidth(0);
    _sub_panel->setShadowWidth(0);
    _sub_panel->setPaddingAll(0);
    _sub_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _time_label = std::make_unique<Label>(parent);
    _time_label->align(LV_ALIGN_CENTER, 0, _time_label_y_offset);
    _time_label->setTextFont(&lv_font_maple_mono_medium_28);
    _time_label->setTextColor(lv_color_hex(_text_color));
    _time_label->setBgOpa(LV_OPA_TRANSP);
    _time_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _time_label->setTextColor(lv_color_hex(0xD9D9D9));

    _weekday_label = std::make_unique<Label>(parent);
    _weekday_label->align(LV_ALIGN_CENTER, 0, _week_label_y_offset);
    _weekday_label->setTextFont(&lv_font_maple_mono_medium_24);
    _weekday_label->setTextColor(lv_color_hex(_text_color));
    _weekday_label->setBgOpa(LV_OPA_TRANSP);
    _weekday_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _weekday_label->setTextColor(lv_color_hex(0x717171));

    _date_panel = std::make_unique<Container>(parent);
    _date_panel->align(LV_ALIGN_CENTER, _date_panel_x_offset, 0);
    _date_panel->setSize(_date_panel_width, _date_panel_height);
    _date_panel->setBgColor(lv_color_hex(_date_panel_color));
    _date_panel->setBgOpa(LV_OPA_COVER);
    _date_panel->setRadius(LV_RADIUS_CIRCLE);
    _date_panel->setBorderWidth(0);
    _date_panel->setOutlineWidth(0);
    _date_panel->setShadowWidth(0);
    _date_panel->setPaddingAll(0);
    _date_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _date_label = std::make_unique<Label>(_date_panel->get());
    _date_label->align(LV_ALIGN_CENTER, 0, 0);
    _date_label->setTextFont(&lv_font_maple_mono_medium_24);
    _date_label->setTextColor(lv_color_hex(_text_color));
    _date_label->setBgOpa(LV_OPA_TRANSP);
    _date_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _date_label->setTextColor(lv_color_hex(0x8B8B8B));

    _hour_hand = std::make_unique<Image>(parent);
    setup_hand(*_hour_hand, classic_hour_hand, _hour_hand_width, _hour_hand_height, _hour_anchor_offset);

    _minute_hand = std::make_unique<Image>(parent);
    setup_hand(*_minute_hand, classic_minute_hand, _minute_hand_width, _minute_hand_height, _minute_anchor_offset);

    // Create the second hand last so its center cap stays on top.
    _second_hand = std::make_unique<Image>(parent);
    setup_hand(*_second_hand, classic_second_hand, _second_hand_width, _second_hand_height, _second_anchor_offset);

    _click_mask = std::make_unique<Container>(parent);
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_mask_size, _mask_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycle_display_mode(); });

    _click_mask->moveForeground();

    apply_display_mode();

    _last_tick = GetHAL().millis();
    update();
}

void WatchFaceClassic::onUpdate()
{
    if (GetHAL().millis() - _last_tick > 1000) {
        _last_tick = GetHAL().millis();
        update();
    }
}

void WatchFaceClassic::onDestroy()
{
    _click_mask.reset();
    _date_label.reset();
    _date_panel.reset();
    _weekday_label.reset();
    _time_label.reset();
    _ticks.reset();
    _second_hand.reset();
    _minute_hand.reset();
    _hour_hand.reset();
    _sub_panel.reset();
}

void WatchFaceClassic::update()
{
    std::time_t now    = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    if (localTime == nullptr) {
        return;
    }

    char time_buffer[6] = {};
    std::snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d", localTime->tm_hour, localTime->tm_min);

    char date_buffer[3] = {};
    std::snprintf(date_buffer, sizeof(date_buffer), "%02d", localTime->tm_mday);

    if (_time_label) {
        _time_label->setText(replace_zero_with_o(time_buffer));
    }

    if (_weekday_label) {
        _weekday_label->setText(_weekday_map[localTime->tm_wday]);
    }

    if (_date_label) {
        _date_label->setText(replace_zero_with_o(date_buffer));
    }

    double second_angle = static_cast<double>(localTime->tm_sec) * 6.0 - 90.0;
    double minute_angle =
        (static_cast<double>(localTime->tm_min) + static_cast<double>(localTime->tm_sec) / 60.0) * 6.0 - 90.0;
    double hour_angle = ((static_cast<double>(localTime->tm_hour % 12) + static_cast<double>(localTime->tm_min) / 60.0 +
                          static_cast<double>(localTime->tm_sec) / 3600.0) *
                         30.0) -
                        90.0;

    if (_hour_hand) {
        set_hand_rotation(*_hour_hand, hour_angle);
    }

    if (_minute_hand) {
        set_hand_rotation(*_minute_hand, minute_angle);
    }

    if (_second_hand) {
        set_hand_rotation(*_second_hand, second_angle);
    }
}

void WatchFaceClassic::cycle_display_mode()
{
    _display_mode++;
    if (_display_mode >= _display_mode_count) {
        _display_mode = 0;
    }

    apply_display_mode();
}

void WatchFaceClassic::apply_display_mode()
{
    bool show_info  = _display_mode == 0;
    bool show_ticks = _display_mode != 2;

    if (_sub_panel) {
        _sub_panel->setHidden(!show_info);
    }

    if (_time_label) {
        _time_label->setHidden(!show_info);
    }

    if (_weekday_label) {
        _weekday_label->setHidden(!show_info);
    }

    if (_date_panel) {
        _date_panel->setHidden(!show_info);
    }

    if (_ticks) {
        _ticks->setHidden(!show_ticks);
    }
}
