# Playlist workspace

Phase 10 replaces the Phase 9 filesystem tree with a flat, ordered Playlist
workspace. The standalone host owns dialogs, selection, save-target policy,
last-folder settings, mouse interaction, custom transport controls, and MIDI
sidecar resolution. The reusable controller owns source items, orthogonal loop
and shuffle state, navigation, transactional replacement, and index-based
removal.

## Controller boundary

`NeniaPlaylistController` exposes:

```cpp
bool loadDirectory(const QString &, QString *error = nullptr); // prepare + play
bool loadDirectoryPrepared(const QString &, QString *error = nullptr);
bool loadItems(const std::vector<NeniaPlaylistItem> &,
               NeniaPlaylistLoadBehavior = PrepareFirst,
               QString *error = nullptr);
bool removeIndex(int index, QString *error = nullptr);
const std::vector<NeniaPlaylistItem> &items() const;
```

`loadDirectory()` keeps the earlier reusable behavior and starts its first
item. The Phase 10 app uses `PrepareFirst` for startup, folder import, and
playlist-file import. Preparation stops the old source, clears the provider,
and sets the new source without calling `play()`.

All successful folder scans are transactional. Recursive discovery includes
supported audio in the selected root and descendants, ignores hidden entries,
does not follow directory symlinks, and fails only when the complete scan has
no usable audio. Directories are sorted before files during traversal only as
an implementation detail; the final flat list is natural-sorted by normalized
relative path using the C locale. Numeric runs compare numerically, so
`track2` precedes `track10`.

Playlist rows display relative paths such as `AIR/01.mp3` and
`CLANNAD/01.mp3`; absolute paths are never used as the normal row label.

## UI selection and playback

Selection and current playback are separate. Up/Down changes the selected row
and automatically adjusts the first visible row. A small persistent marker is
shown only for a source that is currently Playing or Paused; a prepared,
stopped source is not falsely marked as playing.

Playlist selection and playback are global and need no focus-switch step:

```text
Up / Down    select the previous / next row
PageUp/Down  move selection by ten rows
Ctrl+Home    select the first row; End selects the last row
Enter        play the selected item immediately
D / Delete   remove only the selected playlist entry
Ctrl+S       save the current in-memory list
Left/Right   seek by five seconds; Right does not play the selected row
Home         seek to the start
S            stop audio
```

Mouse input is additive: a left-click on a painted row selects that exact
index without starting or interrupting playback; a Qt double-click selects
and activates the row once through the same `playSelected()` / `playIndex()`
path used by Enter. This retains same-source restart, provider, TagLib, and
artwork lifecycle behavior. Empty space, right-click, and press/release across
different rows do not select or activate anything. Row hit testing uses the
renderer-owned row rectangles, including the current first-visible row and
density-specific pitch. Valid mouse row clicks restart the existing 3000 ms
Playlist outline highlight.

Activating the currently loaded source with Enter restarts it from zero
and plays it again. Its automatic metadata, embedded artwork, and currently
attached timeline provider are retained; a different source still follows the
full replacement lifecycle and clears the previous source's automatic
presentation. Duplicate playlist rows remain separate indexes even when they
resolve to the same media URL, so either row can reactivate the shared source.
The standalone host re-resolves a playlist item's MIDI sidecar on its track
change notification; items without a sidecar remain providerless.

Removing an item never deletes or moves its audio file or MIDI sidecar. A
non-current removal leaves playback running. Removing the current item stops,
clears the source/provider, removes the item, selects the nearest survivor,
and prepares it without autoplay. Removing the last item leaves an empty,
stopped Playlist. Shuffle history and bags are rebuilt after every edit while
the selected order mode remains active.

## Host shortcuts (global)

```text
Ctrl+O  open a folder and recursively import it
O       open an M3U/M3U8 file
I       toggle the primary / extended MEDIA INFO page
P / N   previous / next; start immediately
Up/Down select playlist row
Enter   play selected row
D / Del remove selected row only
Ctrl+S  save current playlist
Ctrl+Home / End  select first / last row
Space   play/pause current source
S       stop audio
Left/Right  seek −/+5 seconds; Home seeks to zero
Ctrl+Q  quit Nenia
```

All shortcuts are available without switching focus. `Ctrl+S` is Save and
plain `S` remains Stop, resolving the former key collision. `Esc` and plain
`Q` do not quit; `Ctrl+Shift+Q` and `Ctrl+Alt+Q` are not bound. The Playlist
outline uses a stable accent highlight during playlist actions and returns to
its ordinary border 3.0 seconds after the most recent relevant action.
Up/Down, PageUp/PageDown, Ctrl+Home/End, Enter, D/Delete, Ctrl+S, O, Ctrl+O,
P/N, and valid row clicks trigger the highlight; another trigger before timeout restarts the
three-second timer. I toggles the Media Info page without highlighting the
Playlist. Generic playback keys (Space, S, R, Left/Right, Home) do not trigger
it. Valid Playlist single- and double-clicks do; I and transport-button clicks
do not. The reusable library does not install this host shortcut scheme.

The old Playlist-bottom MODE indicator and its reserved footer geometry are
removed. A five-button custom transport strip sits directly below the square
Artwork viewport: Previous, Play/Pause, Next, Loop, Shuffle. Its button
rectangles are derived from the renderer's adaptive layout and are also used
for widget hit testing. Previous/Next reuse their keyboard/controller paths;
Play/Pause reuses Space semantics. Missing playlist/source states are safe
no-ops.

Loop is a three-state setting (`Off`, `RepeatOne`, `RepeatAll`) and Shuffle is
an independent boolean. The Loop control cycles only the loop state; Shuffle
toggles only itself. Thus Off/Shuffle Off, Off/On, RepeatOne/Off, RepeatOne/On,
RepeatAll/Off, and RepeatAll/On are all represented. Loop Off uses a dim repeat
icon with opposing horizontal arrows; RepeatOne uses the active repeat icon
with a centered `1`; RepeatAll uses the active repeat icon without a marker.
Shuffle uses two straight crossing paths with right-side arrowheads, dim or
active by state. The five square-corner controls have geometric icons, hover
and pressed feedback, and no animation.

The authoritative persisted keys are `playback/loopMode` with tokens `off`,
`repeat-one`, `repeat-all`, and `playback/shuffleEnabled` as a boolean. Defaults
are Off/false. Valid new values override any stale `playback/orderMode`. When
migrating a legacy value, stop-after-current maps to Off/false, repeat-one to
RepeatOne/false, repeat-all to RepeatAll/false, and shuffle to RepeatAll/true.
The final mapping preserves the old Shuffle policy, whose natural completion
refilled its bag and continued indefinitely. `NeniaPlaybackOrder` remains as
an additive public compatibility API: its setter maps the legacy states into
the new model, and its getter is a documented lossy projection for old
callers. The standalone app never uses that projection as authoritative
state. `M`, `L`, and `X` do not control loop or shuffle.

End-of-media behavior is: Off/Off stops; RepeatOne repeats the current source
regardless of Shuffle; RepeatAll/Off advances sequentially and wraps;
Off/Shuffle consumes the current no-repeat bag and stops at exhaustion;
RepeatAll/Shuffle refills at exhaustion and continues. Manual Previous follows
actual shuffle history. Manual Next draws from the bag and may explicitly
start a new bag even under Loop Off; only automatic end-of-media refill is
disabled there. Turning Shuffle on creates a bag excluding the current item;
changing Loop does not discard that history. Turning Shuffle off resumes
sequential navigation from the current index.

## Folder and playlist-file sources

`Ctrl+O` opens a folder and replaces the current list only after recursive import
succeeds. It selects/prepares the first row, remains stopped, and stores the
canonical folder path at `playlist/lastFolder`.

`O` opens `.m3u` or `.m3u8`, preserves authored order, skips invalid local
entries with concise warnings, and replaces the list only if at least one
supported audio entry remains. It does not modify `playlist/lastFolder`.
Duplicate playlist-file entries remain duplicate index-based rows.

`I` switches between the seven-row primary Media Info page and seven-row
extended credits page. It changes no source, selection, or playback state and
is not persisted; the choice remains in effect across track changes until
the user toggles it or restarts the app.

On normal startup, a readable `playlist/lastFolder` is restored recursively
and prepared without autoplay. An unavailable stored path is reported
non-modally, cleared, and replaced by an empty stopped Playlist. Explicit
`--audio`, `--playlist-folder`, and `--playlist-file` modes do not restore a
different source. Explicit CLI folder success updates the same last-folder
setting.

## MIDI sidecars

Playlist sources use the app-side `NeniaMidiSidecarResolver`. For
`/path/sub/song.live.mp3`, only `/path/sub/song.live.mid` and
`/path/sub/song.live.midi` are considered. Matching is case-insensitive and
complete-basename based; `.mid` takes precedence. Each M3U/M3U8 item resolves
against its own immediate parent, never against the playlist file's directory.
No JSON sidecar discovery is performed. Direct `--audio` mode never enables
automatic sidecars; `--midi` remains an explicit provider option.

See [`playlist-file-format.md`](playlist-file-format.md) for the file format
and atomic save contract, and [`adaptive-layout.md`](adaptive-layout.md) for
the native-resolution presentation model.
