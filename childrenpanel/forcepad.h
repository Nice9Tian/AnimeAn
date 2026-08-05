#ifndef FORCEPAD_H
#define FORCEPAD_H

#include <QElapsedTimer>
#include <QPointF>
#include <QWidget>

// Generic 2D vector input pad: a crosshair coordinate frame with a circular
// draggable handle. Reports a position inside the unit disc (-1..1 on both
// axes) while dragging and LATCHES on release - the handle holds the set
// vector until the user moves it again or a caller resets it via setValue
// (optionally it can spring back instead). What the vector MEANS (e.g.
// repulsion strength per axis) is decided by whoever listens - this widget
// carries no tool semantics.
class ForcePadPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ForcePadPanel(QWidget *parent = nullptr);

    QPointF value() const;
    void setValue(const QPointF &value);
    void setSpringBack(bool springBack);

signals:
    void padPressed(double x, double y);
    void padMoved(double x, double y);
    void padReleased(double x, double y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QRectF padRect() const;
    QPointF normalizedFromWidget(const QPointF &widgetPos) const;
    QPointF widgetFromNormalized(const QPointF &normalized) const;

    QPointF m_value;
    bool m_dragging = false;
    bool m_springBack = false;
    QElapsedTimer m_moveTimer;
};

#endif // FORCEPAD_H
