#include "NeniaFileImport.h"

#include <nenia/NeniaPlaylistFile.h>

#include "../src/NeniaDirectMidiSource.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QUrl>

namespace nenia::app {
namespace {

bool isPlaylistFile(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("m3u") || suffix == QStringLiteral("m3u8");
}

} // namespace

bool collectNeniaOpenFileItems(const QStringList &paths,
                               bool directMidiEnabled,
                               NeniaFileImportResult *result,
                               QString *errorMessage)
{
    if (result == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Open File result storage is null.");
        }
        return false;
    }
    result->items.clear();
    result->warnings.clear();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QMimeDatabase mimeDatabase;
    for (const QString &path : paths) {
        const QFileInfo fileInfo(path);
        const QString absolutePath = fileInfo.absoluteFilePath();
        if (!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
            result->warnings.push_back(
                QStringLiteral("Skipped missing, unreadable, or non-file input: %1")
                    .arg(path));
            continue;
        }

        if (isPlaylistFile(fileInfo)) {
            NeniaPlaylistFileLoadResult playlist;
            QString playlistError;
            if (!loadNeniaM3uPlaylist(absolutePath, &playlist, &playlistError)) {
                result->warnings.push_back(
                    QStringLiteral("Skipped playlist %1: %2").arg(path, playlistError));
            }
            result->warnings.append(playlist.warnings);
            result->items.insert(result->items.end(), playlist.items.begin(),
                                 playlist.items.end());
            continue;
        }

        if (detail::hasDirectMidiFileSuffix(fileInfo.fileName())) {
            if (!directMidiEnabled) {
                result->warnings.push_back(
                    QStringLiteral("Direct MIDI playback is unavailable in this build: %1")
                        .arg(path));
                continue;
            }
            result->items.push_back({QUrl::fromLocalFile(absolutePath),
                                     fileInfo.fileName()});
            continue;
        }

        const QMimeType mimeType = mimeDatabase.mimeTypeForFile(
            fileInfo, QMimeDatabase::MatchExtension);
        if (!mimeType.isValid() || !mimeType.name().startsWith(QStringLiteral("audio/"))) {
            result->warnings.push_back(
                QStringLiteral("Skipped unsupported file: %1").arg(path));
            continue;
        }
        result->items.push_back({QUrl::fromLocalFile(absolutePath), fileInfo.fileName()});
    }

    if (result->items.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No supported media files or playlist entries were found.");
        }
        return false;
    }
    return true;
}

} // namespace nenia::app
