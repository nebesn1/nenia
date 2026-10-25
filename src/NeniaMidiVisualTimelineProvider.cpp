#include "NeniaMidiVisualTimelineProvider.h"

#include "NeniaMidiChannelState.h"
#include "NeniaMidiSequence.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace nenia::detail {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

int eventChannel(const NeniaMidiEvent &event)
{
    return std::visit([](const auto &value) -> int {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, NeniaMidiNoteOnEvent>
                      || std::is_same_v<Event, NeniaMidiNoteOffEvent>
                      || std::is_same_v<Event, NeniaMidiPolyPressureEvent>
                      || std::is_same_v<Event, NeniaMidiControlChangeEvent>
                      || std::is_same_v<Event, NeniaMidiProgramChangeEvent>
                      || std::is_same_v<Event, NeniaMidiChannelPressureEvent>
                      || std::is_same_v<Event, NeniaMidiPitchBendEvent>) {
            return value.channel;
        }
        return -1;
    }, event.payload);
}

std::array<std::size_t, 128> activeCounts(const NeniaMidiSingleChannelState &state)
{
    std::array<std::size_t, 128> counts{};
    for (const NeniaMidiNoteInstance &note : state.activeNotes) {
        ++counts.at(note.key);
    }
    return counts;
}

} // namespace

bool NeniaMidiVisualTimelineProvider::load(const QString &path, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        clear();
        setError(errorMessage,
                 QStringLiteral("cannot open MIDI file '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kNeniaMidiMaximumFileBytes) {
        clear();
        setError(errorMessage,
                 QStringLiteral("MIDI file exceeds the %1-byte safety limit")
                     .arg(kNeniaMidiMaximumFileBytes));
        return false;
    }
    const QByteArray bytes = file.read(static_cast<qint64>(kNeniaMidiMaximumFileBytes + 1U));
    if (file.error() != QFile::NoError) {
        clear();
        setError(errorMessage,
                 QStringLiteral("cannot read MIDI file '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }

    auto sequence = std::make_shared<NeniaMidiSequence>();
    QString parseError;
    if (!sequence->loadMidi(bytes, &parseError)) {
        clear();
        setError(errorMessage,
                 QStringLiteral("MIDI file '%1': %2").arg(path, parseError));
        return false;
    }
    return loadSequence(std::move(sequence), errorMessage);
}

bool NeniaMidiVisualTimelineProvider::loadSequence(
    std::shared_ptr<const NeniaMidiSequence> sequence,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (sequence == nullptr || !sequence->isLoaded()) {
        clear();
        setError(errorMessage, QStringLiteral("MIDI sequence is not loaded"));
        return false;
    }

    std::vector<TimelineEvent> projected;
    projected.reserve(sequence->events().size());
    std::vector<NeniaMidiVisualTimelineEvent> projectedVisual;
    projectedVisual.reserve(sequence->events().size());
    NeniaMidiChannelState channelState;

    for (const NeniaMidiEvent &sourceEvent : sequence->events()) {
        const int channel = eventChannel(sourceEvent);
        if (channel < 0) {
            continue;
        }
        NeniaMidiSingleChannelState &beforeState =
            channelState.channel(static_cast<std::size_t>(channel));
        const std::array<std::size_t, 128> beforeCounts = activeCounts(beforeState);

        std::optional<TimelineEventPayload> directPayload;
        std::optional<NeniaMidiVisualEventPayload> visualDirectPayload;
        std::visit([&](const auto &value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, NeniaMidiProgramChangeEvent>) {
                directPayload = ProgramChangeEvent{value.channel, value.program};
                visualDirectPayload = ProgramChangeEvent{value.channel, value.program};
            } else if constexpr (std::is_same_v<Event, NeniaMidiControlChangeEvent>) {
                if (value.controller == 10) {
                    directPayload = PanChangeEvent{value.channel, value.value};
                    visualDirectPayload = PanChangeEvent{value.channel, value.value};
                } else if (value.controller == 7) {
                    NeniaMidiControllerProjectionEvent projection;
                    projection.kind = NeniaMidiControllerProjectionKind::Volume;
                    projection.channel = value.channel;
                    projection.value = value.value;
                    visualDirectPayload = projection;
                } else if (value.controller == 11) {
                    NeniaMidiControllerProjectionEvent projection;
                    projection.kind = NeniaMidiControllerProjectionKind::Expression;
                    projection.channel = value.channel;
                    projection.value = value.value;
                    visualDirectPayload = projection;
                }
            }
        }, sourceEvent.payload);

        channelState.apply(sourceEvent);
        const NeniaMidiSingleChannelState &afterState =
            channelState.channel(static_cast<std::size_t>(channel));
        const std::array<std::size_t, 128> afterCounts = activeCounts(afterState);

        if (const auto *control =
                std::get_if<NeniaMidiControlChangeEvent>(&sourceEvent.payload);
            control != nullptr && control->controller == 121) {
            NeniaMidiControllerProjectionEvent projection;
            // CC121 affects Expression and other controller internals, but
            // leaves Volume and Pan untouched under the supported RP-015 policy.
            projection.kind = NeniaMidiControllerProjectionKind::Expression;
            projection.channel = channel;
            projection.value = afterState.effectiveExpression();
            visualDirectPayload = projection;
        }

        qint64 timeMs = 0;
        if (!sequence->tickToMilliseconds(sourceEvent.tick, &timeMs, errorMessage)) {
            clear();
            return false;
        }
        if (directPayload.has_value()) {
            projected.push_back({timeMs, directPayload.value()});
        }
        if (visualDirectPayload.has_value()) {
            projectedVisual.push_back({timeMs, std::move(visualDirectPayload.value())});
        }

        for (std::size_t key = 0; key < afterCounts.size(); ++key) {
            if (afterCounts[key] > beforeCounts[key]) {
                const auto *noteOn = std::get_if<NeniaMidiNoteOnEvent>(&sourceEvent.payload);
                const float velocity = noteOn != nullptr && noteOn->velocity > 0
                    ? static_cast<float>(noteOn->velocity) / 127.0f : 1.0f;
                for (std::size_t count = beforeCounts[key]; count < afterCounts[key]; ++count) {
                    const NoteOnEvent note{channel, static_cast<int>(key), velocity};
                    projected.push_back({timeMs, note});
                    projectedVisual.push_back({timeMs, note});
                }
            } else if (afterCounts[key] < beforeCounts[key]) {
                for (std::size_t count = afterCounts[key]; count < beforeCounts[key]; ++count) {
                    const NoteOffEvent note{channel, static_cast<int>(key)};
                    projected.push_back({timeMs, note});
                    projectedVisual.push_back({timeMs, note});
                }
            }
        }
    }

    m_events = std::move(projected);
    m_visualEvents = std::move(projectedVisual);
    m_loaded = true;
    return true;
}

bool NeniaMidiVisualTimelineProvider::isLoaded() const noexcept
{
    return m_loaded;
}

std::size_t NeniaMidiVisualTimelineProvider::eventCount() const noexcept
{
    return m_events.size();
}

std::optional<qint64> NeniaMidiVisualTimelineProvider::firstEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.front().timeMs;
}

std::optional<qint64> NeniaMidiVisualTimelineProvider::lastEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.back().timeMs;
}

std::span<const TimelineEvent> NeniaMidiVisualTimelineProvider::eventsBetween(
    qint64 fromExclusiveMs,
    qint64 toInclusiveMs) const noexcept
{
    if (!m_loaded || m_events.empty() || fromExclusiveMs >= toInclusiveMs) {
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

std::span<const NeniaMidiVisualTimelineEvent>
NeniaMidiVisualTimelineProvider::visualEventsBetween(
    qint64 fromExclusiveMs,
    qint64 toInclusiveMs) const noexcept
{
    if (!m_loaded || m_visualEvents.empty() || fromExclusiveMs >= toInclusiveMs) {
        return {};
    }
    const auto first = std::upper_bound(
        m_visualEvents.begin(), m_visualEvents.end(), fromExclusiveMs,
        [](qint64 time, const NeniaMidiVisualTimelineEvent &event) {
            return time < event.timeMs;
        });
    const auto last = std::upper_bound(
        m_visualEvents.begin(), m_visualEvents.end(), toInclusiveMs,
        [](qint64 time, const NeniaMidiVisualTimelineEvent &event) {
            return time < event.timeMs;
        });
    if (first >= last) {
        return {};
    }
    return std::span<const NeniaMidiVisualTimelineEvent>(
        &*first, static_cast<std::size_t>(last - first));
}

void NeniaMidiVisualTimelineProvider::clear() noexcept
{
    m_loaded = false;
    m_events.clear();
    m_visualEvents.clear();
}

} // namespace nenia::detail
