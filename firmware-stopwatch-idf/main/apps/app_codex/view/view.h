/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace view {

class CodexView {
public:
    enum class VoiceMode : uint8_t {
        Idle,
        Recording,
        Processing,
    };

    struct QuotaSlot {
        float used  = 0.0f;
        float limit = 1.0f;
        std::string resetInLabel;
    };

    struct State {
        QuotaSlot fiveHour;
        QuotaSlot weekly;
        bool bleConnected  = false;
        bool wifiConnected = false;
        bool processing    = false;
        std::string message;
        uint32_t messageExpiresAtMs = 0;
    };

    void init(lv_obj_t* parent);
    void update();
    void applyQuota(int fiveHourLeftPct,
                    int weeklyLeftPct,
                    const std::string& fiveHourResetLabel,
                    const std::string& weeklyResetLabel,
                    bool valid,
                    bool wifiConnected,
                    bool processing,
                    const std::string& message);
    void applyBleState(bool connected, const std::string& hostMessage, bool hostMessageChanged);
    void setVoiceActive(bool active);
    void setVoiceMode(VoiceMode mode);
    bool consumeClearInputRequest();

private:
    struct EdgeQuotaBar {
        std::unique_ptr<uitk::lvgl_cpp::Image> trackImage;
        std::unique_ptr<uitk::lvgl_cpp::Container> fillClip;
        std::unique_ptr<uitk::lvgl_cpp::Image> fillImage;
        std::unique_ptr<uitk::lvgl_cpp::Label> percentLabel;
        std::unique_ptr<uitk::lvgl_cpp::Label> title;
        std::unique_ptr<uitk::lvgl_cpp::Container> underline;
        std::unique_ptr<uitk::lvgl_cpp::Label> resetLabel;
        float renderedRatio = -1.0f;
    };

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_hour_panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_minute_panel;
    std::unique_ptr<uitk::lvgl_cpp::NumberFlow> _clock_hour_flow;
    std::unique_ptr<uitk::lvgl_cpp::NumberFlow> _clock_minute_flow;
    std::unique_ptr<uitk::lvgl_cpp::Label> _clock_colon;
    EdgeQuotaBar _bar_5h;
    EdgeQuotaBar _bar_week;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_hit_area;
    std::unique_ptr<uitk::lvgl_cpp::Image> _pet_image;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_glow;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_shadow;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_left_tab;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_right_tab;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_side_button;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_body;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_screen;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pet_highlight;
    std::unique_ptr<uitk::lvgl_cpp::Label> _pet_face;
    std::unique_ptr<uitk::lvgl_cpp::Label> _message_label;
    std::unique_ptr<uitk::lvgl_cpp::Image> _message_image;
    std::unique_ptr<uitk::lvgl_cpp::Container> _voice_waveform;
    std::array<std::unique_ptr<uitk::lvgl_cpp::Container>, 9> _voice_bars;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ble_dot;
    std::unique_ptr<uitk::lvgl_cpp::Container> _wifi_dot;

    State _state;
    uint32_t _last_quota_update_tick = 0;
    uint32_t _last_clock_tick = 0;
    uint32_t _last_imu_update_tick = 0;
    uint32_t _last_pet_update_tick = 0;
    uint32_t _last_idle_tick = 0;
    uint32_t _last_shake_tick = 0;
    uint32_t _pet_effect_until_tick = 0;
    uint32_t _message_phrase_counter = 0;
    uint8_t _shake_trigger_count = 0;
    float _tilt_x             = 0.0f;
    float _tilt_y             = 0.0f;
    float _shake_energy       = 0.0f;
    bool _pet_pressed        = false;
    bool _message_image_active = false;
    bool _clear_input_requested = false;
    VoiceMode _voice_mode = VoiceMode::Idle;
    enum class PetAnim {
        Idle,
        Blink,
        Touch,
        LookAround,
        Stretch,
    };
    PetAnim _pet_anim = PetAnim::Idle;
    uint32_t _pet_anim_start_tick = 0;
    uint32_t _next_blink_tick = 0;
    uint32_t _next_idle_action_tick = 0;
    uint8_t _idle_action_kind = 0;
    const void* _pet_current_src = nullptr;

    void initQuotaBar(EdgeQuotaBar& bar,
                      const char* title,
                      const char* resetLabel,
                      bool leftSide,
                      lv_color_t color);
    void initFlipClock();
    void updateFlipClock(bool force = false);
    void drawQuotaBar(EdgeQuotaBar& bar, bool leftSide, lv_color_t color, float remainingRatio);
    void updateQuotaBar(EdgeQuotaBar& bar,
                        const QuotaSlot& slot,
                        bool leftSide,
                        lv_color_t color,
                        const char* title);
    void initPet();
    void updatePet();
    void initVoiceWaveform();
    void updateVoiceWaveform();
    void setPetFrame(const void* src);
    void updateMotionInput();
    void setMessage(const char* message, uint32_t ttlMs);
    void setMessageImage(const void* src, uint32_t ttlMs);
    void setMessageText(const std::string& message);
    void setIdleMessageImage(uint32_t salt);
    void updateConnectionDots();
    float remainingRatio(const QuotaSlot& slot) const;
};

}  // namespace view
