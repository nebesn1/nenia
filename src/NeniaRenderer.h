#pragma once

#include "NeniaExtendedMediaInfo.h"
#include "NeniaChannelMatrix.h"
#include "NeniaKeyboardRange.h"
#include "NeniaOctaveNaming.h"
#include "NeniaSpectrumFrame.h"

#include <nenia/NeniaMediaInfo.h>
#include <nenia/NeniaUiState.h>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QLine>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <array>

namespace nenia::detail {

struct DisplayState;

enum class NeniaDensity {
    Normal,
    Large,
    ExtraLarge
};

struct NeniaVisualMetrics
{
    NeniaDensity density = NeniaDensity::Normal;
    int titleTextPixelSize = 13;
    int bodyTextPixelSize = 12;
    int smallTextPixelSize = 10;
    int compactTextPixelSize = 8;
    int panelPadding = 8;
    int rowGap = 3;
    int sectionGap = 10;
    int headerLineHeight = 18;
    int playlistHeaderHeight = 35;
    int playlistRowHeight = 22;
    int playlistMarkerWidth = 4;
    int playlistMarkerHeight = 10;
    int matrixHeaderHeight = 18;
    int matrixTopInset = 4;
    int matrixChannelWidth = 29;
    int matrixNoteWidth = 24;
    int matrixProgramWidth = 29;
    int matrixLevelWidth = 19;
    int primaryContainerTitleInsetX = 7;
    int primaryContainerTitleInsetY = 2;
    int rightColumnInnerPadding = 3;
    int rightHeaderHeight = 0;
    int artworkPanelHeight = 150;
    int artworkPanelMaximumHeight = 230;
    int artworkHeaderHeight = 20;
    int artworkFrameInset = 9;
    int artworkContentInset = 3;
    int artworkMinimumSize = 112;
    int artworkMaximumSize = 240;
    int mediaInfoMinimumWidth = 124;
    int mediaInfoRowHeight = 20;
    int mediaInfoRowGap = 3;
    int mediaInfoLabelWidth = 72;
    int spectrumPanelHeight = 96;
    int spectrumPanelMaximumHeight = 160;
    int spectrumHeaderHeight = 20;
    int spectrumPlotLeft = 28;
    int spectrumPlotRight = 8;
    int spectrumPlotTop = 22;
    int spectrumPlotBottom = 29;
    int spectrumSegmentHeight = 3;
    int spectrumSegmentGap = 1;
    int artworkDiagonalSpacing = 22;
    int transportHeight = 24;
    int transportGap = 6;
    int transportButtonGap = 2;
    int transportIconSize = 12;
    QSize transportGlyphBoxSize = QSize(14, 16);
    int outerMargin = 6;
};

struct NeniaKeyboardGeometry
{
    QRect keyboardRect;
    QRect innerRect;
    std::array<int, 76> whiteBoundaries{};
    std::array<QRect, 75> whiteKeys{};
    std::array<int, 75> whiteNotes{};
    std::array<QRect, 53> blackKeys{};
    std::array<int, 53> blackNotes{};
    QRect lowActivityMarker;
    QRect highActivityMarker;
    int whiteKeyCount = 0;
    int blackKeyCount = 0;
    int lowestMidiNote = -1;
    int highestMidiNote = -1;
};

struct NeniaKeyboardRangeActivity
{
    bool low = false;
    bool high = false;
};

struct NeniaPanPointerGeometry
{
    QPointF center;
    QPointF endpoint;
    qreal circleRadius = 0.0;
    qreal pointerRadius = 0.0;
    int directionIndex = 4;
    bool valid = false;
};

struct NeniaActRingGeometry
{
    QPointF center;
    QRectF arcRect;
    qreal radius = 0.0;
    qreal strokeWidth = 0.0;
    // QPainter angles: begin at the lower-left and sweep counter-clockwise
    // through the top, leaving the opening centered at the bottom.
    qreal startAngleDegrees = 225.0;
    qreal sweepAngleDegrees = -270.0;
    float intensity = 0.0F;
    qreal illuminatedSweepAngleDegrees = 0.0;
    bool valid = false;
};

struct NeniaMatrixRowGeometry
{
    QRect row;
    QRect channelLabel;
    QRect note;
    QRect program;
    QRect level;
    QRect panIndicator;
    QRect panKnob;
    QRect keyboard;
};

struct NeniaTransportArrowHeadGeometry
{
    QPoint tip;
    QPoint baseCenter;
    QLine upperWing;
    QLine lowerWing;
    int depth = 0;
    int halfSpan = 0;
};

struct NeniaTransportArrowGeometry
{
    QLine shaft;
    NeniaTransportArrowHeadGeometry head;
};

struct NeniaLoopIconGeometry
{
    NeniaTransportArrowGeometry topRight;
    NeniaTransportArrowGeometry bottomLeft;
    QRectF repeatOneMarkerBounds;
    QPointF repeatOneBaseline;
    int topArrowY = 0;
    int bottomArrowY = 0;
    qreal repeatOneGap = 0.0;
    int markerClearance = 0;
};

struct NeniaShuffleIconGeometry
{
    std::array<NeniaTransportArrowGeometry, 2> paths{};
    qreal verticalOffset = 0.0;
};

struct NeniaLayout
{
    QSize viewport;
    QRect contentRect;
    NeniaVisualMetrics metrics;
    QRect playlistPanel;
    QRect playlistViewport;
    QRect channelMatrixPanel;
    QRect matrixHeaderRect;
    QRect matrixTitleRect;
    int matrixRowsTop = 0;
    int matrixRowPitch = 0;
    int matrixRowHeight = 0;
    std::array<NeniaMatrixRowGeometry, kVisibleTrackCount> matrixRows{};
    QRect rightColumn;
    QRect rightColumnInnerRect;
    QRect artworkPanel;
    QRect artworkControlsColumn;
    QRect artworkViewport;
    QRect artworkContentRect;
    QRect transportStrip;
    std::array<QRect, 5> transportButtons{};
    QRect artworkHeaderRect;
    QRect mediaInfoHeaderRect;
    QRect spectrumHeaderRect;
    QRect mediaInfoColumn;
    QRect spectrumPanel;
    int playlistRowHeight = 16;
    int mediaInfoRowsTop = 0;
};

struct DisplayPresentation
{
    qint64 positionMs = 0;
    qint64 durationMs = -1;
    QString format;
    QString playbackState = QStringLiteral("READY");
    NeniaPlaybackUiState playbackUi;
    OctaveNamingConvention octaveNamingConvention = kDefaultOctaveNamingConvention;
    NeniaExtendedMediaInfo extendedMediaInfo;
    QString location;
    NeniaPlaylistState playlist;
    NeniaTransportControl hoveredTransportControl = NeniaTransportControl::None;
    NeniaTransportControl pressedTransportControl = NeniaTransportControl::None;
    qint64 mediaMarqueeElapsedMs = 0;
};

enum class TextRole {
    Title,
    Label,
    Value,
    Track
};

class NeniaRenderer final
{
public:
    static constexpr int kPreferredWidth = 960;
    static constexpr int kPreferredHeight = 600;
    static constexpr int kMinimumWidth = 840;
    static constexpr int kMinimumHeight = 480;

    NeniaRenderer() = delete;

    static QSize logicalSize();
    static QSize preferredSize();
    static QSize minimumSize();
    static NeniaVisualMetrics visualMetricsForSize(const QSize &viewportSize);
    static NeniaLayout layoutForSize(const QSize &viewportSize);
    static NeniaKeyboardGeometry keyboardGeometry(
        const QRect &keyboardRect,
        KeyboardRangeMode mode = kDefaultKeyboardRangeMode);
    static NeniaKeyboardRangeActivity keyboardRangeActivity(
        const TrackState &track,
        KeyboardRangeMode mode);
    static QRect panelFrameRect(const QRect &panelRect);
    static QRect matrixRowRect(const NeniaLayout &layout, int channel);
    static QRect matrixKeyboardRect(const NeniaLayout &layout, int channel);
    static NeniaMatrixRowGeometry matrixRowGeometry(const NeniaLayout &layout, int channel);
    static QString channelMatrixTitleText();
    static QString midiVolumeText(int volume, bool authoritative);
    static NeniaPanPointerGeometry panPointerGeometry(const QRect &knobRect, int pan);
    static NeniaActRingGeometry actRingGeometry(const QRect &indicatorRect,
                                                float intensity);
    static QColor backgroundColor();
    static QColor gridColor();
    static QColor axisColor();
    static QColor panelOuterFrameColor();
    static QColor primaryColor();
    static QColor mediaInfoValueColor(const DisplayPresentation &presentation, int row);
    static QColor playlistEntryTextColor(bool selected, bool current);
    static QColor keyboardWhiteKeyBaseColor(int midiNote);
    static QFont fontForTextRole(TextRole role, int pixelSize);
    static bool bundledFontLoaded();
    static QString bundledFontFamily();
    static QString bundledFontResourcePath();
    static bool bundledFallbackFontLoaded();
    static QString bundledFallbackFontFamily();
    static QString bundledFallbackFontResourcePath();
    static QRect trackAreaRect();
    static QRect playlistPanelRect();
    static QRect playlistViewportRect();
    static QRect playlistRowRect(const NeniaLayout &layout,
                                 int row,
                                 int firstVisibleRow);
    static int playlistRowAt(const NeniaLayout &layout,
                             const QPoint &position,
                             int entryCount,
                             int firstVisibleRow);
    static int playlistVisibleRowCount(const NeniaLayout &layout);
    static NeniaTransportControl transportControlAt(const NeniaLayout &layout,
                                                    const QPoint &position);
    static QRect transportControlRect(const NeniaLayout &layout,
                                      NeniaTransportControl control);
    static QRect transportIconRect(const QRect &buttonRect, int preferredSide);
    static QRect transportGlyphBox(const QRect &buttonRect, const QSize &preferredSize);
    static NeniaLoopIconGeometry loopIconGeometry(
        const QRect &iconCanvas,
        NeniaLoopIndicatorState loopState,
        int markerPixelSize);
    static NeniaShuffleIconGeometry shuffleIconGeometry(const QRect &iconCanvas);
    static QRect channelMatrixPanelRect();
    static QRect rightColumnRect();
    static QRect artworkPanelRect();
    static QRect artworkViewportRect();
    static QRect mediaInfoColumnRect();
    static QRect spectrumPanelRect();
    static QRect mediaInfoValueRect(const NeniaLayout &layout,
                                    int row,
                                    bool extendedPage = false);
    static QString mediaInfoRowLabel(const DisplayPresentation &presentation, int row);
    static bool mediaInfoValueNeedsMarquee(const QString &value,
                                           const QRect &valueRect,
                                           int pixelSize);
    static int mediaInfoMarqueeOffset(const QString &value,
                                      const QRect &valueRect,
                                      int pixelSize,
                                      qint64 elapsedMs);
    static int mediaInfoMarqueeCycleDistance(const QString &value,
                                             const QRect &valueRect,
                                             int pixelSize);
    static int mediaInfoMarqueeTextX(const QString &value,
                                     const QRect &valueRect,
                                     int pixelSize,
                                     qint64 elapsedMs);
    static bool mediaInfoNeedsMarquee(const NeniaLayout &layout,
                                      const NeniaMediaInfo &mediaInfo,
                                      const DisplayPresentation &presentation);
    static QRect presentationRect(const QSize &availableSize);
    static void render(QImage &surface, const DisplayState &state);
    static void render(QImage &surface,
                       const DisplayState &state,
                       const SpectrumBands &spectrumBands);
    static void render(QImage &surface,
                       const DisplayState &state,
                       const SpectrumBands &spectrumBands,
                       const NeniaMediaInfo &mediaInfo,
                       const QImage &artwork);
    static void render(QImage &surface,
                       const DisplayState &state,
                       const SpectrumBands &spectrumBands,
                       const NeniaMediaInfo &mediaInfo,
                       const QImage &artwork,
                       const DisplayPresentation &presentation,
                       KeyboardRangeMode keyboardRange = kDefaultKeyboardRangeMode);
    static void renderStatic(QImage &surface);
};

} // namespace nenia::detail
