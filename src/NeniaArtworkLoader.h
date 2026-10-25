#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QUrl>

namespace nenia::detail {

// High safety bound for pathological source files. Ordinary artwork below
// this bound is decoded at its native resolution and is only resampled once
// for the current square viewport.
inline constexpr int kArtworkDecodeMaxDimension = 8192;

struct LocalArtworkResult {
    QImage image;
    QString candidateName;
};

QImage loadArtworkFile(const QString &path, QString *errorMessage = nullptr);

// Resolves only the six supported same-directory album-cover filenames. The
// QUrl overload rejects non-local URLs; the path overload requires an absolute
// path so process-working-directory state can never affect resolution.
LocalArtworkResult resolveLocalArtwork(const QUrl &mediaSource,
                                       QSize decodeTarget = {});
LocalArtworkResult resolveLocalArtwork(const QString &absoluteMediaPath,
                                       QSize decodeTarget = {});
QImage selectAutomaticArtwork(const QImage &embeddedArtwork,
                              const QImage &externalArtwork);

// Returns a bounded pixel target with 2x scale headroom for device-pixel-ratio
// rendering. The widget size is only a hint; resolver safety caps still apply.
QSize externalArtworkDecodeTarget(QSize widgetSize, qreal devicePixelRatio);

} // namespace nenia::detail
