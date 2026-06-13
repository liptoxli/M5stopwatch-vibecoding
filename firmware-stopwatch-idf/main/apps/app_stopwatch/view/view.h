/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/stopwatch.h"
#include <apps/common/arc_top_clock/arc_top_clock.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <memory>
#include <vector>

namespace view {

class StopwatchView {
public:
    struct WidigetState_t {
        uint32_t btnLeftColor;
        uint32_t btnRightColor;
        std::string btnLeftText;
        std::string btnRightText;
    };

    void init(lv_obj_t* parent);
    void update(bool updateButtonStates = true);

private:
    std::unique_ptr<model::Stopwatch> _stopwatch;

    std::unique_ptr<view::ArcTopClock> _arc_top_clock;

    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_left;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_right;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_elapsed_time;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_info;
    std::unique_ptr<uitk::lvgl_cpp::Container> _divider_info;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_laps;

    struct LapLabel_t {
        std::unique_ptr<uitk::lvgl_cpp::Label> title;
        std::unique_ptr<uitk::lvgl_cpp::Label> time;
    };
    std::vector<LapLabel_t> _lap_labels;
    std::unique_ptr<uitk::lvgl_cpp::Label> _no_lap_label;

    uitk::AnimateValue _anim_btn_left_x;
    uitk::AnimateValue _anim_btn_left_y;
    uitk::AnimateValue _anim_btn_right_x;
    uitk::AnimateValue _anim_btn_right_y;

    void handle_btn_left_clicked();
    void handle_btn_right_clicked();
    void update_widget_state();
    void apply_widget_state(const WidigetState_t& state);
};

}  // namespace view
