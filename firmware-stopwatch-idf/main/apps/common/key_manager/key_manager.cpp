/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_manager.h"

using namespace input;

const KeyEvent& KeyManager::update(bool updateButtonStates)
{
    if (updateButtonStates) {
        GetHAL().updateButtonStates();
    }

    _event = KeyEvent::None;

    if (GetHAL().btnA.isHolding() && GetHAL().btnB.isHolding()) {
        if (!_go_home_latched) {
            _event           = KeyEvent::GoHome;
            _go_home_latched = true;
        }
    } else if (GetHAL().btnA.wasClicked()) {
        _event = KeyEvent::GoPrevious;
    } else if (GetHAL().btnB.wasClicked()) {
        _event = KeyEvent::GoNext;
    } else if (GetHAL().btnA.isReleased() && GetHAL().btnB.isReleased()) {
        _go_home_latched = false;
    }

    return _event;
}
