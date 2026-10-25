#pragma once

#include <QString>

#include <fluidsynth/types.h>

#include <span>

namespace nenia::detail {

inline constexpr int kOfflineSynthesisSampleRate = 44100;
inline constexpr int kOfflineSynthesisMidiChannels = 16;

struct NeniaSoundFontGainReadback final {
    double settingsGain = 0.0;
    double effectiveSynthGain = 0.0;
};

class NeniaSoundFontSynthesizer final {
public:
    NeniaSoundFontSynthesizer() = default;
    ~NeniaSoundFontSynthesizer();

    NeniaSoundFontSynthesizer(const NeniaSoundFontSynthesizer &) = delete;
    NeniaSoundFontSynthesizer &operator=(const NeniaSoundFontSynthesizer &) = delete;
    NeniaSoundFontSynthesizer(NeniaSoundFontSynthesizer &&) = delete;
    NeniaSoundFontSynthesizer &operator=(NeniaSoundFontSynthesizer &&) = delete;

    bool initialize(QString *errorMessage = nullptr);
    bool initialize(int sampleRate, bool enableEffects, QString *errorMessage = nullptr);
    bool initialize(int sampleRate, bool enableEffects, double gain,
                    QString *errorMessage = nullptr,
                    NeniaSoundFontGainReadback *gainReadback = nullptr);
    bool loadSoundFont(const QString &path, QString *errorMessage = nullptr);

    bool programChange(int channel, int program, QString *errorMessage = nullptr);
    bool noteOn(int channel, int key, int velocity, QString *errorMessage = nullptr);
    bool noteOff(int channel, int key, QString *errorMessage = nullptr);
    bool controlChange(int channel, int controller, int value, QString *errorMessage = nullptr);
    bool pitchBend(int channel, int value14, QString *errorMessage = nullptr);
    bool channelPressure(int channel, int pressure, QString *errorMessage = nullptr);
    bool keyPressure(int channel, int key, int pressure, QString *errorMessage = nullptr);
    bool allSoundsOff(int channel, QString *errorMessage = nullptr);
    bool reset(QString *errorMessage = nullptr);
    bool setGain(double gain,
                 double *previousEffectiveGain = nullptr,
                 double *effectiveGain = nullptr,
                 int *activeVoicesBefore = nullptr,
                 int *activeVoicesAfter = nullptr,
                 QString *errorMessage = nullptr);
    bool getControllerValue(int channel, int controller, int *value) const noexcept;
    bool getPitchBendValue(int channel, int *value14) const noexcept;
    bool getPitchWheelSensitivity(int channel, int *semitones) const noexcept;
    bool getProgramIdentity(int channel, int *bank, int *program) const noexcept;
    bool renderStereoFloat(std::span<float> interleavedSamples,
                           QString *errorMessage = nullptr);

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool hasSoundFont() const noexcept;
    [[nodiscard]] int sampleRate() const noexcept;

private:
    void shutdown() noexcept;

    fluid_settings_t *m_settings = nullptr;
    fluid_synth_t *m_synth = nullptr;
    int m_soundFontId = -1;
    int m_sampleRate = 0;
    double m_gain = 0.2;
    bool m_effectsEnabled = false;
};

} // namespace nenia::detail
