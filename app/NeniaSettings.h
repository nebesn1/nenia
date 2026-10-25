#pragma once

#include "NeniaKeyboardRange.h"
#include "NeniaOctaveNaming.h"
#include "NeniaColorScheme.h"

#include <nenia/NeniaPlaylistController.h>

#include <QSettings>
#include <QPalette>
#include <QString>

namespace nenia::app {

struct NeniaPlaybackOptions
{
    NeniaLoopMode loopMode = NeniaLoopMode::Off;
    bool shuffleEnabled = false;

    bool operator==(const NeniaPlaybackOptions &) const = default;
};

enum class NeniaSoundFontConfigSource {
    CommandLine,
    Environment,
    Preference,
    None
};

struct NeniaSoundFontConfiguration
{
    QString path;
    NeniaSoundFontConfigSource source = NeniaSoundFontConfigSource::None;

    bool operator==(const NeniaSoundFontConfiguration &) const = default;
};

QString playbackOrderToken(NeniaPlaybackOrder order);
NeniaPlaybackOrder playbackOrderFromToken(const QString &token);
NeniaPlaybackOrder loadPlaybackOrder(QSettings &settings);
void savePlaybackOrder(QSettings &settings, NeniaPlaybackOrder order);
NeniaPlaybackOptions loadPlaybackOptions(QSettings &settings);
void savePlaybackOptions(QSettings &settings, const NeniaPlaybackOptions &options);
detail::KeyboardRangeMode loadKeyboardRangeMode(QSettings &settings);
void saveKeyboardRangeMode(QSettings &settings, detail::KeyboardRangeMode mode);
detail::OctaveNamingConvention loadOctaveNamingConvention(QSettings &settings);
void saveOctaveNamingConvention(QSettings &settings,
                                detail::OctaveNamingConvention convention);
detail::NeniaColorScheme loadNeniaColorScheme(QSettings &settings,
                                               bool *valid = nullptr);
bool saveNeniaColorScheme(QSettings &settings,
                          detail::NeniaColorScheme scheme);
QPalette paletteForNeniaColorScheme(const QPalette &base,
                                    detail::NeniaColorScheme scheme);
QString loadLastFolder(QSettings &settings);
void saveLastFolder(QSettings &settings, const QString &folderPath);
void clearLastFolder(QSettings &settings);
QString loadSoundFontPath(QSettings &settings);
void saveSoundFontPath(QSettings &settings, const QString &path);
QString loadMidiLoudnessPresetToken(QSettings &settings, bool *valid = nullptr);
bool saveMidiLoudnessPresetToken(QSettings &settings, const QString &token);
bool isValidMidiLoudnessPresetToken(const QString &token) noexcept;
NeniaSoundFontConfiguration resolveSoundFontConfiguration(
    const QString &commandLinePath,
    bool commandLineSpecified,
    const QString &environmentPath,
    bool environmentSpecified,
    const QString &savedPreference);
QString soundFontConfigSourceName(NeniaSoundFontConfigSource source);

} // namespace nenia::app
