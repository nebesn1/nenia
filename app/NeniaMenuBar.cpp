#include "NeniaMenuBar.h"

#include <QAction>
#include <QActionGroup>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>

#include <array>
#include <cstring>

namespace nenia::app {
namespace {

struct UserMidiPreset final {
    NeniaMidiPreset preset;
    const char *token;
    const char *label;
};

constexpr std::array<UserMidiPreset, 7> kUserMidiPresets{{
    {NeniaMidiPreset::A, "A", "A — Legacy"},
    {NeniaMidiPreset::B, "B", "B — Synth Boost"},
    {NeniaMidiPreset::C, "C", "C — Light"},
    {NeniaMidiPreset::D, "D", "D — Gentle"},
    {NeniaMidiPreset::E, "E", "E — Moderate (Default)"},
    {NeniaMidiPreset::F, "F", "F — Strong"},
    {NeniaMidiPreset::H, "H", "H — Enhanced"},
}};

} // namespace

NeniaMenuActions installNeniaMenuBar(QMainWindow *window,
                                     NeniaPlaylistController *playlist,
                                     const NeniaMenuHandlers &handlers,
                                     detail::KeyboardRangeMode keyboardRange,
                                     detail::OctaveNamingConvention octaveNamingConvention,
                                     detail::NeniaColorScheme colorScheme)
{
    NeniaMenuActions actions;
    if (window == nullptr || playlist == nullptr) {
        return actions;
    }

    QMenuBar *bar = window->menuBar();
    bar->clear();
    QMenu *openMenu = bar->addMenu(QObject::tr("Open"));
    QMenu *playbackMenu = bar->addMenu(QObject::tr("Playback"));
    actions.settingsMenu = bar->addMenu(QObject::tr("Settings"));
    actions.keyboardRangeMenu = actions.settingsMenu->addMenu(
        QObject::tr("Keyboard Range"));
    auto *keyboardRangeGroup = new QActionGroup(actions.keyboardRangeMenu);
    keyboardRangeGroup->setExclusive(true);
    for (std::size_t index = 0; index < detail::kKeyboardRangeDescriptors.size(); ++index) {
        const detail::KeyboardRangeDescriptor &descriptor =
            detail::kKeyboardRangeDescriptors[index];
        QAction *action = actions.keyboardRangeMenu->addAction(
            QString::fromUtf8(descriptor.displayNameUtf8));
        action->setCheckable(true);
        action->setData(QString::fromLatin1(descriptor.persistenceToken));
        action->setObjectName(QString::fromLatin1(descriptor.persistenceToken));
        keyboardRangeGroup->addAction(action);
        actions.keyboardRangeActions[index] = action;
        QObject::connect(action, &QAction::triggered, window,
                         [handlers, mode = descriptor.mode] {
            if (handlers.keyboardRangeChanged) {
                handlers.keyboardRangeChanged(mode);
            }
        });
    }
    synchronizeKeyboardRangeMenu(actions, keyboardRange);

    actions.octaveNamingConventionMenu = actions.settingsMenu->addMenu(
        QObject::tr("Octave Naming Convention"));
    auto *octaveNamingGroup = new QActionGroup(actions.octaveNamingConventionMenu);
    octaveNamingGroup->setExclusive(true);
    for (std::size_t index = 0; index < detail::kOctaveNamingDescriptors.size(); ++index) {
        const detail::OctaveNamingDescriptor &descriptor =
            detail::kOctaveNamingDescriptors[index];
        QAction *action = actions.octaveNamingConventionMenu->addAction(
            QString::fromUtf8(descriptor.displayNameUtf8));
        action->setCheckable(true);
        action->setData(QString::fromLatin1(descriptor.persistenceToken));
        action->setObjectName(QString::fromLatin1(descriptor.persistenceToken));
        octaveNamingGroup->addAction(action);
        actions.octaveNamingConventionActions[index] = action;
        QObject::connect(action, &QAction::triggered, window,
                         [handlers, convention = descriptor.convention] {
            if (handlers.octaveNamingConventionChanged) {
                handlers.octaveNamingConventionChanged(convention);
            }
        });
    }
    synchronizeOctaveNamingConventionMenu(actions, octaveNamingConvention);

    if (handlers.midiPresetMenuVisible) {
        actions.midiPresetMenu = actions.settingsMenu->addMenu(
            QObject::tr("MIDI Preset"));
        if (!handlers.midiPresetSelectionEnabled) {
            actions.midiPresetStatus = actions.midiPresetMenu->addAction(
                handlers.midiPresetLockMessage.isEmpty()
                    ? QObject::tr("Controlled by startup configuration")
                    : handlers.midiPresetLockMessage);
            actions.midiPresetStatus->setEnabled(false);
            actions.midiPresetMenu->addSeparator();
        }
        auto *presetGroup = new QActionGroup(actions.midiPresetMenu);
        presetGroup->setExclusive(true);
        for (std::size_t index = 0; index < kUserMidiPresets.size(); ++index) {
            const UserMidiPreset &preset = kUserMidiPresets[index];
            QAction *action = actions.midiPresetMenu->addAction(
                QObject::tr(preset.label));
            action->setCheckable(true);
            action->setEnabled(handlers.midiPresetSelectionEnabled);
            action->setObjectName(QStringLiteral("settingsMidiPreset%1")
                                      .arg(QString::fromLatin1(preset.token)));
            action->setData(QString::fromLatin1(preset.token));
            presetGroup->addAction(action);
            actions.midiPresetActions[index] = action;
            if (handlers.midiPresetSelectionEnabled && handlers.midiPresetRequested) {
                QObject::connect(action, &QAction::triggered, window,
                                 [handlers, selected = preset.preset] {
                    handlers.midiPresetRequested(selected);
                });
            }
        }
        synchronizeMidiPresetMenu(actions, handlers.midiPresetInitialValue,
                                  handlers.midiPresetHasEffectiveValue);
    }

    actions.colorSchemeMenu = actions.settingsMenu->addMenu(
        QObject::tr("Color Scheme"));
    auto *colorSchemeGroup = new QActionGroup(actions.colorSchemeMenu);
    colorSchemeGroup->setExclusive(true);
    QMenu *familyMenu = nullptr;
    const char *familyName = nullptr;
    const auto schemes = detail::neniaColorSchemeDefinitions();
    for (std::size_t index = 0; index < schemes.size(); ++index) {
        const detail::NeniaColorSchemeDefinition &definition = schemes[index];
        if (definition.familyName[0] == '\0') {
            QAction *action = actions.colorSchemeMenu->addAction(
                QObject::tr(definition.displayName));
            actions.colorSchemeActions[index] = action;
            if (index == 0 && schemes.size() > 1) {
                actions.colorSchemeMenu->addSeparator();
            }
        } else {
            if (familyName == nullptr || std::strcmp(familyName, definition.familyName) != 0) {
                familyName = definition.familyName;
                familyMenu = actions.colorSchemeMenu->addMenu(QObject::tr(familyName));
            }
            QAction *action = familyMenu->addAction(QObject::tr(definition.variantName));
            actions.colorSchemeActions[index] = action;
        }

        QAction *action = actions.colorSchemeActions[index];
        action->setCheckable(true);
        action->setData(QString::fromLatin1(definition.id));
        action->setObjectName(QStringLiteral("settingsColorScheme_%1")
                                  .arg(QString::fromLatin1(definition.id)));
        colorSchemeGroup->addAction(action);
        QObject::connect(action, &QAction::triggered, window,
                         [handlers, scheme = definition.scheme] {
            if (handlers.colorSchemeChanged) {
                handlers.colorSchemeChanged(scheme);
            }
        });
    }
    synchronizeNeniaColorSchemeMenu(actions, colorScheme);

    // Global accelerators are routed by NeniaGlobalInput so they remain safe
    // around modal dialogs and text editors. The tab text only presents them.
    actions.openFolder = openMenu->addAction(QObject::tr("Open Folder…\tCtrl+O"));
    actions.openPlaylist = openMenu->addAction(QObject::tr("Open Playlist…"));
    actions.openFile = openMenu->addAction(QObject::tr("Open File…\tO"));
    QObject::connect(actions.openFolder, &QAction::triggered, window,
                     handlers.openFolder);
    QObject::connect(actions.openPlaylist, &QAction::triggered, window,
                     handlers.openPlaylist);
    QObject::connect(actions.openFile, &QAction::triggered, window,
                     handlers.openFile);

    actions.previous = playbackMenu->addAction(QObject::tr("Previous\tP"));
    actions.playPause = playbackMenu->addAction(QObject::tr("Play / Pause\tSpace"));
    actions.next = playbackMenu->addAction(QObject::tr("Next\tN"));
    QObject::connect(actions.previous, &QAction::triggered, window,
                     [handlers] {
                         if (handlers.transport) {
                             handlers.transport(NeniaTransportControl::Previous);
                         }
                     });
    QObject::connect(actions.playPause, &QAction::triggered, window,
                     [handlers] {
                         if (handlers.transport) {
                             handlers.transport(NeniaTransportControl::PlayPause);
                         }
                     });
    QObject::connect(actions.next, &QAction::triggered, window,
                     [handlers] {
                         if (handlers.transport) {
                             handlers.transport(NeniaTransportControl::Next);
                         }
                     });

    playbackMenu->addSeparator();
    QMenu *loopMenu = playbackMenu->addMenu(QObject::tr("Loop"));
    auto *loopGroup = new QActionGroup(loopMenu);
    loopGroup->setExclusive(true);
    actions.loopOff = loopMenu->addAction(QObject::tr("Off"));
    actions.loopRepeatOne = loopMenu->addAction(QObject::tr("Repeat One"));
    actions.loopRepeatAll = loopMenu->addAction(QObject::tr("Repeat All"));
    for (QAction *action : {actions.loopOff, actions.loopRepeatOne, actions.loopRepeatAll}) {
        action->setCheckable(true);
        loopGroup->addAction(action);
    }
    actions.shuffle = playbackMenu->addAction(QObject::tr("Shuffle"));
    actions.shuffle->setCheckable(true);

    QObject::connect(actions.loopOff, &QAction::triggered, playlist,
                     [playlist] { playlist->setLoopMode(NeniaLoopMode::Off); });
    QObject::connect(actions.loopRepeatOne, &QAction::triggered, playlist,
                     [playlist] { playlist->setLoopMode(NeniaLoopMode::RepeatOne); });
    QObject::connect(actions.loopRepeatAll, &QAction::triggered, playlist,
                     [playlist] { playlist->setLoopMode(NeniaLoopMode::RepeatAll); });
    QObject::connect(actions.shuffle, &QAction::triggered, playlist,
                     [playlist](bool enabled) { playlist->setShuffleEnabled(enabled); });

    const auto syncOptions = [actions](NeniaLoopMode loopMode, bool shuffleEnabled) {
        const QSignalBlocker offBlocker(actions.loopOff);
        const QSignalBlocker oneBlocker(actions.loopRepeatOne);
        const QSignalBlocker allBlocker(actions.loopRepeatAll);
        const QSignalBlocker shuffleBlocker(actions.shuffle);
        actions.loopOff->setChecked(loopMode == NeniaLoopMode::Off);
        actions.loopRepeatOne->setChecked(loopMode == NeniaLoopMode::RepeatOne);
        actions.loopRepeatAll->setChecked(loopMode == NeniaLoopMode::RepeatAll);
        actions.shuffle->setChecked(shuffleEnabled);
    };
    QObject::connect(playlist, &NeniaPlaylistController::playbackOptionsChanged,
                     window, syncOptions);
    syncOptions(playlist->loopMode(), playlist->shuffleEnabled());
    return actions;
}

void synchronizeKeyboardRangeMenu(const NeniaMenuActions &actions,
                                  detail::KeyboardRangeMode keyboardRange)
{
    const QString selectedToken = QString::fromLatin1(
        detail::keyboardRangeDescriptor(keyboardRange).persistenceToken);
    for (QAction *action : actions.keyboardRangeActions) {
        if (action != nullptr && action->data().toString() == selectedToken) {
            action->setChecked(true);
            return;
        }
    }
}

void synchronizeOctaveNamingConventionMenu(
    const NeniaMenuActions &actions,
    detail::OctaveNamingConvention octaveNamingConvention)
{
    const QString selectedToken = QString::fromLatin1(
        detail::octaveNamingDescriptor(octaveNamingConvention).persistenceToken);
    for (QAction *action : actions.octaveNamingConventionActions) {
        if (action != nullptr) {
            action->setChecked(action->data().toString() == selectedToken);
        }
    }
}

void synchronizeNeniaColorSchemeMenu(const NeniaMenuActions &actions,
                                     detail::NeniaColorScheme colorScheme)
{
    const QString selectedId = detail::neniaColorSchemeId(colorScheme);
    for (QAction *action : actions.colorSchemeActions) {
        if (action != nullptr) {
            action->setChecked(action->data().toString() == selectedId);
        }
    }
}

void synchronizeMidiPresetMenu(const NeniaMenuActions &actions,
                               NeniaMidiPreset activePreset,
                               bool hasEffectiveValue)
{
    for (std::size_t index = 0; index < kUserMidiPresets.size(); ++index) {
        QAction *action = actions.midiPresetActions[index];
        if (action != nullptr) {
            action->setChecked(hasEffectiveValue
                               && kUserMidiPresets[index].preset == activePreset);
        }
    }
}

} // namespace nenia::app
