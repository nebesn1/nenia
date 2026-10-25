#pragma once

#include "NeniaMidiChannelState.h"
#include "NeniaPcmRingBuffer.h"
#include "NeniaMidiPcmAnalysisTap.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtCore/qspan.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

class QAudioSink;
class QTimer;

namespace nenia::detail {

enum class NeniaMidiLoudnessPreset : std::uint8_t { A, B, C, D, E, F, H };

struct NeniaMidiLoudnessPresetDefinition final {
    NeniaMidiLoudnessPreset preset = NeniaMidiLoudnessPreset::A;
    const char *name = "A";
    double synthGain = 0.20;
    double postSynthGainDb = 0.0;
    double ceilingDbfs = -1.0;
    bool limiterEnabled = false;
};

[[nodiscard]] const std::array<NeniaMidiLoudnessPresetDefinition, 7> &
neniaMidiLoudnessPresetDefinitions() noexcept;
[[nodiscard]] const NeniaMidiLoudnessPresetDefinition &
neniaMidiLoudnessPresetDefinition(NeniaMidiLoudnessPreset preset) noexcept;

enum class NeniaMidiLoudnessStartupMode : std::uint8_t {
    ProductionH,
    Legacy,
    DeveloperGain,
    DeveloperTrial,
    DeveloperConflict
};

struct NeniaMidiLoudnessStartupConfiguration final {
    NeniaMidiLoudnessStartupMode mode = NeniaMidiLoudnessStartupMode::ProductionH;
    NeniaMidiLoudnessPreset initialPreset = NeniaMidiLoudnessPreset::E;
    double synthGain = 0.40;
    QString requestedGainText = QStringLiteral("(production-E)");
    QString errorMessage;
    QString warningMessage;
    bool valid = true;
    bool explicitPolicy = false;
    bool productionHEnabled = true;
    bool developerTrialRequested = false;
    bool developerTrialEnabled = false;
    bool developerFlagConflict = false;
    bool gainAbEnabled = false;
    bool streamingProtectionEnabled = true;
    bool userPresetSelectionEnabled = false;
};

// Pure startup policy resolver. Passing empty policyValue selects implicit
// precedence; developer flags retain their established opt-in semantics.
[[nodiscard]] NeniaMidiLoudnessStartupConfiguration
resolveNeniaMidiLoudnessStartupConfiguration(
    const QByteArray &policyValue,
    const QByteArray &trialValue,
    const QByteArray &gainAbValue,
    const QByteArray &experimentalGainValue,
    bool experimentalGainWasSet = false,
    NeniaMidiLoudnessPreset preferredUserPreset = NeniaMidiLoudnessPreset::E);

enum class NeniaMidiPlaybackState : std::uint8_t {
    Empty,
    Ready,
    Playing,
    Paused,
    Stopped,
    Draining,
    Ended,
    Error
};

struct NeniaMidiPlaybackDiagnostics {
    QString midiPath;
    QString soundFontPath;
    QString outputDevice;
    QString outputFormat;
    QString errorMessage;
    std::uint32_t sampleRate = 0;
    std::size_t ringCapacityFrames = 0;
    std::size_t queuedPcmFrames = 0;
    std::uint64_t generatedPcmFrames = 0;
    std::uint64_t consumedPcmFrames = 0;
    std::uint64_t underrunCallbacks = 0;
    std::uint64_t underrunFrames = 0;
    std::uint64_t unsupportedSysExMessages = 0;
    std::uint64_t unsupportedSystemCommonMessages = 0;
    std::uint64_t ignoredNoteOffReleaseVelocities = 0;
    std::uint64_t droppedSpectrumAnalysisBlocks = 0;
    std::uint64_t tracePreClampSamples = 0;
    std::uint64_t tracePreClampFiniteSamples = 0;
    std::uint64_t tracePreClampNonFiniteSamples = 0;
    std::uint64_t tracePreClampOverrangeSamples = 0;
    std::uint64_t tracePostClampSamples = 0;
    std::uint64_t tracePostClampFiniteSamples = 0;
    std::uint64_t traceSinkInputSamples = 0;
    std::uint64_t traceSinkDeliveredFrames = 0;
    std::uint64_t traceSinkRingFrames = 0;
    std::uint64_t traceSinkSilenceFrames = 0;
    double requestedSynthGain = 0.0;
    double settingsSynthGain = 0.0;
    double effectiveSynthGain = 0.0;
    double tracePreClampPeak = 0.0;
    double tracePreClampRms = 0.0;
    double tracePostClampPeak = 0.0;
    double tracePostClampRms = 0.0;
    double traceSinkInputPeak = 0.0;
    double traceSinkInputRms = 0.0;
    double trialPostSynthGainDb = 0.0;
    double trialCeilingDbfs = 0.0;
    double trialMaximumGainReductionDb = 0.0;
    double trialLookaheadMilliseconds = 0.0;
    double trialSynthesisBlockAverageMilliseconds = 0.0;
    double trialSynthesisBlockWorstMilliseconds = 0.0;
    double trialDspBlockAverageMilliseconds = 0.0;
    double trialDspBlockWorstMilliseconds = 0.0;
    double trialReportedAudioLatencyMilliseconds = 0.0;
    std::uint64_t trialInputFrames = 0;
    std::uint64_t trialOutputFrames = 0;
    std::uint64_t trialPendingFrames = 0;
    std::uint64_t trialLimiterActiveFrames = 0;
    std::uint64_t trialPotentialOverrangeSamples = 0;
    std::uint64_t trialSafetyClampSamples = 0;
    std::uint64_t trialNonFiniteSamples = 0;
    std::uint64_t trialTimingDiscontinuities = 0;
    std::uint64_t trialRingWriteFailures = 0;
    std::uint64_t trialSynthesisBlocks = 0;
    std::uint64_t trialDspBlocks = 0;
    std::uint64_t trialScratchBytes = 0;
    std::uint64_t trialMaximumRingOccupancyFrames = 0;
    std::uint32_t trialLookaheadFrames = 0;
    NeniaMidiLoudnessPreset selectedLoudnessPreset = NeniaMidiLoudnessPreset::E;
    NeniaMidiLoudnessPreset confirmedLoudnessPreset = NeniaMidiLoudnessPreset::E;
    float controllerMasterVolume = 0.0f;
    float audioSinkVolume = 0.0f;
    bool audioSinkPresent = false;
    bool loudnessTrialEnabled = false;
    bool loudnessTrialConfigurationConflict = false;
    bool userMidiPresetSelectionEnabled = false;
    bool productionHEnabled = false;
    bool streamingProtectionEnabled = false;
    NeniaMidiLoudnessStartupMode startupMode = NeniaMidiLoudnessStartupMode::ProductionH;
    QString startupConfigurationError;
    QString startupConfigurationWarning;
    bool trialLimiterEnabled = false;
    int audioSinkState = -1;
    int audioSinkError = 0;
    std::array<bool, 16> midiChannelUsage{};
};

// Internal playback surface. This class is deliberately not installed. The
// application router reuses it behind NeniaPlaybackController.
class NeniaMidiPlaybackEngine final : public QObject {
    Q_OBJECT

public:
    enum class OutputMode { Device, DeterministicTest };

    explicit NeniaMidiPlaybackEngine(QObject *parent = nullptr);
    explicit NeniaMidiPlaybackEngine(OutputMode outputMode, QObject *parent = nullptr);
    NeniaMidiPlaybackEngine(OutputMode outputMode,
                            std::uint32_t deterministicSampleRateHz,
                            QObject *parent = nullptr,
                            NeniaMidiLoudnessPreset preferredUserPreset =
                                NeniaMidiLoudnessPreset::E);
    ~NeniaMidiPlaybackEngine() override;

    NeniaMidiPlaybackEngine(const NeniaMidiPlaybackEngine &) = delete;
    NeniaMidiPlaybackEngine &operator=(const NeniaMidiPlaybackEngine &) = delete;

    bool prepare(const QString &midiPath,
                 const QString &soundFontPath,
                 QString *errorMessage = nullptr);
    bool play(QString *errorMessage = nullptr);
    bool pause(QString *errorMessage = nullptr);
    bool resume(QString *errorMessage = nullptr);
    bool stop(QString *errorMessage = nullptr);
    bool restart(QString *errorMessage = nullptr);
    bool seekMilliseconds(qint64 positionMs, QString *errorMessage = nullptr);
    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] bool realtimeGainAbEnabled() const noexcept;
    [[nodiscard]] double selectedRealtimeSynthGain() const noexcept;
    quint64 requestRealtimeSynthGain(double gain, QString *errorMessage = nullptr);
    [[nodiscard]] bool realtimeLoudnessTrialEnabled() const noexcept;
    [[nodiscard]] bool realtimeLoudnessTrialConfigurationConflict() const noexcept;
    [[nodiscard]] bool userMidiPresetSelectionEnabled() const noexcept;
    bool setPreferredUserLoudnessPreset(NeniaMidiLoudnessPreset preset,
                                        QString *errorMessage = nullptr);
    [[nodiscard]] NeniaMidiLoudnessPreset selectedLoudnessPreset() const noexcept;
    [[nodiscard]] NeniaMidiLoudnessPreset confirmedLoudnessPreset() const noexcept;
    quint64 requestRealtimeLoudnessPreset(NeniaMidiLoudnessPreset preset,
                                          QString *errorMessage = nullptr);

    // Internal deterministic-consumer seam used by headless playback tests.
    bool consumePcmForTesting(std::span<float> output) noexcept;
    bool simulateOutputFailureForTesting(const QString &message);

    [[nodiscard]] NeniaMidiPlaybackState state() const noexcept;
    [[nodiscard]] std::shared_ptr<const class NeniaMidiSequence>
    sequenceSnapshot() const noexcept;
    [[nodiscard]] qint64 positionMilliseconds() const noexcept;
    [[nodiscard]] qint64 durationMilliseconds() const noexcept;
    [[nodiscard]] NeniaMidiChannelState channelStateSnapshot() const;
    [[nodiscard]] std::uint64_t analysisGeneration() const noexcept;
    [[nodiscard]] std::uint64_t consumedOutputFrame() const noexcept;
    [[nodiscard]] float masterVolume() const noexcept;
    // Internal deterministic-test seam; this engine header is not installed.
    [[nodiscard]] double realtimeSynthGainForTesting() const noexcept;
    [[nodiscard]] double selectedRealtimeSynthGainForTesting() const noexcept;
    [[nodiscard]] std::size_t queuedAnalysisBlocks() const noexcept;
    bool takeEligibleAnalysisBlock(NeniaMidiPcmAnalysisBlock *block) noexcept;
    [[nodiscard]] NeniaMidiPlaybackDiagnostics diagnostics() const;

signals:
    void endOfMedia();
    void realtimeSynthGainChangeCompleted(quint64 requestId,
                                          double requestedGain,
                                          double previousEffectiveGain,
                                          double effectiveGain,
                                          int activeVoicesBefore,
                                          int activeVoicesAfter,
                                          quint64 workerThreadIdentity,
                                          bool succeeded,
                                          const QString &errorMessage);
    void realtimeLoudnessPresetChangeCompleted(
        quint64 requestId, NeniaMidiLoudnessPreset preset,
        double synthGain, double postSynthGainDb, bool limiterEnabled,
        int activeVoicesBefore, int activeVoicesAfter,
        quint64 workerThreadIdentity, bool succeeded,
        const QString &errorMessage);

private:
    struct Worker;
    struct TraceCounters;

    void completeRealtimeSynthGainChange(quint64 requestId,
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
                                         QString errorMessage);
    void completeRealtimeLoudnessPresetChange(
        quint64 requestId, NeniaMidiLoudnessPreset preset,
        double synthGain, double postSynthGainDb, bool limiterEnabled,
        NeniaMidiLoudnessPreset previousSelectedPreset,
        int activeVoicesBefore, int activeVoicesAfter,
        std::uint64_t workerIdentity, bool succeeded, QString errorMessage);
    void reportAudioTrace();
    void reportLoudnessTrialDiagnostics();
    void logAudioTraceLifecycle(const QString &event) const;

    bool requireOwnerThread(QString *errorMessage) const;
    bool startSink(QString *errorMessage);
    bool waitForPrebuffer(QString *errorMessage);
    void stopSink() noexcept;
    void consumeFloat(QSpan<float> output) noexcept;
    void consumeInt16(QSpan<std::int16_t> output) noexcept;
    void consumeSamples(void *output, std::size_t sampleCount, std::size_t sampleBytes) noexcept;
    void setState(NeniaMidiPlaybackState state) noexcept;
    void setError(const QString &message);
    void queueNaturalEnd();
    void handleNaturalEnd();

    std::unique_ptr<Worker> m_worker;
    const OutputMode m_outputMode;
    const std::uint32_t m_deterministicSampleRateHz;
    const bool m_audioTraceEnabled;
    const NeniaMidiLoudnessStartupConfiguration m_startupConfiguration;
    QString m_requestedGainText;
    const bool m_loudnessTrialConfigurationConflict;
    const bool m_loudnessTrialEnabled;
    const bool m_streamingProtectionEnabled;
    const double m_realtimeSynthGain;
    const bool m_gainAbEnabled;
    std::atomic<double> m_selectedRealtimeSynthGain{0.20};
    std::atomic<quint64> m_nextGainRequestId{0};
    std::atomic<quint64> m_latestGainRequestId{0};
    std::atomic<double> m_settingsSynthGain{0.0};
    std::atomic<double> m_activeSynthGain{0.0};
    std::atomic<int> m_selectedLoudnessPreset{
        static_cast<int>(NeniaMidiLoudnessPreset::E)};
    std::atomic<int> m_confirmedLoudnessPreset{
        static_cast<int>(NeniaMidiLoudnessPreset::E)};
    std::atomic<quint64> m_nextLoudnessRequestId{0};
    std::atomic<quint64> m_latestLoudnessRequestId{0};
    std::atomic<double> m_trialPostSynthGainDb{0.0};
    std::atomic<double> m_trialCeilingDbfs{0.0};
    std::atomic<bool> m_trialLimiterEnabled{false};
    std::atomic<double> m_trialMaximumGainReductionDb{0.0};
    std::atomic<std::uint64_t> m_trialInputFrames{0};
    std::atomic<std::uint64_t> m_trialOutputFrames{0};
    std::atomic<std::uint64_t> m_trialPendingFrames{0};
    std::atomic<std::uint64_t> m_trialLimiterActiveFrames{0};
    std::atomic<std::uint64_t> m_trialPotentialOverrangeSamples{0};
    std::atomic<std::uint64_t> m_trialSafetyClampSamples{0};
    std::atomic<std::uint64_t> m_trialNonFiniteSamples{0};
    std::atomic<std::uint64_t> m_trialTimingDiscontinuities{0};
    std::atomic<std::uint64_t> m_trialRingWriteFailures{0};
    std::atomic<std::uint64_t> m_trialLookaheadFrames{0};
    std::atomic<std::uint64_t> m_trialScratchBytes{0};
    std::atomic<std::uint64_t> m_trialSynthesisNanoseconds{0};
    std::atomic<std::uint64_t> m_trialSynthesisWorstNanoseconds{0};
    std::atomic<std::uint64_t> m_trialSynthesisBlocks{0};
    std::atomic<std::uint64_t> m_trialDspNanoseconds{0};
    std::atomic<std::uint64_t> m_trialDspWorstNanoseconds{0};
    std::atomic<std::uint64_t> m_trialDspBlocks{0};
    std::atomic<std::uint64_t> m_trialMaximumRingOccupancyFrames{0};
    std::unique_ptr<TraceCounters> m_traceCounters;
    QTimer *m_traceTimer = nullptr;
    QTimer *m_loudnessTrialTimer = nullptr;
    std::unique_ptr<NeniaPcmRingBuffer> m_pcmRing;
    std::unique_ptr<NeniaMidiPcmAnalysisTap> m_analysisTap;
    std::unique_ptr<QAudioSink> m_audioSink;
    QAudioDevice m_outputDevice;
    QAudioFormat m_outputFormat;
    std::shared_ptr<const class NeniaMidiSequence> m_sequence;
    std::array<bool, 16> m_midiChannelUsage{};
    QString m_midiPath;
    QString m_soundFontPath;
    QString m_errorMessage;
    mutable std::mutex m_diagnosticsMutex;
    std::atomic<NeniaMidiPlaybackState> m_state{NeniaMidiPlaybackState::Empty};
    std::atomic<NeniaMidiPlaybackState> m_stateBeforePause{NeniaMidiPlaybackState::Playing};
    std::atomic<std::uint64_t> m_baseFrame{0};
    std::atomic<std::uint64_t> m_generatedFrames{0};
    std::atomic<std::uint64_t> m_consumedFrames{0};
    std::atomic<std::uint64_t> m_analysisGeneration{0};
    std::atomic<std::uint64_t> m_underrunCallbacks{0};
    std::atomic<std::uint64_t> m_underrunFrames{0};
    std::atomic<std::uint64_t> m_unsupportedSysEx{0};
    std::atomic<std::uint64_t> m_unsupportedSystemCommon{0};
    std::atomic<std::uint64_t> m_ignoredNoteOffVelocity{0};
    std::uint64_t m_durationFrames = 0;
    qint64 m_durationMs = 0;
    std::atomic<bool> m_expectedSinkStop{true};
    std::atomic<bool> m_callbackEnabled{false};
    std::atomic<std::uint32_t> m_activeCallbacks{0};
    std::atomic<bool> m_sinkNeedsStart{true};
    std::atomic<bool> m_endSignalPosted{false};
    std::atomic<bool> m_renderFinished{false};
    float m_masterVolume = 0.7f;
    std::uint64_t m_lastTraceConsumedFrames = 0;
    std::uint64_t m_lastTraceUnderrunFrames = 0;
    std::uint64_t m_lastTraceUnderrunCallbacks = 0;
};

} // namespace nenia::detail
