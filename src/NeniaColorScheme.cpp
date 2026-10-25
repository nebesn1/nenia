#include "NeniaColorScheme.h"

#include <atomic>

namespace nenia::detail {
namespace {

const std::array<NeniaColorSchemeDefinition, 7> kColorSchemes{{
    {
        NeniaColorScheme::Classic,
        "classic",
        "Nenia Classic",
        "",
        "",
        {
            QColor(QStringLiteral("#03050B")), QColor(QStringLiteral("#060C19")),
            QColor(QStringLiteral("#0A162A")), QColor(QStringLiteral("#081122")),
            QColor(QStringLiteral("#1C3954")), QColor(QStringLiteral("#3D6F8C")),
            QColor(QStringLiteral("#67DEFF")), QColor(QStringLiteral("#8AA9FF")),
            QColor(QStringLiteral("#D380FF")), QColor(QStringLiteral("#6FEFA4")),
            QColor(QStringLiteral("#DFF2FF")), QColor(QStringLiteral("#7597B1")),
            QColor(QStringLiteral("#366079")), QColor(QStringLiteral("#173049")),
            QColor(QStringLiteral("#264F6C")), QColor(QStringLiteral("#020409")),
            QColor(QStringLiteral("#3D6F8C")), QColor(QStringLiteral("#0A162A")),
            QColor(QStringLiteral("#DFF2FF")), QColor(QStringLiteral("#7597B1")),
            QColor(QStringLiteral("#DFF2FF")), QColor(QStringLiteral("#6FEFA4")),
            QColor(QStringLiteral("#8AA9FF")), QColor(QStringLiteral("#D380FF")),
            QColor(QStringLiteral("#D380FF")), QColor(QStringLiteral("#173049"))
        }
    },
    {
        NeniaColorScheme::NordDark,
        "nord-dark",
        "Nord Dark",
        "Nord",
        "Dark",
        {
            QColor(QStringLiteral("#2E3440")), QColor(QStringLiteral("#3B4252")),
            QColor(QStringLiteral("#434C5E")), QColor(QStringLiteral("#3B4252")),
            QColor(QStringLiteral("#4C566A")), QColor(QStringLiteral("#81A1C1")),
            QColor(QStringLiteral("#88C0D0")), QColor(QStringLiteral("#B48EAD")),
            QColor(QStringLiteral("#B48EAD")), QColor(QStringLiteral("#A3BE8C")),
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#D8DEE9")),
            QColor(QStringLiteral("#81A1C1")), QColor(QStringLiteral("#3B4252")),
            QColor(QStringLiteral("#81A1C1")), QColor(QStringLiteral("#2E3440")),
            QColor(QStringLiteral("#4C566A")), QColor(QStringLiteral("#4C566A")),
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#D8DEE9")),
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#A3BE8C")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#B48EAD")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#3B4252"))
        }
    },
    {
        NeniaColorScheme::NordLight,
        "nord-light",
        "Nord Light",
        "Nord",
        "Light",
        {
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#E5E9F0")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#E5E9F0")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#5E81AC")),
            QColor(QStringLiteral("#5E81AC")), QColor(QStringLiteral("#B48EAD")),
            QColor(QStringLiteral("#B48EAD")), QColor(QStringLiteral("#A3BE8C")),
            QColor(QStringLiteral("#2E3440")), QColor(QStringLiteral("#4C566A")),
            QColor(QStringLiteral("#4C566A")), QColor(QStringLiteral("#E5E9F0")),
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#2E3440")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#2E3440")),
            QColor(QStringLiteral("#ECEFF4")), QColor(QStringLiteral("#4C566A")),
            QColor(QStringLiteral("#2E3440")), QColor(QStringLiteral("#32653D")),
            QColor(QStringLiteral("#4C566A")), QColor(QStringLiteral("#73445D")),
            QColor(QStringLiteral("#A6ADC8")), QColor(QStringLiteral("#C5CBD6"))
        }
    },
    {
        NeniaColorScheme::CatppuccinMocha,
        "catppuccin-mocha",
        "Catppuccin Mocha",
        "Catppuccin",
        "Mocha",
        {
            QColor(QStringLiteral("#1E1E2E")), QColor(QStringLiteral("#313244")),
            QColor(QStringLiteral("#45475A")), QColor(QStringLiteral("#181926")),
            QColor(QStringLiteral("#45475A")), QColor(QStringLiteral("#89B4FA")),
            QColor(QStringLiteral("#89DCEB")), QColor(QStringLiteral("#CBA6F7")),
            QColor(QStringLiteral("#CBA6F7")), QColor(QStringLiteral("#A6E3A1")),
            QColor(QStringLiteral("#CDD6F4")), QColor(QStringLiteral("#BAC2DE")),
            QColor(QStringLiteral("#7F849C")), QColor(QStringLiteral("#313244")),
            QColor(QStringLiteral("#7F849C")), QColor(QStringLiteral("#1E1E2E")),
            QColor(QStringLiteral("#45475A")), QColor(QStringLiteral("#585B70")),
            QColor(QStringLiteral("#CDD6F4")), QColor(QStringLiteral("#BAC2DE")),
            QColor(QStringLiteral("#CDD6F4")), QColor(QStringLiteral("#A6E3A1")),
            QColor(QStringLiteral("#BAC2DE")), QColor(QStringLiteral("#CBA6F7")),
            QColor(QStringLiteral("#D8DEE9")), QColor(QStringLiteral("#313244"))
        }
    },
    {
        NeniaColorScheme::CatppuccinLatte,
        "catppuccin-latte",
        "Catppuccin Latte",
        "Catppuccin",
        "Latte",
        {
            QColor(QStringLiteral("#EFF1F5")), QColor(QStringLiteral("#E6E9EF")),
            QColor(QStringLiteral("#DCE0E8")), QColor(QStringLiteral("#E6E9EF")),
            QColor(QStringLiteral("#BCC0CC")), QColor(QStringLiteral("#1E66F5")),
            QColor(QStringLiteral("#209FB5")), QColor(QStringLiteral("#8839EF")),
            QColor(QStringLiteral("#8839EF")), QColor(QStringLiteral("#40A02B")),
            QColor(QStringLiteral("#4C4F69")), QColor(QStringLiteral("#5C5F77")),
            QColor(QStringLiteral("#5C5F77")), QColor(QStringLiteral("#E6E9EF")),
            QColor(QStringLiteral("#EFF1F5")), QColor(QStringLiteral("#4C4F69")),
            QColor(QStringLiteral("#BCC0CC")), QColor(QStringLiteral("#8839EF")),
            QColor(QStringLiteral("#EFF1F5")), QColor(QStringLiteral("#5C5F77")),
            QColor(QStringLiteral("#4C4F69")), QColor(QStringLiteral("#286D40")),
            QColor(QStringLiteral("#5C5F77")), QColor(QStringLiteral("#246B3B")),
            QColor(QStringLiteral("#EFF1F5")), QColor(QStringLiteral("#BCC0CC"))
        }
    },
    {
        NeniaColorScheme::GruvboxDark,
        "gruvbox-dark",
        "Gruvbox Dark",
        "Gruvbox",
        "Dark",
        {
            QColor(QStringLiteral("#282828")), QColor(QStringLiteral("#3C3836")),
            QColor(QStringLiteral("#504945")), QColor(QStringLiteral("#32302F")),
            QColor(QStringLiteral("#504945")), QColor(QStringLiteral("#8EC07C")),
            QColor(QStringLiteral("#83A598")), QColor(QStringLiteral("#D3869B")),
            QColor(QStringLiteral("#D3869B")), QColor(QStringLiteral("#B8BB26")),
            QColor(QStringLiteral("#EBDBB2")), QColor(QStringLiteral("#BDAE93")),
            QColor(QStringLiteral("#928374")), QColor(QStringLiteral("#3C3836")),
            QColor(QStringLiteral("#928374")), QColor(QStringLiteral("#282828")),
            QColor(QStringLiteral("#504945")), QColor(QStringLiteral("#504945")),
            QColor(QStringLiteral("#EBDBB2")), QColor(QStringLiteral("#BDAE93")),
            QColor(QStringLiteral("#EBDBB2")), QColor(QStringLiteral("#B8BB26")),
            QColor(QStringLiteral("#BDAE93")), QColor(QStringLiteral("#C5C99A")),
            QColor(QStringLiteral("#C5C99A")), QColor(QStringLiteral("#3C3836"))
        }
    },
    {
        NeniaColorScheme::GruvboxLight,
        "gruvbox-light",
        "Gruvbox Light",
        "Gruvbox",
        "Light",
        {
            QColor(QStringLiteral("#FBF1C7")), QColor(QStringLiteral("#EBDBB2")),
            QColor(QStringLiteral("#D5C4A1")), QColor(QStringLiteral("#F2E5BC")),
            QColor(QStringLiteral("#D5C4A1")), QColor(QStringLiteral("#458588")),
            QColor(QStringLiteral("#076678")), QColor(QStringLiteral("#8F3F71")),
            QColor(QStringLiteral("#8F3F71")), QColor(QStringLiteral("#98971A")),
            QColor(QStringLiteral("#3C3836")), QColor(QStringLiteral("#665C54")),
            QColor(QStringLiteral("#665C54")), QColor(QStringLiteral("#EBDBB2")),
            QColor(QStringLiteral("#FBF1C7")), QColor(QStringLiteral("#3C3836")),
            QColor(QStringLiteral("#D5C4A1")), QColor(QStringLiteral("#076678")),
            QColor(QStringLiteral("#FBF1C7")), QColor(QStringLiteral("#665C54")),
            QColor(QStringLiteral("#3C3836")), QColor(QStringLiteral("#4F4C00")),
            QColor(QStringLiteral("#665C54")), QColor(QStringLiteral("#535D37")),
            QColor(QStringLiteral("#DEE0B3")), QColor(QStringLiteral("#EBDBB2"))
        }
    }
}};

std::atomic<NeniaColorScheme> g_activeColorScheme{NeniaColorScheme::Classic};

std::optional<std::size_t> indexFor(NeniaColorScheme scheme) noexcept
{
    for (std::size_t index = 0; index < kColorSchemes.size(); ++index) {
        if (kColorSchemes[index].scheme == scheme) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace

std::span<const NeniaColorSchemeDefinition> neniaColorSchemeDefinitions() noexcept
{
    return kColorSchemes;
}

const NeniaColorSchemeDefinition &neniaColorSchemeDefinition(
    NeniaColorScheme scheme) noexcept
{
    const auto index = indexFor(scheme);
    return kColorSchemes[index.value_or(0)];
}

QString neniaColorSchemeId(NeniaColorScheme scheme)
{
    return QString::fromLatin1(neniaColorSchemeDefinition(scheme).id);
}

NeniaColorScheme neniaColorSchemeFromId(const QString &id) noexcept
{
    for (const NeniaColorSchemeDefinition &definition : kColorSchemes) {
        if (id == QLatin1String(definition.id)) {
            return definition.scheme;
        }
    }
    return NeniaColorScheme::Classic;
}

NeniaColorScheme activeNeniaColorScheme() noexcept
{
    return g_activeColorScheme.load(std::memory_order_relaxed);
}

bool setActiveNeniaColorScheme(NeniaColorScheme scheme) noexcept
{
    if (!indexFor(scheme)) {
        return false;
    }
    g_activeColorScheme.store(scheme, std::memory_order_relaxed);
    return true;
}

} // namespace nenia::detail
