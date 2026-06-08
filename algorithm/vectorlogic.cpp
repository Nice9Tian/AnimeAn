#include "vectorlogic.h"

#include <QMap>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QQueue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal kEpsilon = 0.0001;
constexpr qreal kVectorRegionOverpaintWidth = 2.0;
constexpr qreal kGraphEpsilon = 0.001;

struct GraphVertex {
    QPointF point;
    QVector<int> neighbors;
};

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
    const qreal aLeft = std::min(a0.x(), a1.x());
    const qreal aRight = std::max(a0.x(), a1.x());
    const qreal aTop = std::min(a0.y(), a1.y());
    const qreal aBottom = std::max(a0.y(), a1.y());
    const qreal bLeft = std::min(b0.x(), b1.x());
    const qreal bRight = std::max(b0.x(), b1.x());
    const qreal bTop = std::min(b0.y(), b1.y());
    const qreal bBottom = std::max(b0.y(), b1.y());
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
    return std::min(std::min(d0, d1), std::min(d2, d3));
}

QVector<QPointF> densifySegment(const QPointF &from, const QPointF &to, qreal step)
{
    QVector<QPointF> points;
    const qreal length = QLineF(from, to).length();
    const int rawCount = std::max(1, static_cast<int>(std::ceil(length / std::max(step, qreal(1)))));
    const int count = std::min(rawCount, 64);
    points.reserve(count);
    for (int i = 1; i <= count; ++i) {
        const qreal t = static_cast<qreal>(i) / count;
        points.append(from + (to - from) * t);
    }
    return points;
}

qreal polygonSignedArea(const QVector<QPointF> &points)
{
    if (points.size() < 3) {
        return 0.0;
    }

    qreal area = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

QString vertexKey(const QPointF &point)
{
    const qint64 x = qRound64(point.x() / kGraphEpsilon);
    const qint64 y = qRound64(point.y() / kGraphEpsilon);
    return QString::number(x) + QLatin1Char(':') + QString::number(y);
}

bool segmentIntersectionParameters(const QLineF &a, const QLineF &b, qreal &ta, qreal &tb)
{
    const QPointF r = a.p2() - a.p1();
    const QPointF s = b.p2() - b.p1();
    const qreal denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) <= kEpsilon) {
        return false;
    }

    const QPointF delta = b.p1() - a.p1();
    ta = (delta.x() * s.y() - delta.y() * s.x()) / denom;
    tb = (delta.x() * r.y() - delta.y() * r.x()) / denom;
    return ta >= -kEpsilon && ta <= 1.0 + kEpsilon &&
           tb >= -kEpsilon && tb <= 1.0 + kEpsilon;
}

void appendUniqueParameter(QVector<qreal> &values, qreal value)
{
    value = clamp01(value);
    for (qreal existing : values) {
        if (std::abs(existing - value) <= kGraphEpsilon) {
            return;
        }
    }
    values.append(value);
}

void appendUniqueNeighbor(QVector<int> &neighbors, int neighbor)
{
    if (!neighbors.contains(neighbor)) {
        neighbors.append(neighbor);
    }
}
}

qreal AnimeVectorLogic::epsilon()
{
    return kEpsilon;
}

QVector<QPointF> AnimeVectorLogic::filteredPoints(const QVector<QPointF> &points)
{
    return points;
}

QPainterPath AnimeVectorLogic::makeSmoothedPath(const QVector<QPointF> &points, int smoothValue)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    QVector<QPointF> smoothed = points;
    path.moveTo(smoothed.first());
    if (smoothed.size() == 1) {
        path.lineTo(smoothed.first() + QPointF(0.01, 0.01));
        return path;
    }

    const int smoothPasses = smoothValue / 25;
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

QPainterPath AnimeVectorLogic::makePolylinePath(const QVector<QPointF> &points)
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

AnimeVectorStroke AnimeVectorLogic::makeStroke(const QVector<QPointF> &points,
                                               const QColor &color,
                                               qreal width,
                                               int id,
                                               bool filterInput,
                                               bool smoothPath,
                                               int smoothValue)
{
    AnimeVectorStroke stroke;
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
    stroke.path = smoothPath ? makeSmoothedPath(stroke.points, smoothValue) : makePolylinePath(stroke.points);
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

bool AnimeVectorLogic::strokeHitsCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius)
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

bool AnimeVectorLogic::strokeHitsCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius)
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

QVector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius)
{
    QVector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
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

        const QVector<QPointF> samples = densifySegment(a, b, std::max(qreal(1), effectiveRadius * 0.25));
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
                eraseRanges.append(AnimeVectorRange{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(AnimeVectorRange{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius)
{
    if (QLineF(from, to).length() <= kEpsilon) {
        return keepRangesForCircle(stroke, from, radius);
    }

    QVector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
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

        const QVector<QPointF> samples = densifySegment(a, b, std::max(qreal(1), effectiveRadius * 0.25));
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
                eraseRanges.append(AnimeVectorRange{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(AnimeVectorRange{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<AnimeVectorRange> AnimeVectorLogic::complementRanges(const QVector<AnimeVectorRange> &eraseRanges)
{
    QVector<AnimeVectorRange> keepRanges;
    qreal last = 0.0;

    for (const AnimeVectorRange &range : eraseRanges) {
        const qreal first = clamp01(range.first);
        const qreal second = clamp01(range.second);
        if (second <= first) {
            continue;
        }
        if (first > last + kEpsilon) {
            keepRanges.append(AnimeVectorRange{last, first});
        }
        if (second > last) {
            last = second;
        }
    }

    if (last < 1.0 - kEpsilon) {
        keepRanges.append(AnimeVectorRange{last, 1.0});
    }

    return keepRanges;
}

AnimeVectorStroke AnimeVectorLogic::subStroke(const AnimeVectorStroke &stroke, qreal fromW, qreal toW, int smoothValue)
{
    const qreal fromLength = clamp01(fromW) * stroke.totalLength;
    const qreal toLength = clamp01(toW) * stroke.totalLength;
    QVector<QPointF> points;
    if (toLength <= fromLength + kEpsilon) {
        return makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, smoothValue);
    }

    points.append(pointAtLength(stroke, fromLength));
    for (int i = 1; i + 1 < stroke.points.size(); ++i) {
        if (stroke.lengths[i] > fromLength + kEpsilon &&
            stroke.lengths[i] < toLength - kEpsilon) {
            points.append(stroke.points[i]);
        }
    }
    points.append(pointAtLength(stroke, toLength));

    return makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, smoothValue);
}

QPointF AnimeVectorLogic::pointAtLength(const AnimeVectorStroke &stroke, qreal length)
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

QVector<QLineF> AnimeVectorLogic::segmentsFromPath(const QPainterPath &path)
{
    QVector<QLineF> segments;
    const QList<QPolygonF> subpaths = path.toSubpathPolygons();
    for (const QPolygonF &polyline : subpaths) {
        for (int i = 1; i < polyline.size(); ++i) {
            const QPointF a = polyline[i - 1];
            const QPointF b = polyline[i];
            if (QLineF(a, b).length() > kGraphEpsilon) {
                segments.append(QLineF(a, b));
            }
        }
    }
    return segments;
}

QVector<AnimeVectorRegionFace> AnimeVectorLogic::computeVectorRegionFaces(const QVector<QLineF> &segments)
{
    QVector<AnimeVectorRegionFace> faces;
    if (segments.isEmpty()) {
        return faces;
    }

    QVector<QVector<qreal>> splitParameters;
    splitParameters.resize(segments.size());
    for (int i = 0; i < segments.size(); ++i) {
        splitParameters[i].append(0.0);
        splitParameters[i].append(1.0);
    }

    for (int i = 0; i < segments.size(); ++i) {
        for (int j = i + 1; j < segments.size(); ++j) {
            qreal ti = 0.0;
            qreal tj = 0.0;
            if (!segmentIntersectionParameters(segments[i], segments[j], ti, tj)) {
                continue;
            }
            appendUniqueParameter(splitParameters[i], ti);
            appendUniqueParameter(splitParameters[j], tj);
        }
    }

    QVector<GraphVertex> vertices;
    QMap<QString, int> vertexByKey;
    auto vertexIndex = [&](const QPointF &point) {
        const QString key = vertexKey(point);
        const auto it = vertexByKey.constFind(key);
        if (it != vertexByKey.constEnd()) {
            const int index = it.value();
            vertices[index].point = (vertices[index].point + point) * 0.5;
            return index;
        }

        GraphVertex vertex;
        vertex.point = point;
        const int index = vertices.size();
        vertices.append(vertex);
        vertexByKey.insert(key, index);
        return index;
    };

    for (int i = 0; i < segments.size(); ++i) {
        QVector<qreal> params = splitParameters[i];
        std::sort(params.begin(), params.end());
        for (int j = 1; j < params.size(); ++j) {
            if (params[j] - params[j - 1] <= kGraphEpsilon) {
                continue;
            }
            const QPointF delta = segments[i].p2() - segments[i].p1();
            const QPointF p0 = segments[i].p1() + delta * params[j - 1];
            const QPointF p1 = segments[i].p1() + delta * params[j];
            const int v0 = vertexIndex(p0);
            const int v1 = vertexIndex(p1);
            if (v0 == v1) {
                continue;
            }
            appendUniqueNeighbor(vertices[v0].neighbors, v1);
            appendUniqueNeighbor(vertices[v1].neighbors, v0);
        }
    }

    for (GraphVertex &vertex : vertices) {
        std::sort(vertex.neighbors.begin(), vertex.neighbors.end(), [&](int lhs, int rhs) {
            const QPointF dl = vertices[lhs].point - vertex.point;
            const QPointF dr = vertices[rhs].point - vertex.point;
            return std::atan2(dl.y(), dl.x()) < std::atan2(dr.y(), dr.x());
        });
    }

    QMap<QString, bool> visited;
    auto edgeKey = [](int from, int to) {
        return QString::number(from) + QLatin1Char('>') + QString::number(to);
    };

    for (int from = 0; from < vertices.size(); ++from) {
        for (int to : vertices[from].neighbors) {
            const QString firstKey = edgeKey(from, to);
            if (visited.value(firstKey, false)) {
                continue;
            }

            QVector<QPointF> facePoints;
            int currentFrom = from;
            int currentTo = to;
            bool closed = false;
            for (int guard = 0; guard < 10000; ++guard) {
                visited.insert(edgeKey(currentFrom, currentTo), true);
                facePoints.append(vertices[currentFrom].point);

                const QVector<int> &nextNeighbors = vertices[currentTo].neighbors;
                const int reverseIndex = nextNeighbors.indexOf(currentFrom);
                if (reverseIndex < 0 || nextNeighbors.isEmpty()) {
                    break;
                }

                const int nextIndex = (reverseIndex - 1 + nextNeighbors.size()) % nextNeighbors.size();
                const int nextTo = nextNeighbors[nextIndex];
                currentFrom = currentTo;
                currentTo = nextTo;
                if (currentFrom == from && currentTo == to) {
                    closed = true;
                    break;
                }
            }

            if (!closed || facePoints.size() < 3) {
                continue;
            }

            const qreal area = polygonSignedArea(facePoints);
            if (area <= kGraphEpsilon) {
                continue;
            }

            QPainterPath facePath;
            facePath.moveTo(facePoints.first());
            for (int i = 1; i < facePoints.size(); ++i) {
                facePath.lineTo(facePoints[i]);
            }
            facePath.closeSubpath();
            faces.append(AnimeVectorRegionFace{facePath.simplified(), area});
        }
    }

    return faces;
}

QPainterPath AnimeVectorLogic::vectorRegionPathAt(const QPointF &seed, const QVector<QLineF> &segments, const QRect &canvasRect)
{
    if (!canvasRect.contains(seed.toPoint())) {
        return QPainterPath();
    }

    const QVector<AnimeVectorRegionFace> faces = computeVectorRegionFaces(segments);
    const AnimeVectorRegionFace *bestFace = nullptr;
    qreal bestArea = std::numeric_limits<qreal>::max();
    for (const AnimeVectorRegionFace &face : faces) {
        const qreal area = std::abs(face.signedArea);
        if (area <= kGraphEpsilon ||
            area >= bestArea ||
            !face.path.contains(seed)) {
            continue;
        }
        bestFace = &face;
        bestArea = area;
    }

    if (!bestFace) {
        return QPainterPath();
    }

    QPainterPath canvas;
    canvas.addRect(canvasRect);

    QPainterPathStroker overpaintStroker;
    overpaintStroker.setWidth(kVectorRegionOverpaintWidth);
    overpaintStroker.setCapStyle(Qt::RoundCap);
    overpaintStroker.setJoinStyle(Qt::RoundJoin);
    return bestFace->path.united(overpaintStroker.createStroke(bestFace->path)).intersected(canvas).simplified();
}

QPainterPath AnimeVectorLogic::fillPathFromMask(const QPoint &seed, const QImage &boundary)
{
    QPainterPath path;
    if (boundary.isNull() || !boundary.rect().contains(seed)) {
        return path;
    }
    if (qGray(boundary.pixel(seed.x(), seed.y())) > 0) {
        return path;
    }

    QImage visited(boundary.size(), QImage::Format_Grayscale8);
    visited.fill(0);

    QQueue<QPoint> queue;
    queue.enqueue(seed);
    visited.setPixel(seed.x(), seed.y(), 255);

    bool touchesCanvasEdge = false;
    while (!queue.isEmpty()) {
        const QPoint p = queue.dequeue();
        if (p.x() == 0 || p.y() == 0 || p.x() == boundary.width() - 1 || p.y() == boundary.height() - 1) {
            touchesCanvasEdge = true;
        }

        const QPoint neighbors[4] = {
            QPoint(p.x() + 1, p.y()),
            QPoint(p.x() - 1, p.y()),
            QPoint(p.x(), p.y() + 1),
            QPoint(p.x(), p.y() - 1)
        };

        for (const QPoint &next : neighbors) {
            if (!boundary.rect().contains(next)) {
                continue;
            }
            if (qGray(visited.pixel(next.x(), next.y())) > 0 ||
                qGray(boundary.pixel(next.x(), next.y())) > 0) {
                continue;
            }
            visited.setPixel(next.x(), next.y(), 255);
            queue.enqueue(next);
        }
    }

    if (touchesCanvasEdge) {
        return QPainterPath();
    }

    for (int y = 0; y < visited.height(); ++y) {
        int runStart = -1;
        for (int x = 0; x < visited.width(); ++x) {
            const bool filled = qGray(visited.pixel(x, y)) > 0;
            if (filled && runStart < 0) {
                runStart = x;
            } else if (!filled && runStart >= 0) {
                path.addRect(QRectF(runStart, y, x - runStart, 1));
                runStart = -1;
            }
        }
        if (runStart >= 0) {
            path.addRect(QRectF(runStart, y, visited.width() - runStart, 1));
        }
    }

    return path.simplified();
}
