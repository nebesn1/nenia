#include "NeniaMediaMetadata.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaType>
#include <QStringList>
#include <QVariant>

#include <utility>

namespace nenia::detail {
namespace {

QString meaningful(QString value)
{
    return value.trimmed().isEmpty() ? QString() : std::move(value);
}

QString firstMetadataString(const QMediaMetaData &metadata,
                            std::initializer_list<QMediaMetaData::Key> keys)
{
    for (const QMediaMetaData::Key key : keys) {
        const QString value = meaningful(metadata.stringValue(key));
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString metadataText(const QMediaMetaData &metadata, QMediaMetaData::Key key)
{
    const QVariant value = metadata.value(key);
    if (!value.isValid()) {
        return {};
    }
    if (value.metaType() == QMetaType::fromType<QStringList>()) {
        return meaningful(value.toStringList().join(QStringLiteral(" / ")));
    }
    if (value.metaType() == QMetaType::fromType<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        return meaningful(dateTime.isValid() ? dateTime.toString(Qt::ISODate)
                                             : value.toString());
    }
    if (value.metaType() == QMetaType::fromType<QDate>()) {
        const QDate date = value.toDate();
        return meaningful(date.isValid() ? date.toString(Qt::ISODate) : value.toString());
    }
    return meaningful(value.toString());
}

QString sourceFilenameTitle(const QUrl &source)
{
    const QString path = source.isLocalFile() ? source.toLocalFile() : source.path();
    if (path.isEmpty()) {
        return {};
    }
    return meaningful(QFileInfo(path).completeBaseName());
}

QString sourceLocation(const QUrl &source)
{
    if (source.isLocalFile()) {
        const QString path = source.toLocalFile();
        if (path.isEmpty()) {
            return {};
        }
        const QFileInfo fileInfo(path);
        const QString canonicalPath = fileInfo.canonicalFilePath();
        return canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath;
    }
    return source.isEmpty() ? QString() : source.toDisplayString(QUrl::PrettyDecoded);
}

} // namespace

NeniaAutomaticMediaPresentation automaticMediaPresentation(
    const QUrl &source,
    const QMediaMetaData &metadata,
    const NeniaRichMediaMetadata &tagMetadata)
{
    NeniaAutomaticMediaPresentation presentation;
    presentation.primary.title = firstMetadataString(metadata, {QMediaMetaData::Title});
    // Keep the established key precedence for ARTIST. The extended
    // ALBUM ARTIST and PERFORMER values are read independently below.
    presentation.primary.artist = firstMetadataString(
        metadata, {QMediaMetaData::ContributingArtist,
                   QMediaMetaData::LeadPerformer,
                   QMediaMetaData::AlbumArtist,
                   QMediaMetaData::Author});
    presentation.primary.album = firstMetadataString(metadata, {QMediaMetaData::AlbumTitle});
    if (presentation.primary.title.isEmpty()) {
        presentation.primary.title = tagMetadata.title;
    }
    if (presentation.primary.artist.isEmpty()) {
        presentation.primary.artist = tagMetadata.artist;
    }
    if (presentation.primary.album.isEmpty()) {
        presentation.primary.album = tagMetadata.album;
    }
    if (presentation.primary.title.isEmpty()) {
        presentation.primary.title = sourceFilenameTitle(source);
    }

    presentation.extended.trackNumber = tagMetadata.trackNumber.isEmpty()
        ? metadataText(metadata, QMediaMetaData::TrackNumber)
        : (tagMetadata.trackTotal.isEmpty()
               ? tagMetadata.trackNumber
               : tagMetadata.trackNumber + QLatin1Char('/') + tagMetadata.trackTotal);
    presentation.extended.date = tagMetadata.date.isEmpty()
        ? metadataText(metadata, QMediaMetaData::Date)
        : tagMetadata.date;
    presentation.extended.albumArtist = firstMetadataString(
        metadata, {QMediaMetaData::AlbumArtist});
    if (presentation.extended.albumArtist.isEmpty()) {
        presentation.extended.albumArtist = tagMetadata.albumArtist;
    }
    presentation.extended.performer = firstMetadataString(
        metadata, {QMediaMetaData::LeadPerformer, QMediaMetaData::ContributingArtist});
    if (presentation.extended.performer.isEmpty()) {
        presentation.extended.performer = tagMetadata.performer;
    }
    presentation.extended.composer = tagMetadata.composer.isEmpty()
        ? metadataText(metadata, QMediaMetaData::Composer)
        : tagMetadata.composer;
    presentation.extended.genre = metadataText(metadata, QMediaMetaData::Genre);
    if (presentation.extended.genre.isEmpty()) {
        presentation.extended.genre = tagMetadata.genre;
    }
    if (!tagMetadata.discNumber.isEmpty()) {
        presentation.extended.discNumber = tagMetadata.discTotal.isEmpty()
            ? tagMetadata.discNumber
            : tagMetadata.discNumber + QLatin1Char('/') + tagMetadata.discTotal;
    }
    presentation.location = sourceLocation(source);
    return presentation;
}

} // namespace nenia::detail
