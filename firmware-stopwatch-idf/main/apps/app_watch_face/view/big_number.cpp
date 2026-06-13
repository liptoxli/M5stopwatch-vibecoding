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

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _mask_size            = 466;
constexpr int _hour_tens_x_offset   = -42;
constexpr int _hour_tens_y_offset   = -51;
constexpr int _hour_ones_x_offset   = 42;
constexpr int _hour_ones_y_offset   = -51;
constexpr int _minute_tens_x_offset = -42;
constexpr int _minute_tens_y_offset = 51;
constexpr int _minute_ones_x_offset = 42;
constexpr int _minute_ones_y_offset = 51;
constexpr int _palette_count        = 4;

using Palette = std::array<uint32_t, 4>;

constexpr std::array<Palette, _palette_count> _palettes = {
    Palette{0xCBDB8C, 0xD3EA71, 0xE9F5B8, 0x94A350},
    Palette{0xD1F3BF, 0xCDFCB9, 0xF8FEE9, 0x84D86D},
    Palette{0xDB9D7D, 0xE8D780, 0xF4EBC0, 0xF2B050},
    Palette{0xB49EDB, 0x9BC1FF, 0xC4C9FA, 0xAF94E7},
};

const lv_image_dsc_t* get_digit_image(int digit)
{
    switch (digit) {
        case 0:
            return &big_number_0;
        case 1:
            return &big_number_1;
        case 2:
            return &big_number_2;
        case 3:
            return &big_number_3;
        case 4:
            return &big_number_4;
        case 5:
            return &big_number_5;
        case 6:
            return &big_number_6;
        case 7:
            return &big_number_7;
        case 8:
            return &big_number_8;
        case 9:
        default:
            return &big_number_9;
    }
}

void setup_digit(Image& digit, int x_offset, int y_offset)
{
    digit.align(LV_ALIGN_CENTER, x_offset, y_offset);
    digit.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

void set_digit(Image* image, int digit)
{
    if (image == nullptr) {
        return;
    }

    image->setSrc(get_digit_image(digit));
}

void set_digit_recolor(Image* image, uint32_t color)
{
    if (image == nullptr) {
        return;
    }

    lv_obj_set_style_image_recolor(image->get(), lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(image->get(), LV_OPA_COVER, 0);
}

}  // namespace

void WatchFaceBigNumber::onCreate(lv_obj_t* parent)
{
    _palette_index = 0;

    _hour_tens = std::make_unique<Image>(parent);
    setup_digit(*_hour_tens, _hour_tens_x_offset, _hour_tens_y_offset);

    _hour_ones = std::make_unique<Image>(parent);
    setup_digit(*_hour_ones, _hour_ones_x_offset, _hour_ones_y_offset);

    _minute_tens = std::make_unique<Image>(parent);
    setup_digit(*_minute_tens, _minute_tens_x_offset, _minute_tens_y_offset);

    _minute_ones = std::make_unique<Image>(parent);
    setup_digit(*_minute_ones, _minute_ones_x_offset, _minute_ones_y_offset);

    _click_mask = std::make_unique<Container>(parent);
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_mask_size, _mask_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycle_palette(); });
    _click_mask->moveForeground();

    apply_palette();

    _last_tick = GetHAL().millis();
    update();
}

void WatchFaceBigNumber::onUpdate()
{
    if (GetHAL().millis() - _last_tick > 1000) {
        _last_tick = GetHAL().millis();
        update();
    }
}

void WatchFaceBigNumber::onDestroy()
{
    _click_mask.reset();
    _minute_ones.reset();
    _minute_tens.reset();
    _hour_ones.reset();
    _hour_tens.reset();
}

void WatchFaceBigNumber::update()
{
    std::time_t now    = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    if (localTime == nullptr) {
        return;
    }

    int hour   = localTime->tm_hour;
    int minute = localTime->tm_min;

    set_digit(_hour_tens.get(), hour / 10);
    set_digit(_hour_ones.get(), hour % 10);
    set_digit(_minute_tens.get(), minute / 10);
    set_digit(_minute_ones.get(), minute % 10);
}

void WatchFaceBigNumber::cycle_palette()
{
    _palette_index++;
    if (_palette_index >= _palette_count) {
        _palette_index = 0;
    }

    apply_palette();
}

void WatchFaceBigNumber::apply_palette()
{
    const Palette& palette = _palettes[_palette_index];

    set_digit_recolor(_hour_tens.get(), palette[0]);
    set_digit_recolor(_hour_ones.get(), palette[1]);
    set_digit_recolor(_minute_tens.get(), palette[2]);
    set_digit_recolor(_minute_ones.get(), palette[3]);
}
