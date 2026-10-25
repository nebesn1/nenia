#include "NeniaStreamingPeakProtector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

namespace nenia::detail {
namespace {

constexpr double kPcmSafetyCeiling = 1.0;
constexpr double kGainEpsilon = 1.0e-7;
constexpr double kMaximumBoostDb = 15.0;
constexpr std::size_t kMaximumBlockFrames = 1U << 20U;

} // namespace

bool StreamingPeakProtector::prepare(
    const StreamingPeakProtectionSettings &settings,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (settings.sampleRateHz < 8'000U || settings.sampleRateHz > 192'000U
        || !std::isfinite(settings.boostDb)
        || settings.boostDb < 0.0 || settings.boostDb > kMaximumBoostDb
        || !std::isfinite(settings.ceilingDbfs)
        || settings.ceilingDbfs < -12.0 || settings.ceilingDbfs >= 0.0
        || settings.lookaheadFrames > settings.sampleRateHz / 100U
        || !std::isfinite(settings.releaseMs)
        || settings.releaseMs < 10.0 || settings.releaseMs > 500.0
        || settings.maximumInputBlockFrames == 0U
        || settings.maximumInputBlockFrames > kMaximumBlockFrames) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "Streaming peak-protection settings are outside bounds (boost must be 0..15 dB)");
        }
        prepared_ = false;
        queue_.clear();
        windowMinimumDeque_.clear();
        queueCapacityFrames_ = 0;
        queuedFrames_ = 0;
        windowDequeHead_ = 0;
        windowDequeSize_ = 0;
        return false;
    }

    try {
        settings_ = settings;
        queueCapacityFrames_ = static_cast<std::size_t>(settings.lookaheadFrames) + 1U;
        queue_.assign(queueCapacityFrames_ * 2U, 0.0F);
        windowMinimumDeque_.assign(queueCapacityFrames_, {});
    } catch (const std::bad_alloc &) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Streaming peak-protection queue allocation failed");
        }
        prepared_ = false;
        queue_.clear();
        windowMinimumDeque_.clear();
        queueCapacityFrames_ = 0;
        queuedFrames_ = 0;
        windowDequeSize_ = 0;
        return false;
    }

    linearGain_ = std::pow(10.0, settings.boostDb / 20.0);
    ceiling_ = std::pow(10.0, settings.ceilingDbfs / 20.0);
    attackStep_ = settings.lookaheadFrames == 0U
        ? 1.0 : 1.0 / static_cast<double>(settings.lookaheadFrames);
    const double releaseFrames = settings.releaseMs
        * static_cast<double>(settings.sampleRateHz) / 1'000.0;
    releaseRetention_ = std::exp(-1.0 / releaseFrames);
    prepared_ = true;
    reset();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

void StreamingPeakProtector::reset() noexcept
{
    headFrame_ = 0;
    queuedFrames_ = 0;
    windowDequeHead_ = 0;
    windowDequeSize_ = 0;
    appliedGain_ = 1.0;
    minimumAppliedGain_ = 1.0;
    endOfInput_ = false;
    stats_ = {};
    stats_.scratchBytes = queue_.capacity() * sizeof(float)
        + windowMinimumDeque_.capacity() * sizeof(WindowMinimum);
    std::fill(queue_.begin(), queue_.end(), 0.0F);
    std::fill(windowMinimumDeque_.begin(), windowMinimumDeque_.end(), WindowMinimum{});
}

StreamingProcessResult StreamingPeakProtector::process(
    std::span<const float> input,
    std::span<float> output,
    QString *errorMessage)
{
    StreamingProcessResult result;
    if (!prepared_) {
        setError(errorMessage, "Streaming peak protector has not been prepared");
        return result;
    }
    if (endOfInput_) {
        setError(errorMessage, "Cannot process input after end-of-stream");
        return result;
    }
    if ((input.size() % 2U) != 0U || (output.size() % 2U) != 0U
        || input.size() / 2U > settings_.maximumInputBlockFrames) {
        setError(errorMessage, "Streaming input/output must be stereo and input block must be within its bound");
        return result;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    result.success = true;
    const std::size_t inputFrames = input.size() / 2U;
    const std::size_t outputCapacityFrames = output.size() / 2U;
    while (true) {
        if (queuedFrames_ > settings_.lookaheadFrames) {
            if (result.framesProduced >= outputCapacityFrames) {
                break;
            }
            emitFrame(output, result.framesProduced);
            ++result.framesProduced;
            continue;
        }
        if (result.framesConsumed >= inputFrames) {
            break;
        }

        const std::size_t scalar = result.framesConsumed * 2U;
        float left = input[scalar];
        float right = input[scalar + 1U];
        if (!std::isfinite(left)) {
            left = 0.0F;
            ++stats_.nonFiniteInputSamples;
        }
        if (!std::isfinite(right)) {
            right = 0.0F;
            ++stats_.nonFiniteInputSamples;
        }
        appendFrame(left, right);
        ++result.framesConsumed;
        ++stats_.inputFramesConsumed;
    }

    stats_.outputFramesProduced += result.framesProduced;
    result.pendingBufferedFrames = queuedFrames_;
    return result;
}

StreamingProcessResult StreamingPeakProtector::endOfStream(
    std::span<float> output,
    QString *errorMessage)
{
    StreamingProcessResult result;
    if (!prepared_) {
        setError(errorMessage, "Streaming peak protector has not been prepared");
        return result;
    }
    if ((output.size() % 2U) != 0U) {
        setError(errorMessage, "Streaming output must contain complete stereo frames");
        return result;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    endOfInput_ = true;
    result.success = true;
    const std::size_t outputCapacityFrames = output.size() / 2U;
    while (queuedFrames_ > 0U && result.framesProduced < outputCapacityFrames) {
        emitFrame(output, result.framesProduced);
        ++result.framesProduced;
    }
    stats_.outputFramesProduced += result.framesProduced;
    result.pendingBufferedFrames = queuedFrames_;
    return result;
}

bool StreamingPeakProtector::isDrained() const noexcept
{
    return prepared_ && endOfInput_ && queuedFrames_ == 0U
        && stats_.inputFramesConsumed == stats_.outputFramesProduced;
}

std::uint32_t StreamingPeakProtector::inputLatencyFrames() const noexcept
{
    return prepared_ ? settings_.lookaheadFrames : 0U;
}

double StreamingPeakProtector::inputLatencyMilliseconds() const noexcept
{
    return prepared_
        ? 1'000.0 * static_cast<double>(settings_.lookaheadFrames)
            / static_cast<double>(settings_.sampleRateHz)
        : 0.0;
}

std::size_t StreamingPeakProtector::scratchBytes() const noexcept
{
    return queue_.capacity() * sizeof(float)
        + windowMinimumDeque_.capacity() * sizeof(WindowMinimum);
}

std::size_t StreamingPeakProtector::queueIndex(std::size_t offset) const noexcept
{
    return (headFrame_ + offset) % queueCapacityFrames_;
}

double StreamingPeakProtector::targetGainForQueuedWindow() noexcept
{
    if (windowDequeSize_ == 0U || queuedFrames_ == 0U) {
        return 1.0;
    }

    const std::uint64_t currentFrame = stats_.inputFramesConsumed
        - static_cast<std::uint64_t>(queuedFrames_);
    while (windowDequeSize_ > 0U
           && windowMinimumDeque_[windowDequeHead_].frameIndex < currentFrame) {
        windowDequeHead_ = (windowDequeHead_ + 1U) % queueCapacityFrames_;
        --windowDequeSize_;
    }
    if (windowDequeSize_ == 0U) {
        return 1.0;
    }
    const double offset = static_cast<double>(currentFrame) * attackStep_;
    return std::clamp(std::min(1.0,
        windowMinimumDeque_[windowDequeHead_].score - offset), 0.0, 1.0);
}

void StreamingPeakProtector::appendFrame(float left, float right) noexcept
{
    const std::uint64_t currentFrame = stats_.inputFramesConsumed
        - static_cast<std::uint64_t>(queuedFrames_);
    while (windowDequeSize_ > 0U
           && windowMinimumDeque_[windowDequeHead_].frameIndex < currentFrame) {
        windowDequeHead_ = (windowDequeHead_ + 1U) % queueCapacityFrames_;
        --windowDequeSize_;
    }

    const std::uint64_t frameIndex = stats_.inputFramesConsumed;
    const std::size_t tailFrame = queueIndex(queuedFrames_);
    queue_[tailFrame * 2U] = left;
    queue_[tailFrame * 2U + 1U] = right;
    ++queuedFrames_;

    const double peak = std::max(std::abs(static_cast<double>(left)),
                                 std::abs(static_cast<double>(right))) * linearGain_;
    const double required = peak > ceiling_ ? ceiling_ / peak : 1.0;
    const double score = required + static_cast<double>(frameIndex) * attackStep_;
    while (windowDequeSize_ > 0U) {
        const std::size_t back = (windowDequeHead_ + windowDequeSize_ - 1U)
            % queueCapacityFrames_;
        if (windowMinimumDeque_[back].score <= score) {
            break;
        }
        --windowDequeSize_;
    }
    const std::size_t tail = (windowDequeHead_ + windowDequeSize_)
        % queueCapacityFrames_;
    windowMinimumDeque_[tail] = {score, frameIndex};
    ++windowDequeSize_;
}

void StreamingPeakProtector::emitFrame(
    std::span<float> output,
    std::size_t outputFrame) noexcept
{
    double target = targetGainForQueuedWindow();
    // The reference algorithm stores its attack envelope in Float32 before
    // applying the release recurrence; retain that quantization for a direct
    // numerical comparison, independent of caller chunk boundaries.
    target = static_cast<double>(static_cast<float>(target));
    if (target < appliedGain_) {
        appliedGain_ = target;
    } else {
        appliedGain_ = target + (appliedGain_ - target) * releaseRetention_;
    }
    appliedGain_ = std::clamp(appliedGain_, 0.0, 1.0);

    if (appliedGain_ < minimumAppliedGain_) {
        minimumAppliedGain_ = appliedGain_;
        stats_.maximumGainReductionDb = minimumAppliedGain_ > 0.0
            ? -20.0 * std::log10(minimumAppliedGain_) : 0.0;
    }
    if (appliedGain_ < 1.0 - kGainEpsilon) {
        ++stats_.limiterActiveFrames;
    } else {
        ++stats_.limiterBypassFrames;
    }

    const std::size_t slot = queueIndex(0U) * 2U;
    const std::size_t outputScalar = outputFrame * 2U;
    const std::array<float, 2> source{queue_[slot], queue_[slot + 1U]};
    for (std::size_t channel = 0; channel < source.size(); ++channel) {
        const double candidate = static_cast<double>(source[channel])
            * linearGain_ * appliedGain_;
        if (std::abs(candidate) > kPcmSafetyCeiling) {
            ++stats_.potentialOverFullScaleSamples;
        }
        const double clamped = std::clamp(
            candidate, -kPcmSafetyCeiling, kPcmSafetyCeiling);
        if (clamped != candidate) {
            ++stats_.finalSafetyClampSamples;
        }
        output[outputScalar + channel] = std::isfinite(clamped)
            ? static_cast<float>(clamped) : 0.0F;
    }

    headFrame_ = (headFrame_ + 1U) % queueCapacityFrames_;
    --queuedFrames_;
}

void StreamingPeakProtector::setError(
    QString *errorMessage,
    const char *message) const
{
    if (errorMessage != nullptr) {
        *errorMessage = QString::fromLatin1(message);
    }
}

} // namespace nenia::detail
