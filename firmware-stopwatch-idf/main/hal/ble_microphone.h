#pragma once

#include <cstdint>

struct ble_gap_event;

namespace ble_microphone {

bool register_service();
void start_capture_task();
void on_connected(std::uint16_t connection_handle);
void on_disconnected();
void on_gap_event(const ble_gap_event& event);
bool is_streaming();
void begin_voice_input();
void end_voice_input();

}  // namespace ble_microphone
