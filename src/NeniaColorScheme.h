#pragma once

#include <QColor>
#include <QString>

#include <array>
#include <optional>
#include <span>

namespace nenia::detail {

enum class NeniaColorScheme {
    Classic,
    NordDark,
    NordLight,
    CatppuccinMocha,
    CatppuccinLatte,
    GruvboxDark,
    GruvboxLight
};

struct NeniaColorRoles
{
    QColor windowBackground;
    QColor panelBackground;
    QColor panelRaisedBackground;
    QColor panelAlternateBackground;
    QColor separatorColor;
    QColor axisColor;
    QColor primaryAccent;
    QColor secondaryAccent;
    QColor selectionAccent;
    QColor channelMeterColor;
    QColor primaryText;
    QColor secondaryText;
    QColor dimText;
    QColor keyboardBackground;
    QColor keyboardNaturalKey;
    QColor keyboardBlackKey;
    QColor borderColor;
    QColor selectionBackground;
    QColor selectionText;
    QColor mediaInfoLabelText;
    QColor mediaInfoValueText;
    QColor playbackStatusText;
    QColor playlistItemText;
    QColor playlistPlayingText;
    QColor playlistPlayingSelectedText;
    QColor keyboardWhiteKey;
};

struct NeniaColorSchemeDefinition
{
    NeniaColorScheme scheme;
    const char *id;
    const char *displayName;
    const char *familyName;
    const char *variantName;
    NeniaColorRoles colors;
};

std::span<const NeniaColorSchemeDefinition> neniaColorSchemeDefinitions() noexcept;
const NeniaColorSchemeDefinition &neniaColorSchemeDefinition(
    NeniaColorScheme scheme) noexcept;
QString neniaColorSchemeId(NeniaColorScheme scheme);
NeniaColorScheme neniaColorSchemeFromId(const QString &id) noexcept;
NeniaColorScheme activeNeniaColorScheme() noexcept;
bool setActiveNeniaColorScheme(NeniaColorScheme scheme) noexcept;

} // namespace nenia::detail
