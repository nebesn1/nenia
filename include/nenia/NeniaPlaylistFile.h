#pragma once

#include <nenia/NeniaPlaylistController.h>

#include <QStringList>

#include <vector>

namespace nenia {

struct NeniaPlaylistFileLoadResult
{
    std::vector<NeniaPlaylistItem> items;
    QStringList warnings;
};

bool loadNeniaM3uPlaylist(const QString &playlistPath,
                          NeniaPlaylistFileLoadResult *result,
                          QString *errorMessage = nullptr);

bool saveNeniaM3u8Playlist(const QString &playlistPath,
                           const std::vector<NeniaPlaylistItem> &items,
                           QString *errorMessage = nullptr);

} // namespace nenia
