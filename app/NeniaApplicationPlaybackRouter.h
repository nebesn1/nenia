#pragma once

#include "../src/NeniaAlternatePlaybackBackend.h"
#include "../src/midi_playback_backend/NeniaMidiPlaybackEngine.h"
#include "NeniaMidiMetadata.h"
#include "NeniaSettings.h"
#include "../src/NeniaMidiVisualTimelineProvider.h"

#include <QTimer>

#include <memory>

namespace nenia {
class NeniaPlaybackController;
}

namespace nenia::app {

// Application-only adapter that routes direct local MIDI sources through the
// accepted realtime engine while the existing controller remains the facade
// used by the Playlist and transport actions.
class NeniaApplicationPlaybackRouter final : public detail::NeniaAlternatePlaybackBackend
{
    Q_OBJECT

public:
    explicit NeniaApplicationPlaybackRouter(
        QString soundFontPath,
        detail::NeniaMidiPlaybackEngine::OutputMode outputMode =
            detail::NeniaMidiPlaybackEngine::OutputMode::Device,
        QObject *parent = nullptr,
        NeniaSoundFontConfigSource source = NeniaSoundFontConfigSource::CommandLine,
        detail::NeniaMidiLoudnessPreset preferredUserPreset =
            detail::NeniaMidiLoudnessPreset::E);

    void attachTo(NeniaPlaybackController *controller);

    bool acceptsSource(const QUrl &source) const noexcept override;
    bool prepareSource(const QUrl &source, QString *errorMessage) override;
    bool play(QString *errorMessage) override;
    bool pause(QString *errorMessage) override;
    bool stop(QString *errorMessage) override;
    bool restart(QString *errorMessage) override;
    bool setPosition(qint64 positionMs, QString *errorMessage) override;
    void setMasterVolume(float volume) noexcept override;

    [[nodiscard]] QMediaPlayer::PlaybackState playbackState() const noexcept override;
    [[nodiscard]] QMediaPlayer::MediaStatus mediaStatus() const noexcept override;
    [[nodiscard]] qint64 position() const noexcept override;
    [[nodiscard]] qint64 duration() const noexcept override;
    [[nodiscard]] QString formatDescription() const override;
    [[nodiscard]] QString errorDescription() const override;
    [[nodiscard]] std::shared_ptr<const TimelineEventProvider>
    timelineProvider() const override;
    [[nodiscard]] detail::NeniaAlternateMediaPresentation mediaPresentation() const override;
    [[nodiscard]] int analysisSampleRate() const noexcept override;
    [[nodiscard]] std::uint64_t analysisGeneration() const noexcept override;
    [[nodiscard]] std::uint64_t consumedAnalysisFrame() const noexcept override;
    bool takeEligibleAnalysisBlock(
        detail::NeniaMidiPcmAnalysisBlock *block) noexcept override;

    void setSavedSoundFontPreference(const QString &path);
    [[nodiscard]] NeniaSoundFontConfiguration soundFontConfiguration() const;
    // Internal diagnostic surface retained for regression tests.
    [[nodiscard]] bool realtimeGainAbEnabled() const noexcept;
    [[nodiscard]] double selectedRealtimeMidiGain() const noexcept;
    quint64 requestRealtimeMidiGain(double gain, QString *errorMessage = nullptr);
    [[nodiscard]] bool realtimeLoudnessTrialEnabled() const noexcept;
    [[nodiscard]] bool realtimeLoudnessTrialConfigurationConflict() const noexcept;
    [[nodiscard]] bool userMidiPresetSelectionEnabled() const noexcept;
    [[nodiscard]] bool hasPreparedRealtimeMidiSource() const noexcept;
    bool setPreferredUserMidiLoudnessPreset(
        detail::NeniaMidiLoudnessPreset preset, QString *errorMessage = nullptr);
    [[nodiscard]] detail::NeniaMidiLoudnessPreset selectedRealtimeMidiLoudnessPreset() const noexcept;
    [[nodiscard]] detail::NeniaMidiLoudnessPreset confirmedRealtimeMidiLoudnessPreset() const noexcept;
    quint64 requestRealtimeMidiLoudnessPreset(
        detail::NeniaMidiLoudnessPreset preset, QString *errorMessage = nullptr);
    // Used only by deterministic integration tests.
    [[nodiscard]] detail::NeniaMidiPlaybackEngine &engineForTesting() noexcept;

signals:
    // The GUI no longer subscribes to gain A/B. The signal remains available
    // to the internal worker-confirmation regression tests.
    void realtimeMidiGainChangeCompleted(quint64 requestId,
                                         double requestedGain,
                                         double previousEffectiveGain,
                                         double effectiveGain,
                                         int activeVoicesBefore,
                                         int activeVoicesAfter,
                                         quint64 workerThreadIdentity,
                                         bool succeeded,
                                         const QString &errorMessage);
    void realtimeMidiLoudnessPresetChangeCompleted(
        quint64 requestId, detail::NeniaMidiLoudnessPreset preset,
        double synthGain, double postSynthGainDb, bool limiterEnabled,
        int activeVoicesBefore, int activeVoicesAfter,
        quint64 workerThreadIdentity, bool succeeded,
        const QString &errorMessage);

private:
    void reportAsynchronousFailure();

    NeniaSoundFontConfiguration m_soundFontConfiguration;
    NeniaMidiMetadata m_midiMetadata;
    detail::NeniaAlternateMediaPresentation m_mediaPresentation;
    QString m_lastError;
    detail::NeniaMidiPlaybackEngine m_engine;
    std::shared_ptr<detail::NeniaMidiVisualTimelineProvider> m_timelineProvider;
    QTimer m_healthTimer;
    bool m_directMidiRouteActive = false;
    bool m_reportedEngineError = false;
};

} // namespace nenia::app
