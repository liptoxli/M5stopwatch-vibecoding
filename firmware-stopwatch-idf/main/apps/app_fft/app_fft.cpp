/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_fft.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;

AppFft::AppFft()
{
    setAppInfo().name = "Audio.FFT";
    setAppInfo().icon = (void*)&icon_fft;
}

void AppFft::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppFft::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _view = std::make_unique<view::FftView>();
    _view->init(lv_screen_active());
}

void AppFft::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    GetHAL().updateAudioSpectrum();

    LvglLockGuard lock;

    if (_view) {
        const auto& spectrum = GetHAL().getAudioSpectrum();
        _view->setSpectrum(spectrum.bands);
        _view->setPeakFrequencyHz(spectrum.peakFrequencyHz);
        _view->update();
    }
}

void AppFft::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
}
