#include "NeniaMidiMetadata.h"

#include "../src/NeniaMidiChannelState.h"

#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <type_traits>
#include <variant>

namespace nenia::app {
namespace {

QString decodeMidiText(const QByteArray &bytes)
{
    QByteArray text = bytes;
    while (!text.isEmpty() && text.endsWith('\0')) {
        text.chop(1);
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString decoded = decoder.decode(text);
    if (decoder.hasError()) {
        return {};
    }
    if (!decoded.isEmpty() && decoded.front() == QChar(0xfeff)) {
        decoded.removeFirst();
    }
    for (QChar &character : decoded) {
        if (character.category() == QChar::Other_Control
            && character != QLatin1Char('\t') && character != QLatin1Char('\n')
            && character != QLatin1Char('\r')) {
            character = QLatin1Char(' ');
        }
    }
    return decoded.section(QRegularExpression(QStringLiteral("[\\r\\n]")), 0, 0).trimmed();
}

QString durationText(qint64 milliseconds)
{
    const qint64 totalMilliseconds = std::max<qint64>(milliseconds, 0);
    const qint64 seconds = totalMilliseconds / 1000;
    const qint64 millis = totalMilliseconds % 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds / 60) % 60;
    const qint64 remainder = seconds % 60;
    QString result;
    if (hours > 0) {
        result = QStringLiteral("%1:%2:%3")
            .arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainder, 2, 10, QLatin1Char('0'));
    } else {
        result = QStringLiteral("%1:%2").arg(seconds / 60)
            .arg(remainder, 2, 10, QLatin1Char('0'));
    }
    if (millis != 0) {
        result += QStringLiteral(".%1").arg(millis, 3, 10, QLatin1Char('0'));
    }
    return result;
}

QString bpmText(std::uint32_t microsecondsPerQuarter)
{
    if (microsecondsPerQuarter == 0) {
        return {};
    }
    QString value = QLocale::c().toString(
        60'000'000.0 / static_cast<double>(microsecondsPerQuarter), 'f', 2);
    while (value.endsWith(QLatin1Char('0'))) {
        value.chop(1);
    }
    if (value.endsWith(QLatin1Char('.'))) {
        value.chop(1);
    }
    return value + QStringLiteral(" BPM");
}

QString validTimeSignature(const detail::NeniaMidiMetaEvent &meta)
{
    if (meta.data.size() != 4) {
        return {};
    }
    const auto numerator = static_cast<unsigned char>(meta.data.at(0));
    const auto exponent = static_cast<unsigned char>(meta.data.at(1));
    if (numerator == 0 || exponent > 7) {
        return {};
    }
    const unsigned int denominator = 1U << exponent;
    return QStringLiteral("%1/%2").arg(numerator).arg(denominator);
}

std::optional<qint64> tickToMs(const detail::NeniaMidiSequence &sequence,
                               detail::NeniaMidiTick tick)
{
    qint64 milliseconds = 0;
    if (!sequence.tickToMilliseconds(tick, &milliseconds, nullptr)) {
        return std::nullopt;
    }
    return milliseconds;
}

} // namespace

NeniaMidiMetadata extractNeniaMidiMetadata(const detail::NeniaMidiSequence &sequence,
                                           const QString &sourcePath)
{
    NeniaMidiMetadata result;
    result.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    result.smfFormat = sequence.format();
    result.trackCount = sequence.originalTrackCount();
    result.ticksPerQuarterNote = sequence.ticksPerQuarterNote();
    result.format = QStringLiteral("SMF Format %1").arg(result.smfFormat);
    result.tracks = QString::number(result.trackCount);
    result.ppqn = QString::number(result.ticksPerQuarterNote);

    qint64 durationMs = 0;
    if (sequence.tickToMilliseconds(sequence.durationTick(), &durationMs, nullptr)) {
        result.duration = durationText(durationMs);
    }

    std::array<bool, detail::NeniaMidiChannelState::kChannelCount> usedChannels{};
    QString soleTrackName;
    QString copyright;
    bool hasInstrumentName = false;
    for (const detail::NeniaMidiEvent &event : sequence.events()) {
        std::visit([&](const auto &value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, detail::NeniaMidiNoteOnEvent>
                          || std::is_same_v<Event, detail::NeniaMidiNoteOffEvent>
                          || std::is_same_v<Event, detail::NeniaMidiPolyPressureEvent>
                          || std::is_same_v<Event, detail::NeniaMidiControlChangeEvent>
                          || std::is_same_v<Event, detail::NeniaMidiProgramChangeEvent>
                          || std::is_same_v<Event, detail::NeniaMidiChannelPressureEvent>
                          || std::is_same_v<Event, detail::NeniaMidiPitchBendEvent>) {
                usedChannels.at(value.channel) = true;
            } else if constexpr (std::is_same_v<Event, detail::NeniaMidiMetaEvent>) {
                if (value.type == 0x03U && event.sourceTrackIndex == 0
                    && soleTrackName.isEmpty()) {
                    soleTrackName = decodeMidiText(value.data);
                } else if (value.type == 0x04U && event.sourceTrackIndex == 0) {
                    hasInstrumentName = true;
                } else if (value.type == 0x02U && copyright.isEmpty()) {
                    copyright = decodeMidiText(value.data);
                } else if (value.type == 0x58U) {
                    result.hasTimeSignatureEvents = true;
                    const QString signature = validTimeSignature(value);
                    const auto eventMs = tickToMs(sequence, event.tick);
                    if (signature.isEmpty()) {
                        result.hasInvalidTimeSignatureEvents = true;
                        if (eventMs.has_value()) {
                            result.timedTimeSignatures.push_back(
                                {eventMs.value(), {}, false});
                        }
                    } else if (eventMs.has_value()) {
                        result.timedTimeSignatures.push_back(
                            {eventMs.value(), signature, true});
                    }
                }
            }
        }, event.payload);
    }
    for (const detail::NeniaMidiTempoEvent &event : sequence.tempoEvents()) {
        if (event.tick == 0) {
            result.initialTempoUsPerQuarter = event.microsecondsPerQuarter;
            result.hasExplicitInitialTempo = true;
        } else {
            ++result.tempoChangeCount;
        }
        const auto eventMs = tickToMs(sequence, event.tick);
        if (eventMs.has_value()) {
            result.timedTempos.push_back(
                {eventMs.value(), event.microsecondsPerQuarter});
        }
    }
    result.channelCount = static_cast<std::size_t>(std::count(usedChannels.begin(),
                                                               usedChannels.end(), true));
    result.channels = QString::number(result.channelCount);
    result.copyright = copyright;

    // In Format 0 the single track name can safely serve as a sequence title
    // only when the file does not separately identify an instrument track.
    // Format 1 track names are routinely instrument/part names, so they are
    // never promoted to the composition title here.
    if (result.smfFormat == 0 && result.trackCount == 1 && !hasInstrumentName) {
        result.title = soleTrackName;
    }
    if (result.title.isEmpty()) {
        result.title = QFileInfo(sourcePath).completeBaseName();
    }
    return result;
}

QString midiTempoAtPosition(const NeniaMidiMetadata &metadata, qint64 positionMs)
{
    const qint64 position = std::max<qint64>(positionMs, 0);
    const auto next = std::upper_bound(
        metadata.timedTempos.begin(), metadata.timedTempos.end(), position,
        [](qint64 time, const NeniaMidiTimedTempo &tempo) {
            return time < tempo.positionMs;
        });
    const std::uint32_t currentTempo = next == metadata.timedTempos.begin()
        ? metadata.initialTempoUsPerQuarter : std::prev(next)->microsecondsPerQuarter;
    QString result = bpmText(metadata.initialTempoUsPerQuarter)
        + (metadata.hasExplicitInitialTempo ? QStringLiteral(" initial")
                                            : QStringLiteral(" default"));
    if (metadata.tempoChangeCount > 0) {
        result += QStringLiteral(" · %1 changes · %2 current")
            .arg(metadata.tempoChangeCount).arg(bpmText(currentTempo));
    }
    return result;
}

QString midiTimeSignatureAtPosition(const NeniaMidiMetadata &metadata, qint64 positionMs)
{
    const qint64 position = std::max<qint64>(positionMs, 0);
    const auto next = std::upper_bound(
        metadata.timedTimeSignatures.begin(), metadata.timedTimeSignatures.end(), position,
        [](qint64 time, const NeniaMidiTimedTimeSignature &signature) {
            return time < signature.positionMs;
        });
    if (next == metadata.timedTimeSignatures.begin()) {
        return metadata.hasTimeSignatureEvents
            ? QStringLiteral("4/4 (MIDI default; explicit event pending)")
            : QStringLiteral("4/4 (MIDI default)");
    }
    const NeniaMidiTimedTimeSignature &current = *std::prev(next);
    if (!current.valid) {
        return QStringLiteral("Unknown (invalid time-signature metadata)");
    }
    return next - metadata.timedTimeSignatures.begin() > 1
        ? QStringLiteral("%1 current").arg(current.value)
        : QStringLiteral("%1 explicit").arg(current.value);
}

} // namespace nenia::app
