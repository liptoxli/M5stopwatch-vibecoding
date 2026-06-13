/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <functional>

namespace view {

void create_status_bar(uint32_t colorSecondary = 0xEDF4FF, uint32_t colorPrimary = 0x385179, bool silent = false,
                       lv_obj_t* parent = lv_screen_active());
void update_status_bar();
void show_status_bar(uint32_t duration_ms = 5000, bool persistent = false);
bool is_status_bar_hidden();
bool is_status_bar_created();
void destroy_status_bar();

}  // namespace view
