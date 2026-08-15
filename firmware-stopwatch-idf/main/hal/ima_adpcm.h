#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ima_adpcm {

constexpr std::size_t kSamplesPerBlock = 320;
constexpr std::size_t kEncodedBytesPerBlock = (kSamplesPerBlock - 1 + 1) / 2;

struct Block {
    std::int16_t predictor = 0;
    std::uint8_t step_index = 0;
    std::array<std::uint8_t, kEncodedBytesPerBlock> encoded = {};
};

class Encoder {
public:
    void reset();
    bool encode(const std::int16_t* samples, std::size_t sample_count, Block& block);

private:
    std::uint8_t step_index_ = 0;
};

}  // namespace ima_adpcm
