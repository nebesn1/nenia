#include "NeniaRenderer.h"
#include "NeniaAnimationEngine.h"
#include "NeniaColorScheme.h"
#include "NeniaPerformanceDiagnostics.h"
#include "NeniaTypography.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace nenia::detail {
namespace {

struct Palette
{
    QColor background;
    QColor panel;
    QColor panelRaised;
    QColor panelAlternate;
    QColor grid;
    QColor axis;
    QColor primary;
    QColor secondary;
    QColor accent;
    QColor meter;
    QColor text;
    QColor muted;
    QColor dim;
    QColor keyboard;
    QColor keyboardLight;
    QColor blackKey;
    QColor panelOuterFrame;
    QColor selectionBackground;
    QColor selectionText;
    QColor mediaInfoLabelText;
    QColor mediaInfoValueText;
    QColor playbackStatusText;
    QColor playlistItemText;
    QColor playlistPlayingText;
    QColor playlistPlayingSelectedText;
    QColor keyboardWhiteKey;
};

Palette makePalette()
{
    const NeniaColorScheme scheme = activeNeniaColorScheme();
    const NeniaColorRoles &roles = neniaColorSchemeDefinition(scheme).colors;
    return {
        roles.windowBackground,
        roles.panelBackground,
        roles.panelRaisedBackground,
        roles.panelAlternateBackground,
        roles.separatorColor,
        roles.axisColor,
        roles.primaryAccent,
        scheme == NeniaColorScheme::Classic
            ? roles.secondaryAccent : roles.secondaryText,
        roles.selectionAccent,
        roles.channelMeterColor,
        roles.primaryText,
        roles.secondaryText,
        roles.dimText,
        roles.keyboardBackground,
        roles.keyboardNaturalKey,
        roles.keyboardBlackKey,
        roles.borderColor,
        roles.selectionBackground,
        roles.selectionText,
        roles.mediaInfoLabelText,
        roles.mediaInfoValueText,
        roles.playbackStatusText,
        roles.playlistItemText,
        roles.playlistPlayingText,
        roles.playlistPlayingSelectedText,
        roles.keyboardWhiteKey
    };
}

QColor mediaInfoValueColorFor(const Palette &colors,
                              const DisplayPresentation &presentation,
                              int row,
                              NeniaColorScheme scheme)
{
    if (row < 0 || row >= 7) {
        return {};
    }
    if (presentation.playbackUi.extendedMediaInfo) {
        return colors.mediaInfoValueText;
    }
    if (presentation.extendedMediaInfo.directMidi) {
        if (row == 2 && scheme != NeniaColorScheme::Classic) {
            return colors.playbackStatusText;
        }
        return colors.mediaInfoValueText;
    }
    return row == 4 ? colors.playbackStatusText : colors.mediaInfoValueText;
}

QColor playlistEntryTextColorFor(const Palette &colors, bool selected, bool current)
{
    if (current) {
        return selected ? colors.playlistPlayingSelectedText
                        : colors.playlistPlayingText;
    }
    return selected ? colors.selectionText : colors.playlistItemText;
}

QColor keyboardWhiteKeyBaseColorFor(const Palette &colors, int midiNote)
{
    return midiNote % 12 == 0 ? colors.keyboardLight : colors.keyboardWhiteKey;
}

void configurePainter(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
}

float clampIntensity(float intensity)
{
    if (std::isnan(intensity)) {
        return 0.0f;
    }
    if (!std::isfinite(intensity)) {
        return intensity > 0.0f ? 1.0f : 0.0f;
    }
    return std::clamp(intensity, 0.0f, 1.0f);
}

QColor interpolateColor(const QColor &from, const QColor &to, float intensity)
{
    const float amount = clampIntensity(intensity);
    const auto channel = [amount](int start, int end) {
        return static_cast<int>(std::lround(start + (end - start) * amount));
    };
    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()),
                  channel(from.alpha(), to.alpha()));
}

float noteIntensity(const TrackState &track, int note)
{
    if (note < 0 || note >= kMidiNoteCount) {
        return 0.0f;
    }
    return clampIntensity(track.intensity.at(static_cast<std::size_t>(note)));
}

QString formatClock(qint64 milliseconds)
{
    if (milliseconds < 0) {
        return QStringLiteral("--:--");
    }

    const qint64 totalSeconds = milliseconds / 1'000;
    const qint64 minutes = totalSeconds / 60;
    const int seconds = static_cast<int>(totalSeconds % 60);
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatPosition(qint64 positionMs, qint64 durationMs)
{
    const QString total = durationMs > 0 ? formatClock(durationMs) : QStringLiteral("--:--");
    return formatClock(std::max<qint64>(positionMs, 0)) + QLatin1Char('/') + total;
}

QString displayField(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("--") : trimmed;
}

QString formatProgramGridValue(int program)
{
    if (program < 0 || program > 127) {
        return QStringLiteral("---");
    }
    return QStringLiteral("%1").arg(program + 1, 3, 10, QLatin1Char('0'));
}

int effectivePanForPointer(int pan)
{
    return pan >= 0 && pan <= 127 ? pan : 64;
}

int panDirectionIndex(int pan)
{
    const int effectivePan = effectivePanForPointer(pan);
    return std::clamp((effectivePan * 8 + 63) / 127, 0, 8);
}

QPoint panDirectionVector(int directionIndex)
{
    static constexpr QPoint offsets[] = {
        QPoint(-7, 0),
        QPoint(-6, -3),
        QPoint(-5, -5),
        QPoint(-3, -6),
        QPoint(0, -7),
        QPoint(3, -6),
        QPoint(5, -5),
        QPoint(6, -3),
        QPoint(7, 0)
    };

    return offsets[std::clamp(directionIndex, 0, 8)];
}

NeniaPanPointerGeometry panPointerGeometryInternal(const QRect &knobRect, int pan)
{
    NeniaPanPointerGeometry geometry;
    if (!knobRect.isValid()) {
        return geometry;
    }

    const int diameter = std::max(1, std::min(knobRect.width(), knobRect.height()) - 3);
    geometry.center = QPointF(knobRect.center());
    geometry.circleRadius = std::max(0.0, (diameter - 1) / 2.0);
    geometry.pointerRadius = std::max(0.0, geometry.circleRadius - 0.5);
    geometry.directionIndex = panDirectionIndex(pan);

    const QPoint direction = panDirectionVector(geometry.directionIndex);
    const qreal length = std::hypot(static_cast<qreal>(direction.x()),
                                    static_cast<qreal>(direction.y()));
    const QPointF unitDirection(static_cast<qreal>(direction.x()) / length,
                                static_cast<qreal>(direction.y()) / length);
    geometry.endpoint = geometry.center
        + QPointF(std::round(unitDirection.x() * geometry.pointerRadius),
                  std::round(unitDirection.y() * geometry.pointerRadius));
    geometry.valid = true;
    return geometry;
}

qreal actRingStrokeWidthForDiameter(int diameter)
{
    return std::clamp(static_cast<qreal>(diameter) * 0.075, 1.25, 4.0);
}

NeniaActRingGeometry actRingGeometryInternal(const QRect &indicatorRect, float intensity)
{
    NeniaActRingGeometry geometry;
    if (!indicatorRect.isValid()) {
        return geometry;
    }

    geometry.center = QPointF(indicatorRect.center());
    const int diameter = std::min(indicatorRect.width(), indicatorRect.height());
    geometry.strokeWidth = actRingStrokeWidthForDiameter(diameter);
    geometry.radius = std::max(0.0, (diameter - geometry.strokeWidth) / 2.0);
    geometry.arcRect = QRectF(geometry.center.x() - geometry.radius,
                              geometry.center.y() - geometry.radius,
                              geometry.radius * 2.0,
                              geometry.radius * 2.0);
    geometry.intensity = clampIntensity(intensity);
    geometry.illuminatedSweepAngleDegrees =
        geometry.sweepAngleDegrees * geometry.intensity;
    geometry.valid = true;
    return geometry;
}

NeniaVisualMetrics metricsForDensity(NeniaDensity density)
{
    NeniaVisualMetrics metrics;
    metrics.density = density;
    switch (density) {
    case NeniaDensity::Normal:
        metrics.primaryContainerTitleInsetX = metrics.panelPadding - 1;
        return metrics;
    case NeniaDensity::Large:
        metrics.titleTextPixelSize = 14;
        metrics.bodyTextPixelSize = 13;
        metrics.smallTextPixelSize = 11;
        metrics.compactTextPixelSize = 9;
        metrics.panelPadding = 10;
        metrics.rowGap = 4;
        metrics.sectionGap = 12;
        metrics.headerLineHeight = 20;
        metrics.playlistHeaderHeight = 40;
        metrics.playlistRowHeight = 26;
        metrics.playlistMarkerWidth = 5;
        metrics.playlistMarkerHeight = 12;
        metrics.matrixHeaderHeight = 19;
        metrics.matrixTopInset = 5;
        metrics.matrixChannelWidth = 46;
        metrics.matrixNoteWidth = 24;
        metrics.matrixProgramWidth = 29;
        metrics.matrixLevelWidth = 19;
        metrics.primaryContainerTitleInsetX = metrics.panelPadding - 1;
        metrics.rightColumnInnerPadding = 4;
        metrics.artworkPanelHeight = 190;
        metrics.artworkPanelMaximumHeight = 270;
        metrics.artworkHeaderHeight = 22;
        metrics.artworkFrameInset = 10;
        metrics.artworkMinimumSize = 136;
        metrics.artworkMaximumSize = 280;
        metrics.mediaInfoMinimumWidth = 142;
        metrics.mediaInfoRowHeight = 22;
        metrics.mediaInfoRowGap = 4;
        metrics.mediaInfoLabelWidth = 84;
        metrics.spectrumPanelHeight = 128;
        metrics.spectrumPanelMaximumHeight = 190;
        metrics.spectrumHeaderHeight = 22;
        metrics.spectrumPlotLeft = 31;
        metrics.spectrumPlotRight = 10;
        metrics.spectrumPlotTop = 24;
        metrics.spectrumPlotBottom = 34;
        metrics.spectrumSegmentHeight = 4;
        metrics.spectrumSegmentGap = 1;
        metrics.artworkDiagonalSpacing = 26;
        metrics.transportHeight = 28;
        metrics.transportGap = 8;
        metrics.transportButtonGap = 2;
        metrics.transportIconSize = 14;
        metrics.transportGlyphBoxSize = QSize(16, 18);
        metrics.outerMargin = 8;
        return metrics;
    case NeniaDensity::ExtraLarge:
        metrics.titleTextPixelSize = 15;
        metrics.bodyTextPixelSize = 14;
        metrics.smallTextPixelSize = 12;
        metrics.compactTextPixelSize = 10;
        metrics.panelPadding = 12;
        metrics.rowGap = 5;
        metrics.sectionGap = 14;
        metrics.headerLineHeight = 22;
        metrics.playlistHeaderHeight = 44;
        metrics.playlistRowHeight = 30;
        metrics.playlistMarkerWidth = 6;
        metrics.playlistMarkerHeight = 14;
        metrics.matrixHeaderHeight = 20;
        metrics.matrixTopInset = 6;
        metrics.matrixChannelWidth = 50;
        metrics.matrixNoteWidth = 25;
        metrics.matrixProgramWidth = 30;
        metrics.matrixLevelWidth = 20;
        metrics.primaryContainerTitleInsetX = metrics.panelPadding - 1;
        metrics.rightColumnInnerPadding = 5;
        metrics.artworkPanelHeight = 230;
        metrics.artworkPanelMaximumHeight = 310;
        metrics.artworkHeaderHeight = 24;
        metrics.artworkFrameInset = 11;
        metrics.artworkMinimumSize = 160;
        metrics.artworkMaximumSize = 320;
        metrics.mediaInfoMinimumWidth = 112;
        metrics.mediaInfoRowHeight = 24;
        metrics.mediaInfoRowGap = 5;
        metrics.mediaInfoLabelWidth = 96;
        metrics.spectrumPanelHeight = 160;
        metrics.spectrumPanelMaximumHeight = 220;
        metrics.spectrumHeaderHeight = 24;
        metrics.spectrumPlotLeft = 34;
        metrics.spectrumPlotRight = 12;
        metrics.spectrumPlotTop = 26;
        metrics.spectrumPlotBottom = 38;
        metrics.spectrumSegmentHeight = 5;
        metrics.spectrumSegmentGap = 2;
        metrics.artworkDiagonalSpacing = 30;
        metrics.transportHeight = 32;
        metrics.transportGap = 10;
        metrics.transportButtonGap = 3;
        metrics.transportIconSize = 16;
        metrics.transportGlyphBoxSize = QSize(18, 20);
        metrics.outerMargin = 10;
        return metrics;
    }
    return metrics;
}

QFont makeFont(TextRole role, int pixelSize)
{
    QFont font = typography::fontForRole(role);
    if (pixelSize > 0) {
        font.setPixelSize(pixelSize);
    }
    return font;
}

void drawGridText(QPainter &painter,
                  const QRect &rect,
                  const QString &value,
                  const QColor &color,
                  TextRole role,
                  int pixelSize,
                  Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    painter.setFont(makeFont(role, pixelSize));
    painter.setPen(color);
    const QString fitted = QFontMetrics(painter.font()).elidedText(
        value, Qt::ElideRight, std::max(0, rect.width()));
    painter.drawText(rect, alignment, fitted);
}

void drawTitleText(QPainter &painter,
                   const QRect &rect,
                   const QString &value,
                   const QColor &color,
                   int pixelSize = 9,
                   Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    drawGridText(painter, rect, value, color, TextRole::Title, pixelSize, alignment);
}

void drawLabelText(QPainter &painter,
                   const QRect &rect,
                   const QString &value,
                   const QColor &color,
                   int pixelSize = 7,
                   Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    drawGridText(painter, rect, value, color, TextRole::Label, pixelSize, alignment);
}

void drawTrackText(QPainter &painter,
                   const QRect &rect,
                   const QString &value,
                   const QColor &color,
                   int pixelSize = 7,
                   Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    drawGridText(painter, rect, value, color, TextRole::Track, pixelSize, alignment);
}

void drawValueText(QPainter &painter,
                   const QRect &rect,
                   const QString &value,
                   const QColor &color,
                   int pixelSize = 8,
                   Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    drawGridText(painter, rect, value, color, TextRole::Value, pixelSize, alignment);
}

constexpr qint64 kMediaMarqueeHoldMilliseconds = 900;
constexpr int kMediaMarqueePixelsPerSecond = 42;

int mediaInfoLabelWidthFor(const NeniaVisualMetrics &metrics,
                           int rowWidth,
                           bool extendedPage)
{
    const int desiredWidth = extendedPage
        ? metrics.mediaInfoLabelWidth
        : std::max(52, metrics.smallTextPixelSize * 6);
    return std::min(std::max(1, rowWidth - 12), desiredWidth);
}

QRect mediaInfoValueRectForRow(const NeniaLayout &layout,
                               int row,
                               bool extendedPage = false)
{
    if (row < 0 || row >= 7 || !layout.mediaInfoColumn.isValid()) {
        return {};
    }

    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect column = layout.mediaInfoColumn;
    const int rowHeight = metrics.mediaInfoRowHeight;
    const int rowGap = metrics.mediaInfoRowGap;
    const int rowTop = layout.mediaInfoRowsTop + row * (rowHeight + rowGap);
    const QRect rowRect(column.left(), rowTop, column.width(), rowHeight);
    const int labelWidth = mediaInfoLabelWidthFor(metrics, rowRect.width(), extendedPage);
    return QRect(rowRect.left() + labelWidth + metrics.rowGap + 2,
                 rowRect.top() + 1,
                 std::max(1, rowRect.width() - labelWidth - metrics.rowGap - 9),
                 rowRect.height() - 2);
}

std::array<QString, 7> mediaInfoRowLabels(const DisplayPresentation &presentation)
{
    if (presentation.extendedMediaInfo.directMidi) {
        if (presentation.playbackUi.extendedMediaInfo) {
            return {QStringLiteral("TRACKS"), QStringLiteral("CHANNELS"),
                    QStringLiteral("PPQN"), QStringLiteral("TEMPO"),
                    QStringLiteral("TIME SIG"), QStringLiteral("COPYRIGHT"),
                    QStringLiteral("SF PATH")};
        }
        return {QStringLiteral("TITLE"), QStringLiteral("SOURCE"),
                QStringLiteral("PLAYBACK"), QStringLiteral("POSITION"),
                QStringLiteral("DURATION"), QStringLiteral("FORMAT"),
                QStringLiteral("SOUNDFONT")};
    }
    if (presentation.playbackUi.extendedMediaInfo) {
        return {QStringLiteral("TRACK"), QStringLiteral("DATE"),
                QStringLiteral("COMPOSER"), QStringLiteral("ALBUM ARTIST"),
                QStringLiteral("PERFORMER"), QStringLiteral("GENRE"),
                QStringLiteral("DISC")};
    }
    return {QStringLiteral("TITLE"), QStringLiteral("ARTIST"),
            QStringLiteral("ALBUM"), QStringLiteral("LOCATION"),
            QStringLiteral("PLAYBACK"), QStringLiteral("POSITION"),
            QStringLiteral("FORMAT")};
}

std::array<QString, 7> mediaInfoRowValues(const NeniaMediaInfo &mediaInfo,
                                          const DisplayPresentation &presentation)
{
    if (presentation.extendedMediaInfo.directMidi) {
        if (presentation.playbackUi.extendedMediaInfo) {
            return {displayField(presentation.extendedMediaInfo.midiTrackCount),
                    displayField(presentation.extendedMediaInfo.midiChannelCount),
                    displayField(presentation.extendedMediaInfo.midiPpqn),
                    displayField(presentation.extendedMediaInfo.midiTempo),
                    displayField(presentation.extendedMediaInfo.midiTimeSignature),
                    displayField(presentation.extendedMediaInfo.midiCopyright),
                    displayField(presentation.extendedMediaInfo.soundFontPath)};
        }
        return {displayField(mediaInfo.title),
                displayField(presentation.extendedMediaInfo.midiSourcePath),
                displayField(presentation.playbackState),
                formatPosition(presentation.positionMs, presentation.durationMs),
                displayField(presentation.extendedMediaInfo.midiDuration),
                displayField(presentation.extendedMediaInfo.midiFormat),
                displayField(presentation.extendedMediaInfo.soundFontNameAndStatus)};
    }
    if (presentation.playbackUi.extendedMediaInfo) {
        return {displayField(presentation.extendedMediaInfo.trackNumber),
                displayField(presentation.extendedMediaInfo.date),
                displayField(presentation.extendedMediaInfo.composer),
                displayField(presentation.extendedMediaInfo.albumArtist),
                displayField(presentation.extendedMediaInfo.performer),
                displayField(presentation.extendedMediaInfo.genre),
                displayField(presentation.extendedMediaInfo.discNumber)};
    }
    return {displayField(mediaInfo.title), displayField(mediaInfo.artist),
            displayField(mediaInfo.album), displayField(presentation.location),
            displayField(presentation.playbackState),
            formatPosition(presentation.positionMs, presentation.durationMs),
            displayField(presentation.format)};
}

int mediaInfoTextWidth(const QString &value, int pixelSize)
{
    return QFontMetrics(makeFont(TextRole::Value, pixelSize)).horizontalAdvance(value);
}

bool mediaInfoValueNeedsMarqueeInternal(const QString &value,
                                        const QRect &valueRect,
                                        int pixelSize)
{
    return valueRect.isValid() && valueRect.width() > 0
        && mediaInfoTextWidth(value, pixelSize) > valueRect.width();
}

int mediaInfoMarqueeCycleDistanceInternal(const QString &value,
                                          const QRect &valueRect,
                                          int pixelSize)
{
    if (!mediaInfoValueNeedsMarqueeInternal(value, valueRect, pixelSize)) {
        return 0;
    }
    return std::max(1, mediaInfoTextWidth(value, pixelSize) + valueRect.width());
}

int mediaInfoMarqueeOffsetInternal(const QString &value,
                                   const QRect &valueRect,
                                   int pixelSize,
                                   qint64 elapsedMs)
{
    if (!mediaInfoValueNeedsMarqueeInternal(value, valueRect, pixelSize)) {
        return 0;
    }

    const int travel = mediaInfoMarqueeCycleDistanceInternal(value, valueRect, pixelSize);
    const qint64 travelMs = std::max<qint64>(1,
        (static_cast<qint64>(travel) * 1'000 + kMediaMarqueePixelsPerSecond - 1)
        / kMediaMarqueePixelsPerSecond);
    if (elapsedMs <= kMediaMarqueeHoldMilliseconds) {
        return 0;
    }

    const qint64 phaseMs = (elapsedMs - kMediaMarqueeHoldMilliseconds) % travelMs;
    return -static_cast<int>((phaseMs * travel + travelMs / 2) / travelMs);
}

int mediaInfoMarqueeTextXInternal(const QString &value,
                                  const QRect &valueRect,
                                  int pixelSize,
                                  qint64 elapsedMs)
{
    return valueRect.left()
        + mediaInfoMarqueeOffsetInternal(value, valueRect, pixelSize, elapsedMs);
}

void drawMarqueeValueText(QPainter &painter,
                          const QRect &valueRect,
                          const QString &value,
                          const QColor &color,
                          int pixelSize,
                          qint64 elapsedMs)
{
    const QFont font = makeFont(TextRole::Value, pixelSize);
    const QFontMetrics fontMetrics(font);
    if (!mediaInfoValueNeedsMarqueeInternal(value, valueRect, pixelSize)) {
        drawValueText(painter, valueRect, value, color, pixelSize,
                      Qt::AlignRight | Qt::AlignVCenter);
        return;
    }

    const int textWidth = fontMetrics.horizontalAdvance(value);
    const int cycleDistance = mediaInfoMarqueeCycleDistanceInternal(
        value, valueRect, pixelSize);
    const int startX = mediaInfoMarqueeTextXInternal(value, valueRect, pixelSize, elapsedMs);
    painter.save();
    painter.setClipRect(valueRect);
    painter.setFont(font);
    painter.setPen(color);
    const auto drawCopy = [&](int x) {
        if (x > valueRect.right() || x + textWidth < valueRect.left()) {
            return;
        }
        painter.drawText(QRect(x, valueRect.top(), textWidth + 1, valueRect.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, value);
    };
    drawCopy(startX);
    drawCopy(startX + cycleDistance);
    painter.restore();
}

void drawRule(QPainter &painter, int x1, int y1, int x2, int y2, const QColor &color)
{
    painter.setPen(color);
    painter.drawLine(QPoint(x1, y1), QPoint(x2, y2));
}

NeniaTransportArrowHeadGeometry arrowHeadGeometry(const QPoint &tip,
                                                   qreal directionX,
                                                   qreal directionY,
                                                   int headDepth,
                                                   int headHalfSpan)
{
    const qreal directionLength = std::hypot(directionX, directionY);
    if (directionLength <= 0.0) {
        return {tip, tip, QLine(tip, tip), QLine(tip, tip), 0, 0};
    }

    const qreal unitX = directionX / directionLength;
    const qreal unitY = directionY / directionLength;
    const qreal perpendicularX = -unitY;
    const qreal perpendicularY = unitX;
    const qreal baseX = tip.x() - unitX * headDepth;
    const qreal baseY = tip.y() - unitY * headDepth;
    const QPoint baseCenter(qRound(baseX), qRound(baseY));
    QPoint first(qRound(baseX + perpendicularX * headHalfSpan),
                 qRound(baseY + perpendicularY * headHalfSpan));
    QPoint second(qRound(baseX - perpendicularX * headHalfSpan),
                  qRound(baseY - perpendicularY * headHalfSpan));
    if (first.y() > second.y()
        || (first.y() == second.y() && first.x() > second.x())) {
        std::swap(first, second);
    }
    return {tip, baseCenter, QLine(tip, first), QLine(tip, second),
            headDepth, headHalfSpan};
}

NeniaTransportArrowHeadGeometry orthogonalArrowHeadGeometry(const QPoint &tip,
                                                            int legLength)
{
    const QPoint horizontalEnd(tip.x() - legLength, tip.y());
    const QPoint verticalEnd(tip.x(), tip.y() + legLength);
    const QPoint baseCenter(
        qRound((horizontalEnd.x() + verticalEnd.x()) / 2.0),
        qRound((horizontalEnd.y() + verticalEnd.y()) / 2.0));
    return {tip, baseCenter, QLine(tip, horizontalEnd), QLine(tip, verticalEnd),
            legLength, legLength};
}

int transportChevronDepth(const QSize &glyphBoxSize)
{
    return glyphBoxSize.width() <= 14 ? 3 : 4;
}

int transportChevronHalfSpan(const QSize &glyphBoxSize)
{
    return glyphBoxSize.height() >= 20 ? 3 : 2;
}

int loopMarkerClearance(const QSize &glyphBoxSize)
{
    // XL gets one extra logical pixel so the rasterized glyph retains at
    // least three fully clear pixel rows above and below the marker.
    return glyphBoxSize.height() >= 20 ? 4 : 2;
}

void drawPixelChevronArrow(QPainter &painter,
                           const NeniaTransportArrowGeometry &arrow,
                           const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(color);
    pen.setWidth(1);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.drawLine(arrow.shaft);
    painter.drawLine(arrow.head.upperWing);
    painter.drawLine(arrow.head.lowerWing);
    painter.restore();
}

QRect boxFrameRectInternal(const QRect &panelRect)
{
    return panelRect.adjusted(0, 0, -1, -1);
}

QRect panelOuterFrameRectInternal(const QRect &panelRect)
{
    return panelRect.adjusted(1, 1, -1, -1);
}

void drawFrameEdges(QPainter &painter, const QRect &frameRect, const QColor &border)
{
    if (!frameRect.isValid()) {
        return;
    }

    painter.fillRect(QRect(frameRect.left(), frameRect.top(), frameRect.width(), 1), border);
    painter.fillRect(QRect(frameRect.left(), frameRect.bottom(), frameRect.width(), 1), border);
    if (frameRect.height() > 2) {
        painter.fillRect(QRect(frameRect.left(), frameRect.top() + 1, 1,
                               frameRect.height() - 2), border);
        painter.fillRect(QRect(frameRect.right(), frameRect.top() + 1, 1,
                               frameRect.height() - 2), border);
    }
}

void drawPanelFrame(QPainter &painter, const QRect &panelRect, const QColor &border)
{
    drawFrameEdges(painter, boxFrameRectInternal(panelRect), border);
}

void drawPanelOuterFrame(QPainter &painter,
                         const QRect &panelRect,
                         const QColor &border)
{
    drawFrameEdges(painter, panelOuterFrameRectInternal(panelRect), border);
}

void drawBox(QPainter &painter,
             const QRect &rect,
             const QColor &fill,
             const QColor &border)
{
    painter.fillRect(rect, fill);
    drawPanelFrame(painter, rect, border);
}

QRect matrixRowRectInternal(const NeniaLayout &layout, int channel)
{
    if (channel < 0 || channel >= kVisibleTrackCount
        || !layout.channelMatrixPanel.isValid()) {
        return {};
    }

    const QRect panel = layout.channelMatrixPanel;
    const int trackHeight = std::max(1, std::min(layout.matrixRowPitch - 1,
                                                 layout.matrixRowHeight));
    return QRect(panel.left() + layout.metrics.panelPadding / 2,
                 layout.matrixRowsTop + channel * layout.matrixRowPitch,
                 std::max(1, panel.width() - layout.metrics.panelPadding), trackHeight);
}

NeniaMatrixRowGeometry matrixRowGeometryInternal(const NeniaLayout &layout, int channel)
{
    NeniaMatrixRowGeometry geometry;
    geometry.row = matrixRowRectInternal(layout, channel);
    if (!geometry.row.isValid()) {
        return geometry;
    }

    const NeniaVisualMetrics &metrics = layout.metrics;
    const int top = geometry.row.top();
    const int compactHeight = std::max(1, std::min(metrics.bodyTextPixelSize,
                                                  geometry.row.height() - 2));
    const int textY = top + (geometry.row.height() - compactHeight) / 2;
    int x = geometry.row.left() + 3;
    geometry.channelLabel = QRect(x, textY, metrics.matrixChannelWidth, compactHeight);
    x = geometry.channelLabel.right() + 3;
    geometry.note = QRect(x, textY, metrics.matrixNoteWidth, compactHeight);
    x = geometry.note.right() + 2;
    geometry.program = QRect(x, textY, metrics.matrixProgramWidth, compactHeight);
    x = geometry.program.right() + 2;
    geometry.level = QRect(x, textY, metrics.matrixLevelWidth, compactHeight);
    x = geometry.level.right() + 4;

    constexpr int minimumKeyboardWidth = 150;
    constexpr int keyboardToIndicatorGap = 4;
    const int keyboardRight = geometry.row.right() - 3;
    const int availableControlWidth = keyboardRight - x + 1;
    const int maximumIndicatorSide = std::max(
        1, availableControlWidth - keyboardToIndicatorGap - minimumKeyboardWidth);
    const int usableRowHeight = std::max(1, geometry.row.height() - 2);
    const int preferredIndicatorSide = std::max(
        1, static_cast<int>(std::floor(usableRowHeight * 0.90)));
    const int indicatorSide = std::min(preferredIndicatorSide, maximumIndicatorSide);
    const int indicatorY = top + (geometry.row.height() - indicatorSide) / 2;
    geometry.panIndicator = QRect(x, indicatorY, indicatorSide, indicatorSide);
    const qreal ringStrokeWidth = actRingStrokeWidthForDiameter(indicatorSide);
    const int preferredKnobInset = static_cast<int>(std::ceil(
        ringStrokeWidth / 2.0 + indicatorSide * 0.08));
    const int inset = std::min((indicatorSide - 1) / 2, preferredKnobInset);
    geometry.panKnob = geometry.panIndicator.adjusted(inset, inset, -inset, -inset);
    x = geometry.panIndicator.right() + keyboardToIndicatorGap + 1;

    const int keyboardHeight = std::max(1, geometry.row.height() - 4);
    const int keyboardY = top + (geometry.row.height() - keyboardHeight) / 2;
    geometry.keyboard = QRect(x, keyboardY, std::max(1, keyboardRight - x + 1),
                              keyboardHeight);
    return geometry;
}

QRect matrixKeyboardRectInternal(const NeniaLayout &layout, int channel)
{
    return matrixRowGeometryInternal(layout, channel).keyboard;
}

void drawPlaylist(QPainter &painter,
                  const Palette &colors,
                  const NeniaLayout &layout,
                  const NeniaPlaylistState &playlist)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect panel = layout.playlistPanel;
    const QRect viewport = layout.playlistViewport;
    const QColor outline = playlist.interactionHighlighted
        ? colors.primary
        : colors.panelOuterFrame;
    painter.fillRect(panel, colors.panel);
    drawPanelOuterFrame(painter, panel, outline);
    drawTitleText(painter,
                  QRect(panel.left() + metrics.panelPadding / 2, panel.top() + 2,
                        std::max(1, panel.width() - metrics.panelPadding),
                        metrics.headerLineHeight),
                  QCoreApplication::translate("NeniaUi", "PLAYLIST"),
                  colors.secondary,
                  metrics.titleTextPixelSize);
    drawLabelText(painter,
                  QRect(panel.left() + metrics.panelPadding / 2,
                        panel.top() + metrics.headerLineHeight + 1,
                        std::max(1, panel.width() - metrics.panelPadding),
                        metrics.smallTextPixelSize + 4),
                  playlist.sourceLabel,
                  colors.muted,
                  metrics.smallTextPixelSize);
    drawRule(painter, panel.left() + metrics.panelPadding / 2,
             viewport.top() - metrics.rowGap,
             panel.right() - metrics.panelPadding / 2,
             viewport.top() - metrics.rowGap, colors.grid);

    painter.save();
    painter.setClipRect(viewport);
    const int rowHeight = metrics.playlistRowHeight;
    const int firstRow = std::max(0, playlist.firstVisibleRow);
    const int visibleRows = NeniaRenderer::playlistVisibleRowCount(layout);
    const int lastRow = std::min(static_cast<int>(playlist.entries.size()),
                                 firstRow + visibleRows);
    for (int row = firstRow; row < lastRow; ++row) {
        const NeniaPlaylistEntry &entry = playlist.entries.at(row);
        const QRect rowRect = NeniaRenderer::playlistRowRect(layout, row, firstRow);
        const int y = rowRect.top();
        if (entry.selected) {
            painter.fillRect(rowRect, colors.selectionBackground);
        }
        if (entry.current) {
            painter.fillRect(QRect(viewport.left() + metrics.panelPadding / 2,
                                   y + (rowHeight - metrics.playlistMarkerHeight) / 2,
                                   metrics.playlistMarkerWidth,
                                   metrics.playlistMarkerHeight),
                             colors.accent);
        }

        const int labelX = viewport.left() + metrics.panelPadding + 1;
        const int labelWidth = std::max(1, viewport.right() - labelX - 3);
        drawTrackText(painter, QRect(labelX, y, labelWidth, rowHeight), entry.displayName,
                      playlistEntryTextColorFor(colors, entry.selected, entry.current),
                      metrics.bodyTextPixelSize);
    }
    painter.restore();

    if (!playlist.hasPlaylist) {
        drawLabelText(painter,
                      viewport.adjusted(4, 22, -4, -viewport.height() + 42),
                      QCoreApplication::translate("NeniaUi", "PRESS O TO OPEN"),
                      colors.muted,
                      metrics.smallTextPixelSize,
                      Qt::AlignCenter | Qt::AlignVCenter);
    }

}

void drawTransportControls(QPainter &painter,
                           const Palette &colors,
                           const NeniaLayout &layout,
                           const DisplayPresentation &presentation)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const auto &playbackUi = presentation.playbackUi;
    const bool isPlaying = presentation.playbackState == QStringLiteral("PLAYING");
    painter.save();
    for (int index = 0; index < static_cast<int>(layout.transportButtons.size()); ++index) {
        const auto control = static_cast<NeniaTransportControl>(index);
        const QRect button = layout.transportButtons.at(static_cast<std::size_t>(index));
        const bool hovered = presentation.hoveredTransportControl == control;
        const bool pressed = presentation.pressedTransportControl == control;
        const bool loopActive = control == NeniaTransportControl::Loop
            && playbackUi.loop != NeniaLoopIndicatorState::StopAfterCurrent;
        const bool shuffleActive = control == NeniaTransportControl::Shuffle
            && playbackUi.shuffle;
        const bool active = loopActive || shuffleActive;

        const QColor border = pressed ? colors.accent
            : active ? colors.primary
                     : hovered ? colors.axis : colors.grid;
        const QColor fill = pressed ? colors.background
            : hovered ? colors.panelRaised : colors.background;
        painter.fillRect(button, fill);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(button.adjusted(0, 0, -1, -1));

        const bool usesCenteredGlyphBox = control == NeniaTransportControl::Loop
            || control == NeniaTransportControl::Shuffle;
        const QRect iconRect = usesCenteredGlyphBox
            ? NeniaRenderer::transportGlyphBox(button, metrics.transportGlyphBoxSize)
            : NeniaRenderer::transportIconRect(button, metrics.transportIconSize);
        const QColor iconColor = pressed || active ? colors.primary
            : hovered ? colors.secondary : colors.dim;
        painter.setPen(QPen(iconColor, 1));
        painter.setBrush(iconColor);
        const int left = iconRect.left();
        const int right = iconRect.right();
        const int top = iconRect.top();
        const int bottom = iconRect.bottom();
        const int centerY = iconRect.center().y();
        switch (control) {
        case NeniaTransportControl::Previous: {
            painter.drawLine(left + 1, top, left + 1, bottom);
            QPolygon triangle;
            triangle << QPoint(right, top) << QPoint(left + 2, centerY)
                     << QPoint(right, bottom);
            painter.drawPolygon(triangle);
            break;
        }
        case NeniaTransportControl::PlayPause:
            if (isPlaying) {
                const int barWidth = std::max(2, iconRect.width() / 4);
                painter.drawRect(QRect(left + 1, top + 1, barWidth, std::max(1, iconRect.height() - 2)));
                painter.drawRect(QRect(right - barWidth, top + 1, barWidth,
                                       std::max(1, iconRect.height() - 2)));
            } else {
                QPolygon triangle;
                triangle << QPoint(left + 1, top) << QPoint(right, centerY)
                         << QPoint(left + 1, bottom);
                painter.drawPolygon(triangle);
            }
            break;
        case NeniaTransportControl::Next: {
            QPolygon triangle;
            triangle << QPoint(left, top) << QPoint(right - 2, centerY)
                     << QPoint(left, bottom);
            painter.drawPolygon(triangle);
            painter.drawLine(right, top, right, bottom);
            break;
        }
        case NeniaTransportControl::Loop: {
            const int markerPixelSize = std::max(5, metrics.smallTextPixelSize - 4);
            const NeniaLoopIconGeometry geometry = NeniaRenderer::loopIconGeometry(
                iconRect, playbackUi.loop, markerPixelSize);
            drawPixelChevronArrow(painter, geometry.topRight, iconColor);
            drawPixelChevronArrow(painter, geometry.bottomLeft, iconColor);
            if (geometry.repeatOneMarkerBounds.isValid()) {
                painter.save();
                painter.setFont(makeFont(TextRole::Label, markerPixelSize));
                painter.setPen(iconColor);
                painter.drawText(geometry.repeatOneBaseline, QStringLiteral("1"));
                painter.restore();
            }
            break;
        }
        case NeniaTransportControl::Shuffle: {
            const NeniaShuffleIconGeometry geometry =
                NeniaRenderer::shuffleIconGeometry(iconRect);
            for (const NeniaTransportArrowGeometry &path : geometry.paths) {
                drawPixelChevronArrow(painter, path, iconColor);
            }
            break;
        }
        case NeniaTransportControl::None:
            break;
        }

    }
    painter.restore();
}

void drawKeyboard(QPainter &painter,
                  const QRect &rect,
                  const Palette &colors,
                  const TrackState &track,
                  KeyboardRangeMode mode)
{
    const NeniaKeyboardGeometry geometry = NeniaRenderer::keyboardGeometry(rect, mode);
    if (!geometry.keyboardRect.isValid() || geometry.innerRect.width() < 20
        || geometry.innerRect.height() < 6 || geometry.whiteKeyCount <= 0) {
        return;
    }

    painter.fillRect(rect, colors.keyboard);
    painter.setPen(colors.grid);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    for (int key = 0; key < geometry.whiteKeyCount; ++key) {
        const QRect whiteKey = geometry.whiteKeys[key];
        const int note = geometry.whiteNotes[key];
        const QColor baseColor = keyboardWhiteKeyBaseColorFor(colors, note);
        const QColor whiteColor = interpolateColor(baseColor, colors.primary,
                                                   noteIntensity(track, note));

        painter.fillRect(whiteKey, whiteColor);
        if (key + 1 < geometry.whiteKeyCount) {
            drawRule(painter, whiteKey.right() + 1, geometry.innerRect.top(),
                     whiteKey.right() + 1, geometry.innerRect.bottom(), colors.blackKey);
        }
    }

    for (int key = 0; key < geometry.blackKeyCount; ++key) {
        const QRect blackKey = geometry.blackKeys[key];
        const QColor blackColor = interpolateColor(colors.blackKey, colors.accent,
                                                   noteIntensity(track,
                                                                 geometry.blackNotes[key]));
        painter.setPen(blackColor);
        painter.setBrush(blackColor);
        painter.drawRect(blackKey);
    }
    painter.setBrush(Qt::NoBrush);

    const NeniaKeyboardRangeActivity activity =
        NeniaRenderer::keyboardRangeActivity(track, mode);
    const auto drawMarker = [&](const QRect &marker, bool pointsRight) {
        if (!marker.isValid()) {
            return;
        }
        const int middleY = marker.center().y();
        const QPoint points[] = {
            QPoint(pointsRight ? marker.left() : marker.right(), marker.top()),
            QPoint(pointsRight ? marker.right() : marker.left(), middleY),
            QPoint(pointsRight ? marker.left() : marker.right(), marker.bottom())
        };
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.accent);
        painter.drawConvexPolygon(points, 3);
        painter.restore();
    };
    if (activity.low) {
        drawMarker(geometry.lowActivityMarker, true);
    }
    if (activity.high) {
        drawMarker(geometry.highActivityMarker, false);
    }
}

void drawMidiPanKnob(QPainter &painter,
                     const Palette &colors,
                     const QRect &rect,
                     const TrackState &track,
                     bool pointerVisible);

void drawActRing(QPainter &painter,
                 const QColor &inactiveColor,
                 const QColor &activeColor,
                 const NeniaActRingGeometry &geometry)
{
    if (!geometry.valid || geometry.radius <= 0.0 || geometry.strokeWidth <= 0.0) {
        return;
    }

    const int startAngle = qRound(geometry.startAngleDegrees * 16.0);
    const int fullSweep = qRound(geometry.sweepAngleDegrees * 16.0);
    QPen pen(inactiveColor);
    pen.setWidthF(geometry.strokeWidth);
    pen.setCapStyle(Qt::RoundCap);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen);
    painter.drawArc(geometry.arcRect, startAngle, fullSweep);

    if (geometry.illuminatedSweepAngleDegrees == 0.0) {
        return;
    }
    pen.setColor(activeColor);
    painter.setPen(pen);
    painter.drawArc(geometry.arcRect, startAngle,
                    qRound(geometry.illuminatedSweepAngleDegrees * 16.0));
}

void drawTrackRows(QPainter &painter,
                   const Palette &colors,
                   const NeniaLayout &layout,
                   const DisplayState &state,
                   KeyboardRangeMode keyboardRange,
                   OctaveNamingConvention octaveNamingConvention)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect panel = layout.channelMatrixPanel;
    painter.fillRect(panel, colors.panel);
    drawLabelText(painter, layout.matrixTitleRect,
                  NeniaRenderer::channelMatrixTitleText(), colors.secondary,
                  metrics.smallTextPixelSize);
    for (int row = 0; row < kVisibleTrackCount; ++row) {
        const QColor &rowFill = (row % 2 == 0) ? colors.panel : colors.panelAlternate;
        const NeniaMatrixRowGeometry &geometry = layout.matrixRows.at(
            static_cast<std::size_t>(row));
        drawBox(painter, geometry.row, rowFill, colors.grid);

        const TrackState &track = state.tracks.at(static_cast<std::size_t>(row));
        const QString channel = matrixChannelLabel(row);
        const QString note = track.lastNote < 0 ? QStringLiteral("--")
            : noteNameForMidi(track.lastNote, octaveNamingConvention);
        drawTrackText(painter, geometry.channelLabel, channel, colors.secondary,
                      metrics.compactTextPixelSize);
        drawTrackText(painter, geometry.note, note, colors.text,
                      metrics.compactTextPixelSize);
        drawTrackText(painter, geometry.program,
                      QStringLiteral("P%1").arg(formatProgramGridValue(track.program)),
                      colors.muted, metrics.compactTextPixelSize);
        const bool authoritativeVolume =
            track.volumeProvenance != MidiControllerProvenance::Unknown;
        drawTrackText(painter, geometry.level,
                      NeniaRenderer::midiVolumeText(track.volume, authoritativeVolume),
                      authoritativeVolume ? colors.muted : colors.dim,
                      std::min(metrics.compactTextPixelSize, 7));

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        const NeniaActRingGeometry ring = actRingGeometryInternal(
            geometry.panIndicator, trackActivity(track));
        drawActRing(painter, colors.grid, colors.primary, ring);
        // The circular control body is filled by drawMidiPanKnob so it cannot
        // mask the antialiased outer ACT arc at the ring's diagonal corners.
        drawMidiPanKnob(painter, colors, geometry.panKnob, track, true);
        painter.restore();
        drawKeyboard(painter, geometry.keyboard, colors, track, keyboardRange);
    }
}

void drawRightColumnBackground(QPainter &painter,
                               const Palette &colors,
                               const NeniaLayout &layout)
{
    const QRect column = layout.rightColumn;
    painter.fillRect(column, colors.panel);
}

void drawArtworkPanel(QPainter &painter,
                      const Palette &colors,
                      const NeniaLayout &layout,
                      const QImage &artwork)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect viewport = layout.artworkViewport;
    const QRect content = layout.artworkContentRect;
    drawLabelText(painter, layout.artworkHeaderRect,
                  QCoreApplication::translate("NeniaUi", "ARTWORK"),
                  colors.secondary, metrics.smallTextPixelSize);

    painter.fillRect(viewport, colors.background);
    painter.setPen(colors.grid);
    painter.drawRect(viewport.adjusted(0, 0, -1, -1));

    painter.save();
    painter.setClipRect(content);
    const int gridSpacing = std::max(8, metrics.artworkDiagonalSpacing);
    for (int x = content.left() + gridSpacing; x < content.right(); x += gridSpacing) {
        drawRule(painter, x, content.top(), x, content.bottom(), colors.panelRaised);
    }
    for (int y = content.top() + gridSpacing; y < content.bottom(); y += gridSpacing) {
        drawRule(painter, content.left(), y, content.right(), y, colors.panelRaised);
    }
    for (int x = content.left() - content.height(); x <= content.right(); x += gridSpacing) {
        drawRule(painter, x, content.bottom(), x + content.height(), content.top(), colors.dim);
    }
    painter.restore();

    if (artwork.isNull()) {
        painter.save();
        painter.setClipRect(content);
        drawValueText(painter, content.adjusted(4, content.height() / 4,
                                                 -4, -content.height() / 2),
                      QCoreApplication::translate("NeniaUi", "NO IMAGE"),
                      colors.text, metrics.bodyTextPixelSize, Qt::AlignCenter);
        drawLabelText(painter,
                      content.adjusted(4, content.height() / 2,
                                      -4, -std::max(4, content.height() / 7)),
                      QCoreApplication::translate("NeniaUi", "STATIC VIEWPORT"),
                      colors.muted, metrics.smallTextPixelSize, Qt::AlignCenter);
        painter.restore();
    } else {
        const QRect imageArea = content;
        const QSize fittedSize = artwork.size().scaled(imageArea.size(), Qt::KeepAspectRatio);
        const QRect target(QPoint(imageArea.left() + (imageArea.width() - fittedSize.width()) / 2,
                                  imageArea.top() + (imageArea.height() - fittedSize.height()) / 2),
                           fittedSize);
        painter.save();
        painter.setClipRect(content);
        painter.drawImage(target, artwork, artwork.rect());
        painter.restore();
    }

    // Keep the frame accents above both the placeholder and the clipped image.
    // The content QRect reaches the right/bottom marker coordinates because
    // QRect::right() and bottom() are inclusive pixel coordinates.
    const int left = viewport.left() + 2;
    const int right = viewport.right() - 3;
    const int top = viewport.top() + 2;
    const int bottom = viewport.bottom() - 3;
    const int marker = 6;
    drawRule(painter, left, top, left + marker, top, colors.primary);
    drawRule(painter, left, top, left, top + marker, colors.primary);
    drawRule(painter, right - marker, top, right, top, colors.primary);
    drawRule(painter, right, top, right, top + marker, colors.primary);
    drawRule(painter, left, bottom, left + marker, bottom, colors.primary);
    drawRule(painter, left, bottom - marker, left, bottom, colors.primary);
    drawRule(painter, right - marker, bottom, right, bottom, colors.primary);
    drawRule(painter, right, bottom - marker, right, bottom, colors.primary);
}

void drawInfoRow(QPainter &painter,
                 const Palette &colors,
                 const NeniaVisualMetrics &metrics,
                 const QRect &rect,
                 const QString &label,
                 const QString &value,
                 const QColor &valueColor,
                 qint64 marqueeElapsedMs,
                 bool extendedPage)
{
    drawBox(painter, rect, colors.panelRaised, colors.grid);
    const int labelWidth = mediaInfoLabelWidthFor(metrics, rect.width(), extendedPage);
    drawLabelText(painter, QRect(rect.left() + metrics.rowGap, rect.top() + 1,
                                 labelWidth, rect.height() - 2), label,
                  colors.mediaInfoLabelText, metrics.smallTextPixelSize);
    const QRect valueRect(rect.left() + labelWidth + metrics.rowGap + 2, rect.top() + 1,
                          std::max(1, rect.width() - labelWidth - metrics.rowGap - 9),
                          rect.height() - 2);
    drawMarqueeValueText(painter, valueRect, value, valueColor,
                         metrics.bodyTextPixelSize, marqueeElapsedMs);
}

void drawMediaInfoColumn(QPainter &painter,
                         const Palette &colors,
                         const NeniaLayout &layout,
                         const NeniaMediaInfo &mediaInfo,
                         const DisplayPresentation &presentation)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect column = layout.mediaInfoColumn;
    const bool extended = presentation.playbackUi.extendedMediaInfo;
    drawLabelText(painter, layout.mediaInfoHeaderRect,
                  extended
                      ? QCoreApplication::translate("NeniaUi", "MEDIA INFO / DETAILS")
                      : QCoreApplication::translate("NeniaUi", "MEDIA INFO"),
                  colors.secondary, metrics.smallTextPixelSize);

    const int rowHeight = metrics.mediaInfoRowHeight;
    const int rowGap = metrics.mediaInfoRowGap;
    const int rowTop = layout.mediaInfoRowsTop;
    const NeniaColorScheme scheme = activeNeniaColorScheme();
    const std::array<QString, 7> labels = mediaInfoRowLabels(presentation);
    const std::array<QString, 7> values = mediaInfoRowValues(mediaInfo, presentation);
    for (int row = 0; row < 7; ++row) {
        const QByteArray labelUtf8 = labels.at(static_cast<std::size_t>(row)).toUtf8();
        const QString label = QCoreApplication::translate("NeniaUi", labelUtf8.constData());
        drawInfoRow(painter, colors, metrics,
                    QRect(column.left(), rowTop + row * (rowHeight + rowGap),
                          column.width(), rowHeight),
                    label, values.at(static_cast<std::size_t>(row)),
                    mediaInfoValueColorFor(colors, presentation, row, scheme),
                    !extended && row == 4 ? 0 : presentation.mediaMarqueeElapsedMs,
                    extended);
    }
}

void drawSpectrum(QPainter &painter,
                  const Palette &colors,
                  const NeniaLayout &layout,
                  const SpectrumBands &spectrumBands)
{
    const NeniaVisualMetrics &metrics = layout.metrics;
    const QRect panel = layout.spectrumPanel;
    const QRect plot(panel.left() + metrics.spectrumPlotLeft,
                     panel.top() + metrics.spectrumPlotTop,
                     std::max(1, panel.width() - metrics.spectrumPlotLeft
                                       - metrics.spectrumPlotRight),
                     std::max(12, panel.height() - metrics.spectrumPlotTop
                                       - metrics.spectrumPlotBottom));
    drawLabelText(painter, layout.spectrumHeaderRect,
                  QCoreApplication::translate("NeniaUi", "SPECTRUM / LIVE FFT"),
                  colors.secondary, metrics.smallTextPixelSize);

    painter.fillRect(plot, colors.background);
    painter.setPen(colors.grid);
    painter.drawRect(plot.adjusted(0, 0, -1, -1));

    for (int step = 1; step < 4; ++step) {
        const int y = plot.top() + step * plot.height() / 4;
        drawRule(painter, plot.left() + 1, y, plot.right() - 2, y, colors.panelRaised);
    }
    for (int step = 1; step < 8; ++step) {
        const int x = plot.left() + step * plot.width() / 8;
        drawRule(painter, x, plot.top() + 1, x, plot.bottom() - 2, colors.panelRaised);
    }

    drawRule(painter, plot.left(), plot.bottom() - 1, plot.right() - 1, plot.bottom() - 1,
             colors.axis);
    drawRule(painter, plot.left(), plot.top(), plot.left(), plot.bottom() - 1, colors.axis);

    for (int bin = 0; bin < 70; ++bin) {
        const int x = plot.left() + 3 + (bin * (plot.width() - 8)) / 70;
        drawRule(painter, x, plot.bottom() - 4, x, plot.bottom() - 2, colors.grid);
    }

    const QRect barArea = plot.adjusted(3, 2, -3, -3);
    const int barGap = std::max(1, metrics.rowGap - 1);
    const int barWidth = std::max(
        1, (barArea.width() - (kSpectrumBandCount - 1) * barGap) / kSpectrumBandCount);
    const int segmentHeight = metrics.spectrumSegmentHeight;
    const int segmentGap = metrics.spectrumSegmentGap;
    const int segmentPitch = segmentHeight + segmentGap;
    const int segmentCount = (barArea.height() + segmentGap) / segmentPitch;
    for (int band = 0; band < kSpectrumBandCount; ++band) {
        const float level = clampIntensity(spectrumBands.at(static_cast<std::size_t>(band)));
        const int activeSegments = static_cast<int>(std::floor(level * segmentCount));
        const int x = barArea.left() + band * (barWidth + barGap);
        for (int segment = 0; segment < activeSegments; ++segment) {
            const int y = barArea.bottom() - segment * segmentPitch - segmentHeight + 1;
            painter.fillRect(QRect(x, y, barWidth, segmentHeight),
                             interpolateColor(colors.dim, colors.primary, level));
        }
    }

    drawLabelText(painter, QRect(panel.left() + 3, plot.top(), metrics.spectrumPlotLeft - 8,
                                 metrics.smallTextPixelSize + 3), QStringLiteral("0"), colors.muted,
                  metrics.smallTextPixelSize,
                  Qt::AlignRight | Qt::AlignVCenter);
    drawLabelText(painter, QRect(panel.left() + 3, plot.bottom() - metrics.smallTextPixelSize,
                                 metrics.spectrumPlotLeft - 8, metrics.smallTextPixelSize + 3),
                  QStringLiteral("-60"), colors.muted, metrics.smallTextPixelSize,
                  Qt::AlignRight | Qt::AlignVCenter);
    const int tickWidth = std::max(metrics.bodyTextPixelSize * 3, plot.width() / 6);
    const int tickTop = panel.bottom() - metrics.spectrumPlotBottom + 4;
    const int tickHeight = metrics.smallTextPixelSize + 3;
    drawLabelText(painter, QRect(plot.left(), tickTop, tickWidth, tickHeight),
                  QStringLiteral("250"), colors.muted, metrics.smallTextPixelSize);
    drawLabelText(painter, QRect(plot.left() + plot.width() / 5, tickTop, tickWidth, tickHeight),
                  QStringLiteral("500"), colors.muted, metrics.smallTextPixelSize);
    drawLabelText(painter, QRect(plot.left() + 2 * plot.width() / 5, tickTop,
                                 tickWidth, tickHeight),
                  QStringLiteral("1K"), colors.muted, metrics.smallTextPixelSize);
    drawLabelText(painter, QRect(plot.left() + 3 * plot.width() / 5, tickTop,
                                 tickWidth, tickHeight),
                  QStringLiteral("2K"), colors.muted, metrics.smallTextPixelSize);
    drawLabelText(painter, QRect(plot.left() + 4 * plot.width() / 5, tickTop,
                                 tickWidth, tickHeight),
                  QStringLiteral("4K"), colors.muted, metrics.smallTextPixelSize);
}

void drawMidiPanKnob(QPainter &painter,
                     const Palette &colors,
                     const QRect &rect,
                     const TrackState &track,
                     bool pointerVisible)
{
    const NeniaPanPointerGeometry pointer = panPointerGeometryInternal(rect, track.pan);
    if (!pointer.valid) {
        return;
    }

    const QRectF circle(pointer.center.x() - pointer.circleRadius,
                        pointer.center.y() - pointer.circleRadius,
                        pointer.circleRadius * 2.0,
                        pointer.circleRadius * 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.panelRaised);
    painter.drawEllipse(circle);
    QPen circlePen(colors.dim);
    circlePen.setWidth(1);
    circlePen.setCapStyle(Qt::FlatCap);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(circlePen);
    if (pointer.circleRadius <= 0.0) {
        painter.drawPoint(pointer.center);
    } else {
        painter.drawEllipse(circle);
    }

    if (!pointerVisible) {
        return;
    }

    // Keep the one-pixel PAN direction marker crisp; antialiasing is limited
    // to the circular knob outline/body and the surrounding ACT arc.
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pointerPen(colors.primary);
    pointerPen.setWidth(1);
    pointerPen.setCapStyle(Qt::FlatCap);
    painter.setPen(pointerPen);
    const QPoint center = pointer.center.toPoint();
    const QPoint endpoint = pointer.endpoint.toPoint();
    const int deltaX = endpoint.x() - center.x();
    const int deltaY = endpoint.y() - center.y();
    const int steps = std::max(std::abs(deltaX), std::abs(deltaY));
    for (int step = 0; step <= steps; ++step) {
        const qreal amount = steps == 0 ? 0.0 : static_cast<qreal>(step) / steps;
        painter.drawPoint(QPoint(static_cast<int>(std::round(center.x() + deltaX * amount)),
                                 static_cast<int>(std::round(center.y() + deltaY * amount))));
    }
}

void drawMajorOuterFrames(QPainter &painter,
                          const Palette &colors,
                          const NeniaLayout &layout)
{
    drawPanelOuterFrame(painter, layout.channelMatrixPanel, colors.panelOuterFrame);
    drawPanelOuterFrame(painter, layout.rightColumn, colors.panelOuterFrame);
}

} // namespace

QSize NeniaRenderer::logicalSize()
{
    return preferredSize();
}

QSize NeniaRenderer::preferredSize()
{
    return QSize(kPreferredWidth, kPreferredHeight);
}

QSize NeniaRenderer::minimumSize()
{
    return QSize(kMinimumWidth, kMinimumHeight);
}

NeniaVisualMetrics NeniaRenderer::visualMetricsForSize(const QSize &viewportSize)
{
    const int minimumDimension = std::min(viewportSize.width(), viewportSize.height());
    NeniaDensity density = NeniaDensity::Normal;
    if (minimumDimension >= 780 && viewportSize.width() >= 1200) {
        density = NeniaDensity::ExtraLarge;
    } else if (minimumDimension >= 560) {
        density = NeniaDensity::Large;
    }
    return metricsForDensity(density);
}

NeniaKeyboardGeometry NeniaRenderer::keyboardGeometry(const QRect &keyboardRect,
                                                       KeyboardRangeMode mode)
{
    NeniaKeyboardGeometry geometry;
    geometry.keyboardRect = keyboardRect;
    if (!keyboardRect.isValid()) {
        return geometry;
    }

    geometry.innerRect = keyboardRect.adjusted(2, 1, -2, -2);
    if (!geometry.innerRect.isValid()) {
        return geometry;
    }

    const KeyboardRangeDescriptor &range = keyboardRangeDescriptor(mode);
    geometry.lowestMidiNote = range.lowestMidiNote;
    geometry.highestMidiNote = range.highestMidiNote;
    geometry.whiteKeyCount = range.whiteKeyCount;
    geometry.blackKeyCount = range.blackKeyCount;

    const int innerWidth = geometry.innerRect.width();
    if (innerWidth < geometry.whiteKeyCount) {
        geometry.whiteKeyCount = 0;
        geometry.blackKeyCount = 0;
        return geometry;
    }

    int whiteIndex = 0;
    int blackIndex = 0;
    for (int note = range.lowestMidiNote; note <= range.highestMidiNote; ++note) {
        if (isBlackMidiPitchClass(note)) {
            geometry.blackNotes[static_cast<std::size_t>(blackIndex++)] = note;
        } else {
            geometry.whiteNotes[static_cast<std::size_t>(whiteIndex++)] = note;
        }
    }
    Q_ASSERT(whiteIndex == geometry.whiteKeyCount);
    Q_ASSERT(blackIndex == geometry.blackKeyCount);

    for (int boundary = 0; boundary <= geometry.whiteKeyCount; ++boundary) {
        geometry.whiteBoundaries[static_cast<std::size_t>(boundary)] =
            geometry.innerRect.left() + (boundary * innerWidth) / geometry.whiteKeyCount;
    }
    for (int key = 0; key < geometry.whiteKeyCount; ++key) {
        const int left = geometry.whiteBoundaries[static_cast<std::size_t>(key)];
        const int right = geometry.whiteBoundaries[static_cast<std::size_t>(key + 1)];
        geometry.whiteKeys[static_cast<std::size_t>(key)] =
            QRect(left, geometry.innerRect.top(), right - left,
                  geometry.innerRect.height());
    }

    const int blackHeight = std::max(3, geometry.innerRect.height() * 3 / 5);
    int rightWhiteIndex = 0;
    for (int key = 0; key < geometry.blackKeyCount; ++key) {
        const int note = geometry.blackNotes[static_cast<std::size_t>(key)];
        while (rightWhiteIndex < geometry.whiteKeyCount
               && geometry.whiteNotes[static_cast<std::size_t>(rightWhiteIndex)] < note) {
            ++rightWhiteIndex;
        }
        Q_ASSERT(rightWhiteIndex > 0 && rightWhiteIndex < geometry.whiteKeyCount);
        if (rightWhiteIndex <= 0 || rightWhiteIndex >= geometry.whiteKeyCount) {
            continue;
        }

        const int boundary = geometry.whiteBoundaries[
            static_cast<std::size_t>(rightWhiteIndex)];
        const int leftWhiteWidth = geometry.whiteKeys[
            static_cast<std::size_t>(rightWhiteIndex - 1)].width();
        const int rightWhiteWidth = geometry.whiteKeys[
            static_cast<std::size_t>(rightWhiteIndex)].width();
        const int narrowestWhite = std::min(leftWhiteWidth, rightWhiteWidth);
        const int blackWidth = std::max(1, std::min(16, narrowestWhite * 2 / 3));
        const int left = std::clamp(boundary - blackWidth / 2,
                                    geometry.innerRect.left(),
                                    geometry.innerRect.right() - blackWidth + 1);
        geometry.blackKeys[static_cast<std::size_t>(key)] =
            QRect(left, geometry.innerRect.top(), blackWidth, blackHeight);
    }

    const int markerHeight = std::min(5, geometry.innerRect.height());
    const int markerY = geometry.innerRect.bottom() - markerHeight + 1;
    const int lowMarkerWidth = std::min(
        4, geometry.whiteKeys.front().width());
    const int highMarkerWidth = std::min(
        4, geometry.whiteKeys[static_cast<std::size_t>(geometry.whiteKeyCount - 1)].width());
    geometry.lowActivityMarker = QRect(geometry.innerRect.left(), markerY,
                                       lowMarkerWidth, markerHeight);
    geometry.highActivityMarker = QRect(geometry.innerRect.right() - highMarkerWidth + 1,
                                        markerY, highMarkerWidth, markerHeight);
    return geometry;
}

NeniaKeyboardRangeActivity NeniaRenderer::keyboardRangeActivity(
    const TrackState &track,
    KeyboardRangeMode mode)
{
    NeniaKeyboardRangeActivity activity;
    const KeyboardRangeDescriptor &range = keyboardRangeDescriptor(mode);
    if (range.lowestMidiNote == 0 && range.highestMidiNote == kMidiNoteCount - 1) {
        return activity;
    }

    for (int note = 0; note < range.lowestMidiNote && !activity.low; ++note) {
        activity.low = noteIntensity(track, note) > 0.0F;
    }
    for (int note = range.highestMidiNote + 1;
         note < kMidiNoteCount && !activity.high; ++note) {
        activity.high = noteIntensity(track, note) > 0.0F;
    }
    return activity;
}

QRect NeniaRenderer::panelFrameRect(const QRect &panelRect)
{
    return panelOuterFrameRectInternal(panelRect);
}

QRect NeniaRenderer::matrixRowRect(const NeniaLayout &layout, int channel)
{
    return matrixRowRectInternal(layout, channel);
}

QRect NeniaRenderer::matrixKeyboardRect(const NeniaLayout &layout, int channel)
{
    return matrixKeyboardRectInternal(layout, channel);
}

NeniaMatrixRowGeometry NeniaRenderer::matrixRowGeometry(const NeniaLayout &layout, int channel)
{
    if (channel < 0 || channel >= kVisibleTrackCount) {
        return {};
    }
    return layout.matrixRows.at(static_cast<std::size_t>(channel));
}

QString NeniaRenderer::midiVolumeText(int volume, bool authoritative)
{
    if (!authoritative || volume < 0 || volume > 127) {
        return QStringLiteral("V---");
    }
    return QStringLiteral("V%1").arg(volume, 3, 10, QLatin1Char('0'));
}

QString NeniaRenderer::channelMatrixTitleText()
{
    return QCoreApplication::translate("NeniaUi", "CHANNEL MATRIX");
}

NeniaPanPointerGeometry NeniaRenderer::panPointerGeometry(const QRect &knobRect, int pan)
{
    return panPointerGeometryInternal(knobRect, pan);
}

NeniaActRingGeometry NeniaRenderer::actRingGeometry(const QRect &indicatorRect,
                                                     float intensity)
{
    return actRingGeometryInternal(indicatorRect, intensity);
}

NeniaLayout NeniaRenderer::layoutForSize(const QSize &viewportSize)
{
    NeniaLayout layout;
    layout.viewport = viewportSize;
    if (viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return layout;
    }

    layout.metrics = visualMetricsForSize(viewportSize);
    const NeniaVisualMetrics &metrics = layout.metrics;
    const int width = viewportSize.width();
    const int height = viewportSize.height();
    const int gap = metrics.sectionGap;
    constexpr int matrixMinimum = 320;
    constexpr int playlistMinimum = 200;
    constexpr int rightMinimum = 280;

    const int margin = std::min(metrics.outerMargin,
                                std::max(0, std::min(width, height) / 4));
    layout.contentRect = QRect(margin, margin, std::max(1, width - 2 * margin),
                               std::max(1, height - 2 * margin));
    const int contentWidth = layout.contentRect.width();
    const int contentHeight = layout.contentRect.height();
    int playlistWidth = std::clamp(contentWidth * 23 / 100, playlistMinimum, 360);
    int rightWidth = std::max(rightMinimum, contentWidth * 35 / 100);
    int matrixWidth = contentWidth - playlistWidth - rightWidth - 2 * gap;
    if (matrixWidth < matrixMinimum) {
        const int required = matrixMinimum - matrixWidth;
        const int rightReduction = std::min(required, rightWidth - rightMinimum);
        rightWidth -= rightReduction;
        matrixWidth += rightReduction;
        const int remaining = matrixMinimum - matrixWidth;
        const int playlistReduction = std::min(remaining, playlistWidth - playlistMinimum);
        playlistWidth -= playlistReduction;
        matrixWidth += playlistReduction;
    }
    if (matrixWidth < 1) {
        matrixWidth = 1;
    }

    const int playlistX = layout.contentRect.left();
    const int matrixX = playlistX + playlistWidth + gap;
    const int rightX = matrixX + matrixWidth + gap;
    const int rightActualWidth = std::max(1, layout.contentRect.right() - rightX + 1);

    layout.playlistPanel = QRect(playlistX, layout.contentRect.top(), playlistWidth,
                                 contentHeight);
    layout.channelMatrixPanel = QRect(matrixX, layout.contentRect.top(), matrixWidth,
                                      contentHeight);
    layout.rightColumn = QRect(rightX, layout.contentRect.top(), rightActualWidth,
                               contentHeight);

    layout.playlistViewport = QRect(playlistX + 3,
                                    layout.contentRect.top() + metrics.playlistHeaderHeight,
                                    std::max(1, playlistWidth - 6),
                                    std::max(1, contentHeight
                                                   - metrics.playlistHeaderHeight
                                                   - metrics.panelPadding / 2 - 4));
    layout.playlistRowHeight = metrics.playlistRowHeight;

    const QRect right = layout.rightColumn;
    const int rightColumnInnerInset = 1 + metrics.rightColumnInnerPadding;
    layout.rightColumnInnerRect = right.adjusted(rightColumnInnerInset,
                                                 rightColumnInnerInset,
                                                 -rightColumnInnerInset,
                                                 -rightColumnInnerInset);
    const QRect &rightInner = layout.rightColumnInnerRect;
    const int artworkTop = rightInner.top() + metrics.rightHeaderHeight;
    const int maxArtworkHeight = std::max(
        1, rightInner.height() - metrics.rightHeaderHeight - metrics.sectionGap
               - 112 - metrics.sectionGap - 1);
    const int mediaRowsTopOffset = 3 + metrics.smallTextPixelSize + 4 + metrics.rowGap;
    const int sevenRowMediaInfoHeight = mediaRowsTopOffset
        + 7 * metrics.mediaInfoRowHeight + 6 * metrics.mediaInfoRowGap
        + metrics.panelPadding / 2;
    const int artworkLowerBound = std::max(1,
                                           std::min(sevenRowMediaInfoHeight,
                                                    maxArtworkHeight));
    const int preferredArtworkLowerBound = std::min(metrics.artworkPanelHeight - 8,
                                                    maxArtworkHeight);
    const int effectiveArtworkLowerBound =
        std::max(artworkLowerBound, preferredArtworkLowerBound);
    const int artworkUpperBound = std::max(effectiveArtworkLowerBound,
                                           std::min(metrics.artworkPanelMaximumHeight,
                                                    maxArtworkHeight));
    const int artworkHeight = std::clamp(contentHeight * 31 / 100,
                                         effectiveArtworkLowerBound, artworkUpperBound);
    layout.artworkPanel = QRect(rightInner.left(),
                                artworkTop,
                                rightInner.width(),
                                std::max(1, artworkHeight));
    const int contentSectionTitleTop = layout.artworkPanel.top() + 3;
    const int contentSectionTitleHeight = metrics.smallTextPixelSize + 4;
    layout.mediaInfoRowsTop = contentSectionTitleTop + contentSectionTitleHeight
        + metrics.rowGap;
    const QRect rightFrame = panelFrameRect(layout.rightColumn);
    const int primaryTitleLeft = rightFrame.left() + metrics.primaryContainerTitleInsetX;
    const int primaryTitleTop = rightFrame.top() + metrics.primaryContainerTitleInsetY;
    const int primaryTitleHeight = metrics.smallTextPixelSize + 5;
    const int artworkWidthLimit = std::max(
        1, layout.artworkPanel.width() - metrics.artworkFrameInset
               - metrics.mediaInfoMinimumWidth - metrics.sectionGap
               - metrics.panelPadding - metrics.rightColumnInnerPadding);
    const int topSectionContentBottom = layout.artworkPanel.bottom()
        - metrics.panelPadding / 2;
    const int artworkHeightLimit = std::max(
        1, topSectionContentBottom - layout.mediaInfoRowsTop + 1
               - metrics.transportGap - metrics.transportHeight);
    const int artworkSize = std::max(1, std::min({artworkWidthLimit,
                                                   artworkHeightLimit,
                                                   metrics.artworkMaximumSize}));
    layout.artworkViewport = QRect(layout.artworkPanel.left() + metrics.artworkFrameInset,
                                   layout.mediaInfoRowsTop,
                                   artworkSize,
                                   artworkSize);
    const int contentInset = std::min(metrics.artworkContentInset,
                                      std::max(0, (artworkSize - 1) / 2));
    layout.artworkContentRect = QRect(layout.artworkViewport.left() + contentInset,
                                      layout.artworkViewport.top() + contentInset,
                                      std::max(1, artworkSize - 2 * contentInset),
                                      std::max(1, artworkSize - 2 * contentInset));
    const int mediaInfoLeft = layout.artworkViewport.right() + metrics.sectionGap;
    layout.artworkHeaderRect = QRect(
        primaryTitleLeft, primaryTitleTop,
        std::max(1, mediaInfoLeft - primaryTitleLeft - metrics.sectionGap),
        primaryTitleHeight);
    layout.mediaInfoHeaderRect = QRect(
        mediaInfoLeft, primaryTitleTop,
        std::max(1, rightInner.right() - mediaInfoLeft - metrics.panelPadding + 1),
        primaryTitleHeight);
    layout.mediaInfoColumn = QRect(mediaInfoLeft, contentSectionTitleTop,
                                   std::max(1, rightInner.right() - mediaInfoLeft
                                                   - metrics.panelPadding + 1),
                                   std::max(1, layout.artworkPanel.bottom() + 1
                                                   - contentSectionTitleTop
                                                   - metrics.panelPadding / 2));
    layout.artworkControlsColumn = QRect(layout.artworkViewport.left(),
                                         layout.mediaInfoColumn.top(),
                                         layout.artworkViewport.width(),
                                         layout.mediaInfoColumn.height());
    layout.transportStrip = QRect(
        layout.artworkViewport.left(),
        layout.artworkViewport.bottom() + 1 + metrics.transportGap,
        layout.artworkViewport.width(), metrics.transportHeight);
    const int totalButtonGap = metrics.transportButtonGap * 4;
    const int buttonBaseWidth = std::max(
        1, (layout.transportStrip.width() - totalButtonGap) / 5);
    const int buttonRemainder = std::max(
        0, layout.transportStrip.width() - totalButtonGap - buttonBaseWidth * 5);
    int buttonX = layout.transportStrip.left();
    for (int button = 0; button < static_cast<int>(layout.transportButtons.size()); ++button) {
        const int buttonWidth = buttonBaseWidth + (button < buttonRemainder ? 1 : 0);
        layout.transportButtons[static_cast<std::size_t>(button)] =
            QRect(buttonX, layout.transportStrip.top(), buttonWidth,
                  layout.transportStrip.height());
        buttonX += buttonWidth + metrics.transportButtonGap;
    }

    const int spectrumTop = layout.artworkPanel.bottom() + metrics.sectionGap;
    layout.spectrumPanel = QRect(rightInner.left(),
                                 spectrumTop,
                                 rightInner.width(),
                                 std::max(1, rightInner.bottom() - spectrumTop + 1));
    layout.matrixHeaderRect = QRect(
        panelFrameRect(layout.channelMatrixPanel).left()
            + metrics.primaryContainerTitleInsetX,
        panelFrameRect(layout.channelMatrixPanel).top()
            + metrics.primaryContainerTitleInsetY,
        std::max(1, layout.channelMatrixPanel.width() - 2 * metrics.panelPadding),
        metrics.smallTextPixelSize + 5);
    layout.matrixTitleRect = layout.matrixHeaderRect;
    layout.spectrumHeaderRect = QRect(
        primaryTitleLeft,
        layout.spectrumPanel.top() + 3,
        std::max(1, layout.spectrumPanel.right() - primaryTitleLeft + 1),
        metrics.smallTextPixelSize + 5);
    layout.matrixRowsTop = layout.channelMatrixPanel.top()
        + metrics.matrixHeaderHeight + metrics.matrixTopInset;
    const int matrixAvailableHeight = std::max(
        1, layout.channelMatrixPanel.height() - metrics.matrixHeaderHeight
               - metrics.matrixTopInset - metrics.panelPadding);
    layout.matrixRowPitch = std::max(1, matrixAvailableHeight / kVisibleTrackCount);
    layout.matrixRowHeight = std::max(1, layout.matrixRowPitch - 1);
    for (int channel = 0; channel < kVisibleTrackCount; ++channel) {
        layout.matrixRows.at(static_cast<std::size_t>(channel)) =
            matrixRowGeometryInternal(layout, channel);
    }
    return layout;
}

QColor NeniaRenderer::backgroundColor()
{
    return makePalette().background;
}

QColor NeniaRenderer::gridColor()
{
    return makePalette().grid;
}

QColor NeniaRenderer::axisColor()
{
    return makePalette().axis;
}

QColor NeniaRenderer::panelOuterFrameColor()
{
    return makePalette().panelOuterFrame;
}

QColor NeniaRenderer::primaryColor()
{
    return makePalette().primary;
}

QColor NeniaRenderer::mediaInfoValueColor(const DisplayPresentation &presentation,
                                          int row)
{
    return mediaInfoValueColorFor(makePalette(), presentation, row,
                                  activeNeniaColorScheme());
}

QColor NeniaRenderer::playlistEntryTextColor(bool selected, bool current)
{
    return playlistEntryTextColorFor(makePalette(), selected, current);
}

QColor NeniaRenderer::keyboardWhiteKeyBaseColor(int midiNote)
{
    return keyboardWhiteKeyBaseColorFor(makePalette(), midiNote);
}

QFont NeniaRenderer::fontForTextRole(TextRole role, int pixelSize)
{
    return makeFont(role, pixelSize);
}

bool NeniaRenderer::bundledFontLoaded()
{
    return typography::bundledFontLoaded();
}

QString NeniaRenderer::bundledFontFamily()
{
    return typography::bundledFontFamily();
}

QString NeniaRenderer::bundledFontResourcePath()
{
    return typography::bundledFontResourcePath();
}

bool NeniaRenderer::bundledFallbackFontLoaded()
{
    return typography::bundledFallbackFontLoaded();
}

QString NeniaRenderer::bundledFallbackFontFamily()
{
    return typography::bundledFallbackFontFamily();
}

QString NeniaRenderer::bundledFallbackFontResourcePath()
{
    return typography::bundledFallbackFontResourcePath();
}

QRect NeniaRenderer::trackAreaRect()
{
    return layoutForSize(preferredSize()).channelMatrixPanel;
}

QRect NeniaRenderer::playlistPanelRect()
{
    return layoutForSize(preferredSize()).playlistPanel;
}

QRect NeniaRenderer::playlistViewportRect()
{
    return layoutForSize(preferredSize()).playlistViewport;
}

int NeniaRenderer::playlistVisibleRowCount(const NeniaLayout &layout)
{
    return std::max(1, layout.playlistViewport.height()
                           / std::max(1, layout.playlistRowHeight));
}

QRect NeniaRenderer::playlistRowRect(const NeniaLayout &layout,
                                     int row,
                                     int firstVisibleRow)
{
    const int firstRow = std::max(0, firstVisibleRow);
    if (row < firstRow || row >= firstRow + playlistVisibleRowCount(layout)) {
        return {};
    }
    const int rowTop = layout.playlistViewport.top()
        + (row - firstRow) * layout.playlistRowHeight;
    return QRect(layout.playlistViewport.left(), rowTop,
                 layout.playlistViewport.width(), layout.playlistRowHeight);
}

int NeniaRenderer::playlistRowAt(const NeniaLayout &layout,
                                 const QPoint &position,
                                 int entryCount,
                                 int firstVisibleRow)
{
    if (entryCount <= 0 || !layout.playlistViewport.contains(position)) {
        return -1;
    }
    const int firstRow = std::max(0, firstVisibleRow);
    const int rowOffset = (position.y() - layout.playlistViewport.top())
        / std::max(1, layout.playlistRowHeight);
    const int row = firstRow + rowOffset;
    if (row >= entryCount
        || !playlistRowRect(layout, row, firstRow).contains(position)) {
        return -1;
    }
    return row;
}

NeniaTransportControl NeniaRenderer::transportControlAt(const NeniaLayout &layout,
                                                         const QPoint &position)
{
    for (int index = 0; index < static_cast<int>(layout.transportButtons.size()); ++index) {
        if (layout.transportButtons.at(static_cast<std::size_t>(index)).contains(position)) {
            return static_cast<NeniaTransportControl>(index);
        }
    }
    return NeniaTransportControl::None;
}

QRect NeniaRenderer::transportControlRect(const NeniaLayout &layout,
                                         NeniaTransportControl control)
{
    const int index = static_cast<int>(control);
    if (index < 0 || index >= static_cast<int>(layout.transportButtons.size())) {
        return {};
    }
    return layout.transportButtons.at(static_cast<std::size_t>(index));
}

QRect NeniaRenderer::transportIconRect(const QRect &buttonRect, int preferredSide)
{
    if (!buttonRect.isValid() || preferredSide <= 0) {
        return {};
    }

    const int iconSide = std::max(1, std::min({preferredSide,
                                               buttonRect.width() - 6,
                                               buttonRect.height() - 6}));
    return QRect(buttonRect.center().x() - iconSide / 2,
                 buttonRect.center().y() - iconSide / 2,
                 iconSide, iconSide);
}

QRect NeniaRenderer::transportGlyphBox(const QRect &buttonRect,
                                       const QSize &preferredSize)
{
    if (!buttonRect.isValid() || preferredSize.width() <= 0
        || preferredSize.height() <= 0) {
        return {};
    }

    const int availableWidth = std::max(1, buttonRect.width() - 6);
    const int availableHeight = std::max(1, buttonRect.height() - 6);
    const QSize glyphSize(std::min(preferredSize.width(), availableWidth),
                          std::min(preferredSize.height(), availableHeight));

    // Center pixel extents explicitly; half-pixel ties resolve right/down so
    // responsive button aspect ratios never introduce a left/up bias.
    const int left = buttonRect.left() + (buttonRect.width() - glyphSize.width() + 1) / 2;
    const int top = buttonRect.top() + (buttonRect.height() - glyphSize.height() + 1) / 2;
    return QRect(QPoint(left, top), glyphSize);
}

NeniaLoopIconGeometry NeniaRenderer::loopIconGeometry(
    const QRect &glyphBox,
    NeniaLoopIndicatorState loopState,
    int markerPixelSize)
{
    NeniaLoopIconGeometry geometry;
    if (!glyphBox.isValid() || markerPixelSize <= 0) {
        return geometry;
    }

    const int edgeInsetX = std::min(1, (glyphBox.width() - 1) / 2);
    const int leftTipX = glyphBox.left() + edgeInsetX;
    const int rightTipX = glyphBox.right() - edgeInsetX;
    const int headDepth = transportChevronDepth(glyphBox.size());
    const int headHalfSpan = transportChevronHalfSpan(glyphBox.size());

    const QFont markerFont = makeFont(TextRole::Label, markerPixelSize);
    const QFontMetricsF markerMetrics(markerFont);
    const QRectF relativeGlyphBounds =
        markerMetrics.tightBoundingRect(QStringLiteral("1"));
    const QPointF canvasCenter(
        (glyphBox.left() + glyphBox.right()) / 2.0,
        (glyphBox.top() + glyphBox.bottom()) / 2.0);
    geometry.repeatOneBaseline = canvasCenter - relativeGlyphBounds.center();
    const QRectF markerGlyphBounds =
        relativeGlyphBounds.translated(geometry.repeatOneBaseline);
    geometry.markerClearance = loopMarkerClearance(glyphBox.size());

    const int safeTopY = glyphBox.top() + headHalfSpan + 1;
    const int safeBottomY = glyphBox.bottom() - headHalfSpan - 1;
    const int markerTopY = static_cast<int>(
        std::floor(markerGlyphBounds.top() - geometry.markerClearance));
    const int markerBottomY = static_cast<int>(
        std::ceil(markerGlyphBounds.bottom() + geometry.markerClearance));
    geometry.topArrowY = std::min(safeTopY, markerTopY);
    geometry.bottomArrowY = std::max(safeBottomY, markerBottomY);
    const qreal topGap = markerGlyphBounds.top() - geometry.topArrowY;
    const qreal bottomGap = geometry.bottomArrowY - markerGlyphBounds.bottom();
    geometry.repeatOneGap = (topGap + bottomGap) / 2.0;

    geometry.topRight.head = arrowHeadGeometry(
        QPoint(rightTipX, geometry.topArrowY), 1.0, 0.0,
        headDepth, headHalfSpan);
    geometry.topRight.shaft = QLine(QPoint(leftTipX, geometry.topArrowY),
                                    geometry.topRight.head.tip);
    geometry.bottomLeft.head = arrowHeadGeometry(
        QPoint(leftTipX, geometry.bottomArrowY), -1.0, 0.0,
        headDepth, headHalfSpan);
    geometry.bottomLeft.shaft = QLine(QPoint(rightTipX, geometry.bottomArrowY),
                                      geometry.bottomLeft.head.tip);

    if (loopState == NeniaLoopIndicatorState::RepeatOne) {
        geometry.repeatOneMarkerBounds = markerGlyphBounds;
    }
    return geometry;
}

NeniaShuffleIconGeometry NeniaRenderer::shuffleIconGeometry(const QRect &glyphBox)
{
    NeniaShuffleIconGeometry geometry;
    if (!glyphBox.isValid()) {
        return geometry;
    }

    const int leftStartX = glyphBox.left();
    const int rightTipX = glyphBox.right();
    const int headDepth = transportChevronDepth(glyphBox.size());
    const int headHalfSpan = transportChevronHalfSpan(glyphBox.size());
    const qreal canvasCenterY = (glyphBox.top() + glyphBox.bottom()) / 2.0;
    const int diagonalSpan = std::min(rightTipX - leftStartX,
                                      std::max(0, glyphBox.height() - 2));
    geometry.verticalOffset = diagonalSpan / 2.0;
    const int upperY = static_cast<int>(
        std::floor(canvasCenterY - geometry.verticalOffset));
    const int lowerY = static_cast<int>(
        std::ceil(canvasCenterY + geometry.verticalOffset));
    const qreal directionX = rightTipX - leftStartX;
    const qreal directionY = lowerY - upperY;
    const QPoint lowerRightTip(rightTipX + 1, lowerY + 1);
    geometry.paths.at(0) = {
        QLine(QPoint(leftStartX, upperY), lowerRightTip),
        arrowHeadGeometry(lowerRightTip, directionX, directionY,
                          headDepth, headHalfSpan)};
    geometry.paths.at(1) = {
        QLine(QPoint(leftStartX, lowerY), QPoint(rightTipX, upperY)),
        orthogonalArrowHeadGeometry(QPoint(rightTipX, upperY), headDepth)};
    return geometry;
}

QRect NeniaRenderer::channelMatrixPanelRect()
{
    return layoutForSize(preferredSize()).channelMatrixPanel;
}

QRect NeniaRenderer::rightColumnRect()
{
    return layoutForSize(preferredSize()).rightColumn;
}

QRect NeniaRenderer::artworkPanelRect()
{
    return layoutForSize(preferredSize()).artworkPanel;
}

QRect NeniaRenderer::artworkViewportRect()
{
    return layoutForSize(preferredSize()).artworkViewport;
}

QRect NeniaRenderer::mediaInfoColumnRect()
{
    return layoutForSize(preferredSize()).mediaInfoColumn;
}

QRect NeniaRenderer::spectrumPanelRect()
{
    return layoutForSize(preferredSize()).spectrumPanel;
}

QRect NeniaRenderer::mediaInfoValueRect(const NeniaLayout &layout,
                                        int row,
                                        bool extendedPage)
{
    return mediaInfoValueRectForRow(layout, row, extendedPage);
}

QString NeniaRenderer::mediaInfoRowLabel(const DisplayPresentation &presentation, int row)
{
    if (row < 0 || row >= 7) {
        return {};
    }
    return mediaInfoRowLabels(presentation).at(static_cast<std::size_t>(row));
}

bool NeniaRenderer::mediaInfoValueNeedsMarquee(const QString &value,
                                               const QRect &valueRect,
                                               int pixelSize)
{
    return mediaInfoValueNeedsMarqueeInternal(value, valueRect, pixelSize);
}

int NeniaRenderer::mediaInfoMarqueeOffset(const QString &value,
                                          const QRect &valueRect,
                                          int pixelSize,
                                          qint64 elapsedMs)
{
    return mediaInfoMarqueeOffsetInternal(value, valueRect, pixelSize, elapsedMs);
}

int NeniaRenderer::mediaInfoMarqueeCycleDistance(const QString &value,
                                                 const QRect &valueRect,
                                                 int pixelSize)
{
    return mediaInfoMarqueeCycleDistanceInternal(value, valueRect, pixelSize);
}

int NeniaRenderer::mediaInfoMarqueeTextX(const QString &value,
                                         const QRect &valueRect,
                                         int pixelSize,
                                         qint64 elapsedMs)
{
    return mediaInfoMarqueeTextXInternal(value, valueRect, pixelSize, elapsedMs);
}

bool NeniaRenderer::mediaInfoNeedsMarquee(const NeniaLayout &layout,
                                          const NeniaMediaInfo &mediaInfo,
                                          const DisplayPresentation &presentation)
{
    const int pixelSize = layout.metrics.bodyTextPixelSize;
    const std::array<QString, 7> values = mediaInfoRowValues(mediaInfo, presentation);
    for (int row = 0; row < 7; ++row) {
        if (!presentation.playbackUi.extendedMediaInfo && row == 4) {
            continue;
        }
        if (mediaInfoValueNeedsMarqueeInternal(
                values.at(static_cast<std::size_t>(row)),
                mediaInfoValueRectForRow(layout, row,
                                         presentation.playbackUi.extendedMediaInfo),
                pixelSize)) {
            return true;
        }
    }
    return false;
}

QRect NeniaRenderer::presentationRect(const QSize &availableSize)
{
    if (availableSize.width() <= 0 || availableSize.height() <= 0) {
        return {};
    }

    return QRect(QPoint(0, 0), availableSize);
}

void NeniaRenderer::render(QImage &surface, const DisplayState &state)
{
    static constexpr SpectrumBands neutralSpectrum{};
    render(surface, state, neutralSpectrum);
}

void NeniaRenderer::render(QImage &surface,
                           const DisplayState &state,
                           const SpectrumBands &spectrumBands)
{
    static const NeniaMediaInfo emptyMediaInfo;
    static const QImage noArtwork;
    render(surface, state, spectrumBands, emptyMediaInfo, noArtwork);
}

void NeniaRenderer::render(QImage &surface,
                           const DisplayState &state,
                           const SpectrumBands &spectrumBands,
                           const NeniaMediaInfo &mediaInfo,
                           const QImage &artwork)
{
    static const DisplayPresentation defaultPresentation;
    render(surface, state, spectrumBands, mediaInfo, artwork, defaultPresentation);
}

void NeniaRenderer::render(QImage &surface,
                           const DisplayState &state,
                           const SpectrumBands &spectrumBands,
                           const NeniaMediaInfo &mediaInfo,
                           const QImage &artwork,
                           const DisplayPresentation &presentation,
                           KeyboardRangeMode keyboardRange)
{
    ScopedPerformanceTimer rendererTimer(NeniaPerformanceDiagnostics::Timing::Renderer);
    if (surface.isNull()) {
        surface = QImage(preferredSize(), QImage::Format_ARGB32_Premultiplied);
    } else if (surface.format() != QImage::Format_ARGB32_Premultiplied) {
        surface = surface.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    const NeniaLayout layout = layoutForSize(surface.size());
    const Palette colors = makePalette();
    surface.fill(colors.background);

    QPainter painter(&surface);
    configurePainter(painter);
    drawPlaylist(painter, colors, layout, presentation.playlist);
    drawTrackRows(painter, colors, layout, state, keyboardRange,
                  presentation.octaveNamingConvention);
    drawRightColumnBackground(painter, colors, layout);
    drawArtworkPanel(painter, colors, layout, artwork);
    drawTransportControls(painter, colors, layout, presentation);
    drawMediaInfoColumn(painter, colors, layout, mediaInfo, presentation);
    drawSpectrum(painter, colors, layout, spectrumBands);
    drawMajorOuterFrames(painter, colors, layout);
}

void NeniaRenderer::renderStatic(QImage &surface)
{
    const DisplayState neutralState;
    static constexpr SpectrumBands neutralSpectrum{};
    render(surface, neutralState, neutralSpectrum);
}

} // namespace nenia::detail
