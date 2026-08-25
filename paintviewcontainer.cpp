#include "paintviewcontainer.h"
#include "openglwidget.h"

#include <QGridLayout>
#include <QScrollBar>
#include <QVBoxLayout>

#include <cmath>

PaintViewContainer::PaintViewContainer(QWidget *parent)
    : QWidget(parent)
{
    // The last link of the chain that lets a board fill the height a panel row
    // offers it (SubControlFrame -> TexturePanel -> here): a canvas takes the
    // room it is given.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Two nested boxes rather than one grid: the chrome band has to span
    // EVERYTHING (canvas plus scroll bars), and expressing that in the same
    // grid as the scroll bars meant every slot had to know the others' spans.
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_outerLayout = outer;

    m_canvasArea = new QWidget(this);
    QGridLayout *layout = new QGridLayout(m_canvasArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_paintWidget = new PaintOpenGLWidget(m_canvasArea);
    m_horizontalBar = new QScrollBar(Qt::Horizontal, m_canvasArea);
    m_verticalBar = new QScrollBar(Qt::Vertical, m_canvasArea);

    layout->addWidget(m_paintWidget, 0, 0);
    layout->addWidget(m_verticalBar, 0, 1);
    layout->addWidget(m_horizontalBar, 1, 0);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(0, 1);

    outer->addWidget(m_canvasArea, 1);

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

QWidget *PaintViewContainer::canvasArea() const
{
    return m_canvasArea;
}

void PaintViewContainer::setBottomChrome(QWidget *widget)
{
    if (m_bottomChrome == widget) {
        return;
    }
    if (m_bottomChrome) {
        // Released from the layout, NOT reparented: the caller is taking it
        // somewhere, and dropping it to a null parent here would flash it as a
        // top-level window on the way.
        m_outerLayout->removeWidget(m_bottomChrome);
    }
    m_bottomChrome = widget;
    if (widget) {
        m_outerLayout->addWidget(widget);
    }
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
