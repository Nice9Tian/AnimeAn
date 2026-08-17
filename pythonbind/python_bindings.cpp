#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "python_bindings.h"

#include "algorithm/animemodel.h"
#include "algorithm/vectorlogic.h"

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QSet>

#include <cmath>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {
std::vector<AnimeSceneModel *> g_uiScenes;
AnimeSceneModel *g_currentUiScene = nullptr;
std::function<void(bool frame, bool layer, bool asset, bool widget)> g_uiRefreshCallback;
std::function<void(bool frozen)> g_uiFreezeCallback;
std::function<void(const QString &view, const QVector<AnimeanOverlayItem> &items)> g_uiOverlayCallback;
std::function<void(const QColor &color)> g_uiDrawColorCallback;
std::function<void(const QString &pad, double x, double y)> g_uiPadValueCallback;
std::function<void(const QString &op, const QString &view, const QString &label)> g_uiHistoryCallback;

// Event subscription mask pushed by python_hooks (ui.set_hook_events).
bool g_hookEventMaskValid = false;
QSet<QString> g_hookEventMask;

// Preview displacement session (ui.displace_*): base geometry plus per-vertex
// offsets for the strokes of one internal layer. Scaling rewrites that
// layer's strokes as base + scale (*) offset entirely in C++, so a pad drag
// costs Python two floats per move instead of shipping every vertex across
// the boundary.
struct DisplacementEntry {
    QVector<QPointF> base;
    QVector<QPointF> offsets;
    QColor color = QColor(0, 0, 0, 255);
    qreal width = 3.0;
};

struct DisplacementSession {
    AnimeSceneModel *model = nullptr;
    int row = -1;
    int layer = -1;
    QVector<DisplacementEntry> entries;
};

DisplacementSession g_displacement;

void requestUiRefresh(bool frame = true, bool layer = true, bool asset = true, bool widget = true)
{
    if (g_uiRefreshCallback) {
        g_uiRefreshCallback(frame, layer, asset, widget);
    }
}

void requireCurrentUiScene()
{
    if (!g_currentUiScene) {
        throw std::runtime_error("AnimeAn UI scene is not available.");
    }
}

void requestUiFreeze(bool frozen)
{
    if (g_uiFreezeCallback) {
        g_uiFreezeCallback(frozen);
    }
}

AnimeSceneModel *sceneForViewName(const QString &view)
{
    const QString wanted = view + QStringLiteral("_paint_view");
    for (AnimeSceneModel *model : g_uiScenes) {
        if (model && model->textId() == wanted) {
            return model;
        }
    }
    return nullptr;
}

void applyDisplacementScale(double scaleX, double scaleY)
{
    if (!g_displacement.model) {
        return;
    }
    // Check the (bounds-safe) internal flag BEFORE touching imageAt: even
    // with create=false, imageAt grows the column/frame tables to cover the
    // requested index, which would resurrect a ghost column right after the
    // undo that removed the session's layer.
    if (!g_displacement.model->layerInternal(g_displacement.layer)) {
        g_displacement = DisplacementSession();
        return;
    }
    AnimeVectorImageModel *image =
        g_displacement.model->imageAt(g_displacement.row, g_displacement.layer, false);
    if (!image) {
        g_displacement = DisplacementSession();
        return;
    }

    image->clear();
    int id = 1;
    for (const DisplacementEntry &entry : g_displacement.entries) {
        QVector<QPointF> points;
        points.reserve(entry.base.size());
        for (int i = 0; i < entry.base.size(); ++i) {
            points.append(entry.base[i] + QPointF(scaleX * entry.offsets[i].x(),
                                                  scaleY * entry.offsets[i].y()));
        }
        if (points.size() < 2) {
            continue;
        }
        image->addStroke(AnimeVectorLogic::makeStroke(points, entry.color, entry.width, id++,
                                                      false, false, 0));
    }
    requestUiRefresh(false, false, false, true);
}

AnimeVectorStroke makePolylineStroke(const std::vector<std::pair<double, double>> &points,
                                     int r,
                                     int g,
                                     int b,
                                     int a,
                                     double width)
{
    AnimeVectorStroke stroke;
    stroke.color = QColor(r, g, b, a);
    stroke.width = width;
    stroke.points.reserve(static_cast<int>(points.size()));
    for (const auto &point : points) {
        stroke.points.append(QPointF(point.first, point.second));
    }

    stroke.lengths.reserve(stroke.points.size());
    stroke.totalLength = 0.0;
    for (int i = 0; i < stroke.points.size(); ++i) {
        if (i > 0) {
            stroke.totalLength += QLineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }

    if (!stroke.points.isEmpty()) {
        stroke.path.moveTo(stroke.points.first());
        if (stroke.points.size() == 1) {
            stroke.path.lineTo(stroke.points.first() + QPointF(0.01, 0.01));
        } else {
            for (int i = 1; i < stroke.points.size(); ++i) {
                stroke.path.lineTo(stroke.points[i]);
            }
        }
    }
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    return stroke;
}

py::tuple rectToTuple(const QRectF &rect)
{
    return py::make_tuple(rect.x(), rect.y(), rect.width(), rect.height());
}

qreal objectToQreal(const py::handle &value, const char *name)
{
    try {
        return value.cast<qreal>();
    } catch (const py::cast_error &) {
        throw py::type_error(std::string(name) + " must be a number.");
    }
}

bool hasKey(const py::dict &dict, const char *key)
{
    return dict.contains(py::str(key));
}

QPointF objectToPointF(const py::handle &value, const char *name = "point")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "x") && hasKey(dict, "y")) {
            return QPointF(objectToQreal(dict["x"], "point.x"), objectToQreal(dict["y"], "point.y"));
        }
    }

    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        if (seq.size() >= 2) {
            return QPointF(objectToQreal(seq[0], "point[0]"), objectToQreal(seq[1], "point[1]"));
        }
    }

    throw py::type_error(std::string(name) + " must be {'x': x, 'y': y} or (x, y).");
}

QPoint objectToPoint(const py::handle &value, const char *name = "point")
{
    const QPointF point = objectToPointF(value, name);
    return QPoint(qRound(point.x()), qRound(point.y()));
}

QRectF objectToRectF(const py::handle &value, const char *name = "rect")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "x") && hasKey(dict, "y") && hasKey(dict, "width") && hasKey(dict, "height")) {
            return QRectF(objectToQreal(dict["x"], "rect.x"),
                          objectToQreal(dict["y"], "rect.y"),
                          objectToQreal(dict["width"], "rect.width"),
                          objectToQreal(dict["height"], "rect.height"));
        }
        if (hasKey(dict, "left") && hasKey(dict, "top") && hasKey(dict, "right") && hasKey(dict, "bottom")) {
            const qreal left = objectToQreal(dict["left"], "rect.left");
            const qreal top = objectToQreal(dict["top"], "rect.top");
            const qreal right = objectToQreal(dict["right"], "rect.right");
            const qreal bottom = objectToQreal(dict["bottom"], "rect.bottom");
            return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
        }
    }

    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        if (seq.size() >= 4) {
            return QRectF(objectToQreal(seq[0], "rect[0]"),
                          objectToQreal(seq[1], "rect[1]"),
                          objectToQreal(seq[2], "rect[2]"),
                          objectToQreal(seq[3], "rect[3]"));
        }
    }

    throw py::type_error(std::string(name) + " must be {'x','y','width','height'} or (x, y, width, height).");
}

QRect objectToRect(const py::handle &value, const char *name = "rect")
{
    return objectToRectF(value, name).toAlignedRect();
}

QColor objectToColor(const py::handle &value, const char *name = "color")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "r") && hasKey(dict, "g") && hasKey(dict, "b")) {
            const int alpha = hasKey(dict, "a") ? dict["a"].cast<int>() : 255;
            return QColor(dict["r"].cast<int>(), dict["g"].cast<int>(), dict["b"].cast<int>(), alpha);
        }
    }

    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        if (seq.size() == 3 || seq.size() >= 4) {
            const int alpha = seq.size() >= 4 ? seq[3].cast<int>() : 255;
            return QColor(seq[0].cast<int>(), seq[1].cast<int>(), seq[2].cast<int>(), alpha);
        }
    }

    throw py::type_error(std::string(name) + " must be {'r','g','b','a'} or (r, g, b[, a]).");
}

py::dict pointToDict(const QPointF &point)
{
    py::dict data;
    data["x"] = point.x();
    data["y"] = point.y();
    return data;
}

py::list pointsToList(const QVector<QPointF> &points)
{
    py::list data;
    for (const QPointF &point : points) {
        data.append(pointToDict(point));
    }
    return data;
}

py::dict rectToDict(const QRectF &rect)
{
    py::dict data;
    data["x"] = rect.x();
    data["y"] = rect.y();
    data["width"] = rect.width();
    data["height"] = rect.height();
    return data;
}

py::dict colorToDict(const QColor &color)
{
    py::dict data;
    data["r"] = color.red();
    data["g"] = color.green();
    data["b"] = color.blue();
    data["a"] = color.alpha();
    return data;
}

py::dict lineToDict(const QLineF &line)
{
    py::dict data;
    data["from"] = pointToDict(line.p1());
    data["to"] = pointToDict(line.p2());
    data["length"] = line.length();
    return data;
}

QVector<QPointF> objectToPoints(const py::handle &value, const char *name = "points")
{
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) {
        throw py::type_error(std::string(name) + " must be a sequence of points.");
    }

    QVector<QPointF> points;
    py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
    points.reserve(static_cast<int>(seq.size()));
    for (py::handle item : seq) {
        points.append(objectToPointF(item, "point"));
    }
    return points;
}

QLineF objectToLine(const py::handle &value, const char *name = "line")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "from") && hasKey(dict, "to")) {
            return QLineF(objectToPointF(dict["from"], "line.from"), objectToPointF(dict["to"], "line.to"));
        }
        if (hasKey(dict, "p1") && hasKey(dict, "p2")) {
            return QLineF(objectToPointF(dict["p1"], "line.p1"), objectToPointF(dict["p2"], "line.p2"));
        }
    }

    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        if (seq.size() == 2) {
            return QLineF(objectToPointF(seq[0], "line[0]"), objectToPointF(seq[1], "line[1]"));
        }
        if (seq.size() >= 4) {
            return QLineF(objectToQreal(seq[0], "line[0]"),
                          objectToQreal(seq[1], "line[1]"),
                          objectToQreal(seq[2], "line[2]"),
                          objectToQreal(seq[3], "line[3]"));
        }
    }

    throw py::type_error(std::string(name) + " must be {'from': p0, 'to': p1}, (p0, p1), or (x1, y1, x2, y2).");
}

QVector<QLineF> objectToLines(const py::handle &value, const char *name = "lines")
{
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) {
        throw py::type_error(std::string(name) + " must be a sequence of lines.");
    }

    QVector<QLineF> lines;
    py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
    lines.reserve(static_cast<int>(seq.size()));
    for (py::handle item : seq) {
        lines.append(objectToLine(item, "line"));
    }
    return lines;
}

AnimeVectorRange objectToRange(const py::handle &value, const char *name = "range")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "first") && hasKey(dict, "second")) {
            return AnimeVectorRange{objectToQreal(dict["first"], "range.first"),
                                    objectToQreal(dict["second"], "range.second")};
        }
    }
    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        if (seq.size() >= 2) {
            return AnimeVectorRange{objectToQreal(seq[0], "range[0]"), objectToQreal(seq[1], "range[1]")};
        }
    }
    throw py::type_error(std::string(name) + " must be {'first': a, 'second': b} or (a, b).");
}

QVector<AnimeVectorRange> objectToRanges(const py::handle &value, const char *name = "ranges")
{
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) {
        throw py::type_error(std::string(name) + " must be a sequence of ranges.");
    }

    QVector<AnimeVectorRange> ranges;
    py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
    ranges.reserve(static_cast<int>(seq.size()));
    for (py::handle item : seq) {
        ranges.append(objectToRange(item));
    }
    return ranges;
}

py::dict rangeToDict(const AnimeVectorRange &range)
{
    py::dict data;
    data["first"] = range.first;
    data["second"] = range.second;
    return data;
}

py::list rangesToList(const QVector<AnimeVectorRange> &ranges)
{
    py::list data;
    for (const AnimeVectorRange &range : ranges) {
        data.append(rangeToDict(range));
    }
    return data;
}

QPointF cubicPointAt(const QPointF &p0,
                     const QPointF &p1,
                     const QPointF &p2,
                     const QPointF &p3,
                     qreal t)
{
    const qreal invT = 1.0 - t;
    return p0 * (invT * invT * invT) +
           p1 * (3.0 * invT * invT * t) +
           p2 * (3.0 * invT * t * t) +
           p3 * (t * t * t);
}

int sampleCountForCurve(const QPointF &p0,
                        const QPointF &p1,
                        const QPointF &p2,
                        const QPointF &p3,
                        double polyStep)
{
    const double step = std::max(0.1, polyStep);
    const double controlNetLength = QLineF(p0, p1).length() +
                                    QLineF(p1, p2).length() +
                                    QLineF(p2, p3).length();
    return std::max(1, static_cast<int>(std::ceil(controlNetLength / step)));
}

py::list pathCommandsToList(const QPainterPath &path)
{
    py::list commands;
    QPointF current;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        const QPointF point(element.x, element.y);
        if (element.isMoveTo()) {
            py::dict command;
            command["type"] = "move";
            command["to"] = pointToDict(point);
            commands.append(command);
            current = point;
        } else if (element.isLineTo()) {
            py::dict command;
            command["type"] = "line";
            command["from"] = pointToDict(current);
            command["to"] = pointToDict(point);
            commands.append(command);
            current = point;
        } else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const QPainterPath::Element controlElement = path.elementAt(i + 1);
            const QPainterPath::Element endElement = path.elementAt(i + 2);
            const QPointF control1(element.x, element.y);
            const QPointF control2(controlElement.x, controlElement.y);
            const QPointF end(endElement.x, endElement.y);

            py::dict command;
            command["type"] = "cubic";
            command["from"] = pointToDict(current);
            command["control1"] = pointToDict(control1);
            command["control2"] = pointToDict(control2);
            command["to"] = pointToDict(end);
            commands.append(command);
            current = end;
            i += 2;
        }
    }
    return commands;
}

py::list pathToPolylines(const QPainterPath &path, double polyStep)
{
    py::list polylines;
    QVector<QPointF> currentPolyline;
    QPointF current;

    auto flushPolyline = [&]() {
        if (!currentPolyline.isEmpty()) {
            polylines.append(pointsToList(currentPolyline));
            currentPolyline.clear();
        }
    };

    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        const QPointF point(element.x, element.y);
        if (element.isMoveTo()) {
            flushPolyline();
            currentPolyline.append(point);
            current = point;
        } else if (element.isLineTo()) {
            if (currentPolyline.isEmpty()) {
                currentPolyline.append(current);
            }
            currentPolyline.append(point);
            current = point;
        } else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const QPainterPath::Element controlElement = path.elementAt(i + 1);
            const QPainterPath::Element endElement = path.elementAt(i + 2);
            const QPointF control1(element.x, element.y);
            const QPointF control2(controlElement.x, controlElement.y);
            const QPointF end(endElement.x, endElement.y);
            if (currentPolyline.isEmpty()) {
                currentPolyline.append(current);
            }
            const int count = sampleCountForCurve(current, control1, control2, end, polyStep);
            for (int sample = 1; sample <= count; ++sample) {
                const qreal t = static_cast<qreal>(sample) / count;
                currentPolyline.append(cubicPointAt(current, control1, control2, end, t));
            }
            current = end;
            i += 2;
        }
    }

    flushPolyline();
    return polylines;
}

qreal perpendicularDistance(const QPointF &point, const QPointF &lineStart, const QPointF &lineEnd)
{
    const qreal length = QLineF(lineStart, lineEnd).length();
    if (length <= 0.0) {
        return QLineF(point, lineStart).length();
    }
    const qreal numerator = std::abs((lineEnd.y() - lineStart.y()) * point.x() -
                                     (lineEnd.x() - lineStart.x()) * point.y() +
                                     lineEnd.x() * lineStart.y() -
                                     lineEnd.y() * lineStart.x());
    return numerator / length;
}

void simplifyRdpRange(const QVector<QPointF> &points,
                      int first,
                      int last,
                      qreal epsilon,
                      QVector<bool> &keep)
{
    if (last <= first + 1) {
        return;
    }

    qreal maxDistance = 0.0;
    int splitIndex = -1;
    for (int i = first + 1; i < last; ++i) {
        const qreal distance = perpendicularDistance(points[i], points[first], points[last]);
        if (distance > maxDistance) {
            maxDistance = distance;
            splitIndex = i;
        }
    }

    if (splitIndex >= 0 && maxDistance > epsilon) {
        keep[splitIndex] = true;
        simplifyRdpRange(points, first, splitIndex, epsilon, keep);
        simplifyRdpRange(points, splitIndex, last, epsilon, keep);
    }
}

QVector<QPointF> simplifyRdp(const QVector<QPointF> &points, qreal epsilon)
{
    if (epsilon <= 0.0 || points.size() <= 2) {
        return points;
    }

    QVector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;
    simplifyRdpRange(points, 0, points.size() - 1, epsilon, keep);

    QVector<QPointF> simplified;
    simplified.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            simplified.append(points[i]);
        }
    }
    return simplified;
}

QVector<QVector<QPointF>> samplePathToPolylines(const QPainterPath &path, double polyStep, double simplify)
{
    QVector<QVector<QPointF>> polylines;
    QVector<QPointF> currentPolyline;
    QPointF current;

    auto flushPolyline = [&]() {
        if (!currentPolyline.isEmpty()) {
            polylines.append(simplifyRdp(currentPolyline, simplify));
            currentPolyline.clear();
        }
    };

    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        const QPointF point(element.x, element.y);
        if (element.isMoveTo()) {
            flushPolyline();
            currentPolyline.append(point);
            current = point;
        } else if (element.isLineTo()) {
            if (currentPolyline.isEmpty()) {
                currentPolyline.append(current);
            }
            currentPolyline.append(point);
            current = point;
        } else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const QPainterPath::Element controlElement = path.elementAt(i + 1);
            const QPainterPath::Element endElement = path.elementAt(i + 2);
            const QPointF control1(element.x, element.y);
            const QPointF control2(controlElement.x, controlElement.y);
            const QPointF end(endElement.x, endElement.y);
            if (currentPolyline.isEmpty()) {
                currentPolyline.append(current);
            }
            const int count = sampleCountForCurve(current, control1, control2, end, polyStep);
            for (int sample = 1; sample <= count; ++sample) {
                const qreal t = static_cast<qreal>(sample) / count;
                currentPolyline.append(cubicPointAt(current, control1, control2, end, t));
            }
            current = end;
            i += 2;
        }
    }

    flushPolyline();
    return polylines;
}

py::list polylineVectorToList(const QVector<QVector<QPointF>> &polylines)
{
    py::list data;
    for (const QVector<QPointF> &polyline : polylines) {
        data.append(pointsToList(polyline));
    }
    return data;
}

py::object strokeLineList(const AnimeVectorStroke &stroke, bool ploy, double simplify, double polyStep = 4.0)
{
    if (ploy) {
        return pathCommandsToList(stroke.path);
    }
    return polylineVectorToList(samplePathToPolylines(stroke.path, polyStep, simplify));
}

QPainterPath objectToPath(const py::handle &value, const char *name = "path")
{
    QPainterPath path;
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) {
        throw py::type_error(std::string(name) + " must be path commands or a point sequence.");
    }

    py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
    bool pathStarted = false;
    QPointF current;
    for (py::handle item : seq) {
        if (!py::isinstance<py::dict>(item)) {
            const QPointF point = objectToPointF(item, "path point");
            if (!pathStarted) {
                path.moveTo(point);
                pathStarted = true;
            } else {
                path.lineTo(point);
            }
            current = point;
            continue;
        }

        py::dict command = py::reinterpret_borrow<py::dict>(item);
        const std::string type = hasKey(command, "type") ? command["type"].cast<std::string>() : "line";
        if (type == "move") {
            const QPointF point = objectToPointF(command["to"], "move.to");
            path.moveTo(point);
            current = point;
            pathStarted = true;
        } else if (type == "line") {
            const QPointF point = objectToPointF(command["to"], "line.to");
            if (!pathStarted) {
                if (hasKey(command, "from")) {
                    path.moveTo(objectToPointF(command["from"], "line.from"));
                } else {
                    path.moveTo(current);
                }
                pathStarted = true;
            }
            path.lineTo(point);
            current = point;
        } else if (type == "quad") {
            const QPointF control = objectToPointF(command["control"], "quad.control");
            const QPointF point = objectToPointF(command["to"], "quad.to");
            if (!pathStarted) {
                if (hasKey(command, "from")) {
                    path.moveTo(objectToPointF(command["from"], "quad.from"));
                } else {
                    path.moveTo(current);
                }
                pathStarted = true;
            }
            path.quadTo(control, point);
            current = point;
        } else if (type == "cubic") {
            const QPointF control1 = objectToPointF(command["control1"], "cubic.control1");
            const QPointF control2 = objectToPointF(command["control2"], "cubic.control2");
            const QPointF point = objectToPointF(command["to"], "cubic.to");
            if (!pathStarted) {
                if (hasKey(command, "from")) {
                    path.moveTo(objectToPointF(command["from"], "cubic.from"));
                } else {
                    path.moveTo(current);
                }
                pathStarted = true;
            }
            path.cubicTo(control1, control2, point);
            current = point;
        } else if (type == "rect") {
            path.addRect(objectToRectF(command["rect"], "rect.rect"));
            pathStarted = true;
        } else if (type == "close") {
            path.closeSubpath();
        } else {
            throw py::type_error("Unknown path command type: " + type);
        }
    }

    return path;
}

py::dict pathToDictValue(const QPainterPath &path, bool toPoly = false, double polyStep = 4.0)
{
    py::dict data;
    data["bounds"] = rectToDict(path.boundingRect());
    data["commands"] = pathCommandsToList(path);
    if (toPoly) {
        data["geometry_type"] = "polyline";
        data["poly_step"] = polyStep;
        data["polylines"] = pathToPolylines(path, polyStep);
    } else {
        data["geometry_type"] = "path";
    }
    return data;
}

py::dict strokeToDict(const AnimeVectorStroke &stroke, bool toPoly, double polyStep)
{
    py::dict data;
    data["id"] = stroke.id;
    data["property"] = stroke.property.toStdString();
    data["width"] = stroke.width;
    data["pen_style"] = stroke.penStyle;
    data["color"] = colorToDict(stroke.color);
    data["bounds"] = rectToDict(stroke.bounds);
    data["raw_points"] = pointsToList(stroke.points);
    data["total_length"] = stroke.totalLength;
    py::list lengths;
    for (qreal length : stroke.lengths) {
        lengths.append(length);
    }
    data["lengths"] = lengths;
    if (toPoly) {
        data["geometry_type"] = "polyline";
        data["poly_step"] = polyStep;
        data["polylines"] = pathToPolylines(stroke.path, polyStep);
    } else {
        data["geometry_type"] = "path";
        data["commands"] = pathCommandsToList(stroke.path);
    }
    return data;
}

py::dict strokeNodeToDict(const AnimeVectorStrokeNode &node, bool toPoly, double polyStep)
{
    const AnimeVectorStroke &stroke = node.stroke;
    py::dict data = strokeToDict(stroke, toPoly, polyStep);

    py::list groupIds;
    for (int id : node.groupId.ids) {
        groupIds.append(id);
    }
    data["group_id"] = groupIds;
    data["is_point"] = node.isPoint;
    data["is_new_for_fill"] = node.isNewForFill;
    data["selected"] = node.selected;

    if (toPoly) {
        data["geometry_type"] = "polyline";
        data["poly_step"] = polyStep;
        data["polylines"] = pathToPolylines(stroke.path, polyStep);
    } else {
        data["geometry_type"] = "path";
        data["commands"] = pathCommandsToList(stroke.path);
    }

    return data;
}

QImage objectToMaskImage(const py::handle &value, const char *name = "boundary")
{
    if (py::isinstance<py::dict>(value)) {
        py::dict dict = py::reinterpret_borrow<py::dict>(value);
        if (hasKey(dict, "width") && hasKey(dict, "height") && hasKey(dict, "pixels")) {
            const int width = dict["width"].cast<int>();
            const int height = dict["height"].cast<int>();
            QImage image(width, height, QImage::Format_Grayscale8);
            image.fill(0);
            py::sequence pixels = dict["pixels"].cast<py::sequence>();
            for (int y = 0; y < height && y < pixels.size(); ++y) {
                py::sequence row = pixels[y].cast<py::sequence>();
                for (int x = 0; x < width && x < row.size(); ++x) {
                    image.setPixelColor(x, y, QColor(row[x].cast<int>(), row[x].cast<int>(), row[x].cast<int>()));
                }
            }
            return image;
        }
    }

    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence rows = py::reinterpret_borrow<py::sequence>(value);
        const int height = static_cast<int>(rows.size());
        const int width = height > 0 ? static_cast<int>(rows[0].cast<py::sequence>().size()) : 0;
        QImage image(width, height, QImage::Format_Grayscale8);
        image.fill(0);
        for (int y = 0; y < height; ++y) {
            py::sequence row = rows[y].cast<py::sequence>();
            for (int x = 0; x < width && x < row.size(); ++x) {
                const int gray = row[x].cast<int>();
                image.setPixelColor(x, y, QColor(gray, gray, gray));
            }
        }
        return image;
    }

    throw py::type_error(std::string(name) + " must be a 2D grayscale list or {'width','height','pixels'}.");
}

py::dict fillRegionToDict(const AnimeVectorFillRegion &fill)
{
    py::dict data;
    data["id"] = fill.id;
    data["property"] = fill.property.toStdString();
    data["seed"] = pointToDict(fill.seedPoint);
    data["color"] = colorToDict(fill.color);
    data["bounds"] = rectToDict(fill.bounds);
    data["source_layer_index"] = fill.sourceLayerIndex;
    data["based_on_all_layers"] = fill.basedOnAllLayers;
    data["commands"] = pathCommandsToList(fill.path);
    return data;
}

py::dict rasterToDict(const AnimeVectorImageModel *image)
{
    py::dict data;
    data["empty"] = image == nullptr || !image->hasRaster();
    if (!image || !image->hasRaster()) {
        return data;
    }

    const AnimeRasterImage &raster = image->raster();
    data["bounds"] = rectToDict(raster.bounds());
    data["top_left"] = pointToDict(raster.topLeft);
    data["width"] = raster.image.width();
    data["height"] = raster.image.height();
    return data;
}

const char *columnTypeToString(AnimeColumnType type)
{
    switch (type) {
    case AnimeColumnType::Raster:
        return "raster";
    case AnimeColumnType::Fill:
        return "fill";
    case AnimeColumnType::Vector:
    default:
        return "vector";
    }
}

AnimeColumnType columnTypeFromString(const QString &type)
{
    if (type == QStringLiteral("raster")) {
        return AnimeColumnType::Raster;
    }
    if (type == QStringLiteral("fill")) {
        return AnimeColumnType::Fill;
    }
    return AnimeColumnType::Vector;
}

py::dict imageToDict(const AnimeVectorImageModel *image, bool toPoly, double polyStep)
{
    py::dict data;
    data["empty"] = image == nullptr;
    data["stroke_count"] = image ? image->strokeCount() : 0;
    data["fill_count"] = image ? image->fillCount() : 0;
    data["has_raster"] = image ? image->hasRaster() : false;
    data["bounds"] = image ? rectToDict(image->bounds()) : py::dict();
    data["raster"] = rasterToDict(image);

    py::list strokes;
    py::list fills;
    if (image) {
        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            strokes.append(strokeNodeToDict(node, toPoly, polyStep));
        }
        for (const AnimeVectorFillRegion &fill : image->fillRegions()) {
            fills.append(fillRegionToDict(fill));
        }
    }
    data["strokes"] = strokes;
    data["fills"] = fills;
    return data;
}

py::dict cellToDict(const AnimeSceneModel &model, int layerIndex, int frameIndex, bool toPoly, double polyStep)
{
    const AnimeCell cell = model.cellAt(frameIndex, layerIndex);
    py::dict data;
    data["layer_index"] = layerIndex;
    data["frame_index"] = frameIndex;
    data["asset_index"] = cell.assetIndex;
    data["frame_id"] = cell.frameId;
    data["empty"] = cell.isEmpty();

    const AnimeVectorImageModel *image = model.imageForCell(cell);
    data["image"] = imageToDict(image, toPoly, polyStep);
    return data;
}

py::dict cellStructureToDict(const AnimeSceneModel &model, int layerIndex, int frameIndex)
{
    const AnimeCell cell = model.cellAt(frameIndex, layerIndex);
    const AnimeVectorImageModel *image = model.imageForCell(cell);

    py::dict data;
    data["layer_index"] = layerIndex;
    data["frame_index"] = frameIndex;
    data["asset_index"] = cell.assetIndex;
    data["frame_id"] = cell.frameId;
    data["empty"] = cell.isEmpty();
    data["asset_name"] = cell.isEmpty() ? "" : model.assetName(cell.assetIndex).toStdString();
    data["asset_type"] = cell.isEmpty() ? "" : columnTypeToString(model.assetType(cell.assetIndex));
    data["stroke_count"] = image ? image->strokeCount() : 0;
    data["fill_count"] = image ? image->fillCount() : 0;
    data["bounds"] = image ? rectToDict(image->bounds()) : py::dict();
    return data;
}

py::dict structureToDict(const AnimeSceneModel &model)
{
    py::dict data;
    data["sceneName"] = model.textId().toStdString();
    data["sceneId"] = model.intId();
    data["scene_id"] = model.textId().toStdString();
    data["current_frame"] = model.currentFrame();
    data["current_layer"] = model.currentLayer();
    data["current_asset"] = model.currentAsset();
    data["frame_count"] = model.frameCount();
    data["layer_count"] = model.layerCount();
    data["asset_count"] = model.assetCount();

    py::list frames;
    for (int frameIndex = 0; frameIndex < model.frameCount(); ++frameIndex) {
        py::dict frame;
        frame["index"] = frameIndex;
        frame["num"] = frameIndex + 1;
        frame["name"] = model.frameName(frameIndex).toStdString();
        frames.append(frame);
    }
    data["frames"] = frames;

    py::list layers;
    const AnimeScene &scene = model.scene();
    for (int layerIndex = 0; layerIndex < model.layerCount(); ++layerIndex) {
        py::dict layer;
        layer["index"] = layerIndex;
        layer["num"] = layerIndex + 1;
        layer["column_name"] = scene.xsheet.columns[layerIndex].name.toStdString();
        layer["name"] = model.layerName(layerIndex).toStdString();
        layer["visible"] = model.layerVisible(layerIndex);
        layer["internal"] = model.layerInternal(layerIndex);
        layer["locked"] = model.layerLocked(layerIndex);
        layer["opacity"] = model.layerOpacity(layerIndex);
        layer["type"] = columnTypeToString(model.layerType(layerIndex));

        py::list cells;
        for (int frameIndex = 0; frameIndex < model.frameCount(); ++frameIndex) {
            cells.append(cellStructureToDict(model, layerIndex, frameIndex));
        }
        layer["cells"] = cells;
        layers.append(layer);
    }
    data["layers"] = layers;

    py::list assets;
    for (int assetIndex = 0; assetIndex < model.assetCount(); ++assetIndex) {
        py::dict asset;
        asset["index"] = assetIndex;
        asset["num"] = assetIndex + 1;
        asset["name"] = model.assetName(assetIndex).toStdString();
        asset["type"] = columnTypeToString(model.assetType(assetIndex));
        asset["internal"] = model.assetInternal(assetIndex);
        assets.append(asset);
    }
    data["assets"] = assets;
    return data;
}
}

py::dict sceneInfoToDict(AnimeSceneModel *model)
{
    py::dict data;
    if (!model) {
        return data;
    }
    data["scene"] = py::cast(model, py::return_value_policy::reference);
    data["sceneName"] = model->textId().toStdString();
    data["sceneId"] = model->intId();
    return data;
}

void registerAnimeanUiScene(AnimeSceneModel *model)
{
    if (!model) {
        return;
    }

    if (std::find(g_uiScenes.begin(), g_uiScenes.end(), model) == g_uiScenes.end()) {
        g_uiScenes.push_back(model);
    }
    g_currentUiScene = model;
}

void unregisterAnimeanUiScene(AnimeSceneModel *model)
{
    g_uiScenes.erase(std::remove(g_uiScenes.begin(), g_uiScenes.end(), model), g_uiScenes.end());
    if (g_currentUiScene == model) {
        g_currentUiScene = g_uiScenes.empty() ? nullptr : g_uiScenes.back();
    }
}

void registerAnimeanUiRefreshCallback(std::function<void(bool frame, bool layer, bool asset, bool widget)> callback)
{
    g_uiRefreshCallback = std::move(callback);
}

void clearAnimeanUiRefreshCallback()
{
    g_uiRefreshCallback = nullptr;
}

void registerAnimeanUiFreezeCallback(std::function<void(bool frozen)> callback)
{
    g_uiFreezeCallback = std::move(callback);
}

void clearAnimeanUiFreezeCallback()
{
    g_uiFreezeCallback = nullptr;
}

void registerAnimeanUiOverlayCallback(std::function<void(const QString &view, const QVector<AnimeanOverlayItem> &items)> callback)
{
    g_uiOverlayCallback = std::move(callback);
}

void clearAnimeanUiOverlayCallback()
{
    g_uiOverlayCallback = nullptr;
}

void registerAnimeanUiDrawColorCallback(std::function<void(const QColor &color)> callback)
{
    g_uiDrawColorCallback = std::move(callback);
}

void clearAnimeanUiDrawColorCallback()
{
    g_uiDrawColorCallback = nullptr;
}

void registerAnimeanUiPadValueCallback(std::function<void(const QString &pad, double x, double y)> callback)
{
    g_uiPadValueCallback = std::move(callback);
}

void clearAnimeanUiPadValueCallback()
{
    g_uiPadValueCallback = nullptr;
}

bool animeanHookEventSubscribed(const QString &event)
{
    return !g_hookEventMaskValid || g_hookEventMask.contains(event);
}

void registerAnimeanUiHistoryCallback(std::function<void(const QString &op, const QString &view, const QString &label)> callback)
{
    g_uiHistoryCallback = std::move(callback);
}

void clearAnimeanUiHistoryCallback()
{
    g_uiHistoryCallback = nullptr;
}

void bindAnimeanPythonModule(py::module_ &m)
{
    m.doc() = "Python bindings for AnimeAn scene, layer, frame, and vector image models.";

    m.def("get_scene", []() {
        py::list scenes;
        for (AnimeSceneModel *model : g_uiScenes) {
            if (model) {
                scenes.append(sceneInfoToDict(model));
            }
        }
        return scenes;
    });

    m.def("get_current", []() -> py::object {
        if (!g_currentUiScene) {
            return py::none();
        }

        py::dict current = sceneInfoToDict(g_currentUiScene);
        if (g_currentUiScene->currentFrame() >= 0) {
            current["frame"] = g_currentUiScene->currentFrame();
        } else {
            current["frame"] = py::none();
        }
        if (g_currentUiScene->currentLayer() >= 0) {
            current["layer"] = g_currentUiScene->currentLayer();
        } else {
            current["layer"] = py::none();
        }
        if (g_currentUiScene->currentAsset() >= 0) {
            current["asset"] = g_currentUiScene->currentAsset();
        } else {
            current["asset"] = py::none();
        }
        return current;
    });

    m.def("unregister_scene", [](AnimeSceneModel &model) {
        unregisterAnimeanUiScene(&model);
    });

    py::module_ ui = m.def_submodule("ui", "Helpers for synchronizing the embedded AnimeAn user interface.");
    ui.def("refresh", []() {
        requestUiRefresh(true, true, true, true);
    });
    ui.def("set_overlay",
           [](const std::string &view, py::sequence items) {
               if (!g_uiOverlayCallback) {
                   return;
               }
               QVector<AnimeanOverlayItem> converted;
               for (py::handle handle : items) {
                   if (!py::isinstance<py::dict>(handle)) {
                       throw py::type_error("overlay items must be dicts.");
                   }
                   py::dict data = py::reinterpret_borrow<py::dict>(handle);
                   AnimeanOverlayItem item;
                   if (hasKey(data, "id")) {
                       item.id = QString::fromStdString(data["id"].cast<std::string>());
                   }
                   item.points = objectToPoints(data["points"], "overlay.points");
                   if (hasKey(data, "closed")) {
                       item.closed = data["closed"].cast<bool>();
                   }
                   if (hasKey(data, "color")) {
                       item.strokeColor = objectToColor(data["color"], "overlay.color");
                   }
                   if (hasKey(data, "fill_color")) {
                       item.fillColor = objectToColor(data["fill_color"], "overlay.fill_color");
                   }
                   if (hasKey(data, "width")) {
                       item.width = data["width"].cast<double>();
                   }
                   if (hasKey(data, "removable")) {
                       item.removable = data["removable"].cast<bool>();
                   }
                   converted.append(item);
               }
               g_uiOverlayCallback(QString::fromStdString(view), converted);
           },
           py::arg("view"),
           py::arg("items"));
    ui.def("set_draw_color", [](py::object color) {
        if (g_uiDrawColorCallback) {
            g_uiDrawColorCallback(objectToColor(color, "color"));
        }
    });
    ui.def("set_pad_value",
           [](const std::string &pad, double x, double y) {
               // Generic: move a named vector pad's handle (no signals fire).
               // Tools use it to recenter a latched pad when the state the
               // held vector referred to no longer exists.
               if (g_uiPadValueCallback) {
                   g_uiPadValueCallback(QString::fromStdString(pad), x, y);
               }
           },
           py::arg("pad"),
           py::arg("x") = 0.0,
           py::arg("y") = 0.0);
    ui.def("set_hook_events",
           [](py::sequence events) {
               // python_hooks pushes the set of events that currently have at
               // least one hook; C++ dispatch sites skip everything else.
               QSet<QString> mask;
               for (py::handle handle : events) {
                   mask.insert(QString::fromStdString(py::str(handle).cast<std::string>()));
               }
               g_hookEventMask = std::move(mask);
               g_hookEventMaskValid = true;
           },
           py::arg("events"));
    ui.def("displace_begin",
           [](const std::string &view, int row, int layerIndex, py::sequence entries,
              double scaleX, double scaleY) {
               AnimeSceneModel *model = sceneForViewName(QString::fromStdString(view));
               if (!model) {
                   throw std::runtime_error("displace_begin: unknown view name.");
               }
               if (!model->layerInternal(layerIndex)) {
                   throw std::runtime_error("displace_begin requires an internal (script-owned) layer.");
               }
               DisplacementSession session;
               session.model = model;
               session.row = row;
               session.layer = layerIndex;
               for (py::handle handle : entries) {
                   if (!py::isinstance<py::dict>(handle)) {
                       throw py::type_error("displacement entries must be dicts.");
                   }
                   py::dict data = py::reinterpret_borrow<py::dict>(handle);
                   DisplacementEntry entry;
                   entry.base = objectToPoints(data["points"], "displace.points");
                   entry.offsets = objectToPoints(data["offsets"], "displace.offsets");
                   if (entry.offsets.size() != entry.base.size()) {
                       throw py::value_error("displacement entry offsets must match points 1:1.");
                   }
                   if (hasKey(data, "color")) {
                       entry.color = objectToColor(data["color"], "displace.color");
                   }
                   if (hasKey(data, "width")) {
                       entry.width = data["width"].cast<double>();
                   }
                   session.entries.append(entry);
               }
               g_displacement = std::move(session);
               applyDisplacementScale(scaleX, scaleY);
           },
           py::arg("view"),
           py::arg("row"),
           py::arg("layer_index"),
           py::arg("entries"),
           py::arg("scale_x") = 0.0,
           py::arg("scale_y") = 0.0);
    ui.def("displace_scale",
           [](double x, double y) {
               applyDisplacementScale(x, y);
           },
           py::arg("x"),
           py::arg("y"));
    ui.def("displace_end", []() {
        g_displacement = DisplacementSession();
    });
    ui.def("history_commit",
           [](const std::string &label, const std::string &view) {
               if (g_uiHistoryCallback) {
                   g_uiHistoryCallback(QStringLiteral("commit"),
                                       QString::fromStdString(view),
                                       QString::fromStdString(label));
               }
           },
           py::arg("label"),
           py::arg("view") = "");
    ui.def("history_undo",
           [](const std::string &view) {
               if (g_uiHistoryCallback) {
                   g_uiHistoryCallback(QStringLiteral("undo"), QString::fromStdString(view), QString());
               }
           },
           py::arg("view") = "");
    ui.def("history_redo",
           [](const std::string &view) {
               if (g_uiHistoryCallback) {
                   g_uiHistoryCallback(QStringLiteral("redo"), QString::fromStdString(view), QString());
               }
           },
           py::arg("view") = "");
    ui.def("freeze", []() {
        requestUiFreeze(true);
    });
    ui.def("unfreeze", []() {
        requestUiFreeze(false);
    });
    ui.def("set_current",
           [](py::object frame, py::object layer, py::object asset) {
               requireCurrentUiScene();
               const bool updateFrame = !frame.is_none();
               const bool updateLayer = !layer.is_none();
               const bool updateAsset = !asset.is_none();
               if (updateFrame) {
                   g_currentUiScene->setCurrentFrame(frame.cast<int>());
               }
               if (updateLayer) {
                   g_currentUiScene->setCurrentLayer(layer.cast<int>());
               }
               if (updateAsset) {
                   g_currentUiScene->setCurrentAsset(asset.cast<int>());
               }
               requestUiRefresh(true, true, true, true);
           },
           py::arg("frame") = py::none(),
           py::arg("layer") = py::none(),
           py::arg("asset") = py::none());

    py::module_ uiMain = ui.def_submodule("main", "Refresh all AnimeAn UI surfaces.");
    uiMain.def("refresh", []() {
        requestUiRefresh(true, true, true, true);
    });

    py::module_ uiChildren = ui.def_submodule("children", "Refresh child panels.");
    uiChildren.def("refresh", []() {
        requestUiRefresh(true, true, true, false);
    });

    py::module_ uiFrame = ui.def_submodule("frame", "Refresh the frame panel.");
    uiFrame.def("refresh", []() {
        requestUiRefresh(true, false, false, false);
    });

    py::module_ uiLayer = ui.def_submodule("layer", "Refresh the layer panel.");
    uiLayer.def("refresh", []() {
        requestUiRefresh(false, true, false, false);
    });

    py::module_ uiAsset = ui.def_submodule("asset", "Refresh the asset panel.");
    uiAsset.def("refresh", []() {
        requestUiRefresh(false, false, true, false);
    });

    py::module_ uiWidget = ui.def_submodule("widget", "Refresh the drawing widget.");
    uiWidget.def("refresh", []() {
        requestUiRefresh(false, false, false, true);
    });

    py::class_<AnimeVectorRange>(m, "VectorRange")
        .def(py::init<>())
        .def(py::init<qreal, qreal>())
        .def_readwrite("first", &AnimeVectorRange::first)
        .def_readwrite("second", &AnimeVectorRange::second)
        .def("to_dict", [](const AnimeVectorRange &range) {
            return rangeToDict(range);
        });

    py::class_<AnimeCell>(m, "Cell")
        .def(py::init<>())
        .def_readwrite("asset_index", &AnimeCell::assetIndex)
        .def_readwrite("frame_id", &AnimeCell::frameId)
        .def("is_empty", &AnimeCell::isEmpty);

    py::class_<AnimeVectorStroke>(m, "VectorStroke")
        .def(py::init<>())
        .def_readwrite("id", &AnimeVectorStroke::id)
        .def_property("property",
                      [](const AnimeVectorStroke &stroke) {
                          return stroke.property.toStdString();
                      },
                      [](AnimeVectorStroke &stroke, const std::string &property) {
                          stroke.property = QString::fromUtf8(property.c_str());
                      })
        .def_readwrite("width", &AnimeVectorStroke::width)
        .def_readwrite("pen_style", &AnimeVectorStroke::penStyle)
        .def_readwrite("total_length", &AnimeVectorStroke::totalLength)
        .def("to_dict",
             [](const AnimeVectorStroke &stroke, bool toPoly, double polyStep) {
                 return strokeToDict(stroke, toPoly, polyStep);
             },
             py::arg("to_poly") = false,
             py::arg("poly_step") = 4.0)
        .def("line_list",
             [](const AnimeVectorStroke &stroke, bool ploy, double simplify) {
                 return strokeLineList(stroke, ploy, simplify);
             },
             py::arg("ploy") = false,
             py::arg("simplify") = 0.0);

    py::class_<AnimeVectorImageModel>(m, "VectorImage")
        .def("stroke_count", &AnimeVectorImageModel::strokeCount)
        .def("fill_count", &AnimeVectorImageModel::fillCount)
        .def("clear", &AnimeVectorImageModel::clear)
        .def("remove_stroke", &AnimeVectorImageModel::removeStrokeAt)
        .def("remove_fill_area", &AnimeVectorImageModel::removeFillRegionAt)
        .def("set_fill_color",
             [](AnimeVectorImageModel &image, int index, py::object color) {
                 return image.setFillRegionColor(index, objectToColor(color));
             },
             py::arg("index"),
             py::arg("color"))
        .def("fill_regions_info", [](const AnimeVectorImageModel &image) {
            py::list regions;
            const QVector<AnimeVectorFillRegion> &fills = image.fillRegions();
            for (int i = 0; i < fills.size(); ++i) {
                const AnimeVectorFillRegion &fill = fills[i];
                py::dict data;
                data["index"] = i;
                data["id"] = fill.id;
                data["property"] = fill.property.toStdString();
                data["seed"] = pointToDict(fill.seedPoint);
                data["color"] = colorToDict(fill.color);
                data["source_layer_index"] = fill.sourceLayerIndex;
                data["based_on_all_layers"] = fill.basedOnAllLayers;
                regions.append(data);
            }
            return regions;
        })
        .def("fill_region_contains",
             [](const AnimeVectorImageModel &image, int index, py::object point) {
                 const QVector<AnimeVectorFillRegion> &fills = image.fillRegions();
                 if (index < 0 || index >= fills.size()) {
                     return false;
                 }
                 return fills[index].path.contains(objectToPointF(point, "point"));
             },
             py::arg("index"),
             py::arg("point"))
        .def("add_fill_region",
             [](AnimeVectorImageModel &image, py::object path, py::object color,
                const std::string &property, py::object seed, int sourceLayerIndex,
                bool basedOnAllLayers) {
                 AnimeVectorFillRegion fill;
                 fill.id = image.fillCount() + 1;
                 fill.property = QString::fromUtf8(property.c_str());
                 fill.path = objectToPath(path, "path");
                 fill.bounds = fill.path.boundingRect();
                 fill.color = objectToColor(color, "color");
                 if (!seed.is_none()) {
                     fill.seedPoint = objectToPointF(seed, "seed");
                 }
                 fill.sourceLayerIndex = sourceLayerIndex;
                 fill.basedOnAllLayers = basedOnAllLayers;
                 image.addFillRegion(fill);
                 return image.fillCount() - 1;
             },
             py::arg("path"),
             py::arg("color"),
             py::arg("property") = "",
             py::arg("seed") = py::none(),
             py::arg("source_layer_index") = -1,
             py::arg("based_on_all_layers") = false)
        .def("set_fill_region",
             [](AnimeVectorImageModel &image, int index, py::object path, py::object color,
                py::object seed) {
                 const QVector<AnimeVectorFillRegion> &fills = image.fillRegions();
                 if (index < 0 || index >= fills.size()) {
                     return false;
                 }
                 AnimeVectorFillRegion updated = fills[index];
                 if (!path.is_none()) {
                     updated.path = objectToPath(path, "path");
                     updated.bounds = updated.path.boundingRect();
                 }
                 if (!color.is_none()) {
                     updated.color = objectToColor(color, "color");
                 }
                 if (!seed.is_none()) {
                     updated.seedPoint = objectToPointF(seed, "seed");
                 }
                 return image.setFillRegionAt(index, updated);
             },
             py::arg("index"),
             py::arg("path") = py::none(),
             py::arg("color") = py::none(),
             py::arg("seed") = py::none())
        .def("clear_raster", &AnimeVectorImageModel::clearRasterImage)
        .def("bounds", [](const AnimeVectorImageModel &image) {
            return rectToTuple(image.bounds());
        })
        .def("to_dict",
             [](const AnimeVectorImageModel &image, bool toPoly, double polyStep) {
                 return imageToDict(&image, toPoly, polyStep);
             },
             py::arg("to_poly") = false,
             py::arg("poly_step") = 4.0)
        .def("add_polyline",
             [](AnimeVectorImageModel &image,
                const std::vector<std::pair<double, double>> &points,
                int r,
                int g,
                int b,
                int a,
                double width) {
                 image.addStroke(makePolylineStroke(points, r, g, b, a, width));
             },
             py::arg("points"),
             py::arg("r") = 0,
             py::arg("g") = 0,
             py::arg("b") = 0,
             py::arg("a") = 255,
             py::arg("width") = 3.0)
        .def("add_stroke_object", [](AnimeVectorImageModel &image, const AnimeVectorStroke &stroke) {
            image.addStroke(stroke);
        })
        .def("replace_stroke_with_pieces",
             [](AnimeVectorImageModel &image, int index, py::sequence pieces) {
                 // Generic in-place geometry swap: keeps the stroke's position
                 // in the draw order (z) instead of remove+append. Returns the
                 // number of pieces actually inserted (-1 = invalid index) so
                 // callers can detect silent drops of degenerate pieces.
                 if (pieces.size() == 0) {
                     // An empty list would silently DELETE the stroke; force
                     // callers to say what they mean with remove_stroke().
                     throw py::value_error("replace_stroke_with_pieces: pieces is empty; use remove_stroke() to delete.");
                 }
                 QVector<AnimeVectorStroke> strokes;
                 strokes.reserve(static_cast<int>(pieces.size()));
                 for (py::handle piece : pieces) {
                     strokes.append(piece.cast<AnimeVectorStroke>());
                 }
                 return image.replaceStrokeWithPieces(index, strokes);
             },
             py::arg("index"),
             py::arg("pieces"));

    py::class_<AnimeSceneModel>(m, "SceneModel")
        .def(py::init<>())
        .def("id", [](const AnimeSceneModel &model) {
            return model.id().toStdString();
        })
        .def("set_id", [](AnimeSceneModel &model, const std::string &id) {
            model.setId(QString::fromUtf8(id.c_str()));
        })
        .def("scene_name", [](const AnimeSceneModel &model) {
            return model.textId().toStdString();
        })
        .def("set_scene_name", [](AnimeSceneModel &model, const std::string &name) {
            model.setTextId(QString::fromUtf8(name.c_str()));
        })
        .def("scene_id", &AnimeSceneModel::intId)
        .def("set_scene_id", &AnimeSceneModel::setIntId)
        .def("script_data", [](const AnimeSceneModel &model) {
            return model.scriptData().toStdString();
        })
        .def("set_script_data", [](AnimeSceneModel &model, const std::string &data) {
            model.setScriptData(QString::fromStdString(data));
        })
        .def("initialize_scene", &AnimeSceneModel::initializeScene, py::arg("layer_count"), py::arg("frame_count"))
        .def("set_current_layer", &AnimeSceneModel::setCurrentLayer)
        .def("set_current_frame", &AnimeSceneModel::setCurrentFrame)
        .def("set_current_asset", &AnimeSceneModel::setCurrentAsset)
        .def("current_layer", &AnimeSceneModel::currentLayer)
        .def("current_frame", &AnimeSceneModel::currentFrame)
        .def("current_asset", &AnimeSceneModel::currentAsset)
        .def("layer_count", &AnimeSceneModel::layerCount)
        .def("frame_count", &AnimeSceneModel::frameCount)
        .def("asset_count", &AnimeSceneModel::assetCount)
        .def("get_structure", [](const AnimeSceneModel &model) {
            return structureToDict(model);
        })
        .def("layer_name", [](const AnimeSceneModel &model, int layerIndex) {
            return model.layerName(layerIndex).toStdString();
        })
        .def("set_layer_name", [](AnimeSceneModel &model, int layerIndex, const std::string &name) {
            model.setLayerName(layerIndex, QString::fromUtf8(name.c_str()));
        })
        .def("frame_name", [](const AnimeSceneModel &model, int frameIndex) {
            return model.frameName(frameIndex).toStdString();
        })
        .def("asset_name", [](const AnimeSceneModel &model, int assetIndex) {
            return model.assetName(assetIndex).toStdString();
        })
        .def("set_asset_name", [](AnimeSceneModel &model, int assetIndex, const std::string &name) {
            model.setAssetName(assetIndex, QString::fromUtf8(name.c_str()));
        })
        .def("layer_visible", &AnimeSceneModel::layerVisible)
        .def("set_layer_visible", &AnimeSceneModel::setLayerVisible)
        .def("layer_internal", &AnimeSceneModel::layerInternal)
        .def("set_layer_internal", &AnimeSceneModel::setLayerInternal)
        .def("asset_internal", &AnimeSceneModel::assetInternal)
        .def("set_asset_internal", &AnimeSceneModel::setAssetInternal)
        .def("layer_locked", &AnimeSceneModel::layerLocked)
        .def("set_layer_locked", &AnimeSceneModel::setLayerLocked)
        .def("layer_opacity", &AnimeSceneModel::layerOpacity)
        .def("set_layer_opacity", &AnimeSceneModel::setLayerOpacity)
        .def("add_layer", &AnimeSceneModel::addLayer)
        .def("add_fill_layer", &AnimeSceneModel::addFillLayer)
        .def("add_asset",
             [](AnimeSceneModel &model, const std::string &type, const std::string &name) {
                 return model.addAsset(columnTypeFromString(QString::fromUtf8(type.c_str())),
                                       QString::fromUtf8(name.c_str()));
             },
             py::arg("type") = "vector",
             py::arg("name") = "")
        .def("delete_layer", &AnimeSceneModel::deleteLayer)
        .def("delete_asset", &AnimeSceneModel::deleteAsset)
        .def("remap_fill_source_layers_after_delete", &AnimeSceneModel::remapFillSourceLayersAfterDelete)
        .def("remap_fill_source_layers_after_move", &AnimeSceneModel::remapFillSourceLayersAfterMove)
        .def("move_layer", &AnimeSceneModel::moveLayer)
        // --- layer groups -------------------------------------------------
        // The tree is handed to Python in the nested form the C++ model uses,
        // but with layer INDICES rather than internal column ids, so it lines
        // up with every other layer call here (set_layer_visible, move_layer,
        // get_structure, ...). An element that is an int IS a layer; an
        // element that is a dict is a group and carries its children.
        .def("layer_tree", [](const AnimeSceneModel &model) {
            std::function<py::list(const QVector<AnimeLayerNode> &)> convert =
                [&](const QVector<AnimeLayerNode> &nodes) {
                py::list out;
                for (const AnimeLayerNode &node : nodes) {
                    if (node.isGroup()) {
                        py::dict group;
                        group["group"] = node.groupId;
                        group["name"] = node.name.toStdString();
                        group["collapsed"] = node.collapsed;
                        group["children"] = convert(node.children);
                        out.append(group);
                        continue;
                    }
                    const int index = model.layerIndexForId(node.layerId);
                    if (index >= 0) {
                        out.append(index);
                    }
                }
                return out;
            };
            return convert(model.layerTree());
        })
        .def("create_layer_group",
             [](AnimeSceneModel &model, const std::string &name,
                const std::vector<int> &layers, const std::vector<int> &groups, bool collapsed) {
                 QVector<int> layerIndices;
                 for (int index : layers) {
                     layerIndices.append(index);
                 }
                 QVector<int> groupIds;
                 for (int id : groups) {
                     groupIds.append(id);
                 }
                 return model.createLayerGroup(QString::fromUtf8(name.c_str()),
                                               layerIndices, groupIds, collapsed);
             },
             py::arg("name"),
             py::arg("layers") = std::vector<int>{},
             py::arg("groups") = std::vector<int>{},
             py::arg("collapsed") = false)
        .def("set_layer_group_collapsed", &AnimeSceneModel::setLayerGroupCollapsed)
        .def("layer_group_collapsed", &AnimeSceneModel::layerGroupCollapsed)
        .def("set_layer_group_name",
             [](AnimeSceneModel &model, int groupId, const std::string &name) {
                 return model.setLayerGroupName(groupId, QString::fromUtf8(name.c_str()));
             })
        .def("dissolve_layer_group", &AnimeSceneModel::dissolveLayerGroup)
        .def("delete_layer_group", &AnimeSceneModel::deleteLayerGroup)
        .def("canvas_size", [](const AnimeSceneModel &model) {
            return py::make_tuple(model.canvasSize().width(), model.canvasSize().height());
        })
        .def("set_canvas_size", [](AnimeSceneModel &model, int width, int height) {
            model.setCanvasSize(QSize(width, height));
        }, py::arg("width"), py::arg("height"))
        .def("layer_ids_in_group",
             [](const AnimeSceneModel &model, int groupId) {
                 py::list ids;
                 for (int id : model.layerIdsInGroup(groupId)) {
                     ids.append(id);
                 }
                 return ids;
             })
        .def("layer_id_at", &AnimeSceneModel::layerIdAt)
        .def("layer_index_for_id", &AnimeSceneModel::layerIndexForId)
        .def("add_frame", &AnimeSceneModel::addFrame)
        .def("delete_frame", &AnimeSceneModel::deleteFrame)
        .def("move_frame", &AnimeSceneModel::moveFrame)
        .def("cell_at", &AnimeSceneModel::cellAt)
        .def("set_cell", &AnimeSceneModel::setCell)
        .def("clear_cell", &AnimeSceneModel::clearCell)
        .def("image_at",
             [](AnimeSceneModel &model, int row, int layerIndex, bool create, const std::string &assetType) {
                 return model.imageAt(row, layerIndex, create,
                                      columnTypeFromString(QString::fromUtf8(assetType.c_str())));
             },
             py::arg("row"),
             py::arg("layer_index"),
             py::arg("create") = false,
             py::arg("asset_type") = "vector",
             py::return_value_policy::reference_internal)
        .def("current_image",
             [](AnimeSceneModel &model, bool create) {
                 return model.currentImage(create, AnimeColumnType::Vector);
             },
             py::arg("create") = false,
             py::return_value_policy::reference_internal)
        .def("asset_image",
             [](AnimeSceneModel &model, int assetIndex, int frameId, bool create) {
                 return model.assetImage(assetIndex, frameId, create);
             },
             py::arg("asset_index"),
             py::arg("frame_id") = 1,
             py::arg("create") = false,
             py::return_value_policy::reference_internal)
        .def("cell_asset_index", &AnimeSceneModel::assetIndexAt, py::arg("row"), py::arg("layer_index"))
        .def("stroke_count", [](const AnimeSceneModel &model, int row, int layerIndex) {
            const AnimeVectorImageModel *image = model.imageAt(row, layerIndex);
            return image ? image->strokeCount() : 0;
        })
        .def("clear_image", [](AnimeSceneModel &model, int row, int layerIndex) {
            AnimeVectorImageModel *image = model.imageAt(row, layerIndex, false);
            if (image) {
                image->clear();
            }
        })
        .def("remove_stroke", [](AnimeSceneModel &model, int row, int layerIndex, int strokeIndex) {
            AnimeVectorImageModel *image = model.imageAt(row, layerIndex, false);
            if (!image || strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
                return false;
            }
            image->removeStrokeAt(strokeIndex);
            return true;
        })
        .def("remove_fill_area", [](AnimeSceneModel &model, int row, int layerIndex, int fillIndex) {
            AnimeVectorImageModel *image = model.imageAt(row, layerIndex, false);
            if (!image) {
                return false;
            }
            return image->removeFillRegionAt(fillIndex);
        })
        .def("clear_raster", [](AnimeSceneModel &model, int row, int layerIndex) {
            AnimeVectorImageModel *image = model.imageAt(row, layerIndex, false);
            if (!image) {
                return false;
            }
            image->clearRasterImage();
            return true;
        })
        .def("cell_to_dict",
             [](const AnimeSceneModel &model, int layerIndex, int frameIndex, bool toPoly, double polyStep) {
                 return cellToDict(model, layerIndex, frameIndex, toPoly, polyStep);
             },
             py::arg("layer_index"),
             py::arg("frame_index"),
             py::arg("to_poly") = false,
             py::arg("poly_step") = 4.0)
        .def("cell_strokes",
             [](const AnimeSceneModel &model, int layerIndex, int frameIndex, bool toPoly, double polyStep) {
                 return cellToDict(model, layerIndex, frameIndex, toPoly, polyStep)["image"];
             },
             py::arg("layer_index"),
             py::arg("frame_index"),
             py::arg("to_poly") = false,
             py::arg("poly_step") = 4.0)
        .def("fill_boundary_path_at",
             // Region-fill geometry as a mechanism: walls are the visible
             // non-fill strokes at the frame (layer_index >= 0 restricts to
             // one column); which region to fill and where it lands is the
             // caller's policy.
             [](const AnimeSceneModel &model, int frame, py::object seed, py::object bounds,
                int layerIndex, bool toPoly, double polyStep) -> py::object {
                 const QPointF seedPoint = objectToPointF(seed, "seed");
                 const QRectF boundsRect = objectToRectF(bounds, "bounds");
                 const QPainterPath path = AnimeVectorLogic::vectorRegionPathAt(
                     seedPoint, model.fillBoundarySegments(frame, layerIndex), boundsRect.toRect());
                 if (path.isEmpty()) {
                     return py::none();
                 }
                 return pathToDictValue(path, toPoly, polyStep);
             },
             py::arg("frame"),
             py::arg("seed"),
             py::arg("bounds"),
             py::arg("layer_index") = -1,
             py::arg("to_poly") = false,
             py::arg("poly_step") = 4.0)
        .def("stroke_line_list",
             [](const AnimeSceneModel &model, int row, int layerIndex, int strokeIndex, bool ploy, double simplify) {
                 const AnimeVectorImageModel *image = model.imageAt(row, layerIndex);
                 if (!image) {
                     throw py::index_error("No image exists for the requested frame/layer.");
                 }
                 if (strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
                     throw py::index_error("Stroke index is out of range.");
                 }
                 return strokeLineList(image->strokeNodeAt(strokeIndex).stroke, ploy, simplify);
             },
             py::arg("row"),
             py::arg("layer_index"),
             py::arg("stroke_index"),
             py::arg("ploy") = false,
             py::arg("simplify") = 0.0)
        .def("add_polyline",
             [](AnimeSceneModel &model,
                int row,
                int layerIndex,
                const std::vector<std::pair<double, double>> &points,
                int r,
                int g,
                int b,
                int a,
                double width) {
                 AnimeVectorImageModel *image = model.imageAt(row, layerIndex, true);
                 if (image) {
                     image->addStroke(makePolylineStroke(points, r, g, b, a, width));
                 }
             },
             py::arg("row"),
             py::arg("layer_index"),
             py::arg("points"),
             py::arg("r") = 0,
             py::arg("g") = 0,
             py::arg("b") = 0,
             py::arg("a") = 255,
             py::arg("width") = 3.0)
        .def("add_stroke_object",
             [](AnimeSceneModel &model, int row, int layerIndex, const AnimeVectorStroke &stroke) {
                 AnimeVectorImageModel *image = model.imageAt(row, layerIndex, true);
                 if (image) {
                     image->addStroke(stroke);
                 }
             },
             py::arg("row"),
             py::arg("layer_index"),
             py::arg("stroke"));

    py::module_ modelPybind = m.def_submodule("model_pybind", "Fast Python-to-Qt value conversion helpers.");
    modelPybind.def("qreal", [](py::object value) {
        return objectToQreal(value, "value");
    });
    modelPybind.def("point", [](py::object value) {
        return pointToDict(objectToPointF(value));
    });
    modelPybind.def("point_i", [](py::object value) {
        const QPoint point = objectToPoint(value);
        py::dict data;
        data["x"] = point.x();
        data["y"] = point.y();
        return data;
    });
    modelPybind.def("rect", [](py::object value) {
        return rectToDict(objectToRectF(value));
    });
    modelPybind.def("rect_i", [](py::object value) {
        const QRect rect = objectToRect(value);
        py::dict data;
        data["x"] = rect.x();
        data["y"] = rect.y();
        data["width"] = rect.width();
        data["height"] = rect.height();
        return data;
    });
    modelPybind.def("color", [](py::object value) {
        return colorToDict(objectToColor(value));
    });
    modelPybind.def("line", [](py::object value) {
        return lineToDict(objectToLine(value));
    });
    modelPybind.def("points", [](py::object value) {
        return pointsToList(objectToPoints(value));
    });
    modelPybind.def("lines", [](py::object value) {
        py::list data;
        for (const QLineF &line : objectToLines(value)) {
            data.append(lineToDict(line));
        }
        return data;
    });
    modelPybind.def("range", [](py::object value) {
        return rangeToDict(objectToRange(value));
    });
    modelPybind.def("ranges", [](py::object value) {
        return rangesToList(objectToRanges(value));
    });
    modelPybind.def("path",
                    [](py::object value, bool toPoly, double polyStep) {
                        return pathToDictValue(objectToPath(value), toPoly, polyStep);
                    },
                    py::arg("value"),
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);

    py::module_ vectorLogic = m.def_submodule("vectorlogic", "Bindings for AnimeVectorLogic geometry helpers.");
    vectorLogic.def("epsilon", &AnimeVectorLogic::epsilon);
    vectorLogic.def("filtered_points", [](py::object points) {
        return pointsToList(AnimeVectorLogic::filteredPoints(objectToPoints(points)));
    });
    vectorLogic.def("make_smoothed_path",
                    [](py::object points, int smoothValue, bool toPoly, double polyStep) {
                        return pathToDictValue(AnimeVectorLogic::makeSmoothedPath(objectToPoints(points), smoothValue),
                                               toPoly,
                                               polyStep);
                    },
                    py::arg("points"),
                    py::arg("smooth_value") = 50,
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("make_polyline_path",
                    [](py::object points, bool toPoly, double polyStep) {
                        return pathToDictValue(AnimeVectorLogic::makePolylinePath(objectToPoints(points)), toPoly, polyStep);
                    },
                    py::arg("points"),
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("make_stroke",
                    [](py::object points,
                       py::object color,
                       qreal width,
                       int id,
                       bool filterInput,
                       bool smoothPath,
                       int smoothValue,
                       bool toPoly,
                       double polyStep) {
                        const AnimeVectorStroke stroke = AnimeVectorLogic::makeStroke(objectToPoints(points),
                                                                                      objectToColor(color),
                                                                                      width,
                                                                                      id,
                                                                                      filterInput,
                                                                                      smoothPath,
                                                                                      smoothValue);
                        return strokeToDict(stroke, toPoly, polyStep);
                    },
                    py::arg("points"),
                    py::arg("color") = py::make_tuple(0, 0, 0, 255),
                    py::arg("width") = 3.0,
                    py::arg("id") = 0,
                    py::arg("filter_input") = true,
                    py::arg("smooth_path") = true,
                    py::arg("smooth_value") = 50,
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("make_stroke_object",
                    [](py::object points,
                       py::object color,
                       qreal width,
                       int id,
                       bool filterInput,
                       bool smoothPath,
                       int smoothValue) {
                        return AnimeVectorLogic::makeStroke(objectToPoints(points),
                                                            objectToColor(color),
                                                            width,
                                                            id,
                                                            filterInput,
                                                            smoothPath,
                                                            smoothValue);
                    },
                    py::arg("points"),
                    py::arg("color") = py::make_tuple(0, 0, 0, 255),
                    py::arg("width") = 3.0,
                    py::arg("id") = 0,
                    py::arg("filter_input") = true,
                    py::arg("smooth_path") = true,
                    py::arg("smooth_value") = 50);
    vectorLogic.def("make_stroke_object_from_path",
                    [](py::object commands,
                       py::object points,
                       py::object color,
                       qreal width,
                       int id) {
                        // Generic mechanism: build a stroke whose QPainterPath keeps
                        // its curve segments (from `commands`) while `points` carries a
                        // dense flattening so hit-testing / erasing / subStroke still
                        // work. The command format is the same one objectToPath()
                        // already parses (move/line/quad/cubic dicts). All the
                        // curve-fitting logic lives in Python (auto_mapping.py).
                        return AnimeVectorLogic::makeStrokeFromPath(objectToPath(commands),
                                                                    objectToPoints(points),
                                                                    objectToColor(color),
                                                                    width,
                                                                    id);
                    },
                    py::arg("commands"),
                    py::arg("points"),
                    py::arg("color") = py::make_tuple(0, 0, 0, 255),
                    py::arg("width") = 3.0,
                    py::arg("id") = 0);
    vectorLogic.def("stroke_hits_circle",
                    [](const AnimeVectorStroke &stroke, py::object center, qreal radius) {
                        return AnimeVectorLogic::strokeHitsCircle(stroke, objectToPointF(center, "center"), radius);
                    },
                    py::arg("stroke"),
                    py::arg("center"),
                    py::arg("radius"));
    vectorLogic.def("stroke_hits_circle",
                    [](py::object points, py::object center, qreal radius, qreal width) {
                        const AnimeVectorStroke stroke = AnimeVectorLogic::makeStroke(objectToPoints(points),
                                                                                      QColor(0, 0, 0, 255),
                                                                                      width,
                                                                                      0,
                                                                                      false,
                                                                                      false);
                        return AnimeVectorLogic::strokeHitsCircle(stroke, objectToPointF(center, "center"), radius);
                    },
                    py::arg("points"),
                    py::arg("center"),
                    py::arg("radius"),
                    py::arg("width") = 3.0);
    vectorLogic.def("stroke_hits_capsule",
                    [](const AnimeVectorStroke &stroke, py::object fromPoint, py::object toPoint, qreal radius) {
                        return AnimeVectorLogic::strokeHitsCapsule(stroke,
                                                                    objectToPointF(fromPoint, "from"),
                                                                    objectToPointF(toPoint, "to"),
                                                                    radius);
                    },
                    py::arg("stroke"),
                    py::arg("from_point"),
                    py::arg("to_point"),
                    py::arg("radius"));
    vectorLogic.def("stroke_hits_capsule",
                    [](py::object points, py::object fromPoint, py::object toPoint, qreal radius, qreal width) {
                        const AnimeVectorStroke stroke = AnimeVectorLogic::makeStroke(objectToPoints(points),
                                                                                      QColor(0, 0, 0, 255),
                                                                                      width,
                                                                                      0,
                                                                                      false,
                                                                                      false);
                        return AnimeVectorLogic::strokeHitsCapsule(stroke,
                                                                    objectToPointF(fromPoint, "from"),
                                                                    objectToPointF(toPoint, "to"),
                                                                    radius);
                    },
                    py::arg("points"),
                    py::arg("from_point"),
                    py::arg("to_point"),
                    py::arg("radius"),
                    py::arg("width") = 3.0);
    vectorLogic.def("keep_ranges_for_circle",
                    [](const AnimeVectorStroke &stroke, py::object center, qreal radius) {
                        return rangesToList(AnimeVectorLogic::keepRangesForCircle(stroke, objectToPointF(center, "center"), radius));
                    },
                    py::arg("stroke"),
                    py::arg("center"),
                    py::arg("radius"));
    vectorLogic.def("keep_ranges_for_capsule",
                    [](const AnimeVectorStroke &stroke, py::object fromPoint, py::object toPoint, qreal radius) {
                        return rangesToList(AnimeVectorLogic::keepRangesForCapsule(stroke,
                                                                                   objectToPointF(fromPoint, "from"),
                                                                                   objectToPointF(toPoint, "to"),
                                                                                   radius));
                    },
                    py::arg("stroke"),
                    py::arg("from_point"),
                    py::arg("to_point"),
                    py::arg("radius"));
    vectorLogic.def("complement_ranges", [](py::object ranges) {
        return rangesToList(AnimeVectorLogic::complementRanges(objectToRanges(ranges)));
    });
    vectorLogic.def("sub_stroke",
                    [](const AnimeVectorStroke &stroke, qreal fromW, qreal toW, int smoothValue, bool toPoly, double polyStep) {
                        return strokeToDict(AnimeVectorLogic::subStroke(stroke, fromW, toW, smoothValue), toPoly, polyStep);
                    },
                    py::arg("stroke"),
                    py::arg("from_w"),
                    py::arg("to_w"),
                    py::arg("smooth_value") = 50,
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("point_at_length",
                    [](const AnimeVectorStroke &stroke, qreal length) {
                        return pointToDict(AnimeVectorLogic::pointAtLength(stroke, length));
                    },
                    py::arg("stroke"),
                    py::arg("length"));
    vectorLogic.def("segments_from_path", [](py::object pathLike) {
        py::list data;
        for (const QLineF &line : AnimeVectorLogic::segmentsFromPath(objectToPath(pathLike))) {
            data.append(lineToDict(line));
        }
        return data;
    });
    vectorLogic.def("compute_vector_region_faces",
                    [](py::object lines, bool toPoly, double polyStep) {
                        py::list data;
                        for (const AnimeVectorRegionFace &face : AnimeVectorLogic::computeVectorRegionFaces(objectToLines(lines))) {
                            py::dict item = pathToDictValue(face.path, toPoly, polyStep);
                            item["signed_area"] = face.signedArea;
                            data.append(item);
                        }
                        return data;
                    },
                    py::arg("segments"),
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("vector_region_path_at",
                    [](py::object seed, py::object lines, py::object canvasRect, bool toPoly, double polyStep) {
                        return pathToDictValue(AnimeVectorLogic::vectorRegionPathAt(objectToPointF(seed, "seed"),
                                                                                    objectToLines(lines),
                                                                                    objectToRect(canvasRect, "canvas_rect")),
                                               toPoly,
                                               polyStep);
                    },
                    py::arg("seed"),
                    py::arg("segments"),
                    py::arg("canvas_rect"),
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
    vectorLogic.def("fill_path_from_mask",
                    [](py::object seed, py::object boundary, bool toPoly, double polyStep) {
                        return pathToDictValue(AnimeVectorLogic::fillPathFromMask(objectToPoint(seed, "seed"),
                                                                                  objectToMaskImage(boundary)),
                                               toPoly,
                                               polyStep);
                    },
                    py::arg("seed"),
                    py::arg("boundary"),
                    py::arg("to_poly") = false,
                    py::arg("poly_step") = 4.0);
}

#ifdef ANIMEAN_BUILDING_PYTHON_MODULE
PYBIND11_MODULE(animean_python, m)
{
    bindAnimeanPythonModule(m);
}
#else
PYBIND11_EMBEDDED_MODULE(animean_python, m)
{
    bindAnimeanPythonModule(m);
}
#endif
