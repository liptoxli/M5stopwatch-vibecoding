/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-DateTime";

namespace setup_workers {

namespace {

std::string build_number_options(int begin, int end, int width = 2)
{
    std::string options;
    for (int value = begin; value <= end; ++value) {
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "%0*d", width, value);
        if (!options.empty()) {
            options.push_back('\n');
        }
        options += buffer;
    }
    return options;
}

void apply_selector_style(Roller& roller)
{
    lv_obj_set_style_radius(roller.get(), 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(roller.get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(0x696969), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_SELECTED);
}

}  // namespace

class SetTimeWorker::TimeAdjustView {
public:
    explicit TimeAdjustView(const TimeHms& initialTime)
    {
        _current_time = initialTime;

        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title_label = std::make_unique<Label>(_panel->get());
        _title_label->align(LV_ALIGN_TOP_MID, 0, 60);
        _title_label->setText("Set Time");
        _title_label->setTextFont(&MontserratSemiBold26);
        _title_label->setTextColor(lv_color_hex(0xFFFFFF));

        _summary_label = std::make_unique<Label>(_panel->get());
        _summary_label->align(LV_ALIGN_TOP_MID, 0, 100);
        _summary_label->setTextFont(&lv_font_montserrat_22);
        _summary_label->setTextColor(lv_color_hex(0x9A9A9A));

        const auto hour_options = build_number_options(0, 23);
        _hour_selector          = std::make_unique<Roller>(_panel->get());
        _hour_selector->align(LV_ALIGN_CENTER, -120, -13);
        _hour_selector->setSize(110, 200);
        _hour_selector->setOptions(hour_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _hour_selector->setVisibleRowCount(5);
        _hour_selector->setSelected(_current_time.hour, LV_ANIM_OFF);
        apply_selector_style(*_hour_selector);
        _hour_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.hour = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        const auto minute_options = build_number_options(0, 59);
        _minute_selector          = std::make_unique<Roller>(_panel->get());
        _minute_selector->align(LV_ALIGN_CENTER, 0, -13);
        _minute_selector->setSize(110, 200);
        _minute_selector->setOptions(minute_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _minute_selector->setVisibleRowCount(5);
        _minute_selector->setSelected(_current_time.minute, LV_ANIM_OFF);
        apply_selector_style(*_minute_selector);
        _minute_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.minute = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        const auto second_options = build_number_options(0, 59);
        _second_selector          = std::make_unique<Roller>(_panel->get());
        _second_selector->align(LV_ALIGN_CENTER, 120, -13);
        _second_selector->setSize(110, 200);
        _second_selector->setOptions(second_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _second_selector->setVisibleRowCount(5);
        _second_selector->setSelected(_current_time.second, LV_ANIM_OFF);
        apply_selector_style(*_second_selector);
        _second_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.second = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        _ok_button = std::make_unique<Button>(_panel->get());
        _ok_button->align(LV_ALIGN_CENTER, 0, 175);
        _ok_button->setSize(374, 130);
        _ok_button->setRadius(77);
        _ok_button->setBorderWidth(0);
        _ok_button->setShadowWidth(0);
        _ok_button->setBgColor(lv_color_hex(0x4AD78C));
        _ok_button->label().setText("OK");
        _ok_button->label().setTextFont(&lv_font_montserrat_28);
        _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
        _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
        _ok_button->onClick().connect([this]() { _save_requested = true; });

        updateSummaryLabel();
    }

    TimeHms currentTime() const
    {
        return _current_time;
    }

    bool consumeSaveRequested()
    {
        bool requested  = _save_requested;
        _save_requested = false;
        return requested;
    }

private:
    void updateSummaryLabel()
    {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", _current_time.hour, _current_time.minute,
                      _current_time.second);
        _summary_label->setText(buffer);
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title_label;
    std::unique_ptr<Label> _summary_label;
    std::unique_ptr<Roller> _hour_selector;
    std::unique_ptr<Roller> _minute_selector;
    std::unique_ptr<Roller> _second_selector;
    std::unique_ptr<Button> _ok_button;
    TimeHms _current_time;
    bool _save_requested = false;
};

class SetDateWorker::DateAdjustView {
public:
    explicit DateAdjustView(const DateYmd& initialDate)
    {
        _current_date = initialDate;

        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title_label = std::make_unique<Label>(_panel->get());
        _title_label->align(LV_ALIGN_TOP_MID, 0, 60);
        _title_label->setText("Set Date");
        _title_label->setTextFont(&MontserratSemiBold26);
        _title_label->setTextColor(lv_color_hex(0xFFFFFF));

        _summary_label = std::make_unique<Label>(_panel->get());
        _summary_label->align(LV_ALIGN_TOP_MID, 0, 100);
        _summary_label->setTextFont(&lv_font_montserrat_22);
        _summary_label->setTextColor(lv_color_hex(0x9A9A9A));

        const auto year_options = build_number_options(2000, 2099, 4);
        _year_selector          = std::make_unique<Roller>(_panel->get());
        _year_selector->align(LV_ALIGN_CENTER, -75, -8);
        _year_selector->setSize(170, 200);
        _year_selector->setOptions(year_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _year_selector->setVisibleRowCount(5);
        _year_selector->setSelected(_current_date.year - 2000, LV_ANIM_OFF);
        apply_selector_style(*_year_selector);
        _year_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.year = static_cast<uint16_t>(2000 + value);
            syncDateValidity();
            updateSummaryLabel();
        });

        const auto month_options = build_number_options(1, 12);
        _month_selector          = std::make_unique<Roller>(_panel->get());
        _month_selector->align(LV_ALIGN_CENTER, 95, -8);
        _month_selector->setSize(110, 200);
        _month_selector->setOptions(month_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _month_selector->setVisibleRowCount(5);
        _month_selector->setSelected(_current_date.month - 1, LV_ANIM_OFF);
        apply_selector_style(*_month_selector);
        _month_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.month = static_cast<uint8_t>(value + 1);
            syncDateValidity();
            updateSummaryLabel();
        });

        _day_selector = std::make_unique<Roller>(_panel->get());
        _day_selector->align(LV_ALIGN_CENTER, 0, -8);
        _day_selector->setSize(140, 220);
        _day_selector->setVisibleRowCount(5);
        apply_selector_style(*_day_selector);
        _day_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.day = static_cast<uint8_t>(value + 1);
            syncDateValidity();
            updateSummaryLabel();
        });
        syncDateValidity();

        _ok_button = std::make_unique<Button>(_panel->get());
        _ok_button->align(LV_ALIGN_CENTER, 0, 175);
        _ok_button->setSize(374, 130);
        _ok_button->setRadius(77);
        _ok_button->setBorderWidth(0);
        _ok_button->setShadowWidth(0);
        _ok_button->setBgColor(lv_color_hex(0x4AD78C));
        _ok_button->label().setTextFont(&lv_font_montserrat_28);
        _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
        _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
        _ok_button->onClick().connect([this]() {
            syncDateValidity();

            if (_stage == Stage::YearMonth) {
                setStage(Stage::Day);
                return;
            }

            _save_requested = _current_date.isValid();
        });

        setStage(Stage::YearMonth);
    }

    DateYmd currentDate() const
    {
        return _current_date;
    }

    bool consumeSaveRequested()
    {
        bool requested  = _save_requested;
        _save_requested = false;
        return requested;
    }

private:
    enum class Stage {
        YearMonth,
        Day,
    };

    void syncDateValidity()
    {
        const uint8_t max_day = DateYmd::daysInMonth(_current_date.year, _current_date.month);
        if (_current_date.day > max_day) {
            _current_date.day = max_day;
        }

        if (_current_date.day < 1) {
            _current_date.day = 1;
        }

        const auto day_options = build_number_options(1, max_day);
        _day_selector->setOptions(day_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _day_selector->setSelected(_current_date.day - 1, LV_ANIM_OFF);
    }

    void updateSummaryLabel()
    {
        char buffer[16] = {};
        if (_stage == Stage::YearMonth) {
            std::snprintf(buffer, sizeof(buffer), "%04u-%02u", _current_date.year, _current_date.month);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", _current_date.year, _current_date.month,
                          _current_date.day);
        }
        _summary_label->setText(buffer);
    }

    void setStage(Stage stage)
    {
        _stage = stage;
        updateSummaryLabel();

        const bool show_year_month = _stage == Stage::YearMonth;
        _year_selector->setHidden(!show_year_month);
        _month_selector->setHidden(!show_year_month);
        _day_selector->setHidden(show_year_month);

        if (show_year_month) {
            _ok_button->label().setText("Next");
            return;
        }

        updateSummaryLabel();
        _ok_button->label().setText("OK");
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title_label;
    std::unique_ptr<Label> _summary_label;
    std::unique_ptr<Roller> _year_selector;
    std::unique_ptr<Roller> _month_selector;
    std::unique_ptr<Roller> _day_selector;
    std::unique_ptr<Button> _ok_button;
    DateYmd _current_date;
    Stage _stage         = Stage::YearMonth;
    bool _save_requested = false;
};

}  // namespace setup_workers

SetTimeWorker::SetTimeWorker()
{
    mclog::tagInfo(_tag, "start set time worker");

    _applied_time = GetHAL().getTimeHms();
    _view         = std::make_unique<TimeAdjustView>(_applied_time);
}

void SetTimeWorker::update()
{
    if (consumeGlobalExitShortcut()) {
        return;
    }

    if (_view && _view->consumeSaveRequested()) {
        GetHAL().setTimeHms(_view->currentTime());
        _is_done = true;
    }
}

SetTimeWorker::~SetTimeWorker()
{
}

SetDateWorker::SetDateWorker()
{
    mclog::tagInfo(_tag, "start set date worker");

    _applied_date = GetHAL().getDateYmd();
    _view         = std::make_unique<DateAdjustView>(_applied_date);
}

void SetDateWorker::update()
{
    if (consumeGlobalExitShortcut()) {
        return;
    }

    if (_view && _view->consumeSaveRequested()) {
        GetHAL().setDateYmd(_view->currentDate());
        _is_done = true;
    }
}

SetDateWorker::~SetDateWorker()
{
}
