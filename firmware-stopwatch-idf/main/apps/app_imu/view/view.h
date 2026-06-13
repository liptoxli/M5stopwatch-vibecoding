/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>
#include "lgfx/utility/lgfx_qrcode.h"

namespace view {

class ImuView {
public:
    void init(lv_obj_t* parent);
    void update();
    void setBallOffset(float normalizedX, float normalizedY);
    void setBallSize(float normalizedValue);
    void setOrbitBallAngle(float angle);
    void setAccelLabelValues(float accelX, float accelY, float accelZ);

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _click_mask;
    std::unique_ptr<uitk::lvgl_cpp::Container> _center_cross_horizontal;
    std::unique_ptr<uitk::lvgl_cpp::Container> _center_cross_vertical;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ball;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orbit_ball;
    std::unique_ptr<uitk::lvgl_cpp::Container> _overlay_cross_horizontal;
    std::unique_ptr<uitk::lvgl_cpp::Container> _overlay_cross_vertical;
    std::unique_ptr<uitk::lvgl_cpp::Label> _accel_x_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _accel_y_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _accel_z_label;

    uitk::AnimateValue _anim_x;
    uitk::AnimateValue _anim_y;
    uitk::AnimateValue _anim_size;
    uitk::AnimateValue _anim_orbit_angle;
    bool _show_accel_labels       = true;
    bool _orbit_angle_initialized = false;
    float _orbit_angle_unwrapped  = 0.0f;

    void applyBallState();
    void applyOrbitBallState();
    void cycleAccelLabelVisibility();
    void applyAccelLabelVisibility();
};

}  // namespace view
