#include "NeniaMidiSequencer.h"

#include "NeniaSoundFontSynthesizer.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <variant>

namespace nenia::detail {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool applyControl(NeniaSoundFontSynthesizer &synthesizer,
                  int channel,
                  int controller,
                  int value,
                  QString *errorMessage)
{
    return synthesizer.controlChange(channel, controller, value, errorMessage);
}

} // namespace

bool NeniaMidiSequencer::initialize(std::shared_ptr<const NeniaMidiSequence> sequence,
                                    std::uint32_t sampleRate,
                                    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    m_initialized = false;
    m_sequence.reset();
    m_scheduledEvents.clear();
    if (sequence == nullptr || !sequence->isLoaded() || sampleRate == 0) {
        setError(errorMessage, QStringLiteral("a loaded MIDI sequence and positive sample rate are required"));
        return false;
    }

    std::uint64_t endFrame = 0;
    if (!sequence->tickToSampleFrame(sequence->durationTick(), sampleRate,
                                     &endFrame, errorMessage)) {
        return false;
    }
    std::vector<ScheduledEvent> scheduled;
    scheduled.reserve(sequence->events().size());
    for (std::size_t index = 0; index < sequence->events().size(); ++index) {
        std::uint64_t frame = 0;
        if (!sequence->tickToSampleFrame(sequence->events()[index].tick, sampleRate,
                                         &frame, errorMessage)) {
            return false;
        }
        scheduled.push_back({frame, index});
    }

    m_sequence = std::move(sequence);
    m_scheduledEvents = std::move(scheduled);
    m_sampleRate = sampleRate;
    m_positionFrame = 0;
    m_endFrame = endFrame;
    m_eventCursor = 0;
    m_dispatchedEventCount = 0;
    m_unsupportedSysExCount = 0;
    m_unsupportedSystemCommonCount = 0;
    m_ignoredNoteOffVelocityCount = 0;
    m_initialized = true;
    return true;
}

bool NeniaMidiSequencer::render(NeniaSoundFontSynthesizer &synthesizer,
                                std::span<float> interleavedOutput,
                                std::size_t *renderedFrames,
                                QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (renderedFrames == nullptr || (interleavedOutput.size() % 2U) != 0U) {
        setError(errorMessage, QStringLiteral("stereo output and a valid rendered-frame result are required"));
        return false;
    }
    *renderedFrames = 0;
    if (!m_initialized || !synthesizer.hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the sequencer and SoundFont synthesizer first"));
        return false;
    }

    const std::size_t requestedFrames = interleavedOutput.size() / 2U;
    while (*renderedFrames < requestedFrames) {
        if (!dispatchDue(synthesizer, errorMessage)) {
            return false;
        }
        if (m_positionFrame >= m_endFrame) {
            break;
        }

        std::uint64_t boundary = m_endFrame;
        if (m_eventCursor < m_scheduledEvents.size()) {
            boundary = std::min(boundary, m_scheduledEvents[m_eventCursor].frame);
        }
        if (boundary <= m_positionFrame) {
            // dispatchDue() consumes every event at the current frame. Reaching
            // this guard means the schedule is internally inconsistent.
            setError(errorMessage, QStringLiteral("MIDI sample-frame schedule did not advance"));
            return false;
        }

        const std::uint64_t availableToBoundary = boundary - m_positionFrame;
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(
            availableToBoundary, requestedFrames - *renderedFrames));
        const std::size_t sampleOffset = *renderedFrames * 2U;
        if (!synthesizer.renderStereoFloat(
                interleavedOutput.subspan(sampleOffset, count * 2U), errorMessage)) {
            return false;
        }
        m_positionFrame += count;
        *renderedFrames += count;
    }

    // Also apply events exactly at the sequence end even if the caller's block
    // ended on that boundary.
    return dispatchDue(synthesizer, errorMessage);
}

bool NeniaMidiSequencer::seek(std::uint64_t sampleFrame,
                              NeniaSoundFontSynthesizer &synthesizer,
                              NeniaMidiChannelState *restoredState,
                              QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!m_initialized || !synthesizer.hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the sequencer and SoundFont synthesizer first"));
        return false;
    }
    sampleFrame = std::min(sampleFrame, m_endFrame);

    NeniaMidiTick low = 0;
    NeniaMidiTick high = m_sequence->durationTick();
    while (low < high) {
        const NeniaMidiTick distance = high - low;
        const NeniaMidiTick middle = low + distance / 2U + distance % 2U;
        std::uint64_t middleFrame = 0;
        if (!m_sequence->tickToSampleFrame(middle, m_sampleRate,
                                           &middleFrame, errorMessage)) {
            return false;
        }
        if (middleFrame <= sampleFrame) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }

    NeniaMidiChannelState state;
    if (!m_sequence->stateAtTick(low, &state, errorMessage)
        || !synthesizer.reset(errorMessage)
        || !restoreState(state, synthesizer, errorMessage)) {
        return false;
    }

    const auto next = std::upper_bound(
        m_scheduledEvents.begin(), m_scheduledEvents.end(), sampleFrame,
        [](std::uint64_t frame, const ScheduledEvent &event) { return frame < event.frame; });
    m_eventCursor = static_cast<std::size_t>(next - m_scheduledEvents.begin());
    m_positionFrame = sampleFrame;
    m_dispatchedEventCount = 0;
    m_unsupportedSysExCount = 0;
    m_unsupportedSystemCommonCount = 0;
    m_ignoredNoteOffVelocityCount = 0;
    if (restoredState != nullptr) {
        *restoredState = std::move(state);
    }
    return true;
}

bool NeniaMidiSequencer::reset(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!m_initialized) {
        setError(errorMessage, QStringLiteral("initialize the MIDI sequencer first"));
        return false;
    }
    m_positionFrame = 0;
    m_eventCursor = 0;
    m_dispatchedEventCount = 0;
    m_unsupportedSysExCount = 0;
    m_unsupportedSystemCommonCount = 0;
    m_ignoredNoteOffVelocityCount = 0;
    return true;
}

bool NeniaMidiSequencer::isInitialized() const noexcept { return m_initialized; }

bool NeniaMidiSequencer::reachedSequenceEnd() const noexcept
{
    return m_initialized && m_positionFrame >= m_endFrame
        && m_eventCursor >= m_scheduledEvents.size();
}

std::uint64_t NeniaMidiSequencer::positionFrame() const noexcept { return m_positionFrame; }
std::uint64_t NeniaMidiSequencer::endFrame() const noexcept { return m_endFrame; }
std::uint32_t NeniaMidiSequencer::sampleRate() const noexcept { return m_sampleRate; }
std::size_t NeniaMidiSequencer::dispatchedEventCount() const noexcept
{
    return m_dispatchedEventCount;
}
std::size_t NeniaMidiSequencer::unsupportedSysExCount() const noexcept
{
    return m_unsupportedSysExCount;
}
std::size_t NeniaMidiSequencer::unsupportedSystemCommonCount() const noexcept
{
    return m_unsupportedSystemCommonCount;
}
std::size_t NeniaMidiSequencer::ignoredNoteOffVelocityCount() const noexcept
{
    return m_ignoredNoteOffVelocityCount;
}
const std::shared_ptr<const NeniaMidiSequence> &NeniaMidiSequencer::sequence() const noexcept
{
    return m_sequence;
}

bool NeniaMidiSequencer::dispatchDue(NeniaSoundFontSynthesizer &synthesizer,
                                     QString *errorMessage)
{
    while (m_eventCursor < m_scheduledEvents.size()
           && m_scheduledEvents[m_eventCursor].frame <= m_positionFrame) {
        const NeniaMidiEvent &event = m_sequence->events().at(
            m_scheduledEvents[m_eventCursor].eventIndex);
        if (!dispatch(event, synthesizer, errorMessage)) {
            return false;
        }
        ++m_eventCursor;
        ++m_dispatchedEventCount;
    }
    return true;
}

bool NeniaMidiSequencer::dispatch(const NeniaMidiEvent &event,
                                  NeniaSoundFontSynthesizer &synthesizer,
                                  QString *errorMessage)
{
    return std::visit([&](const auto &value) -> bool {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, NeniaMidiNoteOnEvent>) {
            if (value.velocity == 0) {
                return synthesizer.noteOff(value.channel, value.key, errorMessage);
            }
            return synthesizer.noteOn(value.channel, value.key, value.velocity, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiNoteOffEvent>) {
            if (value.releaseVelocity != 0) {
                ++m_ignoredNoteOffVelocityCount;
            }
            return synthesizer.noteOff(value.channel, value.key, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiPolyPressureEvent>) {
            return synthesizer.keyPressure(value.channel, value.key,
                                           value.pressure, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiControlChangeEvent>) {
            if (value.controller == 120) {
                return synthesizer.allSoundsOff(value.channel, errorMessage);
            }
            return applyControl(synthesizer, value.channel,
                                value.controller, value.value, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiProgramChangeEvent>) {
            return synthesizer.programChange(value.channel, value.program, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiChannelPressureEvent>) {
            return synthesizer.channelPressure(value.channel, value.pressure, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiPitchBendEvent>) {
            return synthesizer.pitchBend(value.channel, value.value14, errorMessage);
        } else if constexpr (std::is_same_v<Event, NeniaMidiSysExEvent>) {
            // Arbitrary device-specific payloads are deliberately never passed
            // through to FluidSynth. The runtime diagnostics expose this count.
            ++m_unsupportedSysExCount;
            return true;
        } else if constexpr (std::is_same_v<Event, NeniaMidiSystemCommonEvent>) {
            ++m_unsupportedSystemCommonCount;
            return true;
        } else {
            // Tempo/meta events remain sequence timing metadata, not synth input.
            return true;
        }
    }, event.payload);
}

bool NeniaMidiSequencer::restoreState(const NeniaMidiChannelState &state,
                                      NeniaSoundFontSynthesizer &synthesizer,
                                      QString *errorMessage) const
{
    for (int channelIndex = 0; channelIndex < static_cast<int>(NeniaMidiChannelState::kChannelCount);
         ++channelIndex) {
        const NeniaMidiSingleChannelState &channel =
            state.channel(static_cast<std::size_t>(channelIndex));
        const auto apply = [&](int controller, int value) {
            return applyControl(synthesizer, channelIndex, controller, value, errorMessage);
        };

        if (!apply(0, channel.effectiveProgramBankMsb())
            || !apply(32, channel.effectiveProgramBankLsb())
            || !synthesizer.programChange(channelIndex, channel.effectiveProgram(), errorMessage)
            // Bank Select after Program Change updates the current selector but
            // does not retroactively change the already selected preset.
            || !apply(0, channel.effectiveBankMsb())
            || !apply(32, channel.effectiveBankLsb())
            || !apply(1, channel.effectiveModulation())
            || !apply(7, channel.effectiveVolume())
            || !apply(10, channel.effectivePan())
            || !apply(11, channel.effectiveExpression())) {
            return false;
        }

        // Reapply the latest non-core controller values. The source event log
        // remains authoritative during forward playback; this canonical order
        // is only the documented best-effort state reconstruction for seeking.
        for (int controller = 0; controller < 120; ++controller) {
            if (controller == 0 || controller == 1 || controller == 6 || controller == 7
                || controller == 10 || controller == 11 || controller == 32 || controller == 38
                || controller == 64 || controller == 96 || controller == 97
                || controller == 98 || controller == 99 || controller == 100
                || controller == 101) {
                continue;
            }
            const auto &value = channel.controllerValues.at(static_cast<std::size_t>(controller));
            if (value.has_value() && !apply(controller, value.value())) {
                return false;
            }
        }

        // FluidSynth handles RPN 0 pitch-bend sensitivity through the standard
        // selector/data-entry CC sequence, so replay it with its selector first.
        if (!apply(101, 0) || !apply(100, 0)
            || !apply(6, channel.effectivePitchBendSensitivitySemitones())
            || !apply(38, channel.effectivePitchBendSensitivityCents())) {
            return false;
        }
        if (channel.nrpnSelectionActive) {
            if (!apply(99, channel.nrpnMsb.value_or(127))
                || !apply(98, channel.nrpnLsb.value_or(127))) {
                return false;
            }
        } else if (channel.rpnSelectionActive) {
            if (!apply(101, channel.rpnMsb.value_or(127))
                || !apply(100, channel.rpnLsb.value_or(127))) {
                return false;
            }
        } else if (!apply(101, 127) || !apply(100, 127)) {
            return false;
        }

        if (channel.channelPressure.has_value()
            && !synthesizer.channelPressure(channelIndex,
                                            channel.channelPressure.value(), errorMessage)) {
            return false;
        }
        if (!synthesizer.pitchBend(channelIndex, channel.effectivePitchBend(), errorMessage)
            || !apply(64, channel.sustainControllerValue.value_or(0))) {
            return false;
        }

        // Active note envelopes and sample phases cannot be restored. Re-trigger
        // held keys, and model sustain-held notes as note-on then note-off.
        for (const NeniaMidiNoteInstance &note : channel.activeNotes) {
            if (!synthesizer.noteOn(channelIndex, note.key, note.velocity, errorMessage)) {
                return false;
            }
            if (!note.keyHeld
                && !synthesizer.noteOff(channelIndex, note.key, errorMessage)) {
                return false;
            }
        }
        for (std::size_t key = 0; key < channel.polyPressure.size(); ++key) {
            if (channel.polyPressure[key].has_value()
                && !synthesizer.keyPressure(channelIndex, static_cast<int>(key),
                                            channel.polyPressure[key].value(), errorMessage)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace nenia::detail
