#ifndef TEXTUREPANEL_H
#define TEXTUREPANEL_H

#include <QVector>
#include <QWidget>

class PaintOpenGLWidget;
class PaintViewContainer;
class QHBoxLayout;
class QMenu;
class QMenuBar;
class QPushButton;

// The texture board and everything that configures it, as ONE reparentable
// widget: its own menu bar (File from the shell, Setting with the Changable
// flags, Background and the script submenus), the script-button option row and
// the board's container.
//
// It used to be a QDockWidget of its own. It is a plain QWidget now because it
// has three possible homes - the central area's Texture page, a SubControlFrame
// in a panel, or the frame floating - and a dock can only ever be one of them.
// Nothing here knows which home it is in; the shell moves it.
class TexturePanel : public QWidget
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

    explicit TexturePanel(QWidget *parent = nullptr);

    PaintOpenGLWidget *paintWidget() const;
    PaintViewContainer *container() const;
    bool changableTimeline() const;
    // "Changable Texture": when OFF the board's artwork is protected and only
    // the guide tools may draw. Replaces the old "Changable Layer" checkbox,
    // which also governed whether the Layers/Assets panels follow this view -
    // that behaviour rides along with this flag now.
    bool changableTexture() const;
    void setChangableTimeline(bool enabled);
    void setChangableTexture(bool enabled);
    void setScriptButtons(const QVector<ScriptButtonDefinition> &definitions);
    // A board inside a tool-options row cannot ask for the full-size minimum:
    // it would hold the whole right column open. Compact drops the floor to
    // something a sub-control can honour; the container scales either way.
    void setCompact(bool compact);
    // The menu bar and the Setting menu script menus are attached to
    // (MainWindow owns the Python side, so it builds them; this just hands
    // over the places they go). Both travel WITH this widget, so a move
    // between homes needs no re-attach.
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
    PaintViewContainer *m_container = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QMenu *m_settingMenu = nullptr;
    QHBoxLayout *m_optionLayout = nullptr;
    QWidget *m_optionRow = nullptr;
    QVector<QPushButton *> m_scriptButtons;
    QAction *m_changableTimelineAction = nullptr;
    QAction *m_changableTextureAction = nullptr;
};

#endif // TEXTUREPANEL_H
