#include "NeniaArtworkLoader.h"

#include <QFileInfo>
#include <QDir>
#include <QImageReader>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace nenia::detail {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QSize boundedDecodeSize(const QSize &sourceSize)
{
    return sourceSize.scaled(QSize(kArtworkDecodeMaxDimension,
                                   kArtworkDecodeMaxDimension),
                             Qt::KeepAspectRatio);
}

constexpr qint64 kExternalArtworkMaxFileBytes = 64LL * 1024LL * 1024LL;
constexpr std::uint64_t kExternalArtworkMaxSourcePixels = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kExternalArtworkMaxDecodedPixels = 8ULL * 1024ULL * 1024ULL;
constexpr int kExternalArtworkMaxSourceDimension = 8'192;
constexpr int kExternalArtworkMaxDecodeDimension = 4'096;

std::uint64_t pixelCount(const QSize &size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(size.width())
        * static_cast<std::uint64_t>(size.height());
}

QSize clampDecodeTarget(QSize requested)
{
    if (!requested.isValid() || requested.width() <= 0 || requested.height() <= 0) {
        requested = QSize(kExternalArtworkMaxDecodeDimension,
                          kExternalArtworkMaxDecodeDimension);
    }
    if (requested.width() > kExternalArtworkMaxDecodeDimension
        || requested.height() > kExternalArtworkMaxDecodeDimension) {
        requested.scale(kExternalArtworkMaxDecodeDimension,
                        kExternalArtworkMaxDecodeDimension,
                        Qt::KeepAspectRatio);
    }
    if (pixelCount(requested) > kExternalArtworkMaxDecodedPixels) {
        const long double scale = std::sqrt(
            static_cast<long double>(kExternalArtworkMaxDecodedPixels)
            / static_cast<long double>(pixelCount(requested)));
        requested = QSize(std::max(1, static_cast<int>(requested.width() * scale)),
                          std::max(1, static_cast<int>(requested.height() * scale)));
    }
    return requested;
}

QImage decodeExternalCandidate(const QString &path, QSize decodeTarget)
{
    const QFileInfo fileInfo(path);
    if (fileInfo.isSymLink() || !fileInfo.exists() || !fileInfo.isFile()
        || !fileInfo.isReadable() || fileInfo.size() <= 0
        || fileInfo.size() > kExternalArtworkMaxFileBytes) {
        return {};
    }

    QImageReader reader(fileInfo.absoluteFilePath());
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        return {};
    }
    const QByteArray format = reader.format().toLower();
    if (format != QByteArrayLiteral("jpeg") && format != QByteArrayLiteral("png")) {
        return {};
    }

    const QSize sourceSize = reader.size();
    const std::uint64_t sourcePixels = pixelCount(sourceSize);
    if (sourcePixels == 0 || sourcePixels > kExternalArtworkMaxSourcePixels
        || sourceSize.width() > kExternalArtworkMaxSourceDimension
        || sourceSize.height() > kExternalArtworkMaxSourceDimension) {
        return {};
    }

    decodeTarget = clampDecodeTarget(decodeTarget);
    QSize requestedSize = sourceSize;
    if (sourceSize.width() > decodeTarget.width()
        || sourceSize.height() > decodeTarget.height()
        || sourcePixels > kExternalArtworkMaxDecodedPixels) {
        requestedSize = sourceSize.scaled(decodeTarget, Qt::KeepAspectRatio);
        reader.setScaledSize(requestedSize);
    }

    QImage image = reader.read();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return {};
    }

    // Some image handlers may ignore setScaledSize(). The header-based source
    // pixel cap bounds their worst-case decoded image; this final clamp bounds
    // what is retained by the playback/widget presentation.
    if (image.width() > decodeTarget.width() || image.height() > decodeTarget.height()
        || pixelCount(image.size()) > kExternalArtworkMaxDecodedPixels) {
        image = image.scaled(image.size().scaled(decodeTarget, Qt::KeepAspectRatio),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.isNull() || pixelCount(image.size()) > kExternalArtworkMaxDecodedPixels) {
        return {};
    }
    return image;
}

} // namespace

QImage loadArtworkFile(const QString &path, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        setError(errorMessage, QStringLiteral("artwork file does not exist"));
        return {};
    }
    if (!fileInfo.isFile()) {
        setError(errorMessage, QStringLiteral("artwork path is not a regular file"));
        return {};
    }

    QImageReader reader(fileInfo.absoluteFilePath());
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() <= 0 || sourceSize.height() <= 0) {
        const QString detail = reader.errorString().trimmed();
        setError(errorMessage,
                 detail.isEmpty() ? QStringLiteral("artwork format or dimensions are unsupported")
                                  : detail);
        return {};
    }

    if (sourceSize.width() > kArtworkDecodeMaxDimension
        || sourceSize.height() > kArtworkDecodeMaxDimension) {
        reader.setScaledSize(boundedDecodeSize(sourceSize));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        const QString detail = reader.errorString().trimmed();
        setError(errorMessage,
                 detail.isEmpty() ? QStringLiteral("artwork image could not be decoded")
                                  : detail);
        return {};
    }

    if (image.width() > kArtworkDecodeMaxDimension
        || image.height() > kArtworkDecodeMaxDimension) {
        const QSize boundedSize = boundedDecodeSize(image.size());
        image = image.scaled(boundedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        setError(errorMessage, QStringLiteral("artwork image has invalid dimensions"));
        return {};
    }

    return image;
}

LocalArtworkResult resolveLocalArtwork(const QUrl &mediaSource, QSize decodeTarget)
{
    if (!mediaSource.isLocalFile()) {
        return {};
    }
    return resolveLocalArtwork(mediaSource.toLocalFile(), decodeTarget);
}

LocalArtworkResult resolveLocalArtwork(const QString &absoluteMediaPath,
                                       QSize decodeTarget)
{
    const QFileInfo mediaInfo(absoluteMediaPath);
    if (absoluteMediaPath.isEmpty() || !mediaInfo.isAbsolute()) {
        return {};
    }

    // Keep the supplied path's directory (including a symlinked media file's
    // directory); do not canonicalize it to the symlink target's directory.
    const QDir mediaDirectory(QDir::cleanPath(mediaInfo.absolutePath()));
    static constexpr std::array<QStringView, 6> kCandidates{
        u"Cover.jpg", u"cover.jpg", u"Cover.png", u"cover.png",
        u"folder.jpg", u"Folder.jpg"};
    for (const QStringView candidate : kCandidates) {
        const QString candidateName = candidate.toString();
        const QString candidatePath = mediaDirectory.filePath(candidateName);
        QImage image = decodeExternalCandidate(candidatePath, decodeTarget);
        if (!image.isNull()) {
            return {std::move(image), candidateName};
        }
    }
    return {};
}

QImage selectAutomaticArtwork(const QImage &embeddedArtwork,
                              const QImage &externalArtwork)
{
    if (!embeddedArtwork.isNull() && embeddedArtwork.width() > 0
        && embeddedArtwork.height() > 0) {
        return embeddedArtwork;
    }
    return externalArtwork;
}

QSize externalArtworkDecodeTarget(QSize widgetSize, qreal devicePixelRatio)
{
    if (!widgetSize.isValid() || widgetSize.width() <= 0 || widgetSize.height() <= 0) {
        return QSize(kExternalArtworkMaxDecodeDimension,
                     kExternalArtworkMaxDecodeDimension);
    }
    if (!std::isfinite(devicePixelRatio) || devicePixelRatio <= 0.0) {
        devicePixelRatio = 1.0;
    }
    const auto dimension = [devicePixelRatio](int logicalSize) {
        const double requested = static_cast<double>(logicalSize)
            * static_cast<double>(devicePixelRatio) * 2.0;
        return std::clamp(static_cast<int>(std::ceil(requested)),
                          1, kExternalArtworkMaxDecodeDimension);
    };
    return clampDecodeTarget(QSize(dimension(widgetSize.width()),
                                   dimension(widgetSize.height())));
}

} // namespace nenia::detail
