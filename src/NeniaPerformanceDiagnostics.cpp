#include "NeniaPerformanceDiagnostics.h"

#include "NeniaSpectrumFrame.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>
#include <QDebug>

#include <algorithm>
#include <bit>
#include <limits>

namespace nenia::detail {
namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
constexpr std::uint64_t kNanosecondsPerMicrosecond = 1'000;
constexpr std::uint64_t kHeartbeatIntervalNanoseconds = 100'000'000;
constexpr std::uint64_t kReportIntervalNanoseconds = 5'000'000'000;

constexpr std::array<const char *, static_cast<std::size_t>(
    NeniaPerformanceDiagnostics::Timing::Count)> kTimingNames{
    "render_current_state",
    "renderer",
    "paint_event",
    "paint_image_blit",
    "gui_heartbeat_interval",
    "gui_heartbeat_lateness",
    "marquee_timer_interval",
    "marquee_timer_lateness",
    "playback_sync_interval",
    "playback_sync_lateness",
    "pcm_slot",
    "pcm_analysis_and_store",
    "metadata_read",
    "set_source",
    "artwork_cache_rebuild",
    "input_up",
    "input_down",
    "input_play_pause",
    "input_enter_activation",
    "input_double_click_activation",
    "input_next",
    "input_previous",
    "input_seek_backward",
    "input_seek_forward"
};

constexpr std::array<const char *, static_cast<std::size_t>(
    NeniaPerformanceDiagnostics::Counter::Count)> kCounterNames{
    "visual_refresh_requests",
    "render_count",
    "widget_update_requests",
    "paint_count",
    "paint_region_pixels",
    "paint_widget_pixels",
    "full_rendered_logical_pixels",
    "surface_reallocations",
    "geometry_changes",
    "pcm_buffer_count",
    "pcm_frames",
    "pcm_bytes",
    "spectrum_frames",
    "frame_insertions",
    "frame_non_append_insertions",
    "frame_store_clears",
    "artwork_cache_rebuilds"
};

std::uint64_t percentileUpperBoundNanoseconds(
    const NeniaPerformanceDiagnostics::Distribution &distribution,
    std::uint64_t percentile) noexcept
{
    if (distribution.count == 0) {
        return 0;
    }

    const std::uint64_t rank =
        (distribution.count * percentile + 99U) / 100U;
    std::uint64_t cumulative = 0;
    for (std::size_t bucket = 0; bucket < distribution.buckets.size(); ++bucket) {
        cumulative += distribution.buckets[bucket];
        if (cumulative >= rank) {
            const std::uint64_t upperBoundMicroseconds =
                std::uint64_t{1} << bucket;
            return upperBoundMicroseconds * kNanosecondsPerMicrosecond;
        }
    }
    return distribution.maximumNanoseconds;
}

double milliseconds(std::uint64_t nanoseconds) noexcept
{
    return static_cast<double>(nanoseconds)
        / static_cast<double>(kNanosecondsPerMillisecond);
}

QJsonObject distributionObject(
    const NeniaPerformanceDiagnostics::Distribution &distribution)
{
    QJsonObject result;
    result.insert(QStringLiteral("count"),
                  static_cast<qint64>(distribution.count));
    result.insert(QStringLiteral("total_ms"), milliseconds(distribution.totalNanoseconds));
    result.insert(QStringLiteral("p50_ms"), milliseconds(
        percentileUpperBoundNanoseconds(distribution, 50)));
    result.insert(QStringLiteral("p95_ms"), milliseconds(
        percentileUpperBoundNanoseconds(distribution, 95)));
    result.insert(QStringLiteral("p99_ms"), milliseconds(
        percentileUpperBoundNanoseconds(distribution, 99)));
    result.insert(QStringLiteral("max_ms"), milliseconds(distribution.maximumNanoseconds));
    return result;
}

QString windowStateName(const QWidget &widget)
{
    const QWidget *topLevel = widget.window();
    const Qt::WindowStates states = topLevel != nullptr
        ? topLevel->windowState() : Qt::WindowNoState;
    if (states.testFlag(Qt::WindowMinimized)) {
        return QStringLiteral("minimized");
    }
    if (states.testFlag(Qt::WindowFullScreen)) {
        return QStringLiteral("fullscreen");
    }
    if (states.testFlag(Qt::WindowMaximized)) {
        return QStringLiteral("maximized");
    }
    return QStringLiteral("normal");
}

QJsonObject geometryObject(const QWidget &widget)
{
    const double dpr = widget.devicePixelRatioF();
    QJsonObject geometry;
    geometry.insert(QStringLiteral("widget_width"), widget.width());
    geometry.insert(QStringLiteral("widget_height"), widget.height());
    geometry.insert(QStringLiteral("device_pixel_ratio"), dpr);
    geometry.insert(QStringLiteral("physical_width_approx"), qRound(widget.width() * dpr));
    geometry.insert(QStringLiteral("physical_height_approx"), qRound(widget.height() * dpr));
    geometry.insert(QStringLiteral("window_state"), windowStateName(widget));
    geometry.insert(QStringLiteral("visible"), widget.isVisible());
    return geometry;
}

QString pcmSampleFormatName(int value)
{
    switch (static_cast<QAudioFormat::SampleFormat>(value)) {
    case QAudioFormat::UInt8:
        return QStringLiteral("UInt8");
    case QAudioFormat::Int16:
        return QStringLiteral("Int16");
    case QAudioFormat::Int32:
        return QStringLiteral("Int32");
    case QAudioFormat::Float:
        return QStringLiteral("Float");
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        break;
    }
    return QStringLiteral("Unknown");
}

} // namespace

NeniaPerformanceDiagnostics::NeniaPerformanceDiagnostics(bool enabledForTesting) noexcept
    : m_enabled(enabledForTesting)
    , m_environmentInitialized(true)
{
}

NeniaPerformanceDiagnostics &NeniaPerformanceDiagnostics::instance() noexcept
{
    static NeniaPerformanceDiagnostics diagnostics;
    return diagnostics;
}

bool NeniaPerformanceDiagnostics::environmentEnablesTracing()
{
    return qgetenv("NENIA_PERF_TRACE") == QByteArrayLiteral("1");
}

NeniaPerformanceDiagnostics::SourceType
NeniaPerformanceDiagnostics::sourceTypeForUrl(const QUrl &source)
{
    if (source.isEmpty()) {
        return SourceType::None;
    }

    const QString path = source.path().toLower();
    if (path.endsWith(QStringLiteral(".flac"))) {
        return SourceType::Flac;
    }
    if (path.endsWith(QStringLiteral(".ogg")) || path.endsWith(QStringLiteral(".oga"))) {
        return SourceType::Ogg;
    }
    if (path.endsWith(QStringLiteral(".mp3"))) {
        return SourceType::Mp3;
    }
    if (path.endsWith(QStringLiteral(".wav")) || path.endsWith(QStringLiteral(".wave"))) {
        return SourceType::Wav;
    }
    if (path.endsWith(QStringLiteral(".mid")) || path.endsWith(QStringLiteral(".midi"))) {
        return SourceType::Midi;
    }
    return SourceType::Other;
}

const char *NeniaPerformanceDiagnostics::sourceTypeName(SourceType sourceType) noexcept
{
    switch (sourceType) {
    case SourceType::None: return "none";
    case SourceType::Flac: return "FLAC";
    case SourceType::Ogg: return "OGG";
    case SourceType::Mp3: return "MP3";
    case SourceType::Wav: return "WAV";
    case SourceType::Midi: return "MIDI";
    case SourceType::Other: return "other";
    }
    return "other";
}

const char *NeniaPerformanceDiagnostics::playbackStateName(PlaybackState state) noexcept
{
    switch (state) {
    case PlaybackState::Unknown: return "unknown";
    case PlaybackState::Stopped: return "stopped";
    case PlaybackState::Playing: return "playing";
    case PlaybackState::Paused: return "paused";
    }
    return "unknown";
}

void NeniaPerformanceDiagnostics::initializeForWidget(QWidget *widget)
{
    if (widget == nullptr) {
        return;
    }

    if (!m_environmentInitialized) {
        m_enabled.store(environmentEnablesTracing(), std::memory_order_release);
        m_environmentInitialized = true;
    }
    if (!enabled() || m_reportTimer != nullptr) {
        return;
    }

    m_reportTimer = new QTimer(widget);
    m_reportTimer->setInterval(100);
    QObject::connect(m_reportTimer, &QTimer::timeout, widget,
                     [this, widget] { heartbeat(widget); });
    m_heartbeatClock.start();
    m_reportClock.start();
    m_reportTimer->start();
}

bool NeniaPerformanceDiagnostics::enabled() const noexcept
{
    return m_enabled.load(std::memory_order_relaxed);
}

void NeniaPerformanceDiagnostics::recordCounter(Counter counter,
                                                std::uint64_t amount) noexcept
{
    if (!enabled() || amount == 0) {
        return;
    }
    m_counters[index(counter)].fetch_add(amount, std::memory_order_relaxed);
}

void NeniaPerformanceDiagnostics::recordInto(AtomicDistribution &distribution,
                                             std::uint64_t nanoseconds) noexcept
{
    const std::uint64_t microseconds =
        (nanoseconds + kNanosecondsPerMicrosecond - 1U) / kNanosecondsPerMicrosecond;
    const std::size_t bucket = microseconds <= 1U
        ? 0U
        : std::min<std::size_t>(std::bit_width(microseconds - 1U),
                                kHistogramBucketCount - 1U);
    distribution.buckets[bucket].fetch_add(1, std::memory_order_relaxed);
    distribution.totalNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    distribution.count.fetch_add(1, std::memory_order_relaxed);

    std::uint64_t currentMaximum =
        distribution.maximumNanoseconds.load(std::memory_order_relaxed);
    while (currentMaximum < nanoseconds
           && !distribution.maximumNanoseconds.compare_exchange_weak(
               currentMaximum, nanoseconds, std::memory_order_relaxed)) {
    }
}

void NeniaPerformanceDiagnostics::recordDuration(Timing timing,
                                                std::uint64_t nanoseconds) noexcept
{
    if (!enabled()) {
        return;
    }
    recordInto(m_timings[index(timing)], nanoseconds);
}

void NeniaPerformanceDiagnostics::recordTimerInterval(
    Timing intervalTiming,
    Timing latenessTiming,
    std::uint64_t actualIntervalNanoseconds,
    std::uint64_t expectedIntervalNanoseconds) noexcept
{
    if (!enabled()) {
        return;
    }
    recordInto(m_timings[index(intervalTiming)], actualIntervalNanoseconds);
    recordInto(m_timings[index(latenessTiming)],
               actualIntervalNanoseconds > expectedIntervalNanoseconds
                   ? actualIntervalNanoseconds - expectedIntervalNanoseconds : 0U);
}

void NeniaPerformanceDiagnostics::recordFullRender(int width, int height) noexcept
{
    if (!enabled()) {
        return;
    }
    recordCounter(Counter::FullRenders);
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto pixels = static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height);
    recordCounter(Counter::FullRenderPixels, pixels);
}

void NeniaPerformanceDiagnostics::recordPaint(const QRegion &region,
                                             int widgetWidth,
                                             int widgetHeight) noexcept
{
    if (!enabled()) {
        return;
    }
    recordCounter(Counter::PaintEvents);
    if (widgetWidth > 0 && widgetHeight > 0) {
        recordCounter(Counter::PaintWidgetPixels,
                      static_cast<std::uint64_t>(widgetWidth)
                          * static_cast<std::uint64_t>(widgetHeight));
    }
    std::uint64_t regionPixels = 0;
    for (const QRect &rect : region) {
        if (rect.width() > 0 && rect.height() > 0) {
            regionPixels += static_cast<std::uint64_t>(rect.width())
                * static_cast<std::uint64_t>(rect.height());
        }
    }
    recordCounter(Counter::PaintRegionPixels, regionPixels);
}

void NeniaPerformanceDiagnostics::recordGeometryChange() noexcept
{
    if (!enabled()) {
        return;
    }
    recordCounter(Counter::GeometryChanges);
    m_geometryChanged.store(true, std::memory_order_relaxed);
}

void NeniaPerformanceDiagnostics::recordPcmBuffer(const QAudioFormat &format,
                                                  std::uint64_t frameCount,
                                                  std::uint64_t byteCount) noexcept
{
    if (!enabled()) {
        return;
    }
    recordCounter(Counter::PcmBuffers);
    recordCounter(Counter::PcmFrames, frameCount);
    recordCounter(Counter::PcmBytes, byteCount);
    m_pcmSampleRate.store(format.sampleRate(), std::memory_order_relaxed);
    m_pcmChannels.store(format.channelCount(), std::memory_order_relaxed);
    m_pcmSampleFormat.store(static_cast<int>(format.sampleFormat()),
                            std::memory_order_relaxed);
}

void NeniaPerformanceDiagnostics::recordFrameStoreInsert(
    std::uint64_t size,
    bool insertedNewFrame,
    bool appended) noexcept
{
    if (!enabled()) {
        return;
    }
    m_currentFrameStoreSize.store(size, std::memory_order_relaxed);
    if (!insertedNewFrame) {
        return;
    }
    recordCounter(Counter::FrameInsertions);
    if (!appended) {
        recordCounter(Counter::FrameNonAppendInsertions);
    }
}

void NeniaPerformanceDiagnostics::recordFrameStoreClear() noexcept
{
    if (!enabled()) {
        return;
    }
    m_currentFrameStoreSize.store(0, std::memory_order_relaxed);
    recordCounter(Counter::FrameStoreClears);
}

void NeniaPerformanceDiagnostics::recordArtworkCacheRebuild() noexcept
{
    recordCounter(Counter::ArtworkCacheRebuilds);
}

void NeniaPerformanceDiagnostics::setSourceType(SourceType sourceType) noexcept
{
    if (enabled()) {
        m_sourceType.store(static_cast<int>(sourceType), std::memory_order_relaxed);
    }
}

void NeniaPerformanceDiagnostics::setPlaybackState(PlaybackState state) noexcept
{
    if (enabled()) {
        m_playbackState.store(static_cast<int>(state), std::memory_order_relaxed);
    }
}

void NeniaPerformanceDiagnostics::setPcmCallbackOnGuiThread(bool onGuiThread) noexcept
{
    if (enabled()) {
        m_pcmCallbackOnGuiThread.store(onGuiThread, std::memory_order_relaxed);
    }
}

void NeniaPerformanceDiagnostics::recordSpectrumFrames(std::uint64_t frameCount) noexcept
{
    recordCounter(Counter::SpectrumFrames, frameCount);
}

NeniaPerformanceDiagnostics::Distribution
NeniaPerformanceDiagnostics::takeDistribution(AtomicDistribution &distribution) noexcept
{
    Distribution result;
    result.count = distribution.count.exchange(0, std::memory_order_relaxed);
    result.totalNanoseconds =
        distribution.totalNanoseconds.exchange(0, std::memory_order_relaxed);
    result.maximumNanoseconds =
        distribution.maximumNanoseconds.exchange(0, std::memory_order_relaxed);
    for (std::size_t i = 0; i < result.buckets.size(); ++i) {
        result.buckets[i] = distribution.buckets[i].exchange(0, std::memory_order_relaxed);
    }
    return result;
}

NeniaPerformanceDiagnostics::Snapshot
NeniaPerformanceDiagnostics::takeSnapshotAndReset() noexcept
{
    Snapshot result;
    for (std::size_t i = 0; i < result.counters.size(); ++i) {
        result.counters[i] = m_counters[i].exchange(0, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < result.timings.size(); ++i) {
        result.timings[i] = takeDistribution(m_timings[i]);
    }
    result.currentFrameStoreSize =
        m_currentFrameStoreSize.load(std::memory_order_relaxed);
    result.sourceType = m_sourceType.load(std::memory_order_relaxed);
    result.playbackState = m_playbackState.load(std::memory_order_relaxed);
    result.pcmSampleRate = m_pcmSampleRate.load(std::memory_order_relaxed);
    result.pcmChannels = m_pcmChannels.load(std::memory_order_relaxed);
    result.pcmSampleFormat = m_pcmSampleFormat.load(std::memory_order_relaxed);
    result.pcmCallbackOnGuiThread =
        m_pcmCallbackOnGuiThread.load(std::memory_order_relaxed);
    return result;
}

QByteArray NeniaPerformanceDiagnostics::makeReport(const QWidget &widget,
                                                   std::uint64_t intervalNanoseconds)
{
    if (!enabled()) {
        return {};
    }

    const Snapshot snapshot = takeSnapshotAndReset();
    QJsonObject report;
    report.insert(QStringLiteral("interval_ms"), milliseconds(intervalNanoseconds));
    report.insert(QStringLiteral("source_type"), QString::fromLatin1(sourceTypeName(
        static_cast<SourceType>(snapshot.sourceType))));
    report.insert(QStringLiteral("playback_state"), QString::fromLatin1(playbackStateName(
        static_cast<PlaybackState>(snapshot.playbackState))));

    report.insert(QStringLiteral("geometry"), geometryObject(widget));

    for (std::size_t i = 0; i < snapshot.counters.size(); ++i) {
        report.insert(QString::fromLatin1(kCounterNames[i]),
                      static_cast<qint64>(snapshot.counters[i]));
    }
    report.insert(QStringLiteral("full_render_argb32_bytes_est"),
                  static_cast<double>(snapshot.counters[index(Counter::FullRenderPixels)])
                      * 4.0);
    const std::uint64_t fullWidgetPixels = snapshot.counters[index(Counter::PaintWidgetPixels)];
    const double paintRegionPercent = fullWidgetPixels == 0
        ? 0.0
        : static_cast<double>(snapshot.counters[index(Counter::PaintRegionPixels)])
            * 100.0 / static_cast<double>(fullWidgetPixels);
    report.insert(QStringLiteral("paint_region_vs_widget_percent"), paintRegionPercent);
    report.insert(QStringLiteral("paint_draws_full_cached_image"), true);

    for (std::size_t i = 0; i < snapshot.timings.size(); ++i) {
        if (i >= index(Timing::InputUp) && i <= index(Timing::InputSeekForward)) {
            continue;
        }
        report.insert(QString::fromLatin1(kTimingNames[i]),
                      distributionObject(snapshot.timings[i]));
    }

    const double intervalSeconds = intervalNanoseconds == 0
        ? 0.0 : static_cast<double>(intervalNanoseconds) / 1'000'000'000.0;
    const std::uint64_t pcmBuffers = snapshot.counters[index(Counter::PcmBuffers)];
    QJsonObject pcm;
    pcm.insert(QStringLiteral("buffers_per_second"), intervalSeconds == 0.0
        ? 0.0 : static_cast<double>(pcmBuffers) / intervalSeconds);
    pcm.insert(QStringLiteral("average_frames_per_buffer"), pcmBuffers == 0
        ? 0.0 : static_cast<double>(snapshot.counters[index(Counter::PcmFrames)])
            / static_cast<double>(pcmBuffers));
    pcm.insert(QStringLiteral("average_bytes_per_buffer"), pcmBuffers == 0
        ? 0.0 : static_cast<double>(snapshot.counters[index(Counter::PcmBytes)])
            / static_cast<double>(pcmBuffers));
    pcm.insert(QStringLiteral("sample_rate_hz"), snapshot.pcmSampleRate);
    pcm.insert(QStringLiteral("channels"), snapshot.pcmChannels);
    pcm.insert(QStringLiteral("sample_format"), pcmSampleFormatName(snapshot.pcmSampleFormat));
    pcm.insert(QStringLiteral("callback_on_gui_thread"),
               snapshot.pcmCallbackOnGuiThread);
    report.insert(QStringLiteral("pcm"), pcm);

    const std::uint64_t frameCount = snapshot.currentFrameStoreSize;
    QJsonObject frameStore;
    frameStore.insert(QStringLiteral("current_frames"), static_cast<qint64>(frameCount));
    frameStore.insert(QStringLiteral("estimated_payload_bytes"),
                      static_cast<double>(frameCount * sizeof(SpectrumFrame)));
    frameStore.insert(QStringLiteral("insertions"),
                      static_cast<qint64>(snapshot.counters[index(Counter::FrameInsertions)]));
    frameStore.insert(QStringLiteral("non_append_insertions"), static_cast<qint64>(
        snapshot.counters[index(Counter::FrameNonAppendInsertions)]));
    frameStore.insert(QStringLiteral("clears"),
                      static_cast<qint64>(snapshot.counters[index(Counter::FrameStoreClears)]));
    report.insert(QStringLiteral("frame_store"), frameStore);

    QJsonObject inputs;
    constexpr std::array<Timing, 9> inputTimings{
        Timing::InputUp, Timing::InputDown, Timing::InputPlayPause,
        Timing::InputEnterActivation, Timing::InputDoubleClickActivation,
        Timing::InputNext, Timing::InputPrevious,
        Timing::InputSeekBackward, Timing::InputSeekForward
    };
    constexpr std::array<const char *, inputTimings.size()> inputNames{
        "up", "down", "play_pause", "enter_activation", "double_click_activation",
        "next", "previous",
        "seek_backward", "seek_forward"
    };
    for (std::size_t i = 0; i < inputTimings.size(); ++i) {
        inputs.insert(QString::fromLatin1(inputNames[i]),
                      distributionObject(snapshot.timings[index(inputTimings[i])]));
    }
    report.insert(QStringLiteral("input_action_handling"), inputs);

    return QJsonDocument(report).toJson(QJsonDocument::Compact);
}

void NeniaPerformanceDiagnostics::heartbeat(QWidget *widget)
{
    if (!enabled() || widget == nullptr) {
        return;
    }

    const qint64 interval = m_heartbeatClock.nsecsElapsed();
    m_heartbeatClock.restart();
    if (interval > 0) {
        recordTimerInterval(Timing::GuiHeartbeatInterval,
                            Timing::GuiHeartbeatLateness,
                            static_cast<std::uint64_t>(interval),
                            kHeartbeatIntervalNanoseconds);
    }

    if (m_geometryChanged.exchange(false, std::memory_order_relaxed)) {
        const qint64 sinceLastGeometryReport = m_geometryReportClock.isValid()
            ? m_geometryReportClock.nsecsElapsed() : std::numeric_limits<qint64>::max();
        if (sinceLastGeometryReport >= 1'000'000'000) {
            if (m_geometryReportClock.isValid()) {
                m_geometryReportClock.restart();
            } else {
                m_geometryReportClock.start();
            }
            QJsonObject geometryEvent;
            geometryEvent.insert(QStringLiteral("event"), QStringLiteral("geometry"));
            geometryEvent.insert(QStringLiteral("geometry"), geometryObject(*widget));
            qInfo().noquote() << QStringLiteral("[NENIA PERF] %1")
                                     .arg(QString::fromUtf8(
                                         QJsonDocument(geometryEvent).toJson(
                                             QJsonDocument::Compact)));
        } else {
            m_geometryChanged.store(true, std::memory_order_relaxed);
        }
    }

    const qint64 elapsed = m_reportClock.nsecsElapsed();
    if (elapsed < 0 || static_cast<std::uint64_t>(elapsed) < kReportIntervalNanoseconds) {
        return;
    }
    m_reportClock.restart();
    const QByteArray json = makeReport(*widget, static_cast<std::uint64_t>(elapsed));
    qInfo().noquote() << QStringLiteral("[NENIA PERF] %1")
                             .arg(QString::fromUtf8(json));
}

ScopedPerformanceTimer::ScopedPerformanceTimer(
    NeniaPerformanceDiagnostics::Timing timing) noexcept
    : m_diagnostics(&NeniaPerformanceDiagnostics::instance())
    , m_timing(timing)
{
    if (m_diagnostics->enabled()) {
        m_clock.start();
    } else {
        m_diagnostics = nullptr;
    }
}

ScopedPerformanceTimer::~ScopedPerformanceTimer()
{
    if (m_diagnostics != nullptr) {
        m_diagnostics->recordDuration(m_timing,
            static_cast<std::uint64_t>(std::max<qint64>(m_clock.nsecsElapsed(), 0)));
    }
}

} // namespace nenia::detail
