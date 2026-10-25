#include "NeniaTagMetadataReader.h"

#include <QByteArray>
#include <QFile>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace nenia::detail {
namespace {

QString meaningful(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString() : value;
}

QString fromTagLibString(const TagLib::String &value)
{
    const std::string utf8 = value.to8Bit(true);
    return meaningful(QString::fromUtf8(utf8.data(), qsizetype(utf8.size())));
}

QMap<QString, QStringList> canonicalProperties(const NeniaTagProperties &properties)
{
    QMap<QString, QStringList> canonical;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        const QString key = it.key().trimmed().toUpper();
        if (key.isEmpty()) {
            continue;
        }
        QStringList &values = canonical[key];
        for (const QString &value : it.value()) {
            const QString cleaned = meaningful(value);
            if (!cleaned.isEmpty()) {
                values.append(cleaned);
            }
        }
    }
    return canonical;
}

QString joinedValues(const QStringList &values)
{
    QStringList unique;
    for (const QString &value : values) {
        const QString cleaned = meaningful(value);
        if (cleaned.isEmpty()) {
            continue;
        }
        const bool alreadyPresent = std::any_of(
            unique.cbegin(), unique.cend(), [&cleaned](const QString &existing) {
                return existing.compare(cleaned, Qt::CaseInsensitive) == 0;
            });
        if (!alreadyPresent) {
            unique.append(cleaned);
        }
    }
    return unique.join(QStringLiteral(" / "));
}

QString normalizedNumber(QString value)
{
    value = meaningful(std::move(value));
    if (value.isEmpty()) {
        return {};
    }
    bool numeric = false;
    const qulonglong integer = value.toULongLong(&numeric, 10);
    return numeric ? QString::number(integer) : value;
}

struct NumberAndTotal
{
    QString number;
    QString total;
};

NumberAndTotal numberAndTotal(const QStringList &numberValues,
                              const QStringList &totalValues)
{
    if (numberValues.isEmpty()) {
        return {};
    }

    QString number = meaningful(numberValues.constFirst());
    QString compoundTotal;
    const qsizetype slash = number.indexOf(QLatin1Char('/'));
    if (slash >= 0 && number.indexOf(QLatin1Char('/'), slash + 1) >= 0) {
        // Keep malformed multi-slash input intact; appending a separate total
        // would manufacture another component and could duplicate information.
        return {number, {}};
    }
    if (slash >= 0) {
        compoundTotal = meaningful(number.mid(slash + 1));
        number = meaningful(number.left(slash));
    }

    QString total = totalValues.isEmpty() ? QString()
                                          : meaningful(totalValues.constFirst());
    if (total.isEmpty()) {
        total = std::move(compoundTotal);
    }
    return {normalizedNumber(std::move(number)), normalizedNumber(std::move(total))};
}

QString performerValues(const QMap<QString, QStringList> &properties)
{
    const QString generic = joinedValues(properties.value(QStringLiteral("PERFORMER")));
    if (!generic.isEmpty()) {
        return generic;
    }

    QStringList rolePerformers;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("PERFORMER:"))) {
            rolePerformers.append(it.value());
        }
    }
    return joinedValues(rolePerformers);
}

} // namespace

NeniaRichMediaMetadata normalizeTagProperties(const NeniaTagProperties &properties)
{
    const QMap<QString, QStringList> values = canonicalProperties(properties);
    const NumberAndTotal track = numberAndTotal(values.value(QStringLiteral("TRACKNUMBER")),
                                                values.value(QStringLiteral("TRACKTOTAL")));
    const NumberAndTotal disc = numberAndTotal(values.value(QStringLiteral("DISCNUMBER")),
                                               values.value(QStringLiteral("DISCTOTAL")));

    NeniaRichMediaMetadata metadata;
    metadata.title = joinedValues(values.value(QStringLiteral("TITLE")));
    metadata.artist = joinedValues(values.value(QStringLiteral("ARTIST")));
    metadata.album = joinedValues(values.value(QStringLiteral("ALBUM")));
    metadata.albumArtist = joinedValues(values.value(QStringLiteral("ALBUMARTIST")));
    metadata.performer = performerValues(values);
    metadata.composer = joinedValues(values.value(QStringLiteral("COMPOSER")));
    metadata.genre = joinedValues(values.value(QStringLiteral("GENRE")));
    metadata.date = joinedValues(values.value(QStringLiteral("DATE")));
    metadata.trackNumber = track.number;
    metadata.trackTotal = track.total;
    metadata.discNumber = disc.number;
    metadata.discTotal = disc.total;
    return metadata;
}

NeniaTagReadResult readTagMetadata(const QUrl &source)
{
    NeniaTagReadResult result;
    if (!source.isLocalFile()) {
        return result;
    }

    result.attempted = true;
    const QString path = source.toLocalFile();
    if (path.isEmpty()) {
        result.diagnostic = QStringLiteral("the local source path is empty");
        return result;
    }

    const QByteArray encodedPath = QFile::encodeName(path);
    try {
        // Audio properties are intentionally disabled: Qt remains authoritative
        // for playback format, duration, and all decoded-audio information.
        const TagLib::FileRef file(encodedPath.constData(), false);
        if (file.isNull()) {
            result.diagnostic = QStringLiteral(
                "TagLib could not open the file (unsupported or malformed format)");
            return result;
        }

        NeniaTagProperties properties;
        const TagLib::PropertyMap propertyMap = file.properties();
        for (auto it = propertyMap.cbegin(); it != propertyMap.cend(); ++it) {
            const QString key = fromTagLibString(it->first);
            if (key.isEmpty()) {
                continue;
            }
            QStringList values;
            for (const TagLib::String &value : it->second) {
                const QString converted = fromTagLibString(value);
                if (!converted.isEmpty()) {
                    values.append(converted);
                }
            }
            properties[key].append(values);
        }

        result.metadata = normalizeTagProperties(properties);
        result.success = true;
    } catch (const std::exception &exception) {
        result.diagnostic = QStringLiteral("TagLib read failed: %1")
                                .arg(QString::fromUtf8(exception.what()));
    } catch (...) {
        result.diagnostic = QStringLiteral("TagLib read failed with an unknown error");
    }
    return result;
}

} // namespace nenia::detail
