/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "arc_top_clock.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <string>
#include <ctime>
#include <cmath>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace view;

static const std::vector<int> _label_rotation_map = {-100, -46, 0, 32, 84};
static const std::vector<int> _label_x_map        = {3, 3, 1, 4, 4};
static const std::vector<int> _label_y_map        = {0, 0, -2, -1, -1};

void ArcTopClock::init()
{
    setSize(114, 36);
    setBorderWidth(0);
    setOutlineWidth(0);
    setPaddingAll(0);
    setBgOpa(LV_OPA_TRANSP);
    removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < labels.size(); i++) {
        labels[i] = std::make_unique<Label>(get());
        labels[i]->setText(".");
        labels[i]->setTextFont(&lv_font_maple_mono_medium_28);
        labels[i]->setTextColor(lv_color_hex(color));
        labels[i]->setTransformPivot(0, displayRadius);
        labels[i]->align(LV_ALIGN_CENTER, _label_x_map[i], _label_y_map[i]);
        labels[i]->setRotation(_label_rotation_map[i]);
    }

    update(true);
}

void ArcTopClock::update(bool force)
{
    if (force || GetHAL().millis() - update_time_count > updateInterval) {
        std::time_t now    = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        set_clock_to(fmt::format("{:02d}:{:02d}", localTime->tm_hour, localTime->tm_min));
        // set_clock_to("00:00");
        update_time_count = GetHAL().millis();
    }
}

void ArcTopClock::set_clock_to(const std::string_view text)
{
    const int count = std::min((int)text.size(), (int)labels.size());
    for (int i = 0; i < count; i++) {
        if (text[i] == '0') {
            labels[i]->setText("O");
        } else {
            char buf[2] = {text[i], '\0'};
            labels[i]->setText(buf);
        }
    }
}
