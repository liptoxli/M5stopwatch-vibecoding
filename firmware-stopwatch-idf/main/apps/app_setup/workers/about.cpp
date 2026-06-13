/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <tools/random/random.hpp>
#include <string_view>
#include <cstdio>
#include <vector>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-About";

class AboutWorker::AboutView {
public:
    AboutView()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x0078D7));
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        auto face = std::make_unique<Label>(_panel->get());
        face->setTextColor(lv_color_hex(0xFFFFFF));
        face->setTextFont(&CommissionerMedium108);
        face->align(LV_ALIGN_TOP_LEFT, 80, 80);
        face->setText(":(");
        _labels.push_back(std::move(face));

        auto msg = std::make_unique<Label>(_panel->get());
        msg->setTextColor(lv_color_hex(0xFFFFFF));
        msg->setTextFont(&lv_font_montserrat_14);
        msg->align(LV_ALIGN_TOP_LEFT, 82, 216);
        msg->setText(
            "Your StopWatch ran into a problem and\nneeds to restart. We're just collecting some\nerror info, and then "
            "we'll restart for you.");
        _labels.push_back(std::move(msg));

        auto progress = std::make_unique<Label>(_panel->get());
        progress->setTextColor(lv_color_hex(0xFFFFFF));
        progress->setTextFont(&lv_font_montserrat_14);
        progress->align(LV_ALIGN_TOP_LEFT, 82, 293);
        progress->setText("0% Complete");
        _labels.push_back(std::move(progress));

        auto tips = std::make_unique<Label>(_panel->get());
        tips->setTextColor(lv_color_hex(0xFFFFFF));
        tips->setTextFont(&lv_font_montserrat_10);
        tips->align(LV_ALIGN_TOP_LEFT, 82, 336);
        tips->setText("If you call a support person, give them this info:\nStop code: I_DONT_REALLY_KNOW");
        _labels.push_back(std::move(tips));
    }

    void setProgress(int progress)
    {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%d%% Complete", progress);
        _labels[2]->setText(buffer);
    }

private:
    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Label>> _labels;
};

AboutWorker::AboutWorker()
{
    mclog::tagInfo(_tag, "start about worker");

    _view               = std::make_unique<AboutView>();
    _next_progress_tick = GetHAL().millis();
}

void AboutWorker::update()
{
    if (consumeGlobalExitShortcut()) {
        return;
    }

    auto& random = Random::getInstance();
    uint32_t now = GetHAL().millis();

    if (now < _next_progress_tick) {
        return;
    }

    if (_pending_burst_steps <= 0) {
        _pending_burst_steps = random.getInt(1, 5);
    }

    _progress += random.getInt(1, 8);
    if (_view) {
        _view->setProgress(_progress);
    }

    --_pending_burst_steps;
    if (_pending_burst_steps > 0) {
        _next_progress_tick = now + static_cast<uint32_t>(random.getInt(60, 180));
        return;
    }

    _next_progress_tick = now + static_cast<uint32_t>(random.getInt(700, 2600));
}

AboutWorker::~AboutWorker()
{
}
