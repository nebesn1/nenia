#pragma once

#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <vector>

namespace nenia {

class NeniaPlaybackController;

enum class NeniaPlaybackOrder {
    StopAfterCurrent,
    RepeatOne,
    RepeatAll,
    Shuffle
};

enum class NeniaLoopMode {
    Off,
    RepeatOne,
    RepeatAll
};

enum class NeniaPlaylistLoadBehavior {
    PrepareFirst,
    PlayFirst
};

struct NeniaPlaylistItem {
    QUrl source;
    QString displayName;
};

class NeniaPlaylistController final : public QObject
{
    Q_OBJECT

public:
    explicit NeniaPlaylistController(NeniaPlaybackController *playbackController,
                                      QObject *parent = nullptr);
    ~NeniaPlaylistController() override;

    bool loadDirectory(const QString &directoryPath, QString *errorMessage = nullptr);
    bool loadDirectorySelecting(const QString &directoryPath,
                                const QUrl &initialSource,
                                QString *errorMessage = nullptr);
    bool loadDirectoryPrepared(const QString &directoryPath,
                               QString *errorMessage = nullptr);
    bool loadItems(const std::vector<NeniaPlaylistItem> &items,
                   NeniaPlaylistLoadBehavior behavior = NeniaPlaylistLoadBehavior::PrepareFirst,
                   QString *errorMessage = nullptr);
    bool appendItems(const std::vector<NeniaPlaylistItem> &items,
                     QString *errorMessage = nullptr);
    void clearPlaylist();
    bool removeIndex(int index, QString *errorMessage = nullptr);

    int count() const noexcept;
    bool isEmpty() const noexcept;
    int currentIndex() const noexcept;
    const NeniaPlaylistItem *currentItem() const noexcept;
    const std::vector<NeniaPlaylistItem> &items() const noexcept;

    // Compatibility API for clients of the former mutually-exclusive order
    // model. The getter is a lossy projection of loopMode()/shuffleEnabled().
    // Legacy Shuffle maps to RepeatAll plus shuffle enabled.
    NeniaPlaybackOrder playbackOrder() const noexcept;
    void setPlaybackOrder(NeniaPlaybackOrder order);

    NeniaLoopMode loopMode() const noexcept;
    void setLoopMode(NeniaLoopMode mode);
    bool shuffleEnabled() const noexcept;
    void setShuffleEnabled(bool enabled);
    void setPlaybackOptions(NeniaLoopMode loopMode, bool shuffleEnabled);

    bool playIndex(int index);
    bool previous();
    bool next();
    bool canGoPrevious() const noexcept;
    bool canGoNext() const noexcept;

signals:
    void playlistChanged();
    void currentIndexChanged(int index);
    void currentTrackChanged();
    void playbackOrderChanged(nenia::NeniaPlaybackOrder order);
    void playbackOptionsChanged(nenia::NeniaLoopMode loopMode, bool shuffleEnabled);
    void errorOccurred(const QString &errorMessage);

private slots:
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    static bool isValidPlaybackOrder(NeniaPlaybackOrder order) noexcept;
    static bool isValidLoopMode(NeniaLoopMode mode) noexcept;

    bool activateIndex(int index, bool resetShuffleHistory, bool startPlayback = true);
    bool scanDirectory(const QString &directoryPath,
                       std::vector<NeniaPlaylistItem> *items,
                       QString *errorMessage) const;
    bool replaceItems(std::vector<NeniaPlaylistItem> items,
                      int selectedIndex,
                      NeniaPlaylistLoadBehavior behavior,
                      QString *errorMessage);
    void startCurrentPlayback();
    void prepareCurrentSource();
    void resetShuffleHistory();
    void refillShuffleRemaining();
    bool shufflePrevious();
    bool shuffleNext(bool allowRefill);
    void handleNaturalEndOfMedia();

    QPointer<NeniaPlaybackController> m_playbackController;
    std::vector<NeniaPlaylistItem> m_items;
    int m_currentIndex = -1;
    NeniaLoopMode m_loopMode = NeniaLoopMode::Off;
    bool m_shuffleEnabled = false;
    std::vector<int> m_shuffleHistory;
    int m_shuffleHistoryPosition = -1;
    std::vector<int> m_shuffleRemaining;
    bool m_playlistActive = false;
    bool m_endOfMediaHandled = false;
    bool m_endOfMediaPending = false;
};

} // namespace nenia

Q_DECLARE_METATYPE(nenia::NeniaPlaybackOrder)
Q_DECLARE_METATYPE(nenia::NeniaLoopMode)
