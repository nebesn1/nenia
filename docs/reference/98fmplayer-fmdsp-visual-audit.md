# 98fmplayer / FMDSP visual reference audit

Status: Phase 1A audit complete; documentation only
Audit date: 2026-10-01
Reference repository: [myon98/98fmplayer](https://github.com/myon98/98fmplayer)
Pinned upstream snapshot: 'master' at '4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a'
Pinned commit: 'Merge pull request #14 from mistydemeo/osx_fixes' (2024-07-23)
Latest upstream release tag observed: 'v0.1.14' at 'a4b29dd9abf182b74170372a0c3e62188c4a158c' (2018-04-15)

This document records the visual facts that Nenia may use as a reference for a
later, original Qt implementation. It is not a porting plan for upstream C
code and it does not reproduce source code, sprite arrays, font data, logos,
screenshots, or other upstream artwork.

## 1. Scope and method

The audit covers the renderer and the three host paths that reveal its display
contract:

- 'fmdsp/fmdsp.h', 'fmdsp/fmdsp.c', and 'fmdsp/fmdsp_sprites.h'
- 'fmdsp/fmdsp-pacc.c' and 'fmdsp/fmdsp-pacc.h'
- 'fmdsp/fmdsp_gl.c'
- 'fmdsp/font.h', 'fmdsp/font_fmdsp_small.c', 'fmdsp/font_rom.c', and the
  platform font loaders
- 'fft/fft.h'
- 'sdl/main.c', 'gtk/main.c', 'win32/main.c', 'pacc/pacc-gl.c', and
  'pacc/pacc-d3d9.c'
- the upstream 'LICENSE'

The measurements below come from the pinned snapshot. The source links use
that immutable commit so that later upstream changes do not silently alter the
reference. The post-'v0.1.14' visual-path diff was also checked: the only
change under 'fmdsp/' is a forward-declaration cleanup in
'fmdsp-pacc.h'; the other post-release changes in the inspected path are
host/SDL/platform build changes, not new FMDSP layout constants.

Useful source anchors:

- [fmdsp.h](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/fmdsp/fmdsp.h)
  — logical dimensions, palette count, styles, and runtime state
- [fmdsp.c](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/fmdsp/fmdsp.c)
  — VRAM drawing, initialization, static panels, and dynamic updates
- [fmdsp_sprites.h](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/fmdsp/fmdsp_sprites.h)
  — coordinate constants, palette definitions, and sprite dimensions
- [fmdsp-pacc.c](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/fmdsp/fmdsp-pacc.c)
  — the parallel PACC renderer, useful for separating static geometry from
  runtime updates
- [fmdsp_gl.c](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/fmdsp/fmdsp_gl.c)
  — 640×400 texture upload and nearest-neighbor filtering
- [LICENSE](https://github.com/myon98/98fmplayer/blob/4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a/LICENSE)
  — upstream BSD 2-Clause license

Line references below are relative to the pinned commit
'4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a'. The most useful ranges are:

| Upstream file | Symbols/range used in this audit |
| --- | --- |
| 'fmdsp/fmdsp.h' | lines 16–52: dimensions, palette/level counts, style enum, runtime state |
| 'fmdsp/fmdsp.c' | lines 14–44: indexed VRAM blit; 58–123: grid-aligned text; 125–277: track tables and row initialization; 278–490: static panels and bottom area; 644–1205: track, FFT, level, and status updates |
| 'fmdsp/fmdsp_sprites.h' | lines 1–190: geometry constants; 192–333: palettes; 533–608: keyboard masks |
| 'fmdsp/font.h' and font sources | 'font.h' lines 52–73; 'font_fmdsp_small.c' lines 6–28; 'font_rom.c' lines 3–23 |
| 'fmdsp/fmdsp_gl.c' | lines 99–120: 640×400 texture and 'GL_NEAREST'; lines 165–175: full-surface upload/draw |
| 'fmdsp/fmdsp-pacc.c' | lines 1361–1498: static spectrum/level geometry; lines 1625–1800: dynamic spectrum/level portions |
| 'sdl/main.c', 'gtk/main.c', 'win32/main.c' | SDL lines 171–187; GTK lines 389–455 and 551–568; Win32 lines 295–315 and 432–437 |
| 'pacc/pacc-gl.c' and 'pacc/pacc-d3d9.c' | GL lines 459–475 and 513–514; D3D9 lines 618–622 |

## 2. Executive findings

The stable reference contract is a **640×400 logical, indexed-pixel
framebuffer** with ten palette entries and integer-coordinate drawing. The
reference host paths present it at integer scale and preserve its 8:5 aspect
ratio.

The most useful Nenia boundary is:

1. Phase 1B: draw an original, static 640×400 FMDSP-inspired panel.
2. Phase 2: add an explicit visual-state model for highlighted notes, meters,
   peaks, transport state, and timers.
3. Later provider phases: supply JSON, audio/FFT, and MIDI data.

The upstream visual density is much higher than Nenia needs to reproduce
literally. Nenia should preserve the geometry discipline, pixel-grid feel,
track/keyboard relationship, spectrum/level grouping, and restrained palette
model while keeping its labels, glyphs, artwork, and component API original.

## 3. Logical canvas and coordinate system

'fmdsp.h' defines:

- 'PC98_W = 640'
- 'PC98_H = 400'
- 'FMDSP_PALETTE_COLORS = 10'
- 'FMDSP_LEVEL_COUNT = 19'

Coordinates are integer pixel coordinates with origin '(0, 0)' at the upper
left. The low-level blitter writes indexed pixels directly to
'vram[y * 640 + x]'; there is no vector scene graph or subpixel layout.

The OpenGL presentation keeps that indexed representation until the host
boundary: 'fmdsp_gl.c' uploads the 640×400 VRAM as a luminance texture and
uploads the ten-entry RGB palette as a 10×1 texture; its fragment shader
looks up the final RGB value. The logical framebuffer therefore remains
640×400 regardless of host scaling.

The following map is an original schematic of the measured regions. It is not
a copied screenshot or artwork:

~~~text
640 × 400 logical pixels

 y=0  +-------------------------------+--------------------------------+
      | 10 track rows, x=0..319       | ORIGINAL diagnostics /          |
      | or 13 short rows               | non-ORIGINAL track details      |
      |                                | header, time, spectrum, levels |
 y=320+-------------------------------+--------------------------------+
      | empty/transition pixels       |                                |
 y=324+----------------------------------------------------------------+
      | PLAYING / file / PCM status bars, through y=332              |
 y=335+----------------------------------------------------------------+
      | checker/comment area, y=335..399                             |
 y=400+----------------------------------------------------------------+
~~~

The separator at 'x=320' is exact for the non-'ORIGINAL' ten-row layout. The
'ORIGINAL' right panel begins at 'x=312', so its logo and diagnostics visually
use the narrow edge near the track column; 'x=312' should not be treated as a
universal clean split.

### Region measurements

| Region | Measured/reference geometry | Exactness |
| --- | --- | --- |
| Main track column, ten-row styles | 'x=0, y=0, w=320, h=320'; ten rows × '32' px | Directly from 'TRACK_H' and update loops |
| Main track column, 13-row style | 'x=0, y=0, w=320, h=312' of logical rows; 13 rows × '24' px, with an 8 px trailing clear area | Derived from 'TRACK_H_S' and 13-row loop |
| Non-'ORIGINAL' right track-detail area | 'x=320..639, y=0..319' | Derived from the right-panel clear and per-row draws |
| 'ORIGINAL' diagnostics | Mostly 'x=312..639, y=0..319' | Derived from sprite anchors and draw calls |
| 'DEFAULT'/'OPN'/'PPZ8' right details | 'x=320..639, y=0..319' | Derived from the non-'ORIGINAL' clear and per-row draws |
| Playing strip | 'x=0, y=324, w=72, h=9' for the playing indicator; the file/PCM strips share the same horizontal band | Direct constants |
| Lower status/checker area | 'y=335..399', nominal height '65' | Direct 'COMMENT_Y'/height calculation |
| Spectrum plot | bars begin at 'x=352', baseline 'y=207', top 'y=145', 70 bins | Direct constants and FFT update |
| Level meters | first column 'x=337', top 'y=227', 19 columns, pitch '16', visible width '14', height '64' | Direct constants |

## 4. Display styles

The public style enum contains:

- 'FMDSP_DISPSTYLE_ORIGINAL'
- 'FMDSP_DISPSTYLE_DEFAULT'
- 'FMDSP_DISPSTYLE_OPN'
- 'FMDSP_DISPSTYLE_PPZ8'
- 'FMDSP_DISPSTYLE_13'
- a count sentinel

The styles share the same 640×400 surface but not the same information
density:

| Style | Track rows | Track naming/intent | Right side |
| --- | ---: | --- | --- |
| 'ORIGINAL' | 10 | FM1–FM6, SSG1–SSG3, ADPCM | Dense selector/status panel with spectrum and levels |
| 'DEFAULT' | 10 | Default FM/SSG/ADPCM grouping | Track-detail rendering on the right |
| 'OPN' | 10 | FM3 plus FM3 extended channels, SSG, ADPCM | Track-detail rendering on the right |
| 'PPZ8' | 10 | PPZ8 channels plus ADPCM | Track-detail rendering on the right |
| '13' | 13 | Compact short rows | Compact 13-row presentation |

The default ten-row track pitch is '32' pixels. The 13-row presentation uses a
'24'-pixel pitch and a short nine-pixel keyboard ('KEY_H_S = 9') instead of
the normal 17-pixel keyboard.

Nenia recommendation: select one canonical Nenia layout for Phase 1B. Keep
the upstream style taxonomy as a design reference, not as a requirement to
support all five styles in the first renderer.

For the classic FMDSP-style inspiration, 'ORIGINAL' is the closest reference:
it combines the ten-row track display with the dense title/status, spectrum,
and level regions. Nenia should borrow that information architecture only in
an abbreviated, original form.

## 5. Track rows and keyboard relationship

Each standard row is a compact horizontal instrument strip. Its measured
anchors are:

- type/name label near 'x=1', with a nominal '26×5' label region
- track number sprite at 'x=31', '8×11'
- detail text starts at 'x=67'
- note/key value region around 'x=80'
- volume, gate, detune, and mode fields continue through approximately 'x=257'
- keyboard starts at 'x=7, y=14'
- level/volume bar starts around 'x=66'; its narrow meter is '2×4' with a
  64-step logical count

The keyboard is assembled from indexed pixel masks rather than drawn as a
generic vector piano:

- left cap: '6×17' at the row's left edge
- one-pixel separation, then eight octave tiles
- each octave tile: '35×17'
- right cap: '11×17'
- full keyboard extent: 'x=0..297', 298 pixels
- eight octaves × twelve note positions = 96 conceptual note slots
- short-row keyboard height: 9 pixels

The key value is split into an octave nibble and a note nibble. Notes '0..11'
map to 'C, C#, D, D#, E, F, F#, G, G#, A, A#, B' in the row labels; the
special '0xf' value is a rest/return state rather than a note slot.

There are two independent highlight concepts:

- 'actual_key': the current/actually sounding key
- 'key': the nominal or requested key

When both are available the renderer can show both states. The actual state
uses the bright/current palette role; the nominal state uses the normal active
role, or the current role when the track is masked. Inactive keys retain the
keyboard background. This is an important Phase 2 state-model distinction,
but Phase 1B only needs the unhighlighted keyboard geometry.

Nenia should keep the relationship “track metadata above a compact piano strip”
and the two-key-state idea, while using Nenia-owned glyphs and a new mask
design. Do not copy 's_key_bg', 's_key_left', 's_key_right', or 's_key_mask'
data.

## 6. Typography and font constraints

The source uses bitmap-oriented text:

- FMDSP small font: half-width '5' pixels, height '6' pixels
- FMDSP medium font: half-width '6' pixels, height '8' pixels
- ROM-style font: half-width '8' pixels, height '16' pixels
- full-width characters occupy paired half-width cells
- text is placed on integer x/y positions
- Shift-JIS/CP932-style byte handling is part of the upstream text path
- the drawing code does not rely on antialiasing

The font loader can use a ROM file or a bundled fallback on supported host
paths. Windows also has a platform-specific non-antialiased fixed-pitch font
path. Those are upstream implementation details, not Nenia dependencies.

Nenia recommendation:

- Do not copy or bundle the upstream ROM, Shinonome data, or FMDSP font
  arrays.
- For a Qt prototype, use an independently redistributable pixel font only if
  its license is compatible and its metrics are controlled explicitly.
- Render with integer placement, integer baseline metrics, and antialiasing
  disabled. If an external font cannot provide stable 5/6/8-pixel metrics,
  prefer a small Nenia-owned bitmap glyph table in a later implementation.
- Phase 1A does not select or download a font.

### Pixel and line language

The renderer's visual grammar is deliberately discrete:

- one-pixel rules and tick marks are common; bars are often 2–3 pixels wide
- rectangles, filled masks, and square corners dominate; there is no
  subpixel geometry or soft rounded-card treatment
- separators and axes use black negative space, short dashes, and repeated
  fixed intervals instead of antialiased vector strokes
- labels sit on fixed pixel cells and are aligned by integer x/y anchors
- active/inactive contrast comes from palette-index changes and filled areas,
  not gradients or opacity effects

This can be reproduced with QPainter into an off-screen QImage, provided
antialiasing is disabled and all logical coordinates remain integer-valued.

## 7. Palette model

The upstream framebuffer stores palette indices, not per-draw arbitrary
colors. There are ten palettes, each with ten RGB entries. The semantic
roles below are inferred from where the indices are used; the upstream source
does not name the roles as a formal enum.

| Index | Observed role in the renderer |
| ---: | --- |
| 0 | Background/black and cleared pixels |
| 1 | Dim axis, labels, and inactive structural detail |
| 2 | Active/primary bars, text, and live meter fill |
| 3 | Inactive/dim meter and spectrum fill |
| 4 | Keyboard body and light structural surface |
| 5 | Keyboard accents, panpot/background contrast |
| 6 | Nominal active key and secondary emphasis |
| 7 | Peak hold, bright emphasis, and high-contrast markers |
| 8 | Actual/current key highlight |
| 9 | Palette-specific secondary/reserved emphasis used less often by the core layout |

The ten RGB palettes in the source are:

| Palette | RGB entries 0 through 9 |
| ---: | --- |
| 0 | '(0,0,0)', '(170,170,153)', '(102,136,255)', '(68,68,119)', '(204,204,187)', '(102,102,85)', '(136,255,68)', '(51,51,238)', '(0,187,255)', '(68,102,170)' |
| 1 | '(0,0,0)', '(187,187,170)', '(136,170,255)', '(85,85,153)', '(204,204,187)', '(136,136,119)', '(153,255,119)', '(102,85,255)', '(0,204,255)', '(85,119,170)' |
| 2 | '(0,0,0)', '(255,102,0)', '(255,170,0)', '(102,68,51)', '(153,136,102)', '(119,85,68)', '(255,221,85)', '(255,102,0)', '(255,85,0)', '(170,119,85)' |
| 3 | '(0,0,0)', '(170,170,153)', '(170,153,255)', '(85,51,102)', '(204,204,187)', '(102,102,85)', '(119,255,34)', '(136,68,221)', '(0,187,255)', '(136,102,187)' |
| 4 | '(0,0,0)', '(187,187,170)', '(102,153,255)', '(85,68,136)', '(255,255,238)', '(119,119,102)', '(255,68,0)', '(85,85,255)', '(255,119,255)', '(119,119,187)' |
| 5 | '(0,0,0)', '(255,51,119)', '(255,187,0)', '(85,85,68)', '(119,119,102)', '(102,102,102)', '(255,221,0)', '(255,0,51)', '(255,0,51)', '(170,136,0)' |
| 6 | '(102,170,238)', '(0,17,136)', '(0,51,136)', '(153,221,255)', '(153,221,255)', '(119,187,255)', '(0,51,136)', '(34,102,187)', '(0,85,204)', '(68,136,255)' |
| 7 | '(0,0,0)', '(170,170,136)', '(102,153,255)', '(17,0,51)', '(170,170,153)', '(51,51,51)', '(85,255,68)', '(34,17,255)', '(0,170,255)', '(68,85,170)' |
| 8 | LCD-dark interpolation at inputs '255, 72, 0, 182, 0, 145, 218, 109, 218, 0' |
| 9 | LCD-bright interpolation at inputs '255, 72, 0, 182, 145, 145, 0, 109, 0, 0' |

Palettes 8 and 9 interpolate between the source's dark LCD green
'(12,91,0)' and bright LCD green '(172,239,7)'. They are formula-derived in
the source rather than written as literal triples. Palette fades move each RGB
component toward the target by a step of 16 per update.

The literal palette data is in 'fmdsp_sprites.h' lines 212–333; palette
selection/fade behavior is in 'fmdsp.c' lines 492–523.

Nenia recommendation: preserve the indexed-role idea in a small Nenia palette
enum ('Background', 'Dim', 'Primary', 'Inactive', 'Surface', 'Accent',
'Nominal', 'Peak', 'Actual', 'Secondary') and let themes map those roles to
colors. This keeps the renderer deterministic without making the Nenia UI a
literal palette copy.

## 8. Spectrum analyzer

The spectrum panel is a 70-bin, low-resolution display:

- 'FFTDISPLEN = 70'
- bar x origin '352'
- each bar is 3 pixels wide with a 1-pixel gap, so the last bar ends at
  approximately 'x=630'
- plot baseline 'y=207'
- plot top 'y=145'
- visual data range is 0–31 steps; each step is 2 pixels high
- live bars use the active/live role
- empty or dim portions use the inactive role
- peak hold is a one-pixel bright marker that decays separately

Static frame elements:

- title 'SPECTRUM ANALYZER'
- vertical dB axis and short tick marks
- frequency labels around 250, 500, 1k, 2k, and 4k
- lower baseline/tick segments

Dynamic elements:

- live FFT bar heights
- peak-hold markers and decay

The exact source geometry is clearer in the PACC static/dynamic implementation:
the plot bars are filled from 'x=352', with live y calculated from the 32-step
range and the peak marker measured upward from 'y=207'. Phase 1B should draw
the frame, axis, labels, and empty bars only; FFT input and peak decay belong
to later phases. The static frame is implemented around 'fmdsp-pacc.c' lines
1361–1434; dynamic bar/peak updates are around lines 1625–1659.

## 9. Level meters

The level panel contains 19 narrow columns:

1. FM1–FM6
2. SSG1–SSG3
3. one rhythm aggregate
4. one ADPCM column
5. PPZ8-1–PPZ8-8

Measured geometry:

- meter origin 'x=337, y=227'
- visible meter width '14'
- column pitch '16'
- meter height '64'
- 19 columns span to the right edge at approximately 'x=639'
- panpot area begins at 'y=291'
- program labels begin at 'y=306'
- key labels begin at 'y=313'
- the panel heading/field labels begin around 'x=318, y=290'

Static elements include the group labels, meter outlines/grid, 0/negative-dB
scale marks, 'ON', 'PAN', 'PROG', and 'KEY' field labels. Dynamic elements
include the vertical level fill, peak hold, panpot state, program number, and
key value. Phase 1B should draw the empty grid and labels with deterministic
placeholder values; no real channel state is needed yet.

The static level frame is around 'fmdsp-pacc.c' lines 1435–1498; live fills,
peaks, panpots, and value fields are updated in the corresponding dynamic
section around lines 1660–1800.

## 10. Status, transport, and bottom information

The dense 'ORIGINAL' panel is more than a visualizer. Its static information
architecture contains:

- a small logo block at 'x=312, y=1', total measured width about 82 and height
  12
- the 'MUSIC FILE SELECTOR & STATUS DISPLAY' header region around 'y=1..15'
- driver/version information around 'y=27'
- title and file-selector strips
- time fields at 'y=22, 41, 60, 79, 98, 117'
- play/stop/pause/fade controls around 'y=77'
- fast-forward/rewind/floppy controls around 'y=87'
- a 31×31 circular animation area at 'x=312, y=70'
- CPU power count and FPS fields around 'y=115'
- the spectrum panel below/alongside the header
- the level panel below the spectrum

Important source labels include 'TRACK', 'PASSED TIME', 'CLOCK COUNT',
'TIMERB CYCLE', 'LOOP COUNT', 'VOLUME DOWN', 'PGM NUMBER', 'CPU POWER COUNT',
'FRAMES PER SECOND', and 'SPECTRUM ANALYZER'. Their presence explains the
information density; it does not make emulator diagnostics a Nenia
requirement.

Static versus runtime status:

| Area | Static frame content | Runtime content |
| --- | --- | --- |
| Header | labels, panel divisions, version label shape | title/file strings |
| Time panel | field names and separators | elapsed time, clock count, timer B, loop count, volume-down, program number |
| Transport | control labels/icons and layout | active play/stop/pause/fade state, circle animation, timer/loop indicators |
| CPU/FPS | field labels and bars | CPU count and FPS values |
| Bottom status | playing/file/PCM labels, arrows, checker pattern | current file names, comments, scroll position |

Nenia recommendation: retain a compact transport/status concept, but do not
make upstream CPU count, driver internals, floppy imagery, or exact logo
identity part of the Nenia product surface. Those are reference-specific
content, not necessary renderer primitives.

## 11. Scaling, filtering, and aspect behavior

The reference host paths establish a simple presentation contract:

- OpenGL uploads a 640×400 VRAM texture.
- The OpenGL path explicitly uses 'GL_NEAREST' for texture minification and
  magnification.
- SDL starts at 1× and cycles through 1×, 2×, and 3×; its window dimensions
  are the logical dimensions multiplied by that integer scale.
- GTK has a 1×/2× presentation path and marks the main window non-resizable.
  Its active path is OpenGL; an older Cairo path also specifies nearest
  filtering.
- Win32 presents a 1×/2× child surface with fixed client/window dimensions.
  The Direct3D path does not provide the same explicit nearest-filter setting
  in the inspected source, so filtering there should not be inferred beyond
  the fixed-size presentation contract.
- The inspected SDL/GTK/Win32 host code does not implement arbitrary resize
  distortion or a general letterbox policy.

The logical aspect ratio is '640:400 = 1.6:1'.

Nenia recommendation:

1. Render into a canonical 640×400 'QImage' or equivalent indexed/opaque
   surface.
2. Keep all source geometry and text placement in integer logical pixels.
3. Scale the final surface with nearest-neighbor filtering.
4. Preserve the 1.6:1 aspect ratio and center it in the widget with black
   letterbox/pillarbox space when the widget is not an exact multiple.
5. Prefer an integer scale when available, but allow a mathematically
   correct fractional presentation scale only at the outer widget boundary.
6. Avoid redrawing the whole widget through an antialiased vector scene.

For future AVG integration, keep this logical renderer independent of the
windowing backend: expose the finished 640×400 image or a small paint target
boundary, and let the host decide how to composite it. This preserves the
same crisp surface whether the consumer is a QWidget, a scene, or an AVG
screen.

The canonical surface is small: 640×400 is 256,000 pixels. A full-surface
60 Hz redraw is about 15.36 million logical pixel writes per second before
the final scale. That is a modest workload for a QWidget/QImage renderer on
the target Intel UHD 620-class machine; no dedicated GPU renderer is required
for the Phase 1B static panel. This is a reasoned workload estimate, not a
benchmark.

## 12. Static/dynamic implementation matrix

This matrix is the proposed Nenia sequencing boundary, not an assertion that
the upstream renderer has the same phase model.

| Component | Static geometry/content | Dynamic state | Nenia phase |
| --- | --- | --- | --- |
| Track rows | row pitch, labels, field positions, bars, keyboard background | note highlights, note text, volume/gate/detune/mode, masks | 1B / 2 |
| Keyboard | key masks, octave layout, caps, row clipping | actual versus nominal key highlights | 1B / 2 |
| Header | panel title, separators, field labels | file/title values | 1B / later provider |
| Spectrum | plot frame, axis, ticks, frequency labels, empty bars | FFT heights and peak decay | 1B / 6 |
| Level meters | columns, groups, scale, field labels, empty grid | levels, peak holds, panpots, program/key values | 1B / 2 and 6 |
| Transport | control layout and inactive glyphs | play/stop/pause/fade, timer strip, circle animation | 1B / 2 |
| Bottom strip | checker pattern, labels, separators | comments, filenames, scroll state | 1B / later provider |
| Palette | role mapping and theme colors | palette fade/transition if desired | 1B / 2 |

The key architectural observation is that “static renderer” does not mean
“hard-code one screenshot.” It means define stable geometry and draw neutral,
deterministic states without coupling the canvas to audio, MIDI, JSON, or
timers.

## 13. What Nenia can reimplement faithfully

The following are general, measurable design facts and are safe as visual
reference material:

- a 640×400 logical canvas and 1.6:1 presentation ratio
- integer pixel coordinates and nearest-neighbor presentation
- a compact row-plus-keyboard relationship
- eight-octave, 12-note-per-octave keyboard organization
- separate nominal and actual note states
- a 70-bin low-resolution spectrum arrangement
- 19 narrow level columns grouped by instrument family
- static/dynamic separation between frames and live values
- role-based palette organization and palette fading as a concept
- a small, information-dense status/transport panel

These should be expressed in Nenia-owned types, geometry, glyphs, and theme
data.

## 14. What should remain Nenia-original

Do not copy or vendor the following from upstream:

- C/C++ source or line-for-line algorithms
- 's_key_bg', 's_key_left', 's_key_right', 's_key_mask', or any other sprite
  array
- bundled bitmap font data, ROM font data, or Shinonome data
- the logo, exact control glyphs, exact panel artwork, or screenshots
- upstream source filenames/identifiers as Nenia API names
- upstream driver-specific labels and branding as product identity

The Nenia renderer should use its own layout implementation, glyph system,
artwork, naming, and palette theme. This Phase 1A audit adds no upstream
license file to Nenia because it adds no upstream code or asset.

## 15. License and provenance boundary

The upstream repository contains a BSD 2-Clause license attributed to
Takamichi Horikawa, 2016. The license is recorded here as provenance for the
reference repository; no upstream file has been copied into Nenia.

For this audit, the permitted working set is:

- inspect and measure the upstream implementation
- describe factual dimensions, behavior, and broad visual concepts
- independently reimplement those general concepts
- create Nenia-owned geometry, glyphs, artwork, and theme data

If a later Nenia change intentionally adapts upstream source or artwork, stop
and perform a separate license/attribution review before adding it. The
Phase 1A recommendation is a clean-room-style original implementation using
the measurements above, not a source or asset import.

## 16. Recommended Phase 1B scope

Phase 1B should be a static Qt renderer proof with this acceptance boundary:

- one canonical 640×400 logical surface
- one Nenia-original palette/theme
- one Nenia-original bitmap/pixel typography solution
- ten neutral track rows, or a consciously simplified subset with the same
  grid discipline
- unhighlighted keyboard geometry with eight octave groups
- static spectrum frame with empty bars
- static 19-column level frame with empty meters
- compact Nenia-original header, transport, and bottom status strips
- aspect-preserving nearest-neighbor widget presentation
- no audio input, FFT calculation, MIDI input, JSON parsing, wall-clock
  animation, or provider dependency

Phase 1B should explicitly defer:

- actual/nominal key animation
- meter movement and peak decay
- FFT bars and spectrum peak state
- transport timers and circle animation
- file/comment scrolling
- upstream logo/control artwork
- exact upstream font and sprite data

The proof should be judged against this audit for coordinate discipline,
readability, layout balance, and crisp presentation—not against pixel identity
with the upstream program.
