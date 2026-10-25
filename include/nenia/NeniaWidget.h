#pragma once

#include <nenia/NeniaMediaInfo.h>
#include <nenia/NeniaUiState.h>

#include <QImage>
#include <QtGlobal>
#include <QWidget>

#include <chrono>
#include <array>
#include <memory>
#include <span>

class QPaintEvent;
class QMouseEvent;
class QEvent;
class QResizeEvent;
class QShowEvent;
class QHideEvent;
class NeniaPlaybackControllerTest;
class NeniaPlaylistControllerTest;
class DirectMidiIntegrationTest;
class MidiMetadataAndSpectrumTest;
class PlaybackSettingsAndMetadataTest;
class MediaMetadataTest;
class MidiChannelMatrixTest;
class KeyboardRangeSettingsTest;
class OctaveNamingPreferencesTest;
class EndOfMediaMarqueeTest;
class CoalescedRenderingTest;
class LocalArtworkResolutionTest;

namespace nenia {

class NeniaPlaybackController;
namespace detail {
struct NeniaExtendedMediaInfo;
struct DisplayState;
}

class NeniaWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NeniaWidget(QWidget *parent = nullptr);
    ~NeniaWidget() override;

    void noteOn(int track, int note, float velocity = 1.0f);
    void noteOff(int track, int note);
    // Selects one of the supported keyboard display ranges. Unsupported bounds
    // are ignored; MIDI state and playback are not changed.
    bool setKeyboardRange(int lowestMidiNote, int highestMidiNote);
    // Selects the displayed octave number for middle C (3, 4, or 5). This
    // presentation-only setting never changes MIDI note numbers or audio pitch.
    bool setOctaveNamingMiddleC(int octave);
    void setMeterLevel(int meter, float level);
    void resetVisualization();

    void setMediaInfo(const NeniaMediaInfo &info);
    NeniaMediaInfo mediaInfo() const;
    void clearMediaInfo();

    void setArtwork(const QImage &image);
    void clearArtwork();
    bool hasArtwork() const;

    void setPlaybackUiState(const NeniaPlaybackUiState &state);
    NeniaPlaybackUiState playbackUiState() const;
    void setPlaylistState(const NeniaPlaylistState &state);
    NeniaPlaylistState playlistState() const;

    static QImage loadArtworkFile(const QString &path, QString *errorMessage = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void playlistRowSelected(int index);
    void playlistRowActivated(int index);
    void transportControlActivated(nenia::NeniaTransportControl control);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    friend class NeniaPlaybackController;
#ifdef NENIA_TEST_HOOKS_ENABLED
    friend class ::NeniaPlaybackControllerTest;
    friend class ::NeniaPlaylistControllerTest;
    friend class ::DirectMidiIntegrationTest;
    friend class ::MidiMetadataAndSpectrumTest;
    friend class ::PlaybackSettingsAndMetadataTest;
    friend class ::MediaMetadataTest;
    friend class ::MidiChannelMatrixTest;
    friend class ::KeyboardRangeSettingsTest;
    friend class ::OctaveNamingPreferencesTest;
    friend class ::EndOfMediaMarqueeTest;
    friend class ::CoalescedRenderingTest;
    friend class ::LocalArtworkResolutionTest;
#endif

    class Runtime;

    void requestVisualRefresh();
    void scheduleWidgetUpdate();
    void renderCurrentState();
    void startAnimationTimer();
    void stopAnimationTimer();
    void advanceAnimationFrame();
    void advanceMarqueeFrame();
    void beginExternalClockControl();
    void endExternalClockControl();
    void advanceExternalClock(std::chrono::milliseconds delta);
    void renderExternalState();
    void *externalAnimationEngine() noexcept;
    bool setSpectrumBands(std::span<const float> bands);
    bool clearSpectrum();
    void setAutomaticMediaInfo(const NeniaMediaInfo &info);
    void clearAutomaticMediaInfo();
    void setAutomaticMediaDetails(const detail::NeniaExtendedMediaInfo &info,
                                  const QString &location);
#ifdef NENIA_TEST_HOOKS_ENABLED
    detail::NeniaExtendedMediaInfo automaticMediaDetailsForTesting() const;
    QString automaticLocationForTesting() const;
    qint64 marqueeElapsedForTesting() const;
    bool spectrumIsNeutralForTesting() const;
    const detail::DisplayState &displayStateForTesting() const;
#endif
    void setAutomaticArtwork(const QImage &image);
    void clearAutomaticArtwork();
    void setPlaybackDisplay(qint64 positionMs,
                            qint64 durationMs,
                            const QString &format,
                            const QString &playbackState);
    void refreshPresentation();
    void resetMarqueeState();
    void updateMarqueeTimer();
    void invalidateArtworkCache();
    void rebuildArtworkCache();
    int playlistRowAt(const QPoint &position) const;
    void updateMousePresentation(const QPoint &position);
    const QImage &resolvedArtwork() const noexcept;

    std::unique_ptr<Runtime> m_runtime;
    QImage m_logicalSurface;
    NeniaMediaInfo m_explicitMediaInfo;
    NeniaMediaInfo m_automaticMediaInfo;
    bool m_explicitTitle = false;
    bool m_explicitArtist = false;
    bool m_explicitAlbum = false;
    QImage m_explicitArtwork;
    QImage m_automaticArtwork;
    bool m_hasExplicitArtwork = false;
    QImage m_cachedArtwork;
    bool m_artworkCacheValid = false;
    bool m_surfaceDirty = true;
    bool m_externalClockControlled = false;
};

} // namespace nenia
