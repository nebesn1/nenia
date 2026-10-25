#pragma once

#include "NeniaMidiEvent.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace nenia::detail {

class NeniaMidiChannelState;

inline constexpr std::uint32_t kNeniaMidiDefaultTempoUsPerQuarter = 500000;
inline constexpr std::uint64_t kNeniaMidiMaximumFileBytes = 128ULL * 1024ULL * 1024ULL;

class NeniaMidiSequence final {
public:
    bool load(const QString &path, QString *errorMessage = nullptr);
    bool loadMidi(const QByteArray &midi, QString *errorMessage = nullptr);
    void clear() noexcept;

    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] std::uint16_t format() const noexcept;
    [[nodiscard]] std::uint16_t ticksPerQuarterNote() const noexcept;
    [[nodiscard]] std::size_t originalTrackCount() const noexcept;
    [[nodiscard]] const std::vector<NeniaMidiTrackInfo> &tracks() const noexcept;
    [[nodiscard]] const std::vector<NeniaMidiEvent> &events() const noexcept;
    [[nodiscard]] const std::vector<NeniaMidiTempoEvent> &tempoEvents() const noexcept;
    [[nodiscard]] NeniaMidiTick durationTick() const noexcept;
    [[nodiscard]] std::optional<NeniaMidiTick> lastChannelEventTick() const noexcept;

    // Time conversion rounds once at the final result (nearest, ties upward).
    // The exact fractional tick remainder is retained between tempo segments.
    bool tickToMicroseconds(NeniaMidiTick tick,
                           std::uint64_t *microseconds,
                           QString *errorMessage = nullptr) const;
    bool tickToMilliseconds(NeniaMidiTick tick,
                           qint64 *milliseconds,
                           QString *errorMessage = nullptr) const;
    bool tickToSampleFrame(NeniaMidiTick tick,
                           std::uint32_t sampleRate,
                           std::uint64_t *sampleFrame,
                           QString *errorMessage = nullptr) const;

    // stateAtTick(T) replays the stable event prefix with event.tick <= T.
    // It always reconstructs a fresh logical state; it does not cache audio state.
    bool stateAtTick(NeniaMidiTick tick,
                     NeniaMidiChannelState *state,
                     QString *errorMessage = nullptr) const;

private:
    struct TempoSegment {
        NeniaMidiTick startTick = 0;
        std::uint32_t microsecondsPerQuarter = kNeniaMidiDefaultTempoUsPerQuarter;
        std::uint64_t wholeMicroseconds = 0;
        std::uint32_t fractionalTickNumerator = 0;
    };

    bool parse(const QByteArray &midi, QString *errorMessage);
    bool buildTempoMap(QString *errorMessage);
    bool exactTimeAtTick(NeniaMidiTick tick,
                         std::uint64_t *wholeMicroseconds,
                         std::uint32_t *fractionalTickNumerator,
                         QString *errorMessage) const;
    void swap(NeniaMidiSequence &other) noexcept;

    bool m_loaded = false;
    std::uint16_t m_format = 0;
    std::uint16_t m_ticksPerQuarterNote = 0;
    NeniaMidiTick m_durationTick = 0;
    std::optional<NeniaMidiTick> m_lastChannelEventTick;
    std::vector<NeniaMidiTrackInfo> m_tracks;
    std::vector<NeniaMidiEvent> m_events;
    std::vector<NeniaMidiTempoEvent> m_tempoEvents;
    std::vector<TempoSegment> m_tempoSegments;
};

} // namespace nenia::detail
