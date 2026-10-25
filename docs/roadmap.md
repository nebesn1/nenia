# Nenia Roadmap

## Current status

Nenia's main application development is considered functionally complete by
the project owner. The Phase 11E feature series through F8F is implemented.
The project owner explicitly reports that the F8E theme work and F8F F1
artwork correction passed KDE desktop acceptance. Earlier phase reports may
preserve the acceptance status recorded at the time they were written; those
point-in-time notes are not the current project status.

- Phases 0–10: complete.
- Phase 11C: shared full-fidelity Standard MIDI File parser: complete.
- Phase 11D: optional real-time FluidSynth playback backend: accepted.
- Phase 11E: Direct MIDI source integration and follow-up visualization,
  performance, loudness, preset and theme work: implemented; see the detailed
  internal phase reports retained in the private development checkout.
- Phase 12A: read-only release audit: complete.
- Phase 12B F1: source-release documentation, MIT license preparation and
  verified-unused demo cleanup: implementation complete and ready for
  documentation acceptance. This does not certify contributor copyright
  ownership or clear binary/install-artifact redistribution; the first
  release target remains source-only.
- Phase 12B F2: current-working-tree source candidate and release-warning
  review prepared; ready for documentation acceptance. Nothing was published.
- Phase 1B F4 localization catalogs and runtime integration remain optional
  planned work; they are not required for the current player scope.

No binary package has been prepared as part of Phase 12B. ReplayGain/LUFS work
is not part of this release-documentation task.

## Current product scope

Nenia is a Qt 6 / C++20 music player and visualization workspace. The current
application supports encoded audio playback through Qt Multimedia, a
sixteen-channel MIDI visualization, and an optional Direct MIDI synthesis
backend that uses an external SoundFont through FluidSynth. It also provides
local folder and M3U/M3U8 playlists, metadata and artwork presentation, PCM
Spectrum analysis, transport and playlist navigation, seven color schemes,
and persistent user preferences.

The MIDI-ON build is opt-in. MIDI visualization and MIDI sound synthesis are
separate paths. Available encoded-audio formats depend on the installed Qt
Multimedia backend and system codecs. Nenia does not bundle a SoundFont or
provide ReplayGain/LUFS normalization.

## Milestone history and superseded design limits

- Phases 0–2 established the CMake project, original renderer, typography and
  animation state.
- Phase 3 introduced the JSON timeline provider and deterministic timeline
  fixture. Phase 4 added Qt Multimedia playback and its matching diagnostic
  WAV/timeline fixture.
- Phase 5 initially provided visual-only MIDI events mapped to ten legacy
  tracks. This was the state at that milestone, not a current limitation:
  Phase 11C added the shared full-fidelity parser, and Phase 11E added a
  separate sixteen-channel projection and optional Direct MIDI playback.
- Phase 6 added decoded-PCM FFT Spectrum analysis. Phase 7 established the
  reusable library, metadata/artwork presentation and CMake package; its
  original ten-column layout was later superseded by the sixteen-channel
  matrix.
- Phases 8–9 added playlist navigation and the integrated standalone host.
  Their historical keyboard/order controls were later refined by Phase 10 F7;
  current shortcuts are documented in the root README.
- Phase 10 replaced whole-scene scaling with an adaptive native-resolution
  layout, added richer metadata, Playlist interaction and independent Loop
  and Shuffle behavior.
- Phase 11C/11D/11E added the shared MIDI sequence model, opt-in FluidSynth
  backend, direct MIDI source routing, metadata/state projection and the
  sixteen-channel matrix. Later F5–F8 work refined EOM/seek behavior,
  coalesced rendering, MIDI loudness policy, user presets, color themes and
  artwork controls. Detailed records remain in the phase-specific documents.

## Historical technical reports

Phase reports are retained in the private development checkout as records of
implementation decisions, test results and point-in-time acceptance
instructions. Their exact temporary build directories and machine-specific
command outputs describe those past runs; they are excluded from public source
archives. Use the portable build instructions in the root README for new
builds. The Phase 11E reports should be read together with the current
architecture and README when an older phase statement describes a feature
later superseded by another milestone.

## Not currently planned

ReplayGain/LUFS normalization, recording or re-encoding, semantic
audio-to-MIDI analysis, bundled/downloaded SoundFonts, and additional
platform-specific desktop packaging are outside this documentation phase.
