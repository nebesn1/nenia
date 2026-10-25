#pragma once

#include <nenia/TimelineEventProvider.h>

#include <QByteArray>
#include <QString>

#include <memory>
#include <vector>

namespace nenia::detail {
class NeniaMidiSequence;
}

namespace nenia {

namespace app {
class NeniaApplicationPlaybackRouter;
}

class MidiTimelineProvider final : public TimelineEventProvider
{
public:
    MidiTimelineProvider() = default;

    bool load(const QString &path, QString *errorMessage = nullptr);
    bool loadMidi(const QByteArray &midi, QString *errorMessage = nullptr);

    // Channel numbers are zero-based here: 0..9 map to Nenia tracks 0..9.
    // Channels 10..15 are reported once each when they contain note events.
    const std::vector<int> &ignoredMidiChannels() const noexcept;

    bool isLoaded() const noexcept override;
    std::size_t eventCount() const noexcept override;
    std::optional<qint64> firstEventTimeMs() const noexcept override;
    std::optional<qint64> lastEventTimeMs() const noexcept override;
    std::span<const TimelineEvent> eventsBetween(
        qint64 fromExclusiveMs,
        qint64 toInclusiveMs) const noexcept override;
    void clear() noexcept override;

private:
    friend class app::NeniaApplicationPlaybackRouter;

    bool loadSequence(std::shared_ptr<const detail::NeniaMidiSequence> sequence,
                      QString *errorMessage = nullptr);
    bool loadParsedMidi(const QByteArray &midi, QString *errorMessage);

    bool m_loaded = false;
    std::vector<TimelineEvent> m_events;
    std::vector<int> m_ignoredMidiChannels;
};

} // namespace nenia
