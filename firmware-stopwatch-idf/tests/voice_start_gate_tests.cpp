#include <cassert>
#include <cstdint>
#include <iostream>

#include "../main/apps/app_codex/voice_start_gate.h"

int main()
{
    VoiceStartGate gate;

    assert(gate.request(false, 100) == VoiceStartGate::Action::None);
    assert(gate.pending());
    assert(gate.request(false, 200) == VoiceStartGate::Action::None);
    assert(gate.poll(true, 500) == VoiceStartGate::Action::Start);
    assert(!gate.pending());
    assert(gate.poll(true, 600) == VoiceStartGate::Action::None);

    assert(gate.request(true, 700) == VoiceStartGate::Action::Start);
    assert(!gate.pending());

    assert(gate.request(false, 1'000) == VoiceStartGate::Action::None);
    assert(gate.poll(false, 3'999) == VoiceStartGate::Action::None);
    assert(gate.poll(false, 4'000) == VoiceStartGate::Action::Timeout);
    assert(!gate.pending());

    assert(gate.request(false, UINT32_MAX - 500) == VoiceStartGate::Action::None);
    assert(gate.poll(false, 2'499) == VoiceStartGate::Action::Timeout);

    gate.request(false, 8'000);
    gate.cancel();
    assert(!gate.pending());
    assert(gate.poll(true, 8'100) == VoiceStartGate::Action::None);

    std::cout << "PASS: voice start waits for readiness, commits once, times out, and cancels\n";
}
