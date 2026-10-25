#pragma once

#include "NeniaSpectrumFrame.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace nenia::detail {

class NeniaSpectrumFrameStore final
{
public:
    void clear() noexcept;
    void insert(SpectrumFrame frame);

    std::optional<SpectrumFrame> lookup(qint64 positionUs) const;

    std::size_t size() const noexcept;
    bool empty() const noexcept;

private:
    std::vector<SpectrumFrame> m_frames;
};

} // namespace nenia::detail
