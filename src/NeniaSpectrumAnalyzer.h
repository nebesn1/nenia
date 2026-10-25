#pragma once

#include "NeniaSpectrumFrame.h"

#include <QAudioBuffer>

#include <array>
#include <complex>
#include <optional>
#include <span>
#include <vector>

namespace nenia::detail {

inline constexpr int kSpectrumFftSize = 1024;
inline constexpr int kSpectrumHopSize = 512;
inline constexpr float kSpectrumLowerFrequencyHz = 40.0f;
inline constexpr float kSpectrumUpperFrequencyHz = 16'000.0f;
inline constexpr float kSpectrumDbFloor = -60.0f;

class NeniaSpectrumAnalyzer final
{
public:
    using FftBins = std::array<std::complex<float>, kSpectrumFftSize>;

    static FftBins forwardFft(const std::array<float, kSpectrumFftSize> &samples);

    // This testable conversion seam returns interleaved, normalized samples.
    // It intentionally has no dependency on playback or widgets.
    static std::optional<std::vector<float>> decodeInterleavedSamples(
        const QAudioBuffer &buffer);

    static std::optional<SpectrumFrame> analyzeWindow(
        const std::vector<float> &interleavedSamples,
        int channelCount,
        int sampleRate,
        qint64 centerTimeUs);

    std::vector<SpectrumFrame> processBuffer(
        const QAudioBuffer &buffer,
        std::optional<qint64> fallbackStartTimeUs = std::nullopt);

    // Internal direct-MIDI PCM adapter. Samples are interleaved normalized
    // Float32 PCM; startOutputFrame supplies the synthesis/output timeline.
    std::vector<SpectrumFrame> processPcmFrames(std::span<const float> interleavedSamples,
                                                int channelCount,
                                                int sampleRate,
                                                std::uint64_t startOutputFrame);

    void resetStream() noexcept;

private:
    static qint64 sampleTimestamp(qint64 streamStartTimeUs,
                                  qint64 sampleIndex,
                                  int sampleRate) noexcept;
    static bool isSupportedFormat(const QAudioFormat &format) noexcept;

    void beginStream(const QAudioFormat &format, qint64 startTimeUs);

    bool m_hasStream = false;
    QAudioFormat m_format;
    qint64 m_streamStartTimeUs = 0;
    qint64 m_pendingStartSampleIndex = 0;
    qint64 m_nextSampleIndex = 0;
    std::vector<float> m_pendingInterleavedSamples;
};

} // namespace nenia::detail
