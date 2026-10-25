#include <nenia/NeniaWidget.h>

#include "NeniaAnimationEngine.h"
#include "NeniaArtworkLoader.h"
#include "NeniaColorScheme.h"
#include "NeniaExtendedMediaInfo.h"
#include "NeniaKeyboardRange.h"
#include "NeniaPerformanceDiagnostics.h"
#include "NeniaRenderer.h"
#include "NeniaSpectrumFrame.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QSizePolicy>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

namespace nenia {
namespace {

QSize renderViewportFor(const QWidget &widget)
{
    const QSize viewport = widget.size();
    return viewport.isValid() && !viewport.isEmpty()
        ? viewport : detail::NeniaRenderer::preferredSize();
}

} // namespace

class NeniaWidget::Runtime final
{
public:
    detail::NeniaAnimationEngine engine;
    QTimer animationTimer;
    QTimer marqueeTimer;
    QElapsedTimer animationClock;
    QElapsedTimer marqueeClock;
    QElapsedTimer marqueeDiagnosticsClock;
    detail::SpectrumBands spectrum{};
    detail::KeyboardRangeMode keyboardRange = detail::kDefaultKeyboardRangeMode;
    detail::OctaveNamingConvention octaveNamingConvention =
        detail::kDefaultOctaveNamingConvention;
    detail::DisplayPresentation displayPresentation;
    detail::NeniaExtendedMediaInfo automaticExtendedMediaInfo;
    QString audioSourceLocation;
    qint64 marqueeElapsedMs = 0;
    bool marqueeActive = false;
    NeniaTransportControl pressedTransportControl = NeniaTransportControl::None;
    int pressedPlaylistRow = -1;
    bool suppressPlaylistRelease = false;
};

NeniaWidget::NeniaWidget(QWidget *parent)
    : QWidget(parent)
    , m_runtime(std::make_unique<Runtime>())
    , m_logicalSurface(detail::NeniaRenderer::preferredSize(),
                       QImage::Format_ARGB32_Premultiplied)
{
    detail::NeniaPerformanceDiagnostics::instance().initializeForWidget(this);

    m_runtime->animationTimer.setInterval(16);
    connect(&m_runtime->animationTimer, &QTimer::timeout, this,
            [this] { advanceAnimationFrame(); });
    m_runtime->marqueeTimer.setInterval(33);
    connect(&m_runtime->marqueeTimer, &QTimer::timeout, this,
            [this] { advanceMarqueeFrame(); });

    requestVisualRefresh();
    scheduleWidgetUpdate();
    updateMarqueeTimer();
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

NeniaWidget::~NeniaWidget() = default;

void NeniaWidget::noteOn(int track, int note, float velocity)
{
    m_runtime->engine.noteOn(track, note, velocity);
    requestVisualRefresh();
    if (m_externalClockControlled) {
        stopAnimationTimer();
    } else if (m_runtime->engine.hasActiveDecay()) {
        startAnimationTimer();
    } else {
        stopAnimationTimer();
    }
    scheduleWidgetUpdate();
}

void NeniaWidget::noteOff(int track, int note)
{
    m_runtime->engine.noteOff(track, note);
    requestVisualRefresh();
    if (m_externalClockControlled) {
        stopAnimationTimer();
    } else if (m_runtime->engine.hasActiveDecay()) {
        startAnimationTimer();
    } else {
        stopAnimationTimer();
    }
    scheduleWidgetUpdate();
}

bool NeniaWidget::setKeyboardRange(int lowestMidiNote, int highestMidiNote)
{
    const auto mode = detail::keyboardRangeModeForBounds(lowestMidiNote, highestMidiNote);
    if (!mode) {
        return false;
    }
    if (m_runtime->keyboardRange == *mode) {
        return true;
    }

    m_runtime->keyboardRange = *mode;
    requestVisualRefresh();
    scheduleWidgetUpdate();
    return true;
}

bool NeniaWidget::setOctaveNamingMiddleC(int octave)
{
    const auto convention = detail::octaveNamingConventionForMiddleC(octave);
    if (!convention.has_value()) {
        return false;
    }
    if (m_runtime->octaveNamingConvention == convention.value()) {
        return true;
    }

    m_runtime->octaveNamingConvention = convention.value();
    requestVisualRefresh();
    scheduleWidgetUpdate();
    return true;
}

void NeniaWidget::setMeterLevel(int meter, float level)
{
    m_runtime->engine.setMeterLevel(meter, level);
    requestVisualRefresh();
    if (m_externalClockControlled) {
        stopAnimationTimer();
    } else if (m_runtime->engine.hasActiveDecay()) {
        startAnimationTimer();
    } else {
        stopAnimationTimer();
    }
    scheduleWidgetUpdate();
}

void NeniaWidget::resetVisualization()
{
    m_runtime->engine.reset();
    m_runtime->spectrum = {};
    stopAnimationTimer();
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

void NeniaWidget::setMediaInfo(const NeniaMediaInfo &info)
{
    m_explicitMediaInfo = info;
    m_explicitTitle = !info.title.trimmed().isEmpty();
    m_explicitArtist = !info.artist.trimmed().isEmpty();
    m_explicitAlbum = !info.album.trimmed().isEmpty();
    refreshPresentation();
}

NeniaMediaInfo NeniaWidget::mediaInfo() const
{
    NeniaMediaInfo resolved = m_automaticMediaInfo;
    if (m_explicitTitle) {
        resolved.title = m_explicitMediaInfo.title;
    }
    if (m_explicitArtist) {
        resolved.artist = m_explicitMediaInfo.artist;
    }
    if (m_explicitAlbum) {
        resolved.album = m_explicitMediaInfo.album;
    }
    return resolved;
}

void NeniaWidget::clearMediaInfo()
{
    m_explicitMediaInfo = {};
    m_explicitTitle = false;
    m_explicitArtist = false;
    m_explicitAlbum = false;
    refreshPresentation();
}

void NeniaWidget::setArtwork(const QImage &image)
{
    m_explicitArtwork = image;
    m_hasExplicitArtwork = !image.isNull();
    invalidateArtworkCache();
    refreshPresentation();
}

void NeniaWidget::clearArtwork()
{
    m_explicitArtwork = {};
    m_hasExplicitArtwork = false;
    invalidateArtworkCache();
    refreshPresentation();
}

bool NeniaWidget::hasArtwork() const
{
    return !resolvedArtwork().isNull();
}

void NeniaWidget::setPlaybackUiState(const NeniaPlaybackUiState &state)
{
    if (m_runtime->displayPresentation.playbackUi.loop == state.loop
        && m_runtime->displayPresentation.playbackUi.shuffle == state.shuffle
        && m_runtime->displayPresentation.playbackUi.extendedMediaInfo
               == state.extendedMediaInfo) {
        return;
    }

    const bool mediaInfoPageChanged =
        m_runtime->displayPresentation.playbackUi.extendedMediaInfo
        != state.extendedMediaInfo;
    m_runtime->displayPresentation.playbackUi = state;
    if (mediaInfoPageChanged) {
        resetMarqueeState();
        updateMarqueeTimer();
    }
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

NeniaPlaybackUiState NeniaWidget::playbackUiState() const
{
    return m_runtime->displayPresentation.playbackUi;
}

void NeniaWidget::setPlaylistState(const NeniaPlaylistState &state)
{
    if (m_runtime->displayPresentation.playlist == state) {
        return;
    }

    m_runtime->displayPresentation.playlist = state;
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

NeniaPlaylistState NeniaWidget::playlistState() const
{
    return m_runtime->displayPresentation.playlist;
}

QImage NeniaWidget::loadArtworkFile(const QString &path, QString *errorMessage)
{
    return detail::loadArtworkFile(path, errorMessage);
}

QSize NeniaWidget::sizeHint() const
{
    return detail::NeniaRenderer::preferredSize();
}

QSize NeniaWidget::minimumSizeHint() const
{
    return detail::NeniaRenderer::minimumSize();
}

void NeniaWidget::requestVisualRefresh()
{
    detail::NeniaPerformanceDiagnostics::instance().recordCounter(
        detail::NeniaPerformanceDiagnostics::Counter::VisualRefreshRequests);
    m_surfaceDirty = true;
}

void NeniaWidget::scheduleWidgetUpdate()
{
    detail::NeniaPerformanceDiagnostics::instance().recordCounter(
        detail::NeniaPerformanceDiagnostics::Counter::WidgetUpdateRequests);
    update();
}

void NeniaWidget::renderCurrentState()
{
    detail::NeniaPerformanceDiagnostics &diagnostics =
        detail::NeniaPerformanceDiagnostics::instance();
    detail::ScopedPerformanceTimer renderTimer(
        detail::NeniaPerformanceDiagnostics::Timing::RenderCurrentState);
    const QSize viewport = renderViewportFor(*this);
    diagnostics.recordFullRender(viewport.width(), viewport.height());
    if (m_logicalSurface.size() != viewport) {
        diagnostics.recordCounter(
            detail::NeniaPerformanceDiagnostics::Counter::SurfaceReallocations);
        m_logicalSurface = QImage(viewport, QImage::Format_ARGB32_Premultiplied);
        invalidateArtworkCache();
    }
    if (!m_artworkCacheValid) {
        rebuildArtworkCache();
    }
    m_runtime->displayPresentation.extendedMediaInfo =
        m_runtime->automaticExtendedMediaInfo;
    m_runtime->displayPresentation.octaveNamingConvention =
        m_runtime->octaveNamingConvention;
    m_runtime->displayPresentation.location = m_runtime->audioSourceLocation;
    m_runtime->displayPresentation.mediaMarqueeElapsedMs = m_runtime->marqueeElapsedMs;
    detail::NeniaRenderer::render(m_logicalSurface,
                                  m_runtime->engine.displayState(),
                                  m_runtime->spectrum,
                                  mediaInfo(),
                                  m_cachedArtwork,
                                  m_runtime->displayPresentation,
                                  m_runtime->keyboardRange);
    m_surfaceDirty = false;
}

void NeniaWidget::startAnimationTimer()
{
    if (m_runtime->animationTimer.isActive()) {
        return;
    }

    // Idle time is intentionally not counted as release time. Every new
    // runtime interval starts with a fresh measurement origin.
    m_runtime->animationClock.start();
    m_runtime->animationTimer.start();
}

void NeniaWidget::stopAnimationTimer()
{
    m_runtime->animationTimer.stop();
    m_runtime->animationClock.invalidate();
}

void NeniaWidget::advanceAnimationFrame()
{
    if (!m_runtime->animationTimer.isActive()) {
        return;
    }

    const qint64 elapsedMilliseconds = m_runtime->animationClock.restart();
    if (elapsedMilliseconds > 0) {
        m_runtime->engine.advance(std::chrono::milliseconds(elapsedMilliseconds));
    }

    requestVisualRefresh();
    scheduleWidgetUpdate();
    if (!m_runtime->engine.hasActiveDecay()) {
        stopAnimationTimer();
    }
}

void NeniaWidget::advanceMarqueeFrame()
{
    if (!m_runtime->marqueeTimer.isActive() || !m_runtime->marqueeActive) {
        return;
    }

    const qint64 elapsedMilliseconds = m_runtime->marqueeClock.isValid()
        ? m_runtime->marqueeClock.restart()
        : 0;
    m_runtime->marqueeElapsedMs += std::max<qint64>(0, elapsedMilliseconds);
    if (detail::NeniaPerformanceDiagnostics::instance().enabled()
        && m_runtime->marqueeDiagnosticsClock.isValid()) {
        const qint64 actualInterval = m_runtime->marqueeDiagnosticsClock.nsecsElapsed();
        m_runtime->marqueeDiagnosticsClock.restart();
        if (actualInterval > 0) {
            detail::NeniaPerformanceDiagnostics::instance().recordTimerInterval(
                detail::NeniaPerformanceDiagnostics::Timing::MarqueeTimerInterval,
                detail::NeniaPerformanceDiagnostics::Timing::MarqueeTimerLateness,
                static_cast<std::uint64_t>(actualInterval), 33'000'000U);
        }
    }
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

void NeniaWidget::beginExternalClockControl()
{
    if (m_externalClockControlled) {
        return;
    }

    m_externalClockControlled = true;
    stopAnimationTimer();
}

void NeniaWidget::endExternalClockControl()
{
    if (!m_externalClockControlled) {
        return;
    }

    m_externalClockControlled = false;
    if (m_runtime->engine.hasActiveDecay()) {
        startAnimationTimer();
    } else {
        stopAnimationTimer();
    }
}

void NeniaWidget::advanceExternalClock(std::chrono::milliseconds delta)
{
    if (!m_externalClockControlled) {
        return;
    }

    m_runtime->engine.advance(delta);
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

void NeniaWidget::renderExternalState()
{
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

void *NeniaWidget::externalAnimationEngine() noexcept
{
    return &m_runtime->engine;
}

bool NeniaWidget::setSpectrumBands(std::span<const float> bands)
{
    if (bands.size() != m_runtime->spectrum.size()) {
        return false;
    }

    detail::SpectrumBands next{};
    std::copy(bands.begin(), bands.end(), next.begin());
    if (m_runtime->spectrum == next) {
        return false;
    }
    m_runtime->spectrum = next;
    return true;
}

bool NeniaWidget::clearSpectrum()
{
    const detail::SpectrumBands neutral{};
    if (m_runtime->spectrum == neutral) {
        return false;
    }
    m_runtime->spectrum = neutral;
    return true;
}

void NeniaWidget::setAutomaticMediaInfo(const NeniaMediaInfo &info)
{
    if (m_automaticMediaInfo == info) {
        return;
    }
    m_automaticMediaInfo = info;
    refreshPresentation();
}

void NeniaWidget::clearAutomaticMediaInfo()
{
    m_automaticMediaInfo = {};
    m_runtime->automaticExtendedMediaInfo = {};
    m_runtime->audioSourceLocation.clear();
    refreshPresentation();
}

void NeniaWidget::setAutomaticMediaDetails(
    const detail::NeniaExtendedMediaInfo &info, const QString &location)
{
    if (m_runtime->automaticExtendedMediaInfo == info
        && m_runtime->audioSourceLocation == location) {
        return;
    }

    m_runtime->automaticExtendedMediaInfo = info;
    m_runtime->audioSourceLocation = location;
    refreshPresentation();
}

#ifdef NENIA_TEST_HOOKS_ENABLED
detail::NeniaExtendedMediaInfo NeniaWidget::automaticMediaDetailsForTesting() const
{
    return m_runtime->automaticExtendedMediaInfo;
}

QString NeniaWidget::automaticLocationForTesting() const
{
    return m_runtime->audioSourceLocation;
}

qint64 NeniaWidget::marqueeElapsedForTesting() const
{
    return m_runtime->marqueeElapsedMs;
}

bool NeniaWidget::spectrumIsNeutralForTesting() const
{
    return m_runtime->spectrum == detail::SpectrumBands{};
}
#endif

void NeniaWidget::setAutomaticArtwork(const QImage &image)
{
    if (m_automaticArtwork.cacheKey() == image.cacheKey()
        && m_automaticArtwork.size() == image.size()
        && m_automaticArtwork.format() == image.format()) {
        return;
    }
    m_automaticArtwork = image;
    invalidateArtworkCache();
    refreshPresentation();
}

void NeniaWidget::clearAutomaticArtwork()
{
    if (m_automaticArtwork.isNull()) {
        return;
    }
    m_automaticArtwork = {};
    invalidateArtworkCache();
    refreshPresentation();
}

#ifdef NENIA_TEST_HOOKS_ENABLED
const detail::DisplayState &NeniaWidget::displayStateForTesting() const
{
    return m_runtime->engine.displayState();
}
#endif

void NeniaWidget::setPlaybackDisplay(qint64 positionMs,
                                     qint64 durationMs,
                                     const QString &format,
                                     const QString &playbackState)
{
    detail::DisplayPresentation next = m_runtime->displayPresentation;
    next.positionMs = std::max<qint64>(positionMs, 0);
    next.durationMs = durationMs;
    next.format = format.trimmed();
    next.playbackState = playbackState.trimmed().isEmpty()
        ? QStringLiteral("READY")
        : playbackState.trimmed().toUpper();

    if (next.positionMs == m_runtime->displayPresentation.positionMs
        && next.durationMs == m_runtime->displayPresentation.durationMs
        && next.format == m_runtime->displayPresentation.format
        && next.playbackState == m_runtime->displayPresentation.playbackState) {
        return;
    }

    const bool marqueeContentChanged = next.format != m_runtime->displayPresentation.format
        || next.playbackState != m_runtime->displayPresentation.playbackState
        || next.durationMs != m_runtime->displayPresentation.durationMs;
    m_runtime->displayPresentation = std::move(next);
    if (marqueeContentChanged) {
        resetMarqueeState();
    }
    requestVisualRefresh();
    updateMarqueeTimer();
    scheduleWidgetUpdate();
}

void NeniaWidget::refreshPresentation()
{
    resetMarqueeState();
    requestVisualRefresh();
    updateMarqueeTimer();
    scheduleWidgetUpdate();
}

void NeniaWidget::resetMarqueeState()
{
    m_runtime->marqueeElapsedMs = 0;
    if (m_runtime->marqueeActive) {
        m_runtime->marqueeClock.start();
    } else {
        m_runtime->marqueeClock.invalidate();
    }
    m_runtime->displayPresentation.mediaMarqueeElapsedMs = 0;
}

void NeniaWidget::updateMarqueeTimer()
{
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(
        renderViewportFor(*this));
    const bool needsMarquee = detail::NeniaRenderer::mediaInfoNeedsMarquee(
        layout, mediaInfo(), m_runtime->displayPresentation);
    if (!needsMarquee) {
        m_runtime->marqueeActive = false;
        m_runtime->marqueeElapsedMs = 0;
        m_runtime->marqueeClock.invalidate();
        m_runtime->marqueeTimer.stop();
        m_runtime->marqueeDiagnosticsClock.invalidate();
        m_runtime->displayPresentation.mediaMarqueeElapsedMs = 0;
        return;
    }

    if (!isVisible()) {
        m_runtime->marqueeActive = false;
        m_runtime->marqueeElapsedMs = 0;
        m_runtime->marqueeClock.invalidate();
        m_runtime->marqueeTimer.stop();
        m_runtime->marqueeDiagnosticsClock.invalidate();
        m_runtime->displayPresentation.mediaMarqueeElapsedMs = 0;
        return;
    }

    if (!m_runtime->marqueeActive) {
        m_runtime->marqueeActive = true;
        m_runtime->marqueeElapsedMs = 0;
        m_runtime->marqueeClock.start();
    }
    if (!m_runtime->marqueeTimer.isActive()) {
        if (detail::NeniaPerformanceDiagnostics::instance().enabled()) {
            m_runtime->marqueeDiagnosticsClock.start();
        }
        m_runtime->marqueeTimer.start();
    }
}

void NeniaWidget::invalidateArtworkCache()
{
    m_artworkCacheValid = false;
}

void NeniaWidget::rebuildArtworkCache()
{
    detail::NeniaPerformanceDiagnostics::instance().recordArtworkCacheRebuild();
    detail::ScopedPerformanceTimer artworkTimer(
        detail::NeniaPerformanceDiagnostics::Timing::ArtworkCacheRebuild);
    const QImage &source = resolvedArtwork();
    if (source.isNull()) {
        m_cachedArtwork = {};
        m_artworkCacheValid = true;
        return;
    }

    const QRect artworkContent = detail::NeniaRenderer::layoutForSize(
                                     m_logicalSurface.size())
                                     .artworkContentRect;
    const QSize cacheSize(std::max(8, artworkContent.width()),
                          std::max(8, artworkContent.height()));
    m_cachedArtwork = source.scaled(cacheSize, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
    m_artworkCacheValid = true;
}

const QImage &NeniaWidget::resolvedArtwork() const noexcept
{
    return m_hasExplicitArtwork ? m_explicitArtwork : m_automaticArtwork;
}

void NeniaWidget::paintEvent(QPaintEvent *event)
{
    detail::NeniaPerformanceDiagnostics &diagnostics =
        detail::NeniaPerformanceDiagnostics::instance();
    detail::ScopedPerformanceTimer paintTimer(
        detail::NeniaPerformanceDiagnostics::Timing::PaintEvent);
    if (event != nullptr) {
        diagnostics.recordPaint(event->region(), width(), height());
    }

    const QString currentColorScheme = detail::neniaColorSchemeId(
        detail::activeNeniaColorScheme());
    if (property("_neniaRenderedColorScheme").toString() != currentColorScheme) {
        requestVisualRefresh();
    }

    if (m_surfaceDirty || m_logicalSurface.size() != renderViewportFor(*this)) {
        renderCurrentState();
        setProperty("_neniaRenderedColorScheme", currentColorScheme);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!m_logicalSurface.isNull()) {
        detail::ScopedPerformanceTimer blitTimer(
            detail::NeniaPerformanceDiagnostics::Timing::PaintImageBlit);
        painter.drawImage(QPoint(0, 0), m_logicalSurface);
    }
}

void NeniaWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    detail::NeniaPerformanceDiagnostics::instance().recordGeometryChange();
    resetMarqueeState();
    invalidateArtworkCache();
    requestVisualRefresh();
    updateMarqueeTimer();
    scheduleWidgetUpdate();
}

int NeniaWidget::playlistRowAt(const QPoint &position) const
{
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(size());
    const auto &playlist = m_runtime->displayPresentation.playlist;
    return detail::NeniaRenderer::playlistRowAt(
        layout, position, playlist.entries.size(), playlist.firstVisibleRow);
}

void NeniaWidget::updateMousePresentation(const QPoint &position)
{
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(size());
    const auto hoveredControl = detail::NeniaRenderer::transportControlAt(layout, position);
    const auto pressedControl = hoveredControl == m_runtime->pressedTransportControl
        ? m_runtime->pressedTransportControl : NeniaTransportControl::None;
    const bool clickable = hoveredControl != NeniaTransportControl::None
        || playlistRowAt(position) >= 0;
    if (clickable) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }

    auto &presentation = m_runtime->displayPresentation;
    if (presentation.hoveredTransportControl == hoveredControl
        && presentation.pressedTransportControl == pressedControl) {
        return;
    }
    presentation.hoveredTransportControl = hoveredControl;
    presentation.pressedTransportControl = pressedControl;
    requestVisualRefresh();
    scheduleWidgetUpdate();
}

void NeniaWidget::mouseMoveEvent(QMouseEvent *event)
{
    updateMousePresentation(event->position().toPoint());
    event->accept();
}

void NeniaWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint position = event->position().toPoint();
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(size());
    m_runtime->suppressPlaylistRelease = false;
    m_runtime->pressedTransportControl =
        detail::NeniaRenderer::transportControlAt(layout, position);
    m_runtime->pressedPlaylistRow =
            m_runtime->pressedTransportControl == NeniaTransportControl::None
        ? playlistRowAt(position) : -1;
    updateMousePresentation(position);
    event->accept();
}

void NeniaWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const QPoint position = event->position().toPoint();
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(size());
    const auto pressedControl = m_runtime->pressedTransportControl;
    const int pressedRow = m_runtime->pressedPlaylistRow;
    const bool suppressPlaylistRelease = m_runtime->suppressPlaylistRelease;
    m_runtime->pressedTransportControl = NeniaTransportControl::None;
    m_runtime->pressedPlaylistRow = -1;
    m_runtime->suppressPlaylistRelease = false;
    updateMousePresentation(position);

    if (pressedControl != NeniaTransportControl::None
        && detail::NeniaRenderer::transportControlAt(layout, position) == pressedControl) {
        emit transportControlActivated(pressedControl);
    } else if (!suppressPlaylistRelease && pressedRow >= 0
               && playlistRowAt(position) == pressedRow) {
        emit playlistRowSelected(pressedRow);
    }
    event->accept();
}

void NeniaWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const QPoint position = event->position().toPoint();
    const detail::NeniaLayout layout = detail::NeniaRenderer::layoutForSize(size());
    const auto control = detail::NeniaRenderer::transportControlAt(layout, position);
    m_runtime->pressedTransportControl = control;
    m_runtime->pressedPlaylistRow = -1;
    m_runtime->suppressPlaylistRelease = control != NeniaTransportControl::None;
    if (control == NeniaTransportControl::None) {
        const int row = playlistRowAt(position);
        if (row >= 0) {
            m_runtime->suppressPlaylistRelease = true;
            detail::ScopedPerformanceTimer activationTimer(
                detail::NeniaPerformanceDiagnostics::Timing::InputDoubleClickActivation);
            emit playlistRowSelected(row);
            emit playlistRowActivated(row);
        }
    }
    updateMousePresentation(position);
    event->accept();
}

void NeniaWidget::leaveEvent(QEvent *event)
{
    auto &presentation = m_runtime->displayPresentation;
    const bool presentationChanged =
        presentation.hoveredTransportControl != NeniaTransportControl::None
        || presentation.pressedTransportControl != NeniaTransportControl::None;
    presentation.hoveredTransportControl = NeniaTransportControl::None;
    presentation.pressedTransportControl = NeniaTransportControl::None;
    unsetCursor();
    if (presentationChanged) {
        requestVisualRefresh();
        scheduleWidgetUpdate();
    }
    QWidget::leaveEvent(event);
}

void NeniaWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    detail::NeniaPerformanceDiagnostics::instance().recordGeometryChange();
    updateMarqueeTimer();
    if (m_surfaceDirty || m_logicalSurface.size() != renderViewportFor(*this)) {
        scheduleWidgetUpdate();
    }
}

void NeniaWidget::hideEvent(QHideEvent *event)
{
    m_runtime->marqueeTimer.stop();
    m_runtime->marqueeDiagnosticsClock.invalidate();
    m_runtime->marqueeActive = false;
    m_runtime->marqueeClock.invalidate();
    detail::NeniaPerformanceDiagnostics::instance().recordGeometryChange();
    QWidget::hideEvent(event);
}

} // namespace nenia
