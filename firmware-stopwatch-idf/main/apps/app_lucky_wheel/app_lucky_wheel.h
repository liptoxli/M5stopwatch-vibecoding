/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Derived App
 *
 */
class AppLuckyWheel : public mooncake::AppAbility {
public:
    AppLuckyWheel();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::SelectionView> _selection_view;
    std::unique_ptr<view::WheelView> _wheel_view;
};