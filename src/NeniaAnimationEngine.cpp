#include "NeniaAnimationEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nenia::detail {
namespace {

float clampNormalized(float value)
{
    if (std::isnan(value)) {
        return 0.0f;
    }
    if (!std::isfinite(value)) {
        return value > 0.0f ? 1.0f : 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

float releaseFactor(double milliseconds, float tauMilliseconds)
{
    return static_cast<float>(std::exp(-milliseconds / static_cast<double>(tauMilliseconds)));
}

bool validTrack(int track)
{
    return track >= 0 && track < kLegacyTrackCount;
}

bool validMidiChannel(int channel)
{
    return channel >= 0 && channel < kMidiChannelCount;
}

bool validNote(int note)
{
    return note >= 0 && note < kMidiNoteCount;
}

bool validMeter(int meter)
{
    return meter >= 0 && meter < kMeterCount;
}

} // namespace

void NeniaAnimationEngine::noteOn(int track, int note, float velocity)
{
    if (!validTrack(track) || !validNote(note)) {
        return;
    }

    const float normalizedVelocity = clampNormalized(velocity);
    if (normalizedVelocity <= 0.0f) {
        noteOff(track, note);
        return;
    }

    TrackState &trackState = m_state.tracks.at(static_cast<std::size_t>(track));
    trackState.heldCount.at(static_cast<std::size_t>(note)) = 1;
    trackState.held.at(static_cast<std::size_t>(note)) = true;
    float &intensity = trackState.intensity.at(static_cast<std::size_t>(note));
    intensity = std::max(intensity, normalizedVelocity);
    trackState.lastNote = note;
    ++m_state.nextNoteOnOrder;
    if (m_state.nextNoteOnOrder == 0) {
        m_state.nextNoteOnOrder = 1;
    }
    trackState.lastNoteOnOrder = m_state.nextNoteOnOrder;
}

void NeniaAnimationEngine::noteOnMidiChannel(int channel, int note, float velocity)
{
    if (!validMidiChannel(channel) || !validNote(note)) {
        return;
    }

    const float normalizedVelocity = clampNormalized(velocity);
    if (normalizedVelocity <= 0.0f) {
        noteOffMidiChannel(channel, note);
        return;
    }

    TrackState &trackState = m_state.tracks.at(static_cast<std::size_t>(channel));
    std::uint32_t &heldCount = trackState.heldCount.at(static_cast<std::size_t>(note));
    if (heldCount < std::numeric_limits<std::uint32_t>::max()) {
        ++heldCount;
    }
    trackState.held.at(static_cast<std::size_t>(note)) = true;
    float &intensity = trackState.intensity.at(static_cast<std::size_t>(note));
    intensity = std::max(intensity, normalizedVelocity);
    trackState.lastNote = note;
    ++m_state.nextNoteOnOrder;
    if (m_state.nextNoteOnOrder == 0) {
        m_state.nextNoteOnOrder = 1;
    }
    trackState.lastNoteOnOrder = m_state.nextNoteOnOrder;
}

void NeniaAnimationEngine::noteOff(int track, int note)
{
    if (!validTrack(track) || !validNote(note)) {
        return;
    }

    TrackState &trackState = m_state.tracks.at(static_cast<std::size_t>(track));
    trackState.heldCount.at(static_cast<std::size_t>(note)) = 0;
    trackState.held.at(static_cast<std::size_t>(note)) = false;
    float &intensity = trackState.intensity.at(static_cast<std::size_t>(note));
    if (intensity <= kAnimationZeroThreshold) {
        intensity = 0.0f;
    }
}

void NeniaAnimationEngine::noteOffMidiChannel(int channel, int note)
{
    if (!validMidiChannel(channel) || !validNote(note)) {
        return;
    }

    TrackState &trackState = m_state.tracks.at(static_cast<std::size_t>(channel));
    std::uint32_t &heldCount = trackState.heldCount.at(static_cast<std::size_t>(note));
    if (heldCount > 0) {
        --heldCount;
    }
    trackState.held.at(static_cast<std::size_t>(note)) = heldCount > 0;
    float &intensity = trackState.intensity.at(static_cast<std::size_t>(note));
    if (intensity <= kAnimationZeroThreshold) {
        intensity = 0.0f;
    }
}

void NeniaAnimationEngine::programChange(int track, int program)
{
    if (!validTrack(track)) {
        return;
    }
    programChangeMidiChannel(track, program);
}

void NeniaAnimationEngine::programChangeMidiChannel(int channel, int program)
{
    if (!validMidiChannel(channel) || program < 0 || program > 127) {
        return;
    }
    m_state.tracks.at(static_cast<std::size_t>(channel)).program = program;
}

void NeniaAnimationEngine::panChange(int track, int pan)
{
    if (!validTrack(track)) {
        return;
    }
    panChangeMidiChannel(track, pan);
}

void NeniaAnimationEngine::panChangeMidiChannel(int channel, int pan)
{
    if (!validMidiChannel(channel) || pan < 0 || pan > 127) {
        return;
    }
    m_state.tracks.at(static_cast<std::size_t>(channel)).pan = pan;
}

void NeniaAnimationEngine::volumeChangeMidiChannel(
    int channel, int volume, MidiControllerProvenance provenance)
{
    if (!validMidiChannel(channel)) {
        return;
    }
    TrackState &track = m_state.tracks.at(static_cast<std::size_t>(channel));
    if (volume < 0 || volume > 127 || provenance == MidiControllerProvenance::Unknown) {
        track.volume = -1;
        track.volumeProvenance = MidiControllerProvenance::Unknown;
        return;
    }
    track.volume = volume;
    track.volumeProvenance = provenance;
}

void NeniaAnimationEngine::expressionChangeMidiChannel(int channel, int expression)
{
    if (!validMidiChannel(channel) || expression < 0 || expression > 127) {
        return;
    }
    m_state.tracks.at(static_cast<std::size_t>(channel)).expression = expression;
}

void NeniaAnimationEngine::setMeterLevel(int meter, float level)
{
    if (!validMeter(meter)) {
        return;
    }

    float &meterLevel = m_state.meters.at(static_cast<std::size_t>(meter));
    meterLevel = clampNormalized(level);
    if (meterLevel <= kAnimationZeroThreshold) {
        meterLevel = 0.0f;
    }
}

void NeniaAnimationEngine::reset()
{
    m_state = {};
}

void NeniaAnimationEngine::advance(std::chrono::milliseconds delta)
{
    if (delta.count() <= 0) {
        return;
    }

    const double milliseconds = std::chrono::duration<double, std::milli>(delta).count();
    if (!(milliseconds > 0.0)) {
        return;
    }

    const float noteFactor = releaseFactor(milliseconds, kNoteReleaseTauMilliseconds);
    const float meterFactor = releaseFactor(milliseconds, kMeterReleaseTauMilliseconds);

    for (TrackState &track : m_state.tracks) {
        for (int note = 0; note < kMidiNoteCount; ++note) {
            if (track.held.at(static_cast<std::size_t>(note))) {
                continue;
            }

            float &intensity = track.intensity.at(static_cast<std::size_t>(note));
            if (intensity <= 0.0f) {
                continue;
            }
            intensity *= noteFactor;
            if (intensity <= kAnimationZeroThreshold) {
                intensity = 0.0f;
            }
        }
    }

    for (float &meter : m_state.meters) {
        if (meter <= 0.0f) {
            continue;
        }
        meter *= meterFactor;
        if (meter <= kAnimationZeroThreshold) {
            meter = 0.0f;
        }
    }
}

const DisplayState &NeniaAnimationEngine::displayState() const noexcept
{
    return m_state;
}

bool NeniaAnimationEngine::hasActiveDecay() const noexcept
{
    for (const TrackState &track : m_state.tracks) {
        for (int note = 0; note < kMidiNoteCount; ++note) {
            if (!track.held.at(static_cast<std::size_t>(note))
                && track.intensity.at(static_cast<std::size_t>(note)) > kAnimationZeroThreshold) {
                return true;
            }
        }
    }

    for (const float meter : m_state.meters) {
        if (meter > kAnimationZeroThreshold) {
            return true;
        }
    }
    return false;
}

bool trackHasHeldNote(const TrackState &track) noexcept
{
    for (const bool held : track.held) {
        if (held) {
            return true;
        }
    }
    return false;
}

float trackActivity(const TrackState &track) noexcept
{
    float maximum = 0.0f;
    for (const float intensity : track.intensity) {
        maximum = std::max(maximum, clampNormalized(intensity));
    }
    return maximum;
}

int focusTrackForDisplay(const DisplayState &state) noexcept
{
    int focusedHeldTrack = -1;
    std::uint64_t focusedHeldOrder = 0;
    for (int trackIndex = 0; trackIndex < kVisibleTrackCount; ++trackIndex) {
        const TrackState &track = state.tracks.at(static_cast<std::size_t>(trackIndex));
        if (trackHasHeldNote(track) && track.lastNoteOnOrder > focusedHeldOrder) {
            focusedHeldTrack = trackIndex;
            focusedHeldOrder = track.lastNoteOnOrder;
        }
    }
    if (focusedHeldTrack >= 0) {
        return focusedHeldTrack;
    }

    int mostRecentTrack = -1;
    std::uint64_t mostRecentOrder = 0;
    for (int trackIndex = 0; trackIndex < kVisibleTrackCount; ++trackIndex) {
        const TrackState &track = state.tracks.at(static_cast<std::size_t>(trackIndex));
        if (track.lastNoteOnOrder > mostRecentOrder) {
            mostRecentTrack = trackIndex;
            mostRecentOrder = track.lastNoteOnOrder;
        }
    }
    return mostRecentTrack;
}

} // namespace nenia::detail
