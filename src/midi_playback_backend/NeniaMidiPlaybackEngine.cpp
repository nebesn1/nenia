#include "NeniaMidiPlaybackEngine.h"

#include "NeniaMidiSequence.h"
#include "NeniaMidiSequencer.h"
#include "NeniaStreamingPeakProtector.h"
#include "../NeniaMidiAudioTrace.h"
#include "NeniaSoundFontSynthesizer.h"

#include <QAudioSink>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMetaObject>
#include <QTimer>
#include <QThread>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace nenia::detail {
namespace {

constexpr std::size_t kPcmRingCapacityFrames = 16'384;
constexpr std::size_t kInitialPrebufferFrames = 4'096;
constexpr std::size_t kLowWaterFrames = 2'048;
constexpr std::size_t kHighWaterFrames = kLowWaterFrames * 4U;
constexpr std::size_t kRenderBlockFrames = 256;
constexpr std::size_t kAudioSinkBufferFrames = 2'048;
constexpr std::uint64_t kMaximumTailSeconds = 2;
constexpr double kPcmTraceSquareScale = 1'000'000'000.0;

struct AudioChoice {
    QAudioDevice device;
    QAudioFormat format;
};

bool chooseAudioOutput(AudioChoice *choice, QString *errorMessage)
{
    if (choice == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("audio format output is null");
        }
        return false;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Qt reports no default audio output device");
        }
        return false;
    }

    constexpr std::array<int, 2> rates{48'000, 44'100};
    constexpr std::array<QAudioFormat::SampleFormat, 2> formats{
        QAudioFormat::Float, QAudioFormat::Int16};
    for (QAudioFormat::SampleFormat sampleFormat : formats) {
        if (!device.supportedSampleFormats().contains(sampleFormat)) {
            continue;
        }
        for (int rate : rates) {
            QAudioFormat format;
            format.setSampleRate(rate);
            format.setChannelCount(2);
            format.setSampleFormat(sampleFormat);
            if (device.isFormatSupported(format)) {
                choice->device = device;
                choice->format = format;
                return true;
            }
        }
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral(
            "default output does not support stereo Float32 or Int16 at 48 kHz or 44.1 kHz");
    }
    return false;
}

QString formatDescription(const QAudioFormat &format)
{
    const QString sampleFormat = format.sampleFormat() == QAudioFormat::Float
        ? QStringLiteral("Float32") : QStringLiteral("Int16");
    return QStringLiteral("%1 Hz, stereo, %2")
        .arg(format.sampleRate()).arg(sampleFormat);
}

struct PcmTraceBlock final {
    std::uint64_t samples = 0;
    std::uint64_t finiteSamples = 0;
    std::uint64_t nonFiniteSamples = 0;
    std::uint64_t overrangeSamples = 0;
    double sumSquares = 0.0;
    float peakAbs = 0.0F;

    void add(float sample) noexcept
    {
        ++samples;
        if (!std::isfinite(sample)) {
            ++nonFiniteSamples;
            return;
        }
        ++finiteSamples;
        const float absolute = std::abs(sample);
        peakAbs = std::max(peakAbs, absolute);
        sumSquares += static_cast<double>(sample) * sample;
        if (absolute > 1.0F) {
            ++overrangeSamples;
        }
    }
};

struct PcmTraceSnapshot final {
    std::uint64_t samples = 0;
    std::uint64_t finiteSamples = 0;
    std::uint64_t nonFiniteSamples = 0;
    std::uint64_t overrangeSamples = 0;
    std::uint64_t sumSquaresScaled = 0;
    float peakAbs = 0.0F;

    [[nodiscard]] double rms() const noexcept
    {
        return finiteSamples == 0
            ? 0.0
            : std::sqrt(static_cast<double>(sumSquaresScaled)
                        / kPcmTraceSquareScale / static_cast<double>(finiteSamples));
    }
};

std::uint64_t scaledSumSquares(double sumSquares) noexcept
{
    const double maximum = static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    if (!std::isfinite(sumSquares) || sumSquares >= maximum / kPcmTraceSquareScale) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(sumSquares * kPcmTraceSquareScale);
}

struct AtomicPcmTraceStage final {
    std::atomic<std::uint64_t> samples{0};
    std::atomic<std::uint64_t> finiteSamples{0};
    std::atomic<std::uint64_t> nonFiniteSamples{0};
    std::atomic<std::uint64_t> overrangeSamples{0};
    std::atomic<std::uint64_t> sumSquaresScaled{0};
    std::atomic<std::uint32_t> peakAbsBits{0};

    void add(const PcmTraceBlock &block) noexcept
    {
        samples.fetch_add(block.samples, std::memory_order_relaxed);
        finiteSamples.fetch_add(block.finiteSamples, std::memory_order_relaxed);
        nonFiniteSamples.fetch_add(block.nonFiniteSamples, std::memory_order_relaxed);
        overrangeSamples.fetch_add(block.overrangeSamples, std::memory_order_relaxed);
        sumSquaresScaled.fetch_add(scaledSumSquares(block.sumSquares),
                                   std::memory_order_relaxed);
        const std::uint32_t candidate = std::bit_cast<std::uint32_t>(block.peakAbs);
        std::uint32_t current = peakAbsBits.load(std::memory_order_relaxed);
        while (candidate > current
               && !peakAbsBits.compare_exchange_weak(current, candidate,
                                                     std::memory_order_relaxed)) {
        }
    }

    [[nodiscard]] PcmTraceSnapshot load() const noexcept
    {
        return {samples.load(std::memory_order_relaxed),
                finiteSamples.load(std::memory_order_relaxed),
                nonFiniteSamples.load(std::memory_order_relaxed),
                overrangeSamples.load(std::memory_order_relaxed),
                sumSquaresScaled.load(std::memory_order_relaxed),
                std::bit_cast<float>(peakAbsBits.load(std::memory_order_relaxed))};
    }

    [[nodiscard]] PcmTraceSnapshot take() noexcept
    {
        return {samples.exchange(0, std::memory_order_relaxed),
                finiteSamples.exchange(0, std::memory_order_relaxed),
                nonFiniteSamples.exchange(0, std::memory_order_relaxed),
                overrangeSamples.exchange(0, std::memory_order_relaxed),
                sumSquaresScaled.exchange(0, std::memory_order_relaxed),
                std::bit_cast<float>(peakAbsBits.exchange(0, std::memory_order_relaxed))};
    }

    void reset() noexcept
    {
        samples.store(0, std::memory_order_relaxed);
        finiteSamples.store(0, std::memory_order_relaxed);
        nonFiniteSamples.store(0, std::memory_order_relaxed);
        overrangeSamples.store(0, std::memory_order_relaxed);
        sumSquaresScaled.store(0, std::memory_order_relaxed);
        peakAbsBits.store(0, std::memory_order_relaxed);
    }
};

std::uint64_t millisecondsToFrames(qint64 milliseconds, std::uint32_t sampleRate)
{
    const std::uint64_t wholeSeconds = static_cast<std::uint64_t>(milliseconds / 1000);
    const std::uint64_t remainderMs = static_cast<std::uint64_t>(milliseconds % 1000);
    if (wholeSeconds > std::numeric_limits<std::uint64_t>::max() / sampleRate) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t wholeFrames = wholeSeconds * sampleRate;
    const std::uint64_t fractionFrames = (remainderMs * sampleRate + 500U) / 1000U;
    if (fractionFrames > std::numeric_limits<std::uint64_t>::max() - wholeFrames) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return wholeFrames + fractionFrames;
}

qint64 framesToMilliseconds(std::uint64_t frames, std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return 0;
    }
    const std::uint64_t wholeSeconds = frames / sampleRate;
    const std::uint64_t remainderFrames = frames % sampleRate;
    if (wholeSeconds > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()) / 1000U) {
        return std::numeric_limits<qint64>::max();
    }
    const std::uint64_t milliseconds = wholeSeconds * 1000U
        + (remainderFrames * 1000U + sampleRate / 2U) / sampleRate;
    return milliseconds > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
        ? std::numeric_limits<qint64>::max() : static_cast<qint64>(milliseconds);
}

} // namespace

const std::array<NeniaMidiLoudnessPresetDefinition, 7> &
neniaMidiLoudnessPresetDefinitions() noexcept
{
    static constexpr std::array<NeniaMidiLoudnessPresetDefinition, 7> definitions{{
        {NeniaMidiLoudnessPreset::A, "A", 0.20, 0.0, 0.0, false},
        {NeniaMidiLoudnessPreset::B, "B", 0.40, 0.0, 0.0, false},
        {NeniaMidiLoudnessPreset::C, "C", 0.40, 3.0, -1.0, true},
        {NeniaMidiLoudnessPreset::D, "D", 0.40, 6.0, -1.0, true},
        {NeniaMidiLoudnessPreset::E, "E", 0.40, 9.0, -1.0, true},
        {NeniaMidiLoudnessPreset::F, "F", 0.40, 12.0, -1.0, true},
        {NeniaMidiLoudnessPreset::H, "H", 0.40, 15.0, -1.0, true},
    }};
    return definitions;
}

const NeniaMidiLoudnessPresetDefinition &
neniaMidiLoudnessPresetDefinition(NeniaMidiLoudnessPreset preset) noexcept
{
    const auto &definitions = neniaMidiLoudnessPresetDefinitions();
    const auto found = std::find_if(definitions.cbegin(), definitions.cend(),
                                    [preset](const auto &definition) {
        return definition.preset == preset;
    });
    if (found != definitions.cend()) {
        return *found;
    }
    return *std::find_if(definitions.cbegin(), definitions.cend(), [](const auto &definition) {
        return definition.preset == NeniaMidiLoudnessPreset::E;
    });
}

NeniaMidiLoudnessStartupConfiguration
resolveNeniaMidiLoudnessStartupConfiguration(
    const QByteArray &policyValue,
    const QByteArray &trialValue,
    const QByteArray &gainAbValue,
    const QByteArray &experimentalGainValue,
    bool experimentalGainWasSet,
    NeniaMidiLoudnessPreset preferredUserPreset)
{
    constexpr double legacyGain = 0.20;
    constexpr std::array<double, 6> allowedGains{0.20, 0.28, 0.30, 0.32, 0.36, 0.40};
    const bool trialRequested = trialValue == QByteArrayLiteral("1");
    const bool gainAbRequested = gainAbValue == QByteArrayLiteral("1");
    const bool gainOverrideRequested = experimentalGainWasSet
        || !experimentalGainValue.isEmpty();
    const bool developerOverrideRequested =
        trialRequested || gainAbRequested || gainOverrideRequested;

    NeniaMidiLoudnessStartupConfiguration result;
    result.explicitPolicy = !policyValue.isEmpty();
    result.developerTrialRequested = trialRequested;

    const auto selectLegacy = [&result] {
        result.mode = NeniaMidiLoudnessStartupMode::Legacy;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.synthGain = 0.20;
        result.requestedGainText = QStringLiteral("(legacy-policy)");
        result.productionHEnabled = false;
        result.developerTrialEnabled = false;
        result.gainAbEnabled = false;
        result.streamingProtectionEnabled = false;
    };

    const auto selectProductionPreset = [&result, preferredUserPreset] {
        result.mode = NeniaMidiLoudnessStartupMode::ProductionH;
        result.initialPreset = result.explicitPolicy
            ? NeniaMidiLoudnessPreset::H : NeniaMidiLoudnessPreset::E;
        if (!result.explicitPolicy) {
            const auto &definitions = neniaMidiLoudnessPresetDefinitions();
            const auto selected = std::find_if(
                definitions.cbegin(), definitions.cend(),
                [preferredUserPreset](const auto &definition) {
                    return definition.preset == preferredUserPreset;
                });
            if (selected != definitions.cend()) {
                result.initialPreset = selected->preset;
            } else {
                result.warningMessage = QStringLiteral(
                    "invalid saved MIDI loudness preset; using the default E preset");
            }
        }
        const auto &definition = neniaMidiLoudnessPresetDefinition(result.initialPreset);
        result.synthGain = definition.synthGain;
        result.requestedGainText = result.explicitPolicy
            ? QStringLiteral("(production-H)")
            : QStringLiteral("(user-preset-%1)").arg(QLatin1String(definition.name));
        result.productionHEnabled = true;
        result.developerTrialEnabled = false;
        result.gainAbEnabled = false;
        result.streamingProtectionEnabled = true;
        result.userPresetSelectionEnabled = !result.explicitPolicy;
    };

    if (result.explicitPolicy
        && policyValue != QByteArrayLiteral("h")
        && policyValue != QByteArrayLiteral("legacy")) {
        result.valid = false;
        result.mode = NeniaMidiLoudnessStartupMode::ProductionH;
        result.productionHEnabled = false;
        result.streamingProtectionEnabled = false;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.synthGain = legacyGain;
        result.requestedGainText = QStringLiteral("(invalid-policy)");
        result.errorMessage = QStringLiteral(
            "invalid NENIA_MIDI_LOUDNESS_POLICY='%1'; supported values are 'h' and 'legacy' (or unset)")
                                  .arg(QString::fromLocal8Bit(policyValue));
        return result;
    }

    if (result.explicitPolicy && developerOverrideRequested) {
        result.valid = false;
        result.productionHEnabled = false;
        result.streamingProtectionEnabled = false;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.synthGain = legacyGain;
        result.requestedGainText = QStringLiteral("(conflicting-startup-flags)");
        result.errorMessage = QStringLiteral(
            "NENIA_MIDI_LOUDNESS_POLICY cannot be combined with developer MIDI loudness flags; unset NENIA_MIDI_LOUDNESS_TRIAL, NENIA_MIDI_GAIN_AB, and NENIA_EXPERIMENTAL_MIDI_GAIN");
        return result;
    }

    if (result.explicitPolicy) {
        if (policyValue == QByteArrayLiteral("legacy")) {
            selectLegacy();
            return result;
        }
        selectProductionPreset();
        return result;
    }

    if (trialRequested && gainAbRequested) {
        result.mode = NeniaMidiLoudnessStartupMode::DeveloperConflict;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.synthGain = legacyGain;
        result.requestedGainText = QStringLiteral("(conflicting-developer-flags)");
        result.productionHEnabled = false;
        result.developerTrialEnabled = false;
        result.developerFlagConflict = true;
        result.gainAbEnabled = false;
        result.streamingProtectionEnabled = false;
        result.errorMessage = QStringLiteral(
            "conflicting developer flags: NENIA_MIDI_GAIN_AB and NENIA_MIDI_LOUDNESS_TRIAL cannot both be enabled");
        return result;
    }

    if (trialRequested) {
        result.mode = NeniaMidiLoudnessStartupMode::DeveloperTrial;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.synthGain = legacyGain;
        result.requestedGainText = QStringLiteral("(ignored-by-loudness-trial-preset-A)");
        result.productionHEnabled = false;
        result.developerTrialEnabled = true;
        result.gainAbEnabled = false;
        result.streamingProtectionEnabled = true;
        if (gainOverrideRequested) {
            result.warningMessage = QStringLiteral(
                "NENIA_EXPERIMENTAL_MIDI_GAIN is intentionally ignored while the loudness trial starts at preset A");
        }
        return result;
    }

    if (gainAbRequested || gainOverrideRequested) {
        result.mode = NeniaMidiLoudnessStartupMode::DeveloperGain;
        result.initialPreset = NeniaMidiLoudnessPreset::A;
        result.productionHEnabled = false;
        result.streamingProtectionEnabled = false;
        result.gainAbEnabled = gainAbRequested;
        if (!gainOverrideRequested) {
            result.synthGain = legacyGain;
            result.requestedGainText = QStringLiteral("(unset-or-empty)");
            return result;
        }

        const QString gainText = QString::fromLocal8Bit(experimentalGainValue);
        result.requestedGainText = gainText;
        bool conversionSucceeded = false;
        const double parsedGain = gainText.toDouble(&conversionSucceeded);
        if (conversionSucceeded && std::isfinite(parsedGain)) {
            const auto selected = std::find(allowedGains.cbegin(), allowedGains.cend(), parsedGain);
            if (selected != allowedGains.cend()) {
                result.synthGain = *selected;
                return result;
            }
        }
        result.synthGain = legacyGain;
        result.warningMessage = QStringLiteral(
            "invalid NENIA_EXPERIMENTAL_MIDI_GAIN='%1'; using the historical 0.20 fallback")
                                   .arg(gainText);
        return result;
    }

    selectProductionPreset();
    return result;
}

namespace {

void atomicAddMaximum(std::atomic<std::uint64_t> &target,
                      std::uint64_t candidate) noexcept
{
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (candidate > current
           && !target.compare_exchange_weak(current, candidate,
                                            std::memory_order_relaxed)) {
    }
}

void atomicAddMaximum(std::atomic<double> &target, double candidate) noexcept
{
    double current = target.load(std::memory_order_relaxed);
    while (candidate > current
           && !target.compare_exchange_weak(current, candidate,
                                            std::memory_order_relaxed)) {
    }
}

} // namespace

struct NeniaMidiPlaybackEngine::TraceCounters final {
    AtomicPcmTraceStage preClamp;
    AtomicPcmTraceStage postClamp;
    AtomicPcmTraceStage sinkInput;
    std::atomic<std::uint64_t> sinkDeliveredFrames{0};
    std::atomic<std::uint64_t> sinkRingFrames{0};
    std::atomic<std::uint64_t> sinkSilenceFrames{0};

    void addProducer(const PcmTraceBlock &beforeClamp,
                     const PcmTraceBlock &afterClamp) noexcept
    {
        preClamp.add(beforeClamp);
        postClamp.add(afterClamp);
    }

    void addSink(const PcmTraceBlock &samples,
                 std::uint64_t deliveredFrames,
                 std::uint64_t ringFrames,
                 std::uint64_t silenceFrames) noexcept
    {
        sinkInput.add(samples);
        sinkDeliveredFrames.fetch_add(deliveredFrames, std::memory_order_relaxed);
        sinkRingFrames.fetch_add(ringFrames, std::memory_order_relaxed);
        sinkSilenceFrames.fetch_add(silenceFrames, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        preClamp.reset();
        postClamp.reset();
        sinkInput.reset();
        sinkDeliveredFrames.store(0, std::memory_order_relaxed);
        sinkRingFrames.store(0, std::memory_order_relaxed);
        sinkSilenceFrames.store(0, std::memory_order_relaxed);
    }
};

struct NeniaMidiPlaybackEngine::Worker final {
    enum class CommandType {
        Prepare, Start, Pause, Stop, Seek, SetGain, SetLoudnessPreset, Shutdown
    };

    struct Reply {
        bool ok = false;
        QString error;
        quint64 requestId = 0;
        double requestedGain = 0.0;
        double previousSelectedGain = 0.0;
        double previousEffectiveGain = 0.0;
        double effectiveGain = 0.0;
        NeniaMidiLoudnessPreset loudnessPreset = NeniaMidiLoudnessPreset::A;
        NeniaMidiLoudnessPreset previousLoudnessPreset = NeniaMidiLoudnessPreset::A;
        double loudnessSynthGain = 0.20;
        double loudnessPostGainDb = 0.0;
        bool loudnessLimiterEnabled = false;
        int activeVoicesBefore = 0;
        int activeVoicesAfter = 0;
        std::uint64_t workerIdentity = 0;
        QString sourceBasename;
        qint64 requestedPositionMs = 0;
        float requestedMasterVolume = 0.0f;
        float requestedSinkVolume = 0.0f;
    };

    struct Command {
        CommandType type = CommandType::Pause;
        QString soundFontPath;
        std::shared_ptr<const NeniaMidiSequence> sequence;
        std::uint32_t sampleRate = 0;
        std::uint64_t seekFrame = 0;
        bool activateAfterSeek = false;
        double gain = 0.0;
        NeniaMidiLoudnessPreset loudnessPreset = NeniaMidiLoudnessPreset::A;
        Reply gainRequestContext;
        std::shared_ptr<std::promise<Reply>> completion;
    };

    explicit Worker(NeniaMidiPlaybackEngine *engine)
        : owner(engine)
    {
    }

    void start() { thread = std::thread([this] { run(); }); }

    ~Worker()
    {
        if (thread.joinable()) {
            Command shutdown;
            shutdown.type = CommandType::Shutdown;
            (void)submit(std::move(shutdown));
            thread.join();
        }
    }

    Reply submit(Command command)
    {
        auto completion = std::make_shared<std::promise<Reply>>();
        std::future<Reply> result = completion->get_future();
        command.completion = std::move(completion);
        {
            std::lock_guard lock(commandMutex);
            commands.push_back(std::move(command));
        }
        commandAvailable.notify_one();
        return result.get();
    }

    bool enqueue(Command command)
    {
        if (command.type == CommandType::Shutdown) {
            return false;
        }
        {
            std::lock_guard lock(commandMutex);
            commands.push_back(std::move(command));
        }
        commandAvailable.notify_one();
        return true;
    }

    void notifyBufferReady()
    {
        bufferReady.notify_all();
    }

    void run()
    {
        std::unique_ptr<NeniaSoundFontSynthesizer> synthesizer;
        std::unique_ptr<NeniaMidiSequencer> sequencer;
        std::vector<float> floatPcm(kRenderBlockFrames * 2U, 0.0F);
        std::vector<float> processedPcm(owner->m_streamingProtectionEnabled
                                            ? kRenderBlockFrames * 2U : 0U,
                                        0.0F);
        std::vector<std::int16_t> int16Pcm(kRenderBlockFrames * 2U, 0);
        std::unique_ptr<StreamingPeakProtector> peakProtector;
        NeniaMidiLoudnessPreset activeLoudnessPreset = NeniaMidiLoudnessPreset::A;
        std::uint64_t accumulatedLimiterActiveFrames = 0;
        std::uint64_t accumulatedPotentialOverrangeSamples = 0;
        std::uint64_t accumulatedSafetyClampSamples = 0;
        double maximumGainReductionDb = 0.0;
        bool prepared = false;
        bool renderActive = false;
        bool refillActive = false;
        bool renderFinished = false;
        bool tailStarted = false;
        std::uint64_t tailFramesRemaining = 0;
        std::uint32_t sampleRate = 0;
        QString soundFontPath;
        const bool measureWorkerTime = owner->m_streamingProtectionEnabled
            || owner->m_audioTraceEnabled;

        const auto resetTrialCounters = [&] {
            accumulatedLimiterActiveFrames = 0;
            accumulatedPotentialOverrangeSamples = 0;
            accumulatedSafetyClampSamples = 0;
            maximumGainReductionDb = 0.0;
            owner->m_trialInputFrames.store(0, std::memory_order_release);
            owner->m_trialOutputFrames.store(0, std::memory_order_release);
            owner->m_trialPendingFrames.store(0, std::memory_order_release);
            owner->m_trialMaximumGainReductionDb.store(0.0, std::memory_order_release);
            owner->m_trialLimiterActiveFrames.store(0, std::memory_order_release);
            owner->m_trialPotentialOverrangeSamples.store(0, std::memory_order_release);
            owner->m_trialSafetyClampSamples.store(0, std::memory_order_release);
            owner->m_trialNonFiniteSamples.store(0, std::memory_order_release);
            owner->m_trialTimingDiscontinuities.store(0, std::memory_order_release);
            owner->m_trialRingWriteFailures.store(0, std::memory_order_release);
            owner->m_trialLookaheadFrames.store(0, std::memory_order_release);
            owner->m_trialScratchBytes.store(0, std::memory_order_release);
            owner->m_trialSynthesisNanoseconds.store(0, std::memory_order_release);
            owner->m_trialSynthesisWorstNanoseconds.store(0, std::memory_order_release);
            owner->m_trialSynthesisBlocks.store(0, std::memory_order_release);
            owner->m_trialDspNanoseconds.store(0, std::memory_order_release);
            owner->m_trialDspWorstNanoseconds.store(0, std::memory_order_release);
            owner->m_trialDspBlocks.store(0, std::memory_order_release);
            owner->m_trialMaximumRingOccupancyFrames.store(0, std::memory_order_release);
        };

        const auto collectProtectorStats = [&] {
            if (peakProtector == nullptr) {
                return;
            }
            const StreamingPeakProtectionStats &stats = peakProtector->stats();
            owner->m_trialLimiterActiveFrames.store(
                accumulatedLimiterActiveFrames + stats.limiterActiveFrames,
                std::memory_order_release);
            owner->m_trialPotentialOverrangeSamples.store(
                accumulatedPotentialOverrangeSamples + stats.potentialOverFullScaleSamples,
                std::memory_order_release);
            owner->m_trialSafetyClampSamples.store(
                accumulatedSafetyClampSamples + stats.finalSafetyClampSamples,
                std::memory_order_release);
            atomicAddMaximum(owner->m_trialMaximumGainReductionDb,
                             stats.maximumGainReductionDb);
            owner->m_trialScratchBytes.store(stats.scratchBytes, std::memory_order_release);
            owner->m_trialPendingFrames.store(peakProtector->pendingBufferedFrames(),
                                             std::memory_order_release);
        };

        const auto foldProtectorStats = [&] {
            if (peakProtector == nullptr) {
                return;
            }
            const StreamingPeakProtectionStats &stats = peakProtector->stats();
            accumulatedLimiterActiveFrames += stats.limiterActiveFrames;
            accumulatedPotentialOverrangeSamples += stats.potentialOverFullScaleSamples;
            accumulatedSafetyClampSamples += stats.finalSafetyClampSamples;
            maximumGainReductionDb = std::max(maximumGainReductionDb,
                                               stats.maximumGainReductionDb);
            owner->m_trialLimiterActiveFrames.store(accumulatedLimiterActiveFrames,
                                                    std::memory_order_release);
            owner->m_trialPotentialOverrangeSamples.store(
                accumulatedPotentialOverrangeSamples, std::memory_order_release);
            owner->m_trialSafetyClampSamples.store(accumulatedSafetyClampSamples,
                                                   std::memory_order_release);
            atomicAddMaximum(owner->m_trialMaximumGainReductionDb,
                             maximumGainReductionDb);
            owner->m_trialScratchBytes.store(stats.scratchBytes, std::memory_order_release);
            owner->m_trialPendingFrames.store(0, std::memory_order_release);
        };

        const auto makeProtector = [&](NeniaMidiLoudnessPreset preset,
                                       std::unique_ptr<StreamingPeakProtector> *target,
                                       QString *error) {
            const auto &definition = neniaMidiLoudnessPresetDefinition(preset);
            if (!owner->m_streamingProtectionEnabled || !definition.limiterEnabled) {
                target->reset();
                return true;
            }
            auto candidate = std::make_unique<StreamingPeakProtector>();
            StreamingPeakProtectionSettings settings;
            settings.sampleRateHz = sampleRate;
            settings.boostDb = definition.postSynthGainDb;
            settings.ceilingDbfs = definition.ceilingDbfs;
            settings.lookaheadFrames = static_cast<std::uint32_t>(
                std::llround(static_cast<double>(sampleRate) * 0.005));
            settings.releaseMs = 100.0;
            settings.maximumInputBlockFrames = kRenderBlockFrames;
            if (!candidate->prepare(settings, error)) {
                return false;
            }
            *target = std::move(candidate);
            return true;
        };

        const auto publishOutput = [&](std::size_t frames,
                                       std::vector<float> &pcm,
                                       QString *error) {
            if (frames == 0U) {
                return true;
            }
            const std::uint64_t firstOutputFrame =
                owner->m_baseFrame.load(std::memory_order_acquire)
                + owner->m_generatedFrames.load(std::memory_order_acquire);
            if (writePcm(frames, pcm, int16Pcm) != frames) {
                owner->m_trialRingWriteFailures.fetch_add(1, std::memory_order_relaxed);
                if (error != nullptr) {
                    *error = QStringLiteral("PCM ring could not accept a complete processed block");
                }
                return false;
            }
            (void)owner->m_analysisTap->publish(
                std::span<const float>(pcm.data(), frames * 2U), firstOutputFrame,
                owner->m_analysisGeneration.load(std::memory_order_acquire), sampleRate);
            owner->m_generatedFrames.fetch_add(frames, std::memory_order_acq_rel);
            if (owner->m_streamingProtectionEnabled) {
                const std::uint64_t outputFrames =
                    owner->m_trialOutputFrames.fetch_add(frames, std::memory_order_acq_rel) + frames;
                const std::uint64_t inputFrames =
                    owner->m_trialInputFrames.load(std::memory_order_acquire);
                if (outputFrames > inputFrames) {
                    owner->m_trialTimingDiscontinuities.fetch_add(1, std::memory_order_relaxed);
                    owner->m_trialPendingFrames.store(0, std::memory_order_release);
                } else {
                    const std::uint64_t expectedPending = inputFrames - outputFrames;
                    const std::uint64_t actualPending = peakProtector == nullptr
                        ? 0 : peakProtector->pendingBufferedFrames();
                    if (expectedPending != actualPending) {
                        owner->m_trialTimingDiscontinuities.fetch_add(1, std::memory_order_relaxed);
                    }
                    owner->m_trialPendingFrames.store(actualPending, std::memory_order_release);
                }
                if (peakProtector != nullptr) {
                    collectProtectorStats();
                }
            }
            if (measureWorkerTime) {
                const std::size_t occupancy = owner->m_pcmRing->queuedFrames();
                atomicAddMaximum(owner->m_trialMaximumRingOccupancyFrames,
                                 static_cast<std::uint64_t>(occupancy));
            }
            notifyBufferReady();
            return true;
        };

        for (;;) {
            Command command;
            bool hasCommand = false;
            {
                std::unique_lock lock(commandMutex);
                commandAvailable.wait_for(lock, std::chrono::milliseconds(5), [&] {
                    return !commands.empty()
                        || (renderActive && !renderFinished && owner->m_pcmRing != nullptr
                            && (refillActive
                                    ? owner->m_pcmRing->queuedFrames() < kHighWaterFrames
                                    : owner->m_pcmRing->queuedFrames() <= kLowWaterFrames));
                });
                if (!commands.empty()) {
                    command = std::move(commands.front());
                    commands.pop_front();
                    hasCommand = true;
                }
            }

            if (hasCommand) {
                Reply reply;
                bool exitRequested = false;
                switch (command.type) {
                case CommandType::Prepare: {
                    owner->m_analysisGeneration.fetch_add(1, std::memory_order_acq_rel);
                    synthesizer.reset();
                    sequencer.reset();
                    peakProtector.reset();
                    resetTrialCounters();
                    owner->m_activeSynthGain.store(0.0, std::memory_order_release);
                    owner->m_settingsSynthGain.store(0.0, std::memory_order_release);
                    prepared = false;
                    renderActive = false;
                    refillActive = false;
                    renderFinished = false;
                    tailStarted = false;
                    soundFontPath = command.soundFontPath;
                    sampleRate = command.sampleRate;
                    activeLoudnessPreset = command.loudnessPreset;
                    std::unique_ptr<StreamingPeakProtector> candidateProtector;
                    if (!makeProtector(activeLoudnessPreset, &candidateProtector,
                                       &reply.error)) {
                        break;
                    }
                    auto candidateSynth = std::make_unique<NeniaSoundFontSynthesizer>();
                    auto candidateSequencer = std::make_unique<NeniaMidiSequencer>();
                    NeniaSoundFontGainReadback gainReadback;
                    if (!candidateSynth->initialize(
                            static_cast<int>(sampleRate), true,
                            command.gain, &reply.error, &gainReadback)
                        || !candidateSynth->loadSoundFont(soundFontPath, &reply.error)
                        || !candidateSequencer->initialize(command.sequence, sampleRate,
                                                           &reply.error)) {
                        break;
                    }
                    synthesizer = std::move(candidateSynth);
                    sequencer = std::move(candidateSequencer);
                    peakProtector = std::move(candidateProtector);
                    owner->m_settingsSynthGain.store(gainReadback.settingsGain,
                                                     std::memory_order_release);
                    owner->m_activeSynthGain.store(gainReadback.effectiveSynthGain,
                                                   std::memory_order_release);
                    const auto &definition =
                        neniaMidiLoudnessPresetDefinition(activeLoudnessPreset);
                    owner->m_trialPostSynthGainDb.store(definition.postSynthGainDb,
                                                        std::memory_order_release);
                    owner->m_trialLimiterEnabled.store(definition.limiterEnabled,
                                                       std::memory_order_release);
                    owner->m_trialCeilingDbfs.store(
                        definition.limiterEnabled ? definition.ceilingDbfs : 0.0,
                        std::memory_order_release);
                    owner->m_selectedLoudnessPreset.store(
                        static_cast<int>(activeLoudnessPreset), std::memory_order_release);
                    owner->m_confirmedLoudnessPreset.store(
                        static_cast<int>(activeLoudnessPreset), std::memory_order_release);
                    if (peakProtector != nullptr) {
                        owner->m_trialLookaheadFrames.store(
                            peakProtector->inputLatencyFrames(), std::memory_order_release);
                        owner->m_trialScratchBytes.store(peakProtector->scratchBytes(),
                                                         std::memory_order_release);
                    } else {
                        owner->m_trialLookaheadFrames.store(0, std::memory_order_release);
                        owner->m_trialScratchBytes.store(0, std::memory_order_release);
                    }
                    prepared = true;
                    tailFramesRemaining = 0;
                    owner->m_renderFinished.store(false, std::memory_order_release);
                    owner->m_generatedFrames.store(0, std::memory_order_release);
                    owner->m_consumedFrames.store(0, std::memory_order_release);
                    owner->m_baseFrame.store(0, std::memory_order_release);
                    resetTrialCounters();
                    if (peakProtector != nullptr) {
                        peakProtector->reset();
                        owner->m_trialLookaheadFrames.store(
                            peakProtector->inputLatencyFrames(), std::memory_order_release);
                        owner->m_trialScratchBytes.store(peakProtector->scratchBytes(),
                                                         std::memory_order_release);
                    }
                    reply.ok = true;
                    break;
                }
                case CommandType::Start:
                    if (prepared && !renderFinished && owner->m_pcmRing != nullptr) {
                        renderActive = true;
                        refillActive = true;
                        reply.ok = true;
                    } else {
                        reply.error = QStringLiteral("the prepared MIDI source cannot be started in its current state");
                    }
                    break;
                case CommandType::Pause:
                    renderActive = false;
                    refillActive = false;
                    reply.ok = prepared;
                    if (!reply.ok) {
                        reply.error = QStringLiteral("no prepared MIDI source");
                    }
                    break;
                case CommandType::Stop:
                    owner->m_analysisGeneration.fetch_add(1, std::memory_order_acq_rel);
                    renderActive = false;
                    refillActive = false;
                    renderFinished = false;
                    tailStarted = false;
                    tailFramesRemaining = 0;
                    if (synthesizer != nullptr && synthesizer->isInitialized()
                        && !synthesizer->reset(&reply.error)) {
                        break;
                    }
                    if (sequencer != nullptr && sequencer->isInitialized()
                        && !sequencer->reset(&reply.error)) {
                        break;
                    }
                    if (owner->m_pcmRing != nullptr) {
                        owner->m_pcmRing->reset();
                    }
                    if (peakProtector != nullptr) {
                        peakProtector->reset();
                    }
                    resetTrialCounters();
                    if (peakProtector != nullptr) {
                        owner->m_trialLookaheadFrames.store(
                            peakProtector->inputLatencyFrames(), std::memory_order_release);
                        owner->m_trialScratchBytes.store(peakProtector->scratchBytes(),
                                                         std::memory_order_release);
                    }
                    owner->m_renderFinished.store(false, std::memory_order_release);
                    owner->m_generatedFrames.store(0, std::memory_order_release);
                    owner->m_consumedFrames.store(0, std::memory_order_release);
                    owner->m_baseFrame.store(0, std::memory_order_release);
                    reply.ok = prepared;
                    if (!reply.ok) {
                        reply.error = QStringLiteral("no prepared MIDI source");
                    }
                    break;
                case CommandType::Seek:
                    owner->m_analysisGeneration.fetch_add(1, std::memory_order_acq_rel);
                    renderActive = false;
                    refillActive = false;
                    renderFinished = false;
                    tailStarted = false;
                    tailFramesRemaining = 0;
                    if (!prepared || sequencer == nullptr || synthesizer == nullptr
                        || owner->m_pcmRing == nullptr) {
                        reply.error = QStringLiteral("no prepared MIDI source");
                        break;
                    }
                    owner->m_pcmRing->reset();
                    if (peakProtector != nullptr) {
                        peakProtector->reset();
                    }
                    resetTrialCounters();
                    if (peakProtector != nullptr) {
                        owner->m_trialLookaheadFrames.store(
                            peakProtector->inputLatencyFrames(), std::memory_order_release);
                        owner->m_trialScratchBytes.store(peakProtector->scratchBytes(),
                                                         std::memory_order_release);
                    }
                    if (!sequencer->seek(command.seekFrame, *synthesizer, nullptr,
                                         &reply.error)) {
                        break;
                    }
                    owner->m_baseFrame.store(command.seekFrame, std::memory_order_release);
                    owner->m_generatedFrames.store(0, std::memory_order_release);
                    owner->m_consumedFrames.store(0, std::memory_order_release);
                    owner->m_renderFinished.store(false, std::memory_order_release);
                    renderActive = command.activateAfterSeek;
                    refillActive = command.activateAfterSeek;
                    reply.ok = true;
                    break;
                case CommandType::SetGain:
                    reply = command.gainRequestContext;
                    reply.workerIdentity = static_cast<std::uint64_t>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()));
                    if (!prepared || synthesizer == nullptr || !synthesizer->hasSoundFont()) {
                        reply.error = QStringLiteral("no active Direct MIDI synthesizer is available");
                        break;
                    }
                    reply.ok = synthesizer->setGain(
                        command.gain, &reply.previousEffectiveGain, &reply.effectiveGain,
                        &reply.activeVoicesBefore, &reply.activeVoicesAfter, &reply.error);
                    if (std::isfinite(reply.effectiveGain)) {
                        owner->m_activeSynthGain.store(reply.effectiveGain,
                                                       std::memory_order_release);
                    }
                    break;
                case CommandType::SetLoudnessPreset: {
                    reply = command.gainRequestContext;
                    const auto &nextDefinition =
                        neniaMidiLoudnessPresetDefinition(command.loudnessPreset);
                    reply.loudnessPreset = command.loudnessPreset;
                    reply.loudnessSynthGain = nextDefinition.synthGain;
                    reply.loudnessPostGainDb = nextDefinition.postSynthGainDb;
                    reply.loudnessLimiterEnabled = nextDefinition.limiterEnabled;
                    reply.workerIdentity = static_cast<std::uint64_t>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()));
                    reply.previousLoudnessPreset = activeLoudnessPreset;
                    if ((!owner->m_loudnessTrialEnabled
                         && !owner->m_startupConfiguration.userPresetSelectionEnabled)
                        || !prepared
                        || synthesizer == nullptr || !synthesizer->hasSoundFont()
                        || owner->m_pcmRing == nullptr) {
                        reply.error = QStringLiteral(
                            "the real-time MIDI loudness trial requires an active Direct MIDI source");
                        break;
                    }
                    if (command.loudnessPreset == activeLoudnessPreset) {
                        reply.ok = true;
                        reply.previousLoudnessPreset = activeLoudnessPreset;
                        break;
                    }
                    std::unique_ptr<StreamingPeakProtector> candidateProtector;
                    if (!makeProtector(command.loudnessPreset, &candidateProtector,
                                       &reply.error)) {
                        break;
                    }
                    const std::size_t pending = peakProtector == nullptr
                        ? 0U : peakProtector->pendingBufferedFrames();
                    if (pending > owner->m_pcmRing->freeFrames()) {
                        reply.error = QStringLiteral(
                            "not enough PCM ring capacity to drain the prior trial preset safely");
                        break;
                    }
                    double previousEffectiveGain = 0.0;
                    reply.ok = synthesizer->setGain(
                        nextDefinition.synthGain, &previousEffectiveGain,
                        &reply.effectiveGain, &reply.activeVoicesBefore,
                        &reply.activeVoicesAfter, &reply.error);
                    if (!reply.ok) {
                        break;
                    }
                    if (peakProtector != nullptr && pending > 0U) {
                        QString drainError;
                        const StreamingProcessResult drained = peakProtector->endOfStream(
                            std::span<float>(processedPcm.data(), pending * 2U), &drainError);
                        if (!drained.success || drained.framesProduced != pending
                            || !peakProtector->isDrained()) {
                            reply.ok = false;
                            reply.error = drainError.isEmpty()
                                ? QStringLiteral("prior preset lookahead drain was incomplete")
                                : drainError;
                            break;
                        }
                        if (!publishOutput(drained.framesProduced, processedPcm,
                                           &reply.error)) {
                            reply.ok = false;
                            break;
                        }
                    }
                    foldProtectorStats();
                    peakProtector = std::move(candidateProtector);
                    activeLoudnessPreset = command.loudnessPreset;
                    owner->m_activeSynthGain.store(reply.effectiveGain,
                                                   std::memory_order_release);
                    owner->m_trialPostSynthGainDb.store(nextDefinition.postSynthGainDb,
                                                        std::memory_order_release);
                    owner->m_trialLimiterEnabled.store(nextDefinition.limiterEnabled,
                                                       std::memory_order_release);
                    owner->m_trialCeilingDbfs.store(
                        nextDefinition.limiterEnabled ? nextDefinition.ceilingDbfs : 0.0,
                        std::memory_order_release);
                    owner->m_trialLookaheadFrames.store(
                        peakProtector == nullptr ? 0
                            : peakProtector->inputLatencyFrames(),
                        std::memory_order_release);
                    owner->m_trialScratchBytes.store(
                        peakProtector == nullptr ? 0 : peakProtector->scratchBytes(),
                        std::memory_order_release);
                    owner->m_trialPendingFrames.store(0, std::memory_order_release);
                    reply.ok = true;
                    break;
                }
                case CommandType::Shutdown:
                    renderActive = false;
                    refillActive = false;
                    synthesizer.reset();
                    sequencer.reset();
                    reply.ok = true;
                    exitRequested = true;
                    break;
                }
                if (command.type == CommandType::SetGain) {
                    const Reply gainReply = reply;
                    QMetaObject::invokeMethod(
                        owner,
                        [engine = owner, gainReply] {
                            engine->completeRealtimeSynthGainChange(
                                gainReply.requestId, gainReply.requestedGain,
                                gainReply.previousSelectedGain,
                                gainReply.previousEffectiveGain, gainReply.effectiveGain,
                                gainReply.activeVoicesBefore, gainReply.activeVoicesAfter,
                                gainReply.workerIdentity, gainReply.sourceBasename,
                                gainReply.requestedPositionMs,
                                gainReply.requestedMasterVolume,
                                gainReply.requestedSinkVolume, gainReply.ok,
                                gainReply.error);
                        },
                        Qt::QueuedConnection);
                }
                if (command.type == CommandType::SetLoudnessPreset) {
                    const Reply presetReply = reply;
                    if (presetReply.requestId != 0) {
                        QMetaObject::invokeMethod(
                            owner,
                            [engine = owner, presetReply] {
                                engine->completeRealtimeLoudnessPresetChange(
                                    presetReply.requestId, presetReply.loudnessPreset,
                                    presetReply.loudnessSynthGain,
                                    presetReply.loudnessPostGainDb,
                                    presetReply.loudnessLimiterEnabled,
                                    presetReply.previousLoudnessPreset,
                                    presetReply.activeVoicesBefore,
                                    presetReply.activeVoicesAfter,
                                    presetReply.workerIdentity, presetReply.ok,
                                    presetReply.error);
                            }, Qt::QueuedConnection);
                    }
                }
                if (command.completion != nullptr) {
                    command.completion->set_value(reply);
                }
                if (exitRequested) {
                    break;
                }
                if (!reply.ok && command.type != CommandType::Pause
                    && command.type != CommandType::Stop
                    && command.type != CommandType::SetGain
                    && command.type != CommandType::SetLoudnessPreset) {
                    if (!reply.error.isEmpty()) {
                        owner->setError(reply.error);
                        owner->setState(NeniaMidiPlaybackState::Error);
                    }
                }
                continue;
            }

            if (renderActive && !refillActive && !renderFinished
                && owner->m_pcmRing != nullptr
                && owner->m_pcmRing->queuedFrames() <= kLowWaterFrames) {
                refillActive = true;
            }

            if (renderActive && refillActive && !renderFinished && prepared && owner->m_pcmRing != nullptr
                && owner->m_pcmRing->queuedFrames() < kHighWaterFrames) {
                if (synthesizer == nullptr || sequencer == nullptr) {
                    owner->setError(QStringLiteral("synthesis worker lost its prepared context"));
                    owner->setState(NeniaMidiPlaybackState::Error);
                    renderActive = false;
                    continue;
                }

                const std::size_t frameCount = std::min({
                    kRenderBlockFrames,
                    owner->m_pcmRing->freeFrames(),
                    kHighWaterFrames - owner->m_pcmRing->queuedFrames()});
                if (frameCount > 0) {
                    std::size_t generatedThisBlock = 0;
                    QString renderError;
                    if (!sequencer->reachedSequenceEnd()) {
                        const auto synthStart = measureWorkerTime
                            ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
                        if (!sequencer->render(*synthesizer,
                                               std::span<float>(floatPcm.data(), frameCount * 2U),
                                               &generatedThisBlock, &renderError)) {
                            failWorker(renderError, renderActive);
                            continue;
                        }
                        if (measureWorkerTime) {
                            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - synthStart).count();
                            owner->m_trialSynthesisNanoseconds.fetch_add(
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
                                std::memory_order_relaxed);
                            owner->m_trialSynthesisBlocks.fetch_add(1, std::memory_order_relaxed);
                            atomicAddMaximum(owner->m_trialSynthesisWorstNanoseconds,
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
                        }
                    } else if (!tailStarted) {
                        for (int channel = 0; channel < static_cast<int>(NeniaMidiChannelState::kChannelCount);
                             ++channel) {
                            if (!synthesizer->controlChange(channel, 64, 0, &renderError)
                                || !synthesizer->controlChange(channel, 123, 0, &renderError)) {
                                failWorker(renderError, renderActive);
                                break;
                            }
                        }
                        if (!renderActive) {
                            continue;
                        }
                        tailStarted = true;
                        tailFramesRemaining = static_cast<std::uint64_t>(sampleRate)
                            * kMaximumTailSeconds;
                        owner->setState(NeniaMidiPlaybackState::Draining);
                    } else if (tailFramesRemaining > 0) {
                        generatedThisBlock = static_cast<std::size_t>(std::min<std::uint64_t>(
                            frameCount, tailFramesRemaining));
                        const auto synthStart = measureWorkerTime
                            ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
                        if (!synthesizer->renderStereoFloat(
                                std::span<float>(floatPcm.data(), generatedThisBlock * 2U),
                                &renderError)) {
                            failWorker(renderError, renderActive);
                            continue;
                        }
                        if (measureWorkerTime) {
                            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - synthStart).count();
                            owner->m_trialSynthesisNanoseconds.fetch_add(
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
                                std::memory_order_relaxed);
                            owner->m_trialSynthesisBlocks.fetch_add(1, std::memory_order_relaxed);
                            atomicAddMaximum(owner->m_trialSynthesisWorstNanoseconds,
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
                        }
                        tailFramesRemaining -= generatedThisBlock;
                    } else {
                        if (peakProtector != nullptr && !peakProtector->isDrained()) {
                            const std::size_t outputCapacity = std::min(
                                frameCount, processedPcm.size() / 2U);
                            const auto dspStart = std::chrono::steady_clock::now();
                            const StreamingProcessResult drained = peakProtector->endOfStream(
                                std::span<float>(processedPcm.data(), outputCapacity * 2U),
                                &renderError);
                            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - dspStart).count();
                            owner->m_trialDspNanoseconds.fetch_add(
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
                                std::memory_order_relaxed);
                            owner->m_trialDspBlocks.fetch_add(1, std::memory_order_relaxed);
                            atomicAddMaximum(owner->m_trialDspWorstNanoseconds,
                                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
                            if (!drained.success) {
                                failWorker(renderError, renderActive);
                                continue;
                            }
                            collectProtectorStats();
                            if (drained.framesProduced > 0U
                                && !publishOutput(drained.framesProduced, processedPcm,
                                                  &renderError)) {
                                failWorker(renderError, renderActive);
                                continue;
                            }
                            if (!peakProtector->isDrained()) {
                                if (drained.framesProduced == 0U
                                    && drained.pendingBufferedFrames > 0U) {
                                    failWorker(QStringLiteral(
                                        "peak-protector EOS drain made no progress"), renderActive);
                                }
                                continue;
                            }
                            if (peakProtector->stats().inputFramesConsumed
                                != peakProtector->stats().outputFramesProduced) {
                                owner->m_trialTimingDiscontinuities.fetch_add(
                                    1, std::memory_order_relaxed);
                                failWorker(QStringLiteral(
                                    "peak-protector EOS frame accounting did not balance"),
                                    renderActive);
                                continue;
                            }
                        }
                        renderFinished = true;
                        renderActive = false;
                        owner->m_renderFinished.store(true, std::memory_order_release);
                        notifyBufferReady();
                    }
                    if (owner->m_pcmRing->queuedFrames() >= kHighWaterFrames) {
                        refillActive = false;
                    }

                    if (generatedThisBlock > 0) {
                        if (owner->m_streamingProtectionEnabled) {
                            for (std::size_t sample = 0; sample < generatedThisBlock * 2U; ++sample) {
                                if (!std::isfinite(floatPcm[sample])) {
                                    floatPcm[sample] = 0.0F;
                                    owner->m_trialNonFiniteSamples.fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                            }
                        }
                        if (peakProtector == nullptr) {
                            if (owner->m_streamingProtectionEnabled) {
                                owner->m_trialInputFrames.fetch_add(generatedThisBlock,
                                                                    std::memory_order_acq_rel);
                            }
                            if (!publishOutput(generatedThisBlock, floatPcm, &renderError)) {
                                failWorker(renderError, renderActive);
                                continue;
                            }
                        } else {
                            std::size_t inputOffset = 0;
                            while (inputOffset < generatedThisBlock) {
                                const std::size_t remaining = generatedThisBlock - inputOffset;
                                const auto dspStart = std::chrono::steady_clock::now();
                                const StreamingProcessResult processed = peakProtector->process(
                                    std::span<const float>(floatPcm.data() + inputOffset * 2U,
                                                           remaining * 2U),
                                    std::span<float>(processedPcm.data(), processedPcm.size()),
                                    &renderError);
                                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now() - dspStart).count();
                                owner->m_trialDspNanoseconds.fetch_add(
                                    static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
                                    std::memory_order_relaxed);
                                owner->m_trialDspBlocks.fetch_add(1, std::memory_order_relaxed);
                                atomicAddMaximum(owner->m_trialDspWorstNanoseconds,
                                    static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
                                if (!processed.success
                                    || processed.framesConsumed > remaining
                                    || processed.framesProduced > processedPcm.size() / 2U) {
                                    failWorker(renderError.isEmpty()
                                        ? QStringLiteral("streaming peak-protector returned invalid frame counts")
                                        : renderError, renderActive);
                                    break;
                                }
                                owner->m_trialInputFrames.fetch_add(
                                    processed.framesConsumed, std::memory_order_acq_rel);
                                inputOffset += processed.framesConsumed;
                                collectProtectorStats();
                                if (processed.framesProduced > 0U
                                    && !publishOutput(processed.framesProduced, processedPcm,
                                                      &renderError)) {
                                    failWorker(renderError, renderActive);
                                    break;
                                }
                                if (processed.framesConsumed == 0U
                                    && processed.framesProduced == 0U) {
                                    failWorker(QStringLiteral(
                                        "streaming peak-protector made no progress with pending MIDI PCM"),
                                        renderActive);
                                    break;
                                }
                            }
                            if (!renderActive && !renderFinished) {
                                continue;
                            }
                        }
                        if (sequencer != nullptr) {
                            owner->m_unsupportedSysEx.store(sequencer->unsupportedSysExCount(),
                                                            std::memory_order_release);
                            owner->m_unsupportedSystemCommon.store(
                                sequencer->unsupportedSystemCommonCount(), std::memory_order_release);
                            owner->m_ignoredNoteOffVelocity.store(
                                sequencer->ignoredNoteOffVelocityCount(), std::memory_order_release);
                        }
                        notifyBufferReady();
                    }
                }
            }

            if (renderFinished && owner->m_pcmRing != nullptr
                && owner->m_pcmRing->queuedFrames() == 0
                && owner->m_consumedFrames.load(std::memory_order_acquire)
                    >= owner->m_generatedFrames.load(std::memory_order_acquire)) {
                if (owner->m_streamingProtectionEnabled
                    && (owner->m_trialInputFrames.load(std::memory_order_acquire)
                            != owner->m_trialOutputFrames.load(std::memory_order_acquire)
                        || owner->m_trialPendingFrames.load(std::memory_order_acquire) != 0U)) {
                    owner->m_trialTimingDiscontinuities.fetch_add(1,
                                                                  std::memory_order_relaxed);
                    owner->setError(QStringLiteral(
                        "MIDI loudness trial ended with unbalanced synthesized/output frames"));
                    owner->setState(NeniaMidiPlaybackState::Error);
                    renderFinished = false;
                    continue;
                }
                owner->setState(NeniaMidiPlaybackState::Ended);
                if (!owner->m_endSignalPosted.exchange(true, std::memory_order_acq_rel)) {
                    owner->queueNaturalEnd();
                }
                renderFinished = false;
            }
        }
    }

    std::size_t writePcm(std::size_t frames,
                         std::vector<float> &floatPcm,
                         std::vector<std::int16_t> &int16Pcm)
    {
        const std::size_t sampleCount = frames * 2U;
        const bool trace = owner->m_traceCounters != nullptr;
        const auto finalSafeSample = [this](float sample) {
            if (!std::isfinite(sample)) {
                if (owner->m_streamingProtectionEnabled) {
                    owner->m_trialNonFiniteSamples.fetch_add(1, std::memory_order_relaxed);
                }
                sample = 0.0F;
            }
            if (std::abs(sample) > 1.0F && owner->m_streamingProtectionEnabled) {
                if (!owner->m_trialLimiterEnabled.load(std::memory_order_acquire)) {
                    owner->m_trialPotentialOverrangeSamples.fetch_add(1,
                                                                      std::memory_order_relaxed);
                }
                owner->m_trialSafetyClampSamples.fetch_add(1, std::memory_order_relaxed);
            }
            return std::clamp(sample, -1.0F, 1.0F);
        };
        if (owner->m_outputFormat.sampleFormat() == QAudioFormat::Float) {
            if (trace) {
                PcmTraceBlock beforeClamp;
                PcmTraceBlock afterClamp;
                for (std::size_t index = 0; index < sampleCount; ++index) {
                    float sample = floatPcm[index];
                    beforeClamp.add(sample);
                    sample = finalSafeSample(sample);
                    floatPcm[index] = sample;
                    afterClamp.add(sample);
                }
                const std::size_t written = owner->m_pcmRing->writeFrames(floatPcm.data(), frames);
                if (written == frames) {
                    owner->m_traceCounters->addProducer(beforeClamp, afterClamp);
                }
                return written;
            }
            for (std::size_t index = 0; index < sampleCount; ++index) {
                float sample = floatPcm[index];
                floatPcm[index] = finalSafeSample(sample);
            }
            return owner->m_pcmRing->writeFrames(floatPcm.data(), frames);
        }
        if (trace) {
            PcmTraceBlock beforeClamp;
            PcmTraceBlock afterClamp;
            for (std::size_t index = 0; index < sampleCount; ++index) {
                float sample = floatPcm[index];
                beforeClamp.add(sample);
                sample = finalSafeSample(sample);
                afterClamp.add(sample);
                const long value = std::lround(sample < 0.0F
                                                   ? sample * 32768.0F
                                                   : sample * 32767.0F);
                floatPcm[index] = sample;
                int16Pcm[index] = static_cast<std::int16_t>(value);
            }
            const std::size_t written = owner->m_pcmRing->writeFrames(int16Pcm.data(), frames);
            if (written == frames) {
                owner->m_traceCounters->addProducer(beforeClamp, afterClamp);
            }
            return written;
        }
        for (std::size_t index = 0; index < sampleCount; ++index) {
            float sample = floatPcm[index];
            sample = finalSafeSample(sample);
            const long value = std::lround(sample < 0.0F ? sample * 32768.0F : sample * 32767.0F);
            floatPcm[index] = sample;
            int16Pcm[index] = static_cast<std::int16_t>(value);
        }
        return owner->m_pcmRing->writeFrames(int16Pcm.data(), frames);
    }

    void failWorker(const QString &message, bool &renderActive)
    {
        renderActive = false;
        owner->setError(message.isEmpty() ? QStringLiteral("FluidSynth rendering failed") : message);
        owner->setState(NeniaMidiPlaybackState::Error);
        QMetaObject::invokeMethod(owner, [engine = owner] { engine->stopSink(); },
                                  Qt::QueuedConnection);
    }

    NeniaMidiPlaybackEngine *owner;
    std::mutex commandMutex;
    std::condition_variable commandAvailable;
    std::condition_variable bufferReady;
    std::mutex bufferWaitMutex;
    std::deque<Command> commands;
    std::thread thread;
};

NeniaMidiPlaybackEngine::NeniaMidiPlaybackEngine(QObject *parent)
    : NeniaMidiPlaybackEngine(OutputMode::Device, 0U, parent,
                              NeniaMidiLoudnessPreset::E)
{
}

NeniaMidiPlaybackEngine::NeniaMidiPlaybackEngine(OutputMode outputMode, QObject *parent)
    : NeniaMidiPlaybackEngine(outputMode, 0U, parent,
                              NeniaMidiLoudnessPreset::E)
{
}

NeniaMidiPlaybackEngine::NeniaMidiPlaybackEngine(
    OutputMode outputMode,
    std::uint32_t deterministicSampleRateHz,
    QObject *parent,
    NeniaMidiLoudnessPreset preferredUserPreset)
    : QObject(parent)
    , m_outputMode(outputMode)
    , m_deterministicSampleRateHz(
          outputMode == OutputMode::DeterministicTest
              ? (deterministicSampleRateHz == 0U ? 44'100U : deterministicSampleRateHz)
              : 0U)
    , m_audioTraceEnabled(neniaMidiAudioTraceEnabled())
    , m_startupConfiguration(resolveNeniaMidiLoudnessStartupConfiguration(
          qgetenv("NENIA_MIDI_LOUDNESS_POLICY"),
          qgetenv("NENIA_MIDI_LOUDNESS_TRIAL"),
          qgetenv("NENIA_MIDI_GAIN_AB"),
          qgetenv("NENIA_EXPERIMENTAL_MIDI_GAIN"),
          qEnvironmentVariableIsSet("NENIA_EXPERIMENTAL_MIDI_GAIN"),
          preferredUserPreset))
    , m_requestedGainText(m_startupConfiguration.requestedGainText)
    , m_loudnessTrialConfigurationConflict(m_startupConfiguration.developerFlagConflict)
    , m_loudnessTrialEnabled(m_startupConfiguration.developerTrialEnabled)
    , m_streamingProtectionEnabled(m_startupConfiguration.streamingProtectionEnabled)
    , m_realtimeSynthGain(m_startupConfiguration.synthGain)
    , m_gainAbEnabled(m_startupConfiguration.gainAbEnabled)
    , m_selectedRealtimeSynthGain(m_realtimeSynthGain)
    , m_selectedLoudnessPreset(
          static_cast<int>(m_startupConfiguration.initialPreset))
    , m_confirmedLoudnessPreset(
          static_cast<int>(m_startupConfiguration.initialPreset))
    , m_analysisTap(std::make_unique<NeniaMidiPcmAnalysisTap>())
{
    if (!m_startupConfiguration.errorMessage.isEmpty()) {
        qCritical().noquote() << "[NENIA MIDI LOUDNESS POLICY] startup configuration error:"
                              << m_startupConfiguration.errorMessage;
    }
    if (!m_startupConfiguration.warningMessage.isEmpty()) {
        qWarning().noquote() << "[NENIA MIDI LOUDNESS POLICY]"
                             << m_startupConfiguration.warningMessage;
    }
    if (m_loudnessTrialEnabled) {
        m_loudnessTrialTimer = new QTimer(this);
        m_loudnessTrialTimer->setInterval(1'000);
        connect(m_loudnessTrialTimer, &QTimer::timeout,
                this, &NeniaMidiPlaybackEngine::reportLoudnessTrialDiagnostics);
        m_loudnessTrialTimer->start();
    }
    if (m_audioTraceEnabled) {
        m_traceCounters = std::make_unique<TraceCounters>();
        m_traceTimer = new QTimer(this);
        m_traceTimer->setInterval(1'000);
        connect(m_traceTimer, &QTimer::timeout,
                this, &NeniaMidiPlaybackEngine::reportAudioTrace);
        m_traceTimer->start();
        logAudioTraceLifecycle(QStringLiteral("engine-created"));
    }
    m_worker = std::make_unique<Worker>(this);
    m_worker->start();
}

void NeniaMidiPlaybackEngine::logAudioTraceLifecycle(const QString &event) const
{
    if (!m_audioTraceEnabled) {
        return;
    }
    const QString safeRequestedGain = [&] {
        if (m_requestedGainText.startsWith(QLatin1Char('('))) {
            return m_requestedGainText;
        }
        bool ok = false;
        const double value = m_requestedGainText.toDouble(&ok);
        return ok && std::isfinite(value)
            ? QString::number(value, 'g', 9) : QStringLiteral("(invalid)");
    }();
    const QString device = m_outputMode == OutputMode::DeterministicTest
        ? QStringLiteral("deterministic-test-consumer") : m_outputDevice.description();
    const QString soundFont = m_soundFontPath.isEmpty()
        ? QStringLiteral("(not-prepared)") : QFileInfo(m_soundFontPath).fileName();
    const int sinkState = m_audioSink == nullptr
        ? -1 : static_cast<int>(m_audioSink->state());
    const int sinkError = m_audioSink == nullptr
        ? 0 : static_cast<int>(m_audioSink->error());
    qInfo() << "[NENIA MIDI AUDIO]"
            << "event=" << event
            << "pid=" << QCoreApplication::applicationPid()
            << "backend=direct-midi"
            << "mode=" << (m_outputMode == OutputMode::Device ? "device" : "deterministic-test")
            << "requested_gain=" << safeRequestedGain
            << "selected_gain=" << m_selectedRealtimeSynthGain.load(std::memory_order_acquire)
            << "settings_gain=" << m_settingsSynthGain.load(std::memory_order_acquire)
            << "effective_synth_gain=" << m_activeSynthGain.load(std::memory_order_acquire)
            << "source=" << (m_midiPath.isEmpty()
                                  ? QStringLiteral("(not-prepared)")
                                  : QFileInfo(m_midiPath).fileName())
            << "soundfont=" << soundFont
            << "controller_master_volume=" << m_masterVolume
            << "qaudiosink_volume=" << (m_audioSink == nullptr ? 0.0f : m_audioSink->volume())
            << "audio_device=" << device
            << "format=" << (m_outputFormat.isValid()
                                  ? formatDescription(m_outputFormat)
                                  : QStringLiteral("(not-negotiated)"))
            << "sink_state=" << sinkState
            << "sink_error=" << sinkError
            << "playback_state=" << static_cast<int>(state());
}

void NeniaMidiPlaybackEngine::reportAudioTrace()
{
    if (!m_audioTraceEnabled || m_traceCounters == nullptr
        || (state() != NeniaMidiPlaybackState::Playing
            && state() != NeniaMidiPlaybackState::Draining)) {
        return;
    }
    const PcmTraceSnapshot preClamp = m_traceCounters->preClamp.take();
    const PcmTraceSnapshot postClamp = m_traceCounters->postClamp.take();
    const PcmTraceSnapshot sinkInput = m_traceCounters->sinkInput.take();
    const std::uint64_t sinkDeliveredFrames =
        m_traceCounters->sinkDeliveredFrames.exchange(0, std::memory_order_relaxed);
    const std::uint64_t sinkRingFrames =
        m_traceCounters->sinkRingFrames.exchange(0, std::memory_order_relaxed);
    const std::uint64_t sinkSilenceFrames =
        m_traceCounters->sinkSilenceFrames.exchange(0, std::memory_order_relaxed);
    const std::uint64_t consumedFrames = m_consumedFrames.load(std::memory_order_acquire);
    const std::uint64_t underrunFrames = m_underrunFrames.load(std::memory_order_acquire);
    const std::uint64_t underrunCallbacks =
        m_underrunCallbacks.load(std::memory_order_acquire);
    const std::uint64_t intervalConsumed = consumedFrames - m_lastTraceConsumedFrames;
    const std::uint64_t intervalUnderrunFrames = underrunFrames - m_lastTraceUnderrunFrames;
    const std::uint64_t intervalUnderrunCallbacks =
        underrunCallbacks - m_lastTraceUnderrunCallbacks;
    m_lastTraceConsumedFrames = consumedFrames;
    m_lastTraceUnderrunFrames = underrunFrames;
    m_lastTraceUnderrunCallbacks = underrunCallbacks;
    const NeniaMidiPlaybackDiagnostics workerTiming = diagnostics();

    qInfo() << "[NENIA MIDI AUDIO]"
            << "event=pcm-interval"
            << "pid=" << QCoreApplication::applicationPid()
            << "source=" << QFileInfo(m_midiPath).fileName()
            << "selected_gain=" << m_selectedRealtimeSynthGain.load(std::memory_order_acquire)
            << "effective_synth_gain=" << m_activeSynthGain.load(std::memory_order_acquire)
            << "master_volume=" << m_masterVolume
            << "sink_volume=" << (m_audioSink == nullptr ? 0.0f : m_audioSink->volume())
            << "playback_position_ms=" << positionMilliseconds()
            << "interval_ms=~1000"
            << "preclamp_samples=" << preClamp.samples
            << "preclamp_finite=" << preClamp.finiteSamples
            << "preclamp_nonfinite=" << preClamp.nonFiniteSamples
            << "preclamp_overrange=" << preClamp.overrangeSamples
            << "preclamp_peak=" << preClamp.peakAbs
            << "preclamp_rms=" << preClamp.rms()
            << "postclamp_samples=" << postClamp.samples
            << "postclamp_finite=" << postClamp.finiteSamples
            << "postclamp_peak=" << postClamp.peakAbs
            << "postclamp_rms=" << postClamp.rms()
            << "sink_input_samples=" << sinkInput.samples
            << "sink_input_finite=" << sinkInput.finiteSamples
            << "sink_input_nonfinite=" << sinkInput.nonFiniteSamples
            << "sink_input_peak=" << sinkInput.peakAbs
            << "sink_input_rms=" << sinkInput.rms()
            << "sink_delivered_frames=" << sinkDeliveredFrames
            << "ring_frames_read=" << sinkRingFrames
            << "zero_buffer_frames=" << sinkSilenceFrames
            << "consumed_frames_total=" << consumedFrames
            << "consumed_frames_interval=" << intervalConsumed
            << "underflow_frames_interval=" << intervalUnderrunFrames
            << "underflow_callbacks_interval=" << intervalUnderrunCallbacks
            << "synthesis_block_avg_ms="
            << workerTiming.trialSynthesisBlockAverageMilliseconds
            << "synthesis_block_worst_ms="
            << workerTiming.trialSynthesisBlockWorstMilliseconds
            << "dsp_block_avg_ms=" << workerTiming.trialDspBlockAverageMilliseconds
            << "dsp_block_worst_ms=" << workerTiming.trialDspBlockWorstMilliseconds
            << "maximum_ring_occupancy_frames="
            << workerTiming.trialMaximumRingOccupancyFrames
            << "estimated_total_audio_latency_ms="
            << workerTiming.trialReportedAudioLatencyMilliseconds
            << "device=" << (m_audioSink == nullptr
                                  ? QStringLiteral("(no-device-sink)")
                                  : m_outputDevice.description())
            << "format=" << formatDescription(m_outputFormat)
            << "sink_state=" << (m_audioSink == nullptr
                                      ? -1 : static_cast<int>(m_audioSink->state()))
            << "sink_error=" << (m_audioSink == nullptr
                                      ? 0 : static_cast<int>(m_audioSink->error()))
            << "stage_note=all_pcm_stats_are_pre_qaudiosink_volume_not_device_capture";
}

void NeniaMidiPlaybackEngine::reportLoudnessTrialDiagnostics()
{
    if (!m_loudnessTrialEnabled
        || (state() != NeniaMidiPlaybackState::Playing
            && state() != NeniaMidiPlaybackState::Draining)) {
        return;
    }
    const NeniaMidiPlaybackDiagnostics snapshot = diagnostics();
    const auto &preset = neniaMidiLoudnessPresetDefinition(snapshot.confirmedLoudnessPreset);
    qInfo() << "[NENIA MIDI LOUDNESS TRIAL]"
            << "event=runtime-interval"
            << "preset=" << preset.name
            << "selected_preset=" << neniaMidiLoudnessPresetDefinition(
                   snapshot.selectedLoudnessPreset).name
            << "source=" << QFileInfo(snapshot.midiPath).fileName()
            << "position_ms=" << positionMilliseconds()
            << "synth_gain=" << snapshot.effectiveSynthGain
            << "post_gain_db=" << snapshot.trialPostSynthGainDb
            << "limiter=" << snapshot.trialLimiterEnabled
            << "ceiling_dbfs=" << snapshot.trialCeilingDbfs
            << "limiter_active_frames=" << snapshot.trialLimiterActiveFrames
            << "maximum_gain_reduction_db=" << snapshot.trialMaximumGainReductionDb
            << "potential_overrange_samples=" << snapshot.trialPotentialOverrangeSamples
            << "safety_clamp_samples=" << snapshot.trialSafetyClampSamples
            << "nonfinite_samples=" << snapshot.trialNonFiniteSamples
            << "queued_frames=" << snapshot.queuedPcmFrames
            << "maximum_ring_occupancy_frames=" << snapshot.trialMaximumRingOccupancyFrames
            << "pending_lookahead_frames=" << snapshot.trialPendingFrames
            << "lookahead_ms=" << snapshot.trialLookaheadMilliseconds
            << "estimated_total_audio_latency_ms="
            << snapshot.trialReportedAudioLatencyMilliseconds
            << "synthesis_avg_ms=" << snapshot.trialSynthesisBlockAverageMilliseconds
            << "synthesis_worst_ms=" << snapshot.trialSynthesisBlockWorstMilliseconds
            << "dsp_avg_ms=" << snapshot.trialDspBlockAverageMilliseconds
            << "dsp_worst_ms=" << snapshot.trialDspBlockWorstMilliseconds
            << "underrun_callbacks=" << snapshot.underrunCallbacks
            << "underrun_frames=" << snapshot.underrunFrames
            << "ring_write_failures=" << snapshot.trialRingWriteFailures
            << "timing_discontinuities=" << snapshot.trialTimingDiscontinuities
            << "worker_frame_totals=" << snapshot.trialInputFrames << "/"
            << snapshot.trialOutputFrames
            << "master_volume=" << snapshot.controllerMasterVolume
            << "sink_volume=" << snapshot.audioSinkVolume
            << "scratch_bytes=" << snapshot.trialScratchBytes
            << "stage_note=estimated_queue_latency_not_device_loopback_measurement";
}

NeniaMidiPlaybackEngine::~NeniaMidiPlaybackEngine()
{
    stopSink();
    m_worker.reset();
    m_audioSink.reset();
}

bool NeniaMidiPlaybackEngine::requireOwnerThread(QString *errorMessage) const
{
    if (QThread::currentThread() == thread()) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("playback controls must be called on the engine's QObject thread");
    }
    return false;
}

bool NeniaMidiPlaybackEngine::prepare(const QString &midiPath,
                                     const QString &soundFontPath,
                                     QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    if (!m_startupConfiguration.valid || m_loudnessTrialConfigurationConflict) {
        const QString error = !m_startupConfiguration.errorMessage.isEmpty()
            ? m_startupConfiguration.errorMessage
            : QStringLiteral(
                  "conflicting developer flags: unset NENIA_MIDI_GAIN_AB before enabling NENIA_MIDI_LOUDNESS_TRIAL");
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    logAudioTraceLifecycle(QStringLiteral("backend-prepare-begin"));
    if (m_audioTraceEnabled && m_audioSink != nullptr) {
        logAudioTraceLifecycle(QStringLiteral("sink-recreation-begin"));
    }
    stopSink();
    if (m_worker != nullptr && m_sequence != nullptr) {
        Worker::Command stopCommand;
        stopCommand.type = Worker::CommandType::Stop;
        (void)m_worker->submit(std::move(stopCommand));
    }
    if (m_traceCounters != nullptr) {
        m_traceCounters->reset();
    }
    m_settingsSynthGain.store(0.0, std::memory_order_release);
    m_activeSynthGain.store(0.0, std::memory_order_release);
    m_lastTraceConsumedFrames = 0;
    m_lastTraceUnderrunFrames = 0;
    m_lastTraceUnderrunCallbacks = 0;
    m_sequence.reset();
    m_audioSink.reset();
    m_pcmRing.reset();
    m_outputDevice = QAudioDevice{};
    m_outputFormat = QAudioFormat{};
    m_midiPath.clear();
    m_soundFontPath.clear();
    m_durationFrames = 0;
    m_durationMs = 0;
    m_midiChannelUsage.fill(false);
    setState(NeniaMidiPlaybackState::Empty);

    NeniaMidiSequence parsedSequence;
    QString error;
    if (!parsedSequence.load(midiPath, &error)) {
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    m_sequence = std::make_shared<NeniaMidiSequence>(std::move(parsedSequence));
    m_midiPath = QFileInfo(midiPath).absoluteFilePath();
    if (!m_sequence->tickToMilliseconds(m_sequence->durationTick(), &m_durationMs, &error)) {
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    if (soundFontPath.trimmed().isEmpty()) {
        error = QStringLiteral("No SoundFont is configured for direct MIDI playback");
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    const QFileInfo soundFontInfo(soundFontPath);
    if (!soundFontInfo.exists() || !soundFontInfo.isFile() || !soundFontInfo.isReadable()) {
        error = QStringLiteral("SoundFont path is not a readable regular file: %1").arg(soundFontPath);
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    AudioChoice choice;
    if (m_outputMode == OutputMode::DeterministicTest) {
        choice.format.setSampleRate(static_cast<int>(m_deterministicSampleRateHz));
        choice.format.setChannelCount(2);
        choice.format.setSampleFormat(QAudioFormat::Float);
    } else if (!chooseAudioOutput(&choice, &error)) {
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    const auto sequence = m_sequence;
    std::uint64_t durationFrames = 0;
    qint64 durationMs = 0;
    if (!sequence->tickToSampleFrame(sequence->durationTick(),
                                     static_cast<std::uint32_t>(choice.format.sampleRate()),
                                     &durationFrames, &error)
        || !sequence->tickToMilliseconds(sequence->durationTick(), &durationMs, &error)) {
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    m_pcmRing = std::make_unique<NeniaPcmRingBuffer>(
        static_cast<std::size_t>(choice.format.bytesPerFrame()), kPcmRingCapacityFrames);
    m_outputDevice = choice.device;
    m_outputFormat = choice.format;
    if (m_outputMode == OutputMode::Device) {
        m_audioSink = std::make_unique<QAudioSink>(m_outputDevice, m_outputFormat);
        m_audioSink->setBufferFrameCount(static_cast<qsizetype>(kAudioSinkBufferFrames));
        m_audioSink->setVolume(m_masterVolume);
    }
    m_midiChannelUsage.fill(false);
    for (const NeniaMidiEvent &event : m_sequence->events()) {
        std::visit([this](const auto &value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, NeniaMidiNoteOnEvent>
                          || std::is_same_v<Event, NeniaMidiNoteOffEvent>
                          || std::is_same_v<Event, NeniaMidiPolyPressureEvent>
                          || std::is_same_v<Event, NeniaMidiControlChangeEvent>
                          || std::is_same_v<Event, NeniaMidiProgramChangeEvent>
                          || std::is_same_v<Event, NeniaMidiChannelPressureEvent>
                          || std::is_same_v<Event, NeniaMidiPitchBendEvent>) {
                m_midiChannelUsage.at(value.channel) = true;
            }
        }, event.payload);
    }
    m_midiPath = QFileInfo(midiPath).absoluteFilePath();
    m_soundFontPath = soundFontInfo.absoluteFilePath();
    m_durationFrames = durationFrames;
    m_durationMs = durationMs;
    m_baseFrame.store(0, std::memory_order_release);
    m_generatedFrames.store(0, std::memory_order_release);
    m_consumedFrames.store(0, std::memory_order_release);
    m_underrunCallbacks.store(0, std::memory_order_release);
    m_underrunFrames.store(0, std::memory_order_release);
    m_unsupportedSysEx.store(0, std::memory_order_release);
    m_unsupportedSystemCommon.store(0, std::memory_order_release);
    m_ignoredNoteOffVelocity.store(0, std::memory_order_release);
    m_endSignalPosted.store(false, std::memory_order_release);
    m_renderFinished.store(false, std::memory_order_release);
    setError({});

    if (m_audioSink != nullptr) {
        QAudioSink *const preparedSink = m_audioSink.get();
        connect(m_audioSink.get(), &QAudioSink::stateChanged, this,
            [this, preparedSink](QAudio::State sinkState) {
                if (m_audioSink.get() != preparedSink) {
                    return;
                }
                logAudioTraceLifecycle(QStringLiteral("sink-state-changed"));
                if (sinkState == QAudio::StoppedState
                    && !m_expectedSinkStop.load(std::memory_order_acquire)) {
                    const NeniaMidiPlaybackState current = state();
                    if (current == NeniaMidiPlaybackState::Playing
                        || current == NeniaMidiPlaybackState::Draining) {
                        QString message = QStringLiteral("Qt audio output stopped unexpectedly");
                        if (m_audioSink != nullptr && m_audioSink->error() != QAudio::NoError) {
                            message += QStringLiteral(" (QAudioSink error %1)")
                                           .arg(static_cast<int>(m_audioSink->error()));
                        }
                        setError(message);
                        setState(NeniaMidiPlaybackState::Error);
                        stopSink();
                        if (m_worker != nullptr) {
                            Worker::Command pauseCommand;
                            pauseCommand.type = Worker::CommandType::Pause;
                            (void)m_worker->submit(std::move(pauseCommand));
                        }
                    }
                }
                });
    }

    Worker::Command prepareCommand;
    prepareCommand.type = Worker::CommandType::Prepare;
    prepareCommand.soundFontPath = m_soundFontPath;
    prepareCommand.sequence = m_sequence;
    prepareCommand.sampleRate = static_cast<std::uint32_t>(m_outputFormat.sampleRate());
    prepareCommand.loudnessPreset = static_cast<NeniaMidiLoudnessPreset>(
        m_selectedLoudnessPreset.load(std::memory_order_acquire));
    prepareCommand.gain = m_streamingProtectionEnabled
        ? neniaMidiLoudnessPresetDefinition(prepareCommand.loudnessPreset).synthGain
        : m_selectedRealtimeSynthGain.load(std::memory_order_acquire);
    const Worker::Reply reply = m_worker->submit(std::move(prepareCommand));
    if (!reply.ok) {
        m_audioSink.reset();
        m_pcmRing.reset();
        setError(reply.error);
        setState(NeniaMidiPlaybackState::Error);
        logAudioTraceLifecycle(QStringLiteral("backend-prepare-failed"));
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    setState(NeniaMidiPlaybackState::Ready);
    logAudioTraceLifecycle(QStringLiteral("backend-prepared-sink-created"));
    return true;
}

bool NeniaMidiPlaybackEngine::play(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    const NeniaMidiPlaybackState current = state();
    if (current == NeniaMidiPlaybackState::Paused) {
        return resume(errorMessage);
    }
    if (current != NeniaMidiPlaybackState::Ready
        && current != NeniaMidiPlaybackState::Stopped) {
        const QString error = QStringLiteral("Play requires a prepared, ready or stopped source");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    m_endSignalPosted.store(false, std::memory_order_release);
    Worker::Command command;
    command.type = Worker::CommandType::Start;
    const Worker::Reply reply = m_worker->submit(std::move(command));
    if (!reply.ok) {
        setError(reply.error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    if (!waitForPrebuffer(errorMessage)) {
        Worker::Command pauseCommand;
        pauseCommand.type = Worker::CommandType::Pause;
        (void)m_worker->submit(std::move(pauseCommand));
        return false;
    }
    return startSink(errorMessage);
}

bool NeniaMidiPlaybackEngine::pause(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    const NeniaMidiPlaybackState current = state();
    if (current != NeniaMidiPlaybackState::Playing
        && current != NeniaMidiPlaybackState::Draining) {
        return current == NeniaMidiPlaybackState::Paused;
    }
    m_stateBeforePause.store(current, std::memory_order_release);
    m_callbackEnabled.store(false, std::memory_order_release);
    if (m_audioSink != nullptr) {
        m_audioSink->suspend();
    }
    while (m_activeCallbacks.load(std::memory_order_acquire) != 0) {
        QThread::yieldCurrentThread();
    }
    Worker::Command command;
    command.type = Worker::CommandType::Pause;
    const Worker::Reply reply = m_worker->submit(std::move(command));
    if (!reply.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    setState(NeniaMidiPlaybackState::Paused);
    return true;
}

bool NeniaMidiPlaybackEngine::resume(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    if (state() != NeniaMidiPlaybackState::Paused
        || (m_audioSink == nullptr && m_outputMode != OutputMode::DeterministicTest)) {
        const QString error = QStringLiteral("Resume requires a paused source");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    Worker::Command command;
    command.type = Worker::CommandType::Start;
    const Worker::Reply reply = m_worker->submit(std::move(command));
    if (!reply.ok) {
        setError(reply.error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    const NeniaMidiPlaybackState restoreState = m_stateBeforePause.load(std::memory_order_acquire);
    if (m_sinkNeedsStart.load(std::memory_order_acquire)) {
        if (!waitForPrebuffer(errorMessage)) {
            Worker::Command pauseCommand;
            pauseCommand.type = Worker::CommandType::Pause;
            (void)m_worker->submit(std::move(pauseCommand));
            return false;
        }
        m_stateBeforePause.store(restoreState, std::memory_order_release);
        return startSink(errorMessage);
    }
    setState(restoreState);
    m_callbackEnabled.store(true, std::memory_order_release);
    if (m_outputMode == OutputMode::DeterministicTest) {
        return true;
    }
    m_audioSink->resume();
    if (m_audioSink->error() != QAudio::NoError) {
        const QString error = QStringLiteral("Qt audio output could not resume (QAudioSink error %1)")
                                  .arg(static_cast<int>(m_audioSink->error()));
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    return true;
}

bool NeniaMidiPlaybackEngine::stop(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    stopSink();
    if (m_worker == nullptr || m_sequence == nullptr) {
        setState(m_sequence == nullptr ? NeniaMidiPlaybackState::Empty
                                       : NeniaMidiPlaybackState::Stopped);
        return true;
    }
    Worker::Command command;
    command.type = Worker::CommandType::Stop;
    const Worker::Reply reply = m_worker->submit(std::move(command));
    if (!reply.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    m_baseFrame.store(0, std::memory_order_release);
    m_consumedFrames.store(0, std::memory_order_release);
    m_generatedFrames.store(0, std::memory_order_release);
    m_renderFinished.store(false, std::memory_order_release);
    m_endSignalPosted.store(false, std::memory_order_release);
    setState(NeniaMidiPlaybackState::Stopped);
    return true;
}

bool NeniaMidiPlaybackEngine::restart(QString *errorMessage)
{
    if (!stop(errorMessage)) {
        return false;
    }
    return play(errorMessage);
}

bool NeniaMidiPlaybackEngine::seekMilliseconds(qint64 positionMs, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    if (m_sequence == nullptr || m_pcmRing == nullptr
        || (m_audioSink == nullptr && m_outputMode != OutputMode::DeterministicTest)
        || positionMs < 0) {
        const QString error = QStringLiteral("Seek requires a prepared source and non-negative position");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    const NeniaMidiPlaybackState previousState = state();
    if (previousState == NeniaMidiPlaybackState::Error
        || previousState == NeniaMidiPlaybackState::Empty) {
        const QString error = QStringLiteral("Seek is unavailable in the current playback state");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    const bool shouldResume = previousState == NeniaMidiPlaybackState::Playing
        || previousState == NeniaMidiPlaybackState::Draining;
    if (previousState == NeniaMidiPlaybackState::Playing
        || previousState == NeniaMidiPlaybackState::Draining
        || previousState == NeniaMidiPlaybackState::Paused) {
        stopSink();
    }

    const std::uint64_t targetFrame = std::min(
        millisecondsToFrames(std::min(positionMs, m_durationMs),
                             static_cast<std::uint32_t>(m_outputFormat.sampleRate())),
        m_durationFrames);
    Worker::Command command;
    command.type = Worker::CommandType::Seek;
    command.seekFrame = targetFrame;
    command.activateAfterSeek = shouldResume;
    const Worker::Reply reply = m_worker->submit(std::move(command));
    if (!reply.ok) {
        setError(reply.error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = reply.error;
        }
        return false;
    }
    m_endSignalPosted.store(false, std::memory_order_release);
    if (previousState == NeniaMidiPlaybackState::Paused) {
        setState(NeniaMidiPlaybackState::Paused);
    } else if (previousState == NeniaMidiPlaybackState::Stopped) {
        setState(NeniaMidiPlaybackState::Stopped);
    } else if (previousState == NeniaMidiPlaybackState::Ready) {
        setState(NeniaMidiPlaybackState::Ready);
    } else if (shouldResume) {
        if (!waitForPrebuffer(errorMessage)) {
            Worker::Command pauseCommand;
            pauseCommand.type = Worker::CommandType::Pause;
            (void)m_worker->submit(std::move(pauseCommand));
            return false;
        }
        if (!startSink(errorMessage)) {
            return false;
        }
    }
    return true;
}

NeniaMidiPlaybackState NeniaMidiPlaybackEngine::state() const noexcept
{
    return m_state.load(std::memory_order_acquire);
}

std::shared_ptr<const NeniaMidiSequence>
NeniaMidiPlaybackEngine::sequenceSnapshot() const noexcept
{
    return m_sequence;
}

qint64 NeniaMidiPlaybackEngine::positionMilliseconds() const noexcept
{
    if (m_outputFormat.sampleRate() <= 0) {
        return 0;
    }
    const std::uint64_t base = m_baseFrame.load(std::memory_order_acquire);
    const std::uint64_t consumed = m_consumedFrames.load(std::memory_order_acquire);
    const std::uint64_t frame = std::min(
        m_durationFrames, consumed > std::numeric_limits<std::uint64_t>::max() - base
            ? std::numeric_limits<std::uint64_t>::max() : base + consumed);
    return std::min(m_durationMs,
                    framesToMilliseconds(frame, static_cast<std::uint32_t>(m_outputFormat.sampleRate())));
}

qint64 NeniaMidiPlaybackEngine::durationMilliseconds() const noexcept
{
    return m_durationMs;
}

NeniaMidiChannelState NeniaMidiPlaybackEngine::channelStateSnapshot() const
{
    NeniaMidiChannelState result;
    if (m_sequence == nullptr || m_outputFormat.sampleRate() <= 0) {
        return result;
    }
    const std::uint64_t positionFrame = std::min(
        m_durationFrames,
        m_baseFrame.load(std::memory_order_acquire)
            + m_consumedFrames.load(std::memory_order_acquire));
    NeniaMidiTick low = 0;
    NeniaMidiTick high = m_sequence->durationTick();
    while (low < high) {
        const NeniaMidiTick distance = high - low;
        const NeniaMidiTick middle = low + distance / 2U + distance % 2U;
        std::uint64_t frame = 0;
        if (!m_sequence->tickToSampleFrame(middle,
                                           static_cast<std::uint32_t>(m_outputFormat.sampleRate()),
                                           &frame, nullptr)) {
            break;
        }
        if (frame <= positionFrame) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    (void)m_sequence->stateAtTick(low, &result, nullptr);
    return result;
}

std::uint64_t NeniaMidiPlaybackEngine::analysisGeneration() const noexcept
{
    return m_analysisGeneration.load(std::memory_order_acquire);
}

std::uint64_t NeniaMidiPlaybackEngine::consumedOutputFrame() const noexcept
{
    const std::uint64_t base = m_baseFrame.load(std::memory_order_acquire);
    const std::uint64_t consumed = m_consumedFrames.load(std::memory_order_acquire);
    return consumed > std::numeric_limits<std::uint64_t>::max() - base
        ? std::numeric_limits<std::uint64_t>::max() : base + consumed;
}

float NeniaMidiPlaybackEngine::masterVolume() const noexcept
{
    return m_masterVolume;
}

double NeniaMidiPlaybackEngine::realtimeSynthGainForTesting() const noexcept
{
    return m_realtimeSynthGain;
}

double NeniaMidiPlaybackEngine::selectedRealtimeSynthGainForTesting() const noexcept
{
    return selectedRealtimeSynthGain();
}

bool NeniaMidiPlaybackEngine::realtimeGainAbEnabled() const noexcept
{
    return m_gainAbEnabled;
}

bool NeniaMidiPlaybackEngine::realtimeLoudnessTrialEnabled() const noexcept
{
    return m_loudnessTrialEnabled;
}

bool NeniaMidiPlaybackEngine::realtimeLoudnessTrialConfigurationConflict() const noexcept
{
    return m_loudnessTrialConfigurationConflict;
}

bool NeniaMidiPlaybackEngine::userMidiPresetSelectionEnabled() const noexcept
{
    return m_startupConfiguration.userPresetSelectionEnabled;
}

bool NeniaMidiPlaybackEngine::setPreferredUserLoudnessPreset(
    NeniaMidiLoudnessPreset preset, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return false;
    }
    if (!m_startupConfiguration.userPresetSelectionEnabled) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "the MIDI preset is locked by the active startup configuration");
        }
        return false;
    }
    const auto &definitions = neniaMidiLoudnessPresetDefinitions();
    const auto selected = std::find_if(
        definitions.cbegin(), definitions.cend(), [preset](const auto &definition) {
            return definition.preset == preset;
        });
    if (selected == definitions.cend()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unknown MIDI loudness preset");
        }
        return false;
    }
    const NeniaMidiPlaybackState currentState = state();
    if (currentState == NeniaMidiPlaybackState::Playing
        || currentState == NeniaMidiPlaybackState::Paused
        || currentState == NeniaMidiPlaybackState::Draining) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "an active MIDI source must confirm a preset change on the synthesis worker");
        }
        return false;
    }

    const bool workerHasPreparedMidi = m_sequence != nullptr && m_worker != nullptr
        && m_pcmRing != nullptr
        && std::isfinite(m_activeSynthGain.load(std::memory_order_acquire))
        && m_activeSynthGain.load(std::memory_order_acquire) > 0.0
        && (currentState == NeniaMidiPlaybackState::Ready
            || currentState == NeniaMidiPlaybackState::Stopped
            || currentState == NeniaMidiPlaybackState::Ended);
    if (workerHasPreparedMidi) {
        Worker::Command command;
        command.type = Worker::CommandType::SetLoudnessPreset;
        command.loudnessPreset = preset;
        command.gainRequestContext.loudnessPreset = preset;
        command.gainRequestContext.loudnessSynthGain = selected->synthGain;
        command.gainRequestContext.loudnessPostGainDb = selected->postSynthGainDb;
        command.gainRequestContext.loudnessLimiterEnabled = selected->limiterEnabled;
        const Worker::Reply reply = m_worker->submit(std::move(command));
        if (!reply.ok) {
            if (errorMessage != nullptr) {
                *errorMessage = reply.error;
            }
            return false;
        }
    }

    m_selectedLoudnessPreset.store(static_cast<int>(preset), std::memory_order_release);
    m_confirmedLoudnessPreset.store(static_cast<int>(preset), std::memory_order_release);
    m_selectedRealtimeSynthGain.store(selected->synthGain, std::memory_order_release);
    return true;
}

NeniaMidiLoudnessPreset NeniaMidiPlaybackEngine::selectedLoudnessPreset() const noexcept
{
    return static_cast<NeniaMidiLoudnessPreset>(
        m_selectedLoudnessPreset.load(std::memory_order_acquire));
}

NeniaMidiLoudnessPreset NeniaMidiPlaybackEngine::confirmedLoudnessPreset() const noexcept
{
    return static_cast<NeniaMidiLoudnessPreset>(
        m_confirmedLoudnessPreset.load(std::memory_order_acquire));
}

quint64 NeniaMidiPlaybackEngine::requestRealtimeLoudnessPreset(
    NeniaMidiLoudnessPreset preset, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return 0;
    }
    if (m_loudnessTrialConfigurationConflict) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "conflicting developer flags: NENIA_MIDI_GAIN_AB and NENIA_MIDI_LOUDNESS_TRIAL cannot both be enabled");
        }
        return 0;
    }
    if (!m_loudnessTrialEnabled
        && !m_startupConfiguration.userPresetSelectionEnabled) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "MIDI preset changes are disabled by the active startup configuration");
        }
        return 0;
    }
    const auto &definitions = neniaMidiLoudnessPresetDefinitions();
    const auto found = std::find_if(definitions.cbegin(), definitions.cend(),
                                    [preset](const auto &definition) {
        return definition.preset == preset;
    });
    if (found == definitions.cend()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unknown MIDI loudness trial preset");
        }
        return 0;
    }
    const NeniaMidiPlaybackState currentState = state();
    if (m_sequence == nullptr || m_worker == nullptr
        || (currentState != NeniaMidiPlaybackState::Ready
            && currentState != NeniaMidiPlaybackState::Playing
            && currentState != NeniaMidiPlaybackState::Paused
            && currentState != NeniaMidiPlaybackState::Stopped
            && currentState != NeniaMidiPlaybackState::Draining
            && currentState != NeniaMidiPlaybackState::Ended)
        || !std::isfinite(m_activeSynthGain.load(std::memory_order_acquire))
        || m_activeSynthGain.load(std::memory_order_acquire) <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "MIDI loudness preset switching requires an active Direct MIDI source in Playing or Paused state");
        }
        return 0;
    }

    Worker::Command command;
    command.type = Worker::CommandType::SetLoudnessPreset;
    command.loudnessPreset = preset;
    Worker::Reply &context = command.gainRequestContext;
    context.requestId = m_nextLoudnessRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (context.requestId == 0) {
        context.requestId = m_nextLoudnessRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    context.loudnessPreset = preset;
    context.loudnessSynthGain = found->synthGain;
    context.loudnessPostGainDb = found->postSynthGainDb;
    context.loudnessLimiterEnabled = found->limiterEnabled;
    context.previousLoudnessPreset = selectedLoudnessPreset();
    const quint64 requestId = context.requestId;
    const NeniaMidiLoudnessPreset previousSelected = context.previousLoudnessPreset;
    if (!m_worker->enqueue(std::move(command))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("MIDI synthesis worker is shutting down");
        }
        return 0;
    }
    m_selectedLoudnessPreset.store(static_cast<int>(preset), std::memory_order_release);
    m_selectedRealtimeSynthGain.store(found->synthGain, std::memory_order_release);
    m_latestLoudnessRequestId.store(requestId, std::memory_order_release);
    qInfo() << (m_loudnessTrialEnabled ? "[NENIA MIDI LOUDNESS TRIAL]"
                                       : "[NENIA MIDI PRESET]")
            << "event=requested"
            << "request_id=" << requestId
            << "preset=" << found->name
            << "synth_gain=" << found->synthGain
            << "post_synth_gain_db=" << found->postSynthGainDb
            << "limiter=" << found->limiterEnabled
            << "ceiling_dbfs=" << (found->limiterEnabled ? found->ceilingDbfs : 0.0)
            << "previous_selected=" << neniaMidiLoudnessPresetDefinition(previousSelected).name
            << "source=" << QFileInfo(m_midiPath).fileName()
            << "playback_position_ms=" << positionMilliseconds()
            << "master_volume=" << m_masterVolume
            << "sink_volume=" << (m_audioSink == nullptr ? 0.0f : m_audioSink->volume());
    return requestId;
}

double NeniaMidiPlaybackEngine::selectedRealtimeSynthGain() const noexcept
{
    return m_selectedRealtimeSynthGain.load(std::memory_order_acquire);
}

std::size_t NeniaMidiPlaybackEngine::queuedAnalysisBlocks() const noexcept
{
    return m_analysisTap == nullptr ? 0 : m_analysisTap->queuedBlocks();
}

bool NeniaMidiPlaybackEngine::takeEligibleAnalysisBlock(
    NeniaMidiPcmAnalysisBlock *block) noexcept
{
    if (m_analysisTap == nullptr || block == nullptr) {
        return false;
    }
    return m_analysisTap->takeEligible(analysisGeneration(), consumedOutputFrame(), block);
}

NeniaMidiPlaybackDiagnostics NeniaMidiPlaybackEngine::diagnostics() const
{
    NeniaMidiPlaybackDiagnostics result;
    result.midiPath = m_midiPath;
    result.soundFontPath = m_soundFontPath;
    result.outputDevice = m_outputMode == OutputMode::DeterministicTest
        ? QStringLiteral("Deterministic test PCM consumer") : m_outputDevice.description();
    result.outputFormat = m_outputFormat.isValid() ? formatDescription(m_outputFormat) : QString();
    result.sampleRate = m_outputFormat.sampleRate() > 0
        ? static_cast<std::uint32_t>(m_outputFormat.sampleRate()) : 0;
    result.ringCapacityFrames = m_pcmRing == nullptr ? 0 : m_pcmRing->capacityFrames();
    result.queuedPcmFrames = m_pcmRing == nullptr ? 0 : m_pcmRing->queuedFrames();
    result.generatedPcmFrames = m_generatedFrames.load(std::memory_order_acquire);
    result.consumedPcmFrames = m_consumedFrames.load(std::memory_order_acquire);
    result.underrunCallbacks = m_underrunCallbacks.load(std::memory_order_acquire);
    result.underrunFrames = m_underrunFrames.load(std::memory_order_acquire);
    result.unsupportedSysExMessages = m_unsupportedSysEx.load(std::memory_order_acquire);
    result.unsupportedSystemCommonMessages = m_unsupportedSystemCommon.load(std::memory_order_acquire);
    result.ignoredNoteOffReleaseVelocities = m_ignoredNoteOffVelocity.load(std::memory_order_acquire);
    result.droppedSpectrumAnalysisBlocks = m_analysisTap == nullptr
        ? 0 : m_analysisTap->droppedBlocks();
    result.requestedSynthGain = m_selectedRealtimeSynthGain.load(std::memory_order_acquire);
    result.settingsSynthGain = m_settingsSynthGain.load(std::memory_order_acquire);
    result.effectiveSynthGain = m_activeSynthGain.load(std::memory_order_acquire);
    result.controllerMasterVolume = m_masterVolume;
    result.audioSinkPresent = m_audioSink != nullptr;
    result.audioSinkVolume = m_audioSink == nullptr ? 0.0f : m_audioSink->volume();
    result.audioSinkState = m_audioSink == nullptr
        ? -1 : static_cast<int>(m_audioSink->state());
    result.audioSinkError = m_audioSink == nullptr
        ? 0 : static_cast<int>(m_audioSink->error());
    result.loudnessTrialEnabled = m_loudnessTrialEnabled;
    result.loudnessTrialConfigurationConflict = m_loudnessTrialConfigurationConflict;
    result.userMidiPresetSelectionEnabled =
        m_startupConfiguration.userPresetSelectionEnabled;
    result.productionHEnabled = m_startupConfiguration.productionHEnabled;
    result.streamingProtectionEnabled = m_streamingProtectionEnabled;
    result.startupMode = m_startupConfiguration.mode;
    result.startupConfigurationError = m_startupConfiguration.errorMessage;
    result.startupConfigurationWarning = m_startupConfiguration.warningMessage;
    result.selectedLoudnessPreset = selectedLoudnessPreset();
    result.confirmedLoudnessPreset = confirmedLoudnessPreset();
    result.trialPostSynthGainDb = m_trialPostSynthGainDb.load(std::memory_order_acquire);
    result.trialCeilingDbfs = m_trialCeilingDbfs.load(std::memory_order_acquire);
    result.trialLimiterEnabled = m_trialLimiterEnabled.load(std::memory_order_acquire);
    result.trialMaximumGainReductionDb =
        m_trialMaximumGainReductionDb.load(std::memory_order_acquire);
    result.trialInputFrames = m_trialInputFrames.load(std::memory_order_acquire);
    result.trialOutputFrames = m_trialOutputFrames.load(std::memory_order_acquire);
    result.trialPendingFrames = m_trialPendingFrames.load(std::memory_order_acquire);
    result.trialLimiterActiveFrames =
        m_trialLimiterActiveFrames.load(std::memory_order_acquire);
    result.trialPotentialOverrangeSamples =
        m_trialPotentialOverrangeSamples.load(std::memory_order_acquire);
    result.trialSafetyClampSamples = m_trialSafetyClampSamples.load(std::memory_order_acquire);
    result.trialNonFiniteSamples = m_trialNonFiniteSamples.load(std::memory_order_acquire);
    result.trialTimingDiscontinuities =
        m_trialTimingDiscontinuities.load(std::memory_order_acquire);
    result.trialRingWriteFailures = m_trialRingWriteFailures.load(std::memory_order_acquire);
    result.trialLookaheadFrames = static_cast<std::uint32_t>(
        m_trialLookaheadFrames.load(std::memory_order_acquire));
    result.trialLookaheadMilliseconds = result.sampleRate == 0
        ? 0.0 : 1'000.0 * static_cast<double>(result.trialLookaheadFrames)
            / static_cast<double>(result.sampleRate);
    result.trialScratchBytes = m_trialScratchBytes.load(std::memory_order_acquire);
    result.trialSynthesisBlocks = m_trialSynthesisBlocks.load(std::memory_order_acquire);
    result.trialDspBlocks = m_trialDspBlocks.load(std::memory_order_acquire);
    result.trialMaximumRingOccupancyFrames =
        m_trialMaximumRingOccupancyFrames.load(std::memory_order_acquire);
    const std::uint64_t synthesisNs =
        m_trialSynthesisNanoseconds.load(std::memory_order_acquire);
    const std::uint64_t dspNs = m_trialDspNanoseconds.load(std::memory_order_acquire);
    result.trialSynthesisBlockAverageMilliseconds = result.trialSynthesisBlocks == 0
        ? 0.0 : static_cast<double>(synthesisNs)
            / static_cast<double>(result.trialSynthesisBlocks) / 1'000'000.0;
    result.trialSynthesisBlockWorstMilliseconds =
        static_cast<double>(m_trialSynthesisWorstNanoseconds.load(std::memory_order_acquire))
        / 1'000'000.0;
    result.trialDspBlockAverageMilliseconds = result.trialDspBlocks == 0
        ? 0.0 : static_cast<double>(dspNs)
            / static_cast<double>(result.trialDspBlocks) / 1'000'000.0;
    result.trialDspBlockWorstMilliseconds =
        static_cast<double>(m_trialDspWorstNanoseconds.load(std::memory_order_acquire))
        / 1'000'000.0;
    if (result.sampleRate > 0 && (m_streamingProtectionEnabled || m_audioTraceEnabled)) {
        const std::uint64_t totalQueuedFrames = result.queuedPcmFrames
            + (m_audioSink == nullptr ? 0U : kAudioSinkBufferFrames);
        result.trialReportedAudioLatencyMilliseconds = result.trialLookaheadMilliseconds
            + 1'000.0 * static_cast<double>(totalQueuedFrames)
                / static_cast<double>(result.sampleRate);
    }
    if (m_traceCounters != nullptr) {
        const PcmTraceSnapshot preClamp = m_traceCounters->preClamp.load();
        const PcmTraceSnapshot postClamp = m_traceCounters->postClamp.load();
        const PcmTraceSnapshot sinkInput = m_traceCounters->sinkInput.load();
        result.tracePreClampSamples = preClamp.samples;
        result.tracePreClampFiniteSamples = preClamp.finiteSamples;
        result.tracePreClampNonFiniteSamples = preClamp.nonFiniteSamples;
        result.tracePreClampOverrangeSamples = preClamp.overrangeSamples;
        result.tracePostClampSamples = postClamp.samples;
        result.tracePostClampFiniteSamples = postClamp.finiteSamples;
        result.traceSinkInputSamples = sinkInput.samples;
        result.traceSinkDeliveredFrames =
            m_traceCounters->sinkDeliveredFrames.load(std::memory_order_relaxed);
        result.traceSinkRingFrames = m_traceCounters->sinkRingFrames.load(std::memory_order_relaxed);
        result.traceSinkSilenceFrames =
            m_traceCounters->sinkSilenceFrames.load(std::memory_order_relaxed);
        result.tracePreClampPeak = preClamp.peakAbs;
        result.tracePreClampRms = preClamp.rms();
        result.tracePostClampPeak = postClamp.peakAbs;
        result.tracePostClampRms = postClamp.rms();
        result.traceSinkInputPeak = sinkInput.peakAbs;
        result.traceSinkInputRms = sinkInput.rms();
    }
    result.midiChannelUsage = m_midiChannelUsage;
    {
        std::lock_guard lock(m_diagnosticsMutex);
        result.errorMessage = m_errorMessage;
    }
    return result;
}

bool NeniaMidiPlaybackEngine::waitForPrebuffer(QString *errorMessage)
{
    if (m_pcmRing == nullptr || m_worker == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCM output is not prepared");
        }
        return false;
    }
    std::unique_lock lock(m_worker->bufferWaitMutex);
    const bool ready = m_worker->bufferReady.wait_for(lock, std::chrono::seconds(5), [&] {
        return m_pcmRing->queuedFrames() >= std::min(kInitialPrebufferFrames,
                                                      m_pcmRing->capacityFrames())
            || m_renderFinished.load(std::memory_order_acquire)
            || state() == NeniaMidiPlaybackState::Error;
    });
    if (!ready || state() == NeniaMidiPlaybackState::Error
        || m_pcmRing->queuedFrames() == 0) {
        const QString error = state() == NeniaMidiPlaybackState::Error
            ? diagnostics().errorMessage
            : QStringLiteral("timed out waiting for initial MIDI PCM prebuffer");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    return true;
}

bool NeniaMidiPlaybackEngine::startSink(QString *errorMessage)
{
    if ((m_audioSink == nullptr && m_outputMode != OutputMode::DeterministicTest)
        || m_pcmRing == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Qt audio output is not prepared");
        }
        return false;
    }
    m_expectedSinkStop.store(false, std::memory_order_release);
    m_callbackEnabled.store(true, std::memory_order_release);
    m_stateBeforePause.store(NeniaMidiPlaybackState::Playing, std::memory_order_release);
    setState(NeniaMidiPlaybackState::Playing);
    if (m_outputMode == OutputMode::DeterministicTest) {
        m_sinkNeedsStart.store(false, std::memory_order_release);
        logAudioTraceLifecycle(QStringLiteral("test-consumer-started"));
        return true;
    }
    if (m_outputFormat.sampleFormat() == QAudioFormat::Float) {
        m_audioSink->start([this](QSpan<float> output) { consumeFloat(output); });
    } else {
        m_audioSink->start([this](QSpan<std::int16_t> output) { consumeInt16(output); });
    }
    if (m_audioSink->error() != QAudio::NoError) {
        const QString error = QStringLiteral("QAudioSink failed to start (error %1)")
                                  .arg(static_cast<int>(m_audioSink->error()));
        stopSink();
        Worker::Command pauseCommand;
        pauseCommand.type = Worker::CommandType::Pause;
        (void)m_worker->submit(std::move(pauseCommand));
        setError(error);
        setState(NeniaMidiPlaybackState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }
    m_sinkNeedsStart.store(false, std::memory_order_release);
    logAudioTraceLifecycle(QStringLiteral("sink-started"));
    return true;
}

void NeniaMidiPlaybackEngine::stopSink() noexcept
{
    m_expectedSinkStop.store(true, std::memory_order_release);
    m_callbackEnabled.store(false, std::memory_order_release);
    m_sinkNeedsStart.store(true, std::memory_order_release);
    if (m_audioSink != nullptr) {
        m_audioSink->stop();
    }
    while (m_activeCallbacks.load(std::memory_order_acquire) != 0) {
        QThread::yieldCurrentThread();
    }
}

bool NeniaMidiPlaybackEngine::consumePcmForTesting(std::span<float> output) noexcept
{
    if (m_outputMode != OutputMode::DeterministicTest || (output.size() % 2U) != 0U) {
        return false;
    }
    consumeSamples(output.data(), output.size(), sizeof(float));
    // DeterministicTest models the post-QAudioSink master-volume output. The
    // production analysis tap remains on generated PCM before this stage.
    for (float &sample : output) {
        sample *= m_masterVolume;
    }
    return true;
}

void NeniaMidiPlaybackEngine::setMasterVolume(float volume) noexcept
{
    if (std::isnan(volume)) {
        volume = 0.0f;
    } else if (!std::isfinite(volume)) {
        volume = volume > 0.0f ? 1.0f : 0.0f;
    }
    const float sanitized = std::clamp(volume, 0.0f, 1.0f);
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, sanitized] {
            setMasterVolume(sanitized);
        }, Qt::QueuedConnection);
        return;
    }
    m_masterVolume = sanitized;
    if (m_audioSink != nullptr) {
        m_audioSink->setVolume(m_masterVolume);
    }
    logAudioTraceLifecycle(QStringLiteral("master-volume-updated"));
}

quint64 NeniaMidiPlaybackEngine::requestRealtimeSynthGain(double gain,
                                                          QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!requireOwnerThread(errorMessage)) {
        return 0;
    }
    if (!m_gainAbEnabled) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("live MIDI gain A/B is disabled; set NENIA_MIDI_GAIN_AB=1");
        }
        return 0;
    }
    if (gain != 0.20 && gain != 0.40) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("live MIDI gain A/B accepts only preset A 0.20 or preset B 0.40");
        }
        return 0;
    }
    const NeniaMidiPlaybackState currentState = state();
    if (m_sequence == nullptr || m_worker == nullptr
        || (currentState != NeniaMidiPlaybackState::Playing
            && currentState != NeniaMidiPlaybackState::Paused)
        || !std::isfinite(m_activeSynthGain.load(std::memory_order_acquire))
        || m_activeSynthGain.load(std::memory_order_acquire) <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "live MIDI gain A/B requires a prepared Direct MIDI source in Playing or Paused state");
        }
        return 0;
    }

    Worker::Command command;
    command.type = Worker::CommandType::SetGain;
    command.gain = gain;
    Worker::Reply &context = command.gainRequestContext;
    context.requestId = m_nextGainRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (context.requestId == 0) {
        context.requestId = m_nextGainRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    context.requestedGain = gain;
    context.previousSelectedGain = m_selectedRealtimeSynthGain.load(std::memory_order_acquire);
    context.previousEffectiveGain = m_activeSynthGain.load(std::memory_order_acquire);
    context.effectiveGain = context.previousEffectiveGain;
    context.sourceBasename = QFileInfo(m_midiPath).fileName();
    context.requestedPositionMs = positionMilliseconds();
    context.requestedMasterVolume = m_masterVolume;
    context.requestedSinkVolume = m_audioSink == nullptr ? 0.0f : m_audioSink->volume();
    const Worker::Reply requestContext = context;
    const quint64 requestId = context.requestId;
    if (!m_worker->enqueue(std::move(command))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("MIDI synthesis worker is shutting down");
        }
        return 0;
    }
    m_selectedRealtimeSynthGain.store(gain, std::memory_order_release);
    m_latestGainRequestId.store(requestId, std::memory_order_release);
    qInfo() << "[NENIA MIDI GAIN AB]"
            << "event=requested"
            << "request_id=" << requestId
            << "preset=" << (gain == 0.20 ? "A" : "B")
            << "requested_gain=" << gain
            << "source=" << requestContext.sourceBasename
            << "playback_position_ms=" << requestContext.requestedPositionMs
            << "controller_master_volume=" << requestContext.requestedMasterVolume
            << "qaudiosink_volume=" << requestContext.requestedSinkVolume
            << "engine=" << static_cast<const void *>(this);
    return requestId;
}

void NeniaMidiPlaybackEngine::completeRealtimeSynthGainChange(
    quint64 requestId,
    double requestedGain,
    double previousSelectedGain,
    double previousEffectiveGain,
    double effectiveGain,
    int activeVoicesBefore,
    int activeVoicesAfter,
    std::uint64_t workerIdentity,
    QString sourceBasename,
    qint64 requestedPositionMs,
    float requestedMasterVolume,
    float requestedSinkVolume,
    bool succeeded,
    QString errorMessage)
{
    if (!succeeded && m_latestGainRequestId.load(std::memory_order_acquire) == requestId) {
        double expected = requestedGain;
        (void)m_selectedRealtimeSynthGain.compare_exchange_strong(
            expected, previousSelectedGain, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    qInfo().noquote() << "[NENIA MIDI GAIN AB]"
                      << "event=" << (succeeded ? "applied" : "failed")
                      << "request_id=" << requestId
                      << "preset=" << (requestedGain == 0.20 ? "A" : "B")
                      << "requested_gain=" << requestedGain
                      << "previous_effective_gain=" << previousEffectiveGain
                      << "effective_gain=" << effectiveGain
                      << "active_voices_before=" << activeVoicesBefore
                      << "active_voices_after=" << activeVoicesAfter
                      << "source=" << sourceBasename
                      << "playback_position_ms=" << requestedPositionMs
                      << "controller_master_volume=" << requestedMasterVolume
                      << "qaudiosink_volume=" << requestedSinkVolume
                      << "engine=" << static_cast<const void *>(this)
                      << "worker_thread_identity=" << workerIdentity
                      << (errorMessage.isEmpty() ? QString{} : QStringLiteral("error=%1").arg(errorMessage));
    emit realtimeSynthGainChangeCompleted(requestId, requestedGain,
                                          previousEffectiveGain, effectiveGain,
                                          activeVoicesBefore, activeVoicesAfter,
                                          static_cast<quint64>(workerIdentity),
                                          succeeded, errorMessage);
}

void NeniaMidiPlaybackEngine::completeRealtimeLoudnessPresetChange(
    quint64 requestId,
    NeniaMidiLoudnessPreset preset,
    double synthGain,
    double postSynthGainDb,
    bool limiterEnabled,
    NeniaMidiLoudnessPreset previousSelectedPreset,
    int activeVoicesBefore,
    int activeVoicesAfter,
    std::uint64_t workerIdentity,
    bool succeeded,
    QString errorMessage)
{
    if (succeeded) {
        m_confirmedLoudnessPreset.store(static_cast<int>(preset),
                                        std::memory_order_release);
        if (m_latestLoudnessRequestId.load(std::memory_order_acquire) == requestId) {
            m_selectedLoudnessPreset.store(static_cast<int>(preset),
                                           std::memory_order_release);
            m_selectedRealtimeSynthGain.store(synthGain, std::memory_order_release);
        }
    } else if (m_latestLoudnessRequestId.load(std::memory_order_acquire) == requestId) {
        const NeniaMidiLoudnessPreset lastConfirmed = confirmedLoudnessPreset();
        m_selectedLoudnessPreset.store(static_cast<int>(lastConfirmed),
                                       std::memory_order_release);
        m_selectedRealtimeSynthGain.store(
            neniaMidiLoudnessPresetDefinition(lastConfirmed).synthGain,
            std::memory_order_release);
    }
    const auto &effective = neniaMidiLoudnessPresetDefinition(confirmedLoudnessPreset());
    qInfo().noquote() << (m_loudnessTrialEnabled ? "[NENIA MIDI LOUDNESS TRIAL]"
                                                 : "[NENIA MIDI PRESET]")
                      << "event=" << (succeeded ? "applied" : "failed")
                      << "request_id=" << requestId
                      << "requested_preset=" << neniaMidiLoudnessPresetDefinition(preset).name
                      << "previous_selected_preset="
                      << neniaMidiLoudnessPresetDefinition(previousSelectedPreset).name
                      << "confirmed_preset=" << effective.name
                      << "synth_gain=" << synthGain
                      << "post_synth_gain_db=" << postSynthGainDb
                      << "limiter=" << limiterEnabled
                      << "ceiling_dbfs=" << (limiterEnabled ? effective.ceilingDbfs : 0.0)
                      << "active_voices_before=" << activeVoicesBefore
                      << "active_voices_after=" << activeVoicesAfter
                      << "source=" << QFileInfo(m_midiPath).fileName()
                      << "playback_position_ms=" << positionMilliseconds()
                      << "master_volume=" << m_masterVolume
                      << "sink_volume=" << (m_audioSink == nullptr ? 0.0f : m_audioSink->volume())
                      << "worker_thread_identity=" << workerIdentity
                      << (errorMessage.isEmpty() ? QString{} : QStringLiteral("error=%1").arg(errorMessage));
    emit realtimeLoudnessPresetChangeCompleted(
        requestId, preset, synthGain, postSynthGainDb, limiterEnabled,
        activeVoicesBefore, activeVoicesAfter,
        static_cast<quint64>(workerIdentity), succeeded, errorMessage);
}

bool NeniaMidiPlaybackEngine::simulateOutputFailureForTesting(const QString &message)
{
    if (m_outputMode != OutputMode::DeterministicTest) {
        return false;
    }
    m_callbackEnabled.store(false, std::memory_order_release);
    while (m_activeCallbacks.load(std::memory_order_acquire) != 0) {
        QThread::yieldCurrentThread();
    }
    setError(message.isEmpty() ? QStringLiteral("simulated output failure") : message);
    setState(NeniaMidiPlaybackState::Error);
    Worker::Command command;
    command.type = Worker::CommandType::Pause;
    (void)m_worker->submit(std::move(command));
    return true;
}

void NeniaMidiPlaybackEngine::consumeFloat(QSpan<float> output) noexcept
{
    consumeSamples(output.data(), static_cast<std::size_t>(output.size()), sizeof(float));
}

void NeniaMidiPlaybackEngine::consumeInt16(QSpan<std::int16_t> output) noexcept
{
    consumeSamples(output.data(), static_cast<std::size_t>(output.size()), sizeof(std::int16_t));
}

void NeniaMidiPlaybackEngine::consumeSamples(void *output,
                                             std::size_t sampleCount,
                                             std::size_t sampleBytes) noexcept
{
    if (output == nullptr || sampleBytes == 0) {
        return;
    }
    m_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    if (!m_callbackEnabled.load(std::memory_order_acquire)) {
        std::memset(output, 0, sampleCount * sampleBytes);
        m_activeCallbacks.fetch_sub(1, std::memory_order_release);
        return;
    }
    const std::size_t framesRequested = sampleCount / 2U;
    const NeniaMidiPlaybackState current = state();
    std::size_t framesRead = 0;
    if ((current == NeniaMidiPlaybackState::Playing
         || current == NeniaMidiPlaybackState::Draining)
        && m_pcmRing != nullptr) {
        framesRead = m_pcmRing->readFrames(output, framesRequested);
    }
    auto *bytes = static_cast<std::byte *>(output);
    const std::size_t missingFrames = framesRequested - framesRead;
    if (missingFrames > 0) {
        std::memset(bytes + framesRead * 2U * sampleBytes, 0,
                    missingFrames * 2U * sampleBytes);
    }
    if ((sampleCount % 2U) != 0U) {
        std::memset(bytes + (sampleCount - 1U) * sampleBytes, 0, sampleBytes);
    }
    if (m_traceCounters != nullptr) {
        PcmTraceBlock sinkInput;
        std::uint64_t silenceFrames = 0;
        if (sampleBytes == sizeof(float)) {
            const auto *samples = static_cast<const float *>(output);
            for (std::size_t frame = 0; frame < framesRequested; ++frame) {
                const float left = samples[frame * 2U];
                const float right = samples[frame * 2U + 1U];
                sinkInput.add(left);
                sinkInput.add(right);
                if (left == 0.0F && right == 0.0F) {
                    ++silenceFrames;
                }
            }
        } else if (sampleBytes == sizeof(std::int16_t)) {
            const auto *samples = static_cast<const std::int16_t *>(output);
            for (std::size_t frame = 0; frame < framesRequested; ++frame) {
                const float left = static_cast<float>(samples[frame * 2U]) / 32768.0F;
                const float right = static_cast<float>(samples[frame * 2U + 1U]) / 32768.0F;
                sinkInput.add(left);
                sinkInput.add(right);
                if (samples[frame * 2U] == 0 && samples[frame * 2U + 1U] == 0) {
                    ++silenceFrames;
                }
            }
        }
        m_traceCounters->addSink(sinkInput, framesRequested, framesRead, silenceFrames);
    }
    if (framesRead > 0) {
        m_consumedFrames.fetch_add(framesRead, std::memory_order_acq_rel);
    }
    if (missingFrames > 0 && m_renderFinished.load(std::memory_order_acquire) == false
        && (current == NeniaMidiPlaybackState::Playing
            || current == NeniaMidiPlaybackState::Draining)) {
        m_underrunCallbacks.fetch_add(1, std::memory_order_relaxed);
        m_underrunFrames.fetch_add(missingFrames, std::memory_order_relaxed);
    }
    m_activeCallbacks.fetch_sub(1, std::memory_order_release);
}

void NeniaMidiPlaybackEngine::setState(NeniaMidiPlaybackState next) noexcept
{
    m_state.store(next, std::memory_order_release);
}

void NeniaMidiPlaybackEngine::setError(const QString &message)
{
    std::lock_guard lock(m_diagnosticsMutex);
    m_errorMessage = message;
}

void NeniaMidiPlaybackEngine::queueNaturalEnd()
{
    QMetaObject::invokeMethod(this, [this] { handleNaturalEnd(); }, Qt::QueuedConnection);
}

void NeniaMidiPlaybackEngine::handleNaturalEnd()
{
    if (state() != NeniaMidiPlaybackState::Ended) {
        return;
    }
    stopSink();
    emit endOfMedia();
}

} // namespace nenia::detail
