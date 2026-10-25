#pragma once

#include "../src/NeniaMidiSequence.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nenia::app {

struct NeniaMidiTimedTempo
{
    qint64 positionMs = 0;
    std::uint32_t microsecondsPerQuarter = 500'000;
};

struct NeniaMidiTimedTimeSignature
{
    qint64 positionMs = 0;
    QString value;
    bool valid = false;
};

struct NeniaMidiMetadata
{
    QString title;
    QString sourcePath;
    QString duration;
    QString format;
    QString tracks;
    QString channels;
    QString ppqn;
    QString copyright;
    std::uint16_t smfFormat = 0;
    std::uint16_t ticksPerQuarterNote = 0;
    std::size_t trackCount = 0;
    std::size_t channelCount = 0;
    std::uint32_t initialTempoUsPerQuarter = 500'000;
    bool hasExplicitInitialTempo = false;
    std::size_t tempoChangeCount = 0;
    bool hasTimeSignatureEvents = false;
    bool hasInvalidTimeSignatureEvents = false;
    std::vector<NeniaMidiTimedTempo> timedTempos;
    std::vector<NeniaMidiTimedTimeSignature> timedTimeSignatures;
};

NeniaMidiMetadata extractNeniaMidiMetadata(const detail::NeniaMidiSequence &sequence,
                                           const QString &sourcePath);

QString midiTempoAtPosition(const NeniaMidiMetadata &metadata, qint64 positionMs);
QString midiTimeSignatureAtPosition(const NeniaMidiMetadata &metadata, qint64 positionMs);

} // namespace nenia::app
