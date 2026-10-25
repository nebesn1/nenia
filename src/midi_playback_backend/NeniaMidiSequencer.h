#pragma once

#include "NeniaMidiChannelState.h"
#include "NeniaMidiSequence.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace nenia::detail {

class NeniaSoundFontSynthesizer;

class NeniaMidiSequencer final {
public:
    bool initialize(std::shared_ptr<const NeniaMidiSequence> sequence,
                    std::uint32_t sampleRate,
                    QString *errorMessage = nullptr);

    // Output is stereo float PCM. The renderer splits every request at exact
    // sample-frame event boundaries and preserves sequence order for ties.
    bool render(NeniaSoundFontSynthesizer &synthesizer,
                std::span<float> interleavedOutput,
                std::size_t *renderedFrames,
                QString *errorMessage = nullptr);

    bool seek(std::uint64_t sampleFrame,
              NeniaSoundFontSynthesizer &synthesizer,
              NeniaMidiChannelState *restoredState = nullptr,
              QString *errorMessage = nullptr);
    bool reset(QString *errorMessage = nullptr);

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool reachedSequenceEnd() const noexcept;
    [[nodiscard]] std::uint64_t positionFrame() const noexcept;
    [[nodiscard]] std::uint64_t endFrame() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::size_t dispatchedEventCount() const noexcept;
    [[nodiscard]] std::size_t unsupportedSysExCount() const noexcept;
    [[nodiscard]] std::size_t unsupportedSystemCommonCount() const noexcept;
    [[nodiscard]] std::size_t ignoredNoteOffVelocityCount() const noexcept;
    [[nodiscard]] const std::shared_ptr<const NeniaMidiSequence> &sequence() const noexcept;

private:
    struct ScheduledEvent {
        std::uint64_t frame = 0;
        std::size_t eventIndex = 0;
    };

    bool dispatch(const NeniaMidiEvent &event,
                  NeniaSoundFontSynthesizer &synthesizer,
                  QString *errorMessage);
    bool dispatchDue(NeniaSoundFontSynthesizer &synthesizer, QString *errorMessage);
    bool restoreState(const NeniaMidiChannelState &state,
                      NeniaSoundFontSynthesizer &synthesizer,
                      QString *errorMessage) const;

    std::shared_ptr<const NeniaMidiSequence> m_sequence;
    std::vector<ScheduledEvent> m_scheduledEvents;
    std::uint32_t m_sampleRate = 0;
    std::uint64_t m_positionFrame = 0;
    std::uint64_t m_endFrame = 0;
    std::size_t m_eventCursor = 0;
    std::size_t m_dispatchedEventCount = 0;
    std::size_t m_unsupportedSysExCount = 0;
    std::size_t m_unsupportedSystemCommonCount = 0;
    std::size_t m_ignoredNoteOffVelocityCount = 0;
    bool m_initialized = false;
};

} // namespace nenia::detail
