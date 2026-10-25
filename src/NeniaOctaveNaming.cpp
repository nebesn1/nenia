#include "NeniaOctaveNaming.h"

#include "NeniaAnimationState.h"

#include <array>
#include <cstddef>

namespace nenia::detail {

QString noteNameForMidi(int note, OctaveNamingConvention convention)
{
    static constexpr std::array<const char *, 12> pitchNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    if (note < 0 || note >= kMidiNoteCount) {
        return QStringLiteral("--");
    }

    const int octave = note / 12 + octaveNamingDescriptor(convention).octaveOffset;
    return QString::fromLatin1(pitchNames.at(static_cast<std::size_t>(note % 12)))
        + QString::number(octave);
}

} // namespace nenia::detail
