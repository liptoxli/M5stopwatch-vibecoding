/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>

namespace input {

enum class KeyEvent {
    None,
    GoHome,
    GoPrevious,
    GoNext,
};

class KeyManager {
public:
    const KeyEvent& update(bool updateButtonStates = true);
    const KeyEvent& getEvent() const
    {
        return _event;
    }

private:
    KeyEvent _event       = KeyEvent::None;
    bool _go_home_latched = false;
};

}  // namespace input
