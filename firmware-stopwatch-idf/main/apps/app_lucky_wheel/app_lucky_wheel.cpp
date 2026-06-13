/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_lucky_wheel.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;

AppLuckyWheel::AppLuckyWheel()
{
    // Configure App name
    setAppInfo().name = "LuckyWheel";
    // Configure App icon
    setAppInfo().icon = (void*)&icon_lucky_wheel;
}

// Called when the App is installed
void AppLuckyWheel::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppLuckyWheel::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _selection_view = std::make_unique<view::SelectionView>();
    _selection_view->init(lv_screen_active());
}

void AppLuckyWheel::onRunning()
{
    input::KeyEvent key_event = input::KeyEvent::None;
    if (_key_manager) {
        key_event = _key_manager->update();
    }

    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_selection_view) {
        if (_selection_view->isConfirmed()) {
            int optionCount = _selection_view->confirmedOptionCount();

            LvglLockGuard lock;
            _selection_view.reset();
            _wheel_view = std::make_unique<view::WheelView>();
            _wheel_view->init(lv_screen_active(), optionCount);
        }

        return;
    }

    if (_wheel_view) {
        LvglLockGuard lock;

        if (key_event == input::KeyEvent::GoPrevious) {
            _wheel_view->startSpin(view::SpinDirection::Counterclockwise);
        } else if (key_event == input::KeyEvent::GoNext) {
            _wheel_view->startSpin(view::SpinDirection::Clockwise);
        }

        _wheel_view->update();
    }
}

void AppLuckyWheel::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _selection_view.reset();
    _wheel_view.reset();
}
