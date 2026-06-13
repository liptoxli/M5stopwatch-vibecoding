/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_width          = 466;
constexpr int _panel_height         = 466;
constexpr int _button_width         = 374;
constexpr int _button_height        = 119;
constexpr int _title_y              = 36;
constexpr int _row_gap              = 21;
constexpr int _title_gap            = 52;
constexpr int _dialog_width         = 404;
constexpr int _dialog_height        = 202;
constexpr int _dialog_button_width  = 148;
constexpr int _dialog_button_height = 63;

constexpr uint32_t _bg_color            = 0x000000;
constexpr uint32_t _button_color        = 0x4C4C4C;
constexpr uint32_t _label_color         = 0xFFFFFF;
constexpr uint32_t _switch_off_color    = 0x3A3A3A;
constexpr uint32_t _switch_on_color     = 0x53BD65;
constexpr uint32_t _dialog_bg_color     = 0x2B2B2B;
constexpr uint32_t _dialog_border_color = 0x6A6A6A;
constexpr uint32_t _dialog_delete_color = 0xEA5858;
constexpr uint32_t _dialog_cancel_color = 0x515151;

std::string format_alarm_time(const model::AlarmClock::Time24& time)
{
    return fmt::format("{:02d}:{:02d}", time.hour, time.minute);
}

}  // namespace

void DeleteAlarmDialog::init(lv_obj_t* parent)
{
    _is_confirmed = false;
    _is_cancelled = false;

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_dialog_width, _dialog_height);
    _panel->setBgColor(lv_color_hex(_dialog_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setBorderColor(lv_color_hex(_dialog_border_color));
    _panel->setBorderWidth(3);
    _panel->setRadius(58);
    _panel->setPaddingAll(0);
    _panel->moveForeground();

    _label = std::make_unique<Label>(_panel->get());
    _label->setText("Delete this alarm?");
    _label->setTextFont(&MontserratSemiBold26);
    _label->setTextColor(lv_color_hex(_label_color));
    _label->align(LV_ALIGN_TOP_LEFT, 49, 40);

    _confirm_button = std::make_unique<Button>(_panel->get());
    _confirm_button->setSize(_dialog_button_width, _dialog_button_height);
    _confirm_button->align(LV_ALIGN_CENTER, -92, 36);
    _confirm_button->setRadius(LV_RADIUS_CIRCLE);
    _confirm_button->setBorderWidth(0);
    _confirm_button->setShadowWidth(0);
    _confirm_button->setBgColor(lv_color_hex(_dialog_delete_color));
    _confirm_button->label().setText("Delete");
    _confirm_button->label().setTextFont(&lv_font_montserrat_24);
    _confirm_button->label().setTextColor(lv_color_hex(_label_color));
    _confirm_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _confirm_button->onClick().connect([this]() { _is_confirmed = true; });

    _cancel_button = std::make_unique<Button>(_panel->get());
    _cancel_button->setSize(_dialog_button_width, _dialog_button_height);
    _cancel_button->align(LV_ALIGN_CENTER, 92, 36);
    _cancel_button->setRadius(LV_RADIUS_CIRCLE);
    _cancel_button->setBorderWidth(0);
    _cancel_button->setShadowWidth(0);
    _cancel_button->setBgColor(lv_color_hex(_dialog_cancel_color));
    _cancel_button->label().setText("Cancel");
    _cancel_button->label().setTextFont(&lv_font_montserrat_24);
    _cancel_button->label().setTextColor(lv_color_hex(_label_color));
    _cancel_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _cancel_button->onClick().connect([this]() { _is_cancelled = true; });
}

AlarmListView::AlarmListView(model::AlarmClock& alarmClock) : _alarm_clock(alarmClock)
{
}

void AlarmListView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->setSize(_panel_width, _panel_height);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setPadding(0, 72, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    rebuild();
}

void AlarmListView::update()
{
    if (!_delete_dialog) {
        return;
    }

    if (_delete_dialog->isConfirmed()) {
        if (_pending_delete_alarm_id >= 0) {
            _alarm_clock.removeAlarm(_pending_delete_alarm_id);
        }
        _delete_dialog.reset();
        _pending_delete_alarm_id = -1;
        rebuild();
        return;
    }

    if (_delete_dialog->isCancelled()) {
        _delete_dialog.reset();
        _pending_delete_alarm_id = -1;
    }
}

bool AlarmListView::consumeAddRequested()
{
    bool requested = _add_requested;
    _add_requested = false;
    return requested;
}

void AlarmListView::rebuild()
{
    _rows.clear();
    _add_button.reset();
    _title_label.reset();
    _delete_dialog.reset();
    _pending_delete_alarm_id = -1;

    int cursor_y = _title_y;
    createTitle(cursor_y);
    cursor_y += _title_gap;

    _alarm_clock.forEachAlarm([&](int alarmId, model::AlarmClock::Alarm& alarm) {
        createAlarmRow(cursor_y, alarmId, alarm);
        cursor_y += _button_height + _row_gap;
    });

    createAddButton(cursor_y);
}

void AlarmListView::createTitle(int y)
{
    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->setText("Alarms");
    _title_label->setTextFont(&MontserratSemiBold26);
    _title_label->setTextColor(lv_color_hex(_label_color));
    _title_label->align(LV_ALIGN_TOP_MID, 0, y);
}

void AlarmListView::createAlarmRow(int y, int alarmId, const model::AlarmClock::Alarm& alarm)
{
    auto row     = std::make_unique<AlarmRow>();
    row->alarmId = alarmId;

    row->button = std::make_unique<Button>(_panel->get());
    row->button->setSize(_button_width, _button_height);
    row->button->align(LV_ALIGN_TOP_MID, 0, y);
    row->button->setBgColor(lv_color_hex(_button_color));
    row->button->setBorderWidth(0);
    row->button->setShadowWidth(0);
    row->button->setRadius(60);
    row->button->setPaddingAll(0);
    row->button->addEventCb(handleAlarmButtonLongPressed, LV_EVENT_LONG_PRESSED, this);

    row->timeLabel = std::make_unique<Label>(row->button->get());
    row->timeLabel->setText(format_alarm_time(alarm.time()));
    row->timeLabel->setTextFont(&lv_font_montserrat_36);
    row->timeLabel->setTextColor(lv_color_hex(_label_color));
    row->timeLabel->align(LV_ALIGN_LEFT_MID, 49, 0);

    row->enabledSwitch = std::make_unique<Switch>(row->button->get());
    row->enabledSwitch->setSize(80, 44);
    row->enabledSwitch->align(LV_ALIGN_RIGHT_MID, -36, 0);
    row->enabledSwitch->setValue(alarm.enabled());
    row->enabledSwitch->setBgColor(lv_color_hex(_switch_off_color), LV_PART_MAIN);
    row->enabledSwitch->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
    row->enabledSwitch->setBorderWidth(0, LV_PART_MAIN);
    row->enabledSwitch->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row->enabledSwitch->get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(row->enabledSwitch->get(), LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(row->enabledSwitch->get(), 0, LV_PART_KNOB);
    lv_obj_set_style_radius(row->enabledSwitch->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
    row->enabledSwitch->setBgColor(lv_color_hex(_switch_on_color), LV_PART_INDICATOR | LV_STATE_CHECKED);
    row->enabledSwitch->setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    row->enabledSwitch->setBorderWidth(0, LV_PART_INDICATOR | LV_STATE_CHECKED);
    row->enabledSwitch->onValueChanged().connect(
        [this, alarmId](bool enabled) { _alarm_clock.setAlarmEnabled(alarmId, enabled); });

    _rows.push_back(std::move(row));
}

void AlarmListView::createAddButton(int y)
{
    _add_button = std::make_unique<Button>(_panel->get());
    _add_button->setSize(_button_width, _button_height);
    _add_button->align(LV_ALIGN_TOP_MID, 0, y);
    _add_button->setBgColor(lv_color_hex(_button_color));
    _add_button->setBorderWidth(0);
    _add_button->setShadowWidth(0);
    _add_button->setRadius(60);
    _add_button->label().setText("Add");
    _add_button->label().setTextFont(&lv_font_montserrat_28);
    _add_button->label().setTextColor(lv_color_hex(_label_color));
    _add_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _add_button->label().setWidth(294);
    _add_button->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    _add_button->onClick().connect([this]() { _add_requested = true; });
}

void AlarmListView::showDeleteDialog(int alarmId)
{
    _delete_dialog.reset();

    _pending_delete_alarm_id = alarmId;

    _delete_dialog = std::make_unique<DeleteAlarmDialog>();
    _delete_dialog->init(lv_screen_active());
}

AlarmListView::AlarmRow* AlarmListView::findRow(lv_obj_t* target)
{
    for (auto& row : _rows) {
        if (row->button && row->button->get() == target) {
            return row.get();
        }
    }
    return nullptr;
}

void AlarmListView::handleAlarmButtonLongPressed(lv_event_t* e)
{
    auto* self = static_cast<AlarmListView*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    auto* row = self->findRow(lv_event_get_target_obj(e));
    if (row == nullptr) {
        return;
    }

    self->showDeleteDialog(row->alarmId);
}