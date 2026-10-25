#include <nenia/NeniaPlaylistFile.h>

#include "NeniaDirectMidiSource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSaveFile>

namespace nenia {
namespace {

bool excludedPlaylistSuffix(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("json") || suffix == QStringLiteral("mid")
        || suffix == QStringLiteral("midi") || suffix == QStringLiteral("m3u")
        || suffix == QStringLiteral("m3u8") || suffix == QStringLiteral("pls")
        || suffix == QStringLiteral("xspf");
}

bool supportedAudio(const QFileInfo &fileInfo, const QMimeDatabase &mimeDatabase)
{
    if (fileInfo.fileName().startsWith(QLatin1Char('.')) || fileInfo.isSymLink()
        || !fileInfo.isFile() || !fileInfo.isReadable()) {
        return false;
    }
    if (detail::hasDirectMidiFileSuffix(fileInfo.fileName())) {
#ifdef NENIA_ENABLE_DIRECT_MIDI
        return true;
#else
        return false;
#endif
    }
    if (excludedPlaylistSuffix(fileInfo)) {
        return false;
    }
    const QMimeType mimeType =
        mimeDatabase.mimeTypeForFile(fileInfo, QMimeDatabase::MatchExtension);
    return mimeType.isValid() && mimeType.name().startsWith(QStringLiteral("audio/"));
}

QString displayNameFor(const QString &audioPath, const QDir &playlistDirectory)
{
    const QString relative = QDir::fromNativeSeparators(
        playlistDirectory.relativeFilePath(audioPath));
    return relative.isEmpty() || relative == QStringLiteral(".")
        ? QFileInfo(audioPath).fileName()
        : relative;
}

bool isPlaylistSuffix(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("m3u") || suffix == QStringLiteral("m3u8");
}

} // namespace

bool loadNeniaM3uPlaylist(const QString &playlistPath,
                          NeniaPlaylistFileLoadResult *result,
                          QString *errorMessage)
{
    if (result == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist load result is null.");
        }
        return false;
    }
    result->items.clear();
    result->warnings.clear();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo playlistInfo(playlistPath);
    const QString absolutePath = playlistInfo.absoluteFilePath();
    if (!playlistInfo.exists() || !playlistInfo.isFile() || !playlistInfo.isReadable()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist file is missing or unreadable: %1")
                                 .arg(absolutePath);
        }
        return false;
    }
    if (!isPlaylistSuffix(playlistInfo)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unsupported playlist file extension: %1")
                                 .arg(playlistInfo.suffix());
        }
        return false;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to read playlist file: %1")
                                 .arg(file.errorString());
        }
        return false;
    }

    const QDir playlistDirectory = playlistInfo.absoluteDir();
    const QMimeDatabase mimeDatabase;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const QFileInfo entryInfo(line.startsWith(QLatin1Char('/'))
                                      || QFileInfo(line).isAbsolute()
                                  ? line
                                  : playlistDirectory.absoluteFilePath(line));
        if (!supportedAudio(entryInfo, mimeDatabase)) {
            result->warnings.push_back(
                QStringLiteral("Skipped invalid playlist entry: %1").arg(line));
            continue;
        }

        const QString entryPath = entryInfo.absoluteFilePath();
        result->items.push_back({QUrl::fromLocalFile(entryPath),
                                 displayNameFor(entryPath, playlistDirectory)});
    }

    if (result->items.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist file contains no supported audio entries.");
        }
        return false;
    }
    return true;
}

bool saveNeniaM3u8Playlist(const QString &playlistPath,
                           const std::vector<NeniaPlaylistItem> &items,
                           QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (playlistPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist save path is empty.");
        }
        return false;
    }

    const QFileInfo targetInfo(playlistPath);
    const QDir targetDirectory = targetInfo.absoluteDir();
    if (!targetDirectory.exists() && !QDir().mkpath(targetDirectory.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to create playlist directory: %1")
                                 .arg(targetDirectory.absolutePath());
        }
        return false;
    }

    QSaveFile file(targetInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to save playlist file: %1")
                                 .arg(file.errorString());
        }
        return false;
    }
    if (file.write("#EXTM3U\n") != 8) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write playlist header: %1")
                                 .arg(file.errorString());
        }
        return false;
    }

    for (const NeniaPlaylistItem &item : items) {
        QString line;
        if (item.source.isLocalFile()) {
            const QString absoluteSource = QFileInfo(item.source.toLocalFile()).absoluteFilePath();
            line = QDir::fromNativeSeparators(targetDirectory.relativeFilePath(absoluteSource));
            if (line.isEmpty() || line == QStringLiteral(".")) {
                line = QFileInfo(absoluteSource).fileName();
            }
        } else {
            line = item.source.toString();
        }
        if (line.isEmpty()) {
            continue;
        }
        const QByteArray encoded = line.toUtf8() + '\n';
        if (file.write(encoded) != encoded.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Unable to write playlist entry: %1")
                                     .arg(file.errorString());
            }
            return false;
        }
    }

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to commit playlist file: %1")
                                 .arg(file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace nenia
