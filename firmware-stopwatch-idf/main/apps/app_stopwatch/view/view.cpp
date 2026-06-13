/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <cstdint>
#include <memory>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

static const StopwatchView::WidigetState_t _widget_state_stopped = {
    0xB3CDFF,
    0x9CF1B6,
    "LAP",
    "START",
};

static const StopwatchView::WidigetState_t _widget_state_running = {
    0xB3CDFF,
    0xFF9EAB,
    "LAP",
    "STOP",
};

static const StopwatchView::WidigetState_t _widget_state_paused = {
    0xB3CDFF,
    0x9CF1B6,
    "RESET",
    "START",
};

static const int _btn_left_x  = -72;
static const int _btn_left_y  = -143;
static const int _btn_right_x = 72;
static const int _btn_right_y = -143;

static const uint32_t _color_status_bar_clock = 0xCDEBF9;
static const uint32_t _color_elapsed_time     = 0xD8F2FF;
static const uint32_t _color_panel_info       = 0x41484B;
static const uint32_t _color_panel_laps       = 0x41484B;
static const uint32_t _color_no_lap_label     = 0x738086;
static const uint32_t _color_divider_info     = 0x58646A;

static void apply_button_style(Button* button)
{
    button->setRadius(72 / 2 - 2);
    button->setBorderWidth(0);
    button->setSize(105, 72);
    button->setAlign(LV_ALIGN_CENTER);
    button->label().setTextFont(&lv_font_maple_mono_medium_24);
}

void StopwatchView::init(lv_obj_t* parent)
{
    _stopwatch = std::make_unique<model::Stopwatch>();

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), LV_PART_MAIN);

    _arc_top_clock        = std::make_unique<ArcTopClock>(parent);
    _arc_top_clock->color = _color_status_bar_clock;
    _arc_top_clock->init();
    _arc_top_clock->align(LV_ALIGN_TOP_MID, 0, 4);

    _btn_left = std::make_unique<Button>(parent);
    _btn_left->onClick().connect([this]() { handle_btn_left_clicked(); });
    apply_button_style(_btn_left.get());

    _btn_right = std::make_unique<Button>(parent);
    _btn_right->onClick().connect([this]() { handle_btn_right_clicked(); });
    apply_button_style(_btn_right.get());

    _panel_info = std::make_unique<Container>(parent);
    _panel_info->setBgColor(lv_color_hex(_color_panel_info));
    _panel_info->align(LV_ALIGN_TOP_MID, 0, 148);
    _panel_info->setPadding(0, 0, 0, 0);
    _panel_info->setBorderWidth(0);
    _panel_info->setSize(466, 330);
    _panel_info->setRadius(60);

    _label_elapsed_time = std::make_unique<Label>(_panel_info->get());
    _label_elapsed_time->setTextFont(&lv_font_maple_mono_medium_48);
    _label_elapsed_time->setTextColor(lv_color_hex(_color_elapsed_time));
    _label_elapsed_time->align(LV_ALIGN_TOP_MID, 0, 26);

    _divider_info = std::make_unique<Container>(_panel_info->get());
    _divider_info->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _divider_info->setBgColor(lv_color_hex(_color_divider_info));
    _divider_info->align(LV_ALIGN_TOP_MID, 0, 98);
    _divider_info->setBorderWidth(0);
    _divider_info->setSize(160, 4);
    _divider_info->setRadius(6);

    _panel_laps = std::make_unique<Container>(_panel_info->get());
    _panel_laps->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel_laps->setBgColor(lv_color_hex(_color_panel_laps));
    _panel_laps->align(LV_ALIGN_TOP_MID, 0, 104);
    _panel_laps->setPadding(24, 74, 0, 0);
    _panel_laps->setBorderWidth(0);
    _panel_laps->setSize(466, 214);
    _panel_laps->setRadius(60);

    _no_lap_label = std::make_unique<Label>(_panel_laps->get());
    _no_lap_label->setTextFont(&lv_font_maple_mono_medium_28);
    _no_lap_label->setTextColor(lv_color_hex(_color_no_lap_label));
    _no_lap_label->align(LV_ALIGN_TOP_MID, 0, 42);
    _no_lap_label->setText("-.-");

    update_widget_state();

    _anim_btn_left_x.springOptions().visualDuration  = 0.3;
    _anim_btn_left_y.springOptions().visualDuration  = 0.3;
    _anim_btn_right_x.springOptions().visualDuration = 0.3;
    _anim_btn_right_y.springOptions().visualDuration = 0.3;
    _anim_btn_left_x.springOptions().bounce          = 0.5;
    _anim_btn_left_y.springOptions().bounce          = 0.5;
    _anim_btn_right_x.springOptions().bounce         = 0.5;
    _anim_btn_right_y.springOptions().bounce         = 0.5;

    _anim_btn_left_x.teleport(_btn_left_x);
    _anim_btn_left_y.teleport(_btn_left_y);
    _anim_btn_right_x.teleport(_btn_right_x);
    _anim_btn_right_y.teleport(_btn_right_y);
}

void StopwatchView::update(bool updateButtonStates)
{
    _arc_top_clock->update();

    if (_stopwatch->getState() == model::Stopwatch::State_t::Running) {
        _label_elapsed_time->setText(_stopwatch->getElapsedtimeString());
    }

    if (updateButtonStates) {
        GetHAL().updateButtonStates();
    }

    if (GetHAL().btnA.wasPressed()) {
        handle_btn_left_clicked();

        _anim_btn_left_x = _btn_left_x + 8;
        _anim_btn_left_y = _btn_left_y + 8;
    } else if (GetHAL().btnA.wasReleased()) {
        _anim_btn_left_x = _btn_left_x;
        _anim_btn_left_y = _btn_left_y;
    }

    if (GetHAL().btnB.wasPressed()) {
        handle_btn_right_clicked();

        _anim_btn_right_x = _btn_right_x - 8;
        _anim_btn_right_y = _btn_right_y + 8;
    } else if (GetHAL().btnB.wasReleased()) {
        _anim_btn_right_x = _btn_right_x;
        _anim_btn_right_y = _btn_right_y;
    }

    if (!_anim_btn_left_x.done() || !_anim_btn_left_y.done()) {
        _btn_left->setPos(_anim_btn_left_x, _anim_btn_left_y);
    }
    if (!_anim_btn_right_x.done() || !_anim_btn_right_y.done()) {
        _btn_right->setPos(_anim_btn_right_x, _anim_btn_right_y);
    }
}

void StopwatchView::handle_btn_left_clicked()
{
    if (_stopwatch->getState() == model::Stopwatch::State_t::Stopped) {
        // pass
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Running) {
        _stopwatch->lap();
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Paused) {
        _stopwatch->reset();
    }
    update_widget_state();
}

void StopwatchView::handle_btn_right_clicked()
{
    if (_stopwatch->getState() == model::Stopwatch::State_t::Stopped) {
        _stopwatch->start();
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Running) {
        _stopwatch->pause();
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Paused) {
        _stopwatch->start();
    }
    update_widget_state();
}

void StopwatchView::update_widget_state()
{
    _label_elapsed_time->setText(_stopwatch->getElapsedtimeString());

    if (_stopwatch->getState() == model::Stopwatch::State_t::Stopped) {
        apply_widget_state(_widget_state_stopped);
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Running) {
        apply_widget_state(_widget_state_running);
    } else if (_stopwatch->getState() == model::Stopwatch::State_t::Paused) {
        apply_widget_state(_widget_state_paused);
    }

    if (_stopwatch->getLaps().size() == 0) {
        _no_lap_label->setOpa(255);
    } else {
        _no_lap_label->setOpa(0);
    }

    auto& laps      = _stopwatch->getLaps();
    size_t old_size = _lap_labels.size();
    size_t new_size = laps.size();

    if (new_size != old_size) {
        if (new_size == 0) {
            _lap_labels.clear();
            return;
        }

        for (size_t i = old_size; i < new_size; ++i) {
            LapLabel_t lap_label;

            lap_label.title = std::make_unique<Label>(_panel_laps->get());
            lap_label.title->setTextFont(&lv_font_maple_mono_medium_28);
            lap_label.title->setTextColor(lv_color_hex(_color_elapsed_time));
            lap_label.title->setText(fmt::format("LAP {}", i + 1));

            lap_label.time = std::make_unique<Label>(_panel_laps->get());
            lap_label.time->setTextFont(&lv_font_maple_mono_medium_28);
            lap_label.time->setTextColor(lv_color_hex(_color_elapsed_time));
            lap_label.time->setText(_stopwatch->elapsedtimeToString(laps[i]));

            _lap_labels.push_back(std::move(lap_label));
        }
    }

    for (size_t i = 0; i < _lap_labels.size(); ++i) {
        int y_offset = (new_size - 1 - i) * 48;
        _lap_labels[i].title->align(LV_ALIGN_TOP_LEFT, 64, y_offset);
        _lap_labels[i].time->align(LV_ALIGN_TOP_RIGHT, -64, y_offset);
    }
}

void StopwatchView::apply_widget_state(const WidigetState_t& state)
{
    _btn_left->setBgColor(lv_color_hex(state.btnLeftColor));
    _btn_right->setBgColor(lv_color_hex(state.btnRightColor));
    _btn_left->label().setText(state.btnLeftText.c_str());
    _btn_right->label().setText(state.btnRightText.c_str());
    _btn_left->label().setTextColor(lv_color_hex(color::blend_in_difference(state.btnLeftColor, 0x858585).toHex()));
    _btn_right->label().setTextColor(lv_color_hex(color::blend_in_difference(state.btnRightColor, 0x858585).toHex()));
}
