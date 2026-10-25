#include "NeniaSoundFontSynthesizer.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <fluidsynth.h>
#include <fluidsynth/synth.h>
#include <fluidsynth/settings.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace nenia::detail {
namespace {

constexpr int kRenderChunkFrames = 4096;

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validChannel(int channel)
{
    return channel >= 0 && channel < kOfflineSynthesisMidiChannels;
}

bool apiSucceeded(int result)
{
    return result == FLUID_OK;
}

} // namespace

NeniaSoundFontSynthesizer::~NeniaSoundFontSynthesizer()
{
    shutdown();
}

bool NeniaSoundFontSynthesizer::initialize(QString *errorMessage)
{
    return initialize(kOfflineSynthesisSampleRate, false, errorMessage);
}

bool NeniaSoundFontSynthesizer::initialize(int sampleRate,
                                            bool enableEffects,
                                            QString *errorMessage)
{
    return initialize(sampleRate, enableEffects, 0.2, errorMessage);
}

bool NeniaSoundFontSynthesizer::initialize(int sampleRate,
                                            bool enableEffects,
                                            double gain,
                                            QString *errorMessage,
                                            NeniaSoundFontGainReadback *gainReadback)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (sampleRate <= 0) {
        setError(errorMessage, QStringLiteral("sample rate must be positive"));
        return false;
    }
    if (!std::isfinite(gain) || gain < 0.0 || gain > 10.0) {
        setError(errorMessage, QStringLiteral("synthesizer gain must be finite and in 0..10"));
        return false;
    }
    if (m_synth != nullptr && m_settings != nullptr && m_sampleRate == sampleRate
        && m_gain == gain && m_effectsEnabled == enableEffects) {
        return true;
    }

    shutdown();
    m_settings = new_fluid_settings();
    if (m_settings == nullptr) {
        setError(errorMessage, QStringLiteral("FluidSynth could not allocate settings"));
        return false;
    }

    QStringList rejectedSettings;
    if (fluid_settings_setnum(m_settings, "synth.sample-rate",
                              static_cast<double>(sampleRate)) != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.sample-rate"));
    }
    if (fluid_settings_setnum(m_settings, "synth.gain", gain) != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.gain"));
    }
    if (fluid_settings_setint(m_settings, "synth.midi-channels",
                              kOfflineSynthesisMidiChannels) != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.midi-channels"));
    }
    // Make FluidSynth 2.3.5's established GS bank-selection semantics
    // explicit: CC0 selects the bank and CC32 is ignored by this profile.
    if (fluid_settings_setstr(m_settings, "synth.midi-bank-select", "gs") != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.midi-bank-select=gs"));
    }
    if (fluid_settings_setint(m_settings, "synth.reverb.active", enableEffects ? 1 : 0)
        != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.reverb.active"));
    }
    if (enableEffects) {
        if (fluid_settings_setint(m_settings, "synth.chorus.active", 1) != FLUID_OK) {
            rejectedSettings.append(QStringLiteral("synth.chorus.active"));
        }
    } else if (fluid_settings_setint(m_settings, "synth.chorus.active", 0) != FLUID_OK) {
        rejectedSettings.append(QStringLiteral("synth.chorus.active"));
    }
    if (!rejectedSettings.isEmpty()) {
        setError(errorMessage,
                 QStringLiteral("FluidSynth rejected synthesis settings: %1")
                     .arg(rejectedSettings.join(QStringLiteral(", "))));
        shutdown();
        return false;
    }

    double settingsGain = std::numeric_limits<double>::quiet_NaN();
    if (fluid_settings_getnum(m_settings, "synth.gain", &settingsGain) != FLUID_OK
        || !std::isfinite(settingsGain)) {
        setError(errorMessage, QStringLiteral("FluidSynth could not read back synth.gain"));
        shutdown();
        return false;
    }
    m_synth = new_fluid_synth(m_settings);
    if (m_synth == nullptr) {
        setError(errorMessage, QStringLiteral("FluidSynth could not create the synthesizer"));
        shutdown();
        return false;
    }
    const double effectiveSynthGain = static_cast<double>(fluid_synth_get_gain(m_synth));
    if (!std::isfinite(effectiveSynthGain)
        || std::abs(effectiveSynthGain - gain) > 1.0e-6) {
        setError(errorMessage,
                 QStringLiteral("FluidSynth effective gain %1 does not match requested gain %2")
                     .arg(effectiveSynthGain, 0, 'g', 9)
                     .arg(gain, 0, 'g', 9));
        shutdown();
        return false;
    }
    const int effectsEnabled = enableEffects ? 1 : 0;
    if (fluid_synth_reverb_on(m_synth, -1, effectsEnabled) != FLUID_OK
        || fluid_synth_chorus_on(m_synth, -1, effectsEnabled) != FLUID_OK) {
        setError(errorMessage, QStringLiteral("FluidSynth could not configure reverb and chorus"));
        shutdown();
        return false;
    }
    m_sampleRate = sampleRate;
    m_gain = gain;
    m_effectsEnabled = enableEffects;
    if (gainReadback != nullptr) {
        gainReadback->settingsGain = settingsGain;
        gainReadback->effectiveSynthGain = effectiveSynthGain;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::loadSoundFont(const QString &path, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (m_synth == nullptr) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer before loading a SoundFont"));
        return false;
    }
    if (m_soundFontId >= 0) {
        setError(errorMessage, QStringLiteral("this synthesizer already has a loaded SoundFont"));
        return false;
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
        setError(errorMessage, QStringLiteral("SoundFont path is not a readable regular file: %1")
                                   .arg(path));
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(fileInfo.absoluteFilePath());
    const int soundFontId = fluid_synth_sfload(m_synth, encodedPath.constData(), 1);
    if (soundFontId == FLUID_FAILED) {
        setError(errorMessage, QStringLiteral("FluidSynth could not load SoundFont: %1")
                                   .arg(fileInfo.absoluteFilePath()));
        return false;
    }
    const double effectiveGainAfterSoundFont = static_cast<double>(fluid_synth_get_gain(m_synth));
    if (!std::isfinite(effectiveGainAfterSoundFont)
        || std::abs(effectiveGainAfterSoundFont - m_gain) > 1.0e-6) {
        (void)fluid_synth_sfunload(m_synth, soundFontId, 1);
        setError(errorMessage,
                 QStringLiteral("FluidSynth gain changed while loading the SoundFont"));
        return false;
    }
    m_soundFontId = soundFontId;
    return true;
}

bool NeniaSoundFontSynthesizer::programChange(int channel, int program,
                                               QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (program < 0 || program > 127) {
        setError(errorMessage, QStringLiteral("MIDI program must be in the range 0..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_program_change(m_synth, channel, program))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected the program change"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::noteOn(int channel, int key, int velocity,
                                       QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (key < 0 || key > 127) {
        setError(errorMessage, QStringLiteral("MIDI key must be in the range 0..127"));
        return false;
    }
    if (velocity < 1 || velocity > 127) {
        setError(errorMessage, QStringLiteral("note-on velocity must be in the range 1..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_noteon(m_synth, channel, key, velocity))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected the note-on event"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::noteOff(int channel, int key, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (key < 0 || key > 127) {
        setError(errorMessage, QStringLiteral("MIDI key must be in the range 0..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    // A valid MIDI Note Off is a no-op when that key currently has no FluidSynth
    // voice; FluidSynth reports FLUID_FAILED for that ordinary case.
    (void)fluid_synth_noteoff(m_synth, channel, key);
    return true;
}

bool NeniaSoundFontSynthesizer::controlChange(int channel,
                                               int controller,
                                               int value,
                                               QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (controller < 0 || controller > 127 || value < 0 || value > 127) {
        setError(errorMessage, QStringLiteral("MIDI controller and value must be in the range 0..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_cc(m_synth, channel, controller, value))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected the control change"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::pitchBend(int channel,
                                           int value14,
                                           QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (value14 < 0 || value14 > 16383) {
        setError(errorMessage, QStringLiteral("pitch bend must be in the range 0..16383"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_pitch_bend(m_synth, channel, value14))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected the pitch bend"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::channelPressure(int channel,
                                                 int pressure,
                                                 QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (pressure < 0 || pressure > 127) {
        setError(errorMessage, QStringLiteral("channel pressure must be in the range 0..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_channel_pressure(m_synth, channel, pressure))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected channel pressure"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::keyPressure(int channel,
                                             int key,
                                             int pressure,
                                             QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (key < 0 || key > 127 || pressure < 0 || pressure > 127) {
        setError(errorMessage, QStringLiteral("key and pressure must be in the range 0..127"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_key_pressure(m_synth, channel, key, pressure))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected key pressure"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::allSoundsOff(int channel, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validChannel(channel)) {
        setError(errorMessage, QStringLiteral("MIDI channel must be in the range 0..15"));
        return false;
    }
    if (m_synth == nullptr) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_all_sounds_off(m_synth, channel))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected all-sounds-off"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::reset(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (m_synth == nullptr) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer first"));
        return false;
    }
    if (!apiSucceeded(fluid_synth_system_reset(m_synth))) {
        setError(errorMessage, QStringLiteral("FluidSynth rejected system reset"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::setGain(double gain,
                                         double *previousEffectiveGain,
                                         double *effectiveGain,
                                         int *activeVoicesBefore,
                                         int *activeVoicesAfter,
                                         QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!std::isfinite(gain) || gain < 0.0 || gain > 10.0) {
        setError(errorMessage, QStringLiteral("synthesizer gain must be finite and in 0..10"));
        return false;
    }
    if (!hasSoundFont()) {
        setError(errorMessage,
                 QStringLiteral("initialize the synthesizer and load a SoundFont before changing gain"));
        return false;
    }

    const double before = static_cast<double>(fluid_synth_get_gain(m_synth));
    const int voicesBefore = fluid_synth_get_active_voice_count(m_synth);
    if (!std::isfinite(before) || voicesBefore < 0) {
        setError(errorMessage, QStringLiteral("FluidSynth could not read gain or active voices"));
        return false;
    }
    if (previousEffectiveGain != nullptr) {
        *previousEffectiveGain = before;
    }
    if (activeVoicesBefore != nullptr) {
        *activeVoicesBefore = voicesBefore;
    }

    fluid_synth_set_gain(m_synth, static_cast<float>(gain));
    const double after = static_cast<double>(fluid_synth_get_gain(m_synth));
    const int voicesAfter = fluid_synth_get_active_voice_count(m_synth);
    if (std::isfinite(after)) {
        m_gain = after;
    }
    if (effectiveGain != nullptr) {
        *effectiveGain = after;
    }
    if (activeVoicesAfter != nullptr) {
        *activeVoicesAfter = voicesAfter;
    }
    if (!std::isfinite(after) || std::abs(after - gain) > 1.0e-6) {
        setError(errorMessage,
                 QStringLiteral("FluidSynth effective gain %1 does not match requested gain %2")
                     .arg(after, 0, 'g', 9)
                     .arg(gain, 0, 'g', 9));
        return false;
    }
    if (voicesAfter < 0) {
        setError(errorMessage, QStringLiteral("FluidSynth could not read active voices after gain change"));
        return false;
    }
    return true;
}

bool NeniaSoundFontSynthesizer::getControllerValue(
    int channel, int controller, int *value) const noexcept
{
    return m_synth != nullptr && validChannel(channel)
        && controller >= 0 && controller <= 127 && value != nullptr
        && fluid_synth_get_cc(m_synth, channel, controller, value) == FLUID_OK;
}

bool NeniaSoundFontSynthesizer::getPitchBendValue(
    int channel, int *value14) const noexcept
{
    return m_synth != nullptr && validChannel(channel) && value14 != nullptr
        && fluid_synth_get_pitch_bend(m_synth, channel, value14) == FLUID_OK;
}

bool NeniaSoundFontSynthesizer::getPitchWheelSensitivity(
    int channel, int *semitones) const noexcept
{
    return m_synth != nullptr && validChannel(channel) && semitones != nullptr
        && fluid_synth_get_pitch_wheel_sens(m_synth, channel, semitones) == FLUID_OK;
}

bool NeniaSoundFontSynthesizer::getProgramIdentity(
    int channel, int *bank, int *program) const noexcept
{
    if (m_synth == nullptr || !validChannel(channel) || bank == nullptr || program == nullptr) {
        return false;
    }
    int soundFontId = -1;
    return fluid_synth_get_program(m_synth, channel, &soundFontId, bank, program) == FLUID_OK;
}

bool NeniaSoundFontSynthesizer::renderStereoFloat(std::span<float> interleavedSamples,
                                                   QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!hasSoundFont()) {
        setError(errorMessage, QStringLiteral("initialize the synthesizer and load a SoundFont first"));
        return false;
    }
    if ((interleavedSamples.size() % 2U) != 0U) {
        setError(errorMessage, QStringLiteral("stereo output requires an even number of float samples"));
        return false;
    }

    std::size_t frameOffset = 0;
    const std::size_t frameCount = interleavedSamples.size() / 2U;
    while (frameOffset < frameCount) {
        const std::size_t remaining = frameCount - frameOffset;
        const int chunkFrames = static_cast<int>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(kRenderChunkFrames)));
        float *const left = interleavedSamples.data() + frameOffset * 2U;
        float *const right = left + 1;
        if (!apiSucceeded(fluid_synth_write_float(m_synth, chunkFrames,
                                                  left, 0, 2,
                                                  right, 0, 2))) {
            setError(errorMessage, QStringLiteral("FluidSynth failed while rendering float audio"));
            return false;
        }
        frameOffset += static_cast<std::size_t>(chunkFrames);
    }
    return true;
}

bool NeniaSoundFontSynthesizer::isInitialized() const noexcept
{
    return m_synth != nullptr && m_settings != nullptr;
}

bool NeniaSoundFontSynthesizer::hasSoundFont() const noexcept
{
    return isInitialized() && m_soundFontId >= 0;
}

int NeniaSoundFontSynthesizer::sampleRate() const noexcept
{
    return m_sampleRate;
}

void NeniaSoundFontSynthesizer::shutdown() noexcept
{
    if (m_synth != nullptr) {
        delete_fluid_synth(m_synth);
        m_synth = nullptr;
    }
    if (m_settings != nullptr) {
        delete_fluid_settings(m_settings);
        m_settings = nullptr;
    }
    m_soundFontId = -1;
    m_sampleRate = 0;
}

} // namespace nenia::detail
