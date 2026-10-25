#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nenia::detail {

inline constexpr std::size_t kMidiAnalysisBlockFrames = 256;

struct NeniaMidiPcmAnalysisBlock
{
    std::uint64_t generation = 0;
    std::uint64_t firstOutputFrame = 0;
    std::uint32_t sampleRate = 0;
    std::size_t frameCount = 0;
    std::array<float, kMidiAnalysisBlockFrames * 2U> stereoFloat{};
};

} // namespace nenia::detail
