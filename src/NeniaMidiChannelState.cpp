#include "NeniaMidiChannelState.h"

#include <algorithm>
#include <type_traits>

namespace nenia::detail {

bool NeniaMidiSingleChannelState::sustainDown() const noexcept
{
    return sustainControllerValue.has_value() && sustainControllerValue.value() >= 64;
}

bool NeniaMidiSingleChannelState::hasSelectedRpn(std::uint8_t msb,
                                                  std::uint8_t lsb) const noexcept
{
    return rpnSelectionActive && !nrpnSelectionActive
        && rpnMsb.has_value() && rpnLsb.has_value()
        && rpnMsb.value() == msb && rpnLsb.value() == lsb;
}

std::uint8_t NeniaMidiSingleChannelState::effectiveProgram() const noexcept
{
    return program.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveBankMsb() const noexcept
{
    return bankMsb.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveBankLsb() const noexcept
{
    return bankLsb.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveProgramBankMsb() const noexcept
{
    return programBankMsb.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveProgramBankLsb() const noexcept
{
    return programBankLsb.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveModulation() const noexcept
{
    return modulation.value_or(0);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveVolume() const noexcept
{
    return volume.value_or(100);
}

std::uint8_t NeniaMidiSingleChannelState::effectivePan() const noexcept
{
    return pan.value_or(64);
}

std::uint8_t NeniaMidiSingleChannelState::effectiveExpression() const noexcept
{
    return expression.value_or(127);
}

std::uint16_t NeniaMidiSingleChannelState::effectivePitchBend() const noexcept
{
    return pitchBend.value_or(8192);
}

std::uint8_t NeniaMidiSingleChannelState::effectivePitchBendSensitivitySemitones() const noexcept
{
    return pitchBendSensitivitySemitones.value_or(2);
}

std::uint8_t NeniaMidiSingleChannelState::effectivePitchBendSensitivityCents() const noexcept
{
    return pitchBendSensitivityCents.value_or(0);
}

std::size_t NeniaMidiSingleChannelState::heldKeyCount(std::uint8_t key) const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        activeNotes.begin(), activeNotes.end(), [key](const NeniaMidiNoteInstance &note) {
            return note.key == key && note.keyHeld;
        }));
}

std::size_t NeniaMidiSingleChannelState::sustainedNoteCount(std::uint8_t key) const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        activeNotes.begin(), activeNotes.end(), [key](const NeniaMidiNoteInstance &note) {
            return note.key == key && !note.keyHeld && note.heldBySustain;
        }));
}

const NeniaMidiSingleChannelState &NeniaMidiChannelState::channel(std::size_t index) const
{
    return m_channels.at(index);
}

NeniaMidiSingleChannelState &NeniaMidiChannelState::channel(std::size_t index)
{
    return m_channels.at(index);
}

void NeniaMidiChannelState::apply(const NeniaMidiEvent &event)
{
    std::visit([&](const auto &value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, NeniaMidiNoteOnEvent>
                      || std::is_same_v<Event, NeniaMidiNoteOffEvent>
                      || std::is_same_v<Event, NeniaMidiPolyPressureEvent>
                      || std::is_same_v<Event, NeniaMidiControlChangeEvent>
                      || std::is_same_v<Event, NeniaMidiProgramChangeEvent>
                      || std::is_same_v<Event, NeniaMidiChannelPressureEvent>
                      || std::is_same_v<Event, NeniaMidiPitchBendEvent>) {
            m_channels.at(value.channel).apply(event);
        }
    }, event.payload);
}

void NeniaMidiSingleChannelState::apply(const NeniaMidiEvent &event)
{
    std::visit([&](const auto &value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, NeniaMidiNoteOnEvent>) {
            if (value.velocity == 0) {
                releaseOneKey(value.key);
            } else {
                activeNotes.push_back({value.key,
                                       value.velocity,
                                       event.tick,
                                       event.sourceTrackIndex,
                                       event.sourceEventIndex,
                                       true,
                                       false});
            }
        } else if constexpr (std::is_same_v<Event, NeniaMidiNoteOffEvent>) {
            releaseOneKey(value.key);
        } else if constexpr (std::is_same_v<Event, NeniaMidiPolyPressureEvent>) {
            polyPressure.at(value.key) = value.pressure;
        } else if constexpr (std::is_same_v<Event, NeniaMidiControlChangeEvent>) {
            applyControlChange(value);
        } else if constexpr (std::is_same_v<Event, NeniaMidiProgramChangeEvent>) {
            program = value.program;
            programBankMsb = effectiveBankMsb();
            programBankLsb = effectiveBankLsb();
        } else if constexpr (std::is_same_v<Event, NeniaMidiChannelPressureEvent>) {
            channelPressure = value.pressure;
        } else if constexpr (std::is_same_v<Event, NeniaMidiPitchBendEvent>) {
            pitchBend = value.value14;
        }
    }, event.payload);
}

void NeniaMidiSingleChannelState::applyControlChange(
    const NeniaMidiControlChangeEvent &event)
{
    const bool sustainWasDown = sustainDown();
    controllerValues.at(event.controller) = event.value;

    switch (event.controller) {
    case 0:
        bankMsb = event.value;
        break;
    case 1:
        modulation = event.value;
        break;
    case 6:
        if (hasSelectedRpn(0, 0)) {
            pitchBendSensitivitySemitones = event.value;
        }
        break;
    case 7:
        volume = event.value;
        break;
    case 10:
        pan = event.value;
        break;
    case 11:
        expression = event.value;
        break;
    case 32:
        bankLsb = event.value;
        break;
    case 38:
        if (hasSelectedRpn(0, 0)) {
            pitchBendSensitivityCents = event.value;
        }
        break;
    case 64:
        sustainControllerValue = event.value;
        if (sustainWasDown && !sustainDown()) {
            releaseSustainHeldNotes();
        }
        break;
    case 96:
        if (hasSelectedRpn(0, 0)) {
            const std::uint8_t current = effectivePitchBendSensitivitySemitones();
            if (current < 127) {
                pitchBendSensitivitySemitones = static_cast<std::uint8_t>(current + 1);
            }
        }
        break;
    case 97:
        if (hasSelectedRpn(0, 0)) {
            const std::uint8_t current = effectivePitchBendSensitivitySemitones();
            if (current > 0) {
                pitchBendSensitivitySemitones = static_cast<std::uint8_t>(current - 1);
            }
        }
        break;
    case 98:
        nrpnLsb = event.value;
        nrpnSelectionActive = !(nrpnMsb == 127 && nrpnLsb == 127);
        rpnSelectionActive = false;
        break;
    case 99:
        nrpnMsb = event.value;
        nrpnSelectionActive = !(nrpnMsb == 127 && nrpnLsb == 127);
        rpnSelectionActive = false;
        break;
    case 100:
        rpnLsb = event.value;
        rpnSelectionActive = !(rpnMsb == 127 && rpnLsb == 127);
        nrpnSelectionActive = false;
        break;
    case 101:
        rpnMsb = event.value;
        rpnSelectionActive = !(rpnMsb == 127 && rpnLsb == 127);
        nrpnSelectionActive = false;
        break;
    case 120:
        clearSoundingNotes();
        break;
    case 121:
        resetControllers();
        break;
    case 123:
        releaseAllKeys();
        break;
    default:
        break;
    }
}

void NeniaMidiSingleChannelState::releaseOneKey(std::uint8_t key)
{
    const auto note = std::find_if(activeNotes.begin(), activeNotes.end(),
                                   [key](const NeniaMidiNoteInstance &candidate) {
                                       return candidate.key == key && candidate.keyHeld;
                                   });
    if (note == activeNotes.end()) {
        return;
    }
    if (sustainDown()) {
        note->keyHeld = false;
        note->heldBySustain = true;
    } else {
        activeNotes.erase(note);
    }
}

void NeniaMidiSingleChannelState::releaseAllKeys()
{
    if (sustainDown()) {
        for (NeniaMidiNoteInstance &note : activeNotes) {
            if (note.keyHeld) {
                note.keyHeld = false;
                note.heldBySustain = true;
            }
        }
        return;
    }
    std::erase_if(activeNotes, [](const NeniaMidiNoteInstance &note) {
        return note.keyHeld;
    });
}

void NeniaMidiSingleChannelState::releaseSustainHeldNotes()
{
    std::erase_if(activeNotes, [](const NeniaMidiNoteInstance &note) {
        return !note.keyHeld && note.heldBySustain;
    });
}

void NeniaMidiSingleChannelState::resetControllers()
{
    // RP-015 resets selected controls, not every MIDI value. Preserve the
    // explicitly non-resettable values (including CC7/CC10, bank, effects,
    // sound controllers, and RPN data); other retained CC state returns to 0.
    for (std::size_t controller = 0; controller < 120; ++controller) {
        if (!controllerValues.at(controller).has_value()) {
            continue;
        }
        const bool preserved = controller == 0 || controller == 6 || controller == 7
            || controller == 10 || controller == 32 || controller == 38
            || (controller >= 70 && controller <= 79)
            || (controller >= 91 && controller <= 95);
        if (!preserved) {
            controllerValues.at(controller) = std::uint8_t{0};
        }
    }
    for (std::size_t controller = 65; controller <= 67; ++controller) {
        if (controllerValues.at(controller).has_value()) {
            controllerValues.at(controller) = std::uint8_t{0};
        }
    }
    if (controllerValues.at(11).has_value()) {
        controllerValues.at(11) = std::uint8_t{127};
    }
    for (std::size_t controller = 98; controller <= 101; ++controller) {
        if (controllerValues.at(controller).has_value()) {
            controllerValues.at(controller) = std::uint8_t{127};
        }
    }

    modulation.reset();
    // Channel Volume and Pan are explicitly excluded by RP-015 and remain at
    // their last received values. A fresh-source fallback is not an in-song
    // CC121 reset value.
    expression.reset();
    sustainControllerValue.reset();
    pitchBend.reset();
    // CC121 nulls the RPN/NRPN selector, not the stored parameter values.
    channelPressure.reset();
    polyPressure.fill(std::nullopt);
    rpnMsb.reset();
    rpnLsb.reset();
    nrpnMsb.reset();
    nrpnLsb.reset();
    rpnSelectionActive = false;
    nrpnSelectionActive = false;
    releaseSustainHeldNotes();
}

void NeniaMidiSingleChannelState::clearSoundingNotes()
{
    activeNotes.clear();
}

} // namespace nenia::detail
