#include "forcepad.h"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace {
constexpr qreal kHandleRadius = 9.0;
constexpr qreal kMargin = 14.0;
constexpr qint64 kMoveThrottleMs = 16;
const QColor kAccent(61, 142, 201);
}

ForcePadPanel::ForcePadPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(160, 180);
    setCursor(Qt::PointingHandCursor);
}

QPointF ForcePadPanel::value() const
{
    return m_value;
}

void ForcePadPanel::setValue(const QPointF &value)
{
    // Programmatic set (e.g. the listener's baseline was invalidated and the
    // held vector no longer means anything): clamp into the disc, no signals.
    QPointF next = value;
    const qreal length = std::hypot(next.x(), next.y());
    if (length > 1.0) {
        next /= length;
    }
    if (next == m_value) {
        return;
    }
    m_value = next;
    update();
}

void ForcePadPanel::setSpringBack(bool springBack)
{
    m_springBack = springBack;
}

QRectF ForcePadPanel::padRect() const
{
    // Largest centered square, leaving room for the value text underneath.
    const qreal textBand = 20.0;
    const qreal side = qMin(width() - 2.0 * kMargin, height() - 2.0 * kMargin - textBand);
    const qreal left = (width() - side) / 2.0;
    const qreal top = (height() - textBand - side) / 2.0;
    return QRectF(left, top, side, side);
}

QPointF ForcePadPanel::normalizedFromWidget(const QPointF &widgetPos) const
{
    const QRectF rect = padRect();
    const qreal half = rect.width() / 2.0;
    if (half <= 0.0) {
        return QPointF();
    }
    qreal nx = (widgetPos.x() - rect.center().x()) / half;
    qreal ny = (widgetPos.y() - rect.center().y()) / half;
    const qreal length = std::hypot(nx, ny);
    if (length > 1.0) {
        nx /= length;
        ny /= length;
    }
    return QPointF(nx, ny);
}

QPointF ForcePadPanel::widgetFromNormalized(const QPointF &normalized) const
{
    const QRectF rect = padRect();
    const qreal half = rect.width() / 2.0;
    return QPointF(rect.center().x() + normalized.x() * half,
                   rect.center().y() + normalized.y() * half);
}

void ForcePadPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF rect = padRect();
    const QPointF center = rect.center();

    // Boundary circle + crosshair axes, all palette-driven so the pad follows
    // light/dark themes (never hardcode surface colors - see tooloptpanel).
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    painter.setBrush(palette().base());
    painter.drawEllipse(center, rect.width() / 2.0, rect.height() / 2.0);

    painter.drawLine(QPointF(rect.left(), center.y()), QPointF(rect.right(), center.y()));
    painter.drawLine(QPointF(center.x(), rect.top()), QPointF(center.x(), rect.bottom()));

    // Quarter tick marks on both axes.
    for (int step = -1; step <= 1; step += 2) {
        const qreal offset = step * rect.width() / 4.0;
        painter.drawLine(QPointF(center.x() + offset, center.y() - 3.0),
                         QPointF(center.x() + offset, center.y() + 3.0));
        painter.drawLine(QPointF(center.x() - 3.0, center.y() + offset),
                         QPointF(center.x() + 3.0, center.y() + offset));
    }

    // Deflection line + handle.
    const QPointF handle = widgetFromNormalized(m_value);
    if (!m_value.isNull()) {
        painter.setPen(QPen(kAccent, 1.5));
        painter.drawLine(center, handle);
    }
    painter.setPen(QPen(palette().color(QPalette::Text), 1.0));
    painter.setBrush(m_dragging ? kAccent : kAccent.lighter(130));
    painter.drawEllipse(handle, kHandleRadius, kHandleRadius);

    // Current value readout.
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRectF(0.0, rect.bottom() + 2.0, width(), 18.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("x %1   y %2")
                         .arg(m_value.x(), 0, 'f', 2)
                         .arg(m_value.y(), 0, 'f', 2));
}

void ForcePadPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_moveTimer.start();
    m_value = normalizedFromWidget(event->position());
    update();
    emit padPressed(m_value.x(), m_value.y());
}

void ForcePadPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    m_value = normalizedFromWidget(event->position());
    update();
    // Throttle the emission, not the visual: every move repaints the handle,
    // but each padMoved crosses into Python (GIL + hook dispatch), and >60/s
    // is pure waste for a preview.
    if (m_moveTimer.elapsed() >= kMoveThrottleMs) {
        m_moveTimer.restart();
        emit padMoved(m_value.x(), m_value.y());
    }
}

void ForcePadPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    const QPointF released = normalizedFromWidget(event->position());
    m_value = released;
    emit padReleased(released.x(), released.y());
    if (m_springBack) {
        m_value = QPointF();
    }
    update();
}

void ForcePadPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    // QWidget's default forwards double-clicks to mousePressEvent, which
    // would start a second gesture (and a second history commit) out of one
    // hesitant click. A spring-back pad has no double-click meaning: eat it.
    event->accept();
}

void ForcePadPanel::hideEvent(QHideEvent *event)
{
    // Closing/floating the dock mid-drag would otherwise strand the listener
    // in an open session (the matching release never arrives). A centered
    // release is the explicit cancel signal.
    if (m_dragging) {
        m_dragging = false;
        m_value = QPointF();
        emit padReleased(0.0, 0.0);
    }
    QWidget::hideEvent(event);
}
