#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QRegion>
#include <QUrl>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class QTimer;
class QWidget;

namespace nenia::detail {

class NeniaPerformanceDiagnostics final
{
public:
    static constexpr std::size_t kHistogramBucketCount = 32;

    enum class Counter : std::size_t {
        VisualRefreshRequests,
        FullRenders,
        WidgetUpdateRequests,
        PaintEvents,
        PaintRegionPixels,
        PaintWidgetPixels,
        FullRenderPixels,
        SurfaceReallocations,
        GeometryChanges,
        PcmBuffers,
        PcmFrames,
        PcmBytes,
        SpectrumFrames,
        FrameInsertions,
        FrameNonAppendInsertions,
        FrameStoreClears,
        ArtworkCacheRebuilds,
        Count
    };

    enum class Timing : std::size_t {
        RenderCurrentState,
        Renderer,
        PaintEvent,
        PaintImageBlit,
        GuiHeartbeatInterval,
        GuiHeartbeatLateness,
        MarqueeTimerInterval,
        MarqueeTimerLateness,
        PlaybackSyncInterval,
        PlaybackSyncLateness,
        PcmSlot,
        PcmAnalysisAndStore,
        MetadataRead,
        SetSource,
        ArtworkCacheRebuild,
        InputUp,
        InputDown,
        InputPlayPause,
        InputEnterActivation,
        InputDoubleClickActivation,
        InputNext,
        InputPrevious,
        InputSeekBackward,
        InputSeekForward,
        Count
    };

    enum class SourceType : int {
        None,
        Flac,
        Ogg,
        Mp3,
        Wav,
        Midi,
        Other
    };

    enum class PlaybackState : int {
        Unknown,
        Stopped,
        Playing,
        Paused
    };

    struct Distribution final {
        std::uint64_t count = 0;
        std::uint64_t totalNanoseconds = 0;
        std::uint64_t maximumNanoseconds = 0;
        std::array<std::uint64_t, kHistogramBucketCount> buckets{};
    };

    struct Snapshot final {
        std::array<std::uint64_t, static_cast<std::size_t>(Counter::Count)> counters{};
        std::array<Distribution, static_cast<std::size_t>(Timing::Count)> timings{};
        std::uint64_t currentFrameStoreSize = 0;
        int sourceType = static_cast<int>(SourceType::None);
        int playbackState = static_cast<int>(PlaybackState::Unknown);
        int pcmSampleRate = 0;
        int pcmChannels = 0;
        int pcmSampleFormat = static_cast<int>(QAudioFormat::Unknown);
        bool pcmCallbackOnGuiThread = false;
    };

    NeniaPerformanceDiagnostics() noexcept = default;
    explicit NeniaPerformanceDiagnostics(bool enabledForTesting) noexcept;

    static NeniaPerformanceDiagnostics &instance() noexcept;
    static bool environmentEnablesTracing();
    static SourceType sourceTypeForUrl(const QUrl &source);
    static const char *sourceTypeName(SourceType sourceType) noexcept;
    static const char *playbackStateName(PlaybackState state) noexcept;

    // Called once from the GUI thread after QApplication exists. The periodic
    // timer is only created when NENIA_PERF_TRACE is exactly "1".
    void initializeForWidget(QWidget *widget);
    [[nodiscard]] bool enabled() const noexcept;

    void recordCounter(Counter counter, std::uint64_t amount = 1) noexcept;
    void recordDuration(Timing timing, std::uint64_t nanoseconds) noexcept;
    void recordTimerInterval(Timing intervalTiming,
                             Timing latenessTiming,
                             std::uint64_t actualIntervalNanoseconds,
                             std::uint64_t expectedIntervalNanoseconds) noexcept;
    void recordFullRender(int width, int height) noexcept;
    void recordPaint(const QRegion &region, int widgetWidth, int widgetHeight) noexcept;
    void recordGeometryChange() noexcept;
    void recordPcmBuffer(const QAudioFormat &format,
                         std::uint64_t frameCount,
                         std::uint64_t byteCount) noexcept;
    void recordFrameStoreInsert(std::uint64_t size,
                                bool insertedNewFrame,
                                bool appended) noexcept;
    void recordFrameStoreClear() noexcept;
    void recordArtworkCacheRebuild() noexcept;
    void setSourceType(SourceType sourceType) noexcept;
    void setPlaybackState(PlaybackState state) noexcept;
    void setPcmCallbackOnGuiThread(bool onGuiThread) noexcept;
    void recordSpectrumFrames(std::uint64_t frameCount) noexcept;

    [[nodiscard]] Snapshot takeSnapshotAndReset() noexcept;
    [[nodiscard]] QByteArray makeReport(const QWidget &widget,
                                        std::uint64_t intervalNanoseconds);

private:
    struct AtomicDistribution final {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> totalNanoseconds{0};
        std::atomic<std::uint64_t> maximumNanoseconds{0};
        std::array<std::atomic<std::uint64_t>, kHistogramBucketCount> buckets{};
    };

    static constexpr std::size_t index(Counter value) noexcept
    {
        return static_cast<std::size_t>(value);
    }
    static constexpr std::size_t index(Timing value) noexcept
    {
        return static_cast<std::size_t>(value);
    }

    static void recordInto(AtomicDistribution &distribution,
                           std::uint64_t nanoseconds) noexcept;
    static Distribution takeDistribution(AtomicDistribution &distribution) noexcept;
    void heartbeat(QWidget *widget);

    std::atomic<bool> m_enabled{false};
    bool m_environmentInitialized = false;
    bool m_forcedEnabled = false;
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Counter::Count)>
        m_counters{};
    std::array<AtomicDistribution, static_cast<std::size_t>(Timing::Count)> m_timings{};
    std::atomic<std::uint64_t> m_currentFrameStoreSize{0};
    std::atomic<int> m_sourceType{static_cast<int>(SourceType::None)};
    std::atomic<int> m_playbackState{static_cast<int>(PlaybackState::Unknown)};
    std::atomic<int> m_pcmSampleRate{0};
    std::atomic<int> m_pcmChannels{0};
    std::atomic<int> m_pcmSampleFormat{static_cast<int>(QAudioFormat::Unknown)};
    std::atomic<bool> m_pcmCallbackOnGuiThread{false};
    std::atomic<bool> m_geometryChanged{false};
    QPointer<QTimer> m_reportTimer;
    QElapsedTimer m_heartbeatClock;
    QElapsedTimer m_reportClock;
    QElapsedTimer m_geometryReportClock;
};

class ScopedPerformanceTimer final
{
public:
    explicit ScopedPerformanceTimer(NeniaPerformanceDiagnostics::Timing timing) noexcept;
    ~ScopedPerformanceTimer();

    ScopedPerformanceTimer(const ScopedPerformanceTimer &) = delete;
    ScopedPerformanceTimer &operator=(const ScopedPerformanceTimer &) = delete;

private:
    NeniaPerformanceDiagnostics *m_diagnostics = nullptr;
    NeniaPerformanceDiagnostics::Timing m_timing;
    QElapsedTimer m_clock;
};

} // namespace nenia::detail
