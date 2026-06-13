/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <smooth_lvgl.hpp>
#include <assets/assets.h>
#include <fmt/chrono.h>
#include <hal/hal.h>
#include <memory>
#include <vector>
#include <lvgl.h>
#include <src/draw/lv_image_dsc.h>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

/**
 * @brief
 *
 */
class StatuBarGesture {
public:
    std::function<void(void)> onPullDown;
    std::function<void(void)> onSwipeUp;

    StatuBarGesture() : _is_tracking(false), _last_state(LV_INDEV_STATE_REL)
    {
    }

    void init()
    {
        _is_tracking    = false;
        _last_state     = LV_INDEV_STATE_REL;
        _screen_height  = 466;
        _top_threshold  = 20;  // 距离顶部 20 像素内触发
        _panel_threshold = 90;  // 电池框展开区域内触发上滑隐藏
        _swipe_min_dist = 50;  // 向下滑动至少 50 像素才触发
        _swipe_up_min_dist = 28;
    }

    void update()
    {
        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (!indev) {
            return;
        }

        lv_indev_state_t state = lv_indev_get_state(indev);
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);

        // 1. 按下瞬间 (Transition: Released -> Pressed)
        if (state == LV_INDEV_STATE_PR && _last_state == LV_INDEV_STATE_REL) {
            // 只有在按下那一刻就在顶部，才标记为追踪开始
            if (curr_point.y <= _panel_threshold && curr_point.y >= 0) {
                _start_point = curr_point;
                _is_tracking = true;
            } else {
                _is_tracking = false;  // 按下位置不对，此次滑动全程忽略
            }
        }
        // 2. 抬起瞬间 (Transition: Pressed -> Released)
        else if (state == LV_INDEV_STATE_REL && _last_state == LV_INDEV_STATE_PR) {
            if (_is_tracking) {
                int delta_y = curr_point.y - _start_point.y;  // 向下滑为正
                int delta_x = abs(curr_point.x - _start_point.x);

                // 判断标准：向下位移足够，且角度偏垂直
                if (_start_point.y <= _top_threshold && delta_y > _swipe_min_dist && delta_y > delta_x) {
                    if (onPullDown) {
                        onPullDown();
                    }
                } else if (_start_point.y <= _panel_threshold && -delta_y > _swipe_up_min_dist && -delta_y > delta_x) {
                    if (onSwipeUp) {
                        onSwipeUp();
                    }
                }
                _is_tracking = false;  // 重置追踪状态
            }
        }

        _last_state = state;  // 更新状态机
    }

private:
    bool _is_tracking;
    lv_indev_state_t _last_state;  // 记录上一帧的状态
    lv_point_t _start_point;
    int _screen_height;
    int _top_threshold;
    int _panel_threshold;
    int _swipe_min_dist;
    int _swipe_up_min_dist;
};

namespace status_bar_view {

class Widget {
public:
    virtual ~Widget()     = default;
    virtual void update() = 0;
};

class BatteryIcon {
public:
    BatteryIcon(lv_obj_t* parent, uint32_t colorSecondary, uint32_t colorPrimary)
    {
        _color_primary = colorPrimary;
        _color_secondary = colorSecondary;

        _bat_top = std::make_unique<uitk::lvgl_cpp::Container>(parent);
        _bat_top->setBgColor(lv_color_hex(colorSecondary));
        _bat_top->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _bat_top->setBorderWidth(0);
        _bat_top->setSize(4, 4);
        _bat_top->setRadius(4);
        _bat_top->setOutlineWidth(1);
        _bat_top->setOutlineColor(lv_color_hex(colorPrimary));

        _bar = std::make_unique<uitk::lvgl_cpp::Bar>(parent);
        _bar->setSize(30, 12);
        _bar->setRadius(4);
        _bar->setRadius(0, LV_PART_INDICATOR);
        _bar->setBgColor(lv_color_hex(colorSecondary));
        _bar->setBgColor(lv_color_hex(colorPrimary), LV_PART_INDICATOR);
        _bar->setBgOpa(LV_OPA_COVER);
        _bar->setRange(0, 100);
        _bar->setValue(0);
        _bar->setPadding(1, 1, 1, 1);
        _bar->setOutlineWidth(1);
        _bar->setOutlineColor(lv_color_hex(colorPrimary));
        _bar->setRadius(3, LV_PART_INDICATOR);

        _lightning_icon = std::make_unique<uitk::lvgl_cpp::Image>(parent);
        _lightning_icon->setSrc(&icon_bat_lightning);
        _lightning_icon->setImageRecolor(lv_color_hex(colorPrimary));
        _lightning_icon->setImageRecolorOpa(LV_OPA_COVER);
        _lightning_icon->setHidden(true);
    }

    void align(lv_align_t align, int32_t x_ofs, int32_t y_ofs)
    {
        _bar->align(align, x_ofs, y_ofs);
        lv_obj_align_to(_bat_top->get(), _bar->get(), LV_ALIGN_CENTER, 15, 0);
        lv_obj_align_to(_lightning_icon->get(), _bar->get(), LV_ALIGN_CENTER, 0, 0);
    }

    void setLevel(uint8_t level)
    {
        _bar->setValue(level);
    }

    void setCharging(bool charging)
    {
        if (charging) {
            _bar->setBgColor(lv_color_hex(0x19C25F), LV_PART_INDICATOR);
            _lightning_icon->setHidden(false);
        } else {
            _bar->setBgColor(lv_color_hex(_color_primary), LV_PART_INDICATOR);
            _lightning_icon->setHidden(true);
        }
    }

    void setLowPower(bool lowPower)
    {
        const uint32_t primary = lowPower ? 0xFF3B30 : _color_primary;
        _bat_top->setBgColor(lv_color_hex(_color_secondary));
        _bat_top->setOutlineColor(lv_color_hex(primary));
        _bar->setBgColor(lv_color_hex(_color_secondary));
        _bar->setOutlineColor(lv_color_hex(primary));
        _bar->setBgColor(lv_color_hex(primary), LV_PART_INDICATOR);
        _lightning_icon->setImageRecolor(lv_color_hex(primary));
    }

private:
    std::unique_ptr<Bar> _bar;
    std::unique_ptr<Container> _bat_top;
    std::unique_ptr<Image> _lightning_icon;

    uint32_t _color_primary = 0;
    uint32_t _color_secondary = 0;
};

class Battery : public Widget {
public:
    Battery(lv_obj_t* parent, uint32_t colorSecondary, uint32_t colorPrimary)
    {
        _color_primary = colorPrimary;
        _label_level = std::make_unique<Label>(parent);
        _label_level->setText("");
        _label_level->setTextColor(lv_color_hex(colorPrimary));
        _label_level->setTextFont(&lv_font_montserrat_16);
        _label_level->align(LV_ALIGN_RIGHT_MID, -78, 8);

        _battery_icon = std::make_unique<BatteryIcon>(parent, colorSecondary, colorPrimary);
        _battery_icon->align(LV_ALIGN_RIGHT_MID, -43, 8);

        update();
    }

    void update() override
    {
        auto level = GetHAL().getBatteryLevel();
        const bool lowPower = level < 20;
        _label_level->setText(fmt::format("{}%", level));
        _label_level->setTextColor(lv_color_hex(lowPower ? 0xFF3B30 : _color_primary));
        _battery_icon->setLevel(level);
        _battery_icon->setLowPower(lowPower);
        _battery_icon->setCharging(GetHAL().isBatteryCharging());
    }

private:
    std::unique_ptr<Label> _label_level;
    std::unique_ptr<BatteryIcon> _battery_icon;
    uint32_t _color_primary = 0;
};

class StatusBarView {
public:
    StatusBarView(lv_obj_t* parent, uint32_t colorSecondary, uint32_t colorPrimary)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(colorSecondary));
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->align(LV_ALIGN_TOP_MID, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(158, 65);
        _panel->setRadius(27);
        _panel->setPadding(0, 0, 0, 0);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_CLICKABLE);
        _panel->addFlag(LV_OBJ_FLAG_FLOATING);

        _widgets.push_back(std::make_unique<Battery>(_panel->get(), colorSecondary, colorPrimary));

        _panel->setPos(0, _pos_y_hide);
        _panel->setHidden(true);
        _is_hidden                                 = true;
        _pos_y_anim.springOptions().bounce         = 0.1;
        _pos_y_anim.springOptions().visualDuration = 0.3;
        // _pos_y_anim.easingOptions().duration = 0.3;
        _pos_y_anim.teleport(_pos_y_hide);
    }

    void update()
    {
        _pos_y_anim.update();
        if (!_pos_y_anim.done()) {
            _panel->setPos(0, _pos_y_anim.directValue());
        } else {
            if (_hide_panel_flag) {
                _hide_panel_flag = false;
                _panel->setHidden(true);
            }
        }

        if (GetHAL().millis() - _last_update_tick > 1000) {
            _last_update_tick = GetHAL().millis();
            for (auto& widget : _widgets) {
                widget->update();
            }
        }
    }

    void show()
    {
        _panel->moveForeground();
        _panel->setHidden(false);
        _pos_y_anim = _pos_y_show;
        _is_hidden  = false;
    }

    void hide()
    {
        _pos_y_anim      = _pos_y_hide;
        _is_hidden       = true;
        _hide_panel_flag = true;
    }

    bool isHidden() const
    {
        return _is_hidden;
    }

private:
    const int _pos_y_show = -17;
    const int _pos_y_hide = -85;

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Widget>> _widgets;
    AnimateValue _pos_y_anim;
    bool _is_hidden            = false;
    bool _hide_panel_flag      = false;
    uint32_t _last_update_tick = 0;
};

}  // namespace status_bar_view

/**
 * @brief
 *
 */
class StatusBar {
public:
    void init(lv_obj_t* parent, uint32_t colorSecondary, uint32_t colorPrimary, bool silent)
    {
        _status_bar_gesture             = std::make_unique<StatuBarGesture>();
        _status_bar_gesture->onPullDown = [&]() { handle_show(6000, false); };
        _status_bar_gesture->onSwipeUp  = [&]() { hide(true); };
        _status_bar_gesture->init();

        _status_bar_view = std::make_unique<status_bar_view::StatusBarView>(parent, colorSecondary, colorPrimary);
        _is_first_show   = !silent;

        if (!silent) {
            _status_bar_view->show();
            _status_bar_show_tick = GetHAL().millis();
        }
    }

    void update()
    {
        _status_bar_gesture->update();
        _status_bar_view->update();
        update_visibility();
    }

    void show(uint32_t duration_ms = 5000, bool persistent = false)
    {
        handle_show(duration_ms, persistent);
    }

    bool isHidden() const
    {
        return _status_bar_view == nullptr ? true : _status_bar_view->isHidden();
    }

private:
    std::unique_ptr<StatuBarGesture> _status_bar_gesture;
    std::unique_ptr<status_bar_view::StatusBarView> _status_bar_view;
    uint32_t _status_bar_show_tick = 0;
    uint32_t _status_bar_show_duration_ms = 5000;
    bool _is_first_show            = true;
    bool _persistent_show          = false;
    bool _persistent_manual_hidden = false;

    void hide(bool manual = false)
    {
        if (_status_bar_view) {
            _persistent_show = false;
            if (manual) {
                _persistent_manual_hidden = true;
            }
            _status_bar_view->hide();
        }
    }

    void handle_show(uint32_t duration_ms, bool persistent)
    {
        if (persistent && _persistent_manual_hidden) {
            return;
        }
        if (!persistent) {
            _persistent_manual_hidden = false;
        }
        _status_bar_view->show();
        _status_bar_show_tick        = GetHAL().millis();
        _status_bar_show_duration_ms = duration_ms;
        _persistent_show             = persistent;
    }

    void update_visibility()
    {
        if (!_status_bar_view->isHidden()) {
            if (_persistent_show) {
                return;
            }
            if (GetHAL().millis() - _status_bar_show_tick > (_is_first_show ? 1800 : _status_bar_show_duration_ms)) {
                _is_first_show = false;
                _status_bar_view->hide();
            }
        }
    }
};

/**
 * @brief
 *
 */
namespace view {

static std::unique_ptr<StatusBar> _status_bar;

void create_status_bar(uint32_t colorSecondary, uint32_t colorPrimary, bool silent, lv_obj_t* parent)
{
    _status_bar = std::make_unique<StatusBar>();
    _status_bar->init(parent, colorSecondary, colorPrimary, silent);
}

void update_status_bar()
{
    if (_status_bar) {
        _status_bar->update();
    }
}

void show_status_bar(uint32_t duration_ms, bool persistent)
{
    if (_status_bar) {
        _status_bar->show(duration_ms, persistent);
    }
}

bool is_status_bar_hidden()
{
    return _status_bar == nullptr ? true : _status_bar->isHidden();
}

bool is_status_bar_created()
{
    return _status_bar != nullptr;
}

void destroy_status_bar()
{
    _status_bar.reset();
}

}  // namespace view
