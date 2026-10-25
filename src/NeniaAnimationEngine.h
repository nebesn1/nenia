#pragma once

#include "NeniaAnimationState.h"
#include "NeniaOctaveNaming.h"

#include <QString>

#include <chrono>

namespace nenia::detail {

inline constexpr float kNoteReleaseTauMilliseconds = 250.0f;
inline constexpr float kMeterReleaseTauMilliseconds = 300.0f;
inline constexpr float kAnimationZeroThreshold = 0.0005f;

class NeniaAnimationEngine final
{
public:
    NeniaAnimationEngine() = default;

    void noteOn(int track, int note, float velocity = 1.0f);
    void noteOff(int track, int note);
    void programChange(int track, int program);
    void panChange(int track, int pan);
    void noteOnMidiChannel(int channel, int note, float velocity = 1.0f);
    void noteOffMidiChannel(int channel, int note);
    void programChangeMidiChannel(int channel, int program);
    void panChangeMidiChannel(int channel, int pan);
    void volumeChangeMidiChannel(int channel, int volume,
                                 MidiControllerProvenance provenance);
    void expressionChangeMidiChannel(int channel, int expression);
    void setMeterLevel(int meter, float level);
    void reset();

    void advance(std::chrono::milliseconds delta);

    const DisplayState &displayState() const noexcept;
    bool hasActiveDecay() const noexcept;

private:
    DisplayState m_state;
};

bool trackHasHeldNote(const TrackState &track) noexcept;
float trackActivity(const TrackState &track) noexcept;
int focusTrackForDisplay(const DisplayState &state) noexcept;

} // namespace nenia::detail
