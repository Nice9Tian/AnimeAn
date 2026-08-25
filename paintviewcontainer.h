#ifndef PAINTVIEWCONTAINER_H
#define PAINTVIEWCONTAINER_H

#include <QWidget>

class PaintOpenGLWidget;
class QBoxLayout;
class QScrollBar;

// Hosts a PaintOpenGLWidget together with horizontal/vertical scrollbars that
// mirror its zoom/pan view transform, plus one optional band of chrome under
// the whole thing. The container only RESERVES that space - what goes in it is
// the timeline's business, and the timeline only uses it while its strip is
// docked beside the canvas (where the transport has nowhere else to go).
class PaintViewContainer : public QWidget
{
    Q_OBJECT

public:
    explicit PaintViewContainer(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;
    // The canvas and its scroll bars, without the timeline chrome: what a
    // floating overlay (the reopen pill) measures itself against.
    QWidget *canvasArea() const;

    // A null widget empties the band. The previous occupant is only released
    // from the layout: reparenting it is the caller's business, since the
    // caller is where it goes next.
    void setBottomChrome(QWidget *widget);

private:
    void syncScrollBars();
    void applyScrollBars();

    PaintOpenGLWidget *m_paintWidget = nullptr;
    QScrollBar *m_horizontalBar = nullptr;
    QScrollBar *m_verticalBar = nullptr;
    QWidget *m_canvasArea = nullptr;
    QBoxLayout *m_outerLayout = nullptr;
    QWidget *m_bottomChrome = nullptr;
    bool m_syncing = false;
};

#endif // PAINTVIEWCONTAINER_H
