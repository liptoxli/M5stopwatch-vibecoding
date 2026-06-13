/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <memory>
#include <string_view>

namespace setup_workers {

class PercentageAdjustView;

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool consumeGlobalExitShortcut()
    {
        GetHAL().updateButtonStates();

        const bool both_buttons_held = GetHAL().btnA.isHolding() && GetHAL().btnB.isHolding();
        if (!both_buttons_held) {
            _exit_hold_start_tick = 0;
            _exit_shortcut_latched = false;
            return false;
        }

        const uint32_t now = GetHAL().millis();
        if (_exit_hold_start_tick == 0) {
            _exit_hold_start_tick = now;
            return false;
        }

        if (!_exit_shortcut_latched && now - _exit_hold_start_tick >= 700) {
            _exit_shortcut_latched = true;
            _is_done = true;
            return true;
        }

        return false;
    }

    bool _is_done = false;

private:
    uint32_t _exit_hold_start_tick = 0;
    bool _exit_shortcut_latched = false;
};

/**
 * @brief
 *
 */
class BrightnessWorker : public WorkerBase {
public:
    BrightnessWorker();
    ~BrightnessWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_brightness = 0;
    bool _save_requested    = false;
};

/**
 * @brief
 *
 */
class VolumeWorker : public WorkerBase {
public:
    VolumeWorker();
    ~VolumeWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_volume  = 0;
    bool _save_requested = false;
};

/**
 * @brief
 *
 */
class ButtonWorker : public WorkerBase {
public:
    ButtonWorker();
    ~ButtonWorker();
    void update() override;

private:
    class ButtonConfigView;

    std::unique_ptr<ButtonConfigView> _view;
    Hal::ButtonConfig _applied_config;
};

/**
 * @brief
 *
 */
class BluetoothWorker : public WorkerBase {
public:
    BluetoothWorker();
    ~BluetoothWorker();
    void update() override;

private:
    class BluetoothConfigView;

    std::unique_ptr<BluetoothConfigView> _view;
    bool _applied_enabled = false;
};

/**
 * @brief
 *
 */
class WifiConnectWorker : public WorkerBase {
public:
    WifiConnectWorker();
    ~WifiConnectWorker();
    void update() override;

private:
    class WifiConnectView;

    std::unique_ptr<WifiConnectView> _view;
    uint32_t _connect_start_tick = 0;
    uint32_t _done_tick          = 0;
    bool _connecting             = false;
    bool _scan_requested         = false;
};

/**
 * @brief
 *
 */
class SetTimeWorker : public WorkerBase {
public:
    SetTimeWorker();
    ~SetTimeWorker();
    void update() override;

private:
    class TimeAdjustView;

    std::unique_ptr<TimeAdjustView> _view;
    TimeHms _applied_time;
};

/**
 * @brief
 *
 */
class SetDateWorker : public WorkerBase {
public:
    SetDateWorker();
    ~SetDateWorker();
    void update() override;

private:
    class DateAdjustView;

    std::unique_ptr<DateAdjustView> _view;
    DateYmd _applied_date;
};

/**
 * @brief
 *
 */
class AboutWorker : public WorkerBase {
public:
    AboutWorker();
    ~AboutWorker();
    void update() override;

private:
    class AboutView;

    std::unique_ptr<AboutView> _view;
    int _progress                = 0;
    uint32_t _next_progress_tick = 0;
    int _pending_burst_steps     = 0;
};

}  // namespace setup_workers
