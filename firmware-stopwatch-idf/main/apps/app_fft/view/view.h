/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

class FftView {
public:
    static constexpr std::size_t band_count         = 20;
    static constexpr std::size_t reduced_band_count = 4;
    using SpectrumBands                             = std::array<float, band_count>;
    using ReducedSpectrumBands                      = std::array<float, reduced_band_count>;

    void init(lv_obj_t* parent);
    void setSpectrum(const SpectrumBands& bands);
    void setPeakFrequencyHz(float frequencyHz);
    void update();
    const SpectrumBands& displayBands() const
    {
        return _display_bands;
    }
    const ReducedSpectrumBands& reducedBands() const
    {
        return _reduced_bands;
    }
    int barRotation() const
    {
        return _bar_rot;
    }
    int barOffset() const
    {
        return _bar_ofs;
    }
    float barBlend() const
    {
        return _bar_blend;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _click_mask;
    std::unique_ptr<uitk::lvgl_cpp::Container> _center_disc;
    std::unique_ptr<uitk::lvgl_cpp::Container> _spectrum_panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _peak_frequency_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _peak_frequency_unit_label;
    SpectrumBands _target_bands         = {};
    SpectrumBands _display_bands        = {};
    ReducedSpectrumBands _reduced_bands = {};
    int _bar_rot                        = 0;
    int _bar_ofs                        = 0;
    int _bass_hit_count                 = 0;
    int _bass_cooldown                  = 0;
    int _rotation_dir                   = 1;
    float _bar_blend                    = 0.0f;
    float _disc_scale                   = 1.0f;
    float _peak_frequency_hz            = 0.0f;
    bool _show_center_labels            = true;

    void toggleCenterLabels();
    void applyCenterLabelVisibility();
    void updateCenterDisc();
    void applyPeakFrequencyLabel();
    void updateReducedBands();
    void updateMotionState();
    void invalidateSpectrum();
};

}  // namespace view