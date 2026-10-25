#include <nenia/NeniaPlaylistController.h>

#include <nenia/NeniaPlaybackController.h>

#include "NeniaDirectMidiSource.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRandomGenerator>

#include <algorithm>

namespace nenia {
namespace {

bool isExcludedPlaylistMetadata(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("json") || suffix == QStringLiteral("mid")
        || suffix == QStringLiteral("midi") || suffix == QStringLiteral("m3u")
        || suffix == QStringLiteral("m3u8") || suffix == QStringLiteral("pls")
        || suffix == QStringLiteral("xspf");
}

bool isSupportedAudioFile(const QFileInfo &fileInfo, const QMimeDatabase &mimeDatabase)
{
    if (fileInfo.fileName().startsWith(QLatin1Char('.')) || fileInfo.isSymLink()
        || !fileInfo.isFile() || !fileInfo.isReadable()) {
        return false;
    }

    if (detail::hasDirectMidiFileSuffix(fileInfo.fileName())) {
#ifdef NENIA_ENABLE_DIRECT_MIDI
        return true;
#else
        return false;
#endif
    }
    if (isExcludedPlaylistMetadata(fileInfo)) {
        return false;
    }

    const QMimeType mimeType =
        mimeDatabase.mimeTypeForFile(fileInfo, QMimeDatabase::MatchExtension);
    return mimeType.isValid() && mimeType.name().startsWith(QStringLiteral("audio/"));
}

int naturalCompare(const QString &left, const QString &right, QCollator &collator)
{
    int leftPosition = 0;
    int rightPosition = 0;
    while (leftPosition < left.size() && rightPosition < right.size()) {
        const bool leftIsDigit = left.at(leftPosition).isDigit();
        const bool rightIsDigit = right.at(rightPosition).isDigit();
        if (leftIsDigit && rightIsDigit) {
            const int leftStart = leftPosition;
            const int rightStart = rightPosition;
            while (leftPosition < left.size() && left.at(leftPosition).isDigit()) {
                ++leftPosition;
            }
            while (rightPosition < right.size() && right.at(rightPosition).isDigit()) {
                ++rightPosition;
            }

            const QString leftDigits = left.mid(leftStart, leftPosition - leftStart);
            const QString rightDigits = right.mid(rightStart, rightPosition - rightStart);
            int leftSignificantStart = 0;
            while (leftSignificantStart + 1 < leftDigits.size()
                   && leftDigits.at(leftSignificantStart) == QLatin1Char('0')) {
                ++leftSignificantStart;
            }
            int rightSignificantStart = 0;
            while (rightSignificantStart + 1 < rightDigits.size()
                   && rightDigits.at(rightSignificantStart) == QLatin1Char('0')) {
                ++rightSignificantStart;
            }
            const QString leftSignificant = leftDigits.mid(leftSignificantStart);
            const QString rightSignificant = rightDigits.mid(rightSignificantStart);
            if (leftSignificant.size() != rightSignificant.size()) {
                return leftSignificant.size() < rightSignificant.size() ? -1 : 1;
            }
            const int numericComparison = QString::compare(
                leftSignificant, rightSignificant, Qt::CaseSensitive);
            if (numericComparison != 0) {
                return numericComparison < 0 ? -1 : 1;
            }
            continue;
        }

        const int leftStart = leftPosition;
        const int rightStart = rightPosition;
        while (leftPosition < left.size() && !left.at(leftPosition).isDigit()) {
            ++leftPosition;
        }
        while (rightPosition < right.size() && !right.at(rightPosition).isDigit()) {
            ++rightPosition;
        }
        const int textComparison = collator.compare(left.mid(leftStart, leftPosition - leftStart),
                                                     right.mid(rightStart,
                                                               rightPosition - rightStart));
        if (textComparison != 0) {
            return textComparison < 0 ? -1 : 1;
        }
    }

    if (leftPosition != left.size() || rightPosition != right.size()) {
        return leftPosition == left.size() ? -1 : 1;
    }
    return 0;
}

QString normalizedDirectoryPath(const QString &directoryPath)
{
    return QFileInfo(directoryPath).absoluteFilePath();
}

} // namespace

NeniaPlaylistController::NeniaPlaylistController(
    NeniaPlaybackController *playbackController,
    QObject *parent)
    : QObject(parent)
    , m_playbackController(playbackController)
{
    if (playbackController == nullptr) {
        return;
    }

    connect(playbackController,
            &NeniaPlaybackController::mediaStatusChanged,
            this,
            &NeniaPlaylistController::handleMediaStatusChanged);
    connect(playbackController,
            &NeniaPlaybackController::playbackStateChanged,
            this,
            &NeniaPlaylistController::handlePlaybackStateChanged);
    connect(playbackController,
            &NeniaPlaybackController::errorOccurred,
            this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                const QString message = errorString.trimmed().isEmpty()
                    ? QStringLiteral("Audio playback error (%1).").arg(
                          static_cast<int>(error))
                    : errorString;
                emit errorOccurred(message);
            });
}

NeniaPlaylistController::~NeniaPlaylistController() = default;

bool NeniaPlaylistController::loadDirectory(const QString &directoryPath,
                                            QString *errorMessage)
{
    QString localError;
    QString *scanError = errorMessage != nullptr ? errorMessage : &localError;
    std::vector<NeniaPlaylistItem> items;
    if (!scanDirectory(directoryPath, &items, scanError)) {
        emit errorOccurred(*scanError);
        return false;
    }
    return replaceItems(std::move(items), 0,
                        NeniaPlaylistLoadBehavior::PlayFirst, errorMessage);
}

bool NeniaPlaylistController::loadDirectorySelecting(const QString &directoryPath,
                                                     const QUrl &initialSource,
                                                     QString *errorMessage)
{
    QString localError;
    QString *scanError = errorMessage != nullptr ? errorMessage : &localError;
    std::vector<NeniaPlaylistItem> scannedItems;
    if (!scanDirectory(directoryPath, &scannedItems, scanError)) {
        emit errorOccurred(*scanError);
        return false;
    }

    int selectedIndex = 0;
    if (initialSource.isValid()) {
        const auto selected = std::find_if(
            scannedItems.cbegin(),
            scannedItems.cend(),
            [&initialSource](const NeniaPlaylistItem &item) {
                return item.source == initialSource;
            });
        if (selected == scannedItems.cend()) {
            const QString message =
                QStringLiteral("Selected audio file was not found in this folder.");
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            emit errorOccurred(message);
            return false;
        }
        selectedIndex = static_cast<int>(std::distance(scannedItems.cbegin(), selected));
    }

    return replaceItems(std::move(scannedItems), selectedIndex,
                        NeniaPlaylistLoadBehavior::PlayFirst, errorMessage);
}

bool NeniaPlaylistController::loadDirectoryPrepared(const QString &directoryPath,
                                                     QString *errorMessage)
{
    QString localError;
    QString *scanError = errorMessage != nullptr ? errorMessage : &localError;
    std::vector<NeniaPlaylistItem> items;
    if (!scanDirectory(directoryPath, &items, scanError)) {
        emit errorOccurred(*scanError);
        return false;
    }
    return replaceItems(std::move(items), 0,
                        NeniaPlaylistLoadBehavior::PrepareFirst, errorMessage);
}

bool NeniaPlaylistController::loadItems(const std::vector<NeniaPlaylistItem> &items,
                                        NeniaPlaylistLoadBehavior behavior,
                                        QString *errorMessage)
{
    std::vector<NeniaPlaylistItem> replacement = items;
    return replaceItems(std::move(replacement), 0, behavior, errorMessage);
}

bool NeniaPlaylistController::appendItems(const std::vector<NeniaPlaylistItem> &items,
                                         QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (items.empty()) {
        const QString message = QStringLiteral("No playable items were provided to append.");
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }

    const int firstNewIndex = count();
    m_items.insert(m_items.end(), items.begin(), items.end());
    if (m_shuffleEnabled && currentItem() != nullptr) {
        for (int index = firstNewIndex; index < count(); ++index) {
            m_shuffleRemaining.push_back(index);
        }
        for (int index = count() - 1; index > firstNewIndex; --index) {
            const int swapIndex = firstNewIndex
                + QRandomGenerator::global()->bounded(index - firstNewIndex + 1);
            std::swap(m_shuffleRemaining.at(static_cast<std::size_t>(index - firstNewIndex)),
                      m_shuffleRemaining.at(static_cast<std::size_t>(swapIndex - firstNewIndex)));
        }
    }

    emit playlistChanged();
    return true;
}

void NeniaPlaylistController::clearPlaylist()
{
    if (!m_playlistActive && m_items.empty()) {
        return;
    }

    m_playlistActive = false;
    m_endOfMediaHandled = false;
    m_endOfMediaPending = false;
    if (m_playbackController != nullptr) {
        m_playbackController->stop();
        m_playbackController->clearTimelineProvider();
        m_playbackController->setSource({});
    }

    const bool hadCurrentTrack = m_currentIndex >= 0;
    m_items.clear();
    m_currentIndex = -1;
    m_shuffleHistory.clear();
    m_shuffleHistoryPosition = -1;
    m_shuffleRemaining.clear();

    emit playlistChanged();
    if (hadCurrentTrack) {
        emit currentIndexChanged(-1);
        emit currentTrackChanged();
    }
}

int NeniaPlaylistController::count() const noexcept
{
    return static_cast<int>(m_items.size());
}

const std::vector<NeniaPlaylistItem> &NeniaPlaylistController::items() const noexcept
{
    return m_items;
}

bool NeniaPlaylistController::isEmpty() const noexcept
{
    return m_items.empty();
}

int NeniaPlaylistController::currentIndex() const noexcept
{
    return m_currentIndex;
}

const NeniaPlaylistItem *NeniaPlaylistController::currentItem() const noexcept
{
    if (m_currentIndex < 0 || m_currentIndex >= count()) {
        return nullptr;
    }
    return &m_items[static_cast<std::size_t>(m_currentIndex)];
}

NeniaPlaybackOrder NeniaPlaylistController::playbackOrder() const noexcept
{
    if (m_loopMode == NeniaLoopMode::RepeatOne) {
        return NeniaPlaybackOrder::RepeatOne;
    }
    if (m_shuffleEnabled) {
        return NeniaPlaybackOrder::Shuffle;
    }
    if (m_loopMode == NeniaLoopMode::RepeatAll) {
        return NeniaPlaybackOrder::RepeatAll;
    }
    return NeniaPlaybackOrder::StopAfterCurrent;
}

void NeniaPlaylistController::setPlaybackOrder(NeniaPlaybackOrder order)
{
    if (!isValidPlaybackOrder(order)) {
        order = NeniaPlaybackOrder::StopAfterCurrent;
    }
    switch (order) {
    case NeniaPlaybackOrder::StopAfterCurrent:
        setPlaybackOptions(NeniaLoopMode::Off, false);
        return;
    case NeniaPlaybackOrder::RepeatOne:
        setPlaybackOptions(NeniaLoopMode::RepeatOne, false);
        return;
    case NeniaPlaybackOrder::RepeatAll:
        setPlaybackOptions(NeniaLoopMode::RepeatAll, false);
        return;
    case NeniaPlaybackOrder::Shuffle:
        // The legacy Shuffle mode automatically refilled its bag forever.
        setPlaybackOptions(NeniaLoopMode::RepeatAll, true);
        return;
    }
}

NeniaLoopMode NeniaPlaylistController::loopMode() const noexcept
{
    return m_loopMode;
}

void NeniaPlaylistController::setLoopMode(NeniaLoopMode mode)
{
    setPlaybackOptions(mode, m_shuffleEnabled);
}

bool NeniaPlaylistController::shuffleEnabled() const noexcept
{
    return m_shuffleEnabled;
}

void NeniaPlaylistController::setShuffleEnabled(bool enabled)
{
    setPlaybackOptions(m_loopMode, enabled);
}

void NeniaPlaylistController::setPlaybackOptions(NeniaLoopMode loopMode,
                                                 bool shuffleEnabled)
{
    if (!isValidLoopMode(loopMode)) {
        loopMode = NeniaLoopMode::Off;
    }
    if (loopMode == m_loopMode && shuffleEnabled == m_shuffleEnabled) {
        return;
    }

    const NeniaPlaybackOrder oldProjection = playbackOrder();
    const bool shuffleChanged = shuffleEnabled != m_shuffleEnabled;
    m_loopMode = loopMode;
    m_shuffleEnabled = shuffleEnabled;
    if (shuffleChanged && m_shuffleEnabled && currentItem() != nullptr) {
        resetShuffleHistory();
    } else if (shuffleChanged) {
        m_shuffleHistory.clear();
        m_shuffleHistoryPosition = -1;
        m_shuffleRemaining.clear();
    }

    emit playbackOptionsChanged(m_loopMode, m_shuffleEnabled);
    const NeniaPlaybackOrder newProjection = playbackOrder();
    if (newProjection != oldProjection) {
        emit playbackOrderChanged(newProjection);
    }
}

bool NeniaPlaylistController::playIndex(int index)
{
    if (index < 0 || index >= count()) {
        return false;
    }
    m_playlistActive = true;
    if (m_playbackController != nullptr
        && m_playbackController->isSameSource(
            m_items[static_cast<std::size_t>(index)].source)) {
        // Explicit playlist activation is the refresh boundary. Natural
        // EOM/Repeat One routing calls activateIndex() directly and does not
        // rescan the same directory.
        m_playbackController->refreshArtworkForCurrentSourceActivation(
            m_items[static_cast<std::size_t>(index)].source);
    }
    return activateIndex(index, m_shuffleEnabled);
}

bool NeniaPlaylistController::removeIndex(int index, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (index < 0 || index >= count()) {
        const QString message = QStringLiteral("Playlist index is out of range.");
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }

    const bool removedCurrent = index == m_currentIndex;
    const int oldCurrentIndex = m_currentIndex;
    if (removedCurrent && m_playbackController != nullptr) {
        m_playbackController->stop();
        m_playbackController->clearTimelineProvider();
        m_playbackController->setSource({});
    }

    m_items.erase(m_items.begin() + index);
    if (m_items.empty()) {
        m_playlistActive = false;
        m_currentIndex = -1;
        m_endOfMediaHandled = false;
        m_endOfMediaPending = false;
        m_shuffleHistory.clear();
        m_shuffleHistoryPosition = -1;
        m_shuffleRemaining.clear();
        emit playlistChanged();
        emit currentIndexChanged(-1);
        emit currentTrackChanged();
        return true;
    }

    if (removedCurrent) {
        m_currentIndex = std::min(index, count() - 1);
    } else if (index < oldCurrentIndex) {
        --m_currentIndex;
    }
    m_playlistActive = true;
    m_endOfMediaHandled = false;
    m_endOfMediaPending = false;
    if (m_shuffleEnabled) {
        resetShuffleHistory();
    } else {
        m_shuffleHistory.clear();
        m_shuffleHistoryPosition = -1;
        m_shuffleRemaining.clear();
    }

    emit playlistChanged();
    if (m_currentIndex != oldCurrentIndex) {
        emit currentIndexChanged(m_currentIndex);
    }
    if (removedCurrent) {
        emit currentTrackChanged();
        prepareCurrentSource();
    }
    return true;
}

bool NeniaPlaylistController::previous()
{
    if (isEmpty()) {
        return false;
    }
    if (m_shuffleEnabled) {
        return shufflePrevious();
    }

    int targetIndex = m_currentIndex - 1;
    if (m_loopMode == NeniaLoopMode::RepeatAll && targetIndex < 0) {
        targetIndex = count() - 1;
    }
    if (targetIndex < 0) {
        return false;
    }
    return activateIndex(targetIndex, false);
}

bool NeniaPlaylistController::next()
{
    if (isEmpty()) {
        return false;
    }
    if (m_shuffleEnabled) {
        return shuffleNext(true);
    }

    int targetIndex = m_currentIndex + 1;
    if (m_loopMode == NeniaLoopMode::RepeatAll && targetIndex >= count()) {
        targetIndex = 0;
    }
    if (targetIndex >= count()) {
        return false;
    }
    return activateIndex(targetIndex, false);
}

bool NeniaPlaylistController::canGoPrevious() const noexcept
{
    if (isEmpty()) {
        return false;
    }
    if (m_shuffleEnabled) {
        return m_shuffleHistoryPosition > 0;
    }
    return m_loopMode == NeniaLoopMode::RepeatAll || m_currentIndex > 0;
}

bool NeniaPlaylistController::canGoNext() const noexcept
{
    if (isEmpty()) {
        return false;
    }
    if (m_shuffleEnabled) {
        return true;
    }
    if (m_loopMode == NeniaLoopMode::RepeatAll) {
        return true;
    }
    return m_currentIndex >= 0 && m_currentIndex + 1 < count();
}

bool NeniaPlaylistController::isValidPlaybackOrder(NeniaPlaybackOrder order) noexcept
{
    switch (order) {
    case NeniaPlaybackOrder::StopAfterCurrent:
    case NeniaPlaybackOrder::RepeatOne:
    case NeniaPlaybackOrder::RepeatAll:
    case NeniaPlaybackOrder::Shuffle:
        return true;
    }
    return false;
}

bool NeniaPlaylistController::isValidLoopMode(NeniaLoopMode mode) noexcept
{
    switch (mode) {
    case NeniaLoopMode::Off:
    case NeniaLoopMode::RepeatOne:
    case NeniaLoopMode::RepeatAll:
        return true;
    }
    return false;
}

bool NeniaPlaylistController::activateIndex(int index,
                                            bool resetShuffleHistory,
                                            bool startPlayback)
{
    if (index < 0 || index >= count()) {
        return false;
    }

    const bool changed = m_currentIndex != index;
    m_currentIndex = index;
    m_playlistActive = true;
    if (resetShuffleHistory && m_shuffleEnabled) {
        this->resetShuffleHistory();
    }
    m_endOfMediaHandled = false;
    m_endOfMediaPending = false;

    if (changed) {
        emit currentIndexChanged(m_currentIndex);
    }
    emit currentTrackChanged();
    if (startPlayback) {
        startCurrentPlayback();
    } else {
        prepareCurrentSource();
    }
    return true;
}

bool NeniaPlaylistController::scanDirectory(const QString &directoryPath,
                                             std::vector<NeniaPlaylistItem> *items,
                                             QString *errorMessage) const
{
    if (items == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist scan output is null.");
        }
        return false;
    }
    items->clear();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (directoryPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist folder path is empty.");
        }
        return false;
    }

    const QFileInfo directoryInfo(directoryPath);
    const QString absolutePath = normalizedDirectoryPath(directoryPath);
    if (!directoryInfo.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist folder does not exist: %1")
                                 .arg(absolutePath);
        }
        return false;
    }
    if (!directoryInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist path is not a folder: %1")
                                 .arg(absolutePath);
        }
        return false;
    }
    if (!directoryInfo.isReadable()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Playlist folder is not readable: %1")
                                 .arg(absolutePath);
        }
        return false;
    }

    const QMimeDatabase mimeDatabase;
    const QDir root(absolutePath);
    QList<QString> pending{absolutePath};
    QSet<QString> visited;
    while (!pending.isEmpty()) {
        const QString currentPath = pending.takeFirst();
        const QString canonicalPath = QFileInfo(currentPath).canonicalFilePath();
        const QString visitPath = canonicalPath.isEmpty() ? currentPath : canonicalPath;
        if (visited.contains(visitPath)) {
            continue;
        }
        visited.insert(visitPath);

        const QDir directory(currentPath);
        const QFileInfoList entries = directory.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
            QDir::NoSort);
        for (const QFileInfo &fileInfo : entries) {
            if (fileInfo.fileName().startsWith(QLatin1Char('.'))
                || fileInfo.isSymLink()) {
                continue;
            }
            if (fileInfo.isDir()) {
                pending.push_back(fileInfo.absoluteFilePath());
                continue;
            }
            if (!isSupportedAudioFile(fileInfo, mimeDatabase)) {
                continue;
            }
            const QString relativePath = QDir::fromNativeSeparators(
                root.relativeFilePath(fileInfo.absoluteFilePath()));
            items->push_back({QUrl::fromLocalFile(fileInfo.absoluteFilePath()), relativePath});
        }
    }

    if (items->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "No supported audio files were found in this folder or its subfolders.");
        }
        return false;
    }

    QCollator collator(QLocale::c());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(items->begin(), items->end(), [&collator](const NeniaPlaylistItem &left,
                                                               const NeniaPlaylistItem &right) {
        const int displayComparison = naturalCompare(left.displayName, right.displayName,
                                                     collator);
        if (displayComparison != 0) {
            return displayComparison < 0;
        }
        return left.source.toLocalFile() < right.source.toLocalFile();
    });
    return true;
}

bool NeniaPlaylistController::replaceItems(std::vector<NeniaPlaylistItem> items,
                                            int selectedIndex,
                                            NeniaPlaylistLoadBehavior behavior,
                                            QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (items.empty()) {
        const QString message = QStringLiteral("Playlist contains no supported audio files.");
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(items.size()) - 1);

    m_items = std::move(items);
    m_currentIndex = selectedIndex;
    m_playlistActive = true;
    m_endOfMediaHandled = false;
    m_endOfMediaPending = false;
    if (m_shuffleEnabled) {
        resetShuffleHistory();
    } else {
        m_shuffleHistory.clear();
        m_shuffleHistoryPosition = -1;
        m_shuffleRemaining.clear();
    }

    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
    emit currentTrackChanged();
    if (behavior == NeniaPlaylistLoadBehavior::PlayFirst) {
        startCurrentPlayback();
    } else {
        prepareCurrentSource();
    }
    return true;
}

void NeniaPlaylistController::startCurrentPlayback()
{
    const NeniaPlaylistItem *item = currentItem();
    if (item == nullptr || m_playbackController == nullptr) {
        return;
    }

    if (m_playbackController->isSameSource(item->source)) {
        m_playbackController->restart();
        return;
    }

    m_playbackController->clearTimelineProvider();
    m_playbackController->setSource(item->source);
    m_playbackController->play();
}

void NeniaPlaylistController::prepareCurrentSource()
{
    const NeniaPlaylistItem *item = currentItem();
    if (item == nullptr || m_playbackController == nullptr) {
        return;
    }

    m_playbackController->stop();
    m_playbackController->clearTimelineProvider();
    m_playbackController->setSource(item->source);
}

void NeniaPlaylistController::resetShuffleHistory()
{
    m_shuffleHistory.clear();
    m_shuffleHistoryPosition = -1;
    m_shuffleRemaining.clear();
    if (!m_shuffleEnabled || currentItem() == nullptr) {
        return;
    }

    m_shuffleHistory.push_back(m_currentIndex);
    m_shuffleHistoryPosition = 0;
    refillShuffleRemaining();
}

void NeniaPlaylistController::refillShuffleRemaining()
{
    m_shuffleRemaining.clear();
    if (currentItem() == nullptr) {
        return;
    }

    for (int index = 0; index < count(); ++index) {
        if (index != m_currentIndex) {
            m_shuffleRemaining.push_back(index);
        }
    }

    for (int index = static_cast<int>(m_shuffleRemaining.size()) - 1; index > 0; --index) {
        const int swapIndex = QRandomGenerator::global()->bounded(index + 1);
        std::swap(m_shuffleRemaining[static_cast<std::size_t>(index)],
                  m_shuffleRemaining[static_cast<std::size_t>(swapIndex)]);
    }
}

bool NeniaPlaylistController::shufflePrevious()
{
    if (m_shuffleHistoryPosition <= 0
        || m_shuffleHistoryPosition >= static_cast<int>(m_shuffleHistory.size())) {
        return false;
    }

    --m_shuffleHistoryPosition;
    return activateIndex(
        m_shuffleHistory[static_cast<std::size_t>(m_shuffleHistoryPosition)], false);
}

bool NeniaPlaylistController::shuffleNext(bool allowRefill)
{
    if (isEmpty() || m_shuffleHistoryPosition < 0) {
        return false;
    }

    if (m_shuffleHistoryPosition + 1 < static_cast<int>(m_shuffleHistory.size())) {
        ++m_shuffleHistoryPosition;
        return activateIndex(
            m_shuffleHistory[static_cast<std::size_t>(m_shuffleHistoryPosition)], false);
    }

    if (m_shuffleRemaining.empty() && allowRefill) {
        refillShuffleRemaining();
    }
    if (m_shuffleRemaining.empty()) {
        if (allowRefill && count() == 1 && currentItem() != nullptr) {
            m_shuffleHistory.push_back(m_currentIndex);
            ++m_shuffleHistoryPosition;
            return activateIndex(m_currentIndex, false);
        }
        return false;
    }

    const int nextIndex = m_shuffleRemaining.back();
    m_shuffleRemaining.pop_back();
    m_shuffleHistory.push_back(nextIndex);
    ++m_shuffleHistoryPosition;
    return activateIndex(nextIndex, false);
}

void NeniaPlaylistController::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status != QMediaPlayer::EndOfMedia || !m_playlistActive || currentItem() == nullptr
        || m_playbackController == nullptr
        || m_playbackController->source() != currentItem()->source
        || m_endOfMediaHandled) {
        return;
    }

    m_endOfMediaHandled = true;
    if (m_endOfMediaPending) {
        return;
    }
    m_endOfMediaPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (!m_endOfMediaPending) {
                return;
            }
            m_endOfMediaPending = false;
            handleNaturalEndOfMedia();
        },
        Qt::QueuedConnection);
}

void NeniaPlaylistController::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlayingState && !m_endOfMediaPending) {
        // A user replay after StopAfterCurrent starts a new lifecycle for the
        // same source without changing the selected playlist item.
        m_endOfMediaHandled = false;
    }
}

void NeniaPlaylistController::handleNaturalEndOfMedia()
{
    if (!m_playlistActive || currentItem() == nullptr || m_playbackController == nullptr
        || m_playbackController->source() != currentItem()->source) {
        return;
    }

    switch (m_loopMode) {
    case NeniaLoopMode::Off:
        if (m_shuffleEnabled) {
            shuffleNext(false);
        }
        return;
    case NeniaLoopMode::RepeatOne:
        activateIndex(m_currentIndex, false);
        return;
    case NeniaLoopMode::RepeatAll: {
        if (m_shuffleEnabled) {
            shuffleNext(true);
            return;
        }
        const int targetIndex = (m_currentIndex + 1) % count();
        activateIndex(targetIndex, false);
        return;
    }
    }
}

} // namespace nenia
