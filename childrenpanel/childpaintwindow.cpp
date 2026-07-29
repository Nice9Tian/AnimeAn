#include "childpaintwindow.h"
#include "openglwidget.h"
#include "paintviewcontainer.h"

#include <QCheckBox>
#include <QHBoxLayout>
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

    QHBoxLayout *optionLayout = new QHBoxLayout;
    optionLayout->setSpacing(12);
    m_changableTimelineCheck = new QCheckBox(QStringLiteral("Changable Timeline"), panel);
    m_changableTimelineCheck->setToolTip(
        QStringLiteral("When checked, focusing this view switches the Frames panel to this view's timeline."));
    m_changableTimelineCheck->setChecked(false);
    m_changableLayerCheck = new QCheckBox(QStringLiteral("Changable Layer"), panel);
    m_changableLayerCheck->setToolTip(
        QStringLiteral("When checked, focusing this view switches the Layers/Assets panels to this view's layers."));
    m_changableLayerCheck->setChecked(true);
    optionLayout->addWidget(m_changableTimelineCheck);
    optionLayout->addWidget(m_changableLayerCheck);
    optionLayout->addStretch(1);

    PaintViewContainer *container = new PaintViewContainer(panel);
    m_paintWidget = container->paintWidget();
    m_paintWidget->setViewName(QStringLiteral("child"));
    // The reference/texture board is an infinite canvas: the mapped pattern
    // is scaled by the center lines anyway, so no page boundary applies.
    m_paintWidget->setUnboundedCanvas(true);
    container->setMinimumSize(320, 240);

    layout->addLayout(optionLayout);
    layout->addWidget(container, 1);
    setWidget(panel);

    connect(m_changableTimelineCheck, &QCheckBox::toggled, this, &ChildPaintWindow::changableTimelineToggled);
    connect(m_changableLayerCheck, &QCheckBox::toggled, this, &ChildPaintWindow::changableLayerToggled);
}

PaintOpenGLWidget *ChildPaintWindow::paintWidget() const
{
    return m_paintWidget;
}

bool ChildPaintWindow::changableTimeline() const
{
    return m_changableTimelineCheck->isChecked();
}

bool ChildPaintWindow::changableLayer() const
{
    return m_changableLayerCheck->isChecked();
}

void ChildPaintWindow::setChangableTimeline(bool enabled)
{
    m_changableTimelineCheck->setChecked(enabled);
}

void ChildPaintWindow::setChangableLayer(bool enabled)
{
    m_changableLayerCheck->setChecked(enabled);
}
