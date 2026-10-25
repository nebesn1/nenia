# Nenia typography foundation

Phase 10 F2 modernizes the visual layer's font choice without exposing a
public typography API. The production choice is an unmodified readable
monospaced font embedded in the `nenia_lib` static library, with the existing
open-source CJK font loaded as an explicit fallback only after a Qt GUI
application exists.

## Candidate audit

Candidates were compared for readability, retro character, compactness,
technical glyph distinction, UI behavior, file size, Latin/German coverage,
Simplified Chinese readiness, and redistribution terms. The audit included
[Source Code Pro](https://github.com/adobe-fonts/source-code-pro),
[DejaVu Sans Mono](https://dejavu-fonts.github.io/),
[Adwaita Sans/Mono](https://gitlab.gnome.org/GNOME/adwaita-fonts), the existing
[Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font), and
[WenQuanYi Zen Hei](https://github.com/deepin-community/fonts-wqy-zenhei).

The audit loaded the available candidates with Qt 6.12, measured
`QFontMetrics` and `QRawFont`, rendered ambiguity-pair masks with readable
antialiasing enabled, and exercised the native-resolution Nenia layout at
minimum, preferred, wide, and maximized-like sizes.

| Candidate | Size / license | Result |
| --- | --- | --- |
| Source Code Pro Regular | 132,740 bytes / OFL-1.1 | Readable compact mono, strong UI/technical glyph distinction, Latin and German coverage; selected primary; CJK supplied by explicit fallback |
| DejaVu Sans Mono | 340,712 bytes / Bitstream Vera terms | Broad readable Latin, but less compact and less retro-technical in this layout; rejected |
| Adwaita Mono | 1,394,892 bytes / open-source GNOME font | Clear modern UI face, but larger and less classic-computer in the Nenia panels; rejected |
| Fusion Pixel 10px Mono zh_hans | 2,746,916 bytes / OFL-1.1 | Complete tested Simplified Chinese sample and strong pixel identity; retained only as the explicit CJK fallback |
| WenQuanYi Zen Hei Mono | 18,456,324 bytes / GPL-2.0 font exception | Strong CJK coverage, but too large for this small fallback role and not as visually coherent with the selected Latin face; rejected |

The glyph-mask audit confirmed distinct masks for `M/N`, `P/O`, `O/0`,
`C/G`, and `I/L` in Source Code Pro. The production render is checked
directly at the final integer viewport size: primitive geometry remains
pixel-crisp, text antialiasing is enabled for readability, and smooth pixmap
transforms are enabled for artwork. Text cells use `QFontMetrics`-driven right
elision instead of whole-scene scaling.

Source Code Pro is the primary Latin/German face. Its explicit second family
is the bundled Fusion Pixel zh-Hans face, used for missing CJK glyphs; Qt's
platform-default fallback is not the intended localization path.

## Selected primary file and provenance

Nenia bundles:

`assets/fonts/source-code-pro-regular.otf`

The file is an unmodified Source Code Pro Regular OpenType face. It is 132,740
bytes with SHA-256
`39a2bb5651a858a28caaa4ac4b8e6fbce9c68b5ccb94736d4e89ad71469ffb82` and
reports the Qt family `Source Code Pro`. The complete upstream notice is
retained as `assets/fonts/LICENSE-Source-Code-Pro-OFL.txt`. Source Code Pro is
distributed under the [SIL Open Font License 1.1](https://openfontlicense.org)
by Adobe Systems Incorporated; the upstream repository describes it as a
monospaced font designed for UI environments.

## CJK fallback file and provenance

Nenia bundles:

`assets/fonts/fusion-pixel-10px-monospaced-zh_hans.otf`

The exact upstream archive is
`fusion-pixel-font-10px-monospaced-otf-v2026.09.01.zip`, size 13,724,032
bytes, with published SHA-256:

`94fe8aa6f7251dcbe33529a16ff55e183272df4c0fb0748f9ef388d0e02f096f`

The bundled file is 2,746,916 bytes with SHA-256:

`d31045e9558f7eab9e4b894c154cc8020ce8fcb41b1ba5287b5206c86982a563`

The file reports the Qt family `Fusion Pixel 10px Mono zh_hans`. It remains an
OpenType file with the upstream internal name; it was not subset, renamed,
converted, or glyph-edited. The upstream [SIL Open Font License 1.1 notice](https://raw.githubusercontent.com/TakWolf/fusion-pixel-font/master/LICENSE-OFL)
is retained as `assets/fonts/LICENSE-Fusion-Pixel-OFL.txt`, together with the
component notices shipped by the upstream release. The upstream notice does
not declare a Reserved Font Name. The complete provenance record and local
notice inventory are in [`assets/fonts/README.md`](../assets/fonts/README.md).

The Ark secondary archive was also verified from its official release:
`ark-pixel-font-10px-monospaced-ttf-v2026.09.01.zip`, published SHA-256
`a83dcfe769691ed382f3a1e56ef4413ae0f1083ee4289f3ff3ef88830aabb43a`.
Ark was evaluated but is not bundled.

## Qt resource and loading architecture

`src/CMakeLists.txt` uses `qt_add_resources` to place the two files at:

`:/nenia/fonts/source-code-pro-regular.otf`

`:/nenia/fonts/fusion-pixel-10px-monospaced-zh_hans.otf`

The resource is part of the private `nenia_lib` static library. `NeniaTypography`
keeps the resource paths, family lookup, and `QFont` construction private.
Its function-local initialization calls `QFontDatabase::addApplicationFont`
only after `QGuiApplication` exists, so merely linking the library does not
perform GUI-dependent font work. The primary family is placed first and the
bundled zh-Hans family second in the `QFont` family list. If the primary asset
cannot load, Qt's fixed system font remains a last-resort safety path. The
public `NeniaWidget` API is unchanged.

## Runtime policy and measured metrics

Phase 10 F2 keeps F1's bounded integer sizes while changing the primary family
to Source Code Pro:

- Normal: body 12px, title 13px, small 10px;
- Large: body 13px, title 14px, small 11px;
- ExtraLarge: body 14px, title 15px, small 12px;
- Source Code Pro supplies the Latin/German UI glyphs and the ambiguity-pair
  readability checks;
- the explicit Fusion Pixel fallback supplies the representative Simplified
  Chinese sample when the primary has no glyph;
- font weight is Normal, fixed-pitch is requested, and absolute letter spacing
  is exactly `0.0`;
- primitive geometry remains non-antialiased, while text antialiasing and
  `SmoothPixmapTransform` are enabled;
- no `PreferBitmap` strategy is used for the selected OTF;
- the selected sizes are set as integer `QFont::pixelSize()` values, never by
  a whole-scene transform or fractional painter scaling.

The private draw helpers use the tier-selected pixel size and still elide
through `QFontMetrics` against each actual cell. F2 changes the primary family,
fallback chain, and rendering preference without changing the density-tier
contract.

Coverage tests include the existing English UI vocabulary and German umlauts
and `ß` in Source Code Pro, plus the mixed Simplified Chinese specimen
`位置 程序 状态 就绪 图像 频谱 通道 音量 音符 速度 静态 显示` in the explicit
fallback. This is a tested sample, not a claim that either bundled face is a
complete CJK dictionary.

German headroom checks currently enforce `PROGRAMM ≤ 80px`, `BEREIT ≤ 42px`,
and `AKTIVITÄT ≤ 62px`. `KANALAKTIVITÄT` is intentionally measured as a
longer-risk label: it exceeds 62px but remains within the 280px specimen
budget. A future layout pass can promote any translated label that exceeds
its actual panel rectangle into a dedicated shorter translation key.

## Localization boundary

Complete user-facing labels in `src/NeniaRenderer.cpp` use
`QCoreApplication::translate("NeniaUi", "...")`, making the source strings
ready for `lupdate` later. Technical tokens and fixed data remain literal:
channel names (`FM1`, `SSG1`, `ADP`), note names (`C4`), field tokens (`GT`,
`PRG`, `LV`), axis values, numeric readings, and other protocol-like values
are not translated as prose. F2 adds no `.ts` catalogs, language selector,
locale UI, or full German/Chinese translations. Those belong to the explicit
future roadmap item `Phase 1B F4 — Localization Catalogs & Locale Integration`.

## Temporary specimen

The manual-review specimen was generated outside the repository and is not
included in the source archive or runtime assets. It contains Latin ambiguity
pairs, technical tokens, German headroom samples, Chinese samples, mixed
Latin/CJK labels, and a 5px half-width cell ruler. It can be regenerated by
the temporary helper used during F2 validation.
