#include "NeniaTimelineSynchronizer.h"

#include "NeniaAnimationEngine.h"
#include "NeniaMidiVisualTimelineProvider.h"

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <utility>

namespace nenia::detail {

NeniaTimelineSynchronizer::NeniaTimelineSynchronizer(NeniaAnimationEngine *engine) noexcept
    : m_engine(engine)
{
}

void NeniaTimelineSynchronizer::setEngine(NeniaAnimationEngine *engine) noexcept
{
    m_engine = engine;
    reset();
}

void NeniaTimelineSynchronizer::setProvider(
    std::shared_ptr<const TimelineEventProvider> provider)
{
    m_provider = std::move(provider);
    m_fullChannelMidi = std::dynamic_pointer_cast<
        const NeniaMidiVisualTimelineProvider>(m_provider) != nullptr;
    reset();
}

void NeniaTimelineSynchronizer::clearProvider()
{
    m_provider.reset();
    m_fullChannelMidi = false;
    reset();
}

bool NeniaTimelineSynchronizer::syncTo(qint64 positionMs)
{
    if (m_engine == nullptr) {
        return false;
    }

    const qint64 targetPositionMs = std::max<qint64>(positionMs, 0);
    if (m_synchronizedPosition.has_value()
        && m_synchronizedPosition.value() == targetPositionMs) {
        return false;
    }

    if (!m_provider) {
        m_engine->reset();
        m_synchronizedPosition = targetPositionMs;
        return true;
    }

    if (!m_synchronizedPosition.has_value()) {
        m_engine->reset();
        processRange(-1, targetPositionMs);
    } else if (targetPositionMs > m_synchronizedPosition.value()) {
        processRange(m_synchronizedPosition.value(), targetPositionMs);
    } else {
        m_engine->reset();
        processRange(-1, targetPositionMs);
    }

    m_synchronizedPosition = targetPositionMs;
    return true;
}

void NeniaTimelineSynchronizer::reset() noexcept
{
    if (m_engine != nullptr) {
        m_engine->reset();
    }
    m_synchronizedPosition.reset();
}

std::optional<qint64> NeniaTimelineSynchronizer::synchronizedPosition() const noexcept
{
    return m_synchronizedPosition;
}

void NeniaTimelineSynchronizer::processRange(qint64 fromExclusiveMs, qint64 toInclusiveMs)
{
    if (m_engine == nullptr || !m_provider || fromExclusiveMs >= toInclusiveMs) {
        return;
    }

    // Media time starts at zero. The -1 lower-bound is only the provider query
    // convention that makes t=0 events visible; it is not one millisecond of
    // animation time.
    qint64 cursorMs = std::max<qint64>(fromExclusiveMs, 0);
    if (m_fullChannelMidi) {
        const auto midiProvider = std::static_pointer_cast<
            const NeniaMidiVisualTimelineProvider>(m_provider);
        for (const NeniaMidiVisualTimelineEvent &event :
             midiProvider->visualEventsBetween(fromExclusiveMs, toInclusiveMs)) {
            if (event.timeMs > cursorMs) {
                m_engine->advance(std::chrono::milliseconds(event.timeMs - cursorMs));
            }
            applyMidiVisualEvent(event);
            cursorMs = event.timeMs;
        }
    } else {
        for (const TimelineEvent &event :
             m_provider->eventsBetween(fromExclusiveMs, toInclusiveMs)) {
            if (event.timeMs > cursorMs) {
                m_engine->advance(std::chrono::milliseconds(event.timeMs - cursorMs));
            }
            applyEvent(event);
            cursorMs = event.timeMs;
        }
    }

    if (toInclusiveMs > cursorMs) {
        m_engine->advance(std::chrono::milliseconds(toInclusiveMs - cursorMs));
    }
}

void NeniaTimelineSynchronizer::applyEvent(const TimelineEvent &event)
{
    std::visit(
        [this](const auto &payload) {
            using EventType = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<EventType, NoteOnEvent>) {
                if (m_fullChannelMidi) {
                    m_engine->noteOnMidiChannel(payload.track, payload.note, payload.velocity);
                } else {
                    m_engine->noteOn(payload.track, payload.note, payload.velocity);
                }
            } else if constexpr (std::is_same_v<EventType, NoteOffEvent>) {
                if (m_fullChannelMidi) {
                    m_engine->noteOffMidiChannel(payload.track, payload.note);
                } else {
                    m_engine->noteOff(payload.track, payload.note);
                }
            } else if constexpr (std::is_same_v<EventType, MeterEvent>) {
                m_engine->setMeterLevel(payload.meter, payload.level);
            } else if constexpr (std::is_same_v<EventType, ProgramChangeEvent>) {
                if (m_fullChannelMidi) {
                    m_engine->programChangeMidiChannel(payload.track, payload.program);
                } else {
                    m_engine->programChange(payload.track, payload.program);
                }
            } else if constexpr (std::is_same_v<EventType, PanChangeEvent>) {
                if (m_fullChannelMidi) {
                    m_engine->panChangeMidiChannel(payload.track, payload.pan);
                } else {
                    m_engine->panChange(payload.track, payload.pan);
                }
            }
        },
        event.payload);
}

void NeniaTimelineSynchronizer::applyMidiVisualEvent(
    const NeniaMidiVisualTimelineEvent &event)
{
    std::visit(
        [this](const auto &payload) {
            using EventType = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<EventType, NoteOnEvent>) {
                m_engine->noteOnMidiChannel(payload.track, payload.note, payload.velocity);
            } else if constexpr (std::is_same_v<EventType, NoteOffEvent>) {
                m_engine->noteOffMidiChannel(payload.track, payload.note);
            } else if constexpr (std::is_same_v<EventType, ProgramChangeEvent>) {
                m_engine->programChangeMidiChannel(payload.track, payload.program);
            } else if constexpr (std::is_same_v<EventType, PanChangeEvent>) {
                m_engine->panChangeMidiChannel(payload.track, payload.pan);
            } else if constexpr (
                std::is_same_v<EventType, NeniaMidiControllerProjectionEvent>) {
                switch (payload.kind) {
                case NeniaMidiControllerProjectionKind::Volume:
                    m_engine->volumeChangeMidiChannel(
                        payload.channel, payload.value, MidiControllerProvenance::Explicit);
                    break;
                case NeniaMidiControllerProjectionKind::Expression:
                    m_engine->expressionChangeMidiChannel(payload.channel, payload.value);
                    break;
                }
            }
        },
        event.payload);
}

} // namespace nenia::detail
