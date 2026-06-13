/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>
#include <hal/hal.h>
#include <tools/event/signal.hpp>
#include <tools/object_pool/object_pool.hpp>
#include <uitk/short_namespace.hpp>

namespace model {

class AlarmClock {
public:
    struct Time24 {
        uint8_t hour   = 0;
        uint8_t minute = 0;

        bool isValid() const;
    };

    struct AlarmTriggeredEvent {
        int alarmId = -1;
        Time24 time;
    };

    class Alarm : public uitk::Poolable {
    public:
        Alarm(const Time24& time, bool enabled);

        const Time24& time() const
        {
            return _time;
        }

        bool enabled() const
        {
            return _enabled;
        }

        void setTime(const Time24& time);
        void setEnabled(bool enabled);

    private:
        friend class AlarmClock;

        Time24 _time;
        bool _enabled                = false;
        int _last_triggered_date_key = -1;
    };

    bool init();
    void update();

    int addAlarm(const Time24& time, bool enabled = true);
    bool removeAlarm(int alarmId);
    bool setAlarmEnabled(int alarmId, bool enabled);
    bool setAlarmTime(int alarmId, const Time24& time);

    Alarm* getAlarm(int alarmId);
    void forEachAlarm(const std::function<void(int, Alarm&)>& visitor);

    AlarmStorageSnapshot exportStorage();
    bool importStorage(const AlarmStorageSnapshot& snapshot, bool persistAfterImport = false);

    uitk::Signal<const AlarmTriggeredEvent&>& onTriggered()
    {
        return _on_triggered;
    }

private:
    bool persist();

    uitk::ObjectPool<Alarm> _alarms;
    uitk::Signal<const AlarmTriggeredEvent&> _on_triggered;
    uint32_t _last_update_ms = 0;
};

}  // namespace model
