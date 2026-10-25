# Nenia Phase 6 spectrum analysis

Phase 6 adds a read-only audio-analysis path beside the structured visual-event
path. The decoded audio remains owned by Qt Multimedia and is sent unchanged to
the user. Nenia observes decoded PCM through `QAudioBufferOutput`, analyzes it,
and paints timestamped results in the existing right-side Spectrum panel.

```text
QMediaPlayer
    ├── QAudioOutput                 audible, unmodified playback
    └── QAudioBufferOutput           decoded PCM observer
              ↓
       NeniaSpectrumAnalyzer
              ↓
       NeniaSpectrumFrameStore
              ↓  lookup by QMediaPlayer::position()
          NeniaWidget / NeniaRenderer
```

JSON and MIDI remain independent structured-event providers:

```text
JSON/MIDI → TimelineEventProvider → NeniaTimelineSynchronizer
          → NeniaAnimationEngine → display layer
```

The paths meet only when the widget redraws. Spectrum data is not a
`TimelineEvent`, is not inserted into either timeline provider, and does not
drive the animation engine's note or meter decay.

## Analyzer constants and signal preparation

The analyzer uses a fixed 1024-sample forward radix-2 FFT with a 512-sample
hop, giving 50% overlap. A Hann window is precomputed once and applied to
every channel/window before the FFT. The implementation is a small internal
iterative Cooley–Tukey transform using `std::complex<float>`; it has no FFTW,
KissFFT, resampling library, GPU path, worker thread, or external DSP
dependency.

`QAudioBuffer` formats are validated before reading. The supported Qt sample
formats are:

- `UInt8`, centered at 128 and divided by 128;
- `Int16`, divided by 32768;
- `Int32`, divided by 2147483648;
- `Float`, used directly when finite; non-finite values become zero.

Interleaved mono, stereo, and higher channel counts are accepted when the
metadata and byte count agree. Channels are analyzed independently. Their
spectral powers are averaged, so an inverted right channel does not cancel an
otherwise identical left channel. Unknown formats, invalid metadata, empty
buffers, and unsupported channel/rate combinations are ignored safely.

## Frequency bands and levels

The negative FFT half and DC are omitted. Positive bins 1 through `N/2` are
mapped by frequency, with a lower bound of 40 Hz and an upper bound of
`min(16000 Hz, Nyquist)`. The 32 display bands are logarithmically spaced
between those bounds. A bin is assigned to the logarithmic band containing its
frequency.

Each band's energy is the arithmetic mean of the channel-averaged spectral
powers of all FFT bins assigned to that band. Empty bands are zero. The power
uses Hann coherent-gain correction so a full-scale sine has meaningful,
non-auto-normalized energy. Power is mapped to a fixed dBFS-like range:

```text
  0 dB   → 1.0
-60 dB   → 0.0
```

Values outside that range are clamped. There is no frame-by-frame
normalization and no temporal attack/release smoothing in Phase 6. The
overlapping Hann windows provide the only natural continuity; keeping raw
media-time frames makes seek reconstruction independent of decoder history.

## Frame timestamps and rolling input

Each `SpectrumFrame` contains a `centerTimeUs` and 32 normalized bands. The
timestamp is the center of the 1024-sample analysis window, calculated from the
buffer's media start time plus 512 samples using integer microsecond conversion
from the sample-rate rational. Timestamps are not derived from callback wall
time and are not repeatedly rounded to milliseconds.

`QAudioBuffer::startTime()` is used whenever it is non-negative. If a buffer has
no valid start time, an already-established contiguous sample clock is used. If
there is no established clock yet, the controller supplies the current
`QMediaPlayer::position()` explicitly as a fallback; the analyzer never
silently assumes an unknown buffer starts at zero. A buffer start that is not
contiguous with the established clock resets the rolling stream. A two-
microsecond comparison tolerance covers timestamp integer rounding without
allowing ordinary seeks to join windows.

The analyzer retains interleaved normalized samples between callbacks. Incoming
buffer boundaries may therefore fall anywhere within a 1024-sample window or
512-sample hop. On a seek/discontinuity, pending samples and timestamp
continuity are discarded, so samples before and after the seek cannot share a
window. A sample-rate, channel-count, or sample-format change performs the same
reset before accepting the new format.

## Frame cache and media-clock lookup

`NeniaSpectrumFrameStore` belongs to the currently loaded audio source. It keeps
frames sorted by center timestamp, accepts out-of-order insertion after a seek,
and replaces a frame with the same timestamp instead of accumulating a
duplicate.

At media position `P`, lookup chooses the newest frame whose timestamp is at
or before `P`. A future decoded frame is never displayed merely because it
arrived first. The selected frame must be no more than 200 ms old; otherwise the
store returns no frame and the widget shows a zero spectrum. This prevents a
cached 2-second frame from remaining visible after a seek to 70 seconds before
the destination PCM has been decoded.

Replacing the audio source clears the analyzer stream, timestamp continuity,
frame store, and displayed bands. A seek resets only the rolling analyzer
continuity and retains completed same-source frames for seek-back reuse. Stop,
restart, and natural end clear displayed bands immediately while retaining the
same-source cache unless the source is replaced.

## Playback lifecycle

`QMediaPlayer::position()` remains the only authoritative visual clock. The
existing controller position signal and approximately 16 ms polling tick both
sample that position; there is no independent spectrum timer.

- Playing selects the frame at the current media position.
- Pause stops position polling and freezes the selected frame. New decoded data
  may be cached, but it can change the display only if normal lookup at the
  unchanged paused position selects it.
- A buffering/stalled player cannot advance the display because future frames
  are ineligible and position is unchanged.
- A seek resets rolling input, immediately queries the destination cache, and
  shows neutral until a suitable destination frame exists.
- Stop shows a zero spectrum immediately.
- End-of-media performs the accepted final synchronization and then resets all
  visual domains to neutral, including every spectrum band.
- Restart begins with neutral display state and can reuse same-source frames as
  playback advances from zero.

Audio-only playback is a supported Phase 6 mode: audio is audible, the note
matrix and 19 semantic channel meters remain neutral, and the Spectrum panel
is driven by decoded PCM. JSON plus audio supplies JSON notes/meters plus the
same PCM spectrum. MIDI plus audio supplies MIDI notes, leaves semantic meters
neutral, and supplies the same PCM spectrum. Stereo/master PCM never becomes
19 channel-activity values.

## Renderer

The adaptive native-resolution scene computes its plot from the current right
column. The plot contains 32 deterministic integer-width bars with
one-pixel gaps, a small integer inset, bottom-up two-pixel segments, and no
antialiasing, gradients, blur, glow, or wall-clock animation. Bars use the
existing Nenia cyan/dim palette roles. The label is now
`SPECTRUM / LIVE FFT`; the frame, axes, grid, and frequency labels remain
visible when all bands are zero.

Spectrum display state is separate from the 10×128 note state and 19 meter
state. `resetVisualization()` clears displayed spectrum bands but does not
destroy the controller's completed same-source analysis cache.

## Verification

`tests/tst_nenia_spectrum_analyzer.cpp` tests the FFT with impulse and aligned
sine signals; known 220/440/1000/4000 Hz band placement; silence; amplitude
meaning; anti-phase stereo; UInt8/Int16/Int32/Float conversion; mono/stereo/
multi-channel input; fragmented buffers; 50% overlap; rational timestamps;
contiguous timing; discontinuities; format changes; invalid buffers; frame
ordering, deduplication, staleness, prefetch, seek-back, and cache clear. It
also contains a bounded real `QMediaPlayer` + `QAudioBufferOutput` smoke test
using the private Phase 4 WAV fixture. The smoke test is supplemental and does
not replace the deterministic DSP tests. Tests and fixtures remain in the
private development checkout and are excluded from public source archives.
