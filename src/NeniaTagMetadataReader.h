#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace nenia::detail {

// TagLib is kept behind this value-only boundary. Number and total remain
// separate until the presentation layer formats TRACK and DISC.
struct NeniaRichMediaMetadata
{
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString performer;
    QString composer;
    QString genre;
    QString date;
    QString trackNumber;
    QString trackTotal;
    QString discNumber;
    QString discTotal;

    bool operator==(const NeniaRichMediaMetadata &) const = default;
};

using NeniaTagProperties = QMap<QString, QStringList>;

struct NeniaTagReadResult
{
    bool attempted = false;
    bool success = false;
    NeniaRichMediaMetadata metadata;
    QString diagnostic;
};

NeniaRichMediaMetadata normalizeTagProperties(const NeniaTagProperties &properties);
NeniaTagReadResult readTagMetadata(const QUrl &source);

} // namespace nenia::detail
