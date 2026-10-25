# Nenia audio playback and presentation

Phase 4 adds local-file audio playback and Phase 5 adds a MIDI visual-event
source. Phase 6 observes decoded PCM for the live Spectrum panel. The existing
visual state remains synchronized to the media clock. `QMediaPlayer` decodes
the source, `QAudioOutput` remains the audible output, and
`QAudioBufferOutput` provides read-only PCM to the private analyzer. JSON and
MIDI providers both feed the same `TimelineEventProvider` boundary; MIDI is
never synthesized as audio.

## Public controller

`nenia::NeniaPlaybackController` is a reusable `QObject` that coordinates a
`NeniaWidget`, `QMediaPlayer`, `QAudioOutput`, its private
`QAudioBufferOutput`, and a timeline provider. Its compact API is:

```cpp
explicit NeniaPlaybackController(NeniaWidget *widget,
                                 QObject *parent = nullptr);

void setSource(const QUrl &source);
QUrl source() const;

void setTimelineProvider(std::shared_ptr<const TimelineEventProvider> provider);
void clearTimelineProvider();

void play();
void pause();
void stop();
void restart();

void setPosition(qint64 positionMs);
qint64 position() const;
qint64 duration() const;

void setVolume(float volume); // clamped to 0..1
float volume() const;
```

Useful signals expose position, duration, playback state, media status, and
playback errors without duplicating every `QMediaPlayer` signal. A controller
does not parse JSON or MIDI. The provider is held as
`std::shared_ptr<const TimelineEventProvider>` so a caller's local provider
variable may safely go out of scope after attachment.

## Folder playlist layer

`NeniaPlaylistController` is deliberately separate from the playback,
synchronizer, renderer, and spectrum layers. It recursively scans local
folders using Qt MIME detection, sorts normalized relative paths naturally,
and exposes both prepare-without-play and prepare-and-play load behavior. It
accepts the playback controller through a `QPointer`-safe relationship and
remains queryable if that controller or the widget is destroyed.

The authoritative order state combines `NeniaLoopMode` (`Off`, `RepeatOne`,
`RepeatAll`) with an independent Shuffle boolean. Natural completion is
delegated from the accepted `mediaStatusChanged` signal only for
`QMediaPlayer::EndOfMedia`, with one completion guard per source lifecycle.
Off/Off stops, RepeatOne restarts the same source, RepeatAll advances and
wraps, Off+Shuffle consumes one bag then stops, and RepeatAll+Shuffle refills
the bag and continues. Shuffle retains actual playback history for Previous;
RepeatOne+Shuffle still permits manual shuffled Next. Manual navigation always
starts the selected source. The older public `NeniaPlaybackOrder` setter and
getter remain as compatibility mappings; they are not the app's source of
truth. See [`playlist.md`](playlist.md) for the full API and settings contract.

## Media presentation metadata and artwork

Presentation is independent of structured timeline data. For local files the
controller reads TagLib's generic `FileRef::properties()` once when a different
source is prepared, then merges it with `QMediaPlayer::metaDataChanged()` into
one value-only widget snapshot. Non-local sources use Qt metadata only. TagLib
reads textual properties only; it is not used for playback timing, decoding,
PCM, audio format, or artwork. Qt's `CoverArtImage` path remains unchanged.
Metadata may arrive asynchronously after `setSource()`; an empty automatic Qt
field does not erase a TagLib value, and an empty title eventually falls back
to the filename stem (never the absolute path).

The widget resolves each field with this precedence:

1. a non-empty explicit host/app value set with `setMediaInfo()`;
2. Qt-preferred automatic metadata, enriched by TagLib for local files;
3. filename stem for title;
4. the existing neutral placeholder.

Artwork similarly resolves explicit `QImage` first, then valid embedded cover
art, then a valid same-directory local image, then `NO IMAGE`. External images
are checked in the exact order `Cover.jpg`, `cover.jpg`, `Cover.png`,
`cover.png`, `folder.jpg`, `Folder.jpg`; lookup is local-only, non-recursive,
and rejects cover symlinks.
`clearMediaInfo()` and `clearArtwork()` remove explicit overrides and expose the
latest automatic values. Explicit values survive source replacement. Automatic
values are cleared when the source identity changes. Re-selecting the current
playlist item restarts it from zero and rechecks its local cover; Pause/Resume,
seek, and automatic EOM/Repeat One transitions do not recheck. A deleted local
cover falls back to embedded art or the existing placeholder.
The outer title/artist header is removed; title, artist, and album remain
available in the primary `MEDIA INFO` page.

The optional `NeniaWidget::loadArtworkFile()` helper uses `QImageReader` with
auto-transform and native-resolution decoding for ordinary explicit-art files.
External covers use a separate bounded decode path: 64 MiB encoded-file limit,
16-million-pixel/8,192-dimension source-header limits, and an 8-million-pixel
retained-image cap. The widget keeps the decoded source and creates one
smooth-transformed cache sized to the current adaptive square viewport, so
disk decoding is outside the paint path and repeated downscaling is avoided.
Invalid or missing external art is a silent fallback and leaves audio/FFT
playback functional.

## Live MEDIA INFO pages

The default page has exactly seven rows, in order:

```text
TITLE · ARTIST · ALBUM · LOCATION · PLAYBACK · POSITION · FORMAT
```

Explicit non-empty title/artist/album host values override automatic metadata.
Otherwise TITLE uses Qt `Title`, TagLib `TITLE`, then the filename stem;
ARTIST retains the accepted Qt precedence ContributingArtist, LeadPerformer,
AlbumArtist, Author, then TagLib `ARTIST`; ALBUM uses Qt `AlbumTitle` then
TagLib `ALBUM`. LOCATION is the current audio
source's canonical local path when available (absolute path fallback), or its
display URL for a non-local source. It is never the M3U path, browser root, or
MIDI sidecar. PLAYBACK is `READY`, `PLAYING`, `PAUSED`, or `STOPPED`; POSITION
is `current/total`; FORMAT is decoded from `QAudioBuffer::format()` as
`PCM/<sample>/<rate>/<layout>` (for example `PCM/16-bit/44.1kHz/stereo`) and is
`--` until a usable decoded format is received.

I toggles the non-persisted extended page, which also has exactly seven rows:

```text
TRACK · DATE · COMPOSER · ALBUM ARTIST · PERFORMER · GENRE · DISC
```

TRACK uses TagLib `TRACKNUMBER`/`TRACKTOTAL`, including compound values, then
falls back to Qt `TrackNumber`. DATE and COMPOSER prefer TagLib's textual
`DATE`/`COMPOSER` values, then Qt. ALBUM ARTIST, PERFORMER, and GENRE prefer
Qt's existing values and use TagLib `ALBUMARTIST`, `PERFORMER` (or role keys),
and `GENRE` only as fallback. DISC uses TagLib `DISCNUMBER`/`DISCTOTAL`; Qt
6.12 exposes no corresponding public key. Multi-value strings join with
` / `, and track/disc number+total values display once as `n/total`. Empty
values display as `--`; exact precedence and unsupported-file behavior are in
[`rich-metadata.md`](rich-metadata.md).

The page flag is the additive `NeniaPlaybackUiState::extendedMediaInfo` display
field. The standalone app resets it to the primary page on restart and keeps
the current page across source/track changes. Toggling resets the marquee to
the initial 900 ms hold. Overflowing TITLE, ARTIST, ALBUM, LOCATION, POSITION,
FORMAT, COMPOSER, ALBUM ARTIST, PERFORMER, and GENRE use the accepted clipped
900 ms / 42 px/s marquee; PLAYBACK stays static.

The lower-right area always uses the ten-column `MIDI / CHANNEL ACTIVITY` grid.
Each column has its own
activity bar, ON/PAN knob, reconstructed PROGRAM value, and retained KEY
value. A provider-wide mapped `NoteOnEvent` summary controls pointer presence;
the marker is independent of the current held-note/release state. Pointer
angle follows current synchronized pan and unknown raw pan is shown at visual
center. JSON, MIDI, and no-provider/audio-only modes all use this same grid;
no-provider mode has no used-channel markers. `MeterEvent` and decoded PCM
levels never become channel activity bars.

The standalone host selects one visual provider. `--timeline` loads JSON,
`--midi` loads a Standard MIDI File, and supplying both is an error. An
explicit `--audio` without either visual option selects audio-only playback;
startup prepares it but does not play it. `--audio` remains independent from
the visual provider choice. `--playlist-folder` and `--playlist-file` prepare
their first item without autoplay. Playlist mode discovers only the selected
audio's immediate sibling `.mid`/`.midi` sidecar, clears provider state when
none is present, and continues decoded PCM spectrum analysis.

## Media clock authority

`QMediaPlayer::position()` is authoritative. The controller's approximately
16 ms timer only wakes the synchronizer; it never accumulates playback time by
adding 16 ms. `positionChanged` also calls synchronization, which covers
paused seeks and final position corrections. If a playing backend stalls at a
constant position, note/meter state and spectrum lookup also stop advancing.
PCM buffers may arrive ahead of that position, but their timestamps do not make
them eligible for display early.

`QAudioBufferOutput` delivery time is not display time. The controller
analyzes each decoded buffer into timestamped frames, stores them, and selects
the newest frame at or before `QMediaPlayer::position()`. A frame older than
200 ms is stale and produces a neutral spectrum. This prevents a frame from an
old location remaining visible after a large seek.

The synchronizer processes a forward interval incrementally:

```text
cursor = previous media position
for event in eventsBetween(previous, current):
    engine.advance(event.time_ms - cursor)
    apply event
    cursor = event.time_ms
engine.advance(current - cursor)
```

The provider's stable same-time order is retained, and time-zero starts with
`eventsBetween(-1, 0)` exactly once. A repeated sync at the same position is
idempotent.

## External widget clock

The Phase 2 widget mode remains available for direct manual event feeds. When
`NeniaPlaybackController` is constructed with a widget it enters the widget's
private external-clock mode and stops the widget's `QTimer`/`QElapsedTimer`.
The synchronizer advances the same animation engine only from media-position
movement, then asks the widget to render. This is the no-double-decay boundary.

Manual `noteOn`, `noteOff`, `setMeterLevel`, and `resetVisualization` remain
available while external mode is active, but hosts should not mix arbitrary
manual events with a controlled timeline if they require deterministic seek
reconstruction.

When the controller is destroyed, it stops polling and media playback, then
returns a still-live widget to its normal internal clock mode. A remaining
manual release may resume its normal wall-clock decay.

## Seek and lifecycle policy

- Forward seeks process the intervening range incrementally.
- Backward seeks reset the engine and replay from time zero to the target;
  release decay is advanced between event timestamps, never reversed.
- Provider replacement resets old visual state, replaces the provider-wide
  used-channel summary, and reconstructs at the current media position.
  Clearing a provider removes all pointers, leaves audio usable, and keeps the
  unified panel visible.
- Pausing synchronizes once and stops polling. Waiting in wall time does not
  advance note or meter release or spectrum display.
- Seeking while paused immediately reconstructs the new position and remains
  paused. The spectrum rolling window is reset; a cached destination frame may
  be shown immediately, otherwise the Spectrum panel is neutral until
  correctly timestamped PCM arrives.
- `stop()` stops media, stops polling, resets the cursor, and returns dynamic
  notes/meters/spectrum to neutral. A subsequent Play dispatches time-zero
  events once; provider-derived channel pointers remain present.
- `stop()` and `restart()` also return the displayed media position to zero;
  pause freezes the displayed position without a wall-clock substitute.
- `restart()` performs the same neutral reset, seeks to zero, reconstructs,
  and starts playback without clearing automatic media presentation.
- End-of-media synchronizes to the final reported position, stops polling, then
  calls `resetVisualization()` and remains at the media endpoint. This clears
  held notes, partial note releases, meter residuals, and all displayed
  spectrum bands while retaining provider-derived channel pointers; it is
  distinct from Pause, and there is no invented post-audio wall-clock tail.
  Play or Restart begins cleanly from zero.
- Replacing a different source stops the old source and resets visualization
  while retaining the attached provider. It clears old automatic
  metadata/artwork, installs the new source's filename-title fallback
  immediately, and leaves explicit host presentation overrides in place.
- Re-activating the same non-empty source identity restarts playback from zero,
  retains automatic metadata/artwork and the attached provider, and resets the
  Media Info marquee to its initial hold. Local identity uses canonical paths
  when available, with cleaned absolute paths as a fallback; distinct folders
  remain distinct even when filenames match.

Audio and visual-timeline lengths need not match. If audio continues after the
last event, existing note/meter state decays according to media time while
provider-derived pointers remain. If a JSON or MIDI timeline continues beyond
the audio duration, those events are never reached. Audio-only playback is
valid: the unified panel remains visible with neutral bars and no pointers,
the note matrix and semantic meters remain neutral, and the Spectrum panel is
driven by decoded PCM.

Requested positions are clamped at zero and, when duration is known, at the
media duration. Integer milliseconds are used throughout.

## Development demo deployment

In a testing-enabled development build, the standalone host copies
`audio-playback-fixture.wav`, `audio-timeline-fixture.json`, `multichannel-midi-fixture.mid`, and the original
`cover-artwork-fixture.png` into a `demo/` directory next to the executable
after build. A production build with `BUILD_TESTING=OFF` omits this copy step;
the application does not otherwise depend on `assets/demo/`.
The host can pass a fixture to `QMediaPlayer` as a normal local-file `QUrl`;
there is no resource-playback or source-tree-path dependency. Fixture inputs
are selected explicitly and are not loaded automatically at application
startup.

The demo WAV is original, mono, 16-bit PCM, 22,050 Hz, and 10 seconds long.
It is generated with Python's standard library by
`tools/generate_audio_and_timeline_fixtures.py`, using equal-tempered sine tones with
`A4 = MIDI 69 = 440 Hz`. The matching JSON contains note and meter events only;
the 19 meter columns are still JSON-driven, not audio-amplitude-driven. The
Phase 5 MIDI demo contains note events only, so its meter region may remain
neutral. These fixtures and their generator are private-development material
and are omitted from public source archives; this section records their
provenance, not a public build or runtime dependency.

Phase 6 adds `QAudioBufferOutput`, read-only PCM capture, the private FFT
analyzer, timestamped spectrum caching, and live Spectrum bars. Phase 8 adds
folder playlists and standalone order settings; Phase 9 adds the integrated
player and playlist-only MIDI sidecar attachment. Phase 10 adds the adaptive
native-resolution layout and persistent flat Playlist workspace without
coupling either feature to the PCM or structured-event paths. Nenia still does not add
waveform/spectrogram views, artwork extraction from sidecars, MIDI synthesis,
live MIDI I/O, recording, or audio modification. JSON/MIDI events remain
independent of the PCM spectrum, and the 19 semantic activity meters remain
timeline-driven rather than audio-amplitude-driven.
