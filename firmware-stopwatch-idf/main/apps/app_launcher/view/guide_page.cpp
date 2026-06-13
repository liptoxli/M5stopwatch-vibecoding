/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <functional>
#include <cstdint>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

GuidePage::GuidePage()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_black());
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(466, 466);
    _panel->setBorderWidth(0);
    _panel->setRadius(0);

    _img = std::make_unique<Image>(_panel->get());
    _img->setSrc(&go_home_guide);
    _img->align(LV_ALIGN_CENTER, 0, 0);
}
