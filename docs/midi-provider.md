# Nenia MIDI timeline provider

Phase 5 adds `nenia::MidiTimelineProvider`, a second implementation of the
existing `TimelineEventProvider` interface. It converts Standard MIDI File
visual note, program, and pan events into the same normalized `TimelineEvent`
stream used by the JSON provider. It does not synthesize sound, open a MIDI
device, or own a playback clock.

The audible source remains the Phase 4 `QMediaPlayer`/`QAudioOutput` path. When
the playback controller is active, `QMediaPlayer::position()` remains the only
authoritative clock. MIDI ticks are normalized to integer milliseconds before
they reach `NeniaTimelineSynchronizer`.

## Public API

```cpp
nenia::MidiTimelineProvider provider;
QString error;
if (!provider.load(QStringLiteral("song.mid"), &error)) {
    // error contains a contextual parse/load diagnostic
}

const std::vector<int> &ignored = provider.ignoredMidiChannels();
```

`loadMidi(const QByteArray &, QString *)` is available for deterministic
fixture/resource loading. Loading is transactional: a failed load leaves the
previous valid event sequence and ignored-channel diagnostics intact.

The provider implements `isLoaded()`, event counts, first/last event times,
arbitrary binary-searchable `eventsBetween(fromExclusive, toInclusive)`
queries, and `clear()` with the same semantics as `JsonTimelineProvider`. A
valid MIDI file containing no mapped notes is still a loaded empty provider.

## Standard MIDI File support

Supported formats:

- SMF Format 0 with exactly one track.
- SMF Format 1 with one global merged timeline.
- PPQN/ticks-per-quarter-note time division.

Rejected explicitly:

- Format 2, because its independent sequences do not define one global Nenia
  timeline.
- SMPTE time division, which is deferred rather than approximated.
- Zero PPQN, malformed headers, impossible chunk lengths, truncated tracks,
  missing End of Track events, trailing bytes after End of Track, and unsafe
  arithmetic conditions.

The `MThd` header accepts extensions longer than the required six data bytes
and skips those extension bytes safely. Each declared `MTrk` boundary is
authoritative; the parser never scans later bytes for recovery.

Variable-length quantities are limited to the Standard MIDI File four-byte
form and are checked for truncation, malformed continuation, and overflow.
Absolute ticks are accumulated before any conversion to milliseconds.

Diagnostics include track, event, and byte-offset context where an event has
been reached. Typical failures include `Unsupported Standard MIDI File format
2`, `SMPTE time division is not supported`, `track 0 event 3: running status
used without prior channel status`, and `track 1: truncated SysEx payload`.

## Timing and ordering

The default tempo before any `FF 51` Set Tempo event is 500,000 microseconds
per quarter note (120 BPM). All Set Tempo events from all tracks participate in
the Format 1 global tempo map, including tempo events outside track 0.

Raw events are ordered deterministically by:

1. absolute tick;
2. source SMF track chunk index;
3. original event index within that track.

At a shared tick, all events receive the same timestamp. Multiple tempo events
at that tick are processed in that order, and the last one controls subsequent
positive tick intervals.

The implementation integrates each constant-tempo interval using an integer
whole-microsecond plus remainder accumulator with PPQN as the denominator. A
final timestamp is rounded to the nearest millisecond, with exact half-
millisecond ties rounded upward. Different ticks that collapse to one
millisecond retain their original exact MIDI order in the final vector.

## MIDI event normalization

All channel voice messages are parsed with their standard lengths so that
ignored messages cannot corrupt stream position:

- Note On and Note Off become Nenia events.
- Note On velocity 0 becomes `NoteOffEvent`.
- Nonzero Note On velocity is normalized as `velocity / 127.0f`.
- Note Off release velocity is parsed as message data but is not exposed by the
  current Nenia event model.
- Program Change (`0xC0`) becomes `ProgramChangeEvent` with the raw program
  value 0..127.
- Control Change 10 (`0xB0`, controller 10) becomes `PanChangeEvent` with the
  raw pan value 0..127.
- Polyphonic pressure, other Control Change messages, Channel Pressure, and
  Pitch Bend are parsed/skipped but do not become Nenia events. This includes
  Control Change 7 and 64; sustain does not delay Note Off.

Running status is supported for channel messages and data bytes without a
valid preceding channel status are rejected. Standard F0/F7 SysEx events are
skipped by their declared VLQ payload length. Generic meta events are skipped
by their declared length. Set Tempo and End of Track are handled explicitly;
bytes after a valid End of Track are rejected rather than interpreted as
events.

MIDI channel mapping is direct and zero-based:

| MIDI channel | Human channel | Nenia track |
| ---: | ---: | ---: |
| 0..9 | 1..10 | 0..9 |
| 10..15 | 11..16 | not mapped |

Channels 10..15 are still parsed. If they contain note messages, each such
channel is reported once by `ignoredMidiChannels()` using zero-based values.
The host reports them using human 1..16 labels. MIDI channel 9 (human Channel
10) maps normally to Nenia track 9, including percussion notes; no drum-name
semantics are invented.

All mapped Note On/Off events are retained in source order. Chords and
polyphony work through the existing 128-note state. Same-note retriggers are
not given a new voice allocator: the provider preserves the source events and
the existing held/intensity state model determines the resulting display.
MIDI does not synthesize `MeterEvent`; the meter region may remain neutral.

## Program, pan, focus, and activity state

Mapped Program Change and CC10 events are dispatched through the existing
timeline synchronizer. They are not a second side channel, and they do not
alter the JSON schema. The synchronizer preserves source track/event order for
events sharing a tick, applies the existing
`(fromExclusiveMs, toInclusiveMs]` range, and reconstructs the same state on a
backward seek or restart. Reapplying a paused media position is a no-op, so
pause does not advance note release or overwrite program/pan state.

The animation engine stores raw program and pan values per mapped track. The
renderer displays programs as `001`..`128` (`raw + 1`) and uses this pan policy:

| Raw CC10 | Display |
| ---: | :--- |
| 0 | `L64` |
| 1..63 | `L63`..`L01` |
| 64 | `CENTER` |
| 65..127 | `R01`..`R63` |

Unknown program or pan state remains explicit `--` in the underlying state and
legacy summaries. Tracks 10..15 are parsed for diagnostics but never write
mapped state or display activity.

The unified level panel is an FMDSP-inspired ten-column
`CH01`..`CH10` grid for MIDI, JSON, and no-provider/audio-only modes. Each
column contains its own activity bar, circular ON/PAN knob, program row, and
retained recent-key row. On provider attachment, the controller scans the
complete normalized event sequence and marks a channel used if any mapped
`NoteOnEvent` exists anywhere in the timeline. That pointer-presence marker is
independent of the current held-note state: it remains visible before the
first note, after the last note, after Note Off, and through Stop or
EndOfMedia. Provider replacement swaps the markers; clearing the provider
clears them. No-provider mode has no markers.

Each bar is the maximum current note intensity on that mapped track, so it is
visual note activity rather than decoded audio level. Released notes keep their
existing animation-engine release decay, but release does not remove a
provider-derived pointer. The current synchronized CC10 value controls pointer
angle; unknown pan uses a visual center without changing the underlying `-1`
state. Pointer direction is a quantized visual sweep from left through
up/center to right. No synthetic `MeterEvent`, audio-level interpretation, or
second activity decay path is introduced. JSON `MeterEvent` values remain
compatible timeline state but are not drawn as channel bars.

## Demo and command line

The private development fixture `assets/demo/multichannel-midi-fixture.mid` is an original
deterministic Format 1 file generated by `tools/generate_midi_fixture.py`
using Python's standard library. The fixture and generator are excluded from
public source archives. It uses PPQN 480, 120 BPM, three tracks, all MIDI
channels 0..9, varied velocities, chords, running status, Note On velocity-zero
normalization, explicit Note Off, harmless meta/SysEx/channel messages, and the
Phase 4 note schedule rounded to MIDI ticks. The default host pairs it with the existing
`audio-playback-fixture.wav`; the WAV remains unchanged.

The host accepts at most one visual provider:

```sh
./build/app/nenia --audio song.ogg --midi song.mid
./build/app/nenia --audio song.ogg --timeline song.json
./build/app/nenia --audio song.wav
```

`--timeline` and `--midi` together are rejected. No same-basename sibling
discovery is performed. Invalid MIDI reports a useful error, leaves the audio
path usable, and leaves the visual provider unset/neutral. MIDI is never
synthesized by Nenia and no external MIDI library or physical MIDI device is
required.
