#include "childpaintwindow.h"
#include "openglwidget.h"
#include "paintviewcontainer.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

ChildPaintWindow::ChildPaintWindow(QWidget *parent)
    : QDockWidget(QStringLiteral("Child Paint View"), parent)
{
    setObjectName(QStringLiteral("ChildPaintViewDock"));

    QWidget *panel = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // The texture board carries its own menu bar. Its settings are about THIS
    // board, and putting them on the application menu bar would make them
    // read as global.
    m_menuBar = new QMenuBar(panel);

    QMenu *settingMenu = m_menuBar->addMenu(QStringLiteral("Setting"));
    // Qt hides action tool tips in menus by default, so the explanations
    // below were written and then never shown.
    settingMenu->setToolTipsVisible(true);
    m_changableTimelineAction = settingMenu->addAction(QStringLiteral("Changable Timeline"));
    m_changableTimelineAction->setCheckable(true);
    m_changableTimelineAction->setChecked(false);
    m_changableTimelineAction->setToolTip(
        QStringLiteral("Focusing this view switches the Frames panel to its timeline."));
    connect(m_changableTimelineAction, &QAction::toggled,
            this, &ChildPaintWindow::changableTimelineToggled);

    m_changableTextureAction = settingMenu->addAction(QStringLiteral("Changable Texture"));
    m_changableTextureAction->setCheckable(true);
    m_changableTextureAction->setChecked(true);
    m_changableTextureAction->setToolTip(
        QStringLiteral("Unchecked, the texture's own artwork is protected: only the "
                       "H/V axis tools can draw here, so a stray stroke cannot damage "
                       "the reference. Also governs whether the Layers/Assets panels "
                       "follow this view."));

    PaintViewContainer *container = new PaintViewContainer(panel);
    m_paintWidget = container->paintWidget();
    m_paintWidget->setViewName(QStringLiteral("child"));
    // The reference/texture board is an infinite canvas: the mapped pattern
    // is scaled by the center lines anyway, so no page boundary applies.
    m_paintWidget->setUnboundedCanvas(true);
    container->setMinimumSize(320, 240);

    QMenu *backgroundMenu = m_menuBar->addMenu(QStringLiteral("Background"));
    QActionGroup *backgroundGroup = new QActionGroup(this);
    backgroundGroup->setExclusive(true);
    struct BackgroundEntry { const char *title; int mode; };
    const BackgroundEntry entries[] = {
        {"White", int(PaintOpenGLWidget::BackgroundMode::White)},
        {"Black", int(PaintOpenGLWidget::BackgroundMode::Black)},
        {"Transparent", int(PaintOpenGLWidget::BackgroundMode::Transparent)},
    };
    for (const BackgroundEntry &entry : entries) {
        QAction *action = backgroundMenu->addAction(QString::fromUtf8(entry.title));
        action->setCheckable(true);
        action->setActionGroup(backgroundGroup);
        action->setChecked(entry.mode == int(m_paintWidget->backgroundMode()));
        const int mode = entry.mode;
        connect(action, &QAction::triggered, this, [this, mode]() { applyBackgroundMode(mode); });
    }

    // Kept for script buttons that ask for a button rather than a menu entry.
    // Hidden while empty so it costs no height.
    m_optionRow = new QWidget(panel);
    QHBoxLayout *optionLayout = new QHBoxLayout(m_optionRow);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(12);
    optionLayout->addStretch(1);
    m_optionLayout = optionLayout;
    m_optionRow->setVisible(false);

    layout->setMenuBar(m_menuBar);
    layout->addWidget(m_optionRow);
    layout->addWidget(container, 1);
    setWidget(panel);

    connect(m_changableTextureAction, &QAction::toggled, this, [this](bool on) {
        m_paintWidget->setContentEditable(on);
        emit changableTextureToggled(on);
    });
    m_paintWidget->setContentEditable(true);
}

QMenuBar *ChildPaintWindow::menuBar() const
{
    return m_menuBar;
}

void ChildPaintWindow::applyBackgroundMode(int mode)
{
    m_paintWidget->setBackgroundMode(static_cast<PaintOpenGLWidget::BackgroundMode>(mode));
}

PaintOpenGLWidget *ChildPaintWindow::paintWidget() const
{
    return m_paintWidget;
}

bool ChildPaintWindow::changableTimeline() const
{
    return m_changableTimelineAction->isChecked();
}

bool ChildPaintWindow::changableTexture() const
{
    return m_changableTextureAction->isChecked();
}

void ChildPaintWindow::setChangableTimeline(bool enabled)
{
    m_changableTimelineAction->setChecked(enabled);
}

void ChildPaintWindow::setChangableTexture(bool enabled)
{
    m_changableTextureAction->setChecked(enabled);
}

void ChildPaintWindow::setScriptButtons(const QVector<ScriptButtonDefinition> &definitions)
{
    for (QPushButton *button : m_scriptButtons) {
        m_optionLayout->removeWidget(button);
        button->deleteLater();
    }
    m_scriptButtons.clear();

    for (const ScriptButtonDefinition &definition : definitions) {
        if (definition.name.isEmpty()) {
            continue;
        }
        QPushButton *button = new QPushButton(
            definition.title.isEmpty() ? definition.name : definition.title, m_optionRow);
        button->setToolTip(definition.tooltip);
        button->setCheckable(definition.checkable);
        const QString name = definition.name;
        if (definition.checkable) {
            connect(button, &QPushButton::toggled, this, [this, name](bool on) {
                emit scriptButtonToggled(name, on);
            });
        } else {
            connect(button, &QPushButton::clicked, this, [this, name]() {
                emit scriptButtonToggled(name, true);
            });
        }
        // Keep the stretch item last so the row stays left-aligned.
        m_optionLayout->insertWidget(m_optionLayout->count() - 1, button);
        m_scriptButtons.append(button);
    }
    m_optionRow->setVisible(!m_scriptButtons.isEmpty());
}
