#pragma once

#include <cstdint>
#include <string>

namespace ble_bridge {

enum class ButtonAction : uint8_t {
    Down,
    Up,
    Tap,
};

enum class VoicePhase : uint8_t {
    Idle,
    Recording,
    Processing,
};

void set_enabled(bool enabled);
bool is_enabled();
bool is_connected();
bool disconnect_current();
bool is_typeless_input_mode();

void send_typeless_option(ButtonAction action);
void send_codex_enter();
void send_shake_action();

std::string status_text();
std::string host_status_text();
uint32_t host_status_sequence();

bool host_voice_valid();
bool host_voice_active();
VoicePhase host_voice_phase();
uint32_t host_voice_sequence();

bool host_panel_valid();
std::string host_panel_json();
uint32_t host_panel_sequence();

}  // namespace ble_bridge
