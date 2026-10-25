# Architecture

Nenia keeps reusable playback, visual state, rendering, and standalone host
policy separate.

```text
Local Audio ─┬─> QMediaPlayer ──> PlaybackController ──> NeniaWidget
             │       │          ├─ PCM ─> FFT ─────────> Spectrum
             │       │          └─ position ───────────> TimelineSynchronizer
             │       └─ QMediaMetaData ─┐
             └─> TagLib PropertyMap ────┴─> Metadata Merge ─> presentation snapshot
Remote Audio ───> QMediaPlayer / QMediaMetaData ────────────┘
                                                        └─> adaptive Renderer
QSettings visualization/keyboardRange ─> app menu ─> display-only range ─────┤
QSettings visualization/octaveNamingConvention ─> app menu ─> note labels ───┘

JSON ─> JsonTimelineProvider ────────────────────────────────┐
SMF ─> internal NeniaMidiSequence ─┬─> legacy MidiTimelineProvider (0–9) ─┐
                                  └─> internal full MIDI visual projection (0–15) ─┴─> TimelineSynchronizer ─> 16-channel state
                                                                                         └─> one-page CH01–CH16 Matrix

Opt-in Phase 11D/11E:
NeniaMidiSequence ─> sample-frame sequencer ─> FluidSynth worker ─┬─> output SPSC ─> QAudioSink
                                                                  └─> bounded PCM tap
                                                                       └─> consumed-frame gate
                                                                            └─> same FFT ─> Spectrum

MIDI Meta events ─> F1 metadata snapshot ─> existing seven-row Media Info pages
QSettings midi/soundFontPath ─> app SoundFont priority/configuration

Recursive Folder / M3U8
          ↓
     Playlist Model
          ↓
   selected/current item
          ↓
NeniaPlaylistController
       ├→ audio source
       └→ app-side MIDI sidecar resolver
```

The reusable library never owns `QFileDialog`, `QSettings`, host keyboard
shortcuts, or the playlist sidecar policy. The standalone app owns those
decisions and feeds value snapshots to `NeniaWidget`.

## Library layers

`NeniaWidget` owns the animation engine, display presentation, spectrum bands,
primary and extended media snapshots, artwork source/cache, current native-size
surface, and a separate low-cost Media Info marquee timer. The playback
controller merges Qt metadata and (for local audio only) a TagLib rich-tag
snapshot into widget presentation values; the renderer never queries
`QMediaPlayer`, `QMediaMetaData`, or TagLib. `NeniaRenderer` computes integer
`NeniaLayout` geometry from the actual surface size and draws the Playlist,
single-page sixteen-channel Matrix, right-side artwork/media information, and
the expanded Spectrum directly into that surface. One selected keyboard range
is shared by all sixteen rows. Every Matrix row has its own PAN indicator and
separate ACT ring; the obsolete right-side channel
Activity panel is not part of the layout. No whole-scene image transform is
used.

`NeniaPlaybackController` owns `QMediaPlayer`, `QAudioOutput`,
`QAudioBufferOutput`, timestamped PCM analysis, provider synchronization, and
automatic metadata/artwork. For encoded audio, `QMediaPlayer::position()` is
the media clock. In the opt-in direct-MIDI route, the application-only router
delegates state and position to `NeniaMidiPlaybackEngine`; its sequence parser
is the source for MIDI metadata, current channel state, and the legacy visual
projection. The synthesis worker publishes canonical stereo Float32 PCM to a
separate fixed-capacity analysis tap. The controller consumes only blocks
whose tagged output frames have been pulled by the QAudioSink callback and
feeds those samples into the same `NeniaSpectrumAnalyzer` used by encoded
audio. The audio callback does not perform analysis, and the output ring keeps
one consumer. The tap is bounded and may drop visual data under load without
blocking audio. SoundFont preferences and the small file picker remain app
policy; the path is resolved as CLI, environment, then saved QSettings
preference. TagLib is a synchronous, read-only text-tag reader
called once when a different local source is prepared; it is not used by
painting, playback timers, artwork loading, or playlist scanning. Qt metadata
merges asynchronously with that cached per-controller value snapshot.
JSON/MIDI providers remain read-only timestamped event sources; direct MIDI is
an audio path only in the opt-in application/backend configuration.

`NeniaPlaylistController` owns ordered `NeniaPlaylistItem` values, current
index, playback order, shuffle history, transactional recursive scans,
prepare/play behavior, append operations, and index-based edits. Appending
preserves the active source and index; with Shuffle enabled, new items join
the remaining-order bag without resetting history. `PrepareFirst` stops and
sets a source without autoplay; `PlayFirst` preserves the earlier direct
library behavior. It never parses M3U files or reads settings.

The standalone host's `Open / Playback / Settings` menus and global input
router share the same folder, explicit playlist-load, append-media, transport,
Loop, Shuffle, and keyboard-range commands. `Settings → Keyboard Range`
persists one of the four renderer-only ranges at
`visualization/keyboardRange`; changing it does not touch MIDI channel state or
the playback clock. `O` appends supported media and playable entries
expanded from M3U/M3U8 files; `Ctrl+O` retains folder import. Open Playlist is
distinct and retains the established playlist-loading behavior. These are app
policies and do not add a second playback state machine to the reusable
library.

`NeniaPlaylistFile` is a small UI-free UTF-8 M3U/M3U8 parser/saver. It keeps
authored order and duplicates, resolves local relative paths, returns warnings
for invalid entries, and saves atomically with `QSaveFile`.

## MIDI sequence model

The private Phase 11C `NeniaMidiSequence` is the sole Standard MIDI File parser.
It accepts Format 0/1 PPQN files, retains ordered raw channel-voice, system,
SysEx, and meta events from all 16 channels, and records each source track's
End-of-Track tick. Format 1 events merge deterministically by `(tick, track,
event)`. Tempo conversion retains its exact fractional tick remainder across
tempo segments; millisecond and sample-frame projections round once at their
final result. `stateAtTick` reconstructs logical channel state by replaying the
stable prefix, including per-channel controllers, RPN/bank/program data,
sustain, and overlapping notes. It does not represent synthesized voices.

The installed `MidiTimelineProvider` interface remains unchanged. Its
implementation is now a compatibility projection of the shared sequence to
the existing ten visual tracks, Note On/Off, Program Change, and CC10 events;
channels 10–15 continue to be omitted from that view and reported through the
existing ignored-channel diagnostic. The sequence model is internal and is not
installed as a new public API. Phase 11E F2 adds a separate private
`NeniaMidiVisualTimelineProvider` that projects the same parsed sequence into
all 16 physical channel states without changing the public event variant or
legacy provider. The Matrix maps row `n` directly to MIDI channel `n` and labels
it `CH01` through `CH16`; raw channels 9 and 15 therefore appear as CH10 and
CH16. Direct MIDI reuses the playback router's sequence snapshot, and audio
sidecars use this internal projection with the QMediaPlayer clock. Legacy
ten-track JSON and installed-provider sources retain their existing semantics
and leave the remaining rows neutral. Historical track/type records remain
compatible internally but no longer produce FM/SSG/ADP Matrix labels. The
opt-in Phase 11D path consumes that sequence through the separate
`NeniaMidiPlaybackEngine`; Phase 11E routes it through the
normal app's existing controller facade and mixed Playlist. Phase 11E F1 adds
conservative MIDI metadata and current-position tempo/time-signature values,
SoundFont persistence, and the generation-tagged PCM analysis tap described
above. It does not change the installed package API or the legacy audio-sidecar
route. The Phase 11B offline renderer remains available and keeps its original
44.1 kHz / effects-off behavior.

Phase 11E F4B extends only the internal MIDI visual stream with persistent
CC7 volume, CC11 expression, and CC121 reset transitions. The provider keeps
these events in the same `(tick, source track, source event)` order as notes,
Program Change, and PAN, while the installed `TimelineEvent` variant remains
unchanged. CC7 is displayed as its raw 0–127 value (`V000`–`V127`); `V---`
means no explicit or verified reset-derived display value is known. This is
separate from the channel state's effective synthesizer fallback. The adopted
RP-015 CC121 policy preserves CC7 and CC10, while projecting reset Expression
and other controller state in source order; it does not manufacture a volume
or PAN event. Direct MIDI follows the output-consumed playhead; sidecar MIDI
follows the encoded-audio media clock. Backward seek clears and replays the projection prefix. The internal
channel state also remembers the Bank Select active at the latest Program
Change, separately from the current Bank Select controller, so seek restoration
can preserve source event order without changing the selected preset. Runtime
verification with FluidSynth 2.3.5 found its initial CC7/CC10/CC11 values and
its CC121 behavior; native CC7 and CC10 remain unchanged, consistent with the
adopted reset policy. The sequencer dispatches CC121 natively without extra
volume/pan writes. Native Expression, pedal, pitch bend, Bank Select, and
Program retention behavior is covered by runtime tests.

Phase 11E F4C adds one app-persisted octave naming preference with Middle C
at C3, C4, or C5 (C4 default). The private formatter maps the canonical MIDI
note integer to a display label at render time, so all sixteen rows update
immediately on selection, including last-note labels retained after Note Off.
Keyboard range bounds, key indices, note events, and synthesized pitch are
unchanged. The additive widget setter accepts the selected middle-C octave
without exporting the internal convention enum.

## Native-resolution layout

The widget reports a preferred size of 960×600 and a minimum practical size of
840×480. A density-selected 6/8/10 px outer margin bounds a `contentRect`; the
layout uses three columns inside it:

```text
Playlist | Channel Matrix | Right Information / Visualization
```

The Playlist remains at least 200 px wide. The Matrix remains at least 320 px
wide at supported sizes so its 56-white-key keyboards do not collapse below
the accepted pre-Phase-9 usable width. The right column remains at least
280 px. Available height is distributed among sixteen compact Matrix rows,
square artwork/seven Media Info rows, and Spectrum. Matrix row pitch is derived
from the actual height available after its header and padding; the row's
keyboard top and height are then derived from that row. White and black
keyboard rectangles share one local geometry, with black-key height derived
from the same inner height.

The outer Matrix panel contains an inset `CHANNEL MATRIX` header followed by
sixteen compact per-row frames. Each row places channel/note/program/CC7 volume
status before an independent PAN knob with an ACT ring, immediately
followed by its miniature keyboard. Its row pitch is recalculated from the
remaining panel height, so all rows stay inside the frame at the minimum
window size without paging or channel scrolling. The surface keeps Nenia's dark
background visible around the panels; this intentional margin is not an 8:5
lock or a scaled letterbox. There is no fractional painter transform.
`NeniaVisualMetrics`
selects bounded Normal/Large/ExtraLarge integer metrics from the viewport so
typography, rows, keyboards, artwork, spectrum, and PAN/ACT indicators grow
coherently. The artwork frame owns an inner square content rectangle for
both images and the clipped NO IMAGE pattern. The artwork cache preserves the
original decoded source and uses one smooth viewport-sized resample when that
target square changes. Spectrum remains 32 bands and now occupies the full
right-column region below Artwork/Media Info; there is no reserved Activity
module beneath it. ACT remains the accepted note/activity representation and
is independent from the CC10-derived PAN pointer. The external ring uses a
segmented 270-degree sweep with an open bottom, and its illumination is
proportional to clamped ACT.

The right column uses the authoritative `rightColumn` allocation as one
square-corner outer frame, inset one pixel and drawn in axis blue `#3D6F8C`,
the shared normal-frame tone for the Playlist and Channel Matrix. Inside it,
`rightColumnInnerRect` adds 3/4/5 px of density-based padding. ARTWORK / MEDIA
INFO and SPECTRUM / LIVE FFT derive their horizontal bounds from this shared
rectangle. Artwork/Media Info remains above Spectrum; Spectrum extends to the
inner bottom edge and reclaims the previous Activity module's allocation. The
sections share the column background without individual outer fills or frames;
titles and the section gap provide separation. Internal surfaces such as the
square artwork viewport, Media Info rows, and FFT plot retain their own fills
and frames. The Playlist and Channel Matrix also retain their accepted outer
frames, with Matrix rows using the grid color. The active Playlist highlight
remains cyan with its existing three-second behavior. There is no standalone
right-column separator. PAN pointers are flat-capped center-to-edge radial
lines whose direction retains the accepted CC10 mapping and whose length
derives from each row's knob geometry; unknown PAN points neutrally.

The Matrix title is the reference for all primary-container title placement.
Its frame-relative left inset is 7/9/11 px across Normal/Large/ExtraLarge
density, and its top inset is 2 px. The Right Column's ARTWORK and SPECTRUM /
LIVE FFT titles use the same left inset; ARTWORK
and MEDIA INFO also share the Matrix title's top placement. MEDIA INFO remains
anchored to its two-column content layout. Title insets remain independent of
the optimized Right Column content inset.

The Matrix keyboard is generated from one integer local coordinate system:
white-key boundaries are partitioned first, and black-key rectangles derive
from the appropriate boundary. Rendering and active-note highlighting consume
the same geometry, preserving MIDI topology across resize.

Source Code Pro is the primary readable Latin/German UI face at the same
bounded integer tier sizes; the bundled Fusion Pixel zh-Hans face is an
explicit fallback for missing CJK glyphs. Every static text cell elides
through `QFontMetrics` before drawing. Primitive geometry remains crisp while
text antialiasing is enabled for readability. Artwork uses smooth pixmap
transforms, and the Media Info marquee clips raw overflowing values to their
individual value rectangles.

The primary Media Info page is TITLE, ARTIST, ALBUM, LOCATION, PLAYBACK,
POSITION, FORMAT. The extended page is TRACK, DATE, COMPOSER, ALBUM ARTIST,
PERFORMER, GENRE, DISC. The I-key page flag is kept in widget presentation
state only; it is not persisted and is not reset by track changes.

`TITLE`, `ARTIST`, `ALBUM`, `LOCATION`, `FORMAT`, and `POSITION` use the same marquee policy: an
overflowing value starts at the left edge of its value rectangle, holds for
900 ms, then two clipped copies move right-to-left through a
`textWidth + valueRect.width()` cycle. The repeated copy enters from the
right after one full field-width of spacing, while PLAYBACK remains static.
The extended-page COMPOSER, ALBUM ARTIST, PERFORMER, and GENRE values also
marquee; TRACK, DATE, and DISC are eligible too. The widget starts its low-cost
timer only when a visible page has an overflowing field and the widget is
visible. Changing pages resets the marquee to its initial hold.

## Playback loop/shuffle lifecycle

The authoritative order state is orthogonal: `NeniaLoopMode` is Off,
RepeatOne, or RepeatAll, and `shuffleEnabled` is independent. The six
combinations remain representable. Natural completion is handled from
`EndOfMedia`: Off/Off stops; RepeatOne repeats the current source; RepeatAll
without Shuffle advances sequentially and wraps; Off+Shuffle consumes one
no-repeat bag and stops at exhaustion; RepeatAll+Shuffle refills and continues.
RepeatOne takes precedence for automatic completion but leaves Shuffle armed
for manual Next. Shuffle keeps a playback history for Previous; changing Loop
does not reset it, and turning Shuffle off resumes sequential navigation from
the current index. Manual Next can explicitly refill an exhausted bag even
when Loop is Off; automatic refill remains disabled in that state.

`NeniaPlaybackOrder` remains in the installed public API for source
compatibility. Its setter maps old modes into the new state; its getter is a
lossy projection (`RepeatOne` first, then `Shuffle`, then `RepeatAll`, else
`StopAfterCurrent`). The app does not use that projection as authoritative
state. Startup/import preparation never calls play. Activating the
already-loaded source through Enter or Playlist double-click restarts it from
zero while preserving automatic metadata, artwork, and attached provider.
Source identity is canonical-path based for local files and normalized-URL
based otherwise; playlist indexes remain distinct for duplicate URLs.
Different-source activation retains the normal presentation-clear and
filename-fallback flow.

## Standalone state flow

The app builds a `NeniaPlaylistState` snapshot containing display-relative row
names, selection, current-playing marker, first visible row, source label, and
temporary interaction-highlight state. Input is global rather than divided
between Playlist and Visualizer focus; the compatibility focus field is
ignored by rendering and the host. The current-playing marker is absent for a
prepared stopped source.

`Ctrl+O` recursively imports a folder and writes `playlist/lastFolder` only
after a successful transaction. Plain `O` appends selected media files and
playable entries expanded from M3U/M3U8 without changing that key. `I` toggles
the two Media Info pages. The five custom-painted
Previous, Play/Pause, Next, Loop, and Shuffle controls are located below
Artwork; transport hit testing consumes the same rectangles as rendering.
Loop and Shuffle persist separately under `playback/loopMode` and
`playback/shuffleEnabled`. Legacy `playback/orderMode` migrates once; old
Shuffle maps to RepeatAll+Shuffle because it refilled automatically at natural
completion. Valid new keys take precedence. The M key and Playlist MODE footer
are removed. Mouse row click selects; Qt double-click activates once through
the same path as Enter. Valid row clicks highlight the Playlist for three
seconds; I and transport controls do not.
Invalid last-folder settings are cleared non-modally. Sidecars are checked
only for playlist items and only in each item's immediate parent directory.

## Source map

The test-file entries below describe the private development regression
suite. The `tests/` directory is excluded from public source archives; a
production build with `BUILD_TESTING=OFF` does not require those files.

- `src/NeniaRenderer.cpp` — adaptive integer layout and native-resolution paint
- `src/NeniaWidget.cpp` — surface lifecycle, presentation snapshots, cache
- `src/NeniaMediaMetadata.cpp` — one-time Qt metadata-to-display snapshot mapping
- `src/NeniaPlaylistController.cpp` — recursive scan, append, loop/shuffle, prepare/edit API
- `src/NeniaPlaylistFile.cpp` — M3U/M3U8 parser and atomic saver
- `src/NeniaMidiSequence.*` — internal SMF parser, ordered events, tempo map
- `src/NeniaMidiChannelState.*` — seek-reconstructed logical MIDI channel state
- `src/midi_playback_backend/` — opt-in sample-frame sequencer, bounded PCM ring, and QAudioSink engine
- `app/NeniaApplicationPlaybackRouter.*` — opt-in direct-MIDI route behind the existing playback-controller facade
- `src/NeniaAlternatePlaybackBackend.h` — private alternate-source seam; not installed
- `tests/tst_direct_midi_integration.cpp` — mixed-source Playlist, transport, and visualization integration checks
- `tools/midi_playback_diagnostic.cpp` — separate interactive realtime playback harness
- `tests/tst_midi_playback_engine.cpp` — headless event, seek, synthesis, and PCM tests
- `tests/tst_midi_sequence_parser.cpp` — sequence, timing, state, and adapter tests
- `app/main.cpp` — host, dialogs, settings, sidecars
- `app/NeniaGlobalInput.*` — focus-free global keyboard actions
- `app/NeniaFileImport.*` — mixed media/MIDI/playlist expansion for Open File
- `app/NeniaMenuBar.*` — shared Open/Playback/Settings menu commands
- `app/NeniaPlaylistActivity.*` — timed Playlist-outline interaction state
- `app/NeniaMidiSidecarResolver.cpp` — complete-basename sidecar matching
- `app/NeniaSettings.cpp` — order and last-folder QSettings helpers
- `tests/tst_playlist_files_and_import.cpp` — adaptive, import, file, edit, settings tests
- `tests/tst_responsive_layout.cpp` — density, keyboard topology, artwork, and
  wide-viewport visual regressions
- `tests/tst_media_info_spectrum_layout.cpp` — font/fallback policy, keyboard-height,
  marquee, Activity-marker, and smooth-artwork regressions
- `tests/tst_playback_settings_and_metadata.cpp` — shortcut, metadata, seven-row layout,
  margins, matrix-frame, marquee reset, and source-lifecycle regressions
- `tests/tst_channel_matrix_integration.cpp` — sixteen-row layout, PAN/ACT, import, menus
- `tests/tst_playlist_transport.cpp` — mouse sequencing, adaptive transport
  geometry/hit tests, six settings states, and legacy migration
- `tests/tst_nenia_playlist_controller.cpp` — shuffle bag, history, and
  orthogonal end-of-media policy regressions

The Phase 9 tree model and tree-specific tests are removed. No media database,
network source, drag-and-drop, ratings, favorites, waveform, realtime MIDI
hardware I/O, or JSON sidecar discovery is part of the application path. When
`NENIA_BUILD_MIDI_PLAYBACK_BACKEND` is enabled, the normal app can directly
play local MIDI in its existing Playlist through the Phase 11D FluidSynth /
QAudioSink engine; encoded audio and legacy audio+MIDI-sidecar playback remain
on QMediaPlayer. This optional integration is documented in
`phase11e-direct-midi-integration.md`. The default build and installed
`Nenia::Nenia` package remain independent of FluidSynth. Direct MIDI and
full-channel sidecars display all 16 channels in the unified Matrix; legacy
ten-track JSON/provider data leave the remaining rows neutral. MIDI-native
information/activity semantic redesign remains deferred.
