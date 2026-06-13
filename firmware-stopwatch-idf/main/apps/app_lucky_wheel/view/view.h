/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <memory>
#include <vector>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

enum class SpinDirection {
    Counterclockwise,
    Clockwise,
    Random,
};

class SelectionView {
public:
    void init(lv_obj_t* parent);
    int selectedOptionCount() const;
    bool isConfirmed() const
    {
        return _is_confirmed;
    }
    int confirmedOptionCount() const
    {
        return _confirmed_option_count;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_label;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _selector;
    std::unique_ptr<uitk::lvgl_cpp::Button> _ok_button;
    bool _is_confirmed          = false;
    int _confirmed_option_count = 1;
};

class WheelView {
public:
    void init(lv_obj_t* parent, int optionCount);
    void update();
    bool startSpin(SpinDirection direction);
    bool isSpinning() const
    {
        return _is_spinning;
    }
    int optionCount() const
    {
        return _option_count;
    }

private:
    void applyPointerRotation(float degrees);

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _wheel_obj;
    std::unique_ptr<uitk::lvgl_cpp::Image> _pointer_image;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> _sector_labels;
    int _option_count           = 1;
    bool _is_spinning           = false;
    float _pointer_rotation_deg = 0.0f;
    float _spin_start_deg       = 0.0f;
    float _spin_target_deg      = 0.0f;
    uint32_t _spin_start_ms     = 0;
    uint32_t _spin_duration_ms  = 0;
};

}  // namespace view