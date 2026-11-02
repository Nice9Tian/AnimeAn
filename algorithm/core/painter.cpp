#include "algorithm/core/painter.h"

#include <algorithm>

namespace AnimeCore {

Vector<double> Pen::dashPattern() const
{
    Vector<double> dashes;
    switch (m_style) {
    case DashLine:
        dashes << 4.0 << 2.0;
        break;
    case DotLine:
        dashes << 1.0 << 2.0;
        break;
    case DashDotLine:
        dashes << 4.0 << 2.0 << 1.0 << 2.0;
        break;
    case DashDotDotLine:
        dashes << 4.0 << 2.0 << 1.0 << 2.0 << 1.0 << 2.0;
        break;
    default:
        break;
    }
    return dashes;
}

Painter::Painter(PaintDevice *device)
    : m_device(device)
{
    m_ratio = m_device ? m_device->paintDevicePixelRatio() : 1.0;
    updateTransform();
}

Painter::~Painter() = default;

void Painter::end()
{
    m_device = nullptr;
}

void Painter::save()
{
    m_stateStack.append(m_state);
    m_worldStack.append(m_world);
}

void Painter::restore()
{
    if (m_stateStack.isEmpty()) {
        return;
    }
    m_state = m_stateStack.takeLast();
    m_world = m_worldStack.takeLast();
}

void Painter::setRenderHint(RenderHint hint, bool on)
{
    if (hint == Antialiasing) {
        m_state.antialiasing = on;
    } else if (hint == SmoothPixmapTransform) {
        m_state.smoothImages = on;
    }
}

void Painter::setPen(const Pen &pen)
{
    m_state.pen = pen;
}

void Painter::setPen(const Color &color)
{
    m_state.pen = Pen(color);
}

void Painter::setPen(PenStyle style)
{
    m_state.pen = Pen(style);
}

void Painter::setBrush(const Brush &brush)
{
    m_state.brush = brush;
}

void Painter::setBrush(const Color &color)
{
    m_state.brush = Brush(color);
}

void Painter::setBrush(BrushStyle style)
{
    m_state.brush = Brush(style);
}

void Painter::setOpacity(double opacity)
{
    m_state.opacity = std::min(1.0, std::max(0.0, opacity));
}

void Painter::setCompositionMode(CompositionMode mode)
{
    m_state.compositionMode = mode;
}

void Painter::translate(double dx, double dy)
{
    m_world.translate(dx, dy);
    updateTransform();
}

void Painter::scale(double sx, double sy)
{
    m_world.scale(sx, sy);
    updateTransform();
}

void Painter::setWorldTransform(const Transform &transform)
{
    m_world = transform;
    updateTransform();
}

void Painter::updateTransform()
{
    m_state.transform = m_world;
    if (m_ratio != 1.0) {
        m_state.transform *= Transform::fromScale(m_ratio, m_ratio);
    }
}

void Painter::fillRect(const RectF &rect, const Color &color)
{
    const Pen oldPen = m_state.pen;
    const Brush oldBrush = m_state.brush;
    m_state.pen = Pen(NoPen);
    m_state.brush = Brush(color);
    drawRect(rect);
    m_state.brush = oldBrush;
    m_state.pen = oldPen;
}

void Painter::fillRect(const Rect &rect, const Color &color)
{
    fillRect(RectF(rect), color);
}

void Painter::drawRect(const RectF &rect)
{
    if (m_device) {
        m_device->drawRects(m_state, &rect, 1);
    }
}

void Painter::drawRect(const Rect &rect)
{
    drawRect(RectF(rect));
}

void Painter::drawLine(const LineF &line)
{
    if (m_device) {
        m_device->drawLines(m_state, &line, 1);
    }
}

void Painter::drawEllipse(const RectF &rect)
{
    if (m_device) {
        m_device->drawEllipse(m_state, rect.normalized());
    }
}

void Painter::drawEllipse(const PointF &center, double rx, double ry)
{
    drawEllipse(RectF(center.x() - rx, center.y() - ry, 2 * rx, 2 * ry));
}

void Painter::drawPoint(const PointF &point)
{
    if (m_device) {
        m_device->drawPoints(m_state, &point, 1);
    }
}

void Painter::drawPolygon(const PointF *points, int count, FillRule rule)
{
    if (m_device && points && count >= 2) {
        m_device->drawPolygon(m_state, points, count,
                              rule == WindingFill ? PolygonMode::Winding : PolygonMode::OddEven);
    }
}

void Painter::drawPolygon(const Vector<PointF> &points, FillRule rule)
{
    drawPolygon(points.constData(), points.size(), rule);
}

void Painter::drawPolyline(const PointF *points, int count)
{
    if (m_device && points && count >= 2) {
        m_device->drawPolygon(m_state, points, count, PolygonMode::Polyline);
    }
}

void Painter::drawRoundedRect(const RectF &rect, double xRadius, double yRadius)
{
    if (xRadius <= 0.0 || yRadius <= 0.0) {
        drawRect(rect);
        return;
    }
    Path path;
    path.addRoundedRect(rect, xRadius, yRadius);
    drawPath(path);
}

void Painter::drawPath(const Path &path)
{
    if (m_device) {
        m_device->drawPath(m_state, path);
    }
}

void Painter::drawImage(const PointF &topLeft, const Image &image)
{
    if (!m_device || image.isNull()) {
        return;
    }
    const double ratio = image.devicePixelRatio();
    const RectF target(topLeft.x(), topLeft.y(), image.width() / ratio, image.height() / ratio);
    m_device->drawImage(m_state, target, image, RectF(0.0, 0.0, image.width(), image.height()));
}

} // namespace AnimeCore
