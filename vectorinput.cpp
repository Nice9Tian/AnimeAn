#include "paintopenglwidget.h"

#include <QLineF>
#include <QPainter>

#include <cmath>

namespace {
constexpr qreal kEpsilon = 0.0001;

qreal distanceSquared(const QPointF &a, const QPointF &b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

qreal clamp01(qreal value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }
    return value;
}

int intMin(int a, int b)
{
    return a < b ? a : b;
}

int intMax(int a, int b)
{
    return a > b ? a : b;
}

qreal realMax(qreal a, qreal b)
{
    return a > b ? a : b;
}

qreal realMin(qreal a, qreal b)
{
    return a < b ? a : b;
}

qreal distancePointToSegmentSquared(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon) {
        return distanceSquared(point, a);
    }

    const QPointF ap = point - a;
    const qreal t = clamp01((ap.x() * ab.x() + ap.y() * ab.y()) / len2);
    const QPointF closest = a + ab * t;
    return distanceSquared(point, closest);
}

qreal crossProduct(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF ab = b - a;
    const QPointF ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

bool segmentsIntersect(const QPointF &a0, const QPointF &a1, const QPointF &b0, const QPointF &b1)
{
    const qreal aLeft = realMin(a0.x(), a1.x());
    const qreal aRight = realMax(a0.x(), a1.x());
    const qreal aTop = realMin(a0.y(), a1.y());
    const qreal aBottom = realMax(a0.y(), a1.y());
    const qreal bLeft = realMin(b0.x(), b1.x());
    const qreal bRight = realMax(b0.x(), b1.x());
    const qreal bTop = realMin(b0.y(), b1.y());
    const qreal bBottom = realMax(b0.y(), b1.y());
    if (aRight < bLeft || bRight < aLeft || aBottom < bTop || bBottom < aTop) {
        return false;
    }

    const qreal c1 = crossProduct(a0, a1, b0);
    const qreal c2 = crossProduct(a0, a1, b1);
    const qreal c3 = crossProduct(b0, b1, a0);
    const qreal c4 = crossProduct(b0, b1, a1);
    return ((c1 <= 0 && c2 >= 0) || (c1 >= 0 && c2 <= 0)) &&
           ((c3 <= 0 && c4 >= 0) || (c3 >= 0 && c4 <= 0));
}

qreal segmentSegmentDistanceSquared(const QPointF &a0, const QPointF &a1, const QPointF &b0, const QPointF &b1)
{
    if (segmentsIntersect(a0, a1, b0, b1)) {
        return 0;
    }

    const qreal d0 = distancePointToSegmentSquared(a0, b0, b1);
    const qreal d1 = distancePointToSegmentSquared(a1, b0, b1);
    const qreal d2 = distancePointToSegmentSquared(b0, a0, a1);
    const qreal d3 = distancePointToSegmentSquared(b1, a0, a1);
    return realMin(realMin(d0, d1), realMin(d2, d3));
}

QVector<QPointF> densifySegment(const QPointF &from, const QPointF &to, qreal step)
{
    QVector<QPointF> points;
    const qreal length = QLineF(from, to).length();
    const int rawCount = intMax(1, static_cast<int>(std::ceil(length / realMax(step, qreal(1)))));
    const int count = intMin(rawCount, 64);
    points.reserve(count);
    for (int i = 1; i <= count; ++i) {
        const qreal t = static_cast<qreal>(i) / count;
        points.append(from + (to - from) * t);
    }
    return points;
}
}

const QVector<PaintOpenGLWidget::VectorStroke> &PaintOpenGLWidget::VectorImageModel::strokes() const
{
    return m_strokes;
}

int PaintOpenGLWidget::VectorImageModel::strokeCount() const
{
    return m_strokes.size();
}

const PaintOpenGLWidget::VectorStroke &PaintOpenGLWidget::VectorImageModel::strokeAt(int index) const
{
    return m_strokes[index];
}

QRectF PaintOpenGLWidget::VectorImageModel::bounds() const
{
    return m_bounds;
}

void PaintOpenGLWidget::VectorImageModel::addStroke(const VectorStroke &stroke)
{
    m_strokes.append(stroke);
    if (m_bounds.isNull()) {
        m_bounds = stroke.bounds;
    } else {
        m_bounds = m_bounds.united(stroke.bounds);
    }
}

void PaintOpenGLWidget::VectorImageModel::removeStrokeAt(int index)
{
    if (index < 0 || index >= m_strokes.size()) {
        return;
    }

    m_strokes.removeAt(index);
    rebuildBounds();
}

void PaintOpenGLWidget::VectorImageModel::replaceStrokeWithPieces(int index, const QVector<VectorStroke> &pieces)
{
    if (index < 0 || index >= m_strokes.size()) {
        return;
    }

    m_strokes.removeAt(index);
    for (int i = pieces.size() - 1; i >= 0; --i) {
        if (pieces[i].points.size() >= 2 && pieces[i].totalLength > kEpsilon) {
            m_strokes.insert(index, pieces[i]);
        }
    }
    rebuildBounds();
}

void PaintOpenGLWidget::VectorImageModel::clear()
{
    m_strokes.clear();
    m_bounds = QRectF();
}

void PaintOpenGLWidget::VectorImageModel::rebuildBounds()
{
    m_bounds = QRectF();
    for (const VectorStroke &stroke : m_strokes) {
        if (m_bounds.isNull()) {
            m_bounds = stroke.bounds;
        } else {
            m_bounds = m_bounds.united(stroke.bounds);
        }
    }
}

PaintOpenGLWidget::PaintOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAutoFillBackground(false);
    setMouseTracking(true);
}

void PaintOpenGLWidget::setPenColor(const QColor &color)
{
    m_penColor = color;
    m_tool = Tool::Pen;
}

void PaintOpenGLWidget::setTool(Tool tool)
{
    m_tool = tool;
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
}

void PaintOpenGLWidget::setSmoothValue(int value)
{
    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    m_smoothValue = value;
}

void PaintOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    for (const VectorStroke &stroke : m_image.strokes()) {
        painter.setPen(QPen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(stroke.path);
    }

    if (m_hasCurrentStroke) {
        painter.setPen(QPen(m_currentStroke.color, m_currentStroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(m_currentStroke.path);
    }

    if ((m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) && m_hasHoverPos) {
        painter.setPen(QPen(QColor(220, 0, 180), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_hoverPos, m_eraserRadius, m_eraserRadius);
    }
}

void PaintOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const QPointF pos = event->position();
    m_hoverPos = pos;
    m_hasHoverPos = true;
    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        m_hasLastEraserPos = true;
        m_lastEraserPos = pos;
        if (m_tool == Tool::DeleteLine) {
            deleteLineAt(pos);
        } else {
            eraseAt(pos);
        }
        update();
        event->accept();
        return;
    }

    m_points.clear();
    appendPoint(pos);
    m_currentStroke = makeStroke(m_points, m_penColor, m_penWidth);
    m_hasCurrentStroke = true;
    update();
    event->accept();
}

void PaintOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hoverPos = event->position();
    m_hasHoverPos = true;

    if (!(event->buttons() & Qt::LeftButton)) {
        update();
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        if (!m_hasLastEraserPos) {
            m_lastEraserPos = m_hoverPos;
            m_hasLastEraserPos = true;
        }

        if (m_tool == Tool::DeleteLine) {
            deleteLineBetween(m_lastEraserPos, m_hoverPos);
        } else {
            eraseBetween(m_lastEraserPos, m_hoverPos);
        }

        m_lastEraserPos = m_hoverPos;
        update();
        event->accept();
        return;
    }

    if (!m_hasCurrentStroke) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    if (appendPoint(m_hoverPos)) {
        updateCurrentStroke();
    }

    event->accept();
}

void PaintOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        m_hoverPos = event->position();
        m_hasHoverPos = true;
        if (m_tool == Tool::DeleteLine) {
            deleteLineAt(m_hoverPos);
        } else {
            eraseAt(m_hoverPos);
        }
        update();
        m_hasLastEraserPos = false;
        event->accept();
        return;
    }

    if (!m_hasCurrentStroke) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    appendPoint(event->position());
    finishCurrentStroke();
    event->accept();
}

QVector<QPointF> PaintOpenGLWidget::filteredPoints(const QVector<QPointF> &points) const
{
    return points;
}

QPainterPath PaintOpenGLWidget::makeVectorPath(const QVector<QPointF> &points) const
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    QVector<QPointF> smoothed = points;
    if (smoothed.isEmpty()) {
        return path;
    }

    path.moveTo(smoothed.first());
    if (smoothed.size() == 1) {
        path.lineTo(smoothed.first() + QPointF(0.01, 0.01));
        return path;
    }

    const int smoothPasses = m_smoothValue / 25;
    for (int pass = 0; pass < smoothPasses && smoothed.size() > 2; ++pass) {
        QVector<QPointF> next;
        next.reserve(smoothed.size());
        next.append(smoothed.first());
        for (int i = 1; i + 1 < smoothed.size(); ++i) {
            next.append((smoothed[i - 1] + smoothed[i] * 2.0 + smoothed[i + 1]) / 4.0);
        }
        next.append(smoothed.last());
        smoothed = next;
    }

    if (smoothed.size() == 2) {
        path.lineTo(smoothed.last());
        return path;
    }

    for (int i = 1; i < smoothed.size(); ++i) {
        const QPointF control = smoothed[i - 1];
        const QPointF end = (smoothed[i - 1] + smoothed[i]) / 2.0;
        path.quadTo(control, end);
    }

    path.lineTo(smoothed.last());
    return path;
}

QPainterPath PaintOpenGLWidget::makePolylinePath(const QVector<QPointF> &points) const
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    path.moveTo(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first() + QPointF(0.01, 0.01));
        return path;
    }

    for (int i = 1; i < points.size(); ++i) {
        path.lineTo(points[i]);
    }
    return path;
}

void PaintOpenGLWidget::updateCurrentStroke()
{
    if (!m_hasCurrentStroke) {
        return;
    }

    m_currentStroke = makeStroke(m_points, m_currentStroke.color, m_currentStroke.width);
    update();
}

void PaintOpenGLWidget::finishCurrentStroke()
{
    updateCurrentStroke();
    if (!m_currentStroke.points.isEmpty()) {
        m_image.addStroke(m_currentStroke);
    }
    m_hasCurrentStroke = false;
    m_points.clear();
    update();
}

bool PaintOpenGLWidget::eraseAt(const QPointF &pos)
{
    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF eraserBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                              imageRadius * 2.0, imageRadius * 2.0);
    if (!m_image.bounds().intersects(eraserBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = m_image.strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeAt(i, pos) || changed;
    }
    return changed;
}

bool PaintOpenGLWidget::eraseBetween(const QPointF &from, const QPointF &to)
{
    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const qreal left = realMin(from.x(), to.x()) - imageRadius;
    const qreal top = realMin(from.y(), to.y()) - imageRadius;
    const qreal right = realMax(from.x(), to.x()) + imageRadius;
    const qreal bottom = realMax(from.y(), to.y()) + imageRadius;
    const QRectF eraserBounds(QPointF(left, top), QPointF(right, bottom));
    if (!m_image.bounds().intersects(eraserBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = m_image.strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeBetween(i, from, to) || changed;
    }
    return changed;
}

bool PaintOpenGLWidget::deleteLineAt(const QPointF &pos)
{
    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF hitBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                           imageRadius * 2.0, imageRadius * 2.0);
    if (!m_image.bounds().intersects(hitBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = m_image.strokeCount() - 1; i >= 0; --i) {
        const VectorStroke &stroke = m_image.strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        if (strokeHitsCircle(stroke, pos, m_eraserRadius)) {
            m_image.removeStrokeAt(i);
            changed = true;
        }
    }
    return changed;
}

bool PaintOpenGLWidget::deleteLineBetween(const QPointF &from, const QPointF &to)
{
    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const qreal left = realMin(from.x(), to.x()) - imageRadius;
    const qreal top = realMin(from.y(), to.y()) - imageRadius;
    const qreal right = realMax(from.x(), to.x()) + imageRadius;
    const qreal bottom = realMax(from.y(), to.y()) + imageRadius;
    const QRectF hitBounds(QPointF(left, top), QPointF(right, bottom));
    if (!m_image.bounds().intersects(hitBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = m_image.strokeCount() - 1; i >= 0; --i) {
        const VectorStroke &stroke = m_image.strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        if (strokeHitsCapsule(stroke, from, to, m_eraserRadius)) {
            m_image.removeStrokeAt(i);
            changed = true;
        }
    }
    return changed;
}

bool PaintOpenGLWidget::strokeHitsCircle(const VectorStroke &stroke, const QPointF &center, qreal radius) const
{
    if (stroke.points.isEmpty()) {
        return false;
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    if (distanceSquared(stroke.points.first(), center) <= effectiveRadius2) {
        return true;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        if (distancePointToSegmentSquared(center, stroke.points[i - 1], stroke.points[i]) <= effectiveRadius2) {
            return true;
        }
    }
    return false;
}

bool PaintOpenGLWidget::strokeHitsCapsule(const VectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius) const
{
    if (QLineF(from, to).length() <= kEpsilon) {
        return strokeHitsCircle(stroke, from, radius);
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    for (int i = 1; i < stroke.points.size(); ++i) {
        if (segmentSegmentDistanceSquared(stroke.points[i - 1], stroke.points[i], from, to) <= effectiveRadius2) {
            return true;
        }
    }
    return false;
}

bool PaintOpenGLWidget::eraseStrokeAt(int strokeIndex, const QPointF &pos)
{
    if (strokeIndex < 0 || strokeIndex >= m_image.strokeCount()) {
        return false;
    }

    const VectorStroke stroke = m_image.strokeAt(strokeIndex);
    const qreal effectiveRadius = m_eraserRadius + stroke.width * 0.5;
    const QRectF eraserBounds(pos.x() - effectiveRadius, pos.y() - effectiveRadius,
                              effectiveRadius * 2.0, effectiveRadius * 2.0);
    if (!stroke.bounds.intersects(eraserBounds)) {
        return false;
    }

    const QVector<Range> keepRanges = keepRangesForCircle(stroke, pos, m_eraserRadius);
    if (keepRanges.size() == 1 && keepRanges.first().first <= kEpsilon &&
        keepRanges.first().second >= 1.0 - kEpsilon) {
        return false;
    }

    QVector<VectorStroke> pieces;
    for (const Range &range : keepRanges) {
        pieces.append(subStroke(stroke, range.first, range.second));
    }
    m_image.replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

bool PaintOpenGLWidget::eraseStrokeBetween(int strokeIndex, const QPointF &from, const QPointF &to)
{
    if (strokeIndex < 0 || strokeIndex >= m_image.strokeCount()) {
        return false;
    }

    const VectorStroke stroke = m_image.strokeAt(strokeIndex);
    const qreal effectiveRadius = m_eraserRadius + stroke.width * 0.5;
    const qreal left = realMin(from.x(), to.x()) - effectiveRadius;
    const qreal top = realMin(from.y(), to.y()) - effectiveRadius;
    const qreal right = realMax(from.x(), to.x()) + effectiveRadius;
    const qreal bottom = realMax(from.y(), to.y()) + effectiveRadius;
    const QRectF eraserBounds(QPointF(left, top), QPointF(right, bottom));
    if (!stroke.bounds.intersects(eraserBounds)) {
        return false;
    }

    const QVector<Range> keepRanges = keepRangesForCapsule(stroke, from, to, m_eraserRadius);
    if (keepRanges.size() == 1 && keepRanges.first().first <= kEpsilon &&
        keepRanges.first().second >= 1.0 - kEpsilon) {
        return false;
    }

    QVector<VectorStroke> pieces;
    for (const Range &range : keepRanges) {
        pieces.append(subStroke(stroke, range.first, range.second));
    }
    m_image.replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

QVector<PaintOpenGLWidget::Range> PaintOpenGLWidget::keepRangesForCircle(const VectorStroke &stroke, const QPointF &center, qreal radius) const
{
    QVector<Range> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<Range>{Range{0.0, 1.0}};
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    qreal eraseStart = 0.0;
    auto inside = [&](const QPointF &point) {
        return distanceSquared(point, center) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i - 1];
        const QPointF b = stroke.points[i];
        if (distancePointToSegmentSquared(center, a, b) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const QVector<QPointF> samples = densifySegment(a, b, realMax(qreal(1), effectiveRadius * 0.25));
        const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const qreal t = static_cast<qreal>(sampleIndex + 1) / samples.size();
            const qreal w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
            const bool sampleInside = inside(samples[sampleIndex]);
            if (sampleInside && !inErase) {
                inErase = true;
                eraseStart = w;
            } else if (!sampleInside && inErase) {
                inErase = false;
                eraseRanges.append(Range{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(Range{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<PaintOpenGLWidget::Range> PaintOpenGLWidget::keepRangesForCapsule(const VectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius) const
{
    if (QLineF(from, to).length() <= kEpsilon) {
        return keepRangesForCircle(stroke, from, radius);
    }

    QVector<Range> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<Range>{Range{0.0, 1.0}};
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    qreal eraseStart = 0.0;
    auto inside = [&](const QPointF &point) {
        return distancePointToSegmentSquared(point, from, to) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i - 1];
        const QPointF b = stroke.points[i];
        if (segmentSegmentDistanceSquared(a, b, from, to) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const QVector<QPointF> samples = densifySegment(a, b, realMax(qreal(1), effectiveRadius * 0.25));
        const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const qreal t = static_cast<qreal>(sampleIndex + 1) / samples.size();
            const qreal w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
            const bool sampleInside = inside(samples[sampleIndex]);
            if (sampleInside && !inErase) {
                inErase = true;
                eraseStart = w;
            } else if (!sampleInside && inErase) {
                inErase = false;
                eraseRanges.append(Range{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(Range{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<PaintOpenGLWidget::Range> PaintOpenGLWidget::complementRanges(const QVector<Range> &eraseRanges) const
{
    QVector<Range> keepRanges;
    qreal last = 0.0;

    for (const Range &range : eraseRanges) {
        const qreal first = clamp01(range.first);
        const qreal second = clamp01(range.second);
        if (second <= first) {
            continue;
        }
        if (first > last + kEpsilon) {
            keepRanges.append(Range{last, first});
        }
        if (second > last) {
            last = second;
        }
    }

    if (last < 1.0 - kEpsilon) {
        keepRanges.append(Range{last, 1.0});
    }

    return keepRanges;
}

PaintOpenGLWidget::VectorStroke PaintOpenGLWidget::makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id, bool filterInput, bool smoothPath) const
{
    VectorStroke stroke;
    stroke.id = id;
    stroke.points = filterInput ? filteredPoints(points) : points;
    stroke.lengths.clear();
    stroke.lengths.reserve(stroke.points.size());
    stroke.totalLength = 0.0;
    for (int i = 0; i < stroke.points.size(); ++i) {
        if (i > 0) {
            stroke.totalLength += QLineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }
    stroke.path = smoothPath ? makeVectorPath(stroke.points) : makePolylinePath(stroke.points);
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

PaintOpenGLWidget::VectorStroke PaintOpenGLWidget::subStroke(const VectorStroke &stroke, qreal fromW, qreal toW) const
{
    const qreal fromLength = clamp01(fromW) * stroke.totalLength;
    const qreal toLength = clamp01(toW) * stroke.totalLength;
    QVector<QPointF> points;
    if (toLength <= fromLength + kEpsilon) {
        return makeStroke(points, stroke.color, stroke.width, stroke.id, false, true);
    }

    points.append(pointAtLength(stroke, fromLength));
    for (int i = 1; i + 1 < stroke.points.size(); ++i) {
        if (stroke.lengths[i] > fromLength + kEpsilon &&
            stroke.lengths[i] < toLength - kEpsilon) {
            points.append(stroke.points[i]);
        }
    }
    points.append(pointAtLength(stroke, toLength));

    return makeStroke(points, stroke.color, stroke.width, stroke.id, false, true);
}

QPointF PaintOpenGLWidget::pointAtLength(const VectorStroke &stroke, qreal length) const
{
    if (stroke.points.isEmpty()) {
        return QPointF();
    }
    if (length <= 0 || stroke.points.size() == 1) {
        return stroke.points.first();
    }
    if (length >= stroke.totalLength) {
        return stroke.points.last();
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        if (stroke.lengths[i] >= length) {
            const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
            if (segmentLength <= kEpsilon) {
                return stroke.points[i];
            }
            const qreal t = (length - stroke.lengths[i - 1]) / segmentLength;
            return stroke.points[i - 1] + (stroke.points[i] - stroke.points[i - 1]) * t;
        }
    }

    return stroke.points.last();
}

bool PaintOpenGLWidget::appendPoint(const QPointF &point)
{
    if (m_points.isEmpty()) {
        m_points.append(point);
        return true;
    }

    if (QLineF(m_points.last(), point).length() >= m_minPointDistance) {
        m_points.append(point);
        return true;
    }

    return false;
}
