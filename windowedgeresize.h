#ifndef WINDOWEDGERESIZE_H
#define WINDOWEDGERESIZE_H

#include <QObject>
#include <QPointer>

class QPointF;
class QWidget;
class QWindow;

// Resize edges for a FRAMELESS top level. Such a window has no native border
// to grab, and what Qt leaves it (a floating dock's 1px frame, a size grip in
// one corner) is too thin to find. This watches the top level's QWindow - which
// sees every mouse move whatever child is under it - and turns a press in a
// band just inside the edge into a system resize.
//
// Pure mechanism, installed once for a widget's whole life: it is inert while
// the widget is embedded, docked or wears a native frame, and re-attaches
// whenever the widget gets a new native window (floating again after docking).
class WindowEdgeResize : public QObject
{
public:
    // Owned by `widget`.
    static void install(QWidget *widget);
    ~WindowEdgeResize() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit WindowEdgeResize(QWidget *widget);

    void attachWindow();
    bool active() const;
    Qt::Edges edgesAt(const QPointF &windowPos) const;
    void setHoverEdges(Qt::Edges edges);

    QWidget *m_widget = nullptr;
    QPointer<QWindow> m_window;
    Qt::Edges m_hoverEdges;
    // Whether the override cursor on the stack is ours to pop.
    bool m_cursorPushed = false;
};

#endif // WINDOWEDGERESIZE_H
