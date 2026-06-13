/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <core/easing/ease.hpp>
#include <core/color/color.hpp>
#include <cmath>
#include <string>
#include <tools/random/random.hpp>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

void wheel_draw_event_cb(lv_event_t* e);

constexpr int _panel_size                 = 466;
constexpr int _panel_center               = _panel_size / 2;
constexpr int _wheel_outer_radius         = 340;
constexpr int _wheel_width                = _wheel_outer_radius * 2;
constexpr int _pointer_anchor_up          = 47;
constexpr uint32_t _spin_min_duration_ms  = 2400;
constexpr uint32_t _spin_max_duration_ms  = 3600;
constexpr int _spin_min_turns             = 4;
constexpr int _spin_max_turns             = 7;
constexpr float _sector_edge_safe_ratio   = 0.22f;
constexpr float _sector_edge_safe_max_deg = 8.0f;
constexpr int _label_radius               = 200;
constexpr int _label_width                = 180;
constexpr int _label_height               = 40;
constexpr uint32_t _bg_color              = 0x202020;
constexpr uint32_t _label_fg              = 0x4C4C4C;

constexpr std::array<uint32_t, 5> _sector_colors = {
    0x4AD78C, 0x7AC4F5, 0xF4CA63, 0xD194EA, 0xFF77A0,
};

std::vector<int> build_color_order(int optionCount)
{
    std::vector<int> order;
    order.reserve(optionCount);

    std::array<int, _sector_colors.size()> usage = {};

    for (int i = 0; i < optionCount; ++i) {
        int best_index = 0;
        int best_score = -1000000;

        for (int color_index = 0; color_index < static_cast<int>(_sector_colors.size()); ++color_index) {
            if (!order.empty() && order.back() == color_index) {
                continue;
            }

            if (i == optionCount - 1 && !order.empty() && order.front() == color_index) {
                continue;
            }

            int score = -usage[color_index] * 100;

            if (i > 0) {
                int prev              = order.back();
                int circular_distance = std::abs(color_index - prev);
                circular_distance =
                    std::min(circular_distance, static_cast<int>(_sector_colors.size()) - circular_distance);
                score += circular_distance * 10;
            }

            if (score > best_score) {
                best_score = score;
                best_index = color_index;
            }
        }

        order.push_back(best_index);
        usage[best_index]++;
    }

    return order;
}

float normalize_degrees(float degrees)
{
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return normalized;
}

float compute_signed_spin_target(float current_deg, float target_deg, int full_turns, SpinDirection direction)
{
    float current_norm = normalize_degrees(current_deg);
    float target_norm  = normalize_degrees(target_deg);

    if (direction == SpinDirection::Clockwise) {
        float delta = normalize_degrees(target_norm - current_norm);
        return current_deg + static_cast<float>(full_turns * 360) + delta;
    }

    float delta = normalize_degrees(current_norm - target_norm);
    return current_deg - static_cast<float>(full_turns * 360) - delta;
}

}  // namespace

void WheelView::init(lv_obj_t* parent, int optionCount)
{
    _option_count = std::max(2, optionCount);

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _wheel_obj = std::make_unique<Container>(_panel->get());
    _wheel_obj->align(LV_ALIGN_CENTER, 0, 0);
    _wheel_obj->setSize(_panel_size, _panel_size);
    _wheel_obj->setRadius(0);
    _wheel_obj->setBorderWidth(0);
    _wheel_obj->setPaddingAll(0);
    _wheel_obj->setBgOpa(LV_OPA_TRANSP);
    _wheel_obj->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _wheel_obj->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_wheel_obj->get(), wheel_draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_wheel_obj->get());

    _sector_labels.clear();
    _sector_labels.reserve(_option_count);
    const auto color_order = build_color_order(_option_count);

    for (int i = 0; i < _option_count; ++i) {
        auto label = std::make_unique<Label>(_panel->get());
        label->setSize(_label_width, _label_height);
        label->align(LV_ALIGN_CENTER, 0, _label_radius);
        label->setText(std::to_string(i + 1));
        label->setTextFont(&MontserratSemiBold26);
        label->setTextAlign(LV_TEXT_ALIGN_CENTER);

        uint32_t sector_color = _sector_colors[color_order[i]];
        label->setTextColor(lv_color_hex(uitk::color::blend_in_difference(sector_color, _label_fg).toHex()));
        label->setTransformPivot(_label_width / 2, -_label_radius + _label_height / 2);

        int center_deg = 270 + (360 * (2 * i + 1)) / (2 * _option_count);
        label->setRotation((center_deg - 90) * 10);
        label->moveForeground();
        _sector_labels.push_back(std::move(label));
    }

    _pointer_image = std::make_unique<Image>(_panel->get());
    _pointer_image->setSrc(&lucky_wheel_pointer);
    const int pointer_pivot_x = lucky_wheel_pointer.header.w / 2;
    const int pointer_pivot_y = lucky_wheel_pointer.header.h - _pointer_anchor_up;
    _pointer_image->setPos(_panel_center - pointer_pivot_x, _panel_center - pointer_pivot_y);
    _pointer_image->setPivot(pointer_pivot_x, pointer_pivot_y);
    _pointer_image->setRotation(0);
    _pointer_image->moveForeground();

    _panel->onClick().connect([this]() { startSpin(SpinDirection::Random); });
}

void WheelView::update()
{
    if (!_is_spinning) {
        return;
    }

    uint32_t now     = GetHAL().millis();
    uint32_t elapsed = now - _spin_start_ms;
    float t = _spin_duration_ms == 0 ? 1.0f : static_cast<float>(elapsed) / static_cast<float>(_spin_duration_ms);
    if (t >= 1.0f) {
        applyPointerRotation(_spin_target_deg);
        _is_spinning = false;
        return;
    }

    float eased   = ease::ease_out_cubic(std::clamp(t, 0.0f, 1.0f));
    float degrees = _spin_start_deg + (_spin_target_deg - _spin_start_deg) * eased;
    applyPointerRotation(degrees);
}

bool WheelView::startSpin(SpinDirection direction)
{
    if (_is_spinning || _pointer_image == nullptr || _option_count < 2) {
        return false;
    }

    auto& random = Random::getInstance();
    if (direction == SpinDirection::Random) {
        direction = random.getInt(0, 1) == 0 ? SpinDirection::Counterclockwise : SpinDirection::Clockwise;
    }

    int target_sector      = random.getInt(0, _option_count - 1);
    int full_turns         = random.getInt(_spin_min_turns, _spin_max_turns);
    float sector_width_deg = 360.0f / static_cast<float>(_option_count);
    float sector_center_deg =
        360.0f * static_cast<float>(2 * target_sector + 1) / static_cast<float>(2 * _option_count);
    float edge_safe_deg         = std::min(_sector_edge_safe_max_deg, sector_width_deg * _sector_edge_safe_ratio);
    float half_random_range_deg = std::max(0.0f, sector_width_deg * 0.5f - edge_safe_deg);
    float sector_offset_deg     = random.getFloat(-half_random_range_deg, half_random_range_deg);

    _spin_start_deg = _pointer_rotation_deg;
    _spin_target_deg =
        compute_signed_spin_target(_spin_start_deg, sector_center_deg + sector_offset_deg, full_turns, direction);
    _spin_start_ms    = GetHAL().millis();
    _spin_duration_ms = static_cast<uint32_t>(
        random.getInt(static_cast<int>(_spin_min_duration_ms), static_cast<int>(_spin_max_duration_ms)));
    _is_spinning = true;
    return true;
}

void WheelView::applyPointerRotation(float degrees)
{
    _pointer_rotation_deg = degrees;
    if (_pointer_image == nullptr) {
        return;
    }

    _pointer_image->setRotation(static_cast<int32_t>(std::lround(normalize_degrees(degrees) * 10.0f)));
}

namespace {

void wheel_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<WheelView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, _wheel_outer_radius);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_point_t center = {
        static_cast<lv_coord_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<lv_coord_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    lv_draw_arc_dsc_t draw_dsc;
    lv_draw_arc_dsc_init(&draw_dsc);
    draw_dsc.opa     = LV_OPA_COVER;
    draw_dsc.center  = center;
    draw_dsc.radius  = _wheel_outer_radius;
    draw_dsc.width   = _wheel_width;
    draw_dsc.rounded = 0;

    const int option_count = view->optionCount();
    const auto color_order = build_color_order(option_count);

    for (int i = 0; i < option_count; ++i) {
        int start_deg         = 270 + (360 * i) / option_count;
        int end_deg           = 270 + (360 * (i + 1)) / option_count;
        uint32_t sector_color = _sector_colors[color_order[i]];
        draw_dsc.color        = lv_color_hex(sector_color);
        draw_dsc.start_angle  = start_deg;
        draw_dsc.end_angle    = end_deg;
        lv_draw_arc(layer, &draw_dsc);
    }
}

}  // namespace