#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace nenia::detail {

enum class KeyboardRangeMode {
    Piano88,
    FullMidi128,
    Compact49,
    Extended96
};

struct KeyboardRangeDescriptor
{
    KeyboardRangeMode mode;
    int lowestMidiNote;
    int highestMidiNote;
    int whiteKeyCount;
    int blackKeyCount;
    const char *persistenceToken;
    const char *displayNameUtf8;

    constexpr int totalNoteCount() const noexcept
    {
        return highestMidiNote - lowestMidiNote + 1;
    }
};

inline constexpr std::array<KeyboardRangeDescriptor, 4> kKeyboardRangeDescriptors = {{
    {KeyboardRangeMode::Piano88, 21, 108, 52, 36, "piano88", "88 Keys — Piano"},
    {KeyboardRangeMode::FullMidi128, 0, 127, 75, 53, "full128", "128 Keys — Full MIDI"},
    {KeyboardRangeMode::Compact49, 36, 84, 29, 20, "compact49", "49 Keys — Compact"},
    {KeyboardRangeMode::Extended96, 24, 119, 56, 40, "extended96", "96 Keys — Extended"}
}};

inline constexpr KeyboardRangeMode kDefaultKeyboardRangeMode =
    KeyboardRangeMode::Piano88;

inline constexpr const KeyboardRangeDescriptor &keyboardRangeDescriptor(
    KeyboardRangeMode mode) noexcept
{
    for (const auto &descriptor : kKeyboardRangeDescriptors) {
        if (descriptor.mode == mode) {
            return descriptor;
        }
    }
    return kKeyboardRangeDescriptors.front();
}

inline constexpr std::optional<KeyboardRangeMode> keyboardRangeModeFromToken(
    std::string_view token) noexcept
{
    for (const auto &descriptor : kKeyboardRangeDescriptors) {
        if (token == descriptor.persistenceToken) {
            return descriptor.mode;
        }
    }
    return std::nullopt;
}

inline constexpr std::optional<KeyboardRangeMode> keyboardRangeModeForBounds(
    int lowestMidiNote,
    int highestMidiNote) noexcept
{
    for (const auto &descriptor : kKeyboardRangeDescriptors) {
        if (descriptor.lowestMidiNote == lowestMidiNote
            && descriptor.highestMidiNote == highestMidiNote) {
            return descriptor.mode;
        }
    }
    return std::nullopt;
}

inline constexpr bool isBlackMidiPitchClass(int note) noexcept
{
    switch (note % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

} // namespace nenia::detail
