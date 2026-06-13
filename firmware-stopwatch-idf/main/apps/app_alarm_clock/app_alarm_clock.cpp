/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_alarm_clock.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;

AppAlarmClock::AppAlarmClock()
{
    setAppInfo().name = "AlarmClock";
    setAppInfo().icon = (void*)&icon_clock;
}

void AppAlarmClock::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    _alarm_clock = std::make_unique<model::AlarmClock>();
    if (!_alarm_clock->init()) {
        mclog::tagError(getAppInfo().name, "failed to init alarm clock model");
        return;
    }

    _alarm_clock->onTriggered().connect([this](const model::AlarmClock::AlarmTriggeredEvent& event) {
        std::unique_ptr<view::AlarmTriggerView> trigger_view;
        {
            LvglLockGuard lock;
            trigger_view = std::make_unique<view::AlarmTriggerView>();
            trigger_view->init(event.time);
        }

        GetHAL().startAlarm();

        // Block until confirmed
        while (1) {
            GetHAL().feedTheDog();
            GetHAL().delay(100);

            LvglLockGuard lock;
            if (trigger_view->isConfirmed()) {
                trigger_view.reset();
                break;
            }
        }

        GetHAL().stopAlarm();
    });
}

void AppAlarmClock::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    if (_alarm_clock) {
        _list_view = std::make_unique<view::AlarmListView>(*_alarm_clock);
        _list_view->init(lv_screen_active());
    }
}

void AppAlarmClock::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_alarm_clock) {
        _alarm_clock->update();
    }

    LvglLockGuard lock;

    if (_list_view) {
        _list_view->update();

        if (_list_view->consumeAddRequested()) {
            _list_view.reset();
            _add_view = std::make_unique<view::AlarmAddView>();
            _add_view->init(lv_screen_active());
        }
    }

    if (_add_view && _add_view->isConfirmed() && _alarm_clock) {
        auto time = _add_view->selectedTime();
        _alarm_clock->addAlarm(time, true);

        _add_view.reset();
        _list_view = std::make_unique<view::AlarmListView>(*_alarm_clock);
        _list_view->init(lv_screen_active());
    }
}

void AppAlarmClock::onSleeping()
{
    if (_alarm_clock) {
        _alarm_clock->update();
    }
}

void AppAlarmClock::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _list_view.reset();
    _add_view.reset();
}

void AppAlarmClock::onDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on destroy");

    _alarm_clock.reset();
}
