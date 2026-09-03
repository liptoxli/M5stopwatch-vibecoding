#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ble_bridge {

enum class ButtonAction : uint8_t {
    Down,
    Up,
};

enum class VoicePhase : uint8_t {
    Idle,
    Recording,
    Processing,
    Interrupted,
};

struct CodexTask {
    std::string id;
    std::string title;
};

void set_enabled(bool enabled);
bool is_enabled();
bool is_connected();
bool disconnect_current();
bool is_typeless_input_mode();
bool microphone_voice_ready();
void set_voice_capture_active(bool active);
bool voice_session_interrupted();
bool consume_voice_start_timeout();
void clear_voice_session_interruption();

void send_typeless_option(ButtonAction action);
void cancel_typeless_input_after_microphone_timeout();
void send_codex_enter();
void send_confirm_long_press();
void send_shake_action();

std::string status_text();
std::string host_status_text();
uint32_t host_status_sequence();

bool host_voice_valid();
bool host_voice_active();
VoicePhase host_voice_phase();
uint32_t host_voice_sequence();

bool host_codex_unread_valid();
int host_codex_unread_count();
uint32_t host_codex_unread_sequence();

bool host_codex_tasks_valid();
std::vector<CodexTask> host_codex_tasks();
uint32_t host_codex_tasks_sequence();
bool request_open_codex_task(const std::string& taskId);
bool request_new_codex_task();
bool request_codex_reasoning_delta(int delta);

bool host_panel_valid();
std::string host_panel_json();
uint32_t host_panel_sequence();

}  // namespace ble_bridge
