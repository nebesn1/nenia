# Adaptive layout

Phase 10 removes the canonical 640×400 `QImage` presentation path. The
renderer now receives the actual widget-sized surface, computes integer
geometry from that viewport, and paints directly into it. `NeniaWidget`
blits the surface at `(0,0)` with no painter transform, source/destination
scaling, aspect lock, or outer letterbox.

## Why Phase 9 clipped small text

The Phase 9 widget first rendered a fixed 640×400 image and then used a
centered `QRect` to draw that image into the host. At non-integer effective
scales, the final source/destination rectangles rounded different edges of
the raster. The root cause was fractional whole-scene raster presentation, not
insufficient label padding. F2 no longer applies that pixel-oriented text
policy: the primary UI face is readable Source Code Pro, text antialiasing is
enabled, and artwork uses smooth resampling.

## Size hints and columns

The widget reports:

```text
minimumSizeHint() = 840×480
sizeHint()        = 960×600
```

The layout has three inset regions inside an intentional dark `contentRect`:

```text
PLAYLIST | CHANNEL MATRIX | RIGHT INFORMATION / VISUALIZATION
```

Playlist is capped around 23% while retaining a 200 px minimum. The right
column retains a 280 px minimum. The Matrix receives the remaining width and
has a 320 px minimum at supported sizes, restoring a miniature-keyboard width
at least comparable to the accepted pre-Phase-9 geometry. When width is tight,
the Playlist and right column yield before the Matrix keyboard.

The adaptive outer margin is 6 px at Normal density, 8 px at Large density,
and 10 px at ExtraLarge density. Every main column stays within `contentRect`;
the outer band is background padding, not scaled-canvas letterboxing. The
existing 840×480 minimum remains: the Playlist remains usable, the Matrix is
at least 320 px wide, the artwork viewport remains square, and seven Media
Info rows fit without overlapping Spectrum.

The right column allocates a square artwork viewport, seven Media Info rows,
the spectrum, and the ten-channel activity panel from the available height.
Artwork has a frame rectangle and a smaller square `artworkContentRect`; both
real images and the NO IMAGE placeholder are clipped to that content square.
The artwork cache is invalidated when the widget size changes and is rebuilt
for the new content target. Spectrum bars remain 32 FFT bands; their integer
bar width, segment height, and gaps are computed from the current plot.
Activity bars, knobs, and PRG/KEY rows use the available panel height rather
than a small fixed block, preserving ACT, PAN, PRG, KEY, and pointer semantics.

Phase 10 F7 gives the top section a shared content-height envelope. The left
`artworkControlsColumn` and right `mediaInfoColumn` have identical top and
bottom coordinates at all supported sizes. The left stack contains the square
Artwork viewport, a density-specific gap, and a five-button transport strip;
the right column retains all seven primary or extended Media Info rows. The
Artwork square is constrained by both the remaining vertical space and the
left-column width, so it never stretches to make the columns match. The strip
matches the Artwork viewport's left/right edges, and its five button rectangles
are drawn and hit-tested from one layout calculation.

Transport dimensions are integer metrics by density:

| Density | Button height | Artwork-to-strip gap | Inter-button gap | Icon size |
| --- | ---: | ---: | ---: | ---: |
| Normal | 24 px | 6 px | 2 px | 12 px |
| Large | 28 px | 8 px | 2 px | 14 px |
| ExtraLarge | 32 px | 10 px | 3 px | 16 px |

The existing 840×480 minimum remains. At that size, the five buttons remain
separate and wide enough for their compact geometric icons; no minimum-size
increase was needed. The old Playlist MODE footer allocation is removed, and
the Playlist viewport now extends to the normal lower padding rather than
reserving an invisible mode cell.

The Channel Matrix has one outer Nenia panel frame, an inset `CHANNEL MATRIX`
title row, and ten individually framed channel rows below it. The header has
its own integer height; the row pitch is recomputed from the remaining panel
height, so CH01–CH10 stay inside the border without reducing the accepted text
sizes or changing the shared white/black-key geometry.

The complete `rightColumn` allocation has one square-corner outer frame,
inset one pixel and matching the Channel Matrix and Playlist normal frame tone
`#3D6F8C` (the axis color). Its shared `rightColumnInnerRect` applies a further
3/4/5 px padding for Normal/Large/ExtraLarge density, respectively. ARTWORK /
MEDIA INFO, SPECTRUM / LIVE FFT, and MIDI / CHANNEL ACTIVITY all derive their
horizontal bounds from that shared rectangle; Media Info, Spectrum, and
Activity content right edges align. The sections share one background and have
no individual outer fills or frames. Their titles and vertical spacing
establish the sections. Internal surfaces remain: the square artwork viewport,
framed Media Info rows, dark FFT plot and grid, and Activity bars, knobs, and
table lines. The Playlist and Channel Matrix keep their outer frames; the
Matrix also retains its ten individually framed rows. The former standalone
right-column separator remains removed. The active Playlist highlight remains
cyan and retains its existing three-second behavior.

Primary-container titles share the Matrix title's frame-relative inset policy.
The left inset is 7/9/11 px for Normal/Large/ExtraLarge density and the top
inset is 2 px. ARTWORK and MEDIA INFO use that same first-row top alignment;
MEDIA INFO keeps its two-column x position. SPECTRUM / LIVE FFT and MIDI /
CHANNEL ACTIVITY use the shared title left inset while retaining their section
vertical positions. These title insets are separate from the smaller shared
Right Column content inset, so F6 F5's expanded content geometry is unchanged.
The PAN indicator is one flat-capped radial line from the knob center to its
outline. Its direction continues to use the established CC10 mapping, while
its length follows the actual knob radius at each density.

The Playlist row height is selected by a bounded integer visual-density tier.
Larger windows still expose more rows rather than enlarging the same fixed row
count. Matrix rows derive their integer row pitch from the actual height left
after the Matrix header and padding. Each row then derives its keyboard top
and keyboard height from that row rectangle, so small, medium, and maximized
windows visibly grow the keyboard vertically as well as horizontally. White
and black keys use the same local keyboard rectangle; black-key height remains
the 3/5 proportion of the inner white-key height.

## Typography

Source Code Pro is the bundled readable primary font, with integer sizes
selected by `NeniaVisualMetrics`. The bundled Fusion Pixel zh-Hans face is an
explicit fallback only for missing CJK glyphs:

```text
Normal:      body 12px, title 13px, small 10px
Large:       body 13px, title 14px, small 11px
ExtraLarge:  body 14px, title 15px, small 12px
```

The tier is selected from viewport dimensions; there is no continuous font
scaling and no painter transform. Every static text draw uses
`QFontMetrics::elidedText` against the current integer cell width and vertical
alignment. Primitive geometry remains crisp; text antialiasing is enabled for
readability and smooth pixmap transforms are enabled for artwork.

The Matrix keyboard uses one local geometry calculation. White-key boundaries
are integer-partitioned across the actual keyboard width, and every black key
is placed around the corresponding white-boundary coordinate. Active overlays
reuse those exact key rectangles, so resizing cannot move C# between the
wrong white keys or detach an active highlight.

## Regression sizes

The Phase 10 tests render layout snapshots at:

```text
840×480   960×600   1280×720   1648×830
900×500   1200×700   1600×600
```

They verify that the three columns stay inside the calculated outer margin,
do not overlap, the Matrix stays above its usable width, all seven Media Info
value rectangles fit inside the top-right panel, artwork remains square, and
the right-side rectangles remain usable. Phase 10 F7 additionally checks
equal left/right content heights, the exact Artwork-to-transport gap, all five
button rectangles and ordering, square artwork at 840×480, 960×600, 1280×720,
1648×1000 and 1600×600, plus resized mouse hit testing. Phase 10 F1 additionally verifies
the Normal/Large/ExtraLarge metric progression, white/black key topology at
320/400/520/700px keyboard widths, active-note geometry at multiple sizes,
placeholder clipping and edge intersections, real-image containment, and the
wide 1600×600 viewport. Phase 10 F2 additionally verifies keyboard height
growth and vertical white/black scaling, Media Info hold/scroll/loop/clipping,
Activity 1.0/0.0 marker gutters, native-resolution artwork decoding, smooth
sampling, and placeholder containment. Tests inspect metric headroom and
elision behavior rather than relying on fragile whole-window golden images.

## Media Info marquee

`TITLE`, `ARTIST`, `ALBUM`, `LOCATION`, `FORMAT`, and `POSITION` value fields
enter marquee mode only when their rendered Source Code Pro value exceeds the
current value rectangle. Extended-page `COMPOSER`, `ALBUM ARTIST`, `PERFORMER`,
and `GENRE` use the same behavior; TRACK, DATE, and DISC are eligible too. The
first copy begins at the left edge and holds for 900 ms, then
two repeated copies move right-to-left at 42 px/s with one full value-field
width of spacing. The next copy therefore enters from the right instead of
teleporting to the left. Values are painted under a clip rectangle, so they
cannot overwrite labels, adjacent rows, or the artwork column. A small 33 ms
widget timer runs only while the visible page has an overflowing field and
the widget is visible; it is independent of the audio clock, so paused media
still scrolls. Fitting values remain static, and PLAYBACK remains static.
Switching pages resets newly visible values to their initial hold rather than
resuming an off-page marquee phase.

## Activity scale and artwork quality

The Activity gutter reserves a scale rectangle to the left of CH01. Its top
and bottom labels are `1.0` and `0.0`, respectively, because ACT is a
normalized note-intensity value rather than an FFT dB measurement. Compact
top/middle/bottom ticks anchor the ten bars without colliding with the first
channel column.

Artwork keeps the original decoded `QImage` as the cache source. Ordinary
files are decoded at native resolution; only pathological images above the
8,192 px longest-side safety bound are bounded. The widget creates one
viewport-sized cache with `Qt::SmoothTransformation`, invalidating it on
source or viewport changes. The renderer aspect-fits that cache inside the
clipped square `artworkContentRect`; the NO IMAGE scaffold uses the same clip.
