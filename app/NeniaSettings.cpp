#include "NeniaSettings.h"

#include <QByteArray>
#include <QMetaType>
#include <QVariant>

#include <string_view>

namespace nenia::app {
namespace {

constexpr auto playbackOrderSettingsKey = "playback/orderMode";
constexpr auto playbackLoopSettingsKey = "playback/loopMode";
constexpr auto playbackShuffleSettingsKey = "playback/shuffleEnabled";
constexpr auto lastFolderSettingsKey = "playlist/lastFolder";
constexpr auto soundFontSettingsKey = "midi/soundFontPath";
constexpr auto midiLoudnessPresetSettingsKey = "midi/loudnessPreset";
constexpr auto keyboardRangeSettingsKey = "visualization/keyboardRange";
constexpr auto octaveNamingSettingsKey = "visualization/octaveNamingConvention";
constexpr auto colorSchemeSettingsKey = "ui/colorScheme";

bool isMidiLoudnessPresetToken(const QString &token) noexcept
{
    return token == QStringLiteral("A") || token == QStringLiteral("B")
        || token == QStringLiteral("D") || token == QStringLiteral("E")
        || token == QStringLiteral("C") || token == QStringLiteral("F")
        || token == QStringLiteral("H");
}

bool parsePlaybackOrderToken(const QString &token, NeniaPlaybackOrder *order)
{
    const QString normalizedToken = token.trimmed().toLower();
    if (normalizedToken == QStringLiteral("stop-after-current")) {
        *order = NeniaPlaybackOrder::StopAfterCurrent;
    } else if (normalizedToken == QStringLiteral("repeat-one")) {
        *order = NeniaPlaybackOrder::RepeatOne;
    } else if (normalizedToken == QStringLiteral("repeat-all")) {
        *order = NeniaPlaybackOrder::RepeatAll;
    } else if (normalizedToken == QStringLiteral("shuffle")) {
        *order = NeniaPlaybackOrder::Shuffle;
    } else {
        return false;
    }
    return true;
}

bool parseLoopModeToken(const QString &token, NeniaLoopMode *mode)
{
    const QString normalizedToken = token.trimmed().toLower();
    if (normalizedToken == QStringLiteral("off")) {
        *mode = NeniaLoopMode::Off;
    } else if (normalizedToken == QStringLiteral("repeat-one")) {
        *mode = NeniaLoopMode::RepeatOne;
    } else if (normalizedToken == QStringLiteral("repeat-all")) {
        *mode = NeniaLoopMode::RepeatAll;
    } else {
        return false;
    }
    return true;
}

bool parseShuffleValue(const QVariant &value, bool *enabled)
{
    if (value.metaType().id() == QMetaType::Bool) {
        *enabled = value.toBool();
        return true;
    }
    const QString normalizedValue = value.toString().trimmed().toLower();
    if (normalizedValue == QStringLiteral("true") || normalizedValue == QStringLiteral("1")) {
        *enabled = true;
    } else if (normalizedValue == QStringLiteral("false")
               || normalizedValue == QStringLiteral("0")) {
        *enabled = false;
    } else {
        return false;
    }
    return true;
}

NeniaPlaybackOptions playbackOptionsForLegacyOrder(NeniaPlaybackOrder order)
{
    switch (order) {
    case NeniaPlaybackOrder::StopAfterCurrent:
        return {};
    case NeniaPlaybackOrder::RepeatOne:
        return {NeniaLoopMode::RepeatOne, false};
    case NeniaPlaybackOrder::RepeatAll:
        return {NeniaLoopMode::RepeatAll, false};
    case NeniaPlaybackOrder::Shuffle:
        // Legacy Shuffle refilled the bag at end-of-media, so RepeatAll is the
        // compatible loop policy for the new independent state model.
        return {NeniaLoopMode::RepeatAll, true};
    }
    return {};
}

} // namespace

QString playbackOrderToken(NeniaPlaybackOrder order)
{
    switch (order) {
    case NeniaPlaybackOrder::StopAfterCurrent:
        return QStringLiteral("stop-after-current");
    case NeniaPlaybackOrder::RepeatOne:
        return QStringLiteral("repeat-one");
    case NeniaPlaybackOrder::RepeatAll:
        return QStringLiteral("repeat-all");
    case NeniaPlaybackOrder::Shuffle:
        return QStringLiteral("shuffle");
    }
    return QStringLiteral("stop-after-current");
}

NeniaPlaybackOrder playbackOrderFromToken(const QString &token)
{
    const QString normalizedToken = token.trimmed().toLower();
    if (normalizedToken == QStringLiteral("repeat-one")) {
        return NeniaPlaybackOrder::RepeatOne;
    }
    if (normalizedToken == QStringLiteral("repeat-all")) {
        return NeniaPlaybackOrder::RepeatAll;
    }
    if (normalizedToken == QStringLiteral("shuffle")) {
        return NeniaPlaybackOrder::Shuffle;
    }
    return NeniaPlaybackOrder::StopAfterCurrent;
}

NeniaPlaybackOrder loadPlaybackOrder(QSettings &settings)
{
    return playbackOrderFromToken(settings.value(QLatin1String(playbackOrderSettingsKey))
                                       .toString());
}

void savePlaybackOrder(QSettings &settings, NeniaPlaybackOrder order)
{
    settings.setValue(QLatin1String(playbackOrderSettingsKey), playbackOrderToken(order));
    settings.sync();
}

NeniaPlaybackOptions loadPlaybackOptions(QSettings &settings)
{
    NeniaPlaybackOptions options;
    const bool hasLoopMode = settings.contains(QLatin1String(playbackLoopSettingsKey));
    const bool hasShuffle = settings.contains(QLatin1String(playbackShuffleSettingsKey));
    const bool loopValid = hasLoopMode
        && parseLoopModeToken(settings.value(QLatin1String(playbackLoopSettingsKey)).toString(),
                              &options.loopMode);
    const bool shuffleValid = hasShuffle
        && parseShuffleValue(settings.value(QLatin1String(playbackShuffleSettingsKey)),
                             &options.shuffleEnabled);

    NeniaPlaybackOrder legacyOrder = NeniaPlaybackOrder::StopAfterCurrent;
    const bool hasValidLegacy = settings.contains(QLatin1String(playbackOrderSettingsKey))
        && parsePlaybackOrderToken(
            settings.value(QLatin1String(playbackOrderSettingsKey)).toString(), &legacyOrder);
    const NeniaPlaybackOptions legacyOptions = hasValidLegacy
        ? playbackOptionsForLegacyOrder(legacyOrder) : NeniaPlaybackOptions{};
    if (!loopValid) {
        options.loopMode = hasValidLegacy ? legacyOptions.loopMode : NeniaLoopMode::Off;
    }
    if (!shuffleValid) {
        options.shuffleEnabled = hasValidLegacy ? legacyOptions.shuffleEnabled : false;
    }

    const bool needsMigration = hasValidLegacy && (!loopValid || !shuffleValid);
    if (needsMigration) {
        savePlaybackOptions(settings, options);
    }

    return options;
}

void savePlaybackOptions(QSettings &settings, const NeniaPlaybackOptions &options)
{
    QString loopToken = QStringLiteral("off");
    switch (options.loopMode) {
    case NeniaLoopMode::Off:
        break;
    case NeniaLoopMode::RepeatOne:
        loopToken = QStringLiteral("repeat-one");
        break;
    case NeniaLoopMode::RepeatAll:
        loopToken = QStringLiteral("repeat-all");
        break;
    }
    settings.setValue(QLatin1String(playbackLoopSettingsKey), loopToken);
    settings.setValue(QLatin1String(playbackShuffleSettingsKey), options.shuffleEnabled);
    settings.sync();
}

detail::KeyboardRangeMode loadKeyboardRangeMode(QSettings &settings)
{
    const QByteArray token = settings.value(QLatin1String(keyboardRangeSettingsKey))
                                 .toString().toLatin1();
    const auto mode = detail::keyboardRangeModeFromToken(
        std::string_view(token.constData(), static_cast<std::size_t>(token.size())));
    return mode.value_or(detail::kDefaultKeyboardRangeMode);
}

void saveKeyboardRangeMode(QSettings &settings, detail::KeyboardRangeMode mode)
{
    settings.setValue(QLatin1String(keyboardRangeSettingsKey),
                      QLatin1String(detail::keyboardRangeDescriptor(mode).persistenceToken));
    settings.sync();
}

detail::OctaveNamingConvention loadOctaveNamingConvention(QSettings &settings)
{
    const QByteArray token = settings.value(QLatin1String(octaveNamingSettingsKey))
                                 .toString().toLatin1();
    const auto convention = detail::octaveNamingConventionFromToken(
        std::string_view(token.constData(), static_cast<std::size_t>(token.size())));
    return convention.value_or(detail::kDefaultOctaveNamingConvention);
}

void saveOctaveNamingConvention(QSettings &settings,
                                detail::OctaveNamingConvention convention)
{
    settings.setValue(
        QLatin1String(octaveNamingSettingsKey),
        QLatin1String(detail::octaveNamingDescriptor(convention).persistenceToken));
    settings.sync();
}

detail::NeniaColorScheme loadNeniaColorScheme(QSettings &settings, bool *valid)
{
    const QString key = QLatin1String(colorSchemeSettingsKey);
    if (valid != nullptr) {
        *valid = true;
    }
    if (!settings.contains(key)) {
        return detail::NeniaColorScheme::Classic;
    }

    const QString id = settings.value(key).toString();
    for (const detail::NeniaColorSchemeDefinition &definition :
         detail::neniaColorSchemeDefinitions()) {
        if (id == QLatin1String(definition.id)) {
            return definition.scheme;
        }
    }
    if (valid != nullptr) {
        *valid = false;
    }
    return detail::NeniaColorScheme::Classic;
}

bool saveNeniaColorScheme(QSettings &settings, detail::NeniaColorScheme scheme)
{
    const auto &definition = detail::neniaColorSchemeDefinition(scheme);
    if (definition.scheme != scheme) {
        return false;
    }
    settings.setValue(QLatin1String(colorSchemeSettingsKey),
                      QLatin1String(definition.id));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QPalette paletteForNeniaColorScheme(const QPalette &base,
                                    detail::NeniaColorScheme scheme)
{
    if (scheme == detail::NeniaColorScheme::Classic) {
        return base;
    }

    const detail::NeniaColorRoles &colors =
        detail::neniaColorSchemeDefinition(scheme).colors;
    QPalette palette = base;
    const auto set = [&palette](QPalette::ColorGroup group,
                                QPalette::ColorRole role,
                                const QColor &color) {
        palette.setColor(group, role, color);
    };
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        set(group, QPalette::Window, colors.windowBackground);
        set(group, QPalette::WindowText, colors.primaryText);
        set(group, QPalette::Base, colors.panelBackground);
        set(group, QPalette::AlternateBase, colors.panelAlternateBackground);
        set(group, QPalette::ToolTipBase, colors.panelRaisedBackground);
        set(group, QPalette::ToolTipText, colors.primaryText);
        set(group, QPalette::Text, colors.primaryText);
        set(group, QPalette::Button, colors.panelBackground);
        set(group, QPalette::ButtonText, colors.primaryText);
        set(group, QPalette::BrightText, colors.selectionText);
        set(group, QPalette::Highlight, colors.selectionBackground);
        set(group, QPalette::HighlightedText, colors.selectionText);
        set(group, QPalette::Link, colors.primaryAccent);
        set(group, QPalette::LinkVisited, colors.secondaryText);
        set(group, QPalette::PlaceholderText, colors.secondaryText);
    }
    set(QPalette::Disabled, QPalette::Window, colors.windowBackground);
    set(QPalette::Disabled, QPalette::WindowText, colors.dimText);
    set(QPalette::Disabled, QPalette::Base, colors.panelBackground);
    set(QPalette::Disabled, QPalette::AlternateBase, colors.panelAlternateBackground);
    set(QPalette::Disabled, QPalette::ToolTipBase, colors.panelRaisedBackground);
    set(QPalette::Disabled, QPalette::ToolTipText, colors.secondaryText);
    set(QPalette::Disabled, QPalette::Text, colors.dimText);
    set(QPalette::Disabled, QPalette::Button, colors.panelBackground);
    set(QPalette::Disabled, QPalette::ButtonText, colors.dimText);
    set(QPalette::Disabled, QPalette::BrightText, colors.selectionText);
    set(QPalette::Disabled, QPalette::Highlight, colors.borderColor);
    set(QPalette::Disabled, QPalette::HighlightedText, colors.primaryText);
    set(QPalette::Disabled, QPalette::Link, colors.secondaryText);
    set(QPalette::Disabled, QPalette::LinkVisited, colors.secondaryText);
    set(QPalette::Disabled, QPalette::PlaceholderText, colors.dimText);
    return palette;
}

QString loadLastFolder(QSettings &settings)
{
    return settings.value(QLatin1String(lastFolderSettingsKey)).toString();
}

void saveLastFolder(QSettings &settings, const QString &folderPath)
{
    settings.setValue(QLatin1String(lastFolderSettingsKey), folderPath);
    settings.sync();
}

void clearLastFolder(QSettings &settings)
{
    settings.remove(QLatin1String(lastFolderSettingsKey));
    settings.sync();
}

QString loadSoundFontPath(QSettings &settings)
{
    return settings.value(QLatin1String(soundFontSettingsKey)).toString().trimmed();
}

void saveSoundFontPath(QSettings &settings, const QString &path)
{
    const QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        settings.remove(QLatin1String(soundFontSettingsKey));
    } else {
        settings.setValue(QLatin1String(soundFontSettingsKey), normalized);
    }
    settings.sync();
}

QString loadMidiLoudnessPresetToken(QSettings &settings, bool *valid)
{
    if (valid != nullptr) {
        *valid = true;
    }
    const QString key = QLatin1String(midiLoudnessPresetSettingsKey);
    if (!settings.contains(key)) {
        return QStringLiteral("E");
    }

    const QString token = settings.value(key).toString().trimmed();
    if (isMidiLoudnessPresetToken(token)) {
        return token;
    }
    if (valid != nullptr) {
        *valid = false;
    }
    return QStringLiteral("E");
}

bool saveMidiLoudnessPresetToken(QSettings &settings, const QString &token)
{
    if (!isMidiLoudnessPresetToken(token)) {
        return false;
    }
    settings.setValue(QLatin1String(midiLoudnessPresetSettingsKey), token);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool isValidMidiLoudnessPresetToken(const QString &token) noexcept
{
    return isMidiLoudnessPresetToken(token);
}

NeniaSoundFontConfiguration resolveSoundFontConfiguration(
    const QString &commandLinePath,
    bool commandLineSpecified,
    const QString &environmentPath,
    bool environmentSpecified,
    const QString &savedPreference)
{
    if (commandLineSpecified) {
        return {commandLinePath.trimmed(), NeniaSoundFontConfigSource::CommandLine};
    }
    if (environmentSpecified) {
        return {environmentPath.trimmed(), NeniaSoundFontConfigSource::Environment};
    }
    if (!savedPreference.trimmed().isEmpty()) {
        return {savedPreference.trimmed(), NeniaSoundFontConfigSource::Preference};
    }
    return {};
}

QString soundFontConfigSourceName(NeniaSoundFontConfigSource source)
{
    switch (source) {
    case NeniaSoundFontConfigSource::CommandLine:
        return QStringLiteral("--sf2 command-line override");
    case NeniaSoundFontConfigSource::Environment:
        return QStringLiteral("NENIA_SOUNDFONT environment override");
    case NeniaSoundFontConfigSource::Preference:
        return QStringLiteral("saved preference");
    case NeniaSoundFontConfigSource::None:
        return QStringLiteral("not configured");
    }
    return QStringLiteral("not configured");
}

} // namespace nenia::app
