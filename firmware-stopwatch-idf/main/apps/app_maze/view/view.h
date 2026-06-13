/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <array>
#include <memory>
#include <vector>

namespace view {

class MazeView {
public:
    static constexpr int GridSize = 11;
    static constexpr int CellSize = 26;
    static constexpr int BallSize = 14;

    void init(lv_obj_t* parent, const std::array<const char*, GridSize>& maze);
    void setBallPosition(float maze_x, float maze_y);
    void setWon(bool won);

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _board;
    std::unique_ptr<uitk::lvgl_cpp::Container> _goal;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ball_shadow;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ball;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _status;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _walls;

    void setupCell(uitk::lvgl_cpp::Container& cell, uint32_t color, int x, int y, int size, int radius);
};

}  // namespace view
