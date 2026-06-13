/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <apps/common/status_bar/status_bar.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdint>

using namespace mooncake;

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    show_guide_page();

    {
        LvglLockGuard lock;
        create_launcher_view();
        if (_is_first_open) {
            _pending_status_bar_create = true;
            _status_bar_create_tick    = GetHAL().millis() + 800;
        } else {
            view::create_status_bar(0xEDF4FF, 0x385179, true);
        }
    }

    if (_should_play_boot_sfx) {
        _should_play_boot_sfx = false;
        GetHAL().playBootSfx();
    }

    _last_battery_check_tick  = GetHAL().millis();

    _is_first_open = false;
}

void AppLauncher::onLauncherRunning()
{
    LvglLockGuard lock;

    uint32_t now = GetHAL().millis();

    // Check pending status bar creation
    if (_pending_status_bar_create && !view::is_status_bar_created() && now >= _status_bar_create_tick) {
        view::create_status_bar(0xEDF4FF, 0x385179, false);
        _pending_status_bar_create = false;
    }

    update_battery_status_bar(now);

    _view->update();
    view::update_status_bar();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _pending_status_bar_create = false;
    _status_bar_create_tick    = 0;
    _last_battery_check_tick   = 0;
    _view.reset();
    view::destroy_status_bar();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    _view->onAppClicked = [&](int appID) {
        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);
        openApp(appID);
    };
}

void AppLauncher::show_guide_page()
{
    if (!_is_first_open) {
        return;
    }

    if (!GetHAL().shouldShowGuide()) {
        return;
    }

    GetHAL().lvglLock();
    auto guide_page = std::make_unique<view::GuidePage>();
    GetHAL().lvglUnlock();

    _should_play_boot_sfx = false;
    GetHAL().playBootSfx();

    input::KeyManager key_manager;

    while (1) {
        GetHAL().feedTheDog();
        GetHAL().delay(50);

        key_manager.update();
        if (key_manager.getEvent() == input::KeyEvent::GoHome) {
            break;
        }
    }

    LvglLockGuard lock;
    guide_page.reset();
}

void AppLauncher::update_battery_status_bar(uint32_t now)
{
    if (!view::is_status_bar_created()) {
        return;
    }

    if (now - _last_battery_check_tick < 1000) {
        return;
    }
    _last_battery_check_tick = now;

    const auto level = GetHAL().getBatteryLevel();
    if (level < 20) {
        view::show_status_bar(5000, true);
    }
}
