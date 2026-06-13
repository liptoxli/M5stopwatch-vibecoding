/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "alarm_clock.h"
#include <algorithm>
#include <ctime>
#include <string_view>
#include <hal/hal.h>
#include <mooncake_log.h>

using namespace model;

static const std::string_view _tag = "AlarmClockModel";

namespace {

std::string format_time(const AlarmClock::Time24& time)
{
    return fmt::format("{:02d}:{:02d}", time.hour, time.minute);
}

}  // namespace

bool AlarmClock::Time24::isValid() const
{
    return hour < 24 && minute < 60;
}

AlarmClock::Alarm::Alarm(const Time24& time, bool enabled) : _time(time), _enabled(enabled)
{
}

void AlarmClock::Alarm::setTime(const Time24& time)
{
    _time = time;
}

void AlarmClock::Alarm::setEnabled(bool enabled)
{
    _enabled = enabled;
}

bool AlarmClock::init()
{
    mclog::tagInfo(_tag, "init");

    _alarms.clear();
    _last_update_ms = 0;

    AlarmStorageSnapshot snapshot;
    if (!GetHAL().loadAlarmStorage(snapshot)) {
        mclog::tagError(_tag, "load alarm storage failed");
        return false;
    }

    mclog::tagInfo(_tag, "loaded storage snapshot, count: {}", snapshot.count);

    return importStorage(snapshot, false);
}

void AlarmClock::update()
{
    uint32_t now_ms = GetHAL().millis();
    if (_last_update_ms != 0 && now_ms - _last_update_ms < 1000) {
        return;
    }
    _last_update_ms = now_ms;

    std::time_t now     = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    if (local_time == nullptr) {
        return;
    }

    const int date_key = (local_time->tm_year + 1900) * 10000 + (local_time->tm_mon + 1) * 100 + local_time->tm_mday;

    _alarms.forEach([&](Alarm* alarm, int alarm_id) {
        if (alarm == nullptr || !alarm->enabled()) {
            return;
        }

        if (alarm->time().hour != local_time->tm_hour || alarm->time().minute != local_time->tm_min) {
            return;
        }

        if (alarm->_last_triggered_date_key == date_key) {
            return;
        }

        alarm->_last_triggered_date_key = date_key;
        AlarmTriggeredEvent event;
        event.alarmId = alarm_id;
        event.time    = alarm->time();
        mclog::tagInfo(_tag, "alarm triggered, id: {}, time: {}", alarm_id, format_time(event.time));
        _on_triggered.emit(event);
    });
}

int AlarmClock::addAlarm(const Time24& time, bool enabled)
{
    if (!time.isValid() || _alarms.activeCount() >= AlarmStorageSnapshot::maxAlarmCount) {
        mclog::tagError(_tag, "add alarm failed, time: {}, enabled: {}, active count: {}", format_time(time), enabled,
                        _alarms.activeCount());
        return -1;
    }

    auto alarm_id = _alarms.create(std::make_unique<Alarm>(time, enabled));
    mclog::tagInfo(_tag, "add alarm, id: {}, time: {}, enabled: {}", alarm_id, format_time(time), enabled);
    if (!persist()) {
        mclog::tagError(_tag, "persist after add failed, id: {}", alarm_id);
    }

    return alarm_id;
}

bool AlarmClock::removeAlarm(int alarmId)
{
    if (!_alarms.destroy(alarmId)) {
        mclog::tagError(_tag, "remove alarm failed, invalid id: {}", alarmId);
        return false;
    }

    mclog::tagInfo(_tag, "remove alarm, id: {}", alarmId);

    return persist();
}

bool AlarmClock::setAlarmEnabled(int alarmId, bool enabled)
{
    auto* alarm = getAlarm(alarmId);
    if (alarm == nullptr) {
        mclog::tagError(_tag, "set enabled failed, invalid id: {}, enabled: {}", alarmId, enabled);
        return false;
    }

    alarm->setEnabled(enabled);
    if (!enabled) {
        alarm->_last_triggered_date_key = -1;
    }
    mclog::tagInfo(_tag, "set alarm enabled, id: {}, time: {}, enabled: {}", alarmId, format_time(alarm->time()),
                   enabled);
    return persist();
}

bool AlarmClock::setAlarmTime(int alarmId, const Time24& time)
{
    if (!time.isValid()) {
        mclog::tagError(_tag, "set time failed, invalid time: {}, id: {}", format_time(time), alarmId);
        return false;
    }

    auto* alarm = getAlarm(alarmId);
    if (alarm == nullptr) {
        mclog::tagError(_tag, "set time failed, invalid id: {}, time: {}", alarmId, format_time(time));
        return false;
    }

    alarm->setTime(time);
    alarm->_last_triggered_date_key = -1;
    mclog::tagInfo(_tag, "set alarm time, id: {}, time: {}", alarmId, format_time(time));
    return persist();
}

AlarmClock::Alarm* AlarmClock::getAlarm(int alarmId)
{
    return _alarms.get(alarmId);
}

void AlarmClock::forEachAlarm(const std::function<void(int, Alarm&)>& visitor)
{
    _alarms.forEach([&](Alarm* alarm, int alarm_id) {
        if (alarm != nullptr) {
            visitor(alarm_id, *alarm);
        }
    });
}

AlarmStorageSnapshot AlarmClock::exportStorage()
{
    AlarmStorageSnapshot snapshot;
    snapshot.count = 0;

    _alarms.forEach([&](Alarm* alarm, int) {
        if (alarm == nullptr || snapshot.count >= AlarmStorageSnapshot::maxAlarmCount) {
            return;
        }

        auto& out    = snapshot.alarms[snapshot.count++];
        out.hour     = alarm->time().hour;
        out.minute   = alarm->time().minute;
        out.enabled  = alarm->enabled() ? 1 : 0;
        out.reserved = 0;
    });

    return snapshot;
}

bool AlarmClock::importStorage(const AlarmStorageSnapshot& snapshot, bool persistAfterImport)
{
    _alarms.clear();

    mclog::tagInfo(_tag, "import storage, count: {}, persist: {}", snapshot.count, persistAfterImport);

    const std::size_t import_count = std::min<std::size_t>(snapshot.count, AlarmStorageSnapshot::maxAlarmCount);
    for (std::size_t i = 0; i < import_count; ++i) {
        const auto& entry = snapshot.alarms[i];
        if (!entry.isValid()) {
            mclog::tagError(_tag, "skip invalid storage entry, index: {}, hour: {}, minute: {}, enabled: {}", i,
                            entry.hour, entry.minute, entry.enabled);
            continue;
        }

        Time24 time = {
            .hour   = entry.hour,
            .minute = entry.minute,
        };
        _alarms.create(std::make_unique<Alarm>(time, entry.enabled != 0));
    }

    if (!persistAfterImport) {
        return true;
    }

    return persist();
}

bool AlarmClock::persist()
{
    auto snapshot = exportStorage();
    bool ok       = GetHAL().saveAlarmStorage(snapshot);
    if (ok) {
        mclog::tagInfo(_tag, "persist storage, count: {}", snapshot.count);
    } else {
        mclog::tagError(_tag, "persist storage failed, count: {}", snapshot.count);
    }
    return ok;
}
