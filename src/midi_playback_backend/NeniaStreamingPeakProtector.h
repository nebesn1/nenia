#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nenia::detail {

struct StreamingPeakProtectionSettings final {
    std::uint32_t sampleRateHz = 48'000;
    double boostDb = 0.0;
    double ceilingDbfs = -1.0;
    std::uint32_t lookaheadFrames = 240;
    double releaseMs = 100.0;
    std::size_t maximumInputBlockFrames = 2'048;
};

struct StreamingPeakProtectionStats final {
    std::uint64_t inputFramesConsumed = 0;
    std::uint64_t outputFramesProduced = 0;
    std::uint64_t nonFiniteInputSamples = 0;
    std::uint64_t potentialOverFullScaleSamples = 0;
    std::uint64_t finalSafetyClampSamples = 0;
    std::uint64_t limiterActiveFrames = 0;
    std::uint64_t limiterBypassFrames = 0;
    double maximumGainReductionDb = 0.0;
    std::size_t scratchBytes = 0;
};

struct StreamingProcessResult final {
    bool success = false;
    std::size_t framesConsumed = 0;
    std::size_t framesProduced = 0;
    std::size_t pendingBufferedFrames = 0;
};

// Private bounded-memory streaming MIDI peak protector. It is owned and
// invoked by the MIDI synthesis worker, never by the audio callback.
class StreamingPeakProtector final {
public:
    [[nodiscard]] bool prepare(const StreamingPeakProtectionSettings &settings,
                               QString *errorMessage = nullptr);
    void reset() noexcept;

    // Input and output are interleaved stereo Float32 spans. Each call accepts
    // at most maximumInputBlockFrames; results report actual consumption and
    // production so callers can retry unconsumed input after backpressure.
    [[nodiscard]] StreamingProcessResult process(
        std::span<const float> input,
        std::span<float> output,
        QString *errorMessage = nullptr);

    // Marks end-of-input and emits all queued frames in order. The caller may
    // call this repeatedly with smaller output spans until isDrained() is true.
    [[nodiscard]] StreamingProcessResult endOfStream(
        std::span<float> output,
        QString *errorMessage = nullptr);

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool isDrained() const noexcept;
    [[nodiscard]] bool hasEndedInput() const noexcept { return endOfInput_; }
    [[nodiscard]] std::size_t pendingBufferedFrames() const noexcept { return queuedFrames_; }
    [[nodiscard]] std::uint32_t inputLatencyFrames() const noexcept;
    [[nodiscard]] double inputLatencyMilliseconds() const noexcept;
    [[nodiscard]] std::size_t scratchBytes() const noexcept;
    [[nodiscard]] const StreamingPeakProtectionStats &stats() const noexcept { return stats_; }

private:
    struct WindowMinimum final {
        double score = 0.0;
        std::uint64_t frameIndex = 0;
    };

    [[nodiscard]] std::size_t queueIndex(std::size_t offset) const noexcept;
    [[nodiscard]] double targetGainForQueuedWindow() noexcept;
    void appendFrame(float left, float right) noexcept;
    void emitFrame(std::span<float> output, std::size_t outputFrame) noexcept;
    void setError(QString *errorMessage, const char *message) const;

    StreamingPeakProtectionSettings settings_;
    std::vector<float> queue_;
    std::vector<WindowMinimum> windowMinimumDeque_;
    std::size_t headFrame_ = 0;
    std::size_t queuedFrames_ = 0;
    std::size_t queueCapacityFrames_ = 0;
    std::size_t windowDequeHead_ = 0;
    std::size_t windowDequeSize_ = 0;
    double linearGain_ = 1.0;
    double ceiling_ = 1.0;
    double attackStep_ = 1.0;
    double releaseRetention_ = 0.0;
    double appliedGain_ = 1.0;
    double minimumAppliedGain_ = 1.0;
    bool prepared_ = false;
    bool endOfInput_ = false;
    StreamingPeakProtectionStats stats_;
};

} // namespace nenia::detail
