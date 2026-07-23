#ifndef CHILDPAINTWINDOW_H
#define CHILDPAINTWINDOW_H

#include <QDockWidget>

class PaintOpenGLWidget;
class QCheckBox;

class ChildPaintWindow : public QDockWidget
{
    Q_OBJECT

public:
    explicit ChildPaintWindow(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;
    bool changableTimeline() const;
    bool changableLayer() const;
    void setChangableTimeline(bool enabled);
    void setChangableLayer(bool enabled);

signals:
    void changableTimelineToggled(bool enabled);
    void changableLayerToggled(bool enabled);

private:
    PaintOpenGLWidget *m_paintWidget = nullptr;
    QCheckBox *m_changableTimelineCheck = nullptr;
    QCheckBox *m_changableLayerCheck = nullptr;
};

#endif // CHILDPAINTWINDOW_H
