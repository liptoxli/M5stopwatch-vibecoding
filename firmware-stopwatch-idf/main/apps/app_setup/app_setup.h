/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include "workers/workers.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>

/**
 * @brief Derived App
 *
 */
class AppSetup : public mooncake::AppAbility {
public:
    AppSetup();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;
    std::unique_ptr<setup_workers::WorkerBase> _worker;
    std::unique_ptr<input::KeyManager> _key_manager;

    bool _destroy_menu    = false;
    bool _need_warm_reset = false;
    int _magic_count      = 0;
};
