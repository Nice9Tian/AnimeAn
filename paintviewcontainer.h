#ifndef PAINTVIEWCONTAINER_H
#define PAINTVIEWCONTAINER_H

#include <QWidget>

class PaintOpenGLWidget;
class QBoxLayout;
class QScrollBar;

// Hosts a PaintOpenGLWidget together with horizontal/vertical scrollbars that
// mirror its zoom/pan view transform, plus optional timeline chrome: a bar
// under the whole thing and a strip down one side. The container only
// RESERVES that space - what goes in it is the timeline's business.
class PaintViewContainer : public QWidget
{
    Q_OBJECT

public:
    explicit PaintViewContainer(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;
    // The canvas and its scroll bars, without the timeline chrome: what a
    // floating overlay (the reopen pill) measures itself against.
    QWidget *canvasArea() const;

    void setBottomChrome(QWidget *widget);
    // `edge` is Qt::LeftEdge or Qt::RightEdge; a null widget clears the slot.
    void setSideChrome(QWidget *widget, Qt::Edge edge);

private:
    void syncScrollBars();
    void applyScrollBars();

    PaintOpenGLWidget *m_paintWidget = nullptr;
    QScrollBar *m_horizontalBar = nullptr;
    QScrollBar *m_verticalBar = nullptr;
    QWidget *m_canvasArea = nullptr;
    QWidget *m_viewRow = nullptr;
    QBoxLayout *m_outerLayout = nullptr;
    QBoxLayout *m_rowLayout = nullptr;
    QWidget *m_bottomChrome = nullptr;
    QWidget *m_sideChrome = nullptr;
    bool m_syncing = false;
};

#endif // PAINTVIEWCONTAINER_H
