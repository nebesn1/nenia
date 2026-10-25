#pragma once

#include <QKeyEvent>
#include <QObject>
#include <QPointer>

#include <functional>

class QMainWindow;

namespace nenia::app {

struct NeniaGlobalInputActions
{
    std::function<void()> playlistInteraction;
    std::function<void()> openFolder;
    std::function<void()> openFile;
    std::function<void()> openPlaylist;
    std::function<void()> toggleMediaInfoPage;
    std::function<void()> previous;
    std::function<void()> next;
    std::function<void(int)> selectBy;
    std::function<void()> selectFirst;
    std::function<void()> selectLast;
    std::function<void()> playSelected;
    std::function<void()> removeSelected;
    std::function<void()> savePlaylist;
    std::function<void(int)> seekBy;
    std::function<void()> seekToStart;
    std::function<void()> stop;
    std::function<void()> togglePlayPause;
    std::function<void()> restart;
    std::function<void()> quit;
};

class NeniaGlobalInput final
{
public:
    explicit NeniaGlobalInput(NeniaGlobalInputActions actions);

    bool handleKey(const QKeyEvent *event);
    bool handleKeyRelease(const QKeyEvent *event);
    void suppressRightAutoRepeatUntilRelease() noexcept;
    void cancelHeldKeySequence() noexcept;

private:
    void runPlaylistAction(const std::function<void()> &action) const;

    NeniaGlobalInputActions m_actions;
    bool m_rightArrowHeld = false;
    bool m_suppressRightAutoRepeat = false;
};

class NeniaGlobalInputRouter final : public QObject
{
public:
    NeniaGlobalInputRouter(QMainWindow *window,
                           NeniaGlobalInput *input,
                           QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QMainWindow> m_window;
    NeniaGlobalInput *m_input = nullptr;
};

} // namespace nenia::app
