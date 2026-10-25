#include "NeniaSpectrumFrameStore.h"

#include "NeniaPerformanceDiagnostics.h"

#include <algorithm>

namespace nenia::detail {

void NeniaSpectrumFrameStore::clear() noexcept
{
    m_frames.clear();
    NeniaPerformanceDiagnostics::instance().recordFrameStoreClear();
}

void NeniaSpectrumFrameStore::insert(SpectrumFrame frame)
{
    if (frame.centerTimeUs < 0) {
        return;
    }

    const auto it = std::lower_bound(
        m_frames.begin(), m_frames.end(), frame.centerTimeUs,
        [](const SpectrumFrame &existing, qint64 centerTimeUs) {
            return existing.centerTimeUs < centerTimeUs;
        });

    if (it != m_frames.end() && it->centerTimeUs == frame.centerTimeUs) {
        *it = frame;
        NeniaPerformanceDiagnostics::instance().recordFrameStoreInsert(
            static_cast<std::uint64_t>(m_frames.size()), false, false);
    } else {
        const bool appended = it == m_frames.end();
        m_frames.insert(it, frame);
        NeniaPerformanceDiagnostics::instance().recordFrameStoreInsert(
            static_cast<std::uint64_t>(m_frames.size()), true, appended);
    }
}

std::optional<SpectrumFrame> NeniaSpectrumFrameStore::lookup(qint64 positionUs) const
{
    if (positionUs < 0 || m_frames.empty()) {
        return std::nullopt;
    }

    const auto it = std::upper_bound(
        m_frames.begin(), m_frames.end(), positionUs,
        [](qint64 position, const SpectrumFrame &frame) {
            return position < frame.centerTimeUs;
        });
    if (it == m_frames.begin()) {
        return std::nullopt;
    }

    const SpectrumFrame &candidate = *std::prev(it);
    if (positionUs - candidate.centerTimeUs > kSpectrumMaxAgeUs) {
        return std::nullopt;
    }
    return candidate;
}

std::size_t NeniaSpectrumFrameStore::size() const noexcept
{
    return m_frames.size();
}

bool NeniaSpectrumFrameStore::empty() const noexcept
{
    return m_frames.empty();
}

} // namespace nenia::detail
