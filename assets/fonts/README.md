# Nenia bundled font provenance

Nenia bundles a readable primary UI font and an explicit CJK fallback. The
renderer is native-resolution and no longer has a deterministic 640×400 scene.

## Primary UI font

- Name: Source Code Pro Regular
- Project: [Adobe Source Code Pro](https://github.com/adobe-fonts/source-code-pro)
- Release: Roman fonts version `2.038`, tag
  [`2.038R-ro/1.058R-it/1.018R-VAR`](https://github.com/adobe-fonts/source-code-pro/tree/2.038R-ro/1.058R-it/1.018R-VAR)
- Release source: the unmodified `OTF/SourceCodePro-Regular.otf`; its SHA-256
  matches the file at that upstream tag.
- Bundled file: `source-code-pro-regular.otf`
- Bundled file size: 132,740 bytes
- Bundled file SHA-256: `39a2bb5651a858a28caaa4ac4b8e6fbce9c68b5ccb94736d4e89ad71469ffb82`
- Qt family reported by the file: `Source Code Pro`
- Format: OpenType (`.otf`)
- License: SIL Open Font License 1.1; copyright © 2010–2021 Adobe. The
  complete notice records Reserved Font Name `Source`.

The file is copied without subsetting, glyph edits, format conversion, or
internal-name changes. Its SHA-256 was checked against the Adobe 2.038
release file. The complete OFL notice matching that release is retained as
`LICENSE-Source-Code-Pro-OFL.txt`.

Source Code Pro is the primary Latin/German UI face because it is compact,
monospaced, designed for UI environments, and distinguishes the technical
ambiguity pairs used by Nenia. It does not claim full CJK coverage.

## Explicit CJK fallback

- Name: Fusion Pixel 10px Mono zh_hans
- Project: [TakWolf/fusion-pixel-font](https://github.com/TakWolf/fusion-pixel-font)
- Release: `2026.09.01`, commit `51c6d2d`, published 2026-09-01
- Release page: <https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.09.01>
- Selected archive: `fusion-pixel-font-10px-monospaced-otf-v2026.09.01.zip`
- Archive size: 13,724,032 bytes
- Published archive SHA-256: `94fe8aa6f7251dcbe33529a16ff55e183272df4c0fb0748f9ef388d0e02f096f`
- Selected file in the archive: `fusion-pixel-10px-monospaced-zh_hans.otf`
- Bundled file size: 2,746,916 bytes
- Bundled file SHA-256: `d31045e9558f7eab9e4b894c154cc8020ce8fcb41b1ba5287b5206c86982a563`
- Qt family reported by the file: `Fusion Pixel 10px Mono zh_hans`
- Format: OpenType (`.otf`)
- License: SIL Open Font License 1.1; copyright © 2022 TakWolf

The font file is copied without subsetting, glyph edits, format conversion, or
internal-name changes. The exact OTF bytes and the root OFL and three component
notices were checked against the selected upstream release archive. The root
`LICENSE-OFL` copyright statement does not name a Reserved Font Name, while
the retained upstream component notice
`LICENSES/boutique-bitmap-9x9/OFL.txt` declares `Fusion Pixel` as a Reserved
Font Name. OFL clause 3 addresses Modified Versions; the bundled OTF is the
byte-identical upstream release file, not a Nenia-modified font. Do not
describe the font as having no RFN, and do not modify or repackage a derivative
under that reserved name without resolving the applicable license terms.
All root and component notices used by the selected release remain under
`LICENSE-*` and `LICENSES/`.

The 10px monospaced zh-Hans file is no longer the main UI face. It is loaded
as an explicit second family in the Qt font family list and is used for
Simplified Chinese glyphs that Source Code Pro does not contain. The fallback
keeps the existing tested CJK sample working without relying on platform
fallback. Latin and German continue to resolve to Source Code Pro.

The font is embedded through Nenia's private Qt resource and loaded lazily
after `QGuiApplication` exists. Nenia does not require system font installation.
