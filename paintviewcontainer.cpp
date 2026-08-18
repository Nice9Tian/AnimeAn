#include "paintviewcontainer.h"
#include "openglwidget.h"

#include <QGridLayout>
#include <QScrollBar>

#include <cmath>

PaintViewContainer::PaintViewContainer(QWidget *parent)
    : QWidget(parent)
{
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_paintWidget = new PaintOpenGLWidget(this);
    m_horizontalBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalBar = new QScrollBar(Qt::Vertical, this);

    layout->addWidget(m_paintWidget, 0, 0);
    layout->addWidget(m_verticalBar, 0, 1);
    layout->addWidget(m_horizontalBar, 1, 0);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(0, 1);

    connect(m_paintWidget, &PaintOpenGLWidget::viewTransformChanged,
            this, &PaintViewContainer::syncScrollBars);
    connect(m_horizontalBar, &QScrollBar::valueChanged, this, [this](int) {
        applyScrollBars();
    });
    connect(m_verticalBar, &QScrollBar::valueChanged, this, [this](int) {
        applyScrollBars();
    });

    syncScrollBars();
}

PaintOpenGLWidget *PaintViewContainer::paintWidget() const
{
    return m_paintWidget;
}

void PaintViewContainer::syncScrollBars()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;

    if (m_paintWidget->unboundedCanvas()) {
        // An infinite canvas has nothing meaningful to scroll against; wheel
        // zoom and middle-drag pan take over.
        m_horizontalBar->setVisible(false);
        m_verticalBar->setVisible(false);
        m_syncing = false;
        return;
    }
    m_horizontalBar->setVisible(true);
    m_verticalBar->setVisible(true);

    // Against the REACHABLE area, which the widget defines once for both of
    // us (page + whatever is drawn on this frame + slack). Deriving it here
    // from the page alone disagreed with the widget's own pan clamp and left
    // art drawn past the paper unscrollable. The range can start BELOW zero:
    // content sits to the left of and above the page as readily as inside it.
    const qreal zoomFactor = m_paintWidget->zoom();
    const QRectF reach = m_paintWidget->reachableRect();
    const int left = int(std::lround(reach.left() * zoomFactor));
    const int right = int(std::lround(reach.right() * zoomFactor));
    const int top = int(std::lround(reach.top() * zoomFactor));
    const int bottom = int(std::lround(reach.bottom() * zoomFactor));
    // The scroll value is -panOffset, so the bar spans the same interval the
    // clamp allows, expressed the other way round.
    const int hMin = std::min(left, right - m_paintWidget->width());
    const int hMax = std::max(left, right - m_paintWidget->width());
    const int vMin = std::min(top, bottom - m_paintWidget->height());
    const int vMax = std::max(top, bottom - m_paintWidget->height());
    const int hRange = hMax - hMin;
    const int vRange = vMax - vMin;

    m_horizontalBar->setRange(hMin, hMax);
    m_verticalBar->setRange(vMin, vMax);
    m_horizontalBar->setPageStep(m_paintWidget->width());
    m_verticalBar->setPageStep(m_paintWidget->height());
    m_horizontalBar->setValue(int(std::lround(-m_paintWidget->panOffset().x())));
    m_verticalBar->setValue(int(std::lround(-m_paintWidget->panOffset().y())));
    m_horizontalBar->setEnabled(hRange > 0);
    m_verticalBar->setEnabled(vRange > 0);

    m_syncing = false;
}

void PaintViewContainer::applyScrollBars()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    m_paintWidget->setScrollPosition(m_horizontalBar->value(), m_verticalBar->value());
    m_syncing = false;
}
