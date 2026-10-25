#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace nenia::app {

class NeniaPlaylistActivity final : public QObject
{
public:
    static constexpr int kDefaultHighlightTimeoutMs = 3'000;

    explicit NeniaPlaylistActivity(QObject *parent = nullptr,
                                   int highlightTimeoutMs = kDefaultHighlightTimeoutMs);

    bool isHighlighted() const noexcept;
    int highlightTimeoutMs() const noexcept;
    void setChangeHandler(std::function<void()> handler);
    void markInteraction();

private:
    void notifyChange();

    QTimer m_timer;
    int m_highlightTimeoutMs = kDefaultHighlightTimeoutMs;
    bool m_highlighted = false;
    std::function<void()> m_changeHandler;
};

} // namespace nenia::app
