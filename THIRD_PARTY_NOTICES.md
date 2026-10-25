# Third-Party Notices

This repository is currently prepared for source-code publication only. It
does not include Qt, TagLib, FluidSynth, FFmpeg, a SoundFont, or binary
packages of those dependencies. The notice below describes the dependencies
used by the project and the bundled font materials. A future binary package
must be audited against the exact Qt/backend packages it actually ships.

The root LICENSE applies only to Nenia-owned material that its contributors
are authorized to license. It does not replace any third-party license.

## Build and runtime dependencies

| Component | Version / license information | Role and distribution |
|---|---|---|
| Qt 6 | Required version 6.12 or newer. Qt's open-source terms are module-specific; LGPL v3 is the primary option for eligible modules, and GPL terms also apply where selected or required. | Core, Gui, Widgets and Multimedia are direct dependencies. DBus is optional on supported UNIX builds. Test is required only when `BUILD_TESTING=ON` (the default for the private development checkout). Qt itself is external and is not copied into this source tree. Check the license files supplied with the exact Qt package used. |
| Qt Multimedia FFmpeg backend | Qt 6.12 documentation describes FFmpeg 9.0.1 for Qt Online Installer packages; the actual system/SDK package may differ. FFmpeg contains components under different licenses. | Backend/plugin dependency of Qt Multimedia, not vendored by Nenia. Qt documents dynamic FFmpeg linkage for its online-installer binary packages and provides package-specific license/attribution material. A binary distributor must audit the exact plugin and libraries included in its own package. |
| TagLib | CMake requirement: TagLib 2.0 or newer. Upstream distributes it under LGPL and MPL terms; use the exact license texts shipped with the selected TagLib release/package. | External library used for read-only local text-tag metadata. No TagLib source or binary is vendored here. |
| FluidSynth | Exactly 2.3.5 when the optional MIDI synthesis configuration is enabled; upstream LICENSE is LGPL version 2.1. | External library for the optional Direct MIDI synthesis backend. MIDI-OFF builds do not enable this backend. No FluidSynth source or binary is vendored here. |
| C++ standard library and operating-system audio/graphics libraries | Supplied by the toolchain and operating system; package-specific. | External build/runtime dependencies, not vendored by Nenia. |
| CMake, Ninja, compiler, pkg-config | Toolchain dependencies; CMake minimum 3.24, C++20; Ninja is used by the documented examples. | Build-time tools only; not runtime components shipped by this source repository. |

Primary dependency references:

- [Qt Licensing for Qt 6.12](https://doc.qt.io/qt-6/licensing.html)
- [Qt open-source LGPL/GPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt Multimedia backend and FFmpeg deployment information](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [TagLib licensing and project information](https://taglib.org/)
- [FluidSynth 2.3.5 license text](https://raw.githubusercontent.com/FluidSynth/fluidsynth/v2.3.5/LICENSE)

## Bundled fonts

Both OpenType files below are embedded into Nenia's Qt resources. They are
third-party font software and are not relicensed by the root MIT license.
Their original license and copyright notices remain in assets/fonts/.

| Font material | License / notice | Role |
|---|---|---|
| Source Code Pro Regular 2.038, Adobe | SIL Open Font License 1.1 (OFL-1.1); the unchanged upstream font and its release notice declare Reserved Font Name “Source”. The bundled OTF SHA-256 matches Adobe's `2.038R-ro/1.058R-it/1.018R-VAR` tag. | Primary UI font. Full notice: `assets/fonts/LICENSE-Source-Code-Pro-OFL.txt`. |
| Fusion Pixel 10px Mono zh_hans, TakWolf, release 2026.09.01 | SIL Open Font License 1.1 (OFL-1.1). The root font notice has no RFN in its copyright statement; the retained `LICENSES/boutique-bitmap-9x9/OFL.txt` component notice explicitly declares `Fusion Pixel` as a Reserved Font Name. The bundled OTF and all applicable root/component notice files were verified byte-for-byte against the selected upstream release archive. The bundled font itself is unmodified. | CJK fallback. Full root and component notices are under `assets/fonts/`. OFL clause 3 concerns Modified Versions; no claim is made that the upstream font has no RFN. |
| Ark Pixel component notice, TakWolf | SIL Open Font License 1.1 (OFL-1.1). | Retained component notice under assets/fonts/LICENSES/ark-pixel/OFL.txt; no separate Ark Pixel font binary is bundled. |
| Galmuri component notice, Lee Minseo | SIL Open Font License 1.1 (OFL-1.1). | Retained component notice under assets/fonts/LICENSES/galmuri/LICENSE.txt; no separate Galmuri font binary is bundled. |
| BoutiqueBitmap9x9 component notice, 字言字型 / fancy type foundry and contributors | The included bilingual notice grants use, copying and distribution and states that derivatives must use the same license; no SPDX identifier is asserted here. | Retained component notice under assets/fonts/LICENSES/boutique-bitmap-9x9/OFL.txt; no separate BoutiqueBitmap9x9 font binary is bundled. |

The font provenance, release versions and checksums are documented in
`assets/fonts/README.md`. The exact Fusion Pixel upstream artifacts and their
reserved-name terms are described in the
[2026.09.01 release](https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.09.01),
its [root OFL notice](https://raw.githubusercontent.com/TakWolf/fusion-pixel-font/51c6d2d/LICENSE-OFL),
and its [BoutiqueBitmap9x9 component notice](https://raw.githubusercontent.com/TakWolf/fusion-pixel-font/51c6d2d/assets/fonts/boutique-bitmap-9x9/OFL.txt).
Keep all applicable notices with any redistribution of the font files. See
the [official SIL OFL 1.1 text](https://openfontlicense.org/open-font-license-official-text/).

The static Nenia library embeds these font bytes through Qt resources. CMake
installation includes the original root and component font notices under
`${CMAKE_INSTALL_DATADIR}/licenses/Nenia/fonts/`, alongside the Nenia MIT
license and this third-party notice. The notice texts are installed unchanged;
the fonts are not relicensed. Qt, TagLib, FluidSynth, FFmpeg and operating
system libraries remain external dependencies and are not copied into the
install tree. A distributor producing a binary package must still review the
license material supplied with the exact dependency packages it ships.

## External files and user content

Nenia does not bundle or download a SoundFont. Users choose an external
SoundFont for Direct MIDI playback and are responsible for reviewing that
file's separate license. User-selected music, artwork, playlists and SoundFonts
are not covered by Nenia's MIT license.
