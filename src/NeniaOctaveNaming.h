#pragma once

#include <QString>

#include <array>
#include <optional>
#include <string_view>

namespace nenia::detail {

enum class OctaveNamingConvention
{
    MiddleC3,
    MiddleC4,
    MiddleC5
};

struct OctaveNamingDescriptor
{
    OctaveNamingConvention convention;
    int middleCOctave;
    int octaveOffset;
    const char *persistenceToken;
    const char *displayNameUtf8;
};

inline constexpr std::array<OctaveNamingDescriptor, 3> kOctaveNamingDescriptors = {{
    {OctaveNamingConvention::MiddleC3, 3, -2, "middleC3", "Middle C = C3"},
    {OctaveNamingConvention::MiddleC4, 4, -1, "middleC4", "Middle C = C4"},
    {OctaveNamingConvention::MiddleC5, 5, 0, "middleC5", "Middle C = C5"}
}};

inline constexpr OctaveNamingConvention kDefaultOctaveNamingConvention =
    OctaveNamingConvention::MiddleC4;

inline constexpr const OctaveNamingDescriptor &octaveNamingDescriptor(
    OctaveNamingConvention convention) noexcept
{
    for (const auto &descriptor : kOctaveNamingDescriptors) {
        if (descriptor.convention == convention) {
            return descriptor;
        }
    }
    return kOctaveNamingDescriptors[1];
}

inline constexpr std::optional<OctaveNamingConvention> octaveNamingConventionForMiddleC(
    int middleCOctave) noexcept
{
    for (const auto &descriptor : kOctaveNamingDescriptors) {
        if (descriptor.middleCOctave == middleCOctave) {
            return descriptor.convention;
        }
    }
    return std::nullopt;
}

inline constexpr std::optional<OctaveNamingConvention> octaveNamingConventionFromToken(
    std::string_view token) noexcept
{
    for (const auto &descriptor : kOctaveNamingDescriptors) {
        if (token == descriptor.persistenceToken) {
            return descriptor.convention;
        }
    }
    return std::nullopt;
}

QString noteNameForMidi(int note,
                        OctaveNamingConvention convention =
                            kDefaultOctaveNamingConvention);

} // namespace nenia::detail
