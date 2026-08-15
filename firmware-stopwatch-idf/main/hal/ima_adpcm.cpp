#include "ima_adpcm.h"

#include <algorithm>

namespace {

constexpr std::array<int, 16> kIndexTable = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr std::array<int, 89> kStepTable = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

std::uint8_t encode_nibble(std::int16_t sample, int& predictor, int& step_index)
{
    const int step = kStepTable[step_index];
    int difference = static_cast<int>(sample) - predictor;
    std::uint8_t code = 0;
    if (difference < 0) {
        code = 8;
        difference = -difference;
    }

    int reconstructed_difference = step >> 3;
    if (difference >= step) {
        code |= 4;
        difference -= step;
        reconstructed_difference += step;
    }
    if (difference >= (step >> 1)) {
        code |= 2;
        difference -= step >> 1;
        reconstructed_difference += step >> 1;
    }
    if (difference >= (step >> 2)) {
        code |= 1;
        reconstructed_difference += step >> 2;
    }

    predictor += (code & 8) != 0 ? -reconstructed_difference : reconstructed_difference;
    predictor = std::clamp(predictor, -32768, 32767);
    step_index = std::clamp(step_index + kIndexTable[code], 0, 88);
    return code;
}

}  // namespace

namespace ima_adpcm {

void Encoder::reset()
{
    step_index_ = 0;
}

bool Encoder::encode(const std::int16_t* samples, std::size_t sample_count, Block& block)
{
    if (samples == nullptr || sample_count != kSamplesPerBlock) {
        return false;
    }

    block = {};
    block.predictor = samples[0];
    block.step_index = step_index_;
    int predictor = block.predictor;
    int step_index = block.step_index;
    for (std::size_t sample_index = 1; sample_index < sample_count; ++sample_index) {
        const std::size_t code_index = sample_index - 1;
        const std::uint8_t code = encode_nibble(samples[sample_index], predictor, step_index);
        if ((code_index & 1U) == 0) {
            block.encoded[code_index / 2] = code;
        } else {
            block.encoded[code_index / 2] |= static_cast<std::uint8_t>(code << 4);
        }
    }
    step_index_ = static_cast<std::uint8_t>(step_index);
    return true;
}

}  // namespace ima_adpcm
