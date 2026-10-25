#include "NeniaSpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

namespace nenia::detail {
namespace {

constexpr qint64 kTimestampContinuityToleranceUs = 2;

const std::array<float, kSpectrumFftSize> &hannWindow()
{
    static const std::array<float, kSpectrumFftSize> window = [] {
        std::array<float, kSpectrumFftSize> result{};
        for (int index = 0; index < kSpectrumFftSize; ++index) {
            const float phase = static_cast<float>(index)
                / static_cast<float>(kSpectrumFftSize - 1);
            result.at(static_cast<std::size_t>(index)) =
                0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * phase));
        }
        return result;
    }();
    return window;
}

float hannWindowSum()
{
    static const float sum = [] {
        float result = 0.0f;
        for (const float coefficient : hannWindow()) {
            result += coefficient;
        }
        return result;
    }();
    return sum;
}

float normalizedSample(const quint8 *data,
                       qsizetype offset,
                       QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return (static_cast<float>(data[offset]) - 128.0f) / 128.0f;
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return static_cast<float>(value) / 32'768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return static_cast<float>(value) / 2'147'483'648.0f;
    }
    case QAudioFormat::Float: {
        float value = 0.0f;
        std::memcpy(&value, data + offset, sizeof(value));
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return value;
    }
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        return 0.0f;
    }
    return 0.0f;
}

float levelFromPower(double power)
{
    if (!(power > 0.0) || !std::isfinite(power)) {
        return 0.0f;
    }

    const double decibels = 10.0 * std::log10(power);
    if (!std::isfinite(decibels) || decibels <= kSpectrumDbFloor) {
        return 0.0f;
    }
    if (decibels >= 0.0) {
        return 1.0f;
    }

    return std::clamp(static_cast<float>((decibels - kSpectrumDbFloor)
                                         / -kSpectrumDbFloor),
                      0.0f, 1.0f);
}

} // namespace

NeniaSpectrumAnalyzer::FftBins NeniaSpectrumAnalyzer::forwardFft(
    const std::array<float, kSpectrumFftSize> &samples)
{
    FftBins bins{};
    for (int index = 0; index < kSpectrumFftSize; ++index) {
        bins.at(static_cast<std::size_t>(index)) =
            std::complex<float>(samples.at(static_cast<std::size_t>(index)), 0.0f);
    }

    for (std::size_t index = 1, reversed = 0; index < bins.size(); ++index) {
        std::size_t bit = bins.size() >> 1;
        for (; (reversed & bit) != 0; bit >>= 1) {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(bins.at(index), bins.at(reversed));
        }
    }

    for (std::size_t length = 2; length <= bins.size(); length <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float>
            / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < bins.size(); start += length) {
            std::complex<float> rotation(1.0f, 0.0f);
            const std::size_t halfLength = length / 2;
            for (std::size_t offset = 0; offset < halfLength; ++offset) {
                const std::complex<float> even = bins.at(start + offset);
                const std::complex<float> odd =
                    rotation * bins.at(start + offset + halfLength);
                bins.at(start + offset) = even + odd;
                bins.at(start + offset + halfLength) = even - odd;
                rotation *= step;
            }
        }
    }

    return bins;
}

std::optional<std::vector<float>> NeniaSpectrumAnalyzer::decodeInterleavedSamples(
    const QAudioBuffer &buffer)
{
    if (!buffer.isValid() || buffer.frameCount() <= 0) {
        return std::nullopt;
    }

    const QAudioFormat format = buffer.format();
    if (!isSupportedFormat(format) || format.sampleRate() <= 0
        || format.channelCount() <= 0 || format.bytesPerFrame() <= 0) {
        return std::nullopt;
    }

    const qsizetype frameCount = buffer.frameCount();
    const qsizetype bytesPerFrame = format.bytesPerFrame();
    if (frameCount > std::numeric_limits<qsizetype>::max() / bytesPerFrame) {
        return std::nullopt;
    }

    const qsizetype expectedByteCount = frameCount * bytesPerFrame;
    if (buffer.byteCount() != expectedByteCount || buffer.byteCount() <= 0) {
        return std::nullopt;
    }

    const quint8 *data = buffer.constData<quint8>();
    if (data == nullptr) {
        return std::nullopt;
    }

    const qsizetype channelCount = format.channelCount();
    const qsizetype sampleCount = frameCount * channelCount;
    std::vector<float> result(static_cast<std::size_t>(sampleCount));
    const int bytesPerSample = format.bytesPerSample();
    for (qsizetype frame = 0; frame < frameCount; ++frame) {
        for (qsizetype channel = 0; channel < channelCount; ++channel) {
            const qsizetype sampleIndex = frame * channelCount + channel;
            const qsizetype byteOffset = sampleIndex * bytesPerSample;
            result.at(static_cast<std::size_t>(sampleIndex)) =
                normalizedSample(data, byteOffset, format.sampleFormat());
        }
    }

    return result;
}

std::optional<SpectrumFrame> NeniaSpectrumAnalyzer::analyzeWindow(
    const std::vector<float> &interleavedSamples,
    int channelCount,
    int sampleRate,
    qint64 centerTimeUs)
{
    if (channelCount <= 0 || sampleRate <= 0
        || interleavedSamples.size()
               < static_cast<std::size_t>(kSpectrumFftSize * channelCount)) {
        return std::nullopt;
    }

    const double nyquist = static_cast<double>(sampleRate) / 2.0;
    const double upperFrequency =
        std::min(static_cast<double>(kSpectrumUpperFrequencyHz), nyquist);
    const double lowerFrequency = kSpectrumLowerFrequencyHz;
    if (!(upperFrequency > lowerFrequency)) {
        return SpectrumFrame{centerTimeUs, {}};
    }

    std::array<double, kSpectrumFftSize / 2 + 1> averagePower{};
    const auto &window = hannWindow();
    const double normalization =
        2.0 / (static_cast<double>(hannWindowSum()) * hannWindowSum());

    for (int channel = 0; channel < channelCount; ++channel) {
        std::array<float, kSpectrumFftSize> windowed{};
        for (int index = 0; index < kSpectrumFftSize; ++index) {
            const std::size_t sampleIndex =
                static_cast<std::size_t>(index * channelCount + channel);
            const float sample = interleavedSamples.at(sampleIndex);
            windowed.at(static_cast<std::size_t>(index)) =
                (std::isfinite(sample) ? sample : 0.0f)
                * window.at(static_cast<std::size_t>(index));
        }

        const FftBins bins = forwardFft(windowed);
        for (int bin = 1; bin <= kSpectrumFftSize / 2; ++bin) {
            const double magnitudeSquared =
                static_cast<double>(std::norm(bins.at(static_cast<std::size_t>(bin))));
            const double power = magnitudeSquared * normalization;
            if (std::isfinite(power)) {
                averagePower.at(static_cast<std::size_t>(bin)) += power
                    / static_cast<double>(channelCount);
            }
        }
    }

    SpectrumFrame frame;
    frame.centerTimeUs = centerTimeUs;
    const double logRange = std::log(upperFrequency / lowerFrequency);
    std::array<double, kSpectrumBandCount> bandPower{};
    std::array<int, kSpectrumBandCount> bandBinCount{};

    for (int bin = 1; bin <= kSpectrumFftSize / 2; ++bin) {
        const double frequency = static_cast<double>(bin)
            * static_cast<double>(sampleRate)
            / static_cast<double>(kSpectrumFftSize);
        if (frequency < lowerFrequency || frequency > upperFrequency) {
            continue;
        }

        const double normalizedBand = std::log(frequency / lowerFrequency) / logRange;
        int band = static_cast<int>(std::floor(normalizedBand * kSpectrumBandCount));
        if (band < 0) {
            band = 0;
        } else if (band >= kSpectrumBandCount) {
            band = kSpectrumBandCount - 1;
        }
        bandPower.at(static_cast<std::size_t>(band))
            += averagePower.at(static_cast<std::size_t>(bin));
        ++bandBinCount.at(static_cast<std::size_t>(band));
    }

    for (int band = 0; band < kSpectrumBandCount; ++band) {
        const std::size_t index = static_cast<std::size_t>(band);
        if (bandBinCount.at(index) > 0) {
            bandPower.at(index) /= static_cast<double>(bandBinCount.at(index));
        }
        frame.bands.at(index) = levelFromPower(bandPower.at(index));
    }

    return frame;
}

std::vector<SpectrumFrame> NeniaSpectrumAnalyzer::processBuffer(
    const QAudioBuffer &buffer,
    std::optional<qint64> fallbackStartTimeUs)
{
    std::vector<SpectrumFrame> frames;
    if (!buffer.isValid() || buffer.frameCount() <= 0) {
        return frames;
    }

    const QAudioFormat format = buffer.format();
    const auto decoded = decodeInterleavedSamples(buffer);
    if (!decoded.has_value()) {
        resetStream();
        return frames;
    }

    const auto startTime = [&]() -> std::optional<qint64> {
        if (buffer.startTime() >= 0) {
            return buffer.startTime();
        }
        if (fallbackStartTimeUs.has_value() && fallbackStartTimeUs.value() >= 0) {
            return fallbackStartTimeUs;
        }
        return std::nullopt;
    };

    if (!m_hasStream || format != m_format) {
        resetStream();
        const auto initialStart = startTime();
        if (!initialStart.has_value()) {
            return frames;
        }
        beginStream(format, initialStart.value());
    } else if (buffer.startTime() >= 0) {
        const qint64 expectedStartTimeUs =
            sampleTimestamp(m_streamStartTimeUs, m_nextSampleIndex, format.sampleRate());
        const qint64 actualStartTimeUs = buffer.startTime();
        const qint64 difference = actualStartTimeUs - expectedStartTimeUs;
        if (difference > kTimestampContinuityToleranceUs
            || difference < -kTimestampContinuityToleranceUs) {
            resetStream();
            beginStream(format, actualStartTimeUs);
        }
    }

    const qint64 inputFrameCount = buffer.frameCount();
    if (inputFrameCount <= 0
        || inputFrameCount > std::numeric_limits<qint64>::max() - m_nextSampleIndex) {
        resetStream();
        return frames;
    }

    m_pendingInterleavedSamples.insert(m_pendingInterleavedSamples.end(),
                                       decoded->begin(), decoded->end());
    m_nextSampleIndex += inputFrameCount;

    const int channelCount = format.channelCount();
    const std::size_t samplesPerWindow =
        static_cast<std::size_t>(kSpectrumFftSize * channelCount);
    const std::size_t samplesPerHop = static_cast<std::size_t>(kSpectrumHopSize * channelCount);
    while (m_pendingInterleavedSamples.size() >= samplesPerWindow) {
        const qint64 centerSampleIndex =
            m_pendingStartSampleIndex + kSpectrumFftSize / 2;
        const qint64 centerTimeUs =
            sampleTimestamp(m_streamStartTimeUs, centerSampleIndex, format.sampleRate());
        const auto frame = analyzeWindow(m_pendingInterleavedSamples,
                                         channelCount,
                                         format.sampleRate(),
                                         centerTimeUs);
        if (frame.has_value()) {
            frames.push_back(frame.value());
        }

        m_pendingInterleavedSamples.erase(
            m_pendingInterleavedSamples.begin(),
            m_pendingInterleavedSamples.begin()
                + static_cast<std::ptrdiff_t>(samplesPerHop));
        m_pendingStartSampleIndex += kSpectrumHopSize;
    }

    return frames;
}

std::vector<SpectrumFrame> NeniaSpectrumAnalyzer::processPcmFrames(
    std::span<const float> interleavedSamples,
    int channelCount,
    int sampleRate,
    std::uint64_t startOutputFrame)
{
    std::vector<SpectrumFrame> frames;
    if (channelCount <= 0 || sampleRate <= 0 || interleavedSamples.empty()
        || interleavedSamples.size() % static_cast<std::size_t>(channelCount) != 0U) {
        resetStream();
        return frames;
    }

    const long double startUsExact = static_cast<long double>(startOutputFrame)
        * 1'000'000.0L / static_cast<long double>(sampleRate);
    if (startUsExact >= static_cast<long double>(std::numeric_limits<qint64>::max())) {
        resetStream();
        return frames;
    }
    const qint64 startTimeUs = static_cast<qint64>(std::llround(startUsExact));
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channelCount);
    format.setSampleFormat(QAudioFormat::Float);

    if (!m_hasStream || format != m_format) {
        resetStream();
        beginStream(format, startTimeUs);
    } else {
        const qint64 expectedStartTimeUs =
            sampleTimestamp(m_streamStartTimeUs, m_nextSampleIndex, sampleRate);
        const qint64 difference = startTimeUs - expectedStartTimeUs;
        if (difference > kTimestampContinuityToleranceUs
            || difference < -kTimestampContinuityToleranceUs) {
            resetStream();
            beginStream(format, startTimeUs);
        }
    }

    const std::size_t inputFrames =
        interleavedSamples.size() / static_cast<std::size_t>(channelCount);
    if (inputFrames > static_cast<std::size_t>(std::numeric_limits<qint64>::max())
        || static_cast<qint64>(inputFrames)
               > std::numeric_limits<qint64>::max() - m_nextSampleIndex) {
        resetStream();
        return frames;
    }
    m_pendingInterleavedSamples.insert(m_pendingInterleavedSamples.end(),
                                       interleavedSamples.begin(),
                                       interleavedSamples.end());
    m_nextSampleIndex += static_cast<qint64>(inputFrames);

    const std::size_t samplesPerWindow =
        static_cast<std::size_t>(kSpectrumFftSize * channelCount);
    const std::size_t samplesPerHop =
        static_cast<std::size_t>(kSpectrumHopSize * channelCount);
    while (m_pendingInterleavedSamples.size() >= samplesPerWindow) {
        const qint64 centerSampleIndex =
            m_pendingStartSampleIndex + kSpectrumFftSize / 2;
        const qint64 centerTimeUs =
            sampleTimestamp(m_streamStartTimeUs, centerSampleIndex, sampleRate);
        const auto frame = analyzeWindow(m_pendingInterleavedSamples,
                                         channelCount,
                                         sampleRate,
                                         centerTimeUs);
        if (frame.has_value()) {
            frames.push_back(frame.value());
        }
        m_pendingInterleavedSamples.erase(
            m_pendingInterleavedSamples.begin(),
            m_pendingInterleavedSamples.begin()
                + static_cast<std::ptrdiff_t>(samplesPerHop));
        m_pendingStartSampleIndex += kSpectrumHopSize;
    }
    return frames;
}

void NeniaSpectrumAnalyzer::resetStream() noexcept
{
    m_hasStream = false;
    m_format = {};
    m_streamStartTimeUs = 0;
    m_pendingStartSampleIndex = 0;
    m_nextSampleIndex = 0;
    m_pendingInterleavedSamples.clear();
}

qint64 NeniaSpectrumAnalyzer::sampleTimestamp(qint64 streamStartTimeUs,
                                              qint64 sampleIndex,
                                              int sampleRate) noexcept
{
    if (sampleIndex <= 0 || sampleRate <= 0) {
        return streamStartTimeUs;
    }

    const long double offsetUs = static_cast<long double>(sampleIndex) * 1'000'000.0L
        / static_cast<long double>(sampleRate);
    const long double maximum = static_cast<long double>(std::numeric_limits<qint64>::max());
    if (offsetUs >= maximum - static_cast<long double>(streamStartTimeUs)) {
        return std::numeric_limits<qint64>::max();
    }
    return streamStartTimeUs + static_cast<qint64>(std::llround(offsetUs));
}

bool NeniaSpectrumAnalyzer::isSupportedFormat(const QAudioFormat &format) noexcept
{
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
    case QAudioFormat::Int16:
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        return true;
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        return false;
    }
    return false;
}

void NeniaSpectrumAnalyzer::beginStream(const QAudioFormat &format, qint64 startTimeUs)
{
    m_hasStream = true;
    m_format = format;
    m_streamStartTimeUs = std::max<qint64>(startTimeUs, 0);
    m_pendingStartSampleIndex = 0;
    m_nextSampleIndex = 0;
    m_pendingInterleavedSamples.clear();
}

} // namespace nenia::detail
