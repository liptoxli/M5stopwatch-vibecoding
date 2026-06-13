/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size              = 466;
constexpr int _mask_size               = 466;
constexpr int _ball_default_size       = 80;
constexpr int _ball_max_size           = 120;
constexpr int _orbit_ball_size         = 18;
constexpr int _orbit_ball_radius       = 214;
constexpr int _center_cross_length     = 36;
constexpr int _center_cross_thickness  = 2;
constexpr int _overlay_cross_length    = 289;
constexpr int _overlay_cross_thickness = 2;
constexpr int _accel_x_x               = 244;
constexpr int _accel_x_y               = 94;
constexpr int _accel_y_x               = 96;
constexpr int _accel_y_y               = 241;
constexpr int _accel_z_x               = 244;
constexpr int _accel_z_y               = 352;

constexpr uint32_t _bg_color    = 0x000000;
constexpr uint32_t _ball_color  = 0xFFB333;
constexpr uint32_t _orbit_color = 0xCBFFB1;
constexpr uint32_t _cross_color = 0xFFFFFF;
constexpr uint32_t _label_color = 0xCBFFB1;

constexpr float _pi = 3.14159265358979323846f;

float clamp_normalized(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

float clamp_zero_to_one(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
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

void setup_line(Container& line, int width, int height)
{
    line.setSize(width, height);
    line.setBgColor(lv_color_hex(_cross_color));
    line.setBgOpa(LV_OPA_COVER);
    line.setBorderWidth(0);
    line.setOutlineWidth(0);
    line.setShadowWidth(0);
    line.setPaddingAll(0);
    line.setRadius(LV_RADIUS_CIRCLE);
    line.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

void setup_accel_label(Label& label, int x, int y)
{
    label.align(LV_ALIGN_TOP_LEFT, x, y);
    label.setTextFont(&lv_font_montserrat_16);
    label.setTextColor(lv_color_hex(_label_color));
    label.setBgOpa(LV_OPA_TRANSP);
    label.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

void ImuView::init(lv_obj_t* parent)
{
    _orbit_angle_initialized = false;
    _orbit_angle_unwrapped   = 0.0f;

    _anim_x.pause();
    _anim_x.easingOptions().duration       = 0.1;
    _anim_x.easingOptions().easingFunction = ease::linear;
    _anim_x.teleport(0.0f);
    _anim_x.play();

    _anim_y.pause();
    _anim_y.easingOptions().duration       = 0.1;
    _anim_y.easingOptions().easingFunction = ease::linear;
    _anim_y.teleport(0.0f);
    _anim_y.play();

    _anim_size.pause();
    _anim_size.easingOptions().duration       = 0.3;
    _anim_size.easingOptions().easingFunction = ease::linear;
    _anim_size.teleport(0.0f);
    _anim_size.play();

    _anim_orbit_angle.pause();
    _anim_orbit_angle.easingOptions().duration       = 0.2;
    _anim_orbit_angle.easingOptions().easingFunction = ease::linear;
    _anim_orbit_angle.teleport(0.0f);
    _anim_orbit_angle.play();

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setBorderWidth(0);
    _panel->setOutlineWidth(0);
    _panel->setShadowWidth(0);
    _panel->setPaddingAll(0);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _click_mask = std::make_unique<Container>(_panel->get());
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_mask_size, _mask_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycleAccelLabelVisibility(); });

    _center_cross_horizontal = std::make_unique<Container>(_panel->get());
    setup_line(*_center_cross_horizontal, _center_cross_length, _center_cross_thickness);
    _center_cross_horizontal->align(LV_ALIGN_CENTER, 0, 0);

    _center_cross_vertical = std::make_unique<Container>(_panel->get());
    setup_line(*_center_cross_vertical, _center_cross_thickness, _center_cross_length);
    _center_cross_vertical->align(LV_ALIGN_CENTER, 0, 0);

    _ball = std::make_unique<Container>(_panel->get());
    _ball->setBgColor(lv_color_hex(_ball_color));
    _ball->setBgOpa(LV_OPA_COVER);
    _ball->setBorderWidth(0);
    _ball->setOutlineWidth(0);
    _ball->setShadowWidth(0);
    _ball->setPaddingAll(0);
    _ball->setRadius(LV_RADIUS_CIRCLE);
    _ball->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _orbit_ball = std::make_unique<Container>(_panel->get());
    _orbit_ball->setSize(_orbit_ball_size, _orbit_ball_size);
    _orbit_ball->setBgColor(lv_color_hex(_orbit_color));
    _orbit_ball->setBgOpa(LV_OPA_COVER);
    _orbit_ball->setBorderWidth(0);
    _orbit_ball->setOutlineWidth(0);
    _orbit_ball->setShadowWidth(0);
    _orbit_ball->setPaddingAll(0);
    _orbit_ball->setRadius(LV_RADIUS_CIRCLE);
    _orbit_ball->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _overlay_cross_horizontal = std::make_unique<Container>(_panel->get());
    setup_line(*_overlay_cross_horizontal, _overlay_cross_length, _overlay_cross_thickness);
    _overlay_cross_horizontal->align(LV_ALIGN_CENTER, 0, 0);

    _overlay_cross_vertical = std::make_unique<Container>(_panel->get());
    setup_line(*_overlay_cross_vertical, _overlay_cross_thickness, _overlay_cross_length);
    _overlay_cross_vertical->align(LV_ALIGN_CENTER, 0, 0);

    _accel_x_label = std::make_unique<Label>(_panel->get());
    setup_accel_label(*_accel_x_label, _accel_x_x, _accel_x_y);
    _accel_x_label->setText("X: 0.0");

    _accel_y_label = std::make_unique<Label>(_panel->get());
    setup_accel_label(*_accel_y_label, _accel_y_x, _accel_y_y);
    _accel_y_label->setText("Y: 0.0");

    _accel_z_label = std::make_unique<Label>(_panel->get());
    setup_accel_label(*_accel_z_label, _accel_z_x, _accel_z_y);
    _accel_z_label->setText("Z: 0.0");

    _center_cross_horizontal->moveForeground();
    _center_cross_vertical->moveForeground();
    _overlay_cross_horizontal->moveForeground();
    _overlay_cross_vertical->moveForeground();
    _accel_x_label->moveForeground();
    _accel_y_label->moveForeground();
    _accel_z_label->moveForeground();
    _click_mask->moveForeground();

    applyAccelLabelVisibility();

    applyBallState();
    applyOrbitBallState();
}

void ImuView::update()
{
    if (!_anim_x.done() || !_anim_y.done() || !_anim_size.done()) {
        applyBallState();
    }

    if (!_anim_orbit_angle.done()) {
        applyOrbitBallState();
    }
}

void ImuView::setBallOffset(float normalizedX, float normalizedY)
{
    _anim_x = clamp_normalized(normalizedX);
    _anim_y = clamp_normalized(normalizedY);
}

void ImuView::setBallSize(float normalizedValue)
{
    _anim_size = clamp_zero_to_one(normalizedValue);
}

void ImuView::setOrbitBallAngle(float angle)
{
    if (!_orbit_angle_initialized) {
        _orbit_angle_unwrapped   = angle;
        _orbit_angle_initialized = true;
    } else {
        _orbit_angle_unwrapped = unwrap_angle(_orbit_angle_unwrapped, angle);
    }

    _anim_orbit_angle = _orbit_angle_unwrapped;
}

void ImuView::setAccelLabelValues(float accelX, float accelY, float accelZ)
{
    char buffer[24] = {};

    if (_accel_x_label) {
        std::snprintf(buffer, sizeof(buffer), "X: %.3f", accelX);
        _accel_x_label->setText(buffer);
    }

    if (_accel_y_label) {
        std::snprintf(buffer, sizeof(buffer), "Y: %.3f", accelY);
        _accel_y_label->setText(buffer);
    }

    if (_accel_z_label) {
        std::snprintf(buffer, sizeof(buffer), "Z: %.3f", accelZ);
        _accel_z_label->setText(buffer);
    }
}

void ImuView::applyBallState()
{
    if (_ball == nullptr) {
        return;
    }

    float animated_x    = _anim_x;
    float animated_y    = _anim_y;
    float animated_size = _anim_size;

    int ball_size  = _ball_default_size + static_cast<int>((_ball_max_size - _ball_default_size) * animated_size);
    int max_offset = _overlay_cross_length / 2 - ball_size / 2;
    int offset_x   = static_cast<int>(max_offset * animated_x);
    int offset_y   = static_cast<int>(max_offset * animated_y);

    _ball->setSize(ball_size, ball_size);
    _ball->align(LV_ALIGN_CENTER, offset_x, offset_y);

    if (_center_cross_horizontal) {
        _center_cross_horizontal->align(LV_ALIGN_CENTER, offset_x, offset_y);
    }

    if (_center_cross_vertical) {
        _center_cross_vertical->align(LV_ALIGN_CENTER, offset_x, offset_y);
    }
}

void ImuView::applyOrbitBallState()
{
    if (_orbit_ball == nullptr) {
        return;
    }

    if (!_show_accel_labels) {
        return;
    }

    float radians = (static_cast<float>(_anim_orbit_angle) - 90.0f) * _pi / 180.0f;
    int offset_x  = static_cast<int>(std::lround(_orbit_ball_radius * std::cos(radians)));
    int offset_y  = static_cast<int>(std::lround(_orbit_ball_radius * std::sin(radians)));

    _orbit_ball->align(LV_ALIGN_CENTER, offset_x, offset_y);
}

void ImuView::cycleAccelLabelVisibility()
{
    _show_accel_labels = !_show_accel_labels;
    applyAccelLabelVisibility();
}

void ImuView::applyAccelLabelVisibility()
{
    if (_accel_x_label) {
        _accel_x_label->setHidden(!_show_accel_labels);
    }

    if (_accel_y_label) {
        _accel_y_label->setHidden(!_show_accel_labels);
    }

    if (_accel_z_label) {
        _accel_z_label->setHidden(!_show_accel_labels);
    }

    if (_orbit_ball) {
        _orbit_ball->setHidden(!_show_accel_labels);
    }
}
