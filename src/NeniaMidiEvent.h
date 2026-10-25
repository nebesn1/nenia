#pragma once

#include <QByteArray>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace nenia::detail {

using NeniaMidiTick = std::uint64_t;

struct NeniaMidiNoteOnEvent {
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    std::uint8_t velocity = 0;
};

struct NeniaMidiNoteOffEvent {
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    std::uint8_t releaseVelocity = 0;
};

struct NeniaMidiPolyPressureEvent {
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    std::uint8_t pressure = 0;
};

struct NeniaMidiControlChangeEvent {
    std::uint8_t channel = 0;
    std::uint8_t controller = 0;
    std::uint8_t value = 0;
};

struct NeniaMidiProgramChangeEvent {
    std::uint8_t channel = 0;
    std::uint8_t program = 0;
};

struct NeniaMidiChannelPressureEvent {
    std::uint8_t channel = 0;
    std::uint8_t pressure = 0;
};

struct NeniaMidiPitchBendEvent {
    std::uint8_t channel = 0;
    std::uint8_t leastSignificant7 = 0;
    std::uint8_t mostSignificant7 = 0;
    std::uint16_t value14 = 8192;
};

struct NeniaMidiSystemCommonEvent {
    std::uint8_t status = 0;
    std::array<std::uint8_t, 2> data{};
    std::uint8_t dataLength = 0;
};

struct NeniaMidiSysExEvent {
    std::uint8_t status = 0;
    QByteArray data;
};

struct NeniaMidiMetaEvent {
    std::uint8_t type = 0;
    QByteArray data;
};

using NeniaMidiEventPayload = std::variant<NeniaMidiNoteOnEvent,
                                           NeniaMidiNoteOffEvent,
                                           NeniaMidiPolyPressureEvent,
                                           NeniaMidiControlChangeEvent,
                                           NeniaMidiProgramChangeEvent,
                                           NeniaMidiChannelPressureEvent,
                                           NeniaMidiPitchBendEvent,
                                           NeniaMidiSystemCommonEvent,
                                           NeniaMidiSysExEvent,
                                           NeniaMidiMetaEvent>;

enum class NeniaMidiEventCategory {
    ChannelVoice,
    SystemCommon,
    SystemExclusive,
    Meta
};

struct NeniaMidiEvent {
    NeniaMidiTick tick = 0;
    std::size_t sourceTrackIndex = 0;
    std::size_t sourceEventIndex = 0;
    NeniaMidiEventPayload payload = NeniaMidiMetaEvent{};

    [[nodiscard]] NeniaMidiEventCategory category() const noexcept;
};

struct NeniaMidiTrackInfo {
    std::size_t sourceTrackIndex = 0;
    NeniaMidiTick endTick = 0;
};

struct NeniaMidiTempoEvent {
    NeniaMidiTick tick = 0;
    std::size_t sourceTrackIndex = 0;
    std::size_t sourceEventIndex = 0;
    std::uint32_t microsecondsPerQuarter = 500000;
};

} // namespace nenia::detail
