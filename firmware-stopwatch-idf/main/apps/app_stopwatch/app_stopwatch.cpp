/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_stopwatch.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>

using namespace mooncake;

AppStopWatch::AppStopWatch()
{
    setAppInfo().name = "Stopwatch";
    setAppInfo().icon = (void*)&icon_stopwatch;
}

// Called when the App is installed
void AppStopWatch::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppStopWatch::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _view = std::make_unique<view::StopwatchView>();
    _view->init(lv_screen_active());
}

void AppStopWatch::onRunning()
{
    GetHAL().updateButtonStates();
    if (_key_manager && _key_manager->update(false) == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    _view->update(false);
}

void AppStopWatch::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
}
