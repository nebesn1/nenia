#pragma once

#include <QtGlobal>

#include <array>

namespace nenia::detail {

inline constexpr int kSpectrumBandCount = 32;
inline constexpr qint64 kSpectrumMaxAgeUs = 200'000;

using SpectrumBands = std::array<float, kSpectrumBandCount>;

struct SpectrumFrame
{
    qint64 centerTimeUs = 0;
    SpectrumBands bands{};
};

} // namespace nenia::detail
