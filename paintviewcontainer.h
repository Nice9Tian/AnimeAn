#ifndef PAINTVIEWCONTAINER_H
#define PAINTVIEWCONTAINER_H

#include <QWidget>

class PaintOpenGLWidget;
class QScrollBar;

// Hosts a PaintOpenGLWidget together with horizontal/vertical scrollbars that
// mirror its zoom/pan view transform.
class PaintViewContainer : public QWidget
{
    Q_OBJECT

public:
    explicit PaintViewContainer(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;

private:
    void syncScrollBars();
    void applyScrollBars();

    PaintOpenGLWidget *m_paintWidget = nullptr;
    QScrollBar *m_horizontalBar = nullptr;
    QScrollBar *m_verticalBar = nullptr;
    bool m_syncing = false;
};

#endif // PAINTVIEWCONTAINER_H
