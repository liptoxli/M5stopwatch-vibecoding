/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace {

void spectrum_draw_event_cb(lv_event_t* e);

using namespace view;
using namespace uitk::lvgl_cpp;

constexpr int _panel_size       = 466;
constexpr int _disc_size        = 137;
constexpr int _spectrum_size    = 250;
constexpr int _ext_draw_size    = 180;
constexpr int _bar_rest_radius  = 82;
constexpr int _bar_max_radius   = 156;
constexpr int _bar_color1_stop  = 80;
constexpr int _bar_color2_stop  = 100;
constexpr int _bar_per_band_cnt = static_cast<int>(FftView::band_count / FftView::reduced_band_count);

constexpr uint32_t _bg_color    = 0x1F1528;
constexpr uint32_t _disc_color  = 0xF8C6E7;
constexpr uint32_t _value_color = 0xAE5D92;
constexpr uint32_t _unit_color  = 0xD586BA;
constexpr uint32_t _bar_color1  = 0xC19BFF;
constexpr uint32_t _bar_color2  = 0xE97AE0;
constexpr uint32_t _bar_color3  = 0xFF66BC;
constexpr int _deg_step         = 180 / static_cast<int>(FftView::band_count);

constexpr float _view_attack_alpha  = 0.60f;
constexpr float _view_release_alpha = 0.36f;
constexpr float _disc_follow_alpha  = 0.40f;

constexpr std::array<int, FftView::reduced_band_count> _band_widths = {20, 8, 4, 2};
constexpr std::array<int, 10> _rnd_array                            = {994, 285, 553, 11, 792, 707, 966, 641, 852, 827};

float clamp_band(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

int32_t get_cos(int32_t deg, int32_t amplitude)
{
    int32_t result = lv_trigo_cos(deg) * amplitude;
    result += LV_TRIGO_SIN_MAX / 2;
    return result >> LV_TRIGO_SHIFT;
}

int32_t get_sin(int32_t deg, int32_t amplitude)
{
    int32_t result = lv_trigo_sin(deg) * amplitude;
    return (result + LV_TRIGO_SIN_MAX / 2) >> LV_TRIGO_SHIFT;
}

lv_color_t mix_bar_color(int radius)
{
    if (radius < _bar_color1_stop) {
        return lv_color_hex(_bar_color1);
    }

    if (radius > _bar_max_radius) {
        return lv_color_hex(_bar_color3);
    }

    if (radius > _bar_color2_stop) {
        return lv_color_mix(
            lv_color_hex(_bar_color3), lv_color_hex(_bar_color2),
            static_cast<uint8_t>(((radius - _bar_color2_stop) * 255) / (_bar_max_radius - _bar_color2_stop)));
    }

    return lv_color_mix(
        lv_color_hex(_bar_color2), lv_color_hex(_bar_color1),
        static_cast<uint8_t>(((radius - _bar_color1_stop) * 255) / (_bar_color2_stop - _bar_color1_stop)));
}

}  // namespace

void FftView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _click_mask = std::make_unique<Container>(_panel->get());
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_panel_size, _panel_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { toggleCenterLabels(); });

    _spectrum_panel = std::make_unique<Container>(_panel->get());
    _spectrum_panel->align(LV_ALIGN_CENTER, 0, 0);
    _spectrum_panel->setSize(_spectrum_size, _spectrum_size);
    _spectrum_panel->setRadius(LV_RADIUS_CIRCLE);
    _spectrum_panel->setBorderWidth(0);
    _spectrum_panel->setPaddingAll(0);
    _spectrum_panel->setBgOpa(LV_OPA_TRANSP);
    _spectrum_panel->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _spectrum_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_spectrum_panel->get(), spectrum_draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_spectrum_panel->get());

    _center_disc = std::make_unique<Container>(_panel->get());
    _center_disc->align(LV_ALIGN_CENTER, 0, 0);
    _center_disc->setSize(_disc_size, _disc_size);
    _center_disc->setRadius(LV_RADIUS_CIRCLE);
    _center_disc->setBorderWidth(0);
    _center_disc->setPaddingAll(0);
    _center_disc->setBgColor(lv_color_hex(_disc_color));
    _center_disc->setBgOpa(LV_OPA_COVER);
    _center_disc->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _peak_frequency_label = std::make_unique<Label>(_center_disc->get());
    _peak_frequency_label->align(LV_ALIGN_CENTER, 0, -4);
    _peak_frequency_label->setTextFont(&lv_font_montserrat_28);
    _peak_frequency_label->setTextColor(lv_color_hex(_value_color));
    _peak_frequency_label->setText("0.0");

    _peak_frequency_unit_label = std::make_unique<Label>(_center_disc->get());
    _peak_frequency_unit_label->align(LV_ALIGN_CENTER, 0, 24);
    _peak_frequency_unit_label->setTextFont(&lv_font_montserrat_18);
    _peak_frequency_unit_label->setTextColor(lv_color_hex(_unit_color));
    _peak_frequency_unit_label->setText("Hz");

    _click_mask->moveForeground();
    applyCenterLabelVisibility();

    invalidateSpectrum();
}

void FftView::setSpectrum(const SpectrumBands& bands)
{
    _target_bands = bands;
}

void FftView::setPeakFrequencyHz(float frequencyHz)
{
    _peak_frequency_hz = std::max(0.0f, frequencyHz);
    applyPeakFrequencyLabel();
}

void FftView::toggleCenterLabels()
{
    _show_center_labels = !_show_center_labels;
    applyCenterLabelVisibility();
}

void FftView::applyCenterLabelVisibility()
{
    if (_peak_frequency_label) {
        _peak_frequency_label->setHidden(!_show_center_labels);
    }

    if (_peak_frequency_unit_label) {
        _peak_frequency_unit_label->setHidden(!_show_center_labels);
    }
}

void FftView::update()
{
    bool changed = false;

    for (std::size_t i = 0; i < band_count; ++i) {
        float target   = clamp_band(_target_bands[i]);
        float current  = _display_bands[i];
        float alpha    = target > current ? _view_attack_alpha : _view_release_alpha;
        float smoothed = current + (target - current) * alpha;

        if (std::fabs(smoothed - current) > 0.0015f) {
            changed = true;
        }

        _display_bands[i] = smoothed;
    }

    updateReducedBands();
    updateMotionState();
    updateCenterDisc();

    if (changed) {
        invalidateSpectrum();
    }
}

void FftView::updateCenterDisc()
{
    if (_center_disc == nullptr) {
        return;
    }

    float pulse        = std::clamp(_reduced_bands[0] * 0.08f + _reduced_bands[1] * 0.025f, 0.0f, 0.1f);
    float target_scale = 1.0f + pulse;
    _disc_scale += (target_scale - _disc_scale) * _disc_follow_alpha;

    int size = static_cast<int>(std::lround(_disc_size * _disc_scale));
    _center_disc->setSize(size, size);
    _center_disc->align(LV_ALIGN_CENTER, 0, 0);
}

void FftView::applyPeakFrequencyLabel()
{
    if (_peak_frequency_label == nullptr) {
        return;
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%.0f", _peak_frequency_hz);
    _peak_frequency_label->setText(buffer);
}

void FftView::updateReducedBands()
{
    for (std::size_t band = 0; band < reduced_band_count; ++band) {
        std::size_t start  = band * _bar_per_band_cnt;
        std::size_t end    = start + _bar_per_band_cnt;
        float weighted_sum = 0.0f;
        float weight_sum   = 0.0f;

        for (std::size_t i = start; i < end; ++i) {
            float local    = static_cast<float>(i - start) / static_cast<float>(_bar_per_band_cnt - 1);
            float emphasis = 1.0f - local * 0.35f;
            weighted_sum += _display_bands[i] * emphasis;
            weight_sum += emphasis;
        }

        _reduced_bands[band] = weight_sum > 0.0f ? weighted_sum / weight_sum : 0.0f;
    }
}

void FftView::updateMotionState()
{
    _bar_blend += 0.035f;
    if (_bar_blend >= 1.0f) {
        _bar_blend -= 1.0f;
        _bar_ofs = (_bar_ofs + 1) % static_cast<int>(_rnd_array.size());
    }

    if (_bass_cooldown > 0) {
        --_bass_cooldown;
    }

    float bass = _reduced_bands[0];
    if (bass > 0.84f && _bass_cooldown == 0) {
        ++_bass_hit_count;
        _bass_cooldown = 14;
        if (_bass_hit_count >= 3) {
            _bass_hit_count = 0;
            _bar_ofs        = (_bar_ofs + 1) % static_cast<int>(_rnd_array.size());
        }
    }

    if (bass < 0.12f) {
        _bar_rot = (_bar_rot + _rotation_dir + static_cast<int>(band_count)) % static_cast<int>(band_count);
    }
}

void FftView::invalidateSpectrum()
{
    if (_spectrum_panel) {
        lv_obj_invalidate(_spectrum_panel->get());
    }
}

namespace {

void spectrum_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<FftView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, _ext_draw_size);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_opa_t opa      = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    if (opa <= LV_OPA_MIN) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_point_t center = {
        static_cast<lv_coord_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<lv_coord_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    lv_draw_triangle_dsc_t draw_dsc;
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.opa = LV_OPA_COVER;

    std::array<int, FftView::band_count> radii = {};
    radii.fill(_bar_rest_radius);

    for (std::size_t s = 0; s < FftView::reduced_band_count; ++s) {
        int band_w    = _band_widths[s];
        int amplitude = static_cast<int>((_bar_max_radius - _bar_rest_radius) * clamp_band(view->reducedBands()[s]));

        for (int f = 0; f < band_w; ++f) {
            int32_t ampl_mod = get_cos(f * 360 / band_w + 180, 180) + 180;
            int t            = _bar_per_band_cnt * static_cast<int>(s) - band_w / 2 + f;
            if (t < 0) {
                t += static_cast<int>(FftView::band_count);
            }
            if (t >= static_cast<int>(FftView::band_count)) {
                t -= static_cast<int>(FftView::band_count);
            }

            radii[t] += (amplitude * ampl_mod) >> 9;
        }
    }

    for (std::size_t i = 0; i < FftView::band_count; ++i) {
        int j          = (static_cast<int>(i) + view->barRotation() +
                          _rnd_array[view->barOffset() % static_cast<int>(_rnd_array.size())]) %
                         static_cast<int>(FftView::band_count);
        int k          = (static_cast<int>(i) + view->barRotation() +
                          _rnd_array[(view->barOffset() + 1) % static_cast<int>(_rnd_array.size())]) %
                         static_cast<int>(FftView::band_count);
        int radius     = static_cast<int>(radii[k] * view->barBlend() + radii[j] * (1.0f - view->barBlend()));
        draw_dsc.color = mix_bar_color(radius);

        int32_t deg_space   = 1;
        int32_t deg         = static_cast<int32_t>(i) * _deg_step + 90;
        int32_t outer_deg_a = deg + deg_space;
        int32_t outer_deg_b = deg + _deg_step - deg_space;

        int32_t x1_out = get_cos(outer_deg_a, radius);
        int32_t y1_out = get_sin(outer_deg_a, radius);
        int32_t x2_out = get_cos(outer_deg_b, radius);
        int32_t y2_out = get_sin(outer_deg_b, radius);
        int32_t x_in   = get_cos(outer_deg_b, 0);
        int32_t y_in   = get_sin(outer_deg_b, 0);

        draw_dsc.p[0].x = center.x + x1_out;
        draw_dsc.p[0].y = center.y + y1_out;
        draw_dsc.p[1].x = center.x + x2_out;
        draw_dsc.p[1].y = center.y + y2_out;
        draw_dsc.p[2].x = center.x + x_in;
        draw_dsc.p[2].y = center.y + y_in;
        lv_draw_triangle(layer, &draw_dsc);

        draw_dsc.p[0].x = center.x - x1_out;
        draw_dsc.p[1].x = center.x - x2_out;
        draw_dsc.p[2].x = center.x - x_in;
        lv_draw_triangle(layer, &draw_dsc);
    }
}

}  // namespace