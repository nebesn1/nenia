# Nenia

Nenia is an independently implemented Qt 6 / C++20 music player and
visualization workspace. Its pixel-conscious visual style takes inspiration
from FMDSP/98fmplayer, but Nenia is not affiliated with or endorsed by that
project or its developers.

## Features

- Encoded audio playback through Qt Multimedia. Available formats depend on
  the Qt Multimedia backend, plugins and codecs installed on the system.
- Standard MIDI File Format 0/1 parsing, tempo-aware visual timelines and a
  sixteen-channel MIDI state matrix.
- Optional Direct MIDI synthesis through FluidSynth 2.3.5 and a user-provided
  SoundFont. MIDI visualization is separate from the opt-in synthesis backend.
- Recursive local-folder import, M3U/M3U8 playlists, track selection,
  previous/next navigation, seeking, Repeat One/Repeat All and Shuffle.
- Local text-tag metadata, embedded/local artwork, and an optional artwork
  override.
- A 32-band PCM Spectrum, adaptive native-resolution layout, and seven color
  schemes: Nenia Classic, Nord Light/Dark, Catppuccin Latte/Mocha and Gruvbox
  Light/Dark.
- Persistent user preferences for color scheme, playback options, keyboard
  display settings, last folder, SoundFont path and MIDI loudness preset.

The reusable Nenia library and standalone application are separate targets.
CMake installation includes the standalone application, the existing static
library package, public headers, Linux desktop launcher, and the MIT and
third-party/font license notices. It does not bundle Qt, multimedia plugins,
TagLib, FluidSynth, codecs, or a SoundFont; see the Linux installation notes
below.

## Tested environment

The documented desktop acceptance environment is openSUSE Leap 16 with KDE
Plasma on Wayland and Qt 6.12. Other platforms and Qt Multimedia backends have
not been claimed as desktop-accepted by this project.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6.12 or newer: Core, Gui, Widgets and Multimedia
- Qt Test when `BUILD_TESTING=ON` (the default for this development checkout)
- TagLib 2.0 or newer
- Ninja for the commands below (another supported CMake generator may be used)
- For MIDI-ON builds only: pkg-config and FluidSynth exactly 2.3.5

The top-level CMake project only requires Qt Test when `BUILD_TESTING=ON`.
Qt DBus is optional on UNIX systems and enables the XDG Desktop Portal file
chooser when available.

## Build and test

Build the complete MIDI-enabled application:

    cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON \
      -DNENIA_BUILD_FLUIDSYNTH_PROTOTYPE=ON \
      -DNENIA_BUILD_MIDI_PLAYBACK_BACKEND=ON
    cmake --build build --parallel 2
    QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

For a MIDI-OFF build, use a separate build directory and disable both options:

    cmake -S . -B build-midi-off -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON \
      -DNENIA_BUILD_FLUIDSYNTH_PROTOTYPE=OFF \
      -DNENIA_BUILD_MIDI_PLAYBACK_BACKEND=OFF
    cmake --build build-midi-off --parallel 2
    QT_QPA_PLATFORM=offscreen ctest --test-dir build-midi-off --output-on-failure

If CMake cannot find a non-system Qt SDK, add
-DCMAKE_PREFIX_PATH=/path/to/your/Qt/6.12.0/gcc_64 to the configure command and
replace the placeholder with that SDK's prefix. The MIDI backend option
requires the FluidSynth prototype option; enabling it with a different
FluidSynth version is rejected by CMake.

CTest runs headless with audio-device output disabled. Tests use deterministic
project fixtures. Optional real-SoundFont or external-MIDI integration cases
can be skipped unless their documented environment inputs are available. A
successful offscreen suite is not a substitute for testing a target desktop
and audio device.

### Public source snapshot build

The planned public source snapshot excludes the private development-only
directories `/assets/demo/`, `/assets/themes/`, `/tests/` and `/tools/`, along
with the internal `/docs/phase*.md` engineering reports. Since
`tests/CMakeLists.txt` is absent there, `BUILD_TESTING` defaults to `OFF`;
the command below sets it explicitly for clarity. This avoids requiring the Qt
Test component and prevents the application build from copying demo fixtures:

    cmake -S . -B build-public -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DNENIA_BUILD_FLUIDSYNTH_PROTOTYPE=OFF \
      -DNENIA_BUILD_MIDI_PLAYBACK_BACKEND=OFF
    cmake --build build-public --parallel 2

To include Direct MIDI in that source snapshot, enable both FluidSynth options
as described below and provide the required FluidSynth 2.3.5 development
package. The private development checkout retains the full fixture and
regression-test workflow with `BUILD_TESTING=ON`. The private
`NENIA_BUILD_DIAGNOSTIC_TOOLS` option defaults to the value of
`BUILD_TESTING`; it builds optional render and MIDI playback diagnostic
executables when their corresponding backend is enabled. These tools are not
installed, and normal `BUILD_TESTING=OFF` production builds omit them.

## Building and Installing on Linux

The documented desktop-accepted environment is openSUSE Leap 16 with KDE
Plasma on Wayland and Qt 6.12. CMake can install the application and reusable
library, but this source project does not produce a self-contained binary
package.

To build and run without installing, use a Release production build with
`BUILD_TESTING=OFF`. For MIDI-OFF, disable both MIDI options:

    cmake -S . -B build-linux-off -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DNENIA_BUILD_FLUIDSYNTH_PROTOTYPE=OFF \
      -DNENIA_BUILD_MIDI_PLAYBACK_BACKEND=OFF
    cmake --build build-linux-off --parallel 2
    ./build-linux-off/app/nenia

For MIDI-ON, use a separate build directory and enable both options:

    cmake -S . -B build-linux-midi -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DNENIA_BUILD_FLUIDSYNTH_PROTOTYPE=ON \
      -DNENIA_BUILD_MIDI_PLAYBACK_BACKEND=ON
    cmake --build build-linux-midi --parallel 2
    ./build-linux-midi/app/nenia

MIDI-ON requires the FluidSynth 2.3.5 development package at build time and
the matching shared runtime library at launch. Qt 6.12 or newer (Core, Gui,
Widgets and Multimedia), TagLib 2.0 or newer, and the Qt Multimedia backend
and audio dependencies supplied by the target system are also required.
When using a non-system Qt SDK, pass its prefix with
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/gcc_64`. The Qt Multimedia FFmpeg
plugin and its codec dependencies are provided by that Qt/system installation;
Nenia does not copy Qt, FFmpeg, FluidSynth, or audio-backend libraries into
the install tree. Formats and audio devices available at runtime depend on
the installed backend and system configuration.

Direct MIDI additionally needs a SoundFont selected by `--sf2`,
`NENIA_SOUNDFONT`, or the saved preference. Nenia neither bundles nor installs
a SoundFont.

To install a chosen build into a user-local prefix (this command is not run
automatically by the project):

    cmake --install build-linux-off --prefix "$HOME/.local"

Use `build-linux-midi` instead to install the MIDI-ON variant. The executable
is installed under `${CMAKE_INSTALL_BINDIR}` (normally `bin`), the desktop
entry under `${CMAKE_INSTALL_DATADIR}/applications` (normally
`share/applications`), and license notices under
`${CMAKE_INSTALL_DATADIR}/licenses/Nenia`. The static library, public headers
and CMake package retain their existing install layout. No demo fixtures,
private tests or developer harnesses are installed.

For a system-wide source install, choose an appropriate prefix such as
`/usr/local` and run `cmake --install` with the permissions required by that
system. A user-local install under `$HOME/.local` is discovered through the
standard XDG data home; ensure `$HOME/.local/bin` is in the Plasma session's
`PATH` so the launcher's `Exec=nenia` resolves. For another prefix, add its
`share` directory to `XDG_DATA_DIRS` and its `bin` directory to `PATH`, then
refresh the KDE service cache with `kbuildsycoca6 --noincremental` or start a
new Plasma session. KDE locates the `.desktop` file through XDG application
directories. The launcher uses the generic icon-theme name
`audio-x-generic`; Nenia does not yet ship a dedicated project icon.

CMake does not provide a dedicated uninstall target. Review the build tree's
`install_manifest.txt` and remove only the files installed by that build;
do not remove an entire shared prefix that may contain other software. The
source install is not an AppImage, Flatpak, RPM, or dependency-free bundle.
Runtime libraries and Qt plugins must remain available from the target
system's normal loader and plugin paths; the install does not copy the
developer's Qt SDK or hard-code its home-directory path into the installed
executable.

The canonical application output in a build tree is build/app/nenia. No binary
installer or portable application package is provided by this source release.

## Running

Start the application without an initial source:

    ./build/app/nenia

The application may restore a still-readable last-opened folder, but does not
autoplay at startup. Examples below use placeholder paths; replace them with
local files on your system.

Prepare local encoded audio, optionally with a visual timeline:

    ./build/app/nenia --audio /path/to/track.ogg
    ./build/app/nenia --audio /path/to/track.ogg --timeline /path/to/timeline.json
    ./build/app/nenia --audio /path/to/track.ogg --midi /path/to/visual.mid

Import a folder or an M3U/M3U8 playlist:

    ./build/app/nenia --playlist-folder /path/to/music
    ./build/app/nenia --playlist-file /path/to/list.m3u8

Direct MIDI playback is available only in a MIDI-ON build:

    ./build/app/nenia --midi-file /path/to/song.mid --sf2 /path/to/soundfont.sf2

SoundFont selection order is command-line option, NENIA_SOUNDFONT environment
variable, then the saved user preference. Nenia does not bundle, download or
license a SoundFont. Check the selected SoundFont's license separately.

Additional options include --artwork, --title, --artist and --album. The
application also supports --help and --version. Source-mode options
--audio, --playlist-folder, --playlist-file and --midi-file are mutually
exclusive. MIDI visual events can be paired with --audio using --midi; JSON
visual events can be paired with --audio using --timeline. --timeline and
--midi are mutually exclusive and each requires --audio. Direct MIDI requires
a local `.mid` or `.midi` file and a MIDI-ON build. The parser supports SMF
Format 0/1 with PPQN timing; Format 2 and SMPTE time division are not
supported. Metadata and artwork overrides are not accepted with
--playlist-folder or --midi-file.

## Playlists and controls

M3U/M3U8 entries use local paths, preserve playlist order and duplicates, and
are saved only when the user explicitly saves a playlist. Folder import is a
recursive flat Playlist. Selecting a track does not start it; Enter or a
double-click activates the selected item.

    O             Open files (including supported media and playlists)
    Ctrl+O        Open Folder
    Up / Down     Select a Playlist row
    Enter         Play selected row
    P / N         Previous / Next
    Left / Right  Seek −/+5 seconds
    Home          Seek to start
    Space         Play / Pause
    R             Restart and play
    S             Stop
    Ctrl+S        Save Playlist as UTF-8 M3U8
    I             Toggle primary / extended MEDIA INFO
    Ctrl+Q        Quit

The application also provides custom Previous, Play/Pause, Next, Loop and
Shuffle controls. Loop and Shuffle are independent settings.

## Private development demo and test material

The full private development checkout contains synthetic or project-created
fixtures, not user music. These are intentionally absent from the planned
public source snapshot and are only needed by the testing-enabled development
workflow:

- `timeline-provider-fixture.json` is a deterministic JSON timeline fixture
  used by timeline tests.
- `audio-playback-fixture.wav` and `audio-timeline-fixture.json` are the
  generated diagnostic audio and matching timeline; the generator is
  `tools/generate_audio_and_timeline_fixtures.py`.
- `multichannel-midi-fixture.mid` is a generated Format 1 MIDI fixture used by
  parser, visualization and Direct MIDI regression tests; see
  `tools/generate_midi_fixture.py`.
- `cover-artwork-fixture.png` is generated project artwork copied into the
  build tree's explicit demo folder; see `tools/generate_artwork_fixture.py`.
- tests/assets contains synthetic metadata WAVs and generated decoder-test
  tones; see tests/assets/README.md.

In particular, `BUILD_TESTING=OFF` production builds do not need these files
and do not copy them next to the executable.

These assets are not an implicit startup source. Obsolete, unreferenced
bank-switch and program-switch instrument samples were removed; neither is
required by the application, build system, or current regression tests.

## License and acknowledgements

The MIT license applies to Nenia-owned code, public headers, tests, build
scripts, tools, documentation and original project material that the
contributors are authorized to license. Bundled third-party fonts retain
their own licenses. Qt, TagLib, FluidSynth and other external dependencies
retain their respective licenses. User-provided media, artwork, playlists and
SoundFonts are not covered by Nenia's license.

See LICENSE, THIRD_PARTY_NOTICES.md, CREDITS.md, PRIVACY.md and
assets/fonts/README.md for scope, notices, provenance and privacy details.
Nenia's selected palette values include Nenia-specific role and contrast
adjustments; they are not a port of any upstream theme engine.
