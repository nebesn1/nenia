#include "NeniaApplicationPlaybackRouter.h"

#include <nenia/NeniaPlaybackController.h>

#include "../src/NeniaDirectMidiSource.h"

#include <QFileInfo>

namespace nenia::app {

NeniaApplicationPlaybackRouter::NeniaApplicationPlaybackRouter(
    QString soundFontPath,
    detail::NeniaMidiPlaybackEngine::OutputMode outputMode,
    QObject *parent,
    NeniaSoundFontConfigSource source,
    detail::NeniaMidiLoudnessPreset preferredUserPreset)
    : detail::NeniaAlternatePlaybackBackend(parent)
    , m_soundFontConfiguration{std::move(soundFontPath), source}
    , m_engine(outputMode, 0U, nullptr, preferredUserPreset)
{
    connect(&m_engine, &detail::NeniaMidiPlaybackEngine::endOfMedia,
            this, &detail::NeniaAlternatePlaybackBackend::endOfMedia);
    connect(&m_engine, &detail::NeniaMidiPlaybackEngine::realtimeSynthGainChangeCompleted,
            this, &NeniaApplicationPlaybackRouter::realtimeMidiGainChangeCompleted);
    connect(&m_engine,
            &detail::NeniaMidiPlaybackEngine::realtimeLoudnessPresetChangeCompleted,
            this,
            &NeniaApplicationPlaybackRouter::realtimeMidiLoudnessPresetChangeCompleted);
    m_healthTimer.setInterval(100);
    connect(&m_healthTimer, &QTimer::timeout,
            this, &NeniaApplicationPlaybackRouter::reportAsynchronousFailure);
    m_healthTimer.start();
}

void NeniaApplicationPlaybackRouter::attachTo(NeniaPlaybackController *controller)
{
    if (controller != nullptr) {
        controller->setAlternatePlaybackBackend(this);
    }
}

bool NeniaApplicationPlaybackRouter::acceptsSource(const QUrl &source) const noexcept
{
    return detail::isDirectMidiSource(source);
}

bool NeniaApplicationPlaybackRouter::prepareSource(const QUrl &source,
                                                    QString *errorMessage)
{
    m_directMidiRouteActive = false;
    m_lastError.clear();
    m_reportedEngineError = false;
    m_timelineProvider.reset();
    m_midiMetadata = {};
    m_mediaPresentation = {};
    m_mediaPresentation.extended.directMidi = true;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!acceptsSource(source)) {
        m_lastError = QStringLiteral("Direct MIDI playback accepts local .mid and .midi files only.");
    }
    if (!m_lastError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_lastError;
        }
        return false;
    }

    QString error;
    const QString soundFontPath = m_soundFontConfiguration.path;
    const bool prepared = m_engine.prepare(source.toLocalFile(), soundFontPath, &error);
    const auto parsedSequence = m_engine.sequenceSnapshot();
    if (parsedSequence != nullptr) {
        m_midiMetadata = extractNeniaMidiMetadata(*parsedSequence, source.toLocalFile());
        m_mediaPresentation.primary.title = m_midiMetadata.title;
        m_mediaPresentation.location = m_midiMetadata.sourcePath;
        detail::NeniaExtendedMediaInfo &display = m_mediaPresentation.extended;
        display.directMidi = true;
        display.midiSourcePath = m_midiMetadata.sourcePath;
        display.midiDuration = m_midiMetadata.duration;
        display.midiFormat = m_midiMetadata.format;
        display.midiTrackCount = m_midiMetadata.tracks;
        display.midiChannelCount = m_midiMetadata.channels;
        display.midiPpqn = m_midiMetadata.ppqn;
        display.midiCopyright = m_midiMetadata.copyright;
    } else {
        const QFileInfo sourceInfo(source.toLocalFile());
        m_mediaPresentation.primary.title = sourceInfo.completeBaseName();
        m_mediaPresentation.location = sourceInfo.absoluteFilePath();
        m_mediaPresentation.extended.midiSourcePath = sourceInfo.absoluteFilePath();
        m_mediaPresentation.extended.midiFormat = QStringLiteral("Unavailable");
    }
    if (!prepared) {
        m_lastError = std::move(error);
        if (errorMessage != nullptr) {
            *errorMessage = m_lastError;
        }
        return false;
    }

    auto provider = std::make_shared<detail::NeniaMidiVisualTimelineProvider>();
    if (!provider->loadSequence(parsedSequence, &error)) {
        (void)m_engine.stop();
        m_lastError = QStringLiteral("MIDI visualization projection failed: %1").arg(error);
        if (errorMessage != nullptr) {
            *errorMessage = m_lastError;
        }
        return false;
    }
    m_timelineProvider = std::move(provider);
    m_directMidiRouteActive = true;
    return true;
}

bool NeniaApplicationPlaybackRouter::play(QString *errorMessage)
{
    m_lastError.clear();
    m_reportedEngineError = false;
    const bool ok = m_engine.state() == detail::NeniaMidiPlaybackState::Ended
        ? m_engine.restart(errorMessage)
        : m_engine.play(errorMessage);
    if (ok) {
        m_directMidiRouteActive = true;
    }
    if (!ok && errorMessage != nullptr) {
        m_lastError = *errorMessage;
    }
    return ok;
}

bool NeniaApplicationPlaybackRouter::pause(QString *errorMessage)
{
    const bool ok = m_engine.pause(errorMessage);
    if (!ok && errorMessage != nullptr) {
        m_lastError = *errorMessage;
    }
    return ok;
}

bool NeniaApplicationPlaybackRouter::stop(QString *errorMessage)
{
    m_directMidiRouteActive = false;
    const bool ok = m_engine.stop(errorMessage);
    if (!ok && errorMessage != nullptr) {
        m_lastError = *errorMessage;
    }
    return ok;
}

bool NeniaApplicationPlaybackRouter::restart(QString *errorMessage)
{
    m_lastError.clear();
    m_reportedEngineError = false;
    const bool ok = m_engine.restart(errorMessage);
    if (ok) {
        m_directMidiRouteActive = true;
    }
    if (!ok && errorMessage != nullptr) {
        m_lastError = *errorMessage;
    }
    return ok;
}

bool NeniaApplicationPlaybackRouter::setPosition(qint64 positionMs,
                                                  QString *errorMessage)
{
    const bool ok = m_engine.seekMilliseconds(positionMs, errorMessage);
    if (!ok && errorMessage != nullptr) {
        m_lastError = *errorMessage;
    } else {
        m_lastError.clear();
        m_reportedEngineError = false;
    }
    return ok;
}

void NeniaApplicationPlaybackRouter::setMasterVolume(float volume) noexcept
{
    m_engine.setMasterVolume(volume);
}

QMediaPlayer::PlaybackState
NeniaApplicationPlaybackRouter::playbackState() const noexcept
{
    switch (m_engine.state()) {
    case detail::NeniaMidiPlaybackState::Playing:
    case detail::NeniaMidiPlaybackState::Draining:
        return QMediaPlayer::PlayingState;
    case detail::NeniaMidiPlaybackState::Paused:
        return QMediaPlayer::PausedState;
    case detail::NeniaMidiPlaybackState::Empty:
    case detail::NeniaMidiPlaybackState::Ready:
    case detail::NeniaMidiPlaybackState::Stopped:
    case detail::NeniaMidiPlaybackState::Ended:
    case detail::NeniaMidiPlaybackState::Error:
        return QMediaPlayer::StoppedState;
    }
    return QMediaPlayer::StoppedState;
}

QMediaPlayer::MediaStatus NeniaApplicationPlaybackRouter::mediaStatus() const noexcept
{
    switch (m_engine.state()) {
    case detail::NeniaMidiPlaybackState::Empty:
        return QMediaPlayer::NoMedia;
    case detail::NeniaMidiPlaybackState::Error:
        return QMediaPlayer::InvalidMedia;
    case detail::NeniaMidiPlaybackState::Ended:
        return QMediaPlayer::EndOfMedia;
    case detail::NeniaMidiPlaybackState::Ready:
    case detail::NeniaMidiPlaybackState::Playing:
    case detail::NeniaMidiPlaybackState::Paused:
    case detail::NeniaMidiPlaybackState::Stopped:
    case detail::NeniaMidiPlaybackState::Draining:
        return QMediaPlayer::LoadedMedia;
    }
    return QMediaPlayer::NoMedia;
}

qint64 NeniaApplicationPlaybackRouter::position() const noexcept
{
    return m_engine.positionMilliseconds();
}

qint64 NeniaApplicationPlaybackRouter::duration() const noexcept
{
    return m_engine.durationMilliseconds();
}

QString NeniaApplicationPlaybackRouter::formatDescription() const
{
    return m_midiMetadata.format.isEmpty()
        ? QStringLiteral("Standard MIDI File") : m_midiMetadata.format;
}

QString NeniaApplicationPlaybackRouter::errorDescription() const
{
    if (!m_lastError.isEmpty()) {
        return m_lastError;
    }
    return m_engine.diagnostics().errorMessage;
}

std::shared_ptr<const TimelineEventProvider>
NeniaApplicationPlaybackRouter::timelineProvider() const
{
    return m_timelineProvider;
}

detail::NeniaAlternateMediaPresentation
NeniaApplicationPlaybackRouter::mediaPresentation() const
{
    detail::NeniaAlternateMediaPresentation presentation = m_mediaPresentation;
    presentation.extended.directMidi = true;
    const auto sequence = m_engine.sequenceSnapshot();
    if (sequence != nullptr) {
        presentation.extended.midiTempo = midiTempoAtPosition(
            m_midiMetadata, m_engine.positionMilliseconds());
        presentation.extended.midiTimeSignature = midiTimeSignatureAtPosition(
            m_midiMetadata, m_engine.positionMilliseconds());
    }

    const QString configuredPath = m_soundFontConfiguration.path.trimmed();
    const QString configuredSource =
        soundFontConfigSourceName(m_soundFontConfiguration.source);
    if (configuredPath.isEmpty()) {
        presentation.extended.soundFontNameAndStatus = QStringLiteral("Not configured");
        presentation.extended.soundFontPath = QStringLiteral("Not configured · %1")
            .arg(configuredSource);
    } else {
        const QFileInfo soundFontInfo(configuredPath);
        const QString absolutePath = soundFontInfo.absoluteFilePath();
        const detail::NeniaMidiPlaybackDiagnostics diagnostics = m_engine.diagnostics();
        const QString activePath = diagnostics.soundFontPath;
        const bool successfullyLoaded = sequence != nullptr
            && m_engine.state() != detail::NeniaMidiPlaybackState::Error
            && !activePath.isEmpty();
        const bool nextActivationDiffers = successfullyLoaded
            && QFileInfo(activePath).absoluteFilePath() != absolutePath;
        presentation.extended.soundFontPath = nextActivationDiffers
            ? QStringLiteral("Active: %1 · Next: %2 · %3")
                  .arg(activePath, absolutePath, configuredSource)
            : QStringLiteral("%1 · %2").arg(absolutePath, configuredSource);
        if (sequence == nullptr) {
            presentation.extended.soundFontNameAndStatus =
                QStringLiteral("Not loaded (MIDI preparation failed)");
        } else if (m_engine.state() == detail::NeniaMidiPlaybackState::Error) {
            QString error = diagnostics.errorMessage;
            if (error.isEmpty()) {
                error = m_lastError;
            }
            presentation.extended.soundFontNameAndStatus = error.isEmpty()
                ? QStringLiteral("Load failed · %1").arg(soundFontInfo.fileName())
                : QStringLiteral("Unavailable · %1").arg(error);
        } else if (successfullyLoaded) {
            presentation.extended.soundFontNameAndStatus = nextActivationDiffers
                ? QStringLiteral("Loaded · %1 · next %2")
                      .arg(QFileInfo(activePath).fileName(), soundFontInfo.fileName())
                : QStringLiteral("Loaded · %1").arg(soundFontInfo.fileName());
        } else {
            presentation.extended.soundFontNameAndStatus = QStringLiteral("Configured · %1")
                .arg(soundFontInfo.fileName());
        }
    }
    return presentation;
}

int NeniaApplicationPlaybackRouter::analysisSampleRate() const noexcept
{
    return static_cast<int>(m_engine.diagnostics().sampleRate);
}

std::uint64_t NeniaApplicationPlaybackRouter::analysisGeneration() const noexcept
{
    return m_engine.analysisGeneration();
}

std::uint64_t NeniaApplicationPlaybackRouter::consumedAnalysisFrame() const noexcept
{
    return m_engine.consumedOutputFrame();
}

bool NeniaApplicationPlaybackRouter::takeEligibleAnalysisBlock(
    detail::NeniaMidiPcmAnalysisBlock *block) noexcept
{
    return m_engine.takeEligibleAnalysisBlock(block);
}

void NeniaApplicationPlaybackRouter::setSavedSoundFontPreference(const QString &path)
{
    if (m_soundFontConfiguration.source == NeniaSoundFontConfigSource::CommandLine
        || m_soundFontConfiguration.source == NeniaSoundFontConfigSource::Environment) {
        return;
    }
    const QString normalized = path.trimmed();
    m_soundFontConfiguration = normalized.isEmpty()
        ? NeniaSoundFontConfiguration{}
        : NeniaSoundFontConfiguration{normalized, NeniaSoundFontConfigSource::Preference};
}

NeniaSoundFontConfiguration NeniaApplicationPlaybackRouter::soundFontConfiguration() const
{
    return m_soundFontConfiguration;
}

bool NeniaApplicationPlaybackRouter::realtimeGainAbEnabled() const noexcept
{
    return m_engine.realtimeGainAbEnabled();
}

double NeniaApplicationPlaybackRouter::selectedRealtimeMidiGain() const noexcept
{
    return m_engine.selectedRealtimeSynthGain();
}

quint64 NeniaApplicationPlaybackRouter::requestRealtimeMidiGain(
    double gain, QString *errorMessage)
{
    return m_engine.requestRealtimeSynthGain(gain, errorMessage);
}

bool NeniaApplicationPlaybackRouter::realtimeLoudnessTrialEnabled() const noexcept
{
    return m_engine.realtimeLoudnessTrialEnabled();
}

bool NeniaApplicationPlaybackRouter::realtimeLoudnessTrialConfigurationConflict() const noexcept
{
    return m_engine.realtimeLoudnessTrialConfigurationConflict();
}

bool NeniaApplicationPlaybackRouter::userMidiPresetSelectionEnabled() const noexcept
{
    return m_engine.userMidiPresetSelectionEnabled();
}

bool NeniaApplicationPlaybackRouter::hasPreparedRealtimeMidiSource() const noexcept
{
    using State = detail::NeniaMidiPlaybackState;
    const State current = m_engine.state();
    return m_directMidiRouteActive && m_engine.sequenceSnapshot() != nullptr
        && (current == State::Ready || current == State::Playing
            || current == State::Paused || current == State::Stopped
            || current == State::Draining || current == State::Ended);
}

bool NeniaApplicationPlaybackRouter::setPreferredUserMidiLoudnessPreset(
    detail::NeniaMidiLoudnessPreset preset, QString *errorMessage)
{
    return m_engine.setPreferredUserLoudnessPreset(preset, errorMessage);
}

detail::NeniaMidiLoudnessPreset
NeniaApplicationPlaybackRouter::selectedRealtimeMidiLoudnessPreset() const noexcept
{
    return m_engine.selectedLoudnessPreset();
}

detail::NeniaMidiLoudnessPreset
NeniaApplicationPlaybackRouter::confirmedRealtimeMidiLoudnessPreset() const noexcept
{
    return m_engine.confirmedLoudnessPreset();
}

quint64 NeniaApplicationPlaybackRouter::requestRealtimeMidiLoudnessPreset(
    detail::NeniaMidiLoudnessPreset preset, QString *errorMessage)
{
    if (!m_directMidiRouteActive) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "live MIDI preset changes require the active Direct MIDI route");
        }
        return 0;
    }
    return m_engine.requestRealtimeLoudnessPreset(preset, errorMessage);
}

detail::NeniaMidiPlaybackEngine &
NeniaApplicationPlaybackRouter::engineForTesting() noexcept
{
    return m_engine;
}

void NeniaApplicationPlaybackRouter::reportAsynchronousFailure()
{
    if (m_engine.state() != detail::NeniaMidiPlaybackState::Error
        || m_reportedEngineError) {
        return;
    }
    m_reportedEngineError = true;
    m_lastError = m_engine.diagnostics().errorMessage;
    emit backendError(m_lastError.isEmpty()
                          ? QStringLiteral("MIDI output failed unexpectedly")
                          : m_lastError);
}

} // namespace nenia::app
