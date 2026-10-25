#include "NeniaGlobalInput.h"

#include "NeniaPerformanceDiagnostics.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <utility>

namespace nenia::app {

NeniaGlobalInput::NeniaGlobalInput(NeniaGlobalInputActions actions)
    : m_actions(std::move(actions))
{
}

bool NeniaGlobalInput::handleKey(const QKeyEvent *event)
{
    if (event == nullptr || event->type() != QEvent::KeyPress) {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier)) {
        return false;
    }

    if (modifiers.testFlag(Qt::ControlModifier)) {
        if (modifiers != Qt::ControlModifier) {
            return false;
        }
        if (event->key() == Qt::Key_Q) {
            if (m_actions.quit) {
                m_actions.quit();
            }
            return true;
        }
        if (event->key() == Qt::Key_S) {
            runPlaylistAction(m_actions.savePlaylist);
            return true;
        }
        if (event->key() == Qt::Key_O) {
            runPlaylistAction(m_actions.openFolder);
            return true;
        }
        if (event->key() == Qt::Key_Home) {
            runPlaylistAction(m_actions.selectFirst);
            return true;
        }
        return false;
    }
    if (modifiers != Qt::NoModifier) {
        return false;
    }

    switch (event->key()) {
    case Qt::Key_O:
        runPlaylistAction(m_actions.openFile);
        return true;
    case Qt::Key_I:
        if (m_actions.toggleMediaInfoPage) {
            m_actions.toggleMediaInfoPage();
        }
        return true;
    case Qt::Key_P:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputPrevious);
        runPlaylistAction(m_actions.previous);
        return true;
    }
    case Qt::Key_N:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputNext);
        runPlaylistAction(m_actions.next);
        return true;
    }
    case Qt::Key_Up:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputUp);
        runPlaylistAction([this] {
            if (m_actions.selectBy) {
                m_actions.selectBy(-1);
            }
        });
        return true;
    }
    case Qt::Key_Down:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputDown);
        runPlaylistAction([this] {
            if (m_actions.selectBy) {
                m_actions.selectBy(1);
            }
        });
        return true;
    }
    case Qt::Key_PageUp:
        runPlaylistAction([this] {
            if (m_actions.selectBy) {
                m_actions.selectBy(-10);
            }
        });
        return true;
    case Qt::Key_PageDown:
        runPlaylistAction([this] {
            if (m_actions.selectBy) {
                m_actions.selectBy(10);
            }
        });
        return true;
    case Qt::Key_End:
        runPlaylistAction(m_actions.selectLast);
        return true;
    case Qt::Key_Home:
        if (m_actions.seekToStart) {
            m_actions.seekToStart();
        }
        return true;
    case Qt::Key_Left:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputSeekBackward);
        if (m_actions.seekBy) {
            m_actions.seekBy(-5'000);
        }
        return true;
    }
    case Qt::Key_Right:
        if (event->isAutoRepeat() && m_suppressRightAutoRepeat) {
            return true;
        }
        if (!event->isAutoRepeat()) {
            m_suppressRightAutoRepeat = false;
        }
        m_rightArrowHeld = true;
        {
            detail::ScopedPerformanceTimer actionTimer(
                detail::NeniaPerformanceDiagnostics::Timing::InputSeekForward);
            if (m_actions.seekBy) {
                m_actions.seekBy(5'000);
            }
            return true;
        }
    case Qt::Key_Return:
    case Qt::Key_Enter:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputEnterActivation);
        runPlaylistAction(m_actions.playSelected);
        return true;
    }
    case Qt::Key_D:
    case Qt::Key_Delete:
        runPlaylistAction(m_actions.removeSelected);
        return true;
    case Qt::Key_S:
        if (m_actions.stop) {
            m_actions.stop();
        }
        return true;
    case Qt::Key_Space:
    {
        detail::ScopedPerformanceTimer actionTimer(
            detail::NeniaPerformanceDiagnostics::Timing::InputPlayPause);
        if (m_actions.togglePlayPause) {
            m_actions.togglePlayPause();
        }
        return true;
    }
    case Qt::Key_R:
        if (m_actions.restart) {
            m_actions.restart();
        }
        return true;
    default:
        return false;
    }
}

bool NeniaGlobalInput::handleKeyRelease(const QKeyEvent *event)
{
    if (event == nullptr || event->type() != QEvent::KeyRelease
        || event->key() != Qt::Key_Right) {
        return false;
    }

    if (!event->isAutoRepeat()) {
        m_rightArrowHeld = false;
        m_suppressRightAutoRepeat = false;
    }
    return false;
}

void NeniaGlobalInput::suppressRightAutoRepeatUntilRelease() noexcept
{
    if (m_rightArrowHeld) {
        m_suppressRightAutoRepeat = true;
    }
}

void NeniaGlobalInput::cancelHeldKeySequence() noexcept
{
    m_rightArrowHeld = false;
    m_suppressRightAutoRepeat = false;
}

void NeniaGlobalInput::runPlaylistAction(const std::function<void()> &action) const
{
    if (m_actions.playlistInteraction) {
        m_actions.playlistInteraction();
    }
    if (action) {
        action();
    }
}

NeniaGlobalInputRouter::NeniaGlobalInputRouter(QMainWindow *window,
                                               NeniaGlobalInput *input,
                                               QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_input(input)
{
}

bool NeniaGlobalInputRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (m_input == nullptr || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ApplicationDeactivate
        || (watched == m_window && event->type() == QEvent::WindowDeactivate)
        || (watched == m_window && event->type() == QEvent::WindowBlocked)) {
        m_input->cancelHeldKeySequence();
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyRelease) {
        m_input->handleKeyRelease(static_cast<QKeyEvent *>(event));
        return QObject::eventFilter(watched, event);
    }
    if (event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }

    if (m_window == nullptr || !m_window->isActiveWindow()) {
        m_input->cancelHeldKeySequence();
        return QObject::eventFilter(watched, event);
    }

    QWidget *modal = QApplication::activeModalWidget();
    QWidget *popup = QApplication::activePopupWidget();
    QWidget *focus = QApplication::focusWidget();
    const bool textEntryFocused = focus != nullptr
        && (qobject_cast<QLineEdit *>(focus) != nullptr
            || qobject_cast<QTextEdit *>(focus) != nullptr
            || qobject_cast<QPlainTextEdit *>(focus) != nullptr
            || qobject_cast<QAbstractSpinBox *>(focus) != nullptr);
    if ((modal != nullptr && modal != m_window) || popup != nullptr || textEntryFocused) {
        m_input->cancelHeldKeySequence();
        return QObject::eventFilter(watched, event);
    }

    if (m_input->handleKey(static_cast<QKeyEvent *>(event))) {
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace nenia::app
