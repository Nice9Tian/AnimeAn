#ifndef CHILDPAINTWINDOW_H
#define CHILDPAINTWINDOW_H

#include <QDockWidget>
#include <QVector>

class PaintOpenGLWidget;
class QCheckBox;
class QHBoxLayout;
class QPushButton;

class ChildPaintWindow : public QDockWidget
{
    Q_OBJECT

public:
    // Generic script-defined button (per the C++-generic/Python-specific
    // rule): C++ renders a named button and reports presses; what the button
    // MEANS lives entirely in Python (python_hooks.register_view_button).
    struct ScriptButtonDefinition {
        QString name;
        QString title;
        QString tooltip;
        bool checkable = true;
    };

    explicit ChildPaintWindow(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;
    bool changableTimeline() const;
    bool changableLayer() const;
    void setChangableTimeline(bool enabled);
    void setChangableLayer(bool enabled);
    void setScriptButtons(const QVector<ScriptButtonDefinition> &definitions);

signals:
    void changableTimelineToggled(bool enabled);
    void changableLayerToggled(bool enabled);
    // For checkable buttons `on` is the new checked state; for plain buttons
    // it is always true (a press).
    void scriptButtonToggled(const QString &name, bool on);

private:
    PaintOpenGLWidget *m_paintWidget = nullptr;
    QCheckBox *m_changableTimelineCheck = nullptr;
    QCheckBox *m_changableLayerCheck = nullptr;
    QHBoxLayout *m_optionLayout = nullptr;
    QVector<QPushButton *> m_scriptButtons;
};

#endif // CHILDPAINTWINDOW_H
