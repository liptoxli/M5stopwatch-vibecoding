#pragma once

#include <cstdint>

// Holds one user request while the BLE microphone finishes its handshake.
// It deliberately owns no BLE or UI state, so reconnect and wrap-around
// behavior can be verified without ESP-IDF hardware.
class VoiceStartGate {
public:
    enum class Action : std::uint8_t {
        None,
        Start,
        Timeout,
    };

    static constexpr std::uint32_t kTimeoutMs = 3000;

    Action request(bool ready, std::uint32_t now)
    {
        if (ready) {
            return Action::Start;
        }
        if (!_pending) {
            _pending = true;
            _started_at = now;
        }
        return Action::None;
    }

    Action poll(bool ready, std::uint32_t now)
    {
        if (!_pending) {
            return Action::None;
        }
        if (ready) {
            cancel();
            return Action::Start;
        }
        if (static_cast<std::uint32_t>(now - _started_at) >= kTimeoutMs) {
            cancel();
            return Action::Timeout;
        }
        return Action::None;
    }

    void cancel()
    {
        _pending = false;
        _started_at = 0;
    }

    bool pending() const { return _pending; }

private:
    bool _pending = false;
    std::uint32_t _started_at = 0;
};
