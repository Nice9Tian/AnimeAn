#ifndef CHILDPAINTWINDOW_H
#define CHILDPAINTWINDOW_H

#include <QDockWidget>
#include <QVector>

class PaintOpenGLWidget;
class QHBoxLayout;
class QMenu;
class QMenuBar;
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
    // "Changable Texture": when OFF the board's artwork is protected and only
    // the guide tools may draw. Replaces the old "Changable Layer" checkbox,
    // which also governed whether the Layers/Assets panels follow this view -
    // that behaviour rides along with this flag now.
    bool changableTexture() const;
    void setChangableTimeline(bool enabled);
    void setChangableTexture(bool enabled);
    void setScriptButtons(const QVector<ScriptButtonDefinition> &definitions);
    // The menu bar and the Setting menu script menus are attached to
    // (MainWindow owns the Python side, so it builds them; this just hands
    // over the places they go).
    QMenuBar *menuBar() const;
    // Script menus become SUBMENUS of Setting rather than top-level menus:
    // everything that configures this board hangs off one entry.
    QMenu *settingMenu() const;

signals:
    void changableTimelineToggled(bool enabled);
    void changableTextureToggled(bool enabled);
    // For checkable buttons `on` is the new checked state; for plain buttons
    // it is always true (a press).
    void scriptButtonToggled(const QString &name, bool on);

private:
    void applyBackgroundMode(int mode);

    PaintOpenGLWidget *m_paintWidget = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QMenu *m_settingMenu = nullptr;
    QHBoxLayout *m_optionLayout = nullptr;
    QWidget *m_optionRow = nullptr;
    QVector<QPushButton *> m_scriptButtons;
    QAction *m_changableTimelineAction = nullptr;
    QAction *m_changableTextureAction = nullptr;
};

#endif // CHILDPAINTWINDOW_H
