/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_badge.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/loading_page/loading_page.h>
#include <memory>

using namespace mooncake;

AppBadge::AppBadge()
{
    setAppInfo().name = "Badge";
    setAppInfo().icon = (void*)&icon_badge;
}

void AppBadge::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppBadge::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _badge_view = std::make_unique<view::BadgeView>();
    _badge_view->init(lv_screen_active());
    if (_badge_view->imageObject() != nullptr) {
        bool loaded = GetHAL().loadBadgeImage(_badge_view->imageObject());
        _badge_view->setShowEditHint(!loaded);
    }
}

void AppBadge::onRunning()
{
    input::KeyEvent event = input::KeyEvent::None;
    if (_key_manager) {
        event = _key_manager->update();
    }

    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    if (_badge_view && _badge_view->imageObject() != nullptr) {
        if (event == input::KeyEvent::GoPrevious) {
            bool loaded = GetHAL().loadPreviousBadgeImage(_badge_view->imageObject());
            _badge_view->setShowEditHint(!loaded);
        } else if (event == input::KeyEvent::GoNext) {
            bool loaded = GetHAL().loadNextBadgeImage(_badge_view->imageObject());
            _badge_view->setShowEditHint(!loaded);
        }
    }

    if (_badge_view) {
        _badge_view->update();
    }

    if (_badge_view && _badge_view->consumeEditRequested()) {
        mclog::info("start badge edit mode");
        auto loading_page = std::make_unique<view::LoadingPage>(0x2B2B2B, 0xFFFFFF);
        GetHAL().lvglUnlock();
        GetHAL().startBadgeEditModeViaAp([&](std::string_view msg) {
            LvglLockGuard lock;
            loading_page->setMessage(msg);
        });
        GetHAL().lvglLock();

        // Reload image
        if (_badge_view->imageObject() != nullptr) {
            bool loaded = GetHAL().loadBadgeImage(_badge_view->imageObject());
            _badge_view->setShowEditHint(!loaded);
        }
    }
}

void AppBadge::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _badge_view.reset();
}
