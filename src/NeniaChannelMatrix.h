#pragma once

#include "NeniaAnimationState.h"

#include <QString>

namespace nenia::detail {

// Every visible row represents exactly one zero-based physical MIDI channel.
constexpr int midiChannelForMatrixRow(int row) noexcept
{
    if (row < 0 || row >= kVisibleTrackCount) {
        return -1;
    }
    return row;
}

inline QString matrixChannelLabel(int row)
{
    const int channel = midiChannelForMatrixRow(row);
    if (channel < 0) {
        return {};
    }
    return QStringLiteral("CH%1").arg(channel + 1, 2, 10, QLatin1Char('0'));
}

} // namespace nenia::detail
