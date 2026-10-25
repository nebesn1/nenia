# Rich audio tag metadata

Phase 10 F5 supplements Qt Multimedia's metadata with TagLib's format-neutral
text-tag interface. The current/prepared local file is read once on a
different-source transition. This fills gaps such as track/disc totals, date,
and composer values that the active Qt Multimedia backend may not expose.

```text
Local file ─┬─> QMediaPlayer ─> QMediaMetaData ─┐
            └─> TagLib FileRef::properties() ──┴─> merge ─> presentation snapshot
Remote URL ───> QMediaPlayer / QMediaMetaData ────────┘
```

## Scope and ownership

TagLib is a required, private implementation dependency of the static Nenia
library. `NeniaTagMetadataReader` opens only local-file URLs and uses
`TagLib::FileRef::properties()` / `TagLib::PropertyMap`; no TagLib type crosses
the internal value snapshot into the widget or renderer. The reader disables
audio-property reads and does not call any write/save API.

Qt Multimedia remains authoritative for playback, position/duration, decoded
PCM and FFT, audio format, and embedded artwork. TagLib is not used for MIDI,
playlist parsing or indexing, artwork, or any high-frequency rendering or
timer path. A controller reads only its current/prepared source; there is no
eager playlist scan or shared/global metadata cache. A different source clears
the previous TagLib snapshot before reading; same-source reactivation keeps
the snapshot and does not reread. External tag edits are not watched while a
source remains loaded.

## Mapping and precedence

Explicit non-empty host values retain their existing precedence above all
automatic title, artist, and album values.

| Presentation field | Automatic precedence |
| --- | --- |
| TITLE | Qt `Title` → TagLib `TITLE` → local filename stem |
| ARTIST | Qt `ContributingArtist` → `LeadPerformer` → `AlbumArtist` → `Author` → TagLib `ARTIST` |
| ALBUM | Qt `AlbumTitle` → TagLib `ALBUM` |
| TRACK | TagLib `TRACKNUMBER` + optional `TRACKTOTAL` → Qt `TrackNumber` |
| DATE | TagLib `DATE` → Qt `Date` |
| COMPOSER | TagLib `COMPOSER` → Qt `Composer` |
| ALBUM ARTIST | Qt `AlbumArtist` → TagLib `ALBUMARTIST` |
| PERFORMER | Qt `LeadPerformer` → `ContributingArtist` → TagLib `PERFORMER` → sorted `PERFORMER:<role>` values |
| GENRE | Qt `Genre` → TagLib `GENRE` |
| DISC | TagLib `DISCNUMBER` + optional `DISCTOTAL` |

`LOCATION` continues to come from the current source URL/path. `PLAYBACK`,
`POSITION`, and `FORMAT` continue to come from the controller/decoded Qt
signals. Qt's non-empty values win only in the fields designated Qt-first;
empty later Qt metadata never erases a cached TagLib value.

TagLib date strings preserve source precision, such as `2002`, `2002-05`, or
`2002-05-17`. Track and disc values accept separate number/total properties or
a compound `number/total` value. Numeric leading zeroes are removed for the
display (`01/11` → `1/11`); a split total is not appended twice. A number with
no total remains just the number. Multi-value textual tags are trimmed,
case-insensitively deduplicated in source order, and joined with ` / `.
Performer-role keys never appear in the UI; generic `PERFORMER` wins, otherwise
role values are joined deterministically without duplicate names. Nenia does
not infer absent fields or rewrite capitalization/punctuation.

For the audited MP3, TagLib returns `CONDUCTOR=Lia` rather than a generic
`PERFORMER` key. Nenia does not relabel a conductor credit as a performer; Qt
provides `LeadPerformer=Lia` for this file, so the displayed PERFORMER remains
Lia through the documented Qt-first fallback.

## Failure behavior

Non-local URLs skip TagLib entirely and use Qt metadata. If a local file is
unsupported or malformed, the reader returns a diagnostic and the controller
logs one warning for that source; playback continues and values fall back to
Qt, then filename stem/`--` as applicable. Valid files with no tags are a
successful read with an empty tag snapshot. Metadata is synchronous and read
once per different current/prepared source; it is not read in painting,
playback polling, or playlist enumeration.

## Build, package, and provenance

The accepted build requires TagLib 2.0 or newer with its CMake config and
imported target `TagLib::tag`:

```cmake
find_package(TagLib 2.0 CONFIG REQUIRED)
target_link_libraries(nenia_lib PRIVATE TagLib::tag)
```

Because Nenia is static, its installed `NeniaConfig.cmake` calls
`find_dependency(TagLib 2.0 CONFIG REQUIRED)` before importing the exported target.
Downstream applications therefore need only the existing `find_package(Nenia
CONFIG REQUIRED)` and `Nenia::Nenia` link, in addition to their existing Qt
requirements. The build does not fetch or vendor TagLib and has no pkg-config
fallback in this configuration.

The audited openSUSE Leap 16 environment provides TagLib 2.0.2, `TagLib::tag`,
headers under `/usr/include/taglib`, and pkg-config metadata (`taglib`). The
development RPM is `libtag-devel-2.0.2-160000.2.2.x86_64`; its runtime RPM is
`libtag2-2.0.2-160000.2.2.x86_64`. Both RPMs report `LGPL-2.1-or-later`. The
upstream project describes LGPL/MPL licensing;
distributors must comply with at least one applicable upstream license. Nenia
does not declare itself under TagLib's license and includes no copied TagLib
source. See the [TagLib project and license notice](https://taglib.org/),
[PropertyMap API](https://taglib.org/api/classTagLib_1_1PropertyMap.html), and
[property mapping reference](https://taglib.org/api/p_propertymapping.html).

The private development reader tests use small, original, pre-tagged PCM WAV
fixtures in `tests/assets/`; they do not use user media. Tests and fixtures are
excluded from public source archives. Test and production code only read
tags—no TagLib tag-writing API is used.
