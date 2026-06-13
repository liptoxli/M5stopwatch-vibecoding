/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace model {

class Stopwatch {
public:
    enum class State_t { Stopped, Running, Paused };

    void start();
    void pause();
    void reset();
    void lap();

    uint32_t getElapsedTime() const;
    const std::string& elapsedtimeToString(uint32_t time);
    inline const std::string& getElapsedtimeString()
    {
        return elapsedtimeToString(getElapsedTime());
    }
    const std::vector<uint32_t>& getLaps() const;
    State_t getState() const;

private:
    uint32_t _start_time   = 0;
    uint32_t _elapsed_time = 0;
    State_t _state         = State_t::Stopped;
    std::vector<uint32_t> _laps;

    struct ConvertBuffer_t {
        uint32_t hours   = 0;
        uint32_t minutes = 0;
        uint32_t seconds = 0;
        uint32_t ms      = 0;
        std::string time_str;
    };
    ConvertBuffer_t _convert_buffer;

    uint32_t millis() const;
};

}  // namespace model
