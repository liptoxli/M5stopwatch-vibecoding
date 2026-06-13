/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "watch_face.h"
#include <hal/hal.h>
#include <string_view>
#include <mooncake_log.h>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

static const std::string_view _tag         = "WatchFaceManager";
static constexpr int _gesture_min_distance = 60;

WatchFaceManager::~WatchFaceManager()
{
    int watch_face_count = static_cast<int>(_watch_faces.size());
    if (_current_index >= 0 && _current_index < watch_face_count) {
        // Clean up the active watch face before manager is destroyed.
        _watch_faces[_current_index]->onDestroy();
    }
}

void WatchFaceManager::init()
{
    mclog::tagInfo(_tag, "init");

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(466, 466);
    _panel->setBgColor(lv_color_black());
    _panel->setPaddingAll(0);
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // Register available watch faces here.
    _watch_faces.push_back(std::make_unique<WatchFaceClassic>());
    _watch_faces.push_back(std::make_unique<WatchFaceNumberFlow>());
    _watch_faces.push_back(std::make_unique<WatchFaceBigNumber>());
    _watch_faces.push_back(std::make_unique<WatchFaceSimple>());

    if (!_watch_faces.empty()) {
        // Show the first watch face on the next update tick.
        _next_index = 0;
    }
}

void WatchFaceManager::update()
{
    if (_watch_faces.empty()) {
        return;
    }

    update_gesture();

    if (_next_index >= 0 && _next_index != _current_index) {
        if (_current_index >= 0) {
            // Destroy the old watch face before switching.
            _watch_faces[_current_index]->onDestroy();
        }

        _current_index = _next_index;
        _next_index    = -1;

        // Create the new watch face after the index is updated.
        _watch_faces[_current_index]->onCreate(_panel->get());
    }

    if (_current_index >= 0) {
        // Update the active watch face every frame.
        _watch_faces[_current_index]->onUpdate();
    }
}

void WatchFaceManager::goNext()
{
    if (_watch_faces.empty()) {
        return;
    }

    int watch_face_count = static_cast<int>(_watch_faces.size());
    int base_index       = 0;

    if (_current_index >= 0) {
        base_index = _current_index;
    }

    _next_index = (base_index + 1) % watch_face_count;

    mclog::tagInfo(_tag, "go next watch face, next index: {}", _next_index);
}

void WatchFaceManager::goPrevious()
{
    if (_watch_faces.empty()) {
        return;
    }

    int watch_face_count = static_cast<int>(_watch_faces.size());
    int base_index       = 0;

    if (_current_index >= 0) {
        base_index = _current_index;
    }

    _next_index = base_index - 1;
    if (_next_index < 0) {
        _next_index = watch_face_count - 1;
    }

    mclog::tagInfo(_tag, "go previous watch face, next index: {}", _next_index);
}

void WatchFaceManager::update_gesture()
{
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    bool is_pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (is_pressed) {
        if (!_gesture_pressing) {
            // Start tracking from the first pressed point.
            _gesture_pressing    = true;
            _gesture_start_point = point;
        }

        _gesture_last_point = point;
        return;
    }

    if (!_gesture_pressing) {
        return;
    }

    _gesture_pressing = false;

    int delta_x = _gesture_last_point.x - _gesture_start_point.x;
    int delta_y = _gesture_last_point.y - _gesture_start_point.y;
    int abs_x   = delta_x >= 0 ? delta_x : -delta_x;
    int abs_y   = delta_y >= 0 ? delta_y : -delta_y;

    // Only handle clear horizontal swipes.
    if (abs_x < _gesture_min_distance || abs_x <= abs_y) {
        return;
    }

    if (delta_x < 0) {
        goNext();
    } else {
        goPrevious();
    }
}
