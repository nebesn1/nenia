#pragma once

#include <nenia/TimelineEventProvider.h>

#include <QByteArray>
#include <QString>

#include <vector>

namespace nenia {

class JsonTimelineProvider final : public TimelineEventProvider
{
public:
    JsonTimelineProvider() = default;

    bool load(const QString &path, QString *errorMessage = nullptr);
    bool loadJson(const QByteArray &json, QString *errorMessage = nullptr);

    bool isLoaded() const noexcept override;
    std::size_t eventCount() const noexcept override;
    std::optional<qint64> firstEventTimeMs() const noexcept override;
    std::optional<qint64> lastEventTimeMs() const noexcept override;
    std::span<const TimelineEvent> eventsBetween(
        qint64 fromExclusiveMs,
        qint64 toInclusiveMs) const noexcept override;
    void clear() noexcept override;

private:
    bool loadParsedJson(const QByteArray &json, QString *errorMessage);

    bool m_loaded = false;
    std::vector<TimelineEvent> m_events;
};

} // namespace nenia
