# Nenia JSON Timeline Format

FORMAT VERSION: 1

This document describes Nenia's version-1 timestamped event format. A timeline
contains visualization events only. It does not contain audio, artwork,
playback commands, metadata, localization, or spectrum data. Phase 4 uses the
JSON provider with an external audio clock, and Phase 5 adds a separate MIDI
provider implementing the same provider contract; neither provider owns
playback. Phase 6 supplies a separate decoded-PCM spectrum path; it does not
extend this JSON format. In Phase 4–6, `time_ms` values are compared directly
with `QMediaPlayer::position()` in integer milliseconds; the JSON provider
still does not own that clock.

## Root object

Every document must contain these fields:

```json
{
  "format": "nenia-timeline",
  "version": 1,
  "events": []
}
```

- `format` is the exact string `nenia-timeline`.
- `version` is the integral number `1`.
- `events` is an array, including an allowed empty array.

Unknown root members are ignored for version 1. Required members and all
recognized members retain strict type and range validation.

## Event objects

Every event requires a non-negative integral `time_ms` and a recognized
`type`. `time_ms` is an explicit integer number of milliseconds; floating-point
seconds are not accepted.

### Note-on

```json
{
  "time_ms": 0,
  "type": "note_on",
  "track": 0,
  "note": 60,
  "velocity": 1.0
}
```

Required fields:

- `track`: integer 0..9
- `note`: integer 0..127
- `velocity`: finite number 0.0..1.0

### Note-off

```json
{
  "time_ms": 500,
  "type": "note_off",
  "track": 0,
  "note": 60
}
```

Required fields:

- `track`: integer 0..9
- `note`: integer 0..127

### Meter

```json
{
  "time_ms": 250,
  "type": "meter",
  "meter": 0,
  "level": 0.8
}
```

Required fields:

- `meter`: integer 0..18
- `level`: finite number 0.0..1.0

There are no spectrum, artwork, playback, metadata, or arbitrary command
event types in version 1. Unknown event types are fatal errors.

Unknown extra members on recognized version-1 root and event objects are
ignored. They cannot override or supply required fields, and known fields
still must have the correct type.

## Validation and errors

Loading is strict. The parser rejects malformed JSON, non-object roots,
missing fields, wrong JSON types, unknown event types, unsupported versions,
negative or fractional integer-domain values, unsafe large JSON integers, and
normalized values outside 0.0..1.0. Qt JSON numbers are explicitly checked for
integrality before conversion; values such as `100.5`, `1.5`, and `60.2` are
not silently truncated.

Errors are returned through the optional `QString *errorMessage` argument and
identify the relevant location where practical. Examples include:

```text
root.version: unsupported version 2; supported version is 1
events[12].note must be an integer in range 0..127
events[3].velocity must be a finite number in range 0..1
events[4].type: unknown event type 'pitch_bend'
```

Ordinary malformed input does not throw or terminate the application.

## Normalization and ordering

The provider parses the complete document into a temporary vector and then
performs a stable ascending sort by `time_ms`. Input may be out of order. For
events with the same timestamp, original JSON array order is preserved exactly.
This is intentional: `note_off` followed by `note_on` has different semantics
from `note_on` followed by `note_off`.

If validation or sorting fails, the temporary data is discarded and any
previously loaded valid timeline remains active.

## Range queries

`JsonTimelineProvider::eventsBetween(fromExclusiveMs, toInclusiveMs)` returns
the read-only span of events satisfying:

```text
fromExclusiveMs < event.timeMs <= toInclusiveMs
```

The sorted vector is queried with binary search, so successive polling windows
do not scan the entire timeline. These adjacent calls do not duplicate a
boundary event:

```text
eventsBetween(-1, 100)  // includes time 0 and 100
eventsBetween(100, 200)  // includes 200, not 100
eventsBetween(200, 300)  // includes 300, not 200
```

The explicit `eventsBetween(-1, 0)` convention makes time-zero events
dispatchable. A query with an empty or reversed interval returns an empty span.
The span remains valid until the provider is loaded again or cleared.

The provider is not a forward-only cursor. Any chronological range can be
queried in any order, which makes it suitable for future forward seeks,
backward seeks, restarts, and timeline jumps. Phase 3 does not implement state
reconstruction after a seek.

## Loading and empty timelines

`JsonTimelineProvider::load(path, errorMessage)` reads a normal filesystem
path and does not require a `.json` suffix. `loadJson(bytes, errorMessage)` is
provided for resource-backed applications such as the standalone demo.

A successfully loaded `{ "events": [] }` document is a valid loaded empty
timeline: `isLoaded()` is true, `eventCount()` is zero, first/last times are
empty, and range queries return no events. `clear()` returns the provider to
the never-loaded state.

## Complete example

```json
{
  "format": "nenia-timeline",
  "version": 1,
  "events": [
    {
      "time_ms": 0,
      "type": "note_on",
      "track": 0,
      "note": 60,
      "velocity": 1.0
    },
    {
      "time_ms": 250,
      "type": "meter",
      "meter": 0,
      "level": 0.8
    },
    {
      "time_ms": 500,
      "type": "note_off",
      "track": 0,
      "note": 60
    }
  ]
}
```
