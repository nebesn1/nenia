#pragma once

#include <nenia/TimelineEventProvider.h>
#include <nenia/NeniaMediaInfo.h>

#include "NeniaExtendedMediaInfo.h"
#include "NeniaSpectrumPcmBlock.h"

#include <QMediaPlayer>
#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>

namespace nenia::detail {

struct NeniaAlternateMediaPresentation
{
    NeniaMediaInfo primary;
    NeniaExtendedMediaInfo extended;
    QString location;
};

// Internal seam for an application-owned source backend. The installed Nenia
// API remains QMediaPlayer-based; this interface is not installed or exported.
class NeniaAlternatePlaybackBackend : public QObject
{
    Q_OBJECT

public:
    explicit NeniaAlternatePlaybackBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~NeniaAlternatePlaybackBackend() override = default;

    virtual bool acceptsSource(const QUrl &source) const noexcept = 0;
    virtual bool prepareSource(const QUrl &source, QString *errorMessage) = 0;
    virtual bool play(QString *errorMessage) = 0;
    virtual bool pause(QString *errorMessage) = 0;
    virtual bool stop(QString *errorMessage) = 0;
    virtual bool restart(QString *errorMessage) = 0;
    virtual bool setPosition(qint64 positionMs, QString *errorMessage) = 0;
    virtual void setMasterVolume(float volume) noexcept = 0;

    [[nodiscard]] virtual QMediaPlayer::PlaybackState playbackState() const noexcept = 0;
    [[nodiscard]] virtual QMediaPlayer::MediaStatus mediaStatus() const noexcept = 0;
    [[nodiscard]] virtual qint64 position() const noexcept = 0;
    [[nodiscard]] virtual qint64 duration() const noexcept = 0;
    [[nodiscard]] virtual QString formatDescription() const = 0;
    [[nodiscard]] virtual QString errorDescription() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const TimelineEventProvider>
    timelineProvider() const = 0;
    [[nodiscard]] virtual NeniaAlternateMediaPresentation mediaPresentation() const
    {
        return {};
    }
    [[nodiscard]] virtual int analysisSampleRate() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint64_t analysisGeneration() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint64_t consumedAnalysisFrame() const noexcept { return 0; }
    virtual bool takeEligibleAnalysisBlock(NeniaMidiPcmAnalysisBlock *block) noexcept
    {
        Q_UNUSED(block);
        return false;
    }

signals:
    void endOfMedia();
    void backendError(const QString &message);
};

} // namespace nenia::detail
