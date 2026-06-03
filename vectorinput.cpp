#include "paintopenglwidget.h"

#include <QLineF>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QPainterPathStroker>
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

struct DirectedEdgeKey {
    int from = -1;
    int to = -1;
};

struct GraphFace {
    QPainterPath path;
    qreal signedArea = 0.0;
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

QVector<QLineF> segmentsFromPath(const QPainterPath &path)
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

QVector<GraphFace> computeVectorRegionFaces(const QVector<QLineF> &segments)
{
    QVector<GraphFace> faces;
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
            faces.append(GraphFace{facePath.simplified(), area});
        }
    }

    return faces;
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

void PaintOpenGLWidget::setDrawingColor(const QColor &color)
{
    m_penColor = color;
}

void PaintOpenGLWidget::setPenWidth(qreal width)
{
    if (width < 1.0) {
        width = 1.0;
    } else if (width > 50.0) {
        width = 50.0;
    }

    m_penWidth = width;
    if (m_hasCurrentStroke) {
        updateCurrentStroke();
    }
    update();
}

void PaintOpenGLWidget::setTool(Tool tool)
{
    m_tool = tool;
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
}

void PaintOpenGLWidget::setFillScope(FillScope scope)
{
    m_fillScope = scope;
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

void PaintOpenGLWidget::setCurrentLayer(int layerIndex)
{
    m_model.setCurrentLayer(layerIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
}

void PaintOpenGLWidget::setCurrentFrame(int frameIndex)
{
    m_model.setCurrentFrame(frameIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
}

int PaintOpenGLWidget::layerCount() const
{
    return m_model.layerCount();
}

int PaintOpenGLWidget::frameCount() const
{
    return m_model.frameCount();
}

int PaintOpenGLWidget::assetCount() const
{
    return m_model.assetCount();
}

QString PaintOpenGLWidget::layerName(int layerIndex) const
{
    return m_model.layerName(layerIndex);
}

QString PaintOpenGLWidget::frameName(int frameIndex) const
{
    return m_model.frameName(frameIndex);
}

QString PaintOpenGLWidget::assetName(int assetIndex) const
{
    return m_model.assetName(assetIndex);
}

int PaintOpenGLWidget::importRasterLayer(const QImage &image, const QString &layerName)
{
    if (image.isNull()) {
        return -1;
    }

    const QPointF canvasCenter(width() * 0.5, height() * 0.5);
    const QPointF rasterCenter(image.width() * 0.5, image.height() * 0.5);
    const QPointF topLeft = canvasCenter - rasterCenter;
    const int columnIndex = m_model.addRasterLayer(layerName, m_model.currentFrame(), image, topLeft);
    if (columnIndex >= 0) {
        m_points.clear();
        m_hasCurrentStroke = false;
        m_hasLastEraserPos = false;
        update();
    }
    return columnIndex;
}

int PaintOpenGLWidget::addLayer()
{
    const int columnIndex = m_model.addLayer();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
    return columnIndex;
}

bool PaintOpenGLWidget::deleteLayer(int layerIndex)
{
    if (!m_model.deleteLayer(layerIndex)) {
        return false;
    }

    m_model.remapFillSourceLayersAfterDelete(layerIndex);
    removeInvalidFillRegions();
    update();
    return true;
}

bool PaintOpenGLWidget::moveLayer(int fromIndex, int toIndex)
{
    if (!m_model.moveLayer(fromIndex, toIndex)) {
        return false;
    }

    m_model.remapFillSourceLayersAfterMove(fromIndex, toIndex);
    removeInvalidFillRegions();
    update();
    return true;
}

int PaintOpenGLWidget::addFrame()
{
    const int row = m_model.addFrame();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
    return row;
}

bool PaintOpenGLWidget::deleteFrame(int frameIndex)
{
    if (!m_model.deleteFrame(frameIndex)) {
        return false;
    }

    update();
    return true;
}

bool PaintOpenGLWidget::moveFrame(int fromIndex, int toIndex)
{
    if (!m_model.moveFrame(fromIndex, toIndex)) {
        return false;
    }

    update();
    return true;
}

AnimeSceneModel &PaintOpenGLWidget::model()
{
    return m_model;
}

int PaintOpenGLWidget::addAsset(AnimeColumnType type, const QString &name)
{
    const int assetIndex = m_model.addAsset(type, name);
    if (assetIndex >= 0) {
        emit assetListChanged(assetIndex);
    }
    return assetIndex;
}

void PaintOpenGLWidget::setCurrentAsset(int assetIndex)
{
    m_model.setCurrentAsset(assetIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
}

bool PaintOpenGLWidget::assignAssetToLayer(int layerIndex, int assetIndex)
{
    const bool assigned = m_model.assignAssetToLayer(m_model.currentFrame(), layerIndex, assetIndex);
    if (assigned) {
        m_points.clear();
        m_hasCurrentStroke = false;
        m_hasLastEraserPos = false;
        update();
    }
    return assigned;
}

int PaintOpenGLWidget::addLayerForAsset(int assetIndex)
{
    const int layerIndex = m_model.addLayerForAsset(m_model.currentFrame(), assetIndex);
    if (layerIndex >= 0) {
        m_points.clear();
        m_hasCurrentStroke = false;
        m_hasLastEraserPos = false;
        update();
    }
    return layerIndex;
}

const AnimeSceneModel &PaintOpenGLWidget::model() const
{
    return m_model;
}

void PaintOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    const AnimeScene &scene = m_model.scene();
    for (int columnIndex = scene.xsheet.columns.size() - 1; columnIndex >= 0; --columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (!column.visible) {
            continue;
        }

        const AnimeCell cell = column.cellAt(m_model.currentFrame());
        const VectorImageModel *image = m_model.imageForCell(cell);
        painter.setOpacity(column.opacity);
        if (image) {
            if (image->hasRaster()) {
                const AnimeRasterImage &raster = image->raster();
                painter.drawImage(raster.topLeft, raster.image);
            }
            painter.setPen(Qt::NoPen);
            for (const AnimeVectorFillRegion &fill : image->fillRegions()) {
                painter.setBrush(fill.color);
                painter.drawPath(fill.path);
            }
            painter.setBrush(Qt::NoBrush);
            for (const VectorStrokeNode &node : image->strokeNodes()) {
                const VectorStroke &stroke = node.stroke;
                painter.setPen(QPen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(stroke.path);
            }
        }

        if (columnIndex == m_model.currentLayer() && m_hasCurrentStroke) {
            painter.setPen(QPen(m_currentStroke.color, m_currentStroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(m_currentStroke.path);
        }
    }
    painter.setOpacity(1.0);

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
    if (m_tool == Tool::Fill) {
        fillAt(pos);
        update();
        event->accept();
        return;
    }

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

    if (m_model.currentLayer() >= 0 && !currentColumnEditable()) {
        event->accept();
        return;
    }

    const int assetCountBefore = m_model.assetCount();
    if (!currentImage(true, AnimeColumnType::Vector)) {
        event->accept();
        return;
    }
    if (m_model.assetCount() != assetCountBefore) {
        emit assetListChanged(m_model.currentAsset());
        emit layerListChanged(m_model.currentLayer());
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
        const int assetCountBefore = m_model.assetCount();
        if (VectorImageModel *image = currentImage(true, AnimeColumnType::Vector)) {
            image->addStroke(m_currentStroke);
            removeInvalidFillRegions();
            if (m_model.assetCount() != assetCountBefore) {
                emit assetListChanged(m_model.currentAsset());
                emit layerListChanged(m_model.currentLayer());
            }
        }
    }
    m_hasCurrentStroke = false;
    m_points.clear();
    update();
}

bool PaintOpenGLWidget::eraseAt(const QPointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF eraserBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                              imageRadius * 2.0, imageRadius * 2.0);
    if (!image->bounds().intersects(eraserBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeAt(i, pos) || changed;
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed;
}

bool PaintOpenGLWidget::eraseBetween(const QPointF &from, const QPointF &to)
{
    VectorImageModel *image = currentImage(false);
    if (!image || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const qreal left = std::min(from.x(), to.x()) - imageRadius;
    const qreal top = std::min(from.y(), to.y()) - imageRadius;
    const qreal right = std::max(from.x(), to.x()) + imageRadius;
    const qreal bottom = std::max(from.y(), to.y()) + imageRadius;
    const QRectF eraserBounds(QPointF(left, top), QPointF(right, bottom));
    if (!image->bounds().intersects(eraserBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeBetween(i, from, to) || changed;
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed;
}

bool PaintOpenGLWidget::deleteLineAt(const QPointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF hitBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                           imageRadius * 2.0, imageRadius * 2.0);
    if (!image->bounds().intersects(hitBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        const VectorStroke &stroke = image->strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        if (strokeHitsCircle(stroke, pos, m_eraserRadius)) {
            image->removeStrokeAt(i);
            changed = true;
        }
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed;
}

bool PaintOpenGLWidget::deleteLineBetween(const QPointF &from, const QPointF &to)
{
    VectorImageModel *image = currentImage(false);
    if (!image || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const qreal left = std::min(from.x(), to.x()) - imageRadius;
    const qreal top = std::min(from.y(), to.y()) - imageRadius;
    const qreal right = std::max(from.x(), to.x()) + imageRadius;
    const qreal bottom = std::max(from.y(), to.y()) + imageRadius;
    const QRectF hitBounds(QPointF(left, top), QPointF(right, bottom));
    if (!image->bounds().intersects(hitBounds)) {
        return false;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        const VectorStroke &stroke = image->strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        if (strokeHitsCapsule(stroke, from, to, m_eraserRadius)) {
            image->removeStrokeAt(i);
            changed = true;
        }
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed;
}

bool PaintOpenGLWidget::fillAt(const QPointF &pos)
{
    if (!currentLayerAcceptsFill()) {
        return false;
    }
    if (!rect().contains(pos.toPoint())) {
        return false;
    }

    const int originalLayer = m_model.currentLayer();
    const bool originalLayerIsFill = originalLayer >= 0 && m_model.isFillLayer(originalLayer);
    FillScope boundaryScope = m_fillScope;
    int sourceLayerIndex = originalLayer;
    bool allLayers = boundaryScope == FillScope::AllLayers;
    if (originalLayerIsFill && boundaryScope == FillScope::CurrentLayer) {
        boundaryScope = FillScope::AllLayers;
        allLayers = true;
    }
    if (allLayers) {
        sourceLayerIndex = -1;
    }

    const QPainterPath fillPath = vectorRegionPathAt(pos, boundaryScope, originalLayer);
    if (fillPath.isEmpty()) {
        return false;
    }

    const int assetCountBefore = m_model.assetCount();
    int targetLayer = originalLayer;
    if (!originalLayerIsFill) {
        targetLayer = m_model.addFillLayer();
    }
    if (targetLayer < 0) {
        return false;
    }
    m_model.setCurrentLayer(targetLayer);

    VectorImageModel *image = currentImage(true, AnimeColumnType::Fill);
    if (!image) {
        return false;
    }

    bool updatedExistingRegion = false;
    for (int i = image->fillCount() - 1; i >= 0; --i) {
        const AnimeVectorFillRegion &existing = image->fillRegions()[i];
        const bool sameReferMode = existing.basedOnAllLayers == allLayers;
        const bool sameSourceLayer = allLayers || existing.sourceLayerIndex == sourceLayerIndex;
        if (!sameReferMode || !sameSourceLayer || !existing.path.contains(pos)) {
            continue;
        }

        if (!updatedExistingRegion) {
            AnimeVectorFillRegion updated = existing;
            updated.seedPoint = pos;
            updated.path = fillPath;
            updated.bounds = fillPath.boundingRect();
            updated.color = m_penColor;
            image->setFillRegionAt(i, updated);
            updatedExistingRegion = true;
        } else {
            image->removeFillRegionAt(i);
        }
    }

    if (updatedExistingRegion) {
        return true;
    }

    AnimeVectorFillRegion fill;
    fill.id = image->fillCount() + 1;
    fill.seedPoint = pos;
    fill.path = fillPath;
    fill.bounds = fillPath.boundingRect();
    fill.color = m_penColor;
    fill.sourceLayerIndex = sourceLayerIndex;
    fill.basedOnAllLayers = allLayers;
    image->addFillRegion(fill);
    if (m_model.assetCount() != assetCountBefore) {
        emit assetListChanged(m_model.currentAsset());
        emit layerListChanged(m_model.currentLayer());
    }
    return true;
}

bool PaintOpenGLWidget::currentLayerAcceptsFill() const
{
    if (m_model.currentFrame() < 0) {
        return false;
    }
    if (m_model.currentLayer() < 0) {
        return true;
    }
    const AnimeColumn *column = currentColumn();
    return column && !column->locked;
}

QVector<QLineF> PaintOpenGLWidget::fillGraphSegments(FillScope scope, int layerIndex) const
{
    QVector<QLineF> segments;

    const AnimeScene &scene = m_model.scene();
    const int frame = m_model.currentFrame();
    const bool allLayers = scope == FillScope::AllLayers;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        if (!allLayers && layerIndex >= 0 && columnIndex != layerIndex) {
            continue;
        }

        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (column.type == AnimeColumnType::Fill) {
            continue;
        }
        if (!column.visible) {
            continue;
        }

        const AnimeCell cell = column.cellAt(frame);
        const VectorImageModel *image = m_model.imageForCell(cell);
        if (!image) {
            continue;
        }

        for (const VectorStrokeNode &node : image->strokeNodes()) {
            const VectorStroke &stroke = node.stroke;
            segments += segmentsFromPath(stroke.path);
        }
    }
    return segments;
}

QPainterPath PaintOpenGLWidget::vectorRegionPathAt(const QPointF &seed, FillScope scope, int layerIndex) const
{
    return vectorRegionPathAt(seed, fillGraphSegments(scope, layerIndex));
}

QPainterPath PaintOpenGLWidget::vectorRegionPathAt(const QPointF &seed, const QVector<QLineF> &segments) const
{
    if (!rect().contains(seed.toPoint())) {
        return QPainterPath();
    }

    const QVector<GraphFace> faces = computeVectorRegionFaces(segments);
    const GraphFace *bestFace = nullptr;
    qreal bestArea = std::numeric_limits<qreal>::max();
    for (const GraphFace &face : faces) {
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
    canvas.addRect(rect());

    QPainterPathStroker overpaintStroker;
    overpaintStroker.setWidth(kVectorRegionOverpaintWidth);
    overpaintStroker.setCapStyle(Qt::RoundCap);
    overpaintStroker.setJoinStyle(Qt::RoundJoin);
    return bestFace->path.united(overpaintStroker.createStroke(bestFace->path)).intersected(canvas).simplified();
}

void PaintOpenGLWidget::removeInvalidFillRegions()
{
    AnimeScene &scene = m_model.scene();

    for (AnimeLevel &level : scene.levels) {
        for (int frameId : level.frameIds()) {
            VectorImageModel *image = level.frame(frameId, false);
            if (!image || image->fillCount() == 0) {
                continue;
            }

            for (int fillIndex = image->fillCount() - 1; fillIndex >= 0; --fillIndex) {
                const AnimeVectorFillRegion fill = image->fillRegions()[fillIndex];
                if (!fill.basedOnAllLayers &&
                    (fill.sourceLayerIndex < 0 || fill.sourceLayerIndex >= scene.xsheet.columns.size())) {
                    image->removeFillRegionAt(fillIndex);
                }
            }
        }
    }
}

QPainterPath PaintOpenGLWidget::fillPathFromMask(const QPoint &seed, const QImage &boundary) const
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
    VectorImageModel *image = currentImage(false);
    if (!image || strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
        return false;
    }

    const VectorStroke stroke = image->strokeAt(strokeIndex);
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
    image->replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

bool PaintOpenGLWidget::eraseStrokeBetween(int strokeIndex, const QPointF &from, const QPointF &to)
{
    VectorImageModel *image = currentImage(false);
    if (!image || strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
        return false;
    }

    const VectorStroke stroke = image->strokeAt(strokeIndex);
    const qreal effectiveRadius = m_eraserRadius + stroke.width * 0.5;
    const qreal left = std::min(from.x(), to.x()) - effectiveRadius;
    const qreal top = std::min(from.y(), to.y()) - effectiveRadius;
    const qreal right = std::max(from.x(), to.x()) + effectiveRadius;
    const qreal bottom = std::max(from.y(), to.y()) + effectiveRadius;
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
    image->replaceStrokeWithPieces(strokeIndex, pieces);
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

PaintOpenGLWidget::VectorImageModel *PaintOpenGLWidget::currentImage(bool create, AnimeColumnType assetType)
{
    return m_model.currentImage(create, assetType);
}

AnimeColumn *PaintOpenGLWidget::currentColumn()
{
    return m_model.currentColumn();
}

const AnimeColumn *PaintOpenGLWidget::currentColumn() const
{
    return m_model.currentColumn();
}

bool PaintOpenGLWidget::currentColumnEditable() const
{
    return m_model.currentColumnEditable();
}
