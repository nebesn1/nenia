#pragma once

#include <nenia/NeniaPlaylistController.h>

#include <QStringList>

#include <vector>

namespace nenia::app {

struct NeniaFileImportResult
{
    std::vector<NeniaPlaylistItem> items;
    QStringList warnings;
};

bool collectNeniaOpenFileItems(const QStringList &paths,
                               bool directMidiEnabled,
                               NeniaFileImportResult *result,
                               QString *errorMessage = nullptr);

} // namespace nenia::app
