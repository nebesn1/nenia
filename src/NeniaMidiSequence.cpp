#include "NeniaMidiSequence.h"

#include "NeniaMidiChannelState.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace nenia::detail {
namespace {

using Byte = std::uint8_t;
using Tick = NeniaMidiTick;

constexpr std::uint32_t kMaximumVlq = 0x0fffffffU;

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString eventContext(std::size_t trackIndex,
                     std::size_t eventIndex,
                     qsizetype byteOffset)
{
    return QStringLiteral("track %1 event %2 at byte offset %3")
        .arg(trackIndex)
        .arg(eventIndex)
        .arg(byteOffset);
}

class ByteReader final {
public:
    ByteReader(const QByteArray &data, qsizetype begin = 0, qsizetype end = -1)
        : m_data(data)
        , m_offset(begin)
        , m_end(end < 0 ? data.size() : end)
    {
    }

    [[nodiscard]] qsizetype offset() const noexcept { return m_offset; }
    [[nodiscard]] qsizetype remaining() const noexcept { return m_end - m_offset; }

    bool readByte(Byte *value)
    {
        if (value == nullptr || m_offset >= m_end) {
            return false;
        }
        *value = static_cast<Byte>(static_cast<unsigned char>(m_data.at(m_offset)));
        ++m_offset;
        return true;
    }

    bool readUInt16BE(std::uint16_t *value)
    {
        Byte high = 0;
        Byte low = 0;
        if (value == nullptr || !readByte(&high) || !readByte(&low)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(high) << 8U) | low);
        return true;
    }

    bool readUInt32BE(std::uint32_t *value)
    {
        Byte b0 = 0;
        Byte b1 = 0;
        Byte b2 = 0;
        Byte b3 = 0;
        if (value == nullptr || !readByte(&b0) || !readByte(&b1)
            || !readByte(&b2) || !readByte(&b3)) {
            return false;
        }
        *value = (static_cast<std::uint32_t>(b0) << 24U)
            | (static_cast<std::uint32_t>(b1) << 16U)
            | (static_cast<std::uint32_t>(b2) << 8U)
            | static_cast<std::uint32_t>(b3);
        return true;
    }

    bool readTag(const char *tag)
    {
        for (int i = 0; i < 4; ++i) {
            Byte value = 0;
            if (!readByte(&value) || value != static_cast<Byte>(tag[i])) {
                return false;
            }
        }
        return true;
    }

    bool readPayload(std::uint32_t length, QByteArray *payload)
    {
        if (payload == nullptr || static_cast<std::uint64_t>(length)
                                      > static_cast<std::uint64_t>(remaining())) {
            return false;
        }
        *payload = m_data.sliced(m_offset, static_cast<qsizetype>(length));
        m_offset += static_cast<qsizetype>(length);
        return true;
    }

    bool skip(std::uint32_t length)
    {
        if (static_cast<std::uint64_t>(length) > static_cast<std::uint64_t>(remaining())) {
            return false;
        }
        m_offset += static_cast<qsizetype>(length);
        return true;
    }

private:
    const QByteArray &m_data;
    qsizetype m_offset = 0;
    qsizetype m_end = 0;
};

bool readVlq(ByteReader &reader,
             std::uint32_t *value,
             const QString &context,
             QString *errorMessage)
{
    std::uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        const qsizetype byteOffset = reader.offset();
        Byte byte = 0;
        if (!reader.readByte(&byte)) {
            setError(errorMessage,
                     QStringLiteral("%1: truncated variable-length quantity at byte offset %2")
                         .arg(context)
                         .arg(byteOffset));
            return false;
        }
        if (result > (kMaximumVlq >> 7U)) {
            setError(errorMessage,
                     QStringLiteral("%1: variable-length quantity overflow at byte offset %2")
                         .arg(context)
                         .arg(byteOffset));
            return false;
        }
        result = static_cast<std::uint32_t>((result << 7U) | (byte & 0x7fU));
        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }
    }
    setError(errorMessage,
             QStringLiteral("%1: malformed variable-length quantity at byte offset %2")
                 .arg(context)
                 .arg(reader.offset()));
    return false;
}

bool readDataByte(ByteReader &reader,
                  Byte *value,
                  const QString &context,
                  QString *errorMessage)
{
    const qsizetype byteOffset = reader.offset();
    if (!reader.readByte(value)) {
        setError(errorMessage,
                 QStringLiteral("%1: truncated MIDI data byte at byte offset %2")
                     .arg(context)
                     .arg(byteOffset));
        return false;
    }
    if ((*value & 0x80U) != 0) {
        setError(errorMessage,
                 QStringLiteral("%1: expected MIDI data byte at byte offset %2")
                     .arg(context)
                     .arg(byteOffset));
        return false;
    }
    return true;
}

bool appendChannelEvent(Byte status,
                        Byte first,
                        Byte second,
                        std::size_t trackIndex,
                        std::size_t eventIndex,
                        Tick tick,
                        std::vector<NeniaMidiEvent> *events)
{
    const Byte message = status & 0xf0U;
    const Byte channel = status & 0x0fU;
    NeniaMidiEventPayload payload;
    switch (message) {
    case 0x80U:
        payload = NeniaMidiNoteOffEvent{channel, first, second};
        break;
    case 0x90U:
        payload = NeniaMidiNoteOnEvent{channel, first, second};
        break;
    case 0xa0U:
        payload = NeniaMidiPolyPressureEvent{channel, first, second};
        break;
    case 0xb0U:
        payload = NeniaMidiControlChangeEvent{channel, first, second};
        break;
    case 0xc0U:
        payload = NeniaMidiProgramChangeEvent{channel, first};
        break;
    case 0xd0U:
        payload = NeniaMidiChannelPressureEvent{channel, first};
        break;
    case 0xe0U: {
        const auto value14 = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(first)
            | (static_cast<std::uint16_t>(second) << 7U));
        payload = NeniaMidiPitchBendEvent{channel, first, second, value14};
        break;
    }
    default:
        return false;
    }
    events->push_back({tick, trackIndex, eventIndex, std::move(payload)});
    return true;
}

bool parseTrack(const QByteArray &midi,
                qsizetype trackBegin,
                qsizetype trackEnd,
                std::size_t trackIndex,
                std::vector<NeniaMidiEvent> *events,
                NeniaMidiTrackInfo *trackInfo,
                QString *errorMessage)
{
    ByteReader reader(midi, trackBegin, trackEnd);
    Tick absoluteTick = 0;
    Byte runningStatus = 0;
    std::size_t eventIndex = 0;
    bool foundEndOfTrack = false;

    while (reader.remaining() > 0) {
        const qsizetype eventOffset = reader.offset();
        const QString context = eventContext(trackIndex, eventIndex, eventOffset);
        std::uint32_t deltaTicks = 0;
        if (!readVlq(reader, &deltaTicks, context + QStringLiteral(" delta time"),
                     errorMessage)) {
            return false;
        }
        if (static_cast<Tick>(deltaTicks) > std::numeric_limits<Tick>::max() - absoluteTick) {
            setError(errorMessage, QStringLiteral("%1: absolute tick overflow").arg(context));
            return false;
        }
        absoluteTick += static_cast<Tick>(deltaTicks);

        Byte first = 0;
        if (!reader.readByte(&first)) {
            setError(errorMessage, QStringLiteral("%1: missing MIDI event status").arg(context));
            return false;
        }

        if (first >= 0x80U && first <= 0xefU) {
            runningStatus = first;
            const Byte message = first & 0xf0U;
            const int dataLength = (message == 0xc0U || message == 0xd0U) ? 1 : 2;
            Byte firstData = 0;
            Byte secondData = 0;
            if (!readDataByte(reader, &firstData, context, errorMessage)) {
                return false;
            }
            if (dataLength == 2
                && !readDataByte(reader, &secondData, context, errorMessage)) {
                return false;
            }
            if (!appendChannelEvent(first, firstData, secondData, trackIndex, eventIndex,
                                    absoluteTick, events)) {
                setError(errorMessage, QStringLiteral("%1: unsupported MIDI channel event")
                                           .arg(context));
                return false;
            }
        } else if (first < 0x80U) {
            if (runningStatus < 0x80U || runningStatus > 0xefU) {
                setError(errorMessage,
                         QStringLiteral("%1: running status used without prior channel status")
                             .arg(context));
                return false;
            }
            const Byte message = runningStatus & 0xf0U;
            const int dataLength = (message == 0xc0U || message == 0xd0U) ? 1 : 2;
            Byte secondData = 0;
            if (dataLength == 2
                && !readDataByte(reader, &secondData, context, errorMessage)) {
                return false;
            }
            if (!appendChannelEvent(runningStatus, first, secondData, trackIndex, eventIndex,
                                    absoluteTick, events)) {
                setError(errorMessage, QStringLiteral("%1: unsupported MIDI channel event")
                                           .arg(context));
                return false;
            }
        } else if (first == 0xf0U || first == 0xf7U) {
            runningStatus = 0;
            std::uint32_t payloadLength = 0;
            if (!readVlq(reader, &payloadLength, context + QStringLiteral(" SysEx length"),
                         errorMessage)) {
                return false;
            }
            QByteArray payload;
            if (!reader.readPayload(payloadLength, &payload)) {
                setError(errorMessage, QStringLiteral("%1: truncated SysEx payload").arg(context));
                return false;
            }
            events->push_back({absoluteTick, trackIndex, eventIndex,
                               NeniaMidiSysExEvent{first, std::move(payload)}});
        } else if (first == 0xffU) {
            runningStatus = 0;
            Byte metaType = 0;
            if (!reader.readByte(&metaType)) {
                setError(errorMessage, QStringLiteral("%1: truncated MIDI meta-event type")
                                           .arg(context));
                return false;
            }
            std::uint32_t payloadLength = 0;
            if (!readVlq(reader, &payloadLength, context + QStringLiteral(" meta-event length"),
                         errorMessage)) {
                return false;
            }
            if (metaType == 0x2fU && payloadLength != 0) {
                setError(errorMessage,
                         QStringLiteral("%1: End of Track length must be 0").arg(context));
                return false;
            }
            QByteArray payload;
            if (!reader.readPayload(payloadLength, &payload)) {
                setError(errorMessage,
                         QStringLiteral("%1: truncated meta-event payload").arg(context));
                return false;
            }
            if (metaType == 0x51U) {
                if (payload.size() != 3) {
                    setError(errorMessage,
                             QStringLiteral("%1: Set Tempo length must be 3").arg(context));
                    return false;
                }
                const auto tempo =
                    (static_cast<std::uint32_t>(static_cast<unsigned char>(payload.at(0))) << 16U)
                    | (static_cast<std::uint32_t>(static_cast<unsigned char>(payload.at(1))) << 8U)
                    | static_cast<std::uint32_t>(static_cast<unsigned char>(payload.at(2)));
                if (tempo == 0) {
                    setError(errorMessage,
                             QStringLiteral("%1: Set Tempo value must be nonzero").arg(context));
                    return false;
                }
            }

            events->push_back({absoluteTick, trackIndex, eventIndex,
                               NeniaMidiMetaEvent{metaType, std::move(payload)}});
            if (metaType == 0x2fU) {
                if (reader.remaining() != 0) {
                    setError(errorMessage,
                             QStringLiteral("%1: bytes remain after End of Track").arg(context));
                    return false;
                }
                foundEndOfTrack = true;
                trackInfo->endTick = absoluteTick;
                ++eventIndex;
                break;
            }
        } else {
            runningStatus = 0;
            std::uint8_t dataLength = 0;
            switch (first) {
            case 0xf1U:
            case 0xf3U:
                dataLength = 1;
                break;
            case 0xf2U:
                dataLength = 2;
                break;
            case 0xf6U:
                dataLength = 0;
                break;
            default:
                setError(errorMessage,
                         QStringLiteral("%1: unsupported MIDI system status 0x%2")
                             .arg(context)
                             .arg(first, 2, 16, QLatin1Char('0')));
                return false;
            }

            NeniaMidiSystemCommonEvent systemEvent;
            systemEvent.status = first;
            systemEvent.dataLength = dataLength;
            for (std::uint8_t index = 0; index < dataLength; ++index) {
                if (!readDataByte(reader, &systemEvent.data[index], context, errorMessage)) {
                    return false;
                }
            }
            events->push_back({absoluteTick, trackIndex, eventIndex, systemEvent});
        }

        ++eventIndex;
    }

    if (!foundEndOfTrack) {
        setError(errorMessage,
                 QStringLiteral("track %1: missing End of Track meta-event").arg(trackIndex));
        return false;
    }
    return true;
}

struct ExactTime {
    std::uint64_t wholeMicroseconds = 0;
    std::uint32_t fractionalTickNumerator = 0;
};

bool advanceExactTime(ExactTime *time,
                      Tick deltaTicks,
                      std::uint32_t tempo,
                      std::uint16_t ppqn,
                      QString *errorMessage)
{
    if (time == nullptr || ppqn == 0 || tempo == 0) {
        setError(errorMessage, QStringLiteral("invalid MIDI time conversion parameters"));
        return false;
    }

    const Tick wholeQuarters = deltaTicks / ppqn;
    const Tick remainingTicks = deltaTicks % ppqn;
    if (wholeQuarters > std::numeric_limits<std::uint64_t>::max() / tempo) {
        setError(errorMessage, QStringLiteral("MIDI tick-to-time conversion overflow"));
        return false;
    }
    std::uint64_t wholeDelta = wholeQuarters * tempo;
    const std::uint64_t fractionalNumerator =
        remainingTicks * static_cast<std::uint64_t>(tempo)
        + time->fractionalTickNumerator;
    const std::uint64_t fractionalWhole = fractionalNumerator / ppqn;
    if (fractionalWhole > std::numeric_limits<std::uint64_t>::max() - wholeDelta) {
        setError(errorMessage, QStringLiteral("MIDI tick-to-time conversion overflow"));
        return false;
    }
    wholeDelta += fractionalWhole;
    if (wholeDelta > std::numeric_limits<std::uint64_t>::max() - time->wholeMicroseconds) {
        setError(errorMessage, QStringLiteral("MIDI microsecond accumulator overflow"));
        return false;
    }
    time->wholeMicroseconds += wholeDelta;
    time->fractionalTickNumerator = static_cast<std::uint32_t>(fractionalNumerator % ppqn);
    return true;
}

bool multiplyDivideRounded(std::uint64_t value,
                           std::uint32_t multiplier,
                           std::uint64_t denominator,
                           std::uint64_t *result)
{
    if (denominator == 0 || value >= denominator || result == nullptr) {
        return false;
    }

    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
    for (int bit = 31; bit >= 0; --bit) {
        quotient *= 2U;
        remainder *= 2U;
        if (remainder >= denominator) {
            remainder -= denominator;
            ++quotient;
        }
        if (((multiplier >> bit) & 1U) != 0U) {
            remainder += value;
            if (remainder >= denominator) {
                remainder -= denominator;
                ++quotient;
            }
        }
    }
    if (remainder * 2U >= denominator) {
        ++quotient;
    }
    *result = quotient;
    return true;
}

} // namespace

bool NeniaMidiSequence::load(const QString &path, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage,
                 QStringLiteral("cannot open MIDI file '%1': %2").arg(path, file.errorString()));
        return false;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kNeniaMidiMaximumFileBytes) {
        setError(errorMessage,
                 QStringLiteral("MIDI file exceeds the %1-byte safety limit")
                     .arg(kNeniaMidiMaximumFileBytes));
        return false;
    }
    QByteArray midi = file.read(static_cast<qint64>(kNeniaMidiMaximumFileBytes + 1U));
    if (file.error() != QFile::NoError) {
        setError(errorMessage,
                 QStringLiteral("cannot read MIDI file '%1': %2").arg(path, file.errorString()));
        return false;
    }
    if (static_cast<std::uint64_t>(midi.size()) > kNeniaMidiMaximumFileBytes) {
        setError(errorMessage,
                 QStringLiteral("MIDI file exceeds the %1-byte safety limit")
                     .arg(kNeniaMidiMaximumFileBytes));
        return false;
    }
    return loadMidi(midi, errorMessage);
}

bool NeniaMidiSequence::loadMidi(const QByteArray &midi, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    NeniaMidiSequence parsed;
    if (!parsed.parse(midi, errorMessage)) {
        return false;
    }
    swap(parsed);
    return true;
}

void NeniaMidiSequence::clear() noexcept
{
    NeniaMidiSequence empty;
    swap(empty);
}

bool NeniaMidiSequence::isLoaded() const noexcept { return m_loaded; }
std::uint16_t NeniaMidiSequence::format() const noexcept { return m_format; }
std::uint16_t NeniaMidiSequence::ticksPerQuarterNote() const noexcept
{
    return m_ticksPerQuarterNote;
}
std::size_t NeniaMidiSequence::originalTrackCount() const noexcept { return m_tracks.size(); }
const std::vector<NeniaMidiTrackInfo> &NeniaMidiSequence::tracks() const noexcept
{
    return m_tracks;
}
const std::vector<NeniaMidiEvent> &NeniaMidiSequence::events() const noexcept
{
    return m_events;
}
const std::vector<NeniaMidiTempoEvent> &NeniaMidiSequence::tempoEvents() const noexcept
{
    return m_tempoEvents;
}
NeniaMidiTick NeniaMidiSequence::durationTick() const noexcept { return m_durationTick; }
std::optional<NeniaMidiTick> NeniaMidiSequence::lastChannelEventTick() const noexcept
{
    return m_lastChannelEventTick;
}

bool NeniaMidiSequence::tickToMicroseconds(NeniaMidiTick tick,
                                            std::uint64_t *microseconds,
                                            QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (microseconds == nullptr) {
        setError(errorMessage, QStringLiteral("microsecond output pointer is null"));
        return false;
    }
    std::uint64_t whole = 0;
    std::uint32_t fraction = 0;
    if (!exactTimeAtTick(tick, &whole, &fraction, errorMessage)) {
        return false;
    }
    const bool roundUp = static_cast<std::uint64_t>(fraction) * 2U
                         >= m_ticksPerQuarterNote;
    if (roundUp && whole == std::numeric_limits<std::uint64_t>::max()) {
        setError(errorMessage, QStringLiteral("MIDI microsecond timestamp overflow"));
        return false;
    }
    *microseconds = whole + (roundUp ? 1U : 0U);
    return true;
}

bool NeniaMidiSequence::tickToMilliseconds(NeniaMidiTick tick,
                                            qint64 *milliseconds,
                                            QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (milliseconds == nullptr) {
        setError(errorMessage, QStringLiteral("millisecond output pointer is null"));
        return false;
    }
    std::uint64_t wholeMicroseconds = 0;
    std::uint32_t fraction = 0;
    if (!exactTimeAtTick(tick, &wholeMicroseconds, &fraction, errorMessage)) {
        return false;
    }
    const std::uint64_t wholeMilliseconds = wholeMicroseconds / 1000U;
    const std::uint64_t remainderNumerator =
        (wholeMicroseconds % 1000U) * m_ticksPerQuarterNote + fraction;
    const std::uint64_t denominator = 1000U * m_ticksPerQuarterNote;
    std::uint64_t roundedRemainder = remainderNumerator / denominator;
    const std::uint64_t remainder = remainderNumerator % denominator;
    if (remainder * 2U >= denominator) {
        ++roundedRemainder;
    }
    if (wholeMilliseconds > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
        || roundedRemainder > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
                                  - wholeMilliseconds) {
        setError(errorMessage, QStringLiteral("MIDI millisecond timestamp overflow"));
        return false;
    }
    *milliseconds = static_cast<qint64>(wholeMilliseconds + roundedRemainder);
    return true;
}

bool NeniaMidiSequence::tickToSampleFrame(NeniaMidiTick tick,
                                           std::uint32_t sampleRate,
                                           std::uint64_t *sampleFrame,
                                           QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (sampleFrame == nullptr || sampleRate == 0) {
        setError(errorMessage, QStringLiteral("sample rate and output pointer must be valid"));
        return false;
    }
    std::uint64_t wholeMicroseconds = 0;
    std::uint32_t fraction = 0;
    if (!exactTimeAtTick(tick, &wholeMicroseconds, &fraction, errorMessage)) {
        return false;
    }

    const std::uint64_t wholeSeconds = wholeMicroseconds / 1'000'000U;
    if (wholeSeconds > std::numeric_limits<std::uint64_t>::max() / sampleRate) {
        setError(errorMessage, QStringLiteral("MIDI sample-frame timestamp overflow"));
        return false;
    }
    const std::uint64_t wholeFrames = wholeSeconds * sampleRate;
    const std::uint64_t fractionalTimeNumerator =
        (wholeMicroseconds % 1'000'000U) * m_ticksPerQuarterNote + fraction;
    const std::uint64_t fractionalDenominator =
        1'000'000U * m_ticksPerQuarterNote;
    std::uint64_t fractionalFrames = 0;
    if (!multiplyDivideRounded(fractionalTimeNumerator, sampleRate,
                               fractionalDenominator, &fractionalFrames)
        || fractionalFrames > std::numeric_limits<std::uint64_t>::max() - wholeFrames) {
        setError(errorMessage, QStringLiteral("MIDI sample-frame timestamp overflow"));
        return false;
    }
    *sampleFrame = wholeFrames + fractionalFrames;
    return true;
}

bool NeniaMidiSequence::stateAtTick(NeniaMidiTick tick,
                                    NeniaMidiChannelState *state,
                                    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!m_loaded || state == nullptr) {
        setError(errorMessage, state == nullptr
                                   ? QStringLiteral("channel-state output pointer is null")
                                   : QStringLiteral("MIDI sequence is not loaded"));
        return false;
    }

    *state = NeniaMidiChannelState{};
    const auto end = std::upper_bound(
        m_events.begin(), m_events.end(), tick,
        [](Tick position, const NeniaMidiEvent &event) { return position < event.tick; });
    for (auto event = m_events.begin(); event != end; ++event) {
        state->apply(*event);
    }
    return true;
}

bool NeniaMidiSequence::parse(const QByteArray &midi, QString *errorMessage)
{
    if (static_cast<std::uint64_t>(midi.size()) > kNeniaMidiMaximumFileBytes) {
        setError(errorMessage,
                 QStringLiteral("MIDI data exceeds the %1-byte safety limit")
                     .arg(kNeniaMidiMaximumFileBytes));
        return false;
    }

    ByteReader reader(midi);
    if (reader.remaining() < 8 || !reader.readTag("MThd")) {
        setError(errorMessage, QStringLiteral("invalid MThd signature"));
        return false;
    }
    std::uint32_t headerLength = 0;
    if (!reader.readUInt32BE(&headerLength)) {
        setError(errorMessage, QStringLiteral("truncated MThd header length"));
        return false;
    }
    if (headerLength < 6) {
        setError(errorMessage, QStringLiteral("MThd chunk length must be at least 6"));
        return false;
    }
    if (static_cast<std::uint64_t>(headerLength)
        > static_cast<std::uint64_t>(reader.remaining())) {
        setError(errorMessage, QStringLiteral("truncated MThd header chunk"));
        return false;
    }
    const qsizetype headerEnd = reader.offset() + static_cast<qsizetype>(headerLength);
    std::uint16_t format = 0;
    std::uint16_t trackCount = 0;
    std::uint16_t division = 0;
    if (!reader.readUInt16BE(&format) || !reader.readUInt16BE(&trackCount)
        || !reader.readUInt16BE(&division)) {
        setError(errorMessage, QStringLiteral("truncated MThd header data"));
        return false;
    }
    if (format == 2) {
        setError(errorMessage, QStringLiteral("Unsupported Standard MIDI File format 2"));
        return false;
    }
    if (format > 1) {
        setError(errorMessage,
                 QStringLiteral("Unsupported Standard MIDI File format %1").arg(format));
        return false;
    }
    if (format == 0 && trackCount != 1) {
        setError(errorMessage,
                 QStringLiteral("Standard MIDI File format 0 must contain exactly one track"));
        return false;
    }
    if (trackCount == 0) {
        setError(errorMessage, QStringLiteral("Standard MIDI File must contain at least one track"));
        return false;
    }
    if ((division & 0x8000U) != 0) {
        setError(errorMessage, QStringLiteral("SMPTE time division is not supported"));
        return false;
    }
    const std::uint16_t ppqn = division & 0x7fffU;
    if (ppqn == 0) {
        setError(errorMessage, QStringLiteral("MIDI PPQN division must be greater than zero"));
        return false;
    }
    if (reader.offset() < headerEnd
        && !reader.skip(static_cast<std::uint32_t>(headerEnd - reader.offset()))) {
        setError(errorMessage, QStringLiteral("truncated MThd header extension"));
        return false;
    }

    std::vector<NeniaMidiEvent> events;
    std::vector<NeniaMidiTrackInfo> tracks;
    tracks.reserve(trackCount);
    for (std::uint16_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
        const qsizetype chunkOffset = reader.offset();
        if (reader.remaining() < 8 || !reader.readTag("MTrk")) {
            setError(errorMessage,
                     QStringLiteral("track %1 at byte offset %2: invalid MTrk signature")
                         .arg(trackIndex)
                         .arg(chunkOffset));
            return false;
        }
        std::uint32_t trackLength = 0;
        if (!reader.readUInt32BE(&trackLength)) {
            setError(errorMessage,
                     QStringLiteral("track %1: truncated MTrk length").arg(trackIndex));
            return false;
        }
        if (static_cast<std::uint64_t>(trackLength)
            > static_cast<std::uint64_t>(reader.remaining())) {
            setError(errorMessage,
                     QStringLiteral("track %1: declared length exceeds file boundary")
                         .arg(trackIndex));
            return false;
        }
        const qsizetype trackBegin = reader.offset();
        const qsizetype trackEnd = trackBegin + static_cast<qsizetype>(trackLength);
        NeniaMidiTrackInfo trackInfo{trackIndex, 0};
        if (!parseTrack(midi, trackBegin, trackEnd, trackIndex,
                        &events, &trackInfo, errorMessage)) {
            return false;
        }
        if (!reader.skip(trackLength)) {
            setError(errorMessage,
                     QStringLiteral("track %1: truncated track chunk").arg(trackIndex));
            return false;
        }
        tracks.push_back(trackInfo);
    }
    if (reader.remaining() != 0) {
        setError(errorMessage,
                 QStringLiteral("unexpected trailing data after declared MIDI tracks"));
        return false;
    }

    std::stable_sort(events.begin(), events.end(), [](const NeniaMidiEvent &left,
                                                       const NeniaMidiEvent &right) {
        if (left.tick != right.tick) {
            return left.tick < right.tick;
        }
        if (left.sourceTrackIndex != right.sourceTrackIndex) {
            return left.sourceTrackIndex < right.sourceTrackIndex;
        }
        return left.sourceEventIndex < right.sourceEventIndex;
    });

    m_format = format;
    m_ticksPerQuarterNote = ppqn;
    m_tracks = std::move(tracks);
    m_events = std::move(events);
    m_durationTick = 0;
    for (const NeniaMidiTrackInfo &track : m_tracks) {
        m_durationTick = std::max(m_durationTick, track.endTick);
    }
    m_lastChannelEventTick.reset();
    m_tempoEvents.clear();
    for (const NeniaMidiEvent &event : m_events) {
        if (event.category() == NeniaMidiEventCategory::ChannelVoice) {
            m_lastChannelEventTick = event.tick;
        }
        const auto *meta = std::get_if<NeniaMidiMetaEvent>(&event.payload);
        if (meta != nullptr && meta->type == 0x51U) {
            const auto tempo =
                (static_cast<std::uint32_t>(static_cast<unsigned char>(meta->data.at(0))) << 16U)
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(meta->data.at(1))) << 8U)
                | static_cast<std::uint32_t>(static_cast<unsigned char>(meta->data.at(2)));
            m_tempoEvents.push_back({event.tick, event.sourceTrackIndex,
                                     event.sourceEventIndex, tempo});
        }
    }
    if (!buildTempoMap(errorMessage)) {
        return false;
    }
    m_loaded = true;
    return true;
}

bool NeniaMidiSequence::buildTempoMap(QString *errorMessage)
{
    m_tempoSegments.clear();
    m_tempoSegments.push_back({0, kNeniaMidiDefaultTempoUsPerQuarter, 0, 0});
    Tick currentTick = 0;
    std::uint32_t activeTempo = kNeniaMidiDefaultTempoUsPerQuarter;
    ExactTime elapsed;

    for (const NeniaMidiTempoEvent &tempo : m_tempoEvents) {
        if (tempo.tick < currentTick) {
            setError(errorMessage, QStringLiteral("MIDI tempo ticks are not monotonic"));
            return false;
        }
        if (tempo.tick > currentTick
            && !advanceExactTime(&elapsed, tempo.tick - currentTick, activeTempo,
                                 m_ticksPerQuarterNote, errorMessage)) {
            return false;
        }
        currentTick = tempo.tick;
        activeTempo = tempo.microsecondsPerQuarter;
        if (m_tempoSegments.back().startTick == tempo.tick) {
            m_tempoSegments.back().microsecondsPerQuarter = activeTempo;
        } else {
            m_tempoSegments.push_back({tempo.tick, activeTempo,
                                       elapsed.wholeMicroseconds,
                                       elapsed.fractionalTickNumerator});
        }
    }
    return true;
}

bool NeniaMidiSequence::exactTimeAtTick(NeniaMidiTick tick,
                                         std::uint64_t *wholeMicroseconds,
                                         std::uint32_t *fractionalTickNumerator,
                                         QString *errorMessage) const
{
    if (!m_loaded) {
        setError(errorMessage, QStringLiteral("MIDI sequence is not loaded"));
        return false;
    }
    if (m_ticksPerQuarterNote == 0 || m_tempoSegments.empty()) {
        setError(errorMessage, QStringLiteral("MIDI sequence has no valid tempo map"));
        return false;
    }
    const auto upper = std::upper_bound(
        m_tempoSegments.begin(), m_tempoSegments.end(), tick,
        [](Tick position, const TempoSegment &segment) { return position < segment.startTick; });
    const TempoSegment &segment = *(upper - 1);
    ExactTime time{segment.wholeMicroseconds, segment.fractionalTickNumerator};
    if (!advanceExactTime(&time, tick - segment.startTick,
                          segment.microsecondsPerQuarter,
                          m_ticksPerQuarterNote, errorMessage)) {
        return false;
    }
    *wholeMicroseconds = time.wholeMicroseconds;
    *fractionalTickNumerator = time.fractionalTickNumerator;
    return true;
}

void NeniaMidiSequence::swap(NeniaMidiSequence &other) noexcept
{
    using std::swap;
    swap(m_loaded, other.m_loaded);
    swap(m_format, other.m_format);
    swap(m_ticksPerQuarterNote, other.m_ticksPerQuarterNote);
    swap(m_durationTick, other.m_durationTick);
    swap(m_lastChannelEventTick, other.m_lastChannelEventTick);
    swap(m_tracks, other.m_tracks);
    swap(m_events, other.m_events);
    swap(m_tempoEvents, other.m_tempoEvents);
    swap(m_tempoSegments, other.m_tempoSegments);
}

} // namespace nenia::detail
