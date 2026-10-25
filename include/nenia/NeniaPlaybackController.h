#pragma once

#include <nenia/NeniaWidget.h>

#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <memory>

class NeniaPlaybackControllerTest;
class NeniaPlaylistControllerTest;
class DirectMidiIntegrationTest;
class MidiChannelMatrixTest;
class EndOfMediaMarqueeTest;
class LocalArtworkResolutionTest;
class QAudioBuffer;

namespace nenia {

class TimelineEventProvider;
class NeniaPlaylistController;
namespace detail {
class NeniaAlternatePlaybackBackend;
}
namespace app {
class NeniaApplicationPlaybackRouter;
}

class NeniaPlaybackController final : public QObject
{
    Q_OBJECT

public:
    explicit NeniaPlaybackController(NeniaWidget *widget,
                                      QObject *parent = nullptr);
    ~NeniaPlaybackController() override;

    void setSource(const QUrl &source);
    QUrl source() const;

    void setTimelineProvider(std::shared_ptr<const TimelineEventProvider> provider);
    void clearTimelineProvider();

    void play();
    void pause();
    void stop();
    void restart();

    void setPosition(qint64 positionMs);
    void seekBy(qint64 deltaMs);
    qint64 position() const;
    qint64 duration() const;

    void setVolume(float volume);
    float volume() const;

    QMediaPlayer::PlaybackState playbackState() const;
    QMediaPlayer::MediaStatus mediaStatus() const;

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void errorOccurred(QMediaPlayer::Error error, const QString &errorString);

private:
    class Runtime;

    friend class app::NeniaApplicationPlaybackRouter;

#ifdef NENIA_TEST_HOOKS_ENABLED
    friend class ::NeniaPlaybackControllerTest;
    friend class ::NeniaPlaylistControllerTest;
    friend class ::DirectMidiIntegrationTest;
    friend class ::MidiChannelMatrixTest;
    friend class ::EndOfMediaMarqueeTest;
    friend class ::LocalArtworkResolutionTest;
#endif
    friend class NeniaPlaylistController;

    bool isSameSource(const QUrl &source) const;
    void synchronizeTo(qint64 positionMs);
    void synchronizeToCurrentPosition();
    bool refreshSpectrumFor(qint64 positionMs);
    bool processAlternateAnalysisPcm();
    void updateAlternateMediaPresentation();
    void handleAudioBufferReceived(const QAudioBuffer &buffer);
#ifdef NENIA_TEST_HOOKS_ENABLED
    QUrl audioSourceForTesting() const;
    quint64 sourceGenerationForTesting() const;
#endif
    void setAlternatePlaybackBackend(detail::NeniaAlternatePlaybackBackend *backend);
    void connectAudioSignalsForCurrentSource();
    void refreshAlternatePlayback(bool emitTimeSignals = true);
    void handleAlternateEndOfMedia();
    void handleAlternateError(const QString &message);
    void resetVisualSynchronization();
    void dispatchEndOfMedia(qint64 finalPositionMs, quint64 sourceGeneration);
    void handleEndOfMedia(qint64 finalPositionMs);
    void handlePositionChanged(qint64 positionMs);
    void handleDurationChanged(qint64 durationMs);
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status,
                                  quint64 sourceGeneration);
    void handleMetaDataChanged();
    void handleErrorOccurred(QMediaPlayer::Error error, const QString &errorString);
    void updatePlaybackDisplay();
    void updatePlaybackDisplay(qint64 positionMs, qint64 durationMs);
    void updatePlaybackDisplay(qint64 positionMs,
                               qint64 durationMs,
                               const QString &playbackState);
    void updateAutomaticPresentation();
    void updateAutomaticArtwork(bool preserveExistingWhenNoMetadata = false);
    void resetAutomaticPresentation();
    void refreshArtworkForCurrentSourceActivation(const QUrl &source);

    QPointer<NeniaWidget> m_widget;
    std::unique_ptr<Runtime> m_runtime;
    bool m_atNaturalEnd = false;
};

} // namespace nenia
