/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_maze.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <array>
#include <cmath>

using namespace mooncake;

namespace {

constexpr int kGrid = view::MazeView::GridSize;
constexpr float kCell = static_cast<float>(view::MazeView::CellSize);
constexpr float kBallRadius = static_cast<float>(view::MazeView::BallSize) / 2.0f;
constexpr float kMaxSpeed = 285.0f;
constexpr float kDeadZone = 0.018f;
constexpr float kFullTiltG = 0.72f;
constexpr float kMaxAcceleration = 720.0f;
constexpr float kFriction = 2.9f;
constexpr int kBaselineSamples = 16;
constexpr float kFlatMaxXYG = 0.08f;
constexpr float kFlatMaxZErrorG = 0.12f;

constexpr std::array<const char*, kGrid> kMaze = {
    "###########",
    "#S  #     #",
    "### # ### #",
    "#     #   #",
    "# ### # ###",
    "# #     # #",
    "# ### # # #",
    "#   # #   #",
    "### # # # #",
    "#     #  E#",
    "###########",
};

float apply_dead_zone(float value)
{
    if (std::fabs(value) < kDeadZone) {
        return 0.0f;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

float tilt_to_acceleration(float tilt)
{
    const float normalized = apply_dead_zone(tilt / kFullTiltG);
    const float magnitude = std::fabs(normalized);
    const float graded = magnitude * magnitude * (3.0f - 2.0f * magnitude);
    return std::copysign(graded * kMaxAcceleration, normalized);
}

float apply_friction(float velocity, float dt)
{
    if (velocity == 0.0f) {
        return 0.0f;
    }

    const float next = velocity * std::max(0.0f, 1.0f - kFriction * dt);
    return std::fabs(next) < 2.0f ? 0.0f : next;
}

bool is_flat_pose(float accel_x, float accel_y, float accel_z)
{
    return std::fabs(accel_x) < kFlatMaxXYG && std::fabs(accel_y) < kFlatMaxXYG &&
           std::fabs(accel_z - 1.0f) < kFlatMaxZErrorG;
}

}  // namespace

AppMaze::AppMaze()
{
    setAppInfo().name = "Maze";
    setAppInfo().icon = (void*)&icon_lucky_wheel;
}

void AppMaze::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMaze::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    resetGame();

    LvglLockGuard lock;
    _view = std::make_unique<view::MazeView>();
    _view->init(lv_screen_active(), kMaze);
    _view->setBallPosition(_ball_x, _ball_y);
    _view->setWon(false);
}

void AppMaze::onRunning()
{
    input::KeyEvent key_event = input::KeyEvent::None;
    if (_key_manager) {
        key_event = _key_manager->update();
    }

    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (key_event == input::KeyEvent::GoPrevious || key_event == input::KeyEvent::GoNext) {
        resetGame();
        LvglLockGuard lock;
        if (_view) {
            _view->setBallPosition(_ball_x, _ball_y);
            _view->setWon(false);
        }
        return;
    }

    const uint32_t now = GetHAL().millis();
    float dt = static_cast<float>(now - _last_tick) / 1000.0f;
    if (dt <= 0.0f || dt > 0.08f) {
        dt = 0.016f;
    }
    _last_tick = now;

    if (!_won) {
        GetHAL().updateImuData();
        const auto& imu = GetHAL().getImuData();

        _velocity_x += tilt_to_acceleration(_baseline_accel_x - imu.accelX) * dt;
        _velocity_y += tilt_to_acceleration(_baseline_accel_y - imu.accelY) * dt;
        _velocity_x = apply_friction(_velocity_x, dt);
        _velocity_y = apply_friction(_velocity_y, dt);
        _velocity_x = std::clamp(_velocity_x, -kMaxSpeed, kMaxSpeed);
        _velocity_y = std::clamp(_velocity_y, -kMaxSpeed, kMaxSpeed);

        moveBall(_velocity_x * dt, _velocity_y * dt);
        if (isGoal(_ball_x, _ball_y)) {
            _won = true;
        }
    }

    LvglLockGuard lock;
    if (_view) {
        _view->setBallPosition(_ball_x, _ball_y);
        _view->setWon(_won);
    }
}

void AppMaze::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _view.reset();
}

void AppMaze::resetGame()
{
    _ball_x = 1.5f * kCell;
    _ball_y = 1.5f * kCell;
    _velocity_x = 0.0f;
    _velocity_y = 0.0f;
    _won = false;
    _last_tick = GetHAL().millis();
    calibrateTiltBaseline();
}

void AppMaze::calibrateTiltBaseline()
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

void AppMaze::moveBall(float dx, float dy)
{
    const float next_x = _ball_x + dx;
    if (!collides(next_x, _ball_y)) {
        _ball_x = next_x;
    } else {
        _velocity_x = 0.0f;
    }

    const float next_y = _ball_y + dy;
    if (!collides(_ball_x, next_y)) {
        _ball_y = next_y;
    } else {
        _velocity_y = 0.0f;
    }
}

bool AppMaze::collides(float x, float y) const
{
    const int min_x = static_cast<int>(std::floor((x - kBallRadius) / kCell));
    const int max_x = static_cast<int>(std::floor((x + kBallRadius) / kCell));
    const int min_y = static_cast<int>(std::floor((y - kBallRadius) / kCell));
    const int max_y = static_cast<int>(std::floor((y + kBallRadius) / kCell));

    for (int cy = min_y; cy <= max_y; ++cy) {
        for (int cx = min_x; cx <= max_x; ++cx) {
            if (isWallCell(cx, cy)) {
                return true;
            }
        }
    }
    return false;
}

bool AppMaze::isWallCell(int cell_x, int cell_y) const
{
    if (cell_x < 0 || cell_x >= kGrid || cell_y < 0 || cell_y >= kGrid) {
        return true;
    }
    return kMaze[cell_y][cell_x] == '#';
}

bool AppMaze::isGoal(float x, float y) const
{
    const int cell_x = static_cast<int>(std::floor(x / kCell));
    const int cell_y = static_cast<int>(std::floor(y / kCell));
    return cell_x >= 0 && cell_x < kGrid && cell_y >= 0 && cell_y < kGrid && kMaze[cell_y][cell_x] == 'E';
}
