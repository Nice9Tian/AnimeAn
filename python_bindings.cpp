#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "animemodel.h"

#include <QLineF>

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
