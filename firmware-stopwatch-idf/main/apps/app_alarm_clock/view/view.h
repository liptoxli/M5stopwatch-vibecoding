/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/alarm_clock.h"
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <vector>

namespace view {

class DeleteAlarmDialog {
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

class AlarmListView {
public:
    explicit AlarmListView(model::AlarmClock& alarmClock);

    void init(lv_obj_t* parent);
    void update();
    bool consumeAddRequested();

private:
    struct AlarmRow {
        int alarmId = -1;
        std::unique_ptr<uitk::lvgl_cpp::Button> button;
        std::unique_ptr<uitk::lvgl_cpp::Label> timeLabel;
        std::unique_ptr<uitk::lvgl_cpp::Switch> enabledSwitch;
    };

    void rebuild();
    void createTitle(int y);
    void createAlarmRow(int y, int alarmId, const model::AlarmClock::Alarm& alarm);
    void createAddButton(int y);
    void showDeleteDialog(int alarmId);
    AlarmRow* findRow(lv_obj_t* target);
    static void handleAlarmButtonLongPressed(lv_event_t* e);

    model::AlarmClock& _alarm_clock;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _add_button;
    std::unique_ptr<DeleteAlarmDialog> _delete_dialog;
    std::vector<std::unique_ptr<AlarmRow>> _rows;
    int _pending_delete_alarm_id = -1;
    bool _add_requested          = false;
};

class AlarmAddView {
public:
    void init(lv_obj_t* parent);
    bool isConfirmed() const
    {
        return _is_confirmed;
    }
    model::AlarmClock::Time24 selectedTime() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_label;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _hour_selector;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _minute_selector;
    std::unique_ptr<uitk::lvgl_cpp::Button> _ok_button;
    bool _is_confirmed = false;
};

class AlarmTriggerView {
public:
    void init(const model::AlarmClock::Time24& time);
    bool isConfirmed() const
    {
        return _is_confirmed;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _icon;
    std::unique_ptr<uitk::lvgl_cpp::Label> _time_label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _ok_button;
    bool _is_confirmed = false;
};

}  // namespace view