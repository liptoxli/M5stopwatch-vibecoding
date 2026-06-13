/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// Refs: https://developer.android.com/design/ui/wear/guides/foundations/getting-started
#pragma once
#include <lvgl.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <string_view>
#include <cstdint>
#include <memory>
#include <array>

namespace view {

class ArcTopClock : public uitk::lvgl_cpp::Widget<lv_obj_create> {
public:
    using Widget::Widget;

    uint32_t color          = 0xFFFFFF;
    uint32_t updateInterval = 1000;
    int displayRadius       = 233;

    void init();
    void update(bool force = false);

protected:
    std::array<std::unique_ptr<uitk::lvgl_cpp::Label>, 5> labels;
    uint32_t update_time_count = 0;

    void set_clock_to(const std::string_view text);
};

}  // namespace view
