# Nenia integration

Nenia is a widget-only visual component or a widget plus playback controller.
The supported public surface is collected by `<nenia/Nenia.h>`:

- `NeniaWidget`
- `NeniaPlaybackController`
- `NeniaPlaylistController`, `NeniaPlaylistLoadBehavior`, and playlist items
- `NeniaPlaylistFile` M3U/M3U8 load/save helpers
- `NeniaInputFocus`, `NeniaPlaylistState`, and playback indicator values
- media, timeline-provider, and timeline-event types

Renderer, animation-engine, synchronizer, spectrum-analyzer, cache, and
typography headers remain implementation details.

The reusable static library has a required TagLib implementation dependency
for local-file rich text metadata. Its installed `NeniaConfig.cmake` resolves
TagLib automatically, so a consumer that already uses `find_package(Nenia
CONFIG REQUIRED)` does not need to add a separate TagLib lookup. TagLib types
are not exposed through Nenia's public headers. See
[`rich-metadata.md`](rich-metadata.md) for scope and licensing provenance.

## Widget-only embedding

```cpp
#include <nenia/Nenia.h>

auto *widget = new nenia::NeniaWidget(parent);
widget->setMediaInfo({QStringLiteral("My Track"),
                      QStringLiteral("My Artist"),
                      QStringLiteral("My Album")});
widget->setArtwork(applicationImage);
widget->noteOn(0, 60, 1.0f);
```

`QImage` values are copied implicitly. Explicit media fields override
controller-provided metadata until `clearMediaInfo()`. Explicit artwork
similarly overrides automatic artwork until `clearArtwork()`.

## Native-resolution widget

`NeniaWidget` is freely resizable above its practical minimum. Its
`minimumSizeHint()` is 840×480 and its `sizeHint()` is 960×600. The widget
fills its client rectangle and paints directly at the current pixel size; it
does not expose or require a 640×400 logical scene, an 8:5 aspect ratio, or
letterbox handling from the host. Panel geometry is internal and recomputed on
resize. Source Code Pro is the readable primary Latin/German face; the
existing Fusion Pixel asset is an explicit CJK fallback, not the main UI font.
The private renderer selects bounded integer visual-density tiers so text,
rows, keyboards, artwork, spectrum, knobs, and activity content scale
coherently; static text cells use actual `QFontMetrics`-driven elision. Matrix
keyboard height is derived from available row content height, while white/black
key topology remains an integer local geometry. TITLE, ARTIST, and FORMAT value
fields use a separate low-cost, clipped marquee timer only when rendered text
overflows, including while media is paused. Activity uses truthful 1.0/0.0
markers beside the ACT bar area. Artwork is aspect-fit from the original
decoded source through a smooth viewport cache; the artwork placeholder remains
computed in the same local clipped rectangle at every size.

The public display snapshot methods are:

```cpp
widget->setPlaybackUiState(playbackState);
widget->setPlaylistState(playlistState);
```

`NeniaPlaybackUiState::extendedMediaInfo` is one additive display flag so a
host can select the extended seven-row page without exposing internal Qt
metadata or adding a separate page-control API. It defaults to `false`
(primary page), is not persisted by the standalone host, and remains unchanged
when the current audio source changes.

`NeniaInputFocus` remains in `NeniaPlaylistState` for source compatibility,
but the standalone host no longer exposes focus switching and the renderer
does not use that field. `interactionHighlighted` controls the temporary
Playlist-outline accent; the reusable library still installs no keyboard
shortcuts.

## Playback and playlist API

```cpp
nenia::NeniaPlaylistController playlist(controller);
QString error;

// Existing reusable behavior: recursive folder scan, select, and play first.
playlist.loadDirectory(QStringLiteral("/music"), &error);

// Phase 10 host behavior: replace, prepare first, remain stopped.
playlist.loadDirectoryPrepared(QStringLiteral("/music"), &error);
playlist.loadItems(items, nenia::NeniaPlaylistLoadBehavior::PrepareFirst, &error);
playlist.playIndex(0);
playlist.removeIndex(0, &error);
```

Folder imports are recursive, transactional, hidden-entry safe, and do not
follow directory symlinks. `loadItems()` preserves order and duplicates. The
controller does not own dialogs, settings, or MIDI/JSON/artwork discovery.

M3U/M3U8 serialization is reusable but UI-free:

```cpp
nenia::NeniaPlaylistFileLoadResult result;
nenia::loadNeniaM3uPlaylist(path, &result, &error);
nenia::saveNeniaM3u8Playlist(path, result.items, &error);
```

The parser is UTF-8, local-path only, preserves authored order, skips invalid
entries with warnings, and saves through `QSaveFile`.

## Standalone host ownership

The `nenia` executable owns `QFileDialog`, `QSettings`, the flat Playlist UI,
global input router, selection/scroll snapshot, three-second playlist-activity
timeout, save-target policy, and the playlist-only immediate-parent MIDI
sidecar resolver. It has no native Qt toolbar; Previous, Play/Pause, Next,
Loop, and Shuffle are custom-painted by the widget below Artwork. The global
map is Ctrl+O (folder), O (M3U/M3U8), I (Media Info page),
P/N, Up/Down, PageUp/PageDown, Enter, D/Delete, Ctrl+Home/End, Ctrl+S, Space,
R, S, Left/Right, Home, and Ctrl+Q (quit). Ctrl+Q requires Control alone;
Esc, plain Q, Ctrl+Shift+Q, and Ctrl+Alt+Q do not quit. L and X have no
playback-order action. M, L, and X do not change playback. F1/F2/Tab no longer
switch Playlist/Visualizer input modes. Left-clicking a rendered row selects
without playing; Qt double-click activates it once through the same
`playSelected()` / `playIndex()` route as Enter. Blank space and invalid mouse
gestures do not activate a row.

The app owns independent Loop (`Off`, `RepeatOne`, `RepeatAll`) and Shuffle
(boolean) state. The six combinations are persisted at `playback/loopMode`
(`off`, `repeat-one`, `repeat-all`) and `playback/shuffleEnabled` (boolean),
default Off/false. Valid new settings take precedence over stale legacy data.
Legacy `playback/orderMode` migrates once; StopAfterCurrent→Off/false,
RepeatOne→RepeatOne/false, RepeatAll→RepeatAll/false, and Shuffle→RepeatAll/true
to preserve the old automatic bag-refill policy. The public
`NeniaPlaybackOrder` API remains a compatibility mapping, not app state. The
MODE footer geometry is removed. Previous/Next reuse P/N controller behavior;
Play/Pause reuses Space. The Loop button cycles only Loop, while Shuffle
toggles only Shuffle. Mouse Playlist row interactions restart the 3000 ms
highlight timer; transport controls do not.

The successful folder path is
stored separately at `playlist/lastFolder`; playlist-file imports do not alter
it. Startup restores the folder and prepares its first item without calling
play. Explicit `--audio` also prepares without autoplay and never discovers a
sidecar unless `--midi` is supplied.

## Media clock and visual domains

`QMediaPlayer::position()` remains authoritative. `NeniaPlaybackController`
owns Qt Multimedia, decoded PCM capture, the timestamped FFT cache, metadata,
and provider synchronization. JSON and MIDI providers remain independent
from the audio clock. The spectrum is audio-derived only; channel activity is
structured visual state only. Nenia does not synthesize MIDI, add waveform or
spectrogram views, or modify local media files.

MEDIA INFO primary page order is TITLE, ARTIST, ALBUM, LOCATION, PLAYBACK,
POSITION, FORMAT. I toggles the extended page TRACK, DATE, COMPOSER, ALBUM
ARTIST, PERFORMER, GENRE, DISC. Location is the currently loaded audio file's
canonical/absolute path (or a display URL), never the playlist, browser root,
or sidecar. The I-page choice stays in widget display state across track
changes and returns to primary on application restart.

`NeniaPlaybackUiState` has one additive `extendedMediaInfo` display flag so a
host can select the page through the existing widget-state boundary without
coupling the reusable library to global shortcuts. It defaults to `false` and
is not persisted by the library; all extended metadata remains an internal
playback/widget snapshot.

For the complete timing and reset rules see `audio-playback.md`,
`midi-provider.md`, and `spectrum-analysis.md`.
