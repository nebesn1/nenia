#pragma once

#include <nenia/TimelineEvent.h>

#include <QtGlobal>

#include <cstddef>
#include <optional>
#include <span>

namespace nenia {

class TimelineEventProvider
{
public:
    virtual ~TimelineEventProvider() = default;

    virtual bool isLoaded() const noexcept = 0;
    virtual std::size_t eventCount() const noexcept = 0;
    virtual std::optional<qint64> firstEventTimeMs() const noexcept = 0;
    virtual std::optional<qint64> lastEventTimeMs() const noexcept = 0;

    // Returns events in (fromExclusiveMs, toInclusiveMs]. The span remains
    // valid until the provider is loaded again or cleared.
    virtual std::span<const TimelineEvent> eventsBetween(
        qint64 fromExclusiveMs,
        qint64 toInclusiveMs) const noexcept = 0;

    virtual void clear() noexcept = 0;
};

} // namespace nenia
