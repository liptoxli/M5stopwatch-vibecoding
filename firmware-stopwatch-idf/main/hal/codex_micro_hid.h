#pragma once

#include <array>
#include <cstdint>

struct ble_gap_event;

namespace codex_micro_hid {

struct AgentSlotStatus {
    bool assigned = false;
    uint32_t color = 0;
    float brightness = 0.0f;
    uint8_t effect = 0;
    float speed = 0.0f;
};

struct StatusSnapshot {
    bool connected = false;
    bool ready = false;
    bool communicating = false;
    std::array<AgentSlotStatus, 6> agents = {};
    uint32_t sequence = 0;
};

struct StatusSummary {
    bool connected = false;
    bool ready = false;
    bool communicating = false;
    uint8_t assigned = 0;
    uint8_t working = 0;
    uint8_t unread = 0;
    uint8_t attention = 0;
    uint8_t errors = 0;
    uint32_t sequence = 0;
};

// Adds one unified HID service containing the existing keyboard and consumer
// reports plus the Codex Micro vendor-defined control channel. The microphone
// and companion GATT services remain independent.
bool register_service();

void on_connected(uint16_t conn_handle);
void on_disconnected();
void on_gap_event(const ble_gap_event& event);

// Sends one native encoder detent per delta step. Positive values select a
// higher reasoning level; negative values select a lower level.
bool send_reasoning_delta(int delta);
bool tap_agent_slot(uint8_t slot);
bool send_radial(float angle, float distance);
bool ready();
StatusSummary status_summary();
StatusSnapshot status_snapshot();

}  // namespace codex_micro_hid
