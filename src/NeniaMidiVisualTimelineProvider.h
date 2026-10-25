#pragma once

#include <nenia/TimelineEventProvider.h>

#include <QString>

#include <memory>
#include <variant>
#include <vector>

namespace nenia::detail {

class NeniaMidiSequence;

enum class NeniaMidiControllerProjectionKind
{
    Volume,
    Expression
};

struct NeniaMidiControllerProjectionEvent
{
    NeniaMidiControllerProjectionKind kind = NeniaMidiControllerProjectionKind::Volume;
    int channel = 0;
    int value = 0;
};

using NeniaMidiVisualEventPayload = std::variant<NoteOnEvent,
                                                 NoteOffEvent,
                                                 ProgramChangeEvent,
                                                 PanChangeEvent,
                                                 NeniaMidiControllerProjectionEvent>;

struct NeniaMidiVisualTimelineEvent
{
    qint64 timeMs = 0;
    NeniaMidiVisualEventPayload payload = NoteOnEvent{};
};

// Application-internal full-channel projection. The installed legacy
// MidiTimelineProvider intentionally remains limited to channels 0..9.
class NeniaMidiVisualTimelineProvider final : public TimelineEventProvider
{
public:
    bool load(const QString &path, QString *errorMessage = nullptr);
    bool loadSequence(std::shared_ptr<const NeniaMidiSequence> sequence,
                      QString *errorMessage = nullptr);

    bool isLoaded() const noexcept override;
    std::size_t eventCount() const noexcept override;
    std::optional<qint64> firstEventTimeMs() const noexcept override;
    std::optional<qint64> lastEventTimeMs() const noexcept override;
    std::span<const TimelineEvent> eventsBetween(
        qint64 fromExclusiveMs,
        qint64 toInclusiveMs) const noexcept override;
    // Private full-channel stream keeps CC projection in the exact same
    // stable order as note/program/PAN visual events without expanding the
    // installed TimelineEvent variant.
    std::span<const NeniaMidiVisualTimelineEvent> visualEventsBetween(
        qint64 fromExclusiveMs,
        qint64 toInclusiveMs) const noexcept;
    void clear() noexcept override;

private:
    bool m_loaded = false;
    std::vector<TimelineEvent> m_events;
    std::vector<NeniaMidiVisualTimelineEvent> m_visualEvents;
};

} // namespace nenia::detail
