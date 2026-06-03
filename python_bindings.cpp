#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "animemodel.h"

#include <QLineF>
#include <QPainterPath>

#include <cmath>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {
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

py::dict strokeNodeToDict(const AnimeVectorStrokeNode &node, bool toPoly, double polyStep)
{
    const AnimeVectorStroke &stroke = node.stroke;
    py::dict data;
    data["id"] = stroke.id;
    data["width"] = stroke.width;
    data["color"] = colorToDict(stroke.color);
    data["bounds"] = rectToDict(stroke.bounds);
    data["raw_points"] = pointsToList(stroke.points);
    data["total_length"] = stroke.totalLength;

    py::list lengths;
    for (qreal length : stroke.lengths) {
        lengths.append(length);
    }
    data["lengths"] = lengths;

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

py::dict imageToDict(const AnimeVectorImageModel *image, bool toPoly, double polyStep)
{
    py::dict data;
    data["empty"] = image == nullptr;
    data["stroke_count"] = image ? image->strokeCount() : 0;
    data["bounds"] = image ? rectToDict(image->bounds()) : py::dict();

    py::list strokes;
    if (image) {
        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            strokes.append(strokeNodeToDict(node, toPoly, polyStep));
        }
    }
    data["strokes"] = strokes;
    return data;
}

py::dict cellToDict(const AnimeSceneModel &model, int layerIndex, int frameIndex, bool toPoly, double polyStep)
{
    const AnimeCell cell = model.cellAt(frameIndex, layerIndex);
    py::dict data;
    data["layer_index"] = layerIndex;
    data["frame_index"] = frameIndex;
    data["level_index"] = cell.levelIndex;
    data["frame_id"] = cell.frameId;
    data["empty"] = cell.isEmpty();

    const AnimeVectorImageModel *image = model.imageForCell(cell);
    data["image"] = imageToDict(image, toPoly, polyStep);
    return data;
}
}

void bindAnimeanPythonModule(py::module_ &m)
{
    m.doc() = "Python bindings for AnimeAn scene, layer, frame, and vector image models.";

    py::class_<AnimeCell>(m, "Cell")
        .def(py::init<>())
        .def_readwrite("level_index", &AnimeCell::levelIndex)
        .def_readwrite("frame_id", &AnimeCell::frameId)
        .def("is_empty", &AnimeCell::isEmpty);

    py::class_<AnimeVectorImageModel>(m, "VectorImage")
        .def("stroke_count", &AnimeVectorImageModel::strokeCount)
        .def("clear", &AnimeVectorImageModel::clear)
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
             py::arg("width") = 3.0);

    py::class_<AnimeSceneModel>(m, "SceneModel")
        .def(py::init<>())
        .def("initialize_scene", &AnimeSceneModel::initializeScene, py::arg("layer_count"), py::arg("frame_count"))
        .def("set_current_layer", &AnimeSceneModel::setCurrentLayer)
        .def("set_current_frame", &AnimeSceneModel::setCurrentFrame)
        .def("current_layer", &AnimeSceneModel::currentLayer)
        .def("current_frame", &AnimeSceneModel::currentFrame)
        .def("layer_count", &AnimeSceneModel::layerCount)
        .def("frame_count", &AnimeSceneModel::frameCount)
        .def("layer_name", [](const AnimeSceneModel &model, int layerIndex) {
            return model.layerName(layerIndex).toStdString();
        })
        .def("set_layer_name", [](AnimeSceneModel &model, int layerIndex, const std::string &name) {
            model.setLayerName(layerIndex, QString::fromStdString(name));
        })
        .def("frame_name", [](const AnimeSceneModel &model, int frameIndex) {
            return model.frameName(frameIndex).toStdString();
        })
        .def("layer_visible", &AnimeSceneModel::layerVisible)
        .def("set_layer_visible", &AnimeSceneModel::setLayerVisible)
        .def("layer_locked", &AnimeSceneModel::layerLocked)
        .def("set_layer_locked", &AnimeSceneModel::setLayerLocked)
        .def("layer_opacity", &AnimeSceneModel::layerOpacity)
        .def("set_layer_opacity", &AnimeSceneModel::setLayerOpacity)
        .def("add_layer", &AnimeSceneModel::addLayer)
        .def("delete_layer", &AnimeSceneModel::deleteLayer)
        .def("move_layer", &AnimeSceneModel::moveLayer)
        .def("add_frame", &AnimeSceneModel::addFrame)
        .def("delete_frame", &AnimeSceneModel::deleteFrame)
        .def("move_frame", &AnimeSceneModel::moveFrame)
        .def("cell_at", &AnimeSceneModel::cellAt)
        .def("set_cell", &AnimeSceneModel::setCell)
        .def("clear_cell", &AnimeSceneModel::clearCell)
        .def("image_at",
             py::overload_cast<int, int, bool>(&AnimeSceneModel::imageAt),
             py::arg("row"),
             py::arg("layer_index"),
             py::arg("create") = false,
             py::return_value_policy::reference_internal)
        .def("current_image",
             &AnimeSceneModel::currentImage,
             py::arg("create") = false,
             py::return_value_policy::reference_internal)
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
             py::arg("width") = 3.0);
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
