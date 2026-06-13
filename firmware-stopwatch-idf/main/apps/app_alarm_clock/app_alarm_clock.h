/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "model/alarm_clock.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Derived App
 *
 */
class AppAlarmClock : public mooncake::AppAbility {
public:
    AppAlarmClock();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onSleeping() override;
    void onClose() override;
    void onDestroy() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<model::AlarmClock> _alarm_clock;
    std::unique_ptr<view::AlarmListView> _list_view;
    std::unique_ptr<view::AlarmAddView> _add_view;
};