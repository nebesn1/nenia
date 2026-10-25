#pragma once

#include <QString>
#include <QVector>
#include <QMetaType>

namespace nenia {

enum class NeniaInputFocus {
    Playlist,
    Visualizer
};

enum class NeniaLoopIndicatorState {
    StopAfterCurrent,
    Off = StopAfterCurrent,
    RepeatOne,
    RepeatAll
};

enum class NeniaTransportControl {
    None = -1,
    Previous = 0,
    PlayPause,
    Next,
    Loop,
    Shuffle
};

struct NeniaPlaybackUiState
{
    NeniaLoopIndicatorState loop = NeniaLoopIndicatorState::StopAfterCurrent;
    bool shuffle = false;
    bool extendedMediaInfo = false;

    bool operator==(const NeniaPlaybackUiState &) const = default;
};

struct NeniaPlaylistEntry
{
    QString displayName;
    bool selected = false;
    bool current = false;

    bool operator==(const NeniaPlaylistEntry &) const = default;
};

struct NeniaPlaylistState
{
    QString sourceLabel;
    QVector<NeniaPlaylistEntry> entries;
    int firstVisibleRow = 0;
    // Retained for source compatibility; the standalone host no longer routes
    // input by focus and the renderer does not visually switch focus.
    NeniaInputFocus focus = NeniaInputFocus::Visualizer;
    bool hasPlaylist = false;
    bool interactionHighlighted = false;

    bool operator==(const NeniaPlaylistState &) const = default;
};

} // namespace nenia

Q_DECLARE_METATYPE(nenia::NeniaTransportControl)
