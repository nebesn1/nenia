#pragma once

#include <array>
#include <cstdint>

namespace nenia::detail {

inline constexpr int kMidiChannelCount = 16;
inline constexpr int kVisibleTrackCount = kMidiChannelCount;
inline constexpr int kLegacyTrackCount = 10;
inline constexpr int kMidiNoteCount = 128;
inline constexpr int kMeterCount = 19;

enum class MidiControllerProvenance : std::uint8_t
{
    Unknown,
    Explicit,
    ResetDefault
};

struct TrackState
{
    std::array<bool, kMidiNoteCount> held{};
    std::array<std::uint32_t, kMidiNoteCount> heldCount{};
    std::array<float, kMidiNoteCount> intensity{};
    int lastNote = -1;
    int program = -1;
    int pan = -1;
    int volume = -1;
    int expression = -1;
    MidiControllerProvenance volumeProvenance = MidiControllerProvenance::Unknown;
    std::uint64_t lastNoteOnOrder = 0;
};

struct DisplayState
{
    // Physical channel state for all 16 MIDI channels, rendered simultaneously
    // as generic CH01–CH16 rows.
    std::array<TrackState, kMidiChannelCount> tracks{};
    std::array<float, kMeterCount> meters{};
    std::uint64_t nextNoteOnOrder = 0;
};

} // namespace nenia::detail
