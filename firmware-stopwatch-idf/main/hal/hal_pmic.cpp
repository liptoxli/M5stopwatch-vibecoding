/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <M5PM1.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <cmath>
#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const std::string_view _tag = "HAL-PMIC";
static std::unique_ptr<M5PM1> _pm1;

namespace {

uint8_t battery_millivolts_to_percent(uint16_t millivolts)
{
    constexpr uint16_t _battery_mv_empty = 3300;
    constexpr uint16_t _battery_mv_full  = 4200;

    if (millivolts <= _battery_mv_empty) {
        return 0;
    }
    if (millivolts >= _battery_mv_full) {
        return 100;
    }

    const auto scaled =
        static_cast<uint32_t>(millivolts - _battery_mv_empty) * 100U / (_battery_mv_full - _battery_mv_empty);
    return static_cast<uint8_t>(std::min<uint32_t>(scaled, 100U));
}

constexpr uint32_t _bat_reading_period_ms = 1000;
constexpr uint32_t _bat_audit_period_ms = 60 * 1000;
constexpr uint16_t _external_power_mv_threshold = 4000;
constexpr uint16_t _bat_filter_weight_old = 7;
constexpr uint16_t _bat_filter_weight_new = 1;
constexpr uint16_t _bat_filter_weight_sum = _bat_filter_weight_old + _bat_filter_weight_new;

uint8_t _bat_level        = 0;
uint16_t _bat_filtered_mv = 0;
std::mutex _bat_level_mutex;

struct BatteryAuditSample {
    uint32_t elapsed_ms = 0;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
};

std::array<BatteryAuditSample, 90> _bat_audit_samples = {};
std::size_t _bat_audit_count = 0;
TickType_t _bat_audit_start_tick = 0;
TickType_t _bat_audit_last_sample_tick = 0;
bool _bat_audit_on_battery = false;

void update_bat_level(uint8_t level)
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    _bat_level = level;
}

void update_bat_level_from_mv(uint16_t millivolts)
{
    if (_bat_filtered_mv == 0) {
        _bat_filtered_mv = millivolts;
    } else {
        const uint32_t filtered = static_cast<uint32_t>(_bat_filtered_mv) * _bat_filter_weight_old +
                                  static_cast<uint32_t>(millivolts) * _bat_filter_weight_new;
        _bat_filtered_mv = static_cast<uint16_t>((filtered + (_bat_filter_weight_sum / 2)) / _bat_filter_weight_sum);
    }

    update_bat_level(battery_millivolts_to_percent(_bat_filtered_mv));
}

uint8_t get_cached_battery_level()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return _bat_level;
}

void reset_battery_audit(TickType_t now_tick)
{
    _bat_audit_count = 0;
    _bat_audit_start_tick = now_tick;
    _bat_audit_last_sample_tick = 0;
}

void append_battery_audit_sample(TickType_t now_tick, uint16_t battery_mv)
{
    if (_bat_audit_start_tick == 0) {
        _bat_audit_start_tick = now_tick;
    }

    const auto elapsed_ms = static_cast<uint32_t>((now_tick - _bat_audit_start_tick) * portTICK_PERIOD_MS);
    const BatteryAuditSample sample = {
        .elapsed_ms = elapsed_ms,
        .battery_mv = battery_mv,
        .battery_percent = get_cached_battery_level(),
    };

    if (_bat_audit_count < _bat_audit_samples.size()) {
        _bat_audit_samples[_bat_audit_count++] = sample;
    } else {
        std::move(_bat_audit_samples.begin() + 1, _bat_audit_samples.end(), _bat_audit_samples.begin());
        _bat_audit_samples.back() = sample;
    }
    _bat_audit_last_sample_tick = now_tick;
}

void log_battery_audit_summary()
{
    if (_bat_audit_count < 2) {
        mclog::tagInfo(_tag, "battery audit: not enough samples");
        return;
    }

    const auto& first = _bat_audit_samples[0];
    const auto& last = _bat_audit_samples[_bat_audit_count - 1];
    const uint32_t elapsed_ms = std::max<uint32_t>(last.elapsed_ms - first.elapsed_ms, 1);
    const int mv_delta = static_cast<int>(first.battery_mv) - static_cast<int>(last.battery_mv);
    const int percent_delta = static_cast<int>(first.battery_percent) - static_cast<int>(last.battery_percent);
    const float hours = static_cast<float>(elapsed_ms) / 3600000.0f;
    const float mv_per_hour = static_cast<float>(mv_delta) / hours;
    const float percent_per_hour = static_cast<float>(percent_delta) / hours;

    mclog::tagInfo(_tag,
                   "battery audit: samples={}, elapsed={}s, vbat {}->{}mV (delta={}mV, {:.1f}mV/h), level {}->{}% "
                   "(delta={}%, {:.1f}%/h)",
                   _bat_audit_count, elapsed_ms / 1000, first.battery_mv, last.battery_mv, mv_delta, mv_per_hour,
                   first.battery_percent, last.battery_percent, percent_delta, percent_per_hour);
}

void bat_reading_task(void* param)
{
    mclog::tagInfo(_tag, "start bat reading task");

    while (true) {
        if (_pm1) {
            uint16_t battery_mv = 0;
            if (_pm1->readVbat(&battery_mv) == M5PM1_OK) {
                update_bat_level_from_mv(battery_mv);
            }

            uint16_t vin_mv = 0;
            if (_pm1->readVin(&vin_mv) == M5PM1_OK) {
                const bool on_battery = vin_mv <= _external_power_mv_threshold;
                const TickType_t now_tick = xTaskGetTickCount();
                if (on_battery && !_bat_audit_on_battery) {
                    reset_battery_audit(now_tick);
                    append_battery_audit_sample(now_tick, battery_mv);
                    mclog::tagInfo(_tag, "battery audit: started, vbat={}mV, level={}%", battery_mv,
                                   get_cached_battery_level());
                } else if (on_battery && (now_tick - _bat_audit_last_sample_tick) >= pdMS_TO_TICKS(_bat_audit_period_ms)) {
                    append_battery_audit_sample(now_tick, battery_mv);
                } else if (!on_battery && _bat_audit_on_battery) {
                    append_battery_audit_sample(now_tick, battery_mv);
                    log_battery_audit_summary();
                }
                _bat_audit_on_battery = on_battery;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(_bat_reading_period_ms));
    }
}

}  // namespace

// PMIC IO
#define PMG0_RTC_IMU_INT M5PM1_GPIO_NUM_0
#define PMG2_CHG_STAT    M5PM1_GPIO_NUM_2
#define PMG4_PORT_INT    M5PM1_GPIO_NUM_4
#define PMG3_CHG_PROG    M5PM1_GPIO_NUM_3
#define PMG1_G12_PY_IRQ  M5PM1_GPIO_NUM_1

void Hal::pmic_init()
{
    mclog::tagInfo(_tag, "pmic init");

    _pm1 = std::make_unique<M5PM1>();
    if (_pm1->begin(i2c_bus_get_internal_bus_handle(_i2c_bus)) != M5PM1_OK) {
        mclog::tagInfo(_tag, "init failed");
        _pm1.reset();
        return;
    }

    _pm1->setI2cSleepTime(0);
    _pm1->setI2cSleepTime(0);

    // set button delay click 1s
    _pm1->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    // disable WDT, default is open
    _pm1->wdtSet(0);
    //  hold LDO power close when power off, keep power for RTC
    _pm1->ldoSetPowerHold(true);

    // set charge enable or disable, this setting will keep working after power off
    _pm1->setChargeEnable(true);

    // drive CHG_PROG low to force active charge programming
    _pm1->gpioSet(PMG3_CHG_PROG, M5PM1_GPIO_MODE_OUTPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);

    _pm1->gpioSetFunc(PMG2_CHG_STAT, M5PM1_GPIO_FUNC_GPIO);
    _pm1->gpioSetMode(PMG2_CHG_STAT, M5PM1_GPIO_MODE_INPUT);
    _pm1->gpioSetPull(PMG2_CHG_STAT, M5PM1_GPIO_PULL_NONE);

    _pm1->setSingleResetDisable(true);

    uint16_t battery_mv = 0;
    if (_pm1->readVbat(&battery_mv) == M5PM1_OK) {
        update_bat_level_from_mv(battery_mv);
    }

    xTaskCreate(bat_reading_task, "bat_reading", 4 * 1024, NULL, 1, NULL);
}

bool Hal::pmic_get_pwr_btn_state()
{
    bool result = false;
    if (_pm1) {
        return _pm1->btnGetState(&result);
    }
    return result;
}

uint8_t Hal::getBatteryLevel()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return _bat_level;
}

bool Hal::isBatteryCharging(bool strict)
{
    if (!_pm1) {
        return false;
    }

    uint16_t vin_mv       = 0;
    uint8_t charge_status = 1;

    const auto vin_result = _pm1->readVin(&vin_mv);
    const auto chg_result = _pm1->gpioGetInput(PMG2_CHG_STAT, &charge_status);
    if (vin_result != M5PM1_OK || chg_result != M5PM1_OK) {
        return false;
    }

    const bool external_power_inserted = vin_mv > 4000;
    const bool charging_active         = charge_status == 0;

    if (strict) {
        return external_power_inserted && charging_active;
    }
    return external_power_inserted;
}
