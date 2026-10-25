#pragma once

#include <nenia/TimelineEventProvider.h>

#include <QtGlobal>

#include <memory>
#include <optional>

namespace nenia::detail {

class NeniaAnimationEngine;
struct NeniaMidiVisualTimelineEvent;

class NeniaTimelineSynchronizer final
{
public:
    explicit NeniaTimelineSynchronizer(NeniaAnimationEngine *engine = nullptr) noexcept;

    void setEngine(NeniaAnimationEngine *engine) noexcept;

    void setProvider(std::shared_ptr<const TimelineEventProvider> provider);
    void clearProvider();

    // Synchronizes the engine to a media position in integer milliseconds.
    // Returns true when the engine or synchronization cursor changed.
    bool syncTo(qint64 positionMs);

    // Resets visual state and the cursor while retaining the provider.
    void reset() noexcept;

    std::optional<qint64> synchronizedPosition() const noexcept;

private:
    void processRange(qint64 fromExclusiveMs, qint64 toInclusiveMs);
    void applyEvent(const TimelineEvent &event);
    void applyMidiVisualEvent(const NeniaMidiVisualTimelineEvent &event);

    NeniaAnimationEngine *m_engine = nullptr;
    std::shared_ptr<const TimelineEventProvider> m_provider;
    bool m_fullChannelMidi = false;
    std::optional<qint64> m_synchronizedPosition;
};

} // namespace nenia::detail
