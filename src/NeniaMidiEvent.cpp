#include "NeniaMidiEvent.h"

#include <type_traits>

namespace nenia::detail {

NeniaMidiEventCategory NeniaMidiEvent::category() const noexcept
{
    return std::visit([](const auto &value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, NeniaMidiSystemCommonEvent>) {
            return NeniaMidiEventCategory::SystemCommon;
        } else if constexpr (std::is_same_v<Event, NeniaMidiSysExEvent>) {
            return NeniaMidiEventCategory::SystemExclusive;
        } else if constexpr (std::is_same_v<Event, NeniaMidiMetaEvent>) {
            return NeniaMidiEventCategory::Meta;
        } else {
            return NeniaMidiEventCategory::ChannelVoice;
        }
    }, payload);
}

} // namespace nenia::detail
