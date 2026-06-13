/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

class AppMaze : public mooncake::AppAbility {
public:
    AppMaze();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<view::MazeView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;
    uint32_t _last_tick = 0;
    float _ball_x = 0.0f;
    float _ball_y = 0.0f;
    float _velocity_x = 0.0f;
    float _velocity_y = 0.0f;
    float _baseline_accel_x = 0.0f;
    float _baseline_accel_y = 0.0f;
    float _baseline_accel_z = 0.0f;
    float _baseline_gyro_x  = 0.0f;
    float _baseline_gyro_y  = 0.0f;
    float _baseline_gyro_z  = 0.0f;
    bool _won = false;

    void resetGame();
    void calibrateTiltBaseline();
    void moveBall(float dx, float dy);
    bool collides(float x, float y) const;
    bool isWallCell(int cell_x, int cell_y) const;
    bool isGoal(float x, float y) const;
};
