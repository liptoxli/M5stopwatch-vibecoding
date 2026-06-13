/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int kPanelSize = 466;
constexpr int kBoardSize = MazeView::GridSize * MazeView::CellSize;
constexpr int kBoardX = (kPanelSize - kBoardSize) / 2;
constexpr int kBoardY = 92;
constexpr uint32_t kBgColor = 0x02050A;
constexpr uint32_t kBoardColor = 0x07111D;
constexpr uint32_t kWallColor = 0x214366;
constexpr uint32_t kWallEdgeColor = 0x4A9DFF;
constexpr uint32_t kGoalColor = 0x53D36A;
constexpr uint32_t kBallColor = 0xEAFBFF;
constexpr uint32_t kShadowColor = 0x2B96FF;
constexpr uint32_t kTextColor = 0xC9F2FF;
constexpr uint32_t kWinColor = 0x7CFFB2;

}  // namespace

void MazeView::init(lv_obj_t* parent, const std::array<const char*, GridSize>& maze)
{
    _walls.clear();

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(kPanelSize, kPanelSize);
    _panel->setBgColor(lv_color_hex(kBgColor));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setBorderWidth(0);
    _panel->setOutlineWidth(0);
    _panel->setShadowWidth(0);
    _panel->setPaddingAll(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title = std::make_unique<Label>(_panel->get());
    _title->setText("MAZE");
    _title->setTextFont(&lv_font_montserrat_24);
    _title->setTextColor(lv_color_hex(kTextColor));
    _title->align(LV_ALIGN_TOP_MID, 0, 36);

    _board = std::make_unique<Container>(_panel->get());
    _board->setPos(kBoardX, kBoardY);
    _board->setSize(kBoardSize, kBoardSize);
    _board->setBgColor(lv_color_hex(kBoardColor));
    _board->setBgOpa(LV_OPA_COVER);
    _board->setBorderWidth(2);
    _board->setBorderColor(lv_color_hex(0x163152));
    _board->setOutlineWidth(0);
    _board->setShadowWidth(18);
    _board->setShadowColor(lv_color_hex(0x0A5CBA));
    _board->setShadowOpa(LV_OPA_30);
    _board->setPaddingAll(0);
    _board->setRadius(8);
    _board->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < GridSize; ++y) {
        for (int x = 0; x < GridSize; ++x) {
            const char tile = maze[y][x];
            if (tile == '#') {
                auto wall = std::make_unique<Container>(_board->get());
                setupCell(*wall, kWallColor, x * CellSize, y * CellSize, CellSize, 5);
                wall->setBorderWidth(1);
                wall->setBorderColor(lv_color_hex(kWallEdgeColor));
                _walls.push_back(std::move(wall));
            } else if (tile == 'E') {
                _goal = std::make_unique<Container>(_board->get());
                setupCell(*_goal,
                          kGoalColor,
                          x * CellSize + 5,
                          y * CellSize + 5,
                          CellSize - 10,
                          LV_RADIUS_CIRCLE);
                _goal->setShadowWidth(12);
                _goal->setShadowColor(lv_color_hex(kGoalColor));
                _goal->setShadowOpa(LV_OPA_50);
            }
        }
    }

    _ball_shadow = std::make_unique<Container>(_board->get());
    setupCell(*_ball_shadow, kShadowColor, 0, 0, BallSize + 10, LV_RADIUS_CIRCLE);
    _ball_shadow->setBgOpa(LV_OPA_40);

    _ball = std::make_unique<Container>(_board->get());
    setupCell(*_ball, kBallColor, 0, 0, BallSize, LV_RADIUS_CIRCLE);
    _ball->setShadowWidth(16);
    _ball->setShadowColor(lv_color_hex(kShadowColor));
    _ball->setShadowOpa(LV_OPA_70);

    _status = std::make_unique<Label>(_panel->get());
    _status->setText("");
    _status->setTextFont(&lv_font_montserrat_22);
    _status->setTextColor(lv_color_hex(kWinColor));
    _status->align(LV_ALIGN_TOP_MID, 0, 392);
}

void MazeView::setBallPosition(float maze_x, float maze_y)
{
    const int ball_x = kBoardX + static_cast<int>(std::lround(maze_x)) - BallSize / 2;
    const int ball_y = kBoardY + static_cast<int>(std::lround(maze_y)) - BallSize / 2;
    const int shadow_x = kBoardX + static_cast<int>(std::lround(maze_x)) - (BallSize + 10) / 2;
    const int shadow_y = kBoardY + static_cast<int>(std::lround(maze_y)) - (BallSize + 10) / 2;

    if (_ball_shadow) {
        _ball_shadow->setPos(shadow_x - kBoardX, shadow_y - kBoardY);
    }

    if (_ball) {
        _ball->setPos(ball_x - kBoardX, ball_y - kBoardY);
    }
}

void MazeView::setWon(bool won)
{
    if (_status) {
        _status->setText(won ? "CLEAR" : "");
    }
}

void MazeView::setupCell(Container& cell, uint32_t color, int x, int y, int size, int radius)
{
    cell.setPos(x, y);
    cell.setSize(size, size);
    cell.setBgColor(lv_color_hex(color));
    cell.setBgOpa(LV_OPA_COVER);
    cell.setBorderWidth(0);
    cell.setOutlineWidth(0);
    cell.setShadowWidth(0);
    cell.setPaddingAll(0);
    cell.setRadius(radius);
    cell.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}
