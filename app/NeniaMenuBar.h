#pragma once

#include "NeniaKeyboardRange.h"
#include "NeniaOctaveNaming.h"
#include "NeniaColorScheme.h"

#include <nenia/NeniaPlaylistController.h>
#include <nenia/NeniaUiState.h>

#include <QString>

#include <array>
#include <functional>

class QAction;
class QMenu;
class QMainWindow;

namespace nenia::app {

enum class NeniaMidiPreset { A, B, C, D, E, F, H };

struct NeniaMenuHandlers
{
    std::function<void()> openFolder;
    std::function<void()> openPlaylist;
    std::function<void()> openFile;
    std::function<void(NeniaTransportControl)> transport;
    std::function<void(detail::KeyboardRangeMode)> keyboardRangeChanged;
    std::function<void(detail::OctaveNamingConvention)> octaveNamingConventionChanged;
    std::function<void(detail::NeniaColorScheme)> colorSchemeChanged;
    bool midiPresetMenuVisible = false;
    bool midiPresetSelectionEnabled = false;
    bool midiPresetHasEffectiveValue = true;
    NeniaMidiPreset midiPresetInitialValue = NeniaMidiPreset::E;
    QString midiPresetLockMessage;
    std::function<void(NeniaMidiPreset)> midiPresetRequested;
};

struct NeniaMenuActions
{
    QMenu *settingsMenu = nullptr;
    QMenu *octaveNamingConventionMenu = nullptr;
    QMenu *colorSchemeMenu = nullptr;
    QAction *openFolder = nullptr;
    QAction *openPlaylist = nullptr;
    QAction *openFile = nullptr;
    QAction *previous = nullptr;
    QAction *playPause = nullptr;
    QAction *next = nullptr;
    QAction *loopOff = nullptr;
    QAction *loopRepeatOne = nullptr;
    QAction *loopRepeatAll = nullptr;
    QAction *shuffle = nullptr;
    QMenu *midiPresetMenu = nullptr;
    QAction *midiPresetStatus = nullptr;
    std::array<QAction *, 7> midiPresetActions{};
    QMenu *keyboardRangeMenu = nullptr;
    std::array<QAction *, 4> keyboardRangeActions{};
    std::array<QAction *, 3> octaveNamingConventionActions{};
    std::array<QAction *, 7> colorSchemeActions{};
};

NeniaMenuActions installNeniaMenuBar(QMainWindow *window,
                                     NeniaPlaylistController *playlist,
                                     const NeniaMenuHandlers &handlers,
                                     detail::KeyboardRangeMode keyboardRange =
                                         detail::kDefaultKeyboardRangeMode,
                                     detail::OctaveNamingConvention octaveNamingConvention =
                                         detail::kDefaultOctaveNamingConvention,
                                     detail::NeniaColorScheme colorScheme =
                                         detail::NeniaColorScheme::Classic);
void synchronizeKeyboardRangeMenu(const NeniaMenuActions &actions,
                                  detail::KeyboardRangeMode keyboardRange);
void synchronizeOctaveNamingConventionMenu(
    const NeniaMenuActions &actions,
    detail::OctaveNamingConvention octaveNamingConvention);
void synchronizeNeniaColorSchemeMenu(const NeniaMenuActions &actions,
                                     detail::NeniaColorScheme colorScheme);
void synchronizeMidiPresetMenu(const NeniaMenuActions &actions,
                               NeniaMidiPreset activePreset,
                               bool hasEffectiveValue = true);

} // namespace nenia::app
