#pragma once

#include <QByteArray>

namespace nenia::detail {

inline bool neniaMidiAudioTraceEnabled() noexcept
{
    return qgetenv("NENIA_MIDI_AUDIO_TRACE") == QByteArrayLiteral("1");
}

} // namespace nenia::detail
