#include <nenia/NeniaPlaybackController.h>

#include "NeniaTimelineSynchronizer.h"
#include "NeniaAlternatePlaybackBackend.h"
#include "NeniaArtworkLoader.h"
#include "NeniaMediaMetadata.h"
#include "NeniaPerformanceDiagnostics.h"
#include "NeniaTagMetadataReader.h"
#include "NeniaSpectrumAnalyzer.h"
#include "NeniaSpectrumFrameStore.h"
#include "NeniaMidiAudioTrace.h"

#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMetaObject>
#include <QMetaType>
#include <QPixmap>
#include <QDebug>
#include <QTimer>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace nenia {
namespace {

QUrl sourceIdentity(const QUrl &source)
{
    if (source.isEmpty()) {
        return {};
    }
    if (source.isLocalFile()) {
        const QFileInfo fileInfo(source.toLocalFile());
        QString path = fileInfo.canonicalFilePath();
        if (path.isEmpty()) {
            path = fileInfo.absoluteFilePath();
        }
        return QUrl::fromLocalFile(QDir::cleanPath(path));
    }
    return source.adjusted(QUrl::NormalizePathSegments);
}

struct MetadataArtwork {
    QImage image;
    qint64 identity = 0;
};

MetadataArtwork metadataArtwork(const QMediaMetaData &metadata)
{
    const QVariant value = metadata.value(QMediaMetaData::CoverArtImage);
    if (!value.isValid()) {
        return {};
    }

    if (value.metaType().id() == QMetaType::QImage) {
        const QImage image = value.value<QImage>();
        if (!image.isNull()) {
            return {image, image.cacheKey()};
        }
    }
    if (value.metaType().id() == QMetaType::QPixmap) {
        const QPixmap pixmap = value.value<QPixmap>();
        const QImage image = pixmap.toImage();
        if (!image.isNull()) {
            return {image, pixmap.cacheKey()};
        }
    }
    return {};
}

QString decodedSampleFormat(const QAudioFormat &format)
{
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        return QStringLiteral("8-bit");
    case QAudioFormat::Int16:
        return QStringLiteral("16-bit");
    case QAudioFormat::Int32:
        return QStringLiteral("32-bit");
    case QAudioFormat::Float:
        return QStringLiteral("float");
    case QAudioFormat::Unknown:
        break;
    default:
        break;
    }
    return {};
}

QString decodedSampleRate(int sampleRate)
{
    if (sampleRate <= 0) {
        return {};
    }

    QString rate = QString::number(static_cast<double>(sampleRate) / 1'000.0, 'f', 2);
    while (rate.endsWith(QLatin1Char('0'))) {
        rate.chop(1);
    }
    if (rate.endsWith(QLatin1Char('.'))) {
        rate.chop(1);
    }
    return rate + QStringLiteral("kHz");
}

QString decodedChannelLayout(int channelCount)
{
    if (channelCount == 1) {
        return QStringLiteral("mono");
    }
    if (channelCount == 2) {
        return QStringLiteral("stereo");
    }
    if (channelCount > 0) {
        return QStringLiteral("%1ch").arg(channelCount);
    }
    return {};
}

QString decodedAudioFormat(const QAudioFormat &format)
{
    const QString sample = decodedSampleFormat(format);
    const QString rate = decodedSampleRate(format.sampleRate());
    const QString channels = decodedChannelLayout(format.channelCount());
    if (sample.isEmpty() || rate.isEmpty() || channels.isEmpty()) {
        return {};
    }
    return QStringLiteral("PCM/%1/%2/%3").arg(sample, rate, channels);
}

detail::NeniaPerformanceDiagnostics::PlaybackState diagnosticPlaybackState(
    const QString &state)
{
    const QString normalized = state.trimmed().toUpper();
    if (normalized == QStringLiteral("PLAYING")) {
        return detail::NeniaPerformanceDiagnostics::PlaybackState::Playing;
    }
    if (normalized == QStringLiteral("PAUSED")) {
        return detail::NeniaPerformanceDiagnostics::PlaybackState::Paused;
    }
    if (normalized == QStringLiteral("STOPPED") || normalized == QStringLiteral("READY")
        || normalized == QStringLiteral("ERROR")) {
        return detail::NeniaPerformanceDiagnostics::PlaybackState::Stopped;
    }
    return detail::NeniaPerformanceDiagnostics::PlaybackState::Unknown;
}

QString playbackDisplayState(const QMediaPlayer &player,
                             QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::PlayingState:
        return QStringLiteral("PLAYING");
    case QMediaPlayer::PausedState:
        return QStringLiteral("PAUSED");
    case QMediaPlayer::StoppedState:
        if (player.source().isEmpty()
            || player.mediaStatus() == QMediaPlayer::NoMedia
            || player.mediaStatus() == QMediaPlayer::LoadingMedia) {
            return QStringLiteral("READY");
        }
        return QStringLiteral("STOPPED");
    }
    return QStringLiteral("READY");
}

QString playbackDisplayState(const QMediaPlayer &player)
{
    return playbackDisplayState(player, player.playbackState());
}

QString playbackDisplayState(QMediaPlayer::PlaybackState state,
                             QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::InvalidMedia) {
        return QStringLiteral("ERROR");
    }
    switch (state) {
    case QMediaPlayer::PlayingState:
        return QStringLiteral("PLAYING");
    case QMediaPlayer::PausedState:
        return QStringLiteral("PAUSED");
    case QMediaPlayer::StoppedState:
        return status == QMediaPlayer::NoMedia || status == QMediaPlayer::LoadingMedia
            ? QStringLiteral("READY") : QStringLiteral("STOPPED");
    }
    return QStringLiteral("READY");
}

} // namespace

class NeniaPlaybackController::Runtime final
{
public:
    explicit Runtime(detail::NeniaAnimationEngine *engine)
        : synchronizer(engine)
    {
#ifdef NENIA_TEST_HOOKS_ENABLED
        if (!qEnvironmentVariableIsSet("NENIA_TEST_NO_AUDIO_OUTPUT")) {
            audioOutput = std::make_unique<QAudioOutput>();
        }
#else
        audioOutput = std::make_unique<QAudioOutput>();
#endif
        if (audioOutput) {
            audioOutput->setVolume(volume);
            player.setAudioOutput(audioOutput.get());
        }
        player.setAudioBufferOutput(&audioBufferOutput);
        syncTimer.setInterval(16);
    }

    void startSyncTimer()
    {
        auto &diagnostics = detail::NeniaPerformanceDiagnostics::instance();
        if (diagnostics.enabled()) {
            syncDiagnosticsClock.start();
        } else {
            syncDiagnosticsClock.invalidate();
        }
        syncTimer.start();
    }

    void stopSyncTimer()
    {
        syncTimer.stop();
        syncDiagnosticsClock.invalidate();
    }

    void recordSyncTimerTick()
    {
        auto &diagnostics = detail::NeniaPerformanceDiagnostics::instance();
        if (!diagnostics.enabled() || !syncDiagnosticsClock.isValid()) {
            return;
        }
        const qint64 actualInterval = syncDiagnosticsClock.nsecsElapsed();
        syncDiagnosticsClock.restart();
        if (actualInterval > 0) {
            diagnostics.recordTimerInterval(
                detail::NeniaPerformanceDiagnostics::Timing::PlaybackSyncInterval,
                detail::NeniaPerformanceDiagnostics::Timing::PlaybackSyncLateness,
                static_cast<std::uint64_t>(actualInterval), 16'000'000U);
        }
    }

    std::unique_ptr<QAudioOutput> audioOutput;
    QMediaPlayer player;
    QAudioBufferOutput audioBufferOutput;
    detail::NeniaSpectrumAnalyzer spectrumAnalyzer;
    detail::NeniaSpectrumFrameStore spectrumFrameStore;
    std::optional<detail::SpectrumFrame> midiSpectrumFrame;
    std::uint64_t midiSpectrumGeneration = 0;
    detail::NeniaRichMediaMetadata richMediaMetadata;
    QImage externalArtwork;
    QImage lastMetadataArtwork;
    qint64 lastMetadataArtworkIdentity = 0;
    QTimer syncTimer;
    QElapsedTimer syncDiagnosticsClock;
    detail::NeniaTimelineSynchronizer synchronizer;
    QPointer<detail::NeniaAlternatePlaybackBackend> alternateBackend;
    QUrl currentSource;
    std::vector<QMetaObject::Connection> audioConnections;
    std::uint64_t sourceGeneration = 0;
    qint64 lastAlternatePosition = -1;
    qint64 lastAlternateDuration = -1;
    QMediaPlayer::PlaybackState lastAlternateState = QMediaPlayer::StoppedState;
    QMediaPlayer::MediaStatus lastAlternateStatus = QMediaPlayer::NoMedia;
    bool alternateActive = false;
    bool alternateFailed = false;
    bool alternateErrorReported = false;
    bool audioErrorActive = false;
    QString decodedFormat;
    float volume = 0.7f;
};

NeniaPlaybackController::NeniaPlaybackController(NeniaWidget *widget, QObject *parent)
    : QObject(parent)
    , m_widget(widget)
    , m_runtime(std::make_unique<Runtime>(
          widget != nullptr
              ? static_cast<detail::NeniaAnimationEngine *>(
                    widget->externalAnimationEngine())
              : nullptr))
{
    if (m_widget != nullptr) {
        m_widget->beginExternalClockControl();
        m_widget->resetVisualization();
        connect(m_widget, &QObject::destroyed, this, [this] {
            m_runtime->stopSyncTimer();
            m_runtime->synchronizer.setEngine(nullptr);
        });
    }

    connectAudioSignalsForCurrentSource();
    connect(&m_runtime->syncTimer, &QTimer::timeout, this,
            [this] {
                m_runtime->recordSyncTimerTick();
                if (m_runtime->alternateActive) {
                    refreshAlternatePlayback();
                } else {
                    synchronizeToCurrentPosition();
                }
            });

    updatePlaybackDisplay();
}

NeniaPlaybackController::~NeniaPlaybackController()
{
    QObject::disconnect(&m_runtime->player, nullptr, this, nullptr);
    if (m_runtime->alternateBackend != nullptr) {
        QObject::disconnect(m_runtime->alternateBackend, nullptr, this, nullptr);
        if (m_runtime->alternateActive) {
            (void)m_runtime->alternateBackend->stop(nullptr);
        }
    }
    m_runtime->stopSyncTimer();
    m_runtime->player.stop();

    if (m_widget != nullptr) {
        m_widget->endExternalClockControl();
    }
}

void NeniaPlaybackController::setSource(const QUrl &source)
{
    detail::NeniaPerformanceDiagnostics &diagnostics =
        detail::NeniaPerformanceDiagnostics::instance();
    diagnostics.setSourceType(detail::NeniaPerformanceDiagnostics::sourceTypeForUrl(source));
    detail::ScopedPerformanceTimer sourceTimer(
        detail::NeniaPerformanceDiagnostics::Timing::SetSource);
    const bool sameSource = isSameSource(source);
    const bool wantsAlternate = m_runtime->alternateBackend != nullptr
        && m_runtime->alternateBackend->acceptsSource(source);
    if (detail::neniaMidiAudioTraceEnabled()) {
        const QString sourceName = source.isLocalFile()
            ? QFileInfo(source.toLocalFile()).fileName() : source.fileName();
        qInfo() << "[NENIA MIDI AUDIO]"
                << "event=source-route"
                << "pid=" << QCoreApplication::applicationPid()
                << "source=" << sourceName
                << "playback_backend=" << (wantsAlternate ? "direct-midi" : "encoded-audio")
                << "midi_sidecar=visualization-only-if-present";
    }
    const bool wasAlternate = m_runtime->alternateActive;
    const QImage previousExternalArtwork = m_runtime->externalArtwork;
    ++m_runtime->sourceGeneration;
    connectAudioSignalsForCurrentSource();
    m_atNaturalEnd = false;
    m_runtime->audioErrorActive = false;
    m_runtime->stopSyncTimer();
    if (wasAlternate && m_runtime->alternateBackend != nullptr) {
        (void)m_runtime->alternateBackend->stop(nullptr);
    }
    // Mark the new route before stopping/replacing QMediaPlayer so any delayed
    // signals from the previous audio source cannot become MIDI UI/EOM events.
    m_runtime->alternateActive = wantsAlternate;
    m_runtime->alternateFailed = false;
    m_runtime->alternateErrorReported = false;
    if (wantsAlternate) {
        // Force facade notifications for the new MIDI source even when its
        // state, duration, or initial position match the previous sequence.
        m_runtime->lastAlternatePosition = -1;
        m_runtime->lastAlternateDuration = -1;
        m_runtime->lastAlternateState = QMediaPlayer::PlayingState;
        m_runtime->lastAlternateStatus = QMediaPlayer::InvalidMedia;
    }
    m_runtime->currentSource = source;
    m_runtime->player.stop();
    m_runtime->spectrumFrameStore.clear();
    m_runtime->spectrumAnalyzer.resetStream();
    if (!sameSource && (wantsAlternate || wasAlternate)) {
        m_runtime->synchronizer.clearProvider();
    }
    resetVisualSynchronization();
    if (!sameSource) {
        m_runtime->decodedFormat.clear();
        m_runtime->richMediaMetadata = {};
        resetAutomaticPresentation();
        if (!wantsAlternate) {
            const detail::NeniaTagReadResult tagRead = [&] {
                detail::ScopedPerformanceTimer metadataTimer(
                    detail::NeniaPerformanceDiagnostics::Timing::MetadataRead);
                return detail::readTagMetadata(source);
            }();
            m_runtime->richMediaMetadata = tagRead.metadata;
            if (tagRead.attempted && !tagRead.success && !tagRead.diagnostic.isEmpty()) {
                qWarning().noquote()
                    << QStringLiteral("Nenia: rich metadata unavailable for %1: %2")
                           .arg(source.toLocalFile(), tagRead.diagnostic);
            }
            if (m_widget != nullptr) {
                const detail::NeniaAutomaticMediaPresentation initial =
                    detail::automaticMediaPresentation(source, {},
                                                       m_runtime->richMediaMetadata);
                m_widget->setAutomaticMediaInfo(initial.primary);
                m_widget->setAutomaticMediaDetails(initial.extended, initial.location);
            }
        } else {
            m_runtime->decodedFormat = QStringLiteral("Standard MIDI File / MIDI");
            if (m_widget != nullptr) {
                const detail::NeniaAutomaticMediaPresentation initial =
                    detail::automaticMediaPresentation(source, {}, {});
                m_widget->setAutomaticMediaInfo(initial.primary);
                m_widget->setAutomaticMediaDetails(initial.extended, initial.location);
            }
        }
    }

    if (m_widget != nullptr) {
        const QSize artworkDecodeTarget = detail::externalArtworkDecodeTarget(
            m_widget->size(), m_widget->devicePixelRatioF());
        m_runtime->externalArtwork =
            detail::resolveLocalArtwork(source, artworkDecodeTarget).image;
        const bool canReuseCurrentEmbeddedArtwork = !wantsAlternate && sameSource;
        if (canReuseCurrentEmbeddedArtwork) {
            updateAutomaticArtwork(previousExternalArtwork.isNull());
        } else {
            m_runtime->lastMetadataArtwork = {};
            m_runtime->lastMetadataArtworkIdentity = 0;
            m_widget->setAutomaticArtwork(m_runtime->externalArtwork);
        }
    } else {
        m_runtime->externalArtwork = {};
    }

    if (wantsAlternate) {
        m_runtime->player.setSource({});
        QString error;
        if (!m_runtime->alternateBackend->prepareSource(source, &error)) {
            m_runtime->alternateFailed = true;
            m_runtime->alternateErrorReported = false;
            updateAlternateMediaPresentation();
            handleAlternateError(error);
        } else {
            m_runtime->alternateFailed = false;
            m_runtime->alternateErrorReported = false;
            updateAlternateMediaPresentation();
            setTimelineProvider(m_runtime->alternateBackend->timelineProvider());
            refreshAlternatePlayback();
            emit mediaStatusChanged(m_runtime->alternateBackend->mediaStatus());
        }
    } else {
        if (!sameSource || m_runtime->player.source() != source) {
            m_runtime->player.setSource(source);
        }
        updatePlaybackDisplay(0, m_runtime->player.duration());
        if (sameSource) {
            // Same-source activation restarts from the beginning while keeping
            // its attached visual provider; reconstruct its time-zero state.
            synchronizeTo(0);
        }
    }
}

QUrl NeniaPlaybackController::source() const
{
    return m_runtime->currentSource;
}

bool NeniaPlaybackController::isSameSource(const QUrl &source) const
{
    const QUrl &currentSource = m_runtime->currentSource;
    return !source.isEmpty() && !currentSource.isEmpty()
        && sourceIdentity(source) == sourceIdentity(currentSource);
}

void NeniaPlaybackController::setAlternatePlaybackBackend(
    detail::NeniaAlternatePlaybackBackend *backend)
{
    if (m_runtime->alternateBackend == backend) {
        return;
    }
    if (m_runtime->alternateBackend != nullptr) {
        QObject::disconnect(m_runtime->alternateBackend, nullptr, this, nullptr);
        if (m_runtime->alternateActive) {
            (void)m_runtime->alternateBackend->stop(nullptr);
        }
    }
    m_runtime->alternateBackend = backend;
    m_runtime->alternateActive = false;
    m_runtime->alternateFailed = false;
    m_runtime->alternateErrorReported = false;
    if (backend != nullptr) {
        backend->setMasterVolume(m_runtime->volume);
        connect(backend, &QObject::destroyed, this, [this] {
            if (!m_runtime || !m_runtime->alternateActive) {
                return;
            }
            m_runtime->alternateBackend = nullptr;
            handleAlternateError(QStringLiteral("The MIDI playback backend was destroyed."));
        });
        connect(backend, &detail::NeniaAlternatePlaybackBackend::endOfMedia,
                this, &NeniaPlaybackController::handleAlternateEndOfMedia);
        connect(backend, &detail::NeniaAlternatePlaybackBackend::backendError,
                this, &NeniaPlaybackController::handleAlternateError);
    }
}

void NeniaPlaybackController::connectAudioSignalsForCurrentSource()
{
    for (const QMetaObject::Connection &connection : m_runtime->audioConnections) {
        QObject::disconnect(connection);
    }
    m_runtime->audioConnections.clear();
    const std::uint64_t generation = m_runtime->sourceGeneration;
    const auto isCurrentAudioGeneration = [this, generation] {
        return generation == m_runtime->sourceGeneration
            && !m_runtime->alternateActive;
    };

    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::positionChanged, this,
        [this, isCurrentAudioGeneration](qint64 positionMs) {
            if (isCurrentAudioGeneration()) {
                handlePositionChanged(positionMs);
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::durationChanged, this,
        [this, isCurrentAudioGeneration](qint64 durationMs) {
            if (isCurrentAudioGeneration()) {
                handleDurationChanged(durationMs);
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::playbackStateChanged, this,
        [this, isCurrentAudioGeneration](QMediaPlayer::PlaybackState state) {
            if (isCurrentAudioGeneration()) {
                handlePlaybackStateChanged(state);
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::mediaStatusChanged, this,
        [this, isCurrentAudioGeneration](QMediaPlayer::MediaStatus status) {
            if (isCurrentAudioGeneration()) {
                handleMediaStatusChanged(status,
                                         static_cast<quint64>(m_runtime->sourceGeneration));
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::metaDataChanged, this,
        [this, isCurrentAudioGeneration] {
            if (isCurrentAudioGeneration()) {
                handleMetaDataChanged();
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->player, &QMediaPlayer::errorOccurred, this,
        [this, isCurrentAudioGeneration](QMediaPlayer::Error error,
                                         const QString &errorString) {
            if (isCurrentAudioGeneration()) {
                handleErrorOccurred(error, errorString);
            }
        }));
    m_runtime->audioConnections.push_back(connect(
        &m_runtime->audioBufferOutput, &QAudioBufferOutput::audioBufferReceived, this,
        [this, isCurrentAudioGeneration](const QAudioBuffer &buffer) {
            if (isCurrentAudioGeneration()) {
                handleAudioBufferReceived(buffer);
            }
        }));
}

void NeniaPlaybackController::refreshAlternatePlayback(bool emitTimeSignals)
{
    if (!m_runtime->alternateActive || m_runtime->alternateBackend == nullptr) {
        return;
    }

    const qint64 positionMs = m_runtime->alternateBackend->position();
    const qint64 durationMs = m_runtime->alternateBackend->duration();
    const QMediaPlayer::PlaybackState state = m_runtime->alternateFailed
        ? QMediaPlayer::StoppedState : m_runtime->alternateBackend->playbackState();
    const QMediaPlayer::MediaStatus status = m_runtime->alternateFailed
        ? QMediaPlayer::InvalidMedia : m_runtime->alternateBackend->mediaStatus();

    updateAlternateMediaPresentation();
    (void)processAlternateAnalysisPcm();

    if (emitTimeSignals && positionMs != m_runtime->lastAlternatePosition) {
        m_runtime->lastAlternatePosition = positionMs;
        emit positionChanged(positionMs);
    }
    if (emitTimeSignals && durationMs != m_runtime->lastAlternateDuration) {
        m_runtime->lastAlternateDuration = durationMs;
        emit durationChanged(durationMs);
    }
    if (state != m_runtime->lastAlternateState) {
        m_runtime->lastAlternateState = state;
        emit playbackStateChanged(state);
    }
    if (status != m_runtime->lastAlternateStatus) {
        m_runtime->lastAlternateStatus = status;
        emit mediaStatusChanged(status);
    }

    const QString displayState = playbackDisplayState(state, status);
    updatePlaybackDisplay(positionMs, durationMs, displayState);
    if (m_atNaturalEnd) {
        return;
    }
    if (state == QMediaPlayer::PlayingState) {
        synchronizeTo(positionMs);
        m_runtime->startSyncTimer();
    } else if (state == QMediaPlayer::PausedState) {
        m_runtime->stopSyncTimer();
        synchronizeTo(positionMs);
    } else {
        m_runtime->stopSyncTimer();
        synchronizeTo(positionMs);
    }

    if (status == QMediaPlayer::EndOfMedia) {
        handleAlternateEndOfMedia();
    } else if (status == QMediaPlayer::InvalidMedia && !m_runtime->alternateErrorReported) {
        handleAlternateError(m_runtime->alternateBackend->errorDescription());
    }
}

void NeniaPlaybackController::handleAlternateEndOfMedia()
{
    if (!m_runtime->alternateActive || m_runtime->alternateFailed || m_atNaturalEnd) {
        return;
    }
    if (m_runtime->lastAlternateStatus != QMediaPlayer::EndOfMedia) {
        m_runtime->lastAlternateStatus = QMediaPlayer::EndOfMedia;
        emit mediaStatusChanged(QMediaPlayer::EndOfMedia);
    }
    handleEndOfMedia(m_runtime->alternateBackend != nullptr
                         ? m_runtime->alternateBackend->position() : 0);
}

void NeniaPlaybackController::handleAlternateError(const QString &message)
{
    if (!m_runtime->alternateActive || m_runtime->alternateErrorReported) {
        return;
    }
    m_runtime->alternateFailed = true;
    m_runtime->alternateErrorReported = true;
    m_atNaturalEnd = false;
    m_runtime->stopSyncTimer();
    resetVisualSynchronization();
    const QString detail = message.trimmed().isEmpty()
        ? QStringLiteral("Unknown MIDI playback failure") : message.trimmed();
    const QString actionable = QStringLiteral(
        "MIDI playback failed for %1: %2. Choose a local SoundFont in Settings, pass --sf2 <path>, "
        "or set NENIA_SOUNDFONT.")
                                   .arg(m_runtime->currentSource.toLocalFile(), detail);
    m_runtime->lastAlternateStatus = QMediaPlayer::InvalidMedia;
    m_runtime->lastAlternateState = QMediaPlayer::StoppedState;
    emit errorOccurred(QMediaPlayer::ResourceError, actionable);
    emit playbackStateChanged(QMediaPlayer::StoppedState);
    emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
    updatePlaybackDisplay(position(), duration(), QStringLiteral("ERROR"));
}

void NeniaPlaybackController::setTimelineProvider(
    std::shared_ptr<const TimelineEventProvider> provider)
{
    m_atNaturalEnd = false;
    m_runtime->synchronizer.setProvider(std::move(provider));
    synchronizeToCurrentPosition();
}

void NeniaPlaybackController::clearTimelineProvider()
{
    m_atNaturalEnd = false;
    m_runtime->synchronizer.clearProvider();
    if (m_widget != nullptr) {
        m_widget->resetVisualization();
    }
}

void NeniaPlaybackController::play()
{
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateBackend == nullptr || m_runtime->alternateFailed) {
            handleAlternateError(QStringLiteral("MIDI source is not ready for playback."));
            return;
        }
        QString error;
        const bool ok = m_atNaturalEnd
            ? m_runtime->alternateBackend->restart(&error)
            : m_runtime->alternateBackend->play(&error);
        if (!ok) {
            handleAlternateError(error);
            return;
        }
        m_atNaturalEnd = false;
        refreshAlternatePlayback();
        return;
    }
    const qint64 currentPositionMs = m_runtime->player.position();
    const qint64 mediaDurationMs = m_runtime->player.duration();
    if (m_atNaturalEnd || m_runtime->player.mediaStatus() == QMediaPlayer::EndOfMedia
        || (mediaDurationMs > 0 && currentPositionMs >= mediaDurationMs)) {
        m_atNaturalEnd = false;
        ++m_runtime->sourceGeneration;
        connectAudioSignalsForCurrentSource();
        resetVisualSynchronization();
        m_runtime->player.setPosition(0);
    }

    synchronizeToCurrentPosition();
    m_runtime->player.play();
    updatePlaybackDisplay();
}

void NeniaPlaybackController::pause()
{
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateBackend == nullptr || m_runtime->alternateFailed) {
            return;
        }
        synchronizeToCurrentPosition();
        QString error;
        if (!m_runtime->alternateBackend->pause(&error)) {
            handleAlternateError(error);
            return;
        }
        refreshAlternatePlayback();
        return;
    }
    synchronizeToCurrentPosition();
    m_runtime->player.pause();
    updatePlaybackDisplay();
}

void NeniaPlaybackController::stop()
{
    m_atNaturalEnd = false;
    m_runtime->stopSyncTimer();
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateBackend != nullptr) {
            QString error;
            if (!m_runtime->alternateBackend->stop(&error)) {
                handleAlternateError(error);
                return;
            }
        }
        refreshAlternatePlayback();
        resetVisualSynchronization();
        return;
    }
    m_runtime->player.stop();
    resetVisualSynchronization();
    updatePlaybackDisplay(0,
                          m_runtime->player.duration(),
                          playbackDisplayState(m_runtime->player, QMediaPlayer::StoppedState));
}

void NeniaPlaybackController::restart()
{
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateBackend == nullptr || m_runtime->alternateFailed) {
            handleAlternateError(QStringLiteral("MIDI source is not ready to restart."));
            return;
        }
        m_atNaturalEnd = false;
        m_runtime->stopSyncTimer();
        resetVisualSynchronization();
        QString error;
        if (!m_runtime->alternateBackend->restart(&error)) {
            handleAlternateError(error);
            return;
        }
        if (m_widget != nullptr) {
            m_widget->refreshPresentation();
        }
        refreshAlternatePlayback();
        synchronizeToCurrentPosition();
        return;
    }
    stop();
    ++m_runtime->sourceGeneration;
    connectAudioSignalsForCurrentSource();
    m_runtime->player.setPosition(0);
    if (m_widget != nullptr) {
        // Same-source playlist reactivation keeps metadata/artwork intact but
        // starts any overflowing metadata marquee from its initial hold.
        m_widget->refreshPresentation();
    }
    synchronizeToCurrentPosition();
    m_runtime->player.play();
    updatePlaybackDisplay();
}

void NeniaPlaybackController::setPosition(qint64 positionMs)
{
    m_atNaturalEnd = false;
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateBackend == nullptr || m_runtime->alternateFailed) {
            return;
        }
        const qint64 target = std::clamp<qint64>(positionMs, 0,
                                                 m_runtime->alternateBackend->duration());
        m_runtime->spectrumAnalyzer.resetStream();
        m_runtime->midiSpectrumFrame.reset();
        QString error;
        if (!m_runtime->alternateBackend->setPosition(target, &error)) {
            handleAlternateError(error);
            return;
        }
        refreshAlternatePlayback();
        return;
    }
    qint64 targetPositionMs = std::max<qint64>(positionMs, 0);
    const qint64 mediaDurationMs = m_runtime->player.duration();
    if (mediaDurationMs > 0) {
        targetPositionMs = std::min(targetPositionMs, mediaDurationMs);
    }

    // A decoded buffer arriving after this call belongs to the new media
    // position. Never allow a rolling FFT window to cross the seek boundary.
    m_runtime->spectrumAnalyzer.resetStream();
    m_runtime->player.setPosition(targetPositionMs);
    synchronizeToCurrentPosition();
    updatePlaybackDisplay(targetPositionMs, mediaDurationMs);
}

void NeniaPlaybackController::seekBy(qint64 deltaMs)
{
    const qint64 currentPositionMs = position();
    qint64 requestedPositionMs = currentPositionMs;
    if (deltaMs > 0
        && currentPositionMs > std::numeric_limits<qint64>::max() - deltaMs) {
        requestedPositionMs = std::numeric_limits<qint64>::max();
    } else if (deltaMs < 0
               && currentPositionMs < std::numeric_limits<qint64>::min() - deltaMs) {
        requestedPositionMs = std::numeric_limits<qint64>::min();
    } else {
        requestedPositionMs += deltaMs;
    }

    if (m_runtime->alternateActive || deltaMs <= 0 || m_atNaturalEnd) {
        setPosition(requestedPositionMs);
        return;
    }

    QMediaPlayer &player = m_runtime->player;
    const qint64 mediaDurationMs = player.duration();
    const QMediaPlayer::MediaStatus status = player.mediaStatus();
    const bool loadedStatus = status == QMediaPlayer::LoadedMedia
        || status == QMediaPlayer::BufferedMedia
        || status == QMediaPlayer::BufferingMedia;
    const QUrl sourceAtRequest = m_runtime->currentSource;
    const std::uint64_t generationAtRequest = m_runtime->sourceGeneration;
    const bool reachesKnownEnd = mediaDurationMs > 0
        && requestedPositionMs >= mediaDurationMs;
    const bool eligible = reachesKnownEnd
        && player.playbackState() == QMediaPlayer::PlayingState
        && player.isSeekable()
        && loadedStatus
        && !sourceAtRequest.isEmpty()
        && sourceIdentity(player.source()) == sourceIdentity(sourceAtRequest)
        && status != QMediaPlayer::EndOfMedia;

    if (!eligible) {
        setPosition(requestedPositionMs);
        return;
    }

    // Some FFmpeg/Ogg sources accept the duration seek but never report EOM.
    // Stop first, then dispatch only if the source lifecycle remained intact.
    m_runtime->stopSyncTimer();
    player.stop();
    if (generationAtRequest != m_runtime->sourceGeneration
        || sourceIdentity(sourceAtRequest) != sourceIdentity(m_runtime->currentSource)
        || sourceIdentity(player.source()) != sourceIdentity(sourceAtRequest)) {
        return;
    }
    dispatchEndOfMedia(mediaDurationMs, static_cast<quint64>(generationAtRequest));
}

qint64 NeniaPlaybackController::position() const
{
    if (m_runtime->alternateActive) {
        return m_runtime->alternateBackend != nullptr
            ? m_runtime->alternateBackend->position() : 0;
    }
    if (m_atNaturalEnd && m_runtime->player.duration() > 0) {
        return m_runtime->player.duration();
    }
    return m_runtime->player.position();
}

qint64 NeniaPlaybackController::duration() const
{
    if (m_runtime->alternateActive) {
        return m_runtime->alternateBackend != nullptr
            ? m_runtime->alternateBackend->duration() : 0;
    }
    return m_runtime->player.duration();
}

void NeniaPlaybackController::setVolume(float volume)
{
    if (std::isnan(volume)) {
        volume = 0.0f;
    } else if (!std::isfinite(volume)) {
        volume = volume > 0.0f ? 1.0f : 0.0f;
    }
    m_runtime->volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_runtime->audioOutput) {
        m_runtime->audioOutput->setVolume(m_runtime->volume);
    }
    if (m_runtime->alternateBackend != nullptr) {
        m_runtime->alternateBackend->setMasterVolume(m_runtime->volume);
    }
}

float NeniaPlaybackController::volume() const
{
    return m_runtime->volume;
}

#ifdef NENIA_TEST_HOOKS_ENABLED
QUrl NeniaPlaybackController::audioSourceForTesting() const
{
    return m_runtime->player.source();
}

quint64 NeniaPlaybackController::sourceGenerationForTesting() const
{
    return static_cast<quint64>(m_runtime->sourceGeneration);
}
#endif

QMediaPlayer::PlaybackState NeniaPlaybackController::playbackState() const
{
    if (m_runtime->alternateActive) {
        return m_runtime->alternateFailed || m_runtime->alternateBackend == nullptr
            ? QMediaPlayer::StoppedState : m_runtime->alternateBackend->playbackState();
    }
    return m_runtime->player.playbackState();
}

QMediaPlayer::MediaStatus NeniaPlaybackController::mediaStatus() const
{
    if (m_runtime->alternateActive) {
        if (m_runtime->alternateFailed || m_runtime->alternateBackend == nullptr) {
            return QMediaPlayer::InvalidMedia;
        }
        return m_runtime->alternateBackend->mediaStatus();
    }
    if (m_runtime->audioErrorActive) {
        return QMediaPlayer::InvalidMedia;
    }
    if (m_atNaturalEnd) {
        return QMediaPlayer::EndOfMedia;
    }
    return m_runtime->player.mediaStatus();
}

void NeniaPlaybackController::synchronizeTo(qint64 positionMs)
{
    const bool timelineChanged = m_runtime->synchronizer.syncTo(positionMs);
    const bool spectrumChanged = refreshSpectrumFor(positionMs);
    if ((timelineChanged || spectrumChanged) && m_widget != nullptr) {
        m_widget->renderExternalState();
    }
}

void NeniaPlaybackController::synchronizeToCurrentPosition()
{
    synchronizeTo(position());
}

void NeniaPlaybackController::resetVisualSynchronization()
{
    m_runtime->spectrumAnalyzer.resetStream();
    m_runtime->midiSpectrumFrame.reset();
    m_runtime->midiSpectrumGeneration = 0;
    m_runtime->synchronizer.reset();
    if (m_widget != nullptr) {
        m_widget->resetVisualization();
    }
}

void NeniaPlaybackController::handlePositionChanged(qint64 positionMs)
{
    emit positionChanged(positionMs);
    updatePlaybackDisplay(positionMs, m_runtime->player.duration());
    if (m_atNaturalEnd) {
        return;
    }
    synchronizeTo(positionMs);
}

void NeniaPlaybackController::handleDurationChanged(qint64 durationMs)
{
    emit durationChanged(durationMs);
    updatePlaybackDisplay(m_runtime->player.position(), durationMs);
}

void NeniaPlaybackController::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    emit playbackStateChanged(state);
    updatePlaybackDisplay(m_runtime->player.position(),
                          m_runtime->player.duration(),
                          playbackDisplayState(m_runtime->player, state));
    if (state == QMediaPlayer::PlayingState) {
        synchronizeToCurrentPosition();
        m_runtime->startSyncTimer();
    } else if (state == QMediaPlayer::PausedState) {
        m_runtime->stopSyncTimer();
        synchronizeToCurrentPosition();
    } else {
        m_runtime->stopSyncTimer();
    }
}

void NeniaPlaybackController::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    handleMediaStatusChanged(status,
                             static_cast<quint64>(m_runtime->sourceGeneration));
}

void NeniaPlaybackController::handleMediaStatusChanged(QMediaPlayer::MediaStatus status,
                                                       quint64 sourceGeneration)
{
    if (m_runtime->audioErrorActive
        || sourceGeneration != static_cast<quint64>(m_runtime->sourceGeneration)
        || m_runtime->alternateActive
        || sourceIdentity(m_runtime->player.source())
            != sourceIdentity(m_runtime->currentSource)) {
        return;
    }
    if (status == QMediaPlayer::EndOfMedia) {
        if (m_runtime->audioErrorActive) {
            return;
        }
        dispatchEndOfMedia(m_runtime->player.position(), sourceGeneration);
        return;
    }
    emit mediaStatusChanged(status);
    updatePlaybackDisplay();
}

void NeniaPlaybackController::handleMetaDataChanged()
{
    updateAutomaticPresentation();
}

void NeniaPlaybackController::refreshArtworkForCurrentSourceActivation(const QUrl &source)
{
    if (m_widget == nullptr || source.isEmpty() || !isSameSource(source)) {
        return;
    }

    const QImage previousExternalArtwork = m_runtime->externalArtwork;
    const QSize decodeTarget = detail::externalArtworkDecodeTarget(
        m_widget->size(), m_widget->devicePixelRatioF());
    m_runtime->externalArtwork =
        detail::resolveLocalArtwork(source, decodeTarget).image;
    if (m_runtime->alternateActive) {
        m_runtime->lastMetadataArtwork = {};
        m_runtime->lastMetadataArtworkIdentity = 0;
        m_widget->setAutomaticArtwork(m_runtime->externalArtwork);
        return;
    }
    updateAutomaticArtwork(previousExternalArtwork.isNull());
}

void NeniaPlaybackController::updatePlaybackDisplay()
{
    if (m_runtime->alternateActive) {
        refreshAlternatePlayback(false);
        return;
    }
    updatePlaybackDisplay(m_runtime->player.position(), m_runtime->player.duration());
}

void NeniaPlaybackController::updatePlaybackDisplay(qint64 positionMs, qint64 durationMs)
{
    if (m_runtime->alternateActive) {
        refreshAlternatePlayback(false);
        return;
    }
    updatePlaybackDisplay(positionMs,
                          durationMs,
                          playbackDisplayState(m_runtime->player));
}

void NeniaPlaybackController::updatePlaybackDisplay(qint64 positionMs,
                                                    qint64 durationMs,
                                                    const QString &playbackState)
{
    detail::NeniaPerformanceDiagnostics::instance().setPlaybackState(
        diagnosticPlaybackState(playbackState));
    if (m_widget == nullptr) {
        return;
    }

    m_widget->setPlaybackDisplay(positionMs,
                                 durationMs,
                                 m_runtime->decodedFormat,
                                 playbackState);
}

void NeniaPlaybackController::updateAutomaticPresentation()
{
    if (m_widget == nullptr || m_runtime->alternateActive) {
        return;
    }

    const detail::NeniaAutomaticMediaPresentation presentation =
        detail::automaticMediaPresentation(m_runtime->player.source(),
                                           m_runtime->player.metaData(),
                                           m_runtime->richMediaMetadata);
    m_widget->setAutomaticMediaInfo(presentation.primary);
    m_widget->setAutomaticMediaDetails(presentation.extended, presentation.location);
    updateAutomaticArtwork();
}

void NeniaPlaybackController::updateAutomaticArtwork(bool preserveExistingWhenNoMetadata)
{
    if (m_widget == nullptr || m_runtime->alternateActive) {
        return;
    }

    const bool metadataBelongsToCurrentSource =
        sourceIdentity(m_runtime->player.source())
        == sourceIdentity(m_runtime->currentSource);
    const MetadataArtwork metadata = metadataBelongsToCurrentSource
        ? metadataArtwork(m_runtime->player.metaData()) : MetadataArtwork{};
    if (metadata.image.isNull()) {
        m_runtime->lastMetadataArtwork = {};
        m_runtime->lastMetadataArtworkIdentity = 0;
    } else if (metadata.identity == 0
               || metadata.identity != m_runtime->lastMetadataArtworkIdentity) {
        m_runtime->lastMetadataArtwork = metadata.image;
        m_runtime->lastMetadataArtworkIdentity = metadata.identity;
    }

    if (preserveExistingWhenNoMetadata && m_runtime->lastMetadataArtwork.isNull()
        && m_runtime->externalArtwork.isNull()) {
        return;
    }
    m_widget->setAutomaticArtwork(detail::selectAutomaticArtwork(
        m_runtime->lastMetadataArtwork, m_runtime->externalArtwork));
}

void NeniaPlaybackController::resetAutomaticPresentation()
{
    m_runtime->externalArtwork = {};
    m_runtime->lastMetadataArtwork = {};
    m_runtime->lastMetadataArtworkIdentity = 0;
    if (m_widget == nullptr) {
        return;
    }
    m_widget->clearAutomaticMediaInfo();
    m_widget->clearAutomaticArtwork();
}

void NeniaPlaybackController::dispatchEndOfMedia(qint64 finalPositionMs,
                                                 quint64 sourceGeneration)
{
    if (m_runtime->alternateActive || m_runtime->audioErrorActive || m_atNaturalEnd
        || sourceGeneration != static_cast<quint64>(m_runtime->sourceGeneration)
        || sourceIdentity(m_runtime->player.source())
            != sourceIdentity(m_runtime->currentSource)) {
        return;
    }
    // Set the guard before emitting so direct/reentrant observers cannot
    // dispatch the same terminal event twice.
    m_atNaturalEnd = true;
    emit mediaStatusChanged(QMediaPlayer::EndOfMedia);
    handleEndOfMedia(finalPositionMs);
}

void NeniaPlaybackController::handleEndOfMedia(qint64 finalPositionMs)
{
    m_runtime->stopSyncTimer();
    synchronizeTo(finalPositionMs);
    m_runtime->spectrumAnalyzer.resetStream();
    m_runtime->midiSpectrumFrame.reset();
    if (m_widget != nullptr) {
        m_widget->resetVisualization();
    }
    if (m_runtime->alternateActive) {
        updatePlaybackDisplay(finalPositionMs,
                              duration(),
                              playbackDisplayState(QMediaPlayer::StoppedState,
                                                   QMediaPlayer::EndOfMedia));
    } else {
        updatePlaybackDisplay(finalPositionMs, m_runtime->player.duration());
    }
    m_atNaturalEnd = true;
}

void NeniaPlaybackController::handleErrorOccurred(QMediaPlayer::Error error,
                                                   const QString &errorString)
{
    m_atNaturalEnd = false;
    m_runtime->audioErrorActive = true;
    m_runtime->stopSyncTimer();
    m_runtime->player.stop();
    resetVisualSynchronization();
    emit errorOccurred(error, errorString);
    if (m_runtime->player.mediaStatus() != QMediaPlayer::InvalidMedia) {
        emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
    }
    updatePlaybackDisplay(m_runtime->player.position(),
                          m_runtime->player.duration(),
                          QStringLiteral("ERROR"));
}

bool NeniaPlaybackController::processAlternateAnalysisPcm()
{
    if (!m_runtime->alternateActive || m_runtime->alternateFailed
        || m_runtime->alternateBackend == nullptr) {
        return false;
    }
    auto *backend = m_runtime->alternateBackend.data();
    const int sampleRate = backend->analysisSampleRate();
    const std::uint64_t generation = backend->analysisGeneration();
    if (sampleRate <= 0) {
        const bool hadFrame = m_runtime->midiSpectrumFrame.has_value();
        m_runtime->spectrumAnalyzer.resetStream();
        m_runtime->midiSpectrumFrame.reset();
        return hadFrame;
    }
    if (m_runtime->midiSpectrumGeneration != generation) {
        m_runtime->spectrumAnalyzer.resetStream();
        m_runtime->midiSpectrumFrame.reset();
        m_runtime->midiSpectrumGeneration = generation;
    }

    bool changed = false;
    detail::NeniaMidiPcmAnalysisBlock block;
    while (backend->takeEligibleAnalysisBlock(&block)) {
        if (block.generation != generation || block.frameCount == 0
            || block.frameCount > detail::kMidiAnalysisBlockFrames
            || block.sampleRate != static_cast<std::uint32_t>(sampleRate)) {
            continue;
        }
        const std::vector<detail::SpectrumFrame> frames =
            m_runtime->spectrumAnalyzer.processPcmFrames(
                std::span<const float>(block.stereoFloat.data(), block.frameCount * 2U),
                2, sampleRate, block.firstOutputFrame);
        if (!frames.empty()) {
            m_runtime->midiSpectrumFrame = frames.back();
            changed = true;
        }
    }
    return changed;
}

void NeniaPlaybackController::updateAlternateMediaPresentation()
{
    if (!m_runtime->alternateActive || m_runtime->alternateBackend == nullptr
        || m_widget == nullptr) {
        return;
    }
    const detail::NeniaAlternateMediaPresentation presentation =
        m_runtime->alternateBackend->mediaPresentation();
    if (!presentation.extended.directMidi) {
        return;
    }
    m_runtime->decodedFormat = m_runtime->alternateBackend->formatDescription();
    m_widget->setAutomaticMediaInfo(presentation.primary);
    m_widget->setAutomaticMediaDetails(presentation.extended, presentation.location);
}

bool NeniaPlaybackController::refreshSpectrumFor(qint64 positionMs)
{
    if (m_widget == nullptr) {
        return false;
    }

    if (m_runtime->alternateActive) {
        const auto *backend = m_runtime->alternateBackend.data();
        if (backend == nullptr || !m_runtime->midiSpectrumFrame.has_value()) {
            return m_widget->clearSpectrum();
        }
        const int sampleRate = backend->analysisSampleRate();
        if (sampleRate <= 0) {
            return m_widget->clearSpectrum();
        }
        const std::uint64_t consumedFrame = backend->consumedAnalysisFrame();
        const long double nowUs = static_cast<long double>(consumedFrame)
            * 1'000'000.0L / static_cast<long double>(sampleRate);
        const long double ageUs = nowUs
            - static_cast<long double>(m_runtime->midiSpectrumFrame->centerTimeUs);
        if (ageUs < -2.0L || ageUs > detail::kSpectrumMaxAgeUs) {
            return m_widget->clearSpectrum();
        }
        return m_widget->setSpectrumBands(std::span<const float>(
            m_runtime->midiSpectrumFrame->bands.data(),
            m_runtime->midiSpectrumFrame->bands.size()));
    }

    const qint64 nonNegativePositionMs = std::max<qint64>(positionMs, 0);
    qint64 positionUs = 0;
    if (nonNegativePositionMs
        > std::numeric_limits<qint64>::max() / 1'000) {
        positionUs = std::numeric_limits<qint64>::max();
    } else {
        positionUs = nonNegativePositionMs * 1'000;
    }

    const auto frame = m_runtime->spectrumFrameStore.lookup(positionUs);
    if (frame.has_value()) {
        return m_widget->setSpectrumBands(
            std::span<const float>(frame->bands.data(), frame->bands.size()));
    }
    return m_widget->clearSpectrum();
}

void NeniaPlaybackController::handleAudioBufferReceived(const QAudioBuffer &buffer)
{
    if (m_runtime->alternateActive) {
        return;
    }
    detail::NeniaPerformanceDiagnostics &diagnostics =
        detail::NeniaPerformanceDiagnostics::instance();
    detail::ScopedPerformanceTimer slotTimer(
        detail::NeniaPerformanceDiagnostics::Timing::PcmSlot);
    const QCoreApplication *application = QCoreApplication::instance();
    diagnostics.setPcmCallbackOnGuiThread(
        application != nullptr && QThread::currentThread() == application->thread());
    diagnostics.recordPcmBuffer(
        buffer.format(),
        static_cast<std::uint64_t>(buffer.frameCount() > 0 ? buffer.frameCount() : 0),
        static_cast<std::uint64_t>(buffer.byteCount() > 0 ? buffer.byteCount() : 0));
    const QString format = decodedAudioFormat(buffer.format());
    if (format != m_runtime->decodedFormat) {
        m_runtime->decodedFormat = format;
        updatePlaybackDisplay();
    }

    const qint64 currentPositionMs = std::max<qint64>(m_runtime->player.position(), 0);
    const qint64 fallbackStartTimeUs = currentPositionMs
        > std::numeric_limits<qint64>::max() / 1'000
        ? std::numeric_limits<qint64>::max()
        : currentPositionMs * 1'000;

    std::vector<detail::SpectrumFrame> frames;
    {
        detail::ScopedPerformanceTimer analysisTimer(
            detail::NeniaPerformanceDiagnostics::Timing::PcmAnalysisAndStore);
        frames = m_runtime->spectrumAnalyzer.processBuffer(buffer, fallbackStartTimeUs);
        for (const detail::SpectrumFrame &frame : frames) {
            m_runtime->spectrumFrameStore.insert(frame);
        }
    }
    diagnostics.recordSpectrumFrames(static_cast<std::uint64_t>(frames.size()));

    if (!frames.empty()) {
        if (refreshSpectrumFor(currentPositionMs) && m_widget != nullptr) {
            m_widget->renderExternalState();
        }
    }
}

} // namespace nenia
