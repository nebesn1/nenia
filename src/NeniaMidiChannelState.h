#pragma once

#include "NeniaMidiEvent.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace nenia::detail {

class NeniaMidiVisualTimelineProvider;

struct NeniaMidiNoteInstance {
    std::uint8_t key = 0;
    std::uint8_t velocity = 0;
    NeniaMidiTick onTick = 0;
    std::size_t sourceTrackIndex = 0;
    std::size_t sourceEventIndex = 0;
    bool keyHeld = true;
    bool heldBySustain = false;
};

struct NeniaMidiSingleChannelState {
    std::optional<std::uint8_t> program;
    std::optional<std::uint8_t> bankMsb;
    std::optional<std::uint8_t> bankLsb;
    // Bank selector active at the most recent Program Change. FluidSynth's
    // selected preset is not changed by a later Bank Select alone.
    std::optional<std::uint8_t> programBankMsb;
    std::optional<std::uint8_t> programBankLsb;
    std::optional<std::uint8_t> modulation;
    std::optional<std::uint8_t> volume;
    std::optional<std::uint8_t> pan;
    std::optional<std::uint8_t> expression;
    std::optional<std::uint8_t> sustainControllerValue;
    std::optional<std::uint16_t> pitchBend;
    std::optional<std::uint8_t> pitchBendSensitivitySemitones;
    std::optional<std::uint8_t> pitchBendSensitivityCents;
    std::optional<std::uint8_t> channelPressure;
    std::array<std::optional<std::uint8_t>, 128> polyPressure{};
    std::array<std::optional<std::uint8_t>, 128> controllerValues{};
    std::vector<NeniaMidiNoteInstance> activeNotes;

    std::optional<std::uint8_t> rpnMsb;
    std::optional<std::uint8_t> rpnLsb;
    std::optional<std::uint8_t> nrpnMsb;
    std::optional<std::uint8_t> nrpnLsb;
    bool rpnSelectionActive = false;
    bool nrpnSelectionActive = false;

    [[nodiscard]] bool sustainDown() const noexcept;
    [[nodiscard]] bool hasSelectedRpn(std::uint8_t msb, std::uint8_t lsb) const noexcept;
    [[nodiscard]] std::uint8_t effectiveProgram() const noexcept;
    [[nodiscard]] std::uint8_t effectiveBankMsb() const noexcept;
    [[nodiscard]] std::uint8_t effectiveBankLsb() const noexcept;
    [[nodiscard]] std::uint8_t effectiveProgramBankMsb() const noexcept;
    [[nodiscard]] std::uint8_t effectiveProgramBankLsb() const noexcept;
    [[nodiscard]] std::uint8_t effectiveModulation() const noexcept;
    [[nodiscard]] std::uint8_t effectiveVolume() const noexcept;
    [[nodiscard]] std::uint8_t effectivePan() const noexcept;
    [[nodiscard]] std::uint8_t effectiveExpression() const noexcept;
    [[nodiscard]] std::uint16_t effectivePitchBend() const noexcept;
    [[nodiscard]] std::uint8_t effectivePitchBendSensitivitySemitones() const noexcept;
    [[nodiscard]] std::uint8_t effectivePitchBendSensitivityCents() const noexcept;
    [[nodiscard]] std::size_t heldKeyCount(std::uint8_t key) const noexcept;
    [[nodiscard]] std::size_t sustainedNoteCount(std::uint8_t key) const noexcept;

private:
    void apply(const NeniaMidiEvent &event);
    void applyControlChange(const NeniaMidiControlChangeEvent &event);
    void releaseOneKey(std::uint8_t key);
    void releaseAllKeys();
    void releaseSustainHeldNotes();
    void resetControllers();
    void clearSoundingNotes();

    friend class NeniaMidiChannelState;
};

class NeniaMidiChannelState final {
public:
    static constexpr std::size_t kChannelCount = 16;

    [[nodiscard]] const NeniaMidiSingleChannelState &channel(std::size_t index) const;
    [[nodiscard]] NeniaMidiSingleChannelState &channel(std::size_t index);

private:
    void apply(const NeniaMidiEvent &event);

    std::array<NeniaMidiSingleChannelState, kChannelCount> m_channels{};

    friend class NeniaMidiSequence;
    friend class NeniaMidiVisualTimelineProvider;
};

} // namespace nenia::detail
