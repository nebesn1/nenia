#include <nenia/MidiTimelineProvider.h>

#include "NeniaMidiSequence.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace nenia {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool projectLegacyTimeline(const detail::NeniaMidiSequence &sequence,
                           std::vector<TimelineEvent> *events,
                           std::vector<int> *ignoredMidiChannels,
                           QString *errorMessage)
{
    if (events == nullptr || ignoredMidiChannels == nullptr) {
        setError(errorMessage, QStringLiteral("timeline projection output is null"));
        return false;
    }

    std::vector<TimelineEvent> projectedEvents;
    projectedEvents.reserve(sequence.events().size());
    std::array<bool, 16> ignoredChannels{};

    for (const detail::NeniaMidiEvent &event : sequence.events()) {
        if (event.category() != detail::NeniaMidiEventCategory::ChannelVoice) {
            continue;
        }

        int midiChannel = -1;
        std::optional<TimelineEventPayload> payload;
        std::visit([&](const auto &value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, detail::NeniaMidiNoteOnEvent>) {
                midiChannel = value.channel;
                if (value.velocity == 0) {
                    payload = NoteOffEvent{value.channel, value.key};
                } else {
                    payload = NoteOnEvent{value.channel,
                                          value.key,
                                          static_cast<float>(value.velocity) / 127.0f};
                }
            } else if constexpr (std::is_same_v<Event, detail::NeniaMidiNoteOffEvent>) {
                midiChannel = value.channel;
                payload = NoteOffEvent{value.channel, value.key};
            } else if constexpr (std::is_same_v<Event, detail::NeniaMidiProgramChangeEvent>) {
                midiChannel = value.channel;
                payload = ProgramChangeEvent{value.channel, value.program};
            } else if constexpr (std::is_same_v<Event, detail::NeniaMidiControlChangeEvent>) {
                if (value.controller == 10) {
                    midiChannel = value.channel;
                    payload = PanChangeEvent{value.channel, value.value};
                }
            }
        }, event.payload);

        if (midiChannel < 0) {
            continue;
        }
        if (midiChannel >= 10) {
            ignoredChannels.at(static_cast<std::size_t>(midiChannel)) = true;
            continue;
        }
        if (!payload.has_value()) {
            continue;
        }

        qint64 timeMs = 0;
        if (!sequence.tickToMilliseconds(event.tick, &timeMs, errorMessage)) {
            return false;
        }
        projectedEvents.push_back({timeMs, std::move(payload.value())});
    }

    std::vector<int> normalizedIgnoredChannels;
    normalizedIgnoredChannels.reserve(6);
    for (int channel = 10; channel < 16; ++channel) {
        if (ignoredChannels.at(static_cast<std::size_t>(channel))) {
            normalizedIgnoredChannels.push_back(channel);
        }
    }
    *events = std::move(projectedEvents);
    *ignoredMidiChannels = std::move(normalizedIgnoredChannels);
    return true;
}

} // namespace

bool MidiTimelineProvider::load(const QString &path, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage,
                 QStringLiteral("cannot open MIDI file '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 0
        || static_cast<std::uint64_t>(fileSize)
               > detail::kNeniaMidiMaximumFileBytes) {
        setError(errorMessage,
                 QStringLiteral("MIDI file exceeds the %1-byte safety limit")
                     .arg(detail::kNeniaMidiMaximumFileBytes));
        return false;
    }
    const QByteArray midi = file.read(static_cast<qint64>(
        detail::kNeniaMidiMaximumFileBytes + 1U));
    if (file.error() != QFile::NoError) {
        setError(errorMessage,
                 QStringLiteral("cannot read MIDI file '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }

    QString parseError;
    if (!loadParsedMidi(midi, &parseError)) {
        setError(errorMessage,
                 QStringLiteral("MIDI file '%1': %2").arg(path, parseError));
        return false;
    }
    return true;
}

bool MidiTimelineProvider::loadMidi(const QByteArray &midi, QString *errorMessage)
{
    return loadParsedMidi(midi, errorMessage);
}

const std::vector<int> &MidiTimelineProvider::ignoredMidiChannels() const noexcept
{
    return m_ignoredMidiChannels;
}

bool MidiTimelineProvider::isLoaded() const noexcept
{
    return m_loaded;
}

std::size_t MidiTimelineProvider::eventCount() const noexcept
{
    return m_events.size();
}

std::optional<qint64> MidiTimelineProvider::firstEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.front().timeMs;
}

std::optional<qint64> MidiTimelineProvider::lastEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.back().timeMs;
}

std::span<const TimelineEvent> MidiTimelineProvider::eventsBetween(
    qint64 fromExclusiveMs,
    qint64 toInclusiveMs) const noexcept
{
    if (m_events.empty() || fromExclusiveMs >= toInclusiveMs) {
        return {};
    }

    const auto first = std::upper_bound(
        m_events.begin(), m_events.end(), fromExclusiveMs,
        [](qint64 time, const TimelineEvent &event) { return time < event.timeMs; });
    const auto last = std::upper_bound(
        m_events.begin(), m_events.end(), toInclusiveMs,
        [](qint64 time, const TimelineEvent &event) { return time < event.timeMs; });
    if (first >= last) {
        return {};
    }
    return std::span<const TimelineEvent>(&*first,
                                          static_cast<std::size_t>(last - first));
}

void MidiTimelineProvider::clear() noexcept
{
    m_events.clear();
    m_ignoredMidiChannels.clear();
    m_loaded = false;
}

bool MidiTimelineProvider::loadParsedMidi(const QByteArray &midi,
                                          QString *errorMessage)
{
    auto sequence = std::make_shared<detail::NeniaMidiSequence>();
    QString parseError;
    if (!sequence->loadMidi(midi, &parseError)) {
        setError(errorMessage, parseError);
        return false;
    }

    return loadSequence(std::move(sequence), errorMessage);
}

bool MidiTimelineProvider::loadSequence(
    std::shared_ptr<const detail::NeniaMidiSequence> sequence,
    QString *errorMessage)
{
    if (sequence == nullptr || !sequence->isLoaded()) {
        setError(errorMessage, QStringLiteral("MIDI sequence is not loaded"));
        return false;
    }
    std::vector<TimelineEvent> parsedEvents;
    std::vector<int> parsedIgnoredChannels;
    QString parseError;
    if (!projectLegacyTimeline(*sequence, &parsedEvents, &parsedIgnoredChannels,
                               &parseError)) {
        setError(errorMessage, parseError);
        return false;
    }

    m_events = std::move(parsedEvents);
    m_ignoredMidiChannels = std::move(parsedIgnoredChannels);
    m_loaded = true;
    setError(errorMessage, {});
    return true;
}

} // namespace nenia
