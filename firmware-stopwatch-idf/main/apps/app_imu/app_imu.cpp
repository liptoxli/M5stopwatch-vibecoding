/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_imu.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kBaselineSamples = 16;
constexpr float kFlatMaxXYG = 0.08f;
constexpr float kFlatMaxZErrorG = 0.12f;
constexpr float kAccelDeadZoneG = 0.012f;
constexpr float kGyroDeadZoneDps = 0.5f;

bool is_flat_pose(float accel_x, float accel_y, float accel_z)
{
    return std::fabs(accel_x) < kFlatMaxXYG && std::fabs(accel_y) < kFlatMaxXYG &&
           std::fabs(accel_z - 1.0f) < kFlatMaxZErrorG;
}

float apply_dead_zone(float value, float dead_zone)
{
    return std::fabs(value) < dead_zone ? 0.0f : value;
}

}  // namespace

AppImu::AppImu()
{
    setAppInfo().name = "IMU";
    setAppInfo().icon = (void*)&icon_imu;
}

void AppImu::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppImu::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    calibrateBaseline();

    LvglLockGuard lock;

    _yaw           = 0.0f;
    _yaw_last_tick = GetHAL().millis();

    _view = std::make_unique<view::ImuView>();
    _view->init(lv_screen_active());
    _view->setBallOffset(0.0f, 0.0f);
    _view->setBallSize(0.0f);
    _view->setOrbitBallAngle(_yaw);
}

void AppImu::onRunning()
{
    const auto key_event = _key_manager->update();
    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }
    if (key_event == input::KeyEvent::GoPrevious || key_event == input::KeyEvent::GoNext) {
        calibrateBaseline();
        _yaw = 0.0f;
        _yaw_last_tick = GetHAL().millis();
    }

    LvglLockGuard lock;

    if (_view) {
        GetHAL().updateImuData();

        uint32_t now = GetHAL().millis();
        float dt     = static_cast<float>(now - _yaw_last_tick) / 1000.0f;
        if (dt <= 0.0f) {
            dt = 0.016f;
        }
        _yaw_last_tick = now;

        const auto& imu_data = GetHAL().getImuData();
        const float accel_x = apply_dead_zone(imu_data.accelX - _baseline_accel_x, kAccelDeadZoneG);
        const float accel_y = apply_dead_zone(imu_data.accelY - _baseline_accel_y, kAccelDeadZoneG);
        const float accel_z = 1.0f + apply_dead_zone(imu_data.accelZ - _baseline_accel_z, kAccelDeadZoneG);
        const float gyro_x = apply_dead_zone(imu_data.gyroX - _baseline_gyro_x, kGyroDeadZoneDps);
        const float gyro_y = apply_dead_zone(imu_data.gyroY - _baseline_gyro_y, kGyroDeadZoneDps);
        const float gyro_z = apply_dead_zone(imu_data.gyroZ - _baseline_gyro_z, kGyroDeadZoneDps);
        float motion = std::sqrt(gyro_x * gyro_x + gyro_y * gyro_y + gyro_z * gyro_z);
        float normalized_motion = std::clamp(motion / 180.0f, 0.0f, 1.0f);

        _yaw += gyro_z * dt;
        if (_yaw >= 360.0f || _yaw <= -360.0f) {
            _yaw = std::fmod(_yaw, 360.0f);
        }

        _view->setBallOffset(accel_x, accel_y);
        _view->setBallSize(normalized_motion);
        _view->setOrbitBallAngle(_yaw);
        _view->setAccelLabelValues(accel_x, accel_y, accel_z);
        _view->update();
    }
}

void AppImu::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
    _yaw           = 0.0f;
    _yaw_last_tick = 0;
}

void AppImu::calibrateBaseline()
{
    float accel_x = 0.0f;
    float accel_y = 0.0f;
    float accel_z = 0.0f;
    float gyro_x  = 0.0f;
    float gyro_y  = 0.0f;
    float gyro_z  = 0.0f;

    for (int i = 0; i < kBaselineSamples; ++i) {
        GetHAL().updateImuData();
        const auto& imu = GetHAL().getImuData();
        accel_x += imu.accelX;
        accel_y += imu.accelY;
        accel_z += imu.accelZ;
        gyro_x += imu.gyroX;
        gyro_y += imu.gyroY;
        gyro_z += imu.gyroZ;
        GetHAL().delay(6);
    }

    const float sample_count = static_cast<float>(kBaselineSamples);
    const float avg_accel_x = accel_x / sample_count;
    const float avg_accel_y = accel_y / sample_count;
    const float avg_accel_z = accel_z / sample_count;
    const float avg_gyro_x  = gyro_x / sample_count;
    const float avg_gyro_y  = gyro_y / sample_count;
    const float avg_gyro_z  = gyro_z / sample_count;
    const bool flat_pose    = is_flat_pose(avg_accel_x, avg_accel_y, avg_accel_z);

    _baseline_accel_x = flat_pose ? avg_accel_x : 0.0f;
    _baseline_accel_y = flat_pose ? avg_accel_y : 0.0f;
    _baseline_accel_z = flat_pose ? avg_accel_z : 1.0f;
    _baseline_gyro_x  = flat_pose ? avg_gyro_x : 0.0f;
    _baseline_gyro_y  = flat_pose ? avg_gyro_y : 0.0f;
    _baseline_gyro_z  = flat_pose ? avg_gyro_z : 0.0f;

    mclog::tagInfo(getAppInfo().name,
                   "baseline mode={} raw_accel=({:.3f},{:.3f},{:.3f}) raw_gyro=({:.3f},{:.3f},{:.3f}) "
                   "active_accel=({:.3f},{:.3f},{:.3f}) active_gyro=({:.3f},{:.3f},{:.3f}) samples={}",
                   flat_pose ? "measured-flat" : "system-standard",
                   avg_accel_x,
                   avg_accel_y,
                   avg_accel_z,
                   avg_gyro_x,
                   avg_gyro_y,
                   avg_gyro_z,
                   _baseline_accel_x,
                   _baseline_accel_y,
                   _baseline_accel_z,
                   _baseline_gyro_x,
                   _baseline_gyro_y,
                   _baseline_gyro_z,
                   kBaselineSamples);
}
