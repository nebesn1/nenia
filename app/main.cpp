#include "NeniaMidiSidecarResolver.h"
#include "NeniaGlobalInput.h"
#include "NeniaFileImport.h"
#include "NeniaFileDialog.h"
#include "NeniaMenuBar.h"
#include "NeniaPlaylistActivity.h"
#include "NeniaPlaybackOptionActions.h"
#include "NeniaSettings.h"
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
#include "NeniaApplicationPlaybackRouter.h"
#include "../src/midi_playback_backend/NeniaMidiPlaybackEngine.h"
#endif

#include "../src/NeniaRenderer.h"
#include "../src/NeniaDirectMidiSource.h"
#include "../src/NeniaMidiAudioTrace.h"
#include "../src/NeniaMidiVisualTimelineProvider.h"

#include <nenia/JsonTimelineProvider.h>
#include <nenia/NeniaMediaInfo.h>
#include <nenia/NeniaPlaybackController.h>
#include <nenia/NeniaPlaylistFile.h>
#include <nenia/NeniaPlaylistController.h>
#include <nenia/NeniaWidget.h>

#include <QApplication>
#include <QAction>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

std::shared_ptr<const nenia::TimelineEventProvider> loadJsonTimeline(
    const QString &path,
    QString *errorMessage)
{
    auto provider = std::make_shared<nenia::JsonTimelineProvider>();
    if (!provider->load(path, errorMessage)) {
        return {};
    }
    return provider;
}

std::shared_ptr<const nenia::TimelineEventProvider> loadMidiTimeline(
    const QString &path,
    QString *errorMessage)
{
    auto provider = std::make_shared<nenia::detail::NeniaMidiVisualTimelineProvider>();
    if (!provider->load(path, errorMessage)) {
        return {};
    }
    return provider;
}

QString normalizedFolderPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
nenia::detail::NeniaMidiLoudnessPreset midiPresetFromToken(const QString &token)
{
    using Preset = nenia::detail::NeniaMidiLoudnessPreset;
    if (token == QStringLiteral("A")) return Preset::A;
    if (token == QStringLiteral("B")) return Preset::B;
    if (token == QStringLiteral("C")) return Preset::C;
    if (token == QStringLiteral("D")) return Preset::D;
    if (token == QStringLiteral("E")) return Preset::E;
    if (token == QStringLiteral("F")) return Preset::F;
    return Preset::E;
}

QString midiStartupModeName(nenia::detail::NeniaMidiLoudnessStartupMode mode)
{
    using Mode = nenia::detail::NeniaMidiLoudnessStartupMode;
    switch (mode) {
    case Mode::ProductionH: return QStringLiteral("production preset");
    case Mode::Legacy: return QStringLiteral("legacy policy");
    case Mode::DeveloperGain: return QStringLiteral("Developer gain override");
    case Mode::DeveloperTrial: return QStringLiteral("Developer trial");
    case Mode::DeveloperConflict: return QStringLiteral("Developer conflict");
    }
    return QStringLiteral("unknown");
}

QString midiPresetToken(nenia::app::NeniaMidiPreset preset)
{
    switch (preset) {
    case nenia::app::NeniaMidiPreset::A: return QStringLiteral("A");
    case nenia::app::NeniaMidiPreset::B: return QStringLiteral("B");
    case nenia::app::NeniaMidiPreset::C: return QStringLiteral("C");
    case nenia::app::NeniaMidiPreset::D: return QStringLiteral("D");
    case nenia::app::NeniaMidiPreset::E: return QStringLiteral("E");
    case nenia::app::NeniaMidiPreset::F: return QStringLiteral("F");
    case nenia::app::NeniaMidiPreset::H: return QStringLiteral("H");
    }
    return QStringLiteral("E");
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    const std::array<std::pair<const char *, const char *>, 3> retiredMidiFlags{{
        {"NENIA_MIDI_LOUDNESS_TRIAL", "select MIDI loudness in Settings → MIDI Preset"},
        {"NENIA_MIDI_GAIN_AB", "select MIDI loudness in Settings → MIDI Preset"},
        {"NENIA_EXPERIMENTAL_MIDI_GAIN",
         "fixed-gain diagnostics are available only in private developer builds; use Settings → MIDI Preset for normal playback"},
    }};
    for (const auto &[name, migration] : retiredMidiFlags) {
        if (!qEnvironmentVariableIsSet(name)) {
            continue;
        }
        qCritical().noquote()
            << "Nenia startup configuration error: " << name
            << "is not supported by the GUI. Unset it and " << migration
            << ". Exiting with code 2.";
        return 2;
    }
    const nenia::detail::NeniaMidiLoudnessStartupConfiguration midiLoudnessStartup =
        nenia::detail::resolveNeniaMidiLoudnessStartupConfiguration(
            qgetenv("NENIA_MIDI_LOUDNESS_POLICY"),
            qgetenv("NENIA_MIDI_LOUDNESS_TRIAL"),
            qgetenv("NENIA_MIDI_GAIN_AB"),
            qgetenv("NENIA_EXPERIMENTAL_MIDI_GAIN"),
            qEnvironmentVariableIsSet("NENIA_EXPERIMENTAL_MIDI_GAIN"));
    if (!midiLoudnessStartup.valid) {
        qCritical().noquote() << "Nenia startup configuration error:"
                              << midiLoudnessStartup.errorMessage;
        return 2;
    }
    if (!midiLoudnessStartup.warningMessage.isEmpty()) {
        qWarning().noquote() << "[NENIA MIDI LOUDNESS POLICY]"
                             << midiLoudnessStartup.warningMessage;
    }
#endif
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Nenia"));
    QCoreApplication::setOrganizationName(QStringLiteral("Nenia"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NENIA_PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Nenia adaptive music player and playlist workspace"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption audioOption(
        QStringLiteral("audio"), QStringLiteral("Local audio file to prepare."),
        QStringLiteral("path"));
    const QCommandLineOption playlistFolderOption(
        QStringLiteral("playlist-folder"),
        QStringLiteral("Recursively import a folder as a playlist."),
        QStringLiteral("path"));
    const QCommandLineOption playlistFileOption(
        QStringLiteral("playlist-file"),
        QStringLiteral("Open an M3U or M3U8 playlist file."),
        QStringLiteral("path"));
    const QCommandLineOption timelineOption(
        QStringLiteral("timeline"), QStringLiteral("JSON timeline file to synchronize."),
        QStringLiteral("path"));
    const QCommandLineOption midiOption(
        QStringLiteral("midi"), QStringLiteral("Standard MIDI file for visual events."),
        QStringLiteral("path"));
    const QCommandLineOption directMidiOption(
        QStringLiteral("midi-file"), QStringLiteral("Directly play a local Standard MIDI file."),
        QStringLiteral("path"));
    const QCommandLineOption soundFontOption(
        QStringLiteral("sf2"), QStringLiteral("Local SoundFont 2 file for direct MIDI playback."),
        QStringLiteral("path"));
    const QCommandLineOption artworkOption(
        QStringLiteral("artwork"), QStringLiteral("Artwork image override."),
        QStringLiteral("path"));
    const QCommandLineOption titleOption(
        QStringLiteral("title"), QStringLiteral("Explicit media title override."),
        QStringLiteral("text"));
    const QCommandLineOption artistOption(
        QStringLiteral("artist"), QStringLiteral("Explicit media artist override."),
        QStringLiteral("text"));
    const QCommandLineOption albumOption(
        QStringLiteral("album"), QStringLiteral("Explicit media album override."),
        QStringLiteral("text"));
    parser.addOption(audioOption);
    parser.addOption(playlistFolderOption);
    parser.addOption(playlistFileOption);
    parser.addOption(timelineOption);
    parser.addOption(midiOption);
    parser.addOption(directMidiOption);
    parser.addOption(soundFontOption);
    parser.addOption(artworkOption);
    parser.addOption(titleOption);
    parser.addOption(artistOption);
    parser.addOption(albumOption);
    parser.process(application);

    const bool hasAudio = parser.isSet(audioOption);
    const bool hasFolder = parser.isSet(playlistFolderOption);
    const bool hasPlaylistFile = parser.isSet(playlistFileOption);
    const bool hasDirectMidi = parser.isSet(directMidiOption);
    const int sourceModeCount = static_cast<int>(hasAudio) + static_cast<int>(hasFolder)
        + static_cast<int>(hasPlaylistFile) + static_cast<int>(hasDirectMidi);
    if (sourceModeCount > 1) {
        qCritical().noquote()
            << "--audio, --playlist-folder, --playlist-file, and --midi-file are mutually exclusive";
        return 2;
    }
#ifndef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    if (hasDirectMidi || parser.isSet(soundFontOption)) {
        qCritical().noquote()
            << "--midi-file/--sf2 require a build with "
               "NENIA_BUILD_MIDI_PLAYBACK_BACKEND=ON";
        return 2;
    }
#endif
    if (hasDirectMidi && !nenia::detail::hasDirectMidiFileSuffix(
                             parser.value(directMidiOption))) {
        qCritical().noquote() << "--midi-file expects a local .mid or .midi file";
        return 2;
    }
    if (parser.isSet(timelineOption) && parser.isSet(midiOption)) {
        qCritical().noquote()
            << "--timeline and --midi are mutually exclusive; choose one visual provider";
        return 2;
    }
    if ((parser.isSet(timelineOption) || parser.isSet(midiOption))
        && (!hasAudio || hasDirectMidi)) {
        qCritical().noquote() << "--timeline/--midi requires --audio";
        return 2;
    }
    if ((parser.isSet(artworkOption) || parser.isSet(titleOption)
         || parser.isSet(artistOption) || parser.isSet(albumOption))
        && (hasFolder || hasDirectMidi)) {
        qCritical().noquote()
            << "metadata/artwork overrides are not supported with --playlist-folder or --midi-file";
        return 2;
    }

    QSettings settings;
    const QPalette systemPalette = application.palette();
    bool colorSchemePreferenceValid = true;
    const nenia::detail::NeniaColorScheme colorScheme =
        nenia::app::loadNeniaColorScheme(settings, &colorSchemePreferenceValid);
    if (!colorSchemePreferenceValid) {
        qWarning().noquote()
            << "[NENIA COLOR SCHEME] invalid saved scheme; using Nenia Classic";
    }
    (void)nenia::detail::setActiveNeniaColorScheme(colorScheme);

    QMainWindow window;
    window.setPalette(nenia::app::paletteForNeniaColorScheme(systemPalette, colorScheme));
    window.setWindowTitle(QStringLiteral("Nenia"));
    auto *widget = new nenia::NeniaWidget(&window);
    window.setCentralWidget(widget);
    window.resize(960, 600);

#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    bool midiPresetPreferenceValid = true;
    const QString savedMidiPresetToken =
        nenia::app::loadMidiLoudnessPresetToken(settings, &midiPresetPreferenceValid);
    if (!midiPresetPreferenceValid) {
        qWarning().noquote()
            << "[NENIA MIDI PRESET] invalid saved preset; using default E";
    }
    const auto savedMidiPreset = midiPresetFromToken(savedMidiPresetToken);
    const auto effectiveMidiStartup =
        nenia::detail::resolveNeniaMidiLoudnessStartupConfiguration(
            qgetenv("NENIA_MIDI_LOUDNESS_POLICY"),
            qgetenv("NENIA_MIDI_LOUDNESS_TRIAL"),
            qgetenv("NENIA_MIDI_GAIN_AB"),
            qgetenv("NENIA_EXPERIMENTAL_MIDI_GAIN"),
            qEnvironmentVariableIsSet("NENIA_EXPERIMENTAL_MIDI_GAIN"),
            savedMidiPreset);
    const auto &effectiveMidiPreset =
        nenia::detail::neniaMidiLoudnessPresetDefinition(
            effectiveMidiStartup.initialPreset);
    qInfo().noquote() << "[NENIA MIDI LOUDNESS POLICY] mode="
                      << midiStartupModeName(effectiveMidiStartup.mode)
                      << "effective_preset=" << effectiveMidiPreset.name
                      << "synth_gain=" << effectiveMidiStartup.synthGain
                      << "user_selectable="
                      << effectiveMidiStartup.userPresetSelectionEnabled
                      << "streaming_protection="
                      << effectiveMidiStartup.streamingProtectionEnabled;
#endif
    const nenia::detail::KeyboardRangeMode keyboardRange =
        nenia::app::loadKeyboardRangeMode(settings);
    const nenia::detail::OctaveNamingConvention octaveNamingConvention =
        nenia::app::loadOctaveNamingConvention(settings);
    const auto &keyboardRangeDescriptor =
        nenia::detail::keyboardRangeDescriptor(keyboardRange);
    widget->setKeyboardRange(keyboardRangeDescriptor.lowestMidiNote,
                             keyboardRangeDescriptor.highestMidiNote);
    widget->setOctaveNamingMiddleC(
        nenia::detail::octaveNamingDescriptor(octaveNamingConvention).middleCOctave);
    auto *controller = new nenia::NeniaPlaybackController(widget, &window);
    controller->setVolume(0.7f);
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    const bool soundFontEnvironmentSpecified = qEnvironmentVariableIsSet("NENIA_SOUNDFONT");
    const nenia::app::NeniaSoundFontConfiguration soundFontConfiguration =
        nenia::app::resolveSoundFontConfiguration(
            parser.value(soundFontOption), parser.isSet(soundFontOption),
            qEnvironmentVariable("NENIA_SOUNDFONT"), soundFontEnvironmentSpecified,
            nenia::app::loadSoundFontPath(settings));
    auto *midiRouter = new nenia::app::NeniaApplicationPlaybackRouter(
        soundFontConfiguration.path,
        nenia::detail::NeniaMidiPlaybackEngine::OutputMode::Device,
        controller,
        soundFontConfiguration.source,
        savedMidiPreset);
    midiRouter->attachTo(controller);
    qInfo().noquote() << "Nenia effective SoundFont configuration:"
                      << nenia::app::soundFontConfigSourceName(soundFontConfiguration.source)
                      << (soundFontConfiguration.path.isEmpty()
                              ? QStringLiteral("(none)")
                              : QFileInfo(soundFontConfiguration.path).absoluteFilePath());
#endif
    auto *playlist = new nenia::NeniaPlaylistController(controller, &window);

    const nenia::app::NeniaPlaybackOptions restoredPlaybackOptions =
        nenia::app::loadPlaybackOptions(settings);
    playlist->setPlaybackOptions(restoredPlaybackOptions.loopMode,
                                 restoredPlaybackOptions.shuffleEnabled);

    nenia::app::NeniaPlaylistActivity playlistActivity(&window);
    int selectedIndex = -1;
    QString playlistSourceLabel;
    QString currentPlaylistFile;
    bool playlistSidecarsEnabled = false;

    const auto publishPlaylist = [&] {
        nenia::NeniaPlaylistState state;
        state.sourceLabel = playlistSourceLabel;
        state.interactionHighlighted = playlistActivity.isHighlighted();
        state.hasPlaylist = !playlist->isEmpty();
        const int count = playlist->count();
        if (count == 0) {
            selectedIndex = -1;
        } else {
            selectedIndex = std::clamp(selectedIndex, 0, count - 1);
        }

        const nenia::detail::NeniaLayout layout =
            nenia::detail::NeniaRenderer::layoutForSize(widget->size());
        const int visibleRows =
            nenia::detail::NeniaRenderer::playlistVisibleRowCount(layout);
        if (selectedIndex >= 0 && selectedIndex < state.firstVisibleRow) {
            state.firstVisibleRow = selectedIndex;
        }
        if (selectedIndex >= 0 && selectedIndex >= state.firstVisibleRow + visibleRows) {
            state.firstVisibleRow = selectedIndex - visibleRows + 1;
        }
        state.firstVisibleRow = std::max(0, std::min(state.firstVisibleRow,
                                                      std::max(0, count - visibleRows)));

        const auto playbackState = controller->playbackState();
        const bool currentIsPlaying = playbackState == QMediaPlayer::PlayingState
            || playbackState == QMediaPlayer::PausedState;
        state.entries.reserve(count);
        for (int index = 0; index < count; ++index) {
            const nenia::NeniaPlaylistItem &item = playlist->items().at(
                static_cast<std::size_t>(index));
            nenia::NeniaPlaylistEntry entry;
            entry.displayName = item.displayName;
            entry.selected = index == selectedIndex;
            entry.current = currentIsPlaying && index == playlist->currentIndex();
            state.entries.push_back(std::move(entry));
        }
        widget->setPlaylistState(state);
    };
    playlistActivity.setChangeHandler(publishPlaylist);

    bool extendedMediaInfoPage = false;
    const auto syncPlaybackUi = [widget, &extendedMediaInfoPage](
                                    nenia::NeniaLoopMode loopMode, bool shuffleEnabled) {
        nenia::NeniaPlaybackUiState state;
        state.shuffle = shuffleEnabled;
        state.extendedMediaInfo = extendedMediaInfoPage;
        switch (loopMode) {
        case nenia::NeniaLoopMode::Off:
            state.loop = nenia::NeniaLoopIndicatorState::StopAfterCurrent;
            break;
        case nenia::NeniaLoopMode::RepeatOne:
            state.loop = nenia::NeniaLoopIndicatorState::RepeatOne;
            break;
        case nenia::NeniaLoopMode::RepeatAll:
            state.loop = nenia::NeniaLoopIndicatorState::RepeatAll;
            break;
        }
        widget->setPlaybackUiState(state);
    };
    syncPlaybackUi(restoredPlaybackOptions.loopMode,
                   restoredPlaybackOptions.shuffleEnabled);

    QObject::connect(playlist, &nenia::NeniaPlaylistController::playlistChanged,
                     &window, [&] {
                         publishPlaylist();
                     });
    QObject::connect(playlist, &nenia::NeniaPlaylistController::currentIndexChanged,
                     &window, [&](int index) {
                         if (selectedIndex < 0 || controller->playbackState()
                                 == QMediaPlayer::PlayingState) {
                             selectedIndex = index;
                         }
                         publishPlaylist();
                     });
    QObject::connect(controller, &nenia::NeniaPlaybackController::playbackStateChanged,
                     &window, [&](QMediaPlayer::PlaybackState) { publishPlaylist(); });
    QObject::connect(widget, &QObject::destroyed, &window, [] {});
    QObject::connect(playlist, &nenia::NeniaPlaylistController::playbackOptionsChanged,
                     &window,
                     [&settings, syncPlaybackUi](nenia::NeniaLoopMode loopMode,
                                                 bool shuffleEnabled) {
                         syncPlaybackUi(loopMode, shuffleEnabled);
                         nenia::app::savePlaybackOptions(
                             settings, {loopMode, shuffleEnabled});
                     });
    QObject::connect(playlist, &nenia::NeniaPlaylistController::errorOccurred,
                     &window, [](const QString &message) {
                         qWarning().noquote() << "Nenia playlist:" << message;
                     });
    QObject::connect(controller, &nenia::NeniaPlaybackController::errorOccurred,
                     &window,
                     [&window, controller](QMediaPlayer::Error error,
                                           const QString &errorString) {
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
                         if (nenia::detail::isDirectMidiSource(controller->source())) {
                             QMessageBox::warning(&window,
                                                  QObject::tr("MIDI Playback Error"),
                                                  errorString);
                             return;
                         }
#endif
                         qWarning().noquote()
                             << "Nenia audio error" << error << ":" << errorString;
                     });

    const auto resolveCurrentSidecar = [&] {
        if (!playlistSidecarsEnabled) {
            const nenia::NeniaPlaylistItem *item = playlist->currentItem();
            if (item != nullptr && nenia::detail::neniaMidiAudioTraceEnabled()) {
                const bool directMidi = nenia::detail::isDirectMidiSource(item->source);
                const QString sourceName = item->source.isLocalFile()
                    ? QFileInfo(item->source.toLocalFile()).fileName() : item->source.fileName();
                qInfo() << "[NENIA MIDI AUDIO]"
                        << "event=timeline-route"
                        << "pid=" << QCoreApplication::applicationPid()
                        << "source=" << sourceName
                        << "playback_backend=" << (directMidi ? "direct-midi" : "encoded-audio")
                        << "midi_sidecar=" << (directMidi ? "not-applicable" : "disabled");
            }
            return;
        }
        const nenia::NeniaPlaylistItem *item = playlist->currentItem();
        if (item == nullptr) {
            controller->clearTimelineProvider();
            return;
        }
        if (nenia::detail::isDirectMidiSource(item->source)) {
            // Direct MIDI already owns the visualization provider from the
            // same parsed MIDI sequence; never attach a sidecar to it.
            if (nenia::detail::neniaMidiAudioTraceEnabled()) {
                qInfo() << "[NENIA MIDI AUDIO]"
                        << "event=timeline-route"
                        << "pid=" << QCoreApplication::applicationPid()
                        << "source=" << QFileInfo(item->source.toLocalFile()).fileName()
                        << "playback_backend=direct-midi"
                        << "midi_sidecar=not-applicable";
            }
            return;
        }
        controller->clearTimelineProvider();
        const QString sourceName = item->source.isLocalFile()
            ? QFileInfo(item->source.toLocalFile()).fileName() : item->source.fileName();
        if (!item->source.isLocalFile()) {
            if (nenia::detail::neniaMidiAudioTraceEnabled()) {
                qInfo() << "[NENIA MIDI AUDIO]"
                        << "event=timeline-route"
                        << "pid=" << QCoreApplication::applicationPid()
                        << "source=" << sourceName
                        << "playback_backend=encoded-audio"
                        << "midi_sidecar=unsupported-source";
            }
            return;
        }
        const QString sidecar = nenia::app::findPlaylistMidiSidecar(item->source.toLocalFile());
        if (sidecar.isEmpty()) {
            if (nenia::detail::neniaMidiAudioTraceEnabled()) {
                qInfo() << "[NENIA MIDI AUDIO]"
                        << "event=timeline-route"
                        << "pid=" << QCoreApplication::applicationPid()
                        << "source=" << sourceName
                        << "playback_backend=encoded-audio"
                        << "midi_sidecar=none";
            }
            return;
        }

        QString error;
        const auto provider = loadMidiTimeline(sidecar, &error);
        if (!provider) {
            qWarning().noquote() << "Nenia MIDI sidecar disabled:" << error
                                 << "(" << QFileInfo(sidecar).fileName() << ")";
            if (nenia::detail::neniaMidiAudioTraceEnabled()) {
                qInfo() << "[NENIA MIDI AUDIO]"
                        << "event=timeline-route"
                        << "pid=" << QCoreApplication::applicationPid()
                        << "source=" << sourceName
                        << "playback_backend=encoded-audio"
                        << "midi_sidecar=load-failed"
                        << "sidecar=" << QFileInfo(sidecar).fileName();
            }
            return;
        }
        controller->setTimelineProvider(provider);
        if (nenia::detail::neniaMidiAudioTraceEnabled()) {
            qInfo() << "[NENIA MIDI AUDIO]"
                    << "event=timeline-route"
                    << "pid=" << QCoreApplication::applicationPid()
                    << "source=" << sourceName
                    << "playback_backend=encoded-audio"
                    << "midi_sidecar=attached-visualization-only"
                    << "sidecar=" << QFileInfo(sidecar).fileName();
        }
    };
    QObject::connect(playlist, &nenia::NeniaPlaylistController::currentTrackChanged,
                     &window, resolveCurrentSidecar, Qt::QueuedConnection);

    const auto prepareFolder = [&](const QString &folderPath, bool persistFolder) {
        QString error;
        if (!playlist->loadDirectoryPrepared(folderPath, &error)) {
            qWarning().noquote() << "Nenia playlist folder:" << error;
            return false;
        }
        playlistSourceLabel = QFileInfo(folderPath).fileName();
        if (playlistSourceLabel.isEmpty()) {
            playlistSourceLabel = normalizedFolderPath(folderPath);
        }
        currentPlaylistFile.clear();
        playlistSidecarsEnabled = true;
        selectedIndex = 0;
        if (!nenia::detail::isDirectMidiSource(controller->source())) {
            widget->clearMediaInfo();
        }
        if (persistFolder) {
            nenia::app::saveLastFolder(settings, normalizedFolderPath(folderPath));
        }
        playlistActivity.markInteraction();
        publishPlaylist();
        return true;
    };

    const auto preparePlaylistFile = [&](const QString &playlistPath) {
        nenia::NeniaPlaylistFileLoadResult result;
        QString error;
        if (!nenia::loadNeniaM3uPlaylist(playlistPath, &result, &error)) {
            qWarning().noquote() << "Nenia playlist file:" << error;
            for (const QString &warning : result.warnings) {
                qWarning().noquote() << "Nenia playlist file:" << warning;
            }
            return false;
        }
        for (const QString &warning : result.warnings) {
            qWarning().noquote() << "Nenia playlist file:" << warning;
        }
        if (!playlist->loadItems(result.items,
                                 nenia::NeniaPlaylistLoadBehavior::PrepareFirst,
                                 &error)) {
            qWarning().noquote() << "Nenia playlist file:" << error;
            return false;
        }
        playlistSourceLabel = QFileInfo(playlistPath).fileName();
        currentPlaylistFile = QFileInfo(playlistPath).absoluteFilePath();
        playlistSidecarsEnabled = true;
        selectedIndex = 0;
        if (!nenia::detail::isDirectMidiSource(controller->source())) {
            widget->clearMediaInfo();
        }
        playlistActivity.markInteraction();
        publishPlaylist();
        return true;
    };

    const auto openFolder = [&window, &prepareFolder] {
        QString initialFolder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (initialFolder.isEmpty()) {
            initialFolder = QDir::homePath();
        }
        const QString folder = nenia::app::getNeniaExistingDirectory(
            &window, QObject::tr("Open Music Folder"), initialFolder);
        if (!folder.isEmpty()) {
            prepareFolder(folder, true);
        }
    };

    const auto openPlaylistFile = [&window, &preparePlaylistFile] {
        const QString playlistPath = nenia::app::getNeniaOpenFileName(
            &window,
            QObject::tr("Open Playlist File"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QObject::tr("M3U Playlists (*.m3u *.m3u8)"));
        if (!playlistPath.isEmpty()) {
            preparePlaylistFile(playlistPath);
        }
    };

    const auto openFile = [&window, playlist, &playlistSourceLabel,
                           &currentPlaylistFile, &playlistSidecarsEnabled,
                           &playlistActivity, &publishPlaylist] {
        const QString initialFolder =
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        const QStringList paths = nenia::app::getNeniaOpenFileNames(
            &window, QObject::tr("Open File"), initialFolder,
            QObject::tr("Media, MIDI, and Playlists (*.mid *.midi *.ogg *.mp3 *.wav "
                        "*.flac *.aac *.m4a *.opus *.wma *.aiff *.aif *.ape *.m3u "
                        "*.m3u8);;All files (*)"));
        if (paths.isEmpty()) {
            return;
        }

#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
        constexpr bool directMidiEnabled = true;
#else
        constexpr bool directMidiEnabled = false;
#endif
        nenia::app::NeniaFileImportResult imported;
        QString error;
        const bool collected = nenia::app::collectNeniaOpenFileItems(
            paths, directMidiEnabled, &imported, &error);
        if (!collected) {
            QString details = error;
            if (!imported.warnings.isEmpty()) {
                details += QStringLiteral("\n\n") + imported.warnings.join(QLatin1Char('\n'));
            }
            QMessageBox::warning(&window, QObject::tr("Open File"), details);
            return;
        }
        const int addedCount = static_cast<int>(imported.items.size());
        if (!playlist->appendItems(imported.items, &error)) {
            QMessageBox::warning(&window, QObject::tr("Open File"), error);
            return;
        }

        playlistSourceLabel = QObject::tr("Open Files");
        currentPlaylistFile.clear();
        playlistSidecarsEnabled = true;
        playlistActivity.markInteraction();
        publishPlaylist();
        qInfo().noquote() << "Appended" << addedCount << "item(s) to the Nenia playlist";
        if (!imported.warnings.isEmpty()) {
            QMessageBox::warning(&window, QObject::tr("Some Files Were Skipped"),
                                 imported.warnings.join(QLatin1Char('\n')));
        }
    };

    const auto savePlaylist = [&window, playlist, &currentPlaylistFile, &playlistActivity] {
        QString target = currentPlaylistFile;
        if (target.isEmpty()) {
            target = QFileDialog::getSaveFileName(
                &window,
                QObject::tr("Save Playlist"),
                QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                    + QStringLiteral("/playlist.m3u8"),
                QObject::tr("M3U8 Playlists (*.m3u8)"));
            if (target.isEmpty()) {
                return;
            }
            if (!target.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
                target += QStringLiteral(".m3u8");
            }
        }

        QString error;
        if (!nenia::saveNeniaM3u8Playlist(target, playlist->items(), &error)) {
            qWarning().noquote() << "Nenia playlist save:" << error;
            return;
        }
        currentPlaylistFile = QFileInfo(target).absoluteFilePath();
        playlistActivity.markInteraction();
        qInfo().noquote() << "Nenia playlist saved:" << currentPlaylistFile;
    };

    const auto selectBy = [&](int delta) {
        if (playlist->isEmpty()) {
            return false;
        }
        const int next = std::clamp(selectedIndex + delta, 0, playlist->count() - 1);
        if (next == selectedIndex) {
            publishPlaylist();
            return false;
        }
        selectedIndex = next;
        publishPlaylist();
        return true;
    };
    const auto selectHome = [&] {
        if (playlist->isEmpty()) {
            return false;
        }
        selectedIndex = 0;
        publishPlaylist();
        return true;
    };
    const auto selectEnd = [&] {
        if (playlist->isEmpty()) {
            return false;
        }
        selectedIndex = playlist->count() - 1;
        publishPlaylist();
        return true;
    };
    const auto playSelected = [&] {
        if (selectedIndex < 0 || selectedIndex >= playlist->count()) {
            return false;
        }
        return playlist->playIndex(selectedIndex);
    };
    const auto removeSelected = [&] {
        if (selectedIndex < 0 || selectedIndex >= playlist->count()) {
            return false;
        }
        QString error;
        const bool removed = playlist->removeIndex(selectedIndex, &error);
        if (!removed) {
            qWarning().noquote() << "Nenia playlist remove:" << error;
            return false;
        }
        if (playlist->isEmpty()) {
            selectedIndex = -1;
        } else {
            selectedIndex = std::min(selectedIndex, playlist->count() - 1);
        }
        publishPlaylist();
        return true;
    };

    const auto toggleMediaInfoPage = [&] {
        extendedMediaInfoPage = !extendedMediaInfoPage;
        nenia::NeniaPlaybackUiState state = widget->playbackUiState();
        state.extendedMediaInfo = extendedMediaInfoPage;
        widget->setPlaybackUiState(state);
    };
    const auto previousTrack = [&] {
        if (playlist->previous()) {
            selectedIndex = playlist->currentIndex();
            publishPlaylist();
        }
    };
    const auto nextTrack = [&] {
        if (playlist->next()) {
            selectedIndex = playlist->currentIndex();
            publishPlaylist();
        }
    };
    const auto togglePlayPause = [&] {
        if (controller->playbackState() == QMediaPlayer::PlayingState) {
            controller->pause();
        } else {
            controller->play();
        }
    };
    const auto selectPlaylistRow = [&](int index) {
        if (index < 0 || index >= playlist->count()) {
            return;
        }
        selectedIndex = index;
        playlistActivity.markInteraction();
        publishPlaylist();
    };
    const auto activatePlaylistRow = [&](int index) {
        if (index < 0 || index >= playlist->count()) {
            return;
        }
        selectedIndex = index;
        playlistActivity.markInteraction();
        publishPlaylist();
        playSelected();
    };
    QObject::connect(widget, &nenia::NeniaWidget::playlistRowSelected,
                     &window, selectPlaylistRow);
    QObject::connect(widget, &nenia::NeniaWidget::playlistRowActivated,
                     &window, activatePlaylistRow);
    const auto dispatchTransport = [&](nenia::NeniaTransportControl control) {
        switch (control) {
        case nenia::NeniaTransportControl::Previous:
            playlistActivity.markInteraction();
            previousTrack();
            break;
        case nenia::NeniaTransportControl::PlayPause:
            togglePlayPause();
            break;
        case nenia::NeniaTransportControl::Next:
            playlistActivity.markInteraction();
            nextTrack();
            break;
        case nenia::NeniaTransportControl::Loop:
        case nenia::NeniaTransportControl::Shuffle:
            nenia::app::applyPlaybackOptionControl(*playlist, control);
            break;
        case nenia::NeniaTransportControl::None:
            break;
        }
    };
    QObject::connect(widget, &nenia::NeniaWidget::transportControlActivated,
                     &window, dispatchTransport);

    nenia::app::NeniaMenuHandlers menuHandlers;
    menuHandlers.openFolder = openFolder;
    menuHandlers.openPlaylist = openPlaylistFile;
    menuHandlers.openFile = openFile;
    menuHandlers.transport = dispatchTransport;
    nenia::app::NeniaMenuActions menuActions;
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    quint64 latestMidiLoudnessRequest = 0;
    nenia::detail::NeniaMidiLoudnessPreset confirmedMidiLoudnessPreset =
        midiRouter->confirmedRealtimeMidiLoudnessPreset();
    menuHandlers.midiPresetMenuVisible = true;
    menuHandlers.midiPresetSelectionEnabled =
        midiRouter->userMidiPresetSelectionEnabled();
    menuHandlers.midiPresetInitialValue = static_cast<nenia::app::NeniaMidiPreset>(
        confirmedMidiLoudnessPreset);
    menuHandlers.midiPresetHasEffectiveValue = true;
    if (!menuHandlers.midiPresetSelectionEnabled) {
        menuHandlers.midiPresetLockMessage = QObject::tr(
            "Locked by startup policy — effective preset %1")
            .arg(QString::fromLatin1(
                nenia::detail::neniaMidiLoudnessPresetDefinition(
                    effectiveMidiStartup.initialPreset).name));
    }
    if (menuHandlers.midiPresetSelectionEnabled) {
        menuHandlers.midiPresetRequested =
            [controller, midiRouter, &settings, &menuActions,
             &latestMidiLoudnessRequest,
             &confirmedMidiLoudnessPreset](nenia::app::NeniaMidiPreset preset) {
                const auto detailPreset =
                    static_cast<nenia::detail::NeniaMidiLoudnessPreset>(preset);
                const auto confirmedUiPreset =
                    static_cast<nenia::app::NeniaMidiPreset>(
                        confirmedMidiLoudnessPreset);
                if (nenia::detail::isDirectMidiSource(controller->source())
                    && midiRouter->hasPreparedRealtimeMidiSource()) {
                    QString error;
                    const quint64 requestId = midiRouter->requestRealtimeMidiLoudnessPreset(
                        detailPreset, &error);
                    if (requestId == 0) {
                        nenia::app::synchronizeMidiPresetMenu(
                            menuActions, confirmedUiPreset);
                        qWarning().noquote()
                            << "[NENIA MIDI PRESET] live preset request failed:" << error;
                        return;
                    }
                    latestMidiLoudnessRequest = requestId;
                    nenia::app::synchronizeMidiPresetMenu(
                        menuActions, confirmedUiPreset);
                    return;
                }

                QString error;
                if (!midiRouter->setPreferredUserMidiLoudnessPreset(detailPreset, &error)) {
                    nenia::app::synchronizeMidiPresetMenu(
                        menuActions, confirmedUiPreset);
                    qWarning().noquote()
                        << "[NENIA MIDI PRESET] could not select preset"
                        << midiPresetToken(preset) << ':' << error;
                    return;
                }
                if (!nenia::app::saveMidiLoudnessPresetToken(
                        settings, midiPresetToken(preset))) {
                    (void)midiRouter->setPreferredUserMidiLoudnessPreset(
                        confirmedMidiLoudnessPreset);
                    confirmedMidiLoudnessPreset =
                        midiRouter->confirmedRealtimeMidiLoudnessPreset();
                    nenia::app::synchronizeMidiPresetMenu(
                        menuActions, static_cast<nenia::app::NeniaMidiPreset>(
                            confirmedMidiLoudnessPreset));
                    qWarning().noquote()
                        << "[NENIA MIDI PRESET] could not save the preference; selection was restored";
                    return;
                }
                confirmedMidiLoudnessPreset =
                    midiRouter->confirmedRealtimeMidiLoudnessPreset();
                nenia::app::synchronizeMidiPresetMenu(
                    menuActions, static_cast<nenia::app::NeniaMidiPreset>(
                        confirmedMidiLoudnessPreset));
            };
    }
#endif
    menuHandlers.keyboardRangeChanged = [&settings, widget, &menuActions](
                                            nenia::detail::KeyboardRangeMode mode) {
        const auto &descriptor = nenia::detail::keyboardRangeDescriptor(mode);
        if (!widget->setKeyboardRange(descriptor.lowestMidiNote,
                                      descriptor.highestMidiNote)) {
            return;
        }
        nenia::app::saveKeyboardRangeMode(settings, mode);
        nenia::app::synchronizeKeyboardRangeMenu(menuActions, mode);
    };
    menuHandlers.octaveNamingConventionChanged =
        [&settings, widget, &menuActions](nenia::detail::OctaveNamingConvention convention) {
            const auto &descriptor = nenia::detail::octaveNamingDescriptor(convention);
            if (!widget->setOctaveNamingMiddleC(descriptor.middleCOctave)) {
                return;
            }
            nenia::app::saveOctaveNamingConvention(settings, convention);
            nenia::app::synchronizeOctaveNamingConventionMenu(menuActions, convention);
        };
    menuHandlers.colorSchemeChanged = [&settings, &window, widget, &systemPalette,
                                       &menuActions](nenia::detail::NeniaColorScheme scheme) {
        const auto previous = nenia::detail::activeNeniaColorScheme();
        if (!nenia::app::saveNeniaColorScheme(settings, scheme)) {
            nenia::app::synchronizeNeniaColorSchemeMenu(menuActions, previous);
            qWarning().noquote()
                << "[NENIA COLOR SCHEME] could not save the preference; selection was restored";
            return;
        }
        if (!nenia::detail::setActiveNeniaColorScheme(scheme)) {
            nenia::app::synchronizeNeniaColorSchemeMenu(menuActions, previous);
            return;
        }
        window.setPalette(nenia::app::paletteForNeniaColorScheme(systemPalette, scheme));
        widget->update();
        nenia::app::synchronizeNeniaColorSchemeMenu(menuActions, scheme);
    };
    menuActions = nenia::app::installNeniaMenuBar(
        &window, playlist, menuHandlers, keyboardRange, octaveNamingConvention, colorScheme);
#ifdef NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    if (menuHandlers.midiPresetSelectionEnabled) {
        QObject::connect(
            midiRouter,
            &nenia::app::NeniaApplicationPlaybackRouter::realtimeMidiLoudnessPresetChangeCompleted,
            &window,
            [midiRouter, &settings, &menuActions,
             &latestMidiLoudnessRequest,
             &confirmedMidiLoudnessPreset](
                quint64 requestId, nenia::detail::NeniaMidiLoudnessPreset preset,
                double synthGain, double postSynthGainDb, bool limiterEnabled,
                int activeVoicesBefore, int activeVoicesAfter,
                quint64 workerThreadIdentity, bool succeeded,
                const QString &errorMessage) {
                Q_UNUSED(synthGain);
                Q_UNUSED(postSynthGainDb);
                Q_UNUSED(limiterEnabled);
                Q_UNUSED(activeVoicesBefore);
                Q_UNUSED(activeVoicesAfter);
                Q_UNUSED(workerThreadIdentity);
                if (requestId != latestMidiLoudnessRequest) {
                    return;
                }
                confirmedMidiLoudnessPreset =
                    midiRouter->confirmedRealtimeMidiLoudnessPreset();
                const auto confirmedUiPreset =
                    static_cast<nenia::app::NeniaMidiPreset>(
                        confirmedMidiLoudnessPreset);
                nenia::app::synchronizeMidiPresetMenu(
                    menuActions, confirmedUiPreset);
                if (succeeded) {
                    const QString token = QString::fromLatin1(
                        nenia::detail::neniaMidiLoudnessPresetDefinition(preset).name);
                    if (!nenia::app::saveMidiLoudnessPresetToken(settings, token)) {
                        qWarning().noquote()
                            << "[NENIA MIDI PRESET] preset" << token
                            << "was applied, but its preference could not be saved";
                    }
                } else {
                    qWarning().noquote()
                        << "[NENIA MIDI PRESET] worker could not apply the requested preset:"
                        << errorMessage;
                }
            });
    }
    QAction *chooseSoundFontAction = menuActions.settingsMenu->addAction(
        QObject::tr("Choose SoundFont…"));
    QObject::connect(chooseSoundFontAction, &QAction::triggered, &window,
                     [&window, &settings, midiRouter] {
        QString initialPath = nenia::app::loadSoundFontPath(settings);
        if (initialPath.isEmpty()) {
            initialPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        }
        const QString path = QFileDialog::getOpenFileName(
            &window, QObject::tr("Choose SoundFont 2 File"), initialPath,
            QObject::tr("SoundFont 2 files (*.sf2)"));
        if (path.isEmpty()) {
            return;
        }
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        nenia::app::saveSoundFontPath(settings, absolutePath);
        midiRouter->setSavedSoundFontPreference(absolutePath);
        const auto effective = midiRouter->soundFontConfiguration();
        if (effective.source == nenia::app::NeniaSoundFontConfigSource::CommandLine
            || effective.source == nenia::app::NeniaSoundFontConfigSource::Environment) {
            QMessageBox::information(
                &window, QObject::tr("SoundFont Preference Saved"),
                QObject::tr("The saved preference is used only when neither --sf2 nor NENIA_SOUNDFONT is set. This run still uses %1 (%2).")
                    .arg(nenia::app::soundFontConfigSourceName(effective.source),
                         QFileInfo(effective.path).absoluteFilePath()));
        } else {
            QMessageBox::information(
                &window, QObject::tr("SoundFont Preference Saved"),
                QObject::tr("The saved SoundFont will be used the next time a MIDI source is activated. Active synthesis is not hot-reloaded."));
        }
    });
#endif
    nenia::app::NeniaGlobalInputActions inputActions;
    inputActions.playlistInteraction = [&playlistActivity] {
        playlistActivity.markInteraction();
    };
    inputActions.openFolder = openFolder;
    inputActions.openFile = openFile;
    inputActions.openPlaylist = openPlaylistFile;
    inputActions.toggleMediaInfoPage = toggleMediaInfoPage;
    inputActions.previous = previousTrack;
    inputActions.next = nextTrack;
    inputActions.selectBy = [&](int delta) { selectBy(delta); };
    inputActions.selectFirst = selectHome;
    inputActions.selectLast = selectEnd;
    inputActions.playSelected = [&] { playSelected(); };
    inputActions.removeSelected = [&] { removeSelected(); };
    inputActions.savePlaylist = savePlaylist;
    inputActions.seekBy = [&](int deltaMs) {
        controller->seekBy(deltaMs);
    };
    inputActions.seekToStart = [&] { controller->setPosition(0); };
    inputActions.stop = [&] { controller->stop(); };
    inputActions.togglePlayPause = togglePlayPause;
    inputActions.restart = [&] { controller->restart(); };
    inputActions.quit = [&] { application.quit(); };
    nenia::app::NeniaGlobalInput globalInput(std::move(inputActions));
    QObject::connect(controller, &nenia::NeniaPlaybackController::mediaStatusChanged,
                     &application, [&globalInput](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            globalInput.suppressRightAutoRepeatUntilRelease();
        }
    });
    auto *inputRouter = new nenia::app::NeniaGlobalInputRouter(
        &window, &globalInput, &window);
    application.installEventFilter(inputRouter);

    nenia::NeniaMediaInfo overrides;
    if (parser.isSet(titleOption)) {
        overrides.title = parser.value(titleOption);
    }
    if (parser.isSet(artistOption)) {
        overrides.artist = parser.value(artistOption);
    }
    if (parser.isSet(albumOption)) {
        overrides.album = parser.value(albumOption);
    }
    if (!overrides.title.isEmpty() || !overrides.artist.isEmpty() || !overrides.album.isEmpty()) {
        widget->setMediaInfo(overrides);
    }
    if (parser.isSet(artworkOption)) {
        QString error;
        const QImage artwork = nenia::NeniaWidget::loadArtworkFile(
            QFileInfo(parser.value(artworkOption)).absoluteFilePath(), &error);
        if (artwork.isNull()) {
            qWarning().noquote() << "Nenia artwork override disabled:" << error;
        } else {
            widget->setArtwork(artwork);
        }
    }

    if (hasAudio) {
        const QString audioPath = QFileInfo(parser.value(audioOption)).absoluteFilePath();
        controller->setSource(QUrl::fromLocalFile(audioPath));
        if (parser.isSet(timelineOption) || parser.isSet(midiOption)) {
            const QString providerPath = QFileInfo(
                parser.isSet(timelineOption) ? parser.value(timelineOption)
                                             : parser.value(midiOption)).absoluteFilePath();
            QString error;
            const auto provider = parser.isSet(timelineOption)
                ? loadJsonTimeline(providerPath, &error)
                : loadMidiTimeline(providerPath, &error);
            if (!provider) {
                qWarning().noquote() << "Nenia visual timeline disabled:" << error;
            } else {
                controller->setTimelineProvider(provider);
            }
        }
        qInfo().noquote() << "Audio prepared:" << QFileInfo(audioPath).fileName()
                          << "(startup playback remains stopped)";
    } else if (hasFolder) {
        prepareFolder(parser.value(playlistFolderOption), true);
    } else if (hasPlaylistFile) {
        preparePlaylistFile(parser.value(playlistFileOption));
    } else if (hasDirectMidi) {
        const QString midiPath = QFileInfo(parser.value(directMidiOption)).absoluteFilePath();
        const QUrl midiSource = QUrl::fromLocalFile(midiPath);
        QString error;
        if (!playlist->loadItems({{midiSource, QFileInfo(midiPath).fileName()}},
                                 nenia::NeniaPlaylistLoadBehavior::PrepareFirst,
                                 &error)) {
            qWarning().noquote() << "Nenia direct MIDI:" << error;
        } else {
            playlistSourceLabel = QFileInfo(midiPath).fileName();
            currentPlaylistFile.clear();
            playlistSidecarsEnabled = false;
            selectedIndex = 0;
            playlistActivity.markInteraction();
            publishPlaylist();
            qInfo().noquote() << "Direct MIDI prepared:" << QFileInfo(midiPath).fileName()
                              << "(startup playback remains stopped)";
        }
    } else {
        const QString lastFolder = nenia::app::loadLastFolder(settings);
        const QFileInfo lastFolderInfo(lastFolder);
        if (!lastFolder.isEmpty() && lastFolderInfo.exists() && lastFolderInfo.isDir()
            && lastFolderInfo.isReadable()) {
            prepareFolder(lastFolder, false);
            qInfo().noquote() << "Restored last folder:" << normalizedFolderPath(lastFolder);
        } else if (!lastFolder.isEmpty()) {
            qWarning().noquote() << "Nenia last folder is unavailable:" << lastFolder;
            nenia::app::clearLastFolder(settings);
        }
    }

    publishPlaylist();
    qInfo().noquote()
        << "Nenia controls: O open file | Ctrl+O folder | I Media Info | Up/Down select"
        << "| Enter play | Delete/D remove | Ctrl+S save | P/N previous/next"
        << "| Left/Right seek | Home start | S stop | Space play/pause | R restart | Ctrl+Q quit";

    window.show();
    return application.exec();
}
