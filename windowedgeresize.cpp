#include "windowedgeresize.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace {
// The band INSIDE the window that resizes it: a frameless window has nothing
// outside its rect to grab. The top band is thinner because the top edge is
// also the title bar, which is what the user drags to move the window.
constexpr int kEdgeBand = 8;
constexpr int kTopBand = 5;
// Along an edge, this far from a corner also takes the adjacent edge, so a
// diagonal resize does not need an 8x8 target.
constexpr int kCornerReach = 16;

Qt::CursorShape cursorFor(Qt::Edges edges)
{
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge);
    const bool bottom = edges.testFlag(Qt::BottomEdge);
    if ((left && top) || (right && bottom)) {
        return Qt::SizeFDiagCursor;
    }
    if ((right && top) || (left && bottom)) {
        return Qt::SizeBDiagCursor;
    }
    return (left || right) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
}
}

void WindowEdgeResize::install(QWidget *widget)
{
    if (widget) {
        new WindowEdgeResize(widget);
    }
}

WindowEdgeResize::WindowEdgeResize(QWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
    widget->installEventFilter(this);
    attachWindow();
}

WindowEdgeResize::~WindowEdgeResize()
{
    // A window destroyed while the cursor sat on its edge must not leave the
    // resize arrow stuck on the whole application.
    setHoverEdges({});
}

void WindowEdgeResize::attachWindow()
{
    // Null for anything that is not a native top level, which is exactly when
    // there is nothing to resize.
    QWindow *window = m_widget->windowHandle();
    if (window == m_window) {
        return;
    }
    setHoverEdges({});
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
    }
}

bool WindowEdgeResize::active() const
{
    return m_widget->isWindow() && m_widget->isVisible()
           && m_widget->windowFlags().testFlag(Qt::FramelessWindowHint)
           && !(m_widget->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen));
}

Qt::Edges WindowEdgeResize::edgesAt(const QPointF &pos) const
{
    const qreal w = m_window->width();
    const qreal h = m_window->height();
    Qt::Edges edges;
    if (pos.x() < kEdgeBand) {
        edges |= Qt::LeftEdge;
    } else if (pos.x() >= w - kEdgeBand) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() < kTopBand) {
        edges |= Qt::TopEdge;
    } else if (pos.y() >= h - kEdgeBand) {
        edges |= Qt::BottomEdge;
    }
    if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        if (pos.y() < kCornerReach) {
            edges |= Qt::TopEdge;
        } else if (pos.y() >= h - kCornerReach) {
            edges |= Qt::BottomEdge;
        }
    }
    if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        if (pos.x() < kCornerReach) {
            edges |= Qt::LeftEdge;
        } else if (pos.x() >= w - kCornerReach) {
            edges |= Qt::RightEdge;
        }
    }
    // An axis the window cannot grow or shrink along is not an edge.
    const QSize minimum = m_widget->minimumSize();
    const QSize maximum = m_widget->maximumSize();
    if (minimum.width() == maximum.width()) {
        edges &= ~(Qt::LeftEdge | Qt::RightEdge);
    }
    if (minimum.height() == maximum.height()) {
        edges &= ~(Qt::TopEdge | Qt::BottomEdge);
    }
    return edges;
}

void WindowEdgeResize::setHoverEdges(Qt::Edges edges)
{
    if (edges == m_hoverEdges) {
        return;
    }
    m_hoverEdges = edges;
    // An OVERRIDE cursor rather than the window's: child widgets re-apply
    // their own cursors as the mouse crosses them, which would flicker the
    // arrow away on every move along an edge.
    if (!edges) {
        if (m_cursorPushed) {
            QGuiApplication::restoreOverrideCursor();
            m_cursorPushed = false;
        }
        return;
    }
    if (m_cursorPushed) {
        QGuiApplication::changeOverrideCursor(QCursor(cursorFor(edges)));
    } else {
        QGuiApplication::setOverrideCursor(QCursor(cursorFor(edges)));
        m_cursorPushed = true;
    }
}

bool WindowEdgeResize::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_widget) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::WinIdChange:
        case QEvent::ParentChange:
            attachWindow();
            break;
        case QEvent::Hide:
            setHoverEdges({});
            break;
        default:
            break;
        }
        return false;
    }
    if (!m_window || watched != m_window) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->buttons() != Qt::NoButton) {
            // Something inside is being dragged; its cursor is its business.
            return false;
        }
        setHoverEdges(active() ? edgesAt(mouse->position()) : Qt::Edges());
        return false;
    }
    case QEvent::MouseButtonPress: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !active()) {
            return false;
        }
        const Qt::Edges edges = edgesAt(mouse->position());
        if (!edges) {
            return false;
        }
        // Handed to the window system: it tracks the drag, clamps to the
        // minimum size and draws its own cursor. A press it refuses goes on to
        // whatever is under the cursor as usual.
        if (m_window->startSystemResize(edges)) {
            setHoverEdges({});
            return true;
        }
        return false;
    }
    case QEvent::Leave:
        setHoverEdges({});
        return false;
    default:
        return false;
    }
}
