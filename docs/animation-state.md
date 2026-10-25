# Nenia animation state and clock boundary

Phase 2 separates event input, mathematical display state, and painting:

```text
manual/provider event or media synchronization
        ↓
NeniaAnimationEngine
        ↓  DisplayState snapshot
NeniaRenderer
        ↓  native-resolution integer surface
NeniaWidget direct paint
```

The engine is private and provider-independent. It includes no QWidget,
QPainter, Qt timer, system clock, audio, JSON, MIDI, or file dependency.

## Fixed state domains

`NeniaAnimationState.h` uses fixed-size storage:

- 10 `TrackState` values for the visible track rows;
- 128 note slots per track for MIDI-style note numbers 0–127;
- one `bool held[128]` and one `float intensity[128]` per track;
- one `lastNote` integer per track, initialized to `-1`;
- one raw `program` and one raw CC10 `pan` value per track, initialized to
  `-1` when unknown;
- one monotonically increasing `lastNoteOnOrder` value per track, plus the
  engine's next order value, for deterministic focus selection;
- 19 independent `float` meter values.

Note state and meter state are deliberately separate. The engine does not
assume that ten visual tracks correspond to nineteen meter channels.

## Note operations

`noteOn(track, note, velocity)` validates indexes and clamps velocity to 0–1.
Positive velocity marks the note held, refreshes intensity with
`max(current, velocity)`, and records that note as the track's most recent
note. Repeated note-on events therefore never dim a bright note. Velocity 0
uses the same behavior as `noteOff`.

`noteOff(track, note)` clears only the held flag. Its current intensity remains
visible and enters release decay. The most-recent-note field remains available
for the text display until reset.

`programChange(track, program)` and `panChange(track, pan)` accept raw MIDI
values 0..127 and retain the latest value for that track. They do not create a
meter event, change note intensity, or affect the PCM spectrum. A successful
Note On records a global order token on its track; this makes focus independent
of the order in which renderer fields happen to be painted.

Display focus is still deterministic for consumers that need a single summary:
among tracks with held notes, the track with the newest Note On wins. If no
track currently holds a note, the newest mapped track that has ever received a
Note On wins. With no Note On history there is no focus. The channel-activity
renderer does not use that focus for its grid; it renders each track's
activity, provider-derived presence marker, current pan angle, program, and
retained key independently.

Invalid track and note indexes are ignored safely. Notes outside the visible
keyboard range are still valid state and are never wrapped into another key.

## Release and advancement

The engine accepts explicit `std::chrono::milliseconds` through
`advance(delta)`. Negative and zero durations are ignored. It never reads
`QDateTime`, `QElapsedTimer`, `system_clock`, or `steady_clock`.

Released notes use an exponential, frame-rate-independent decay:

```text
intensity(t + dt) = intensity(t) × exp(-dt / 250 ms)
```

Meters use the same rule with a separate 300 ms time constant. A value at or
below `0.0005f` is assigned exactly `0.0f`. This makes `hasActiveDecay()`
deterministic and lets the widget stop its timer without tiny values living
forever.

Held notes are never changed by `advance`. Positive meter levels are assigned
immediately, clamp to 0–1, and decay on subsequent advances unless refreshed.
An explicit zero clears a meter. `reset()` clears every held flag, intensity,
last-note value, program, pan, Note On order, and meter. It also resets the
global Note On order counter. This is the state boundary used for provider
replacement, restart, and backward-seek reconstruction.

The constants are private implementation policy, not user settings:

- note release tau: 250 ms;
- meter release tau: 300 ms;
- zero threshold: 0.0005f.

## Note naming and keyboard mapping

`noteNameForMidi` is centralized in `src/NeniaOctaveNaming.cpp`. It uses sharps
only and defaults to the convention MIDI 60 = `C4`; F4C allows the display
convention to be selected as C3, C4, or C5 without changing the note number:

| MIDI | Name |
| ---: | --- |
| 0 | C-1 |
| 12 | C0 |
| 60 | C4 |
| 61 | C#4 |
| 127 | G9 |

The accepted miniature keyboard has 56 white keys, arranged as eight octaves
with the existing five black-key positions per octave. Phase 2 maps those
positions to C1 (MIDI 24) through B8 (MIDI 119). The geometry is unchanged.
MIDI 0–23 and 120–127 remain valid in the engine but simply have no visible
key to illuminate.

## Public widget feed API

`NeniaWidget` exposes only the minimal provider-agnostic feed methods:

```cpp
void noteOn(int track, int note, float velocity = 1.0f);
void noteOff(int track, int note);
void setMeterLevel(int meter, float level);
void resetVisualization();
```

Track indexes are 0–9, note indexes are 0–127, and meter indexes are 0–18.
Invalid indexes are ignored. Normalized values are clamped consistently.
These are ordinary methods, not signals; future providers can call them
directly without knowing the demo or renderer implementation.

## Widget clock policy

The widget redraws immediately after each feed method. It starts a nominal
16 ms `QTimer` only if the engine reports a released note or positive meter
still decaying. Each timeout uses `QElapsedTimer::restart()` to measure actual
elapsed milliseconds and passes that duration to the engine. The engine does
not assume the timer fires at exactly 16 ms.

When no time-dependent state remains, the widget redraws the final neutral
frame and stops the timer. Starting the timer again resets the elapsed-time
origin; time spent idle is not charged against a later release.

When `NeniaPlaybackController` is attached, the widget enters private external
clock mode. Its wall-clock timer is stopped and remains disabled while the
controller owns synchronization. The controller's timeline synchronizer
advances this same engine from `QMediaPlayer::position()` deltas, including
event-boundary advances during forward playback and full replay during a
backward seek. Pause therefore freezes release exactly at the media position;
wall time spent paused is never charged to note or meter decay.

Destroying the controller safely returns a live widget to internal mode. Direct
feed methods remain available in either mode, but arbitrary manual feeds mixed
with a controlled provider are intentionally outside deterministic seek
reconstruction semantics.

## Renderer contract

`NeniaRenderer::render(surface, state, spectrumBands)` is deterministic for a
given note/meter `DisplayState`, separate 32-band spectrum array, palette, font,
layout, and private provider-used-channel presentation summary. It redraws the
complete logical surface, changing only:

- keyboard key fill colors;
- dynamic `NOTE C4`-style track fields;
- the unified ten-column activity panel in every source mode;
- one static provider-presence pointer, current-pan angle, program value, and
  retained key per column;
- current note activity bars, which are not decoded PCM or `MeterEvent` levels;
- live spectrum bars from the separately selected PCM frame.

Artwork, spectrum frame, axis labels, panel geometry, text policy, scaling, and
typography remain the accepted Phase 1B behavior. Spectrum values never enter
the animation engine's note or meter state and do not use its release decay.

## Timeline and playback boundary

Phase 3's JSON provider remains intentionally outside the animation engine.
Phase 4–6's `NeniaPlaybackController` attaches a
`std::shared_ptr<const TimelineEventProvider>` (JSON or MIDI) and uses the
media position as the only playback clock. At attachment it scans the loaded
provider's normalized events for mapped `NoteOnEvent` values to establish the
private ten-channel presence summary. The same controller also observes
decoded PCM with `QAudioBufferOutput`, but spectrum lookup remains separate
from this event path. In the private development checkout, the
`assets/demo/audio-timeline-fixture.json` fixture matches the original
`assets/demo/audio-playback-fixture.wav` diagnostic tone sequence. It covers all ten
tracks, white and black notes, different velocities, polyphony, and all
nineteen meters. These fixtures are excluded from public source archives.
Some meter feeds are deliberately sparse so media-time meter release remains
visible.

The provider owns no playback clock, and the controller does not synthesize
progress from timer intervals. Natural completion synchronizes the final media
position, then resets dynamic widget state while retaining external-clock
ownership and provider-derived pointers at that endpoint; pressing Stop has the
same dynamic reset, and Play/Restart begins from zero. Constructing
`NeniaWidget` in another application does not start audio or a timeline. All
source modes use maximum current note intensity for channel activity; semantic
`MeterEvent` and PCM values remain independent. Pointer absence means that no
provider-wide mapped NoteOn was found. An included channel with unknown pan
uses a center pointer for presentation only.
