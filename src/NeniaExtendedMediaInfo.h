#pragma once

#include <QString>

#include <array>

namespace nenia::detail {

struct NeniaExtendedMediaInfo
{
    QString trackNumber;
    QString date;
    QString albumArtist;
    QString performer;
    QString composer;
    // Formatted from TagLib's local-file DISCNUMBER/DISCTOTAL properties.
    QString discNumber;
    QString genre;

    // Direct MIDI metadata is intentionally kept in the application-only
    // presentation model; it is not part of Nenia's installed media API.
    bool directMidi = false;
    QString midiSourcePath;
    QString midiDuration;
    QString midiFormat;
    QString midiTrackCount;
    QString midiChannelCount;
    QString midiPpqn;
    QString midiTempo;
    QString midiTimeSignature;
    QString midiCopyright;
    QString soundFontNameAndStatus;
    QString soundFontPath;

    bool operator==(const NeniaExtendedMediaInfo &) const = default;
};

} // namespace nenia::detail
