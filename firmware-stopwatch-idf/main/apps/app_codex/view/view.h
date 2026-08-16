/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <apps/app_codex/codex_quota_client.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace view {

class CodexView {
public:
    enum class ThemeMode : uint8_t {
        OfficialV1,
        OpenWatcherV2,
    };

    enum class VoiceMode : uint8_t {
        Idle,
        Recording,
        Processing,
        Interrupted,
    };

    struct QuotaSlot {
        float used  = 0.0f;
        float limit = 1.0f;
        std::string resetInLabel;
    };

    struct State {
        QuotaSlot weekly;
        int weeklyDayStartLeftPct = -1;
        int weeklySegmentStartLeftPct = -1;
        int weeklyTodayUsedPctPoints = 0;
        int weeklyResetCount = 0;
        std::string weeklyTrackingPeriodStart;
        bool bleConnected  = false;
        bool wifiConnected = false;
        bool processing    = false;
        std::string message;
        std::string source;
        std::string updatedAt;
        bool stale = false;
        std::string hostName;
        std::string sessionTitle = "Codex Ready";
        std::string contextLabel = "-- / --";
        int contextPressurePct = 0;
        int compactThresholdPct = -1;
        bool compactWarning = false;
        std::string totalTokensLabel = "--";
        std::string modelLabel = "--";
        std::string reasoningLabel = "--";
        std::string activityLabel = "--";
        bool activityLive = false;
        std::array<float, 24> activityBuckets = {};
        uint32_t messageExpiresAtMs = 0;
    };

    void init(lv_obj_t* parent, ThemeMode themeMode = ThemeMode::OfficialV1);
    void update();
    void applyQuota(int weeklyLeftPct,
                    const std::string& weeklyResetLabel,
                    bool valid,
                    bool wifiConnected,
                    bool processing,
                    const std::string& message);
    void applySnapshot(const codex::QuotaSnapshot& snapshot);
    void applyBleState(bool connected, const std::string& hostMessage, bool hostMessageChanged);
    void setVoiceActive(bool active);
    void setVoiceMode(VoiceMode mode);
    bool consumeClearInputRequest();
    uint32_t frameIntervalMs() const;

private:
    struct SemicircleQuota {
        std::unique_ptr<uitk::lvgl_cpp::Container> canvas;
        std::unique_ptr<uitk::lvgl_cpp::Label> todayCaption;
        std::unique_ptr<uitk::lvgl_cpp::Label> todayValue;
        std::unique_ptr<uitk::lvgl_cpp::Label> remainingCaption;
        std::unique_ptr<uitk::lvgl_cpp::Label> remainingValue;
        std::unique_ptr<uitk::lvgl_cpp::Label> resetLabel;
    };

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    ThemeMode _theme_mode = ThemeMode::OfficialV1;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_hour_panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _clock_minute_panel;
    std::unique_ptr<uitk::lvgl_cpp::NumberFlow> _clock_hour_flow;
    std::unique_ptr<uitk::lvgl_cpp::NumberFlow> _clock_minute_flow;
    std::unique_ptr<uitk::lvgl_cpp::Label> _clock_colon;
    SemicircleQuota _semicircle_quota;
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
    std::unique_ptr<uitk::lvgl_cpp::Container> _v2_canvas;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_title_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_left_caption_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_remaining_value_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_remaining_unit_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_today_caption_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_today_value_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_status_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_meta_left_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _v2_meta_right_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _v2_sync_indicator;
    std::unique_ptr<uitk::lvgl_cpp::Container> _v2_sync_left_line;
    std::unique_ptr<uitk::lvgl_cpp::Container> _v2_sync_right_line;
    std::unique_ptr<uitk::lvgl_cpp::Container> _v2_sync_dot;
    std::array<int, 9> _voice_bar_heights = {{-1, -1, -1, -1, -1, -1, -1, -1, -1}};
    std::array<int, 9> _voice_bar_opacities = {{-1, -1, -1, -1, -1, -1, -1, -1, -1}};

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

    void initSemicircleQuota();
    void updateSemicircleQuota();
    static void drawSemicircleQuotaEvent(lv_event_t* event);
    void drawSemicircleQuota(lv_layer_t* layer, const lv_area_t& coords);
    void initFlipClock();
    void updateFlipClock(bool force = false);
    void initOpenWatcherV2();
    void updateOpenWatcherV2Labels();
    static void drawOpenWatcherV2Event(lv_event_t* event);
    void drawOpenWatcherV2(lv_layer_t* layer, const lv_area_t& coords);
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
