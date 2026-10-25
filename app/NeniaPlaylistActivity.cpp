#include "NeniaPlaylistActivity.h"

#include <algorithm>
#include <utility>

namespace nenia::app {

NeniaPlaylistActivity::NeniaPlaylistActivity(QObject *parent, int highlightTimeoutMs)
    : QObject(parent)
    , m_timer()
    , m_highlightTimeoutMs(std::max(1, highlightTimeoutMs))
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!m_highlighted) {
            return;
        }
        m_highlighted = false;
        notifyChange();
    });
}

bool NeniaPlaylistActivity::isHighlighted() const noexcept
{
    return m_highlighted;
}

int NeniaPlaylistActivity::highlightTimeoutMs() const noexcept
{
    return m_highlightTimeoutMs;
}

void NeniaPlaylistActivity::setChangeHandler(std::function<void()> handler)
{
    m_changeHandler = std::move(handler);
}

void NeniaPlaylistActivity::markInteraction()
{
    m_timer.start(m_highlightTimeoutMs);
    if (m_highlighted) {
        return;
    }
    m_highlighted = true;
    notifyChange();
}

void NeniaPlaylistActivity::notifyChange()
{
    if (m_changeHandler) {
        m_changeHandler();
    }
}

} // namespace nenia::app
