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

    // Against the PAGE, not the widget: with the canvas a document property
    // the two are unrelated, and scrolling a widget-sized "document" against
    // itself could only ever produce an empty range.
    const qreal zoomFactor = m_paintWidget->zoom();
    const QRectF page = m_paintWidget->documentRect();
    const int docWidth = int(std::lround(page.width() * zoomFactor));
    const int docHeight = int(std::lround(page.height() * zoomFactor));
    const int hRange = std::max(0, docWidth - m_paintWidget->width());
    const int vRange = std::max(0, docHeight - m_paintWidget->height());

    m_horizontalBar->setRange(0, hRange);
    m_verticalBar->setRange(0, vRange);
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
