/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <vector>

namespace view {

class EditBadgeDialog {
public:
    void init(lv_obj_t* parent);
    bool isConfirmed() const
    {
        return _is_confirmed;
    }
    bool isCancelled() const
    {
        return _is_cancelled;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _confirm_button;
    std::unique_ptr<uitk::lvgl_cpp::Button> _cancel_button;
    bool _is_confirmed = false;
    bool _is_cancelled = false;
};

class BadgeView {
public:
    void init(lv_obj_t* parent = lv_screen_active());
    void setShowEditHint(bool show);
    void update();
    bool consumeEditRequested();
    lv_obj_t* imageObject() const
    {
        return _image ? _image->get() : nullptr;
    }

private:
    void showEditDialog();
    static void handleBadgeLongPressed(lv_event_t* e);

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _image;
    std::unique_ptr<uitk::lvgl_cpp::Label> _edit_hint_label;
    std::unique_ptr<EditBadgeDialog> _edit_dialog;
    bool _edit_requested = false;
};

}  // namespace view
