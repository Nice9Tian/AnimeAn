#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "assetpanel.h"
#include "framepanel.h"
#include "layerpanel.h"
#include "openglwidget.h"
#include "tooloptpanel.h"
#include "toolspanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QBuffer>
#include <QByteArray>
#include <QDockWidget>
#include <QDropEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>

#include <string>

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots
#define ANIMEAN_RESTORE_QT_SLOTS
#endif
#include <pybind11/embed.h>
#ifdef ANIMEAN_RESTORE_QT_SLOTS
#define slots Q_SLOTS
#undef ANIMEAN_RESTORE_QT_SLOTS
#endif

namespace py = pybind11;
#endif

namespace {
int movedRowTarget(int sourceRow, int destinationChild)
{
    int target = destinationChild;
    if (sourceRow < destinationChild) {
        --target;
    }
    return target;
}

QString utf8String(const std::string &text)
{
    return QString::fromUtf8(text.c_str());
}

QString projectFilter()
{
    return QStringLiteral("AnimeAn Projects (*.animean);;All Files (*)");
}

QString columnTypeName(AnimeColumnType type)
{
    switch (type) {
    case AnimeColumnType::Raster:
        return QStringLiteral("raster");
    case AnimeColumnType::Fill:
        return QStringLiteral("fill");
    case AnimeColumnType::Vector:
    default:
        return QStringLiteral("vector");
    }
}

AnimeColumnType columnTypeFromName(const QString &name)
{
    if (name == QStringLiteral("raster")) {
        return AnimeColumnType::Raster;
    }
    if (name == QStringLiteral("fill")) {
        return AnimeColumnType::Fill;
    }
    return AnimeColumnType::Vector;
}

QJsonObject pointToJson(const QPointF &point)
{
    QJsonObject object;
    object[QStringLiteral("x")] = point.x();
    object[QStringLiteral("y")] = point.y();
    return object;
}

QPointF pointFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return QPointF(object.value(QStringLiteral("x")).toDouble(),
                   object.value(QStringLiteral("y")).toDouble());
}

QJsonArray pointsToJson(const QVector<QPointF> &points)
{
    QJsonArray array;
    for (const QPointF &point : points) {
        array.append(pointToJson(point));
    }
    return array;
}

QVector<QPointF> pointsFromJson(const QJsonValue &value)
{
    QVector<QPointF> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue &point : array) {
        points.append(pointFromJson(point));
    }
    return points;
}

QJsonObject colorToJson(const QColor &color)
{
    QJsonObject object;
    object[QStringLiteral("name")] = color.name(QColor::HexArgb);
    return object;
}

QColor colorFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QColor color(object.value(QStringLiteral("name")).toString(QStringLiteral("#ff000000")));
    return color.isValid() ? color : QColor(Qt::black);
}

QJsonObject pathToJson(const QPainterPath &path)
{
    QJsonObject object;
    object[QStringLiteral("fillRule")] = static_cast<int>(path.fillRule());

    QJsonArray elements;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        QJsonObject elementObject;
        elementObject[QStringLiteral("type")] = static_cast<int>(element.type);
        elementObject[QStringLiteral("x")] = element.x;
        elementObject[QStringLiteral("y")] = element.y;
        elements.append(elementObject);
    }
    object[QStringLiteral("elements")] = elements;
    return object;
}

QPainterPath pathFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QJsonArray elements = object.value(QStringLiteral("elements")).toArray();
    QPainterPath path;
    path.setFillRule(static_cast<Qt::FillRule>(object.value(QStringLiteral("fillRule")).toInt(Qt::OddEvenFill)));

    for (int i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements[i].toObject();
        const int type = element.value(QStringLiteral("type")).toInt();
        const QPointF point(element.value(QStringLiteral("x")).toDouble(),
                            element.value(QStringLiteral("y")).toDouble());
        if (type == QPainterPath::MoveToElement) {
            path.moveTo(point);
        } else if (type == QPainterPath::LineToElement) {
            path.lineTo(point);
        } else if (type == QPainterPath::CurveToElement && i + 2 < elements.size()) {
            const QJsonObject controlElement = elements[i + 1].toObject();
            const QJsonObject endElement = elements[i + 2].toObject();
            const QPointF control2(controlElement.value(QStringLiteral("x")).toDouble(),
                                   controlElement.value(QStringLiteral("y")).toDouble());
            const QPointF endPoint(endElement.value(QStringLiteral("x")).toDouble(),
                                   endElement.value(QStringLiteral("y")).toDouble());
            path.cubicTo(point, control2, endPoint);
            i += 2;
        }
    }
    return path;
}

QPainterPath polylinePathFromPoints(const QVector<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }
    path.moveTo(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first() + QPointF(0.01, 0.01));
    } else {
        for (int i = 1; i < points.size(); ++i) {
            path.lineTo(points[i]);
        }
    }
    return path;
}

void rebuildStrokeMetrics(AnimeVectorStroke *stroke)
{
    stroke->lengths.clear();
    stroke->lengths.reserve(stroke->points.size());
    stroke->totalLength = 0.0;
    for (int i = 0; i < stroke->points.size(); ++i) {
        if (i > 0) {
            stroke->totalLength += QLineF(stroke->points[i - 1], stroke->points[i]).length();
        }
        stroke->lengths.append(stroke->totalLength);
    }
    if (stroke->path.isEmpty()) {
        stroke->path = polylinePathFromPoints(stroke->points);
    }
    stroke->bounds = stroke->path.boundingRect().adjusted(-stroke->width,
                                                          -stroke->width,
                                                          stroke->width,
                                                          stroke->width);
}

QJsonObject rasterToJson(const AnimeRasterImage &raster)
{
    QJsonObject object;
    object[QStringLiteral("topLeft")] = pointToJson(raster.topLeft);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    raster.image.save(&buffer, "PNG");
    object[QStringLiteral("png")] = QString::fromLatin1(bytes.toBase64());
    return object;
}

AnimeRasterImage rasterFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeRasterImage raster;
    raster.topLeft = pointFromJson(object.value(QStringLiteral("topLeft")));
    raster.image.loadFromData(QByteArray::fromBase64(object.value(QStringLiteral("png")).toString().toLatin1()), "PNG");
    if (!raster.image.isNull()) {
        raster.image = raster.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return raster;
}

QJsonObject strokeNodeToJson(const AnimeVectorStrokeNode &node)
{
    QJsonObject object;
    const AnimeVectorStroke &stroke = node.stroke;
    object[QStringLiteral("id")] = stroke.id;
    object[QStringLiteral("points")] = pointsToJson(stroke.points);
    object[QStringLiteral("path")] = pathToJson(stroke.path);
    object[QStringLiteral("color")] = colorToJson(stroke.color);
    object[QStringLiteral("width")] = stroke.width;

    QJsonArray groupIds;
    for (int id : node.groupId.ids) {
        groupIds.append(id);
    }
    object[QStringLiteral("groupId")] = groupIds;
    object[QStringLiteral("isPoint")] = node.isPoint;
    object[QStringLiteral("isNewForFill")] = node.isNewForFill;
    object[QStringLiteral("selected")] = node.selected;
    return object;
}

AnimeVectorStrokeNode strokeNodeFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorStrokeNode node;
    node.stroke.id = object.value(QStringLiteral("id")).toInt();
    node.stroke.points = pointsFromJson(object.value(QStringLiteral("points")));
    node.stroke.path = pathFromJson(object.value(QStringLiteral("path")));
    node.stroke.color = colorFromJson(object.value(QStringLiteral("color")));
    node.stroke.width = object.value(QStringLiteral("width")).toDouble(3.0);
    rebuildStrokeMetrics(&node.stroke);

    const QJsonArray groupIds = object.value(QStringLiteral("groupId")).toArray();
    node.groupId.ids.reserve(groupIds.size());
    for (const QJsonValue &id : groupIds) {
        node.groupId.ids.append(id.toInt());
    }
    node.isPoint = object.value(QStringLiteral("isPoint")).toBool(false);
    node.isNewForFill = object.value(QStringLiteral("isNewForFill")).toBool(true);
    node.selected = object.value(QStringLiteral("selected")).toBool(false);
    return node;
}

QJsonObject fillRegionToJson(const AnimeVectorFillRegion &fill)
{
    QJsonObject object;
    object[QStringLiteral("id")] = fill.id;
    object[QStringLiteral("seedPoint")] = pointToJson(fill.seedPoint);
    object[QStringLiteral("path")] = pathToJson(fill.path);
    object[QStringLiteral("color")] = colorToJson(fill.color);
    object[QStringLiteral("sourceLayerIndex")] = fill.sourceLayerIndex;
    object[QStringLiteral("basedOnAllLayers")] = fill.basedOnAllLayers;
    return object;
}

AnimeVectorFillRegion fillRegionFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorFillRegion fill;
    fill.id = object.value(QStringLiteral("id")).toInt();
    fill.seedPoint = pointFromJson(object.value(QStringLiteral("seedPoint")));
    fill.path = pathFromJson(object.value(QStringLiteral("path")));
    fill.bounds = fill.path.boundingRect();
    fill.color = colorFromJson(object.value(QStringLiteral("color")));
    fill.sourceLayerIndex = object.value(QStringLiteral("sourceLayerIndex")).toInt(-1);
    fill.basedOnAllLayers = object.value(QStringLiteral("basedOnAllLayers")).toBool(false);
    return fill;
}

QJsonObject imageToJson(const AnimeVectorImageModel &image)
{
    QJsonObject object;
    if (image.hasRaster()) {
        object[QStringLiteral("raster")] = rasterToJson(image.raster());
    }

    QJsonArray strokes;
    for (const AnimeVectorStrokeNode &node : image.strokeNodes()) {
        strokes.append(strokeNodeToJson(node));
    }
    object[QStringLiteral("strokes")] = strokes;

    QJsonArray fills;
    for (const AnimeVectorFillRegion &fill : image.fillRegions()) {
        fills.append(fillRegionToJson(fill));
    }
    object[QStringLiteral("fills")] = fills;
    return object;
}

void loadImageFromJson(AnimeVectorImageModel *image, const QJsonValue &value)
{
    if (!image) {
        return;
    }

    image->clear();
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("raster"))) {
        const AnimeRasterImage raster = rasterFromJson(object.value(QStringLiteral("raster")));
        if (!raster.image.isNull()) {
            image->setRasterImage(raster.image, raster.topLeft);
        }
    }

    const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
    for (const QJsonValue &stroke : strokes) {
        image->addStrokeNode(strokeNodeFromJson(stroke));
    }

    const QJsonArray fills = object.value(QStringLiteral("fills")).toArray();
    for (const QJsonValue &fill : fills) {
        image->addFillRegion(fillRegionFromJson(fill));
    }
}

QJsonObject modelToJson(const AnimeSceneModel &model)
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("AnimeAn Project");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("currentFrame")] = model.currentFrame();
    root[QStringLiteral("currentLayer")] = model.currentLayer();
    root[QStringLiteral("currentAsset")] = model.currentAsset();

    const AnimeScene &scene = model.scene();
    QJsonObject xsheet;
    xsheet[QStringLiteral("frameCount")] = scene.xsheet.frameCount;

    QJsonArray columns;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        QJsonObject columnObject;
        columnObject[QStringLiteral("name")] = column.name;
        columnObject[QStringLiteral("type")] = columnTypeName(column.type);
        columnObject[QStringLiteral("visible")] = column.visible;
        columnObject[QStringLiteral("locked")] = column.locked;
        columnObject[QStringLiteral("opacity")] = column.opacity;

        QJsonArray cells;
        for (int row = 0; row < scene.xsheet.frameCount; ++row) {
            const AnimeCell cell = column.cellAt(row);
            if (cell.isEmpty()) {
                continue;
            }
            QJsonObject cellObject;
            cellObject[QStringLiteral("row")] = row;
            cellObject[QStringLiteral("levelIndex")] = cell.levelIndex;
            cellObject[QStringLiteral("frameId")] = cell.frameId;
            cells.append(cellObject);
        }
        columnObject[QStringLiteral("cells")] = cells;
        columns.append(columnObject);
    }
    xsheet[QStringLiteral("columns")] = columns;
    root[QStringLiteral("xsheet")] = xsheet;

    QJsonArray levels;
    for (const AnimeLevel &level : scene.levels) {
        QJsonObject levelObject;
        levelObject[QStringLiteral("name")] = level.name;
        levelObject[QStringLiteral("type")] = columnTypeName(level.type);

        QJsonArray frames;
        for (int frameId : level.frameIds()) {
            const AnimeVectorImageModel *image = level.frame(frameId);
            if (!image) {
                continue;
            }
            QJsonObject frameObject;
            frameObject[QStringLiteral("frameId")] = frameId;
            frameObject[QStringLiteral("image")] = imageToJson(*image);
            frames.append(frameObject);
        }
        levelObject[QStringLiteral("frames")] = frames;
        levels.append(levelObject);
    }
    root[QStringLiteral("levels")] = levels;
    return root;
}

bool modelFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error)
{
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("AnimeAn Project")) {
        if (error) {
            *error = QStringLiteral("Unsupported project file.");
        }
        return false;
    }

    AnimeSceneModel loaded;
    AnimeScene &scene = loaded.scene();
    scene = AnimeScene();

    const QJsonObject xsheet = root.value(QStringLiteral("xsheet")).toObject();
    scene.xsheet.frameCount = qMax(1, xsheet.value(QStringLiteral("frameCount")).toInt(1));

    const QJsonArray levels = root.value(QStringLiteral("levels")).toArray();
    scene.levels.reserve(levels.size());
    for (const QJsonValue &levelValue : levels) {
        const QJsonObject levelObject = levelValue.toObject();
        AnimeLevel level;
        level.name = levelObject.value(QStringLiteral("name")).toString();
        level.type = columnTypeFromName(levelObject.value(QStringLiteral("type")).toString());

        const QJsonArray frames = levelObject.value(QStringLiteral("frames")).toArray();
        for (const QJsonValue &frameValue : frames) {
            const QJsonObject frameObject = frameValue.toObject();
            const int frameId = frameObject.value(QStringLiteral("frameId")).toInt();
            if (frameId <= 0) {
                continue;
            }
            loadImageFromJson(level.frame(frameId, true), frameObject.value(QStringLiteral("image")));
        }
        scene.levels.append(level);
    }

    const QJsonArray columns = xsheet.value(QStringLiteral("columns")).toArray();
    scene.xsheet.columns.reserve(columns.size());
    for (const QJsonValue &columnValue : columns) {
        const QJsonObject columnObject = columnValue.toObject();
        AnimeColumn column;
        column.name = columnObject.value(QStringLiteral("name")).toString();
        column.type = columnTypeFromName(columnObject.value(QStringLiteral("type")).toString());
        column.visible = columnObject.value(QStringLiteral("visible")).toBool(true);
        column.locked = columnObject.value(QStringLiteral("locked")).toBool(false);
        column.opacity = columnObject.value(QStringLiteral("opacity")).toDouble(1.0);

        const QJsonArray cells = columnObject.value(QStringLiteral("cells")).toArray();
        for (const QJsonValue &cellValue : cells) {
            const QJsonObject cellObject = cellValue.toObject();
            AnimeCell cell;
            cell.levelIndex = cellObject.value(QStringLiteral("levelIndex")).toInt(-1);
            cell.frameId = cellObject.value(QStringLiteral("frameId")).toInt(0);
            if (cell.levelIndex >= 0 && cell.levelIndex < scene.levels.size() && cell.frameId > 0) {
                column.setCell(cellObject.value(QStringLiteral("row")).toInt(), cell);
            }
        }
        scene.xsheet.columns.append(column);
    }

    loaded.setCurrentFrame(root.value(QStringLiteral("currentFrame")).toInt(0));
    loaded.setCurrentLayer(root.value(QStringLiteral("currentLayer")).toInt(-1));
    loaded.setCurrentAsset(root.value(QStringLiteral("currentAsset")).toInt(-1));
    *model = loaded;
    return true;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupDocks();
    setupListDragDrop();

    updateAttention(AttentionChange::FrameChange,
                    m_paintWidget->model().currentFrame(),
                    m_paintWidget->model().currentLayer(),
                    m_paintWidget->model().currentAsset());

    setupPython();
    setupConnections();
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupDocks()
{
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    m_paintWidget = ui->graphicsView;
    createListDocks();
    createToolDocks();
}

void MainWindow::setupListDragDrop()
{
    m_layerPanel->layerList()->setDragDropMode(QAbstractItemView::DragDrop);
    m_layerPanel->layerList()->setDefaultDropAction(Qt::MoveAction);
    m_layerPanel->layerList()->setDragDropOverwriteMode(false);
    m_layerPanel->layerList()->setDragEnabled(true);
    m_layerPanel->layerList()->setSelectionMode(QAbstractItemView::SingleSelection);

    m_framePanel->frameList()->setDragDropMode(QAbstractItemView::InternalMove);
    m_framePanel->frameList()->setDefaultDropAction(Qt::MoveAction);
    m_framePanel->frameList()->setDragDropOverwriteMode(false);
    m_framePanel->frameList()->setSelectionMode(QAbstractItemView::SingleSelection);

    m_assetPanel->assetList()->setDragDropMode(QAbstractItemView::DragOnly);
    m_assetPanel->assetList()->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerPanel->layerList()->viewport()->setAcceptDrops(true);
    m_layerPanel->layerList()->viewport()->installEventFilter(this);
    m_framePanel->frameList()->viewport()->installEventFilter(this);
    m_assetPanel->assetList()->viewport()->installEventFilter(this);
}

void MainWindow::setupPython()
{
#ifdef ANIMEAN_WITH_PYTHON
    try {
        py::list pythonPath = py::module_::import("sys").attr("path");
        pythonPath.insert(0, QCoreApplication::applicationDirPath().toStdString());
        if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_SOURCE_DIR)).filePath(QStringLiteral("hello_world.py")))) {
            pythonPath.insert(0, ANIMEAN_SOURCE_DIR);
        }

        const std::string helloText = py::module_::import("hello_world")
                                          .attr("hello_world")()
                                          .cast<std::string>();
        ui->label->setText(utf8String(helloText));
    } catch (const py::error_already_set &error) {
        ui->label->setText(QStringLiteral("Python error: %1").arg(QString::fromUtf8(error.what())));
    }
#else
    ui->label->setText(QStringLiteral("Python disabled"));
#endif
}

void MainWindow::setupConnections()
{
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openProject);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveProject);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveProjectAs);

    connect(ui->PythonAxisButton, &QPushButton::clicked, this, [this]() {
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::module_::import("animean_python");
            const std::string result = py::module_::import("hello_world")
                                           .attr("draw_axis_test")(
                                               py::cast(&m_paintWidget->model(), py::return_value_policy::reference),
                                               m_paintWidget->width(),
                                               m_paintWidget->height())
                                           .cast<std::string>();
            m_paintWidget->update();
            ui->label->setText(utf8String(result));
        } catch (const py::error_already_set &error) {
            ui->label->setText(QStringLiteral("Python axis error: %1").arg(QString::fromUtf8(error.what())));
        }
#else
        ui->label->setText(QStringLiteral("Python disabled"));
#endif
    });

    connect(ui->CellDictButton, &QPushButton::clicked, this, [this]() {
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::module_::import("animean_python");
            const py::object model = py::cast(&m_paintWidget->model(), py::return_value_policy::reference);
            const py::object cellDict = model.attr("cell_to_dict")(
                m_paintWidget->model().currentLayer(),
                m_paintWidget->model().currentFrame(),
                false,
                4.0);
            ui->label->setText(utf8String(py::str(cellDict).cast<std::string>()));
        } catch (const py::error_already_set &error) {
            ui->label->setText(QStringLiteral("Python cell dict error: %1").arg(QString::fromUtf8(error.what())));
        }
#else
        ui->label->setText(QStringLiteral("Python disabled"));
#endif
    });

    connect(m_layerPanel->layerList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            QListWidgetItem *item = m_layerPanel->layerList()->item(row);
            const int layerIndex = item ? item->data(Qt::UserRole).toInt() : -1;
            requestAttentionUpdate(AttentionChange::LayerChange, m_attention.frame, layerIndex, m_attention.asset);
        }
    });

    connect(m_paintWidget, &PaintOpenGLWidget::layerListChanged, this, [this](int selectedLayer) {
        updateAttention(AttentionChange::LayerChange,
                        m_paintWidget->model().currentFrame(),
                        selectedLayer,
                        m_paintWidget->model().currentAsset());
    });

    connect(m_paintWidget, &PaintOpenGLWidget::assetListChanged, this, [this](int selectedAsset) {
        updateAttention(AttentionChange::AssetChange, m_attention.frame, m_attention.layer, selectedAsset);
    });

    connect(m_framePanel->frameList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            requestAttentionUpdate(AttentionChange::FrameChange, row, m_attention.layer, m_attention.asset);
        }
    });

    connect(m_layerPanel->addButton(), &QPushButton::clicked, this, [this]() {
        const int layerIndex = m_paintWidget->addLayer();
        updateAttention(AttentionChange::LayerChange, m_attention.frame, layerIndex, m_attention.asset);
    });

    connect(m_layerPanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = m_layerPanel->layerList()->currentItem();
        const int layerIndex = item ? item->data(Qt::UserRole).toInt() : -1;
        if (m_paintWidget->deleteLayer(layerIndex)) {
            const int nextLayer = layerIndex < m_paintWidget->layerCount() ? layerIndex : m_paintWidget->layerCount() - 1;
            updateAttention(AttentionChange::LayerChange, m_attention.frame, nextLayer, m_attention.asset);
        }
    });

    connect(m_layerPanel->unselectButton(), &QPushButton::clicked, this, [this]() {
        updateAttention(AttentionChange::LayerChange, m_attention.frame, -1, -1);
    });

    connect(m_framePanel->addButton(), &QPushButton::clicked, this, [this]() {
        const int frameIndex = m_paintWidget->addFrame();
        updateAttention(AttentionChange::FrameChange, frameIndex, m_attention.layer, m_attention.asset);
    });

    connect(m_framePanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        const int row = m_framePanel->frameList()->currentRow();
        if (m_paintWidget->deleteFrame(row)) {
            const int nextFrame = row < m_paintWidget->frameCount() ? row : m_paintWidget->frameCount() - 1;
            updateAttention(AttentionChange::FrameChange, nextFrame, m_attention.layer, m_attention.asset);
        }
    });

    connect(m_assetPanel->addButton(), &QPushButton::clicked, this, [this]() {
        const int assetIndex = m_paintWidget->addAsset();
        updateAttention(AttentionChange::AssetChange, m_attention.frame, m_attention.layer, assetIndex);
    });

    connect(m_assetPanel->unselectButton(), &QPushButton::clicked, this, [this]() {
        updateAttention(AttentionChange::AssetChange, m_attention.frame, -1, -1);
    });

    connect(m_assetPanel->assetList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists) {
            requestAttentionUpdate(AttentionChange::AssetChange, m_attention.frame, m_attention.layer, row);
        }
    });

    connect(m_layerPanel->layerList()->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        const int targetRow = movedRowTarget(sourceStart, destinationChild);
        QListWidgetItem *movedItem = m_layerPanel->layerList()->item(targetRow);
        if (!movedItem) {
            updateAttention(AttentionChange::LayerChange,
                            m_paintWidget->model().currentFrame(),
                            m_paintWidget->model().currentLayer(),
                            m_paintWidget->model().currentAsset());
            return;
        }

        const int fromIndex = movedItem->data(Qt::UserRole).toInt();
        int toIndex = fromIndex;
        if (targetRow <= 0) {
            toIndex = 0;
        } else {
            QListWidgetItem *previousItem = m_layerPanel->layerList()->item(targetRow - 1);
            toIndex = previousItem ? previousItem->data(Qt::UserRole).toInt() + 1 : fromIndex;
        }
        if (fromIndex < toIndex) {
            --toIndex;
        }

        if (!m_paintWidget->moveLayer(fromIndex, toIndex)) {
            updateAttention(AttentionChange::LayerChange,
                            m_paintWidget->model().currentFrame(),
                            m_paintWidget->model().currentLayer(),
                            m_paintWidget->model().currentAsset());
            return;
        }
        updateAttention(AttentionChange::LayerChange, m_attention.frame, toIndex, m_attention.asset);
    });

    connect(m_framePanel->frameList()->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        const int target = movedRowTarget(sourceStart, destinationChild);
        if (!m_paintWidget->moveFrame(sourceStart, target)) {
            updateAttention(AttentionChange::FrameChange,
                            m_paintWidget->model().currentFrame(),
                            m_paintWidget->model().currentLayer(),
                            m_paintWidget->model().currentAsset());
            return;
        }
        updateAttention(AttentionChange::FrameChange, target, m_attention.layer, m_attention.asset);
    });

    connect(ui->actionimport_Raster, &QAction::triggered, this, &MainWindow::importRaster);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool watchedListViewport = watched == m_layerPanel->layerList()->viewport()
                                     || watched == m_framePanel->frameList()->viewport()
                                     || watched == m_assetPanel->assetList()->viewport();
    if (watchedListViewport) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_listMousePressed = true;
                m_listDragActive = false;
                m_hasPendingAttention = false;
                m_listPressPos = mouseEvent->pos();
            }
        } else if (event->type() == QEvent::MouseMove && m_listMousePressed) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if ((mouseEvent->pos() - m_listPressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_listDragActive = true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_listMousePressed) {
                const bool shouldCommit = m_hasPendingAttention && !m_listDragActive;
                const AttentionChange change = m_pendingAttentionChange;
                const SelectionAttention attention = m_pendingAttention;
                m_listMousePressed = false;
                m_listDragActive = false;
                m_hasPendingAttention = false;
                if (shouldCommit) {
                    updateAttention(change, attention.frame, attention.layer, attention.asset);
                }
            }
        }
    }

    if (watched == m_layerPanel->layerList()->viewport() &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
        QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
        m_listDragActive = true;
        if (dropEvent->source() == m_layerPanel->layerList()) {
            return QMainWindow::eventFilter(watched, event);
        }

        if (dropEvent->source() != m_assetPanel->assetList()) {
            if (event->type() == QEvent::Drop) {
                m_hasPendingAttention = false;
                m_listMousePressed = false;
                m_listDragActive = false;
            }
            dropEvent->ignore();
            return true;
        }

        const int assetIndex = m_assetPanel->assetList()->currentRow();
        if (assetIndex < 0) {
            if (event->type() == QEvent::Drop) {
                m_hasPendingAttention = false;
                m_listMousePressed = false;
                m_listDragActive = false;
            }
            dropEvent->ignore();
            return true;
        }

        dropEvent->acceptProposedAction();
        if (event->type() == QEvent::Drop) {
            m_hasPendingAttention = false;
            const int layerIndex = m_paintWidget->addLayerForAsset(assetIndex);
            if (layerIndex >= 0) {
                updateAttention(AttentionChange::LayerChange, m_attention.frame, layerIndex, assetIndex);
            }
            m_listMousePressed = false;
            m_listDragActive = false;
        }
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createToolDocks()
{
    ToolsPanel *toolsPanel = new ToolsPanel(this);
    ToolOptPanel *toolOptPanel = new ToolOptPanel(this);

    m_toolsDock = new QDockWidget(QStringLiteral("Pen"), this);
    m_toolsDock->setWidget(toolsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);

    m_toolOptDock = new QDockWidget(QStringLiteral("ToolOpt"), this);
    m_toolOptDock->setWidget(toolOptPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_toolOptDock);
    splitDockWidget(m_toolOptDock, m_layerDock, Qt::Vertical);
    splitDockWidget(m_layerDock, m_assetDock, Qt::Vertical);

    auto selectTool = [this, toolsPanel, toolOptPanel](PaintOpenGLWidget::Tool tool) {
        m_paintWidget->setTool(tool);
        toolsPanel->setTool(tool);
        toolOptPanel->setTool(tool);
    };

    connect(toolsPanel, &ToolsPanel::toolSelected, this, selectTool);

    connect(toolOptPanel, &ToolOptPanel::colorSelected, this, [this, toolOptPanel, selectTool](const QColor &color) {
        if (toolOptPanel->tool() == PaintOpenGLWidget::Tool::Fill) {
            m_paintWidget->setDrawingColor(color);
            selectTool(PaintOpenGLWidget::Tool::Fill);
            return;
        }

        m_paintWidget->setPenColor(color);
        selectTool(PaintOpenGLWidget::Tool::Pen);
    });

    connect(toolOptPanel, &ToolOptPanel::fillScopeSelected, this, [this](PaintOpenGLWidget::FillScope scope) {
        m_paintWidget->setFillScope(scope);
    });

    connect(toolOptPanel, &ToolOptPanel::eraserModeSelected, this, selectTool);

    connect(toolOptPanel, &ToolOptPanel::smoothValueChanged, this, [this](int value) {
        m_paintWidget->setSmoothValue(value);
    });

    connect(toolOptPanel, &ToolOptPanel::penWidthChanged, this, [this](int value) {
        m_paintWidget->setPenWidth(value);
    });
}

void MainWindow::createListDocks()
{
    m_layerPanel = new LayerPanel(this);
    m_framePanel = new FramePanel(this);
    m_assetPanel = new AssetPanel(this);

    m_layerDock = new QDockWidget(QStringLiteral("Layer"), this);
    m_layerDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    m_frameDock = new QDockWidget(QStringLiteral("Frame"), this);
    m_frameDock->setWidget(m_framePanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_frameDock);

    m_assetDock = new QDockWidget(QStringLiteral("Asset"), this);
    m_assetDock->setWidget(m_assetPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_assetDock);
}

void MainWindow::openProject()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开工程"),
        m_currentFilePath.isEmpty() ? QString() : QFileInfo(m_currentFilePath).absolutePath(),
        projectFilter());
    if (fileName.isEmpty()) {
        return;
    }

    loadProjectFrom(fileName);
}

bool MainWindow::saveProject()
{
    if (m_currentFilePath.isEmpty()) {
        return saveProjectAs();
    }
    return saveProjectTo(m_currentFilePath);
}

bool MainWindow::saveProjectAs()
{
    QString selectedFile = m_currentFilePath;
    if (selectedFile.isEmpty()) {
        selectedFile = QDir::home().filePath(QStringLiteral("untitled.animean"));
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("另存为"),
        selectedFile,
        projectFilter());
    if (fileName.isEmpty()) {
        return false;
    }
    if (QFileInfo(fileName).suffix().isEmpty()) {
        fileName += QStringLiteral(".animean");
    }
    return saveProjectTo(fileName);
}

bool MainWindow::saveProjectTo(const QString &fileName)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存工程"),
                             QStringLiteral("无法写入文件:\n%1").arg(file.errorString()));
        return false;
    }

    const QJsonDocument document(modelToJson(m_paintWidget->model()));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this,
                             QStringLiteral("保存工程"),
                             QStringLiteral("无法保存文件:\n%1").arg(file.errorString()));
        return false;
    }

    m_currentFilePath = fileName;
    updateWindowTitle();
    ui->label->setText(QStringLiteral("已保存: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

bool MainWindow::loadProjectFrom(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("打开工程"),
                             QStringLiteral("无法读取文件:\n%1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this,
                             QStringLiteral("打开工程"),
                             QStringLiteral("工程文件格式错误:\n%1").arg(parseError.errorString()));
        return false;
    }

    AnimeSceneModel loadedModel;
    QString error;
    if (!modelFromJson(document.object(), &loadedModel, &error)) {
        QMessageBox::warning(this,
                             QStringLiteral("打开工程"),
                             error.isEmpty() ? QStringLiteral("工程文件格式不受支持。") : error);
        return false;
    }

    m_paintWidget->model() = loadedModel;
    m_currentFilePath = fileName;
    updateWindowTitle();
    updateAttention(AttentionChange::FrameChange,
                    m_paintWidget->model().currentFrame(),
                    m_paintWidget->model().currentLayer(),
                    m_paintWidget->model().currentAsset());
    m_paintWidget->update();
    ui->label->setText(QStringLiteral("已打开: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

void MainWindow::importRaster()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Raster"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All Files (*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Raster"),
                             QStringLiteral("Failed to load image:\n%1").arg(reader.errorString()));
        return;
    }

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QFileInfo fileInfo(fileName);
    const int layerIndex = m_paintWidget->importRasterLayer(image, fileInfo.completeBaseName());
    if (layerIndex < 0) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Raster"),
                             QStringLiteral("Failed to import raster layer."));
        return;
    }

    updateAttention(AttentionChange::LayerChange,
                    m_paintWidget->model().currentFrame(),
                    layerIndex,
                    m_paintWidget->model().currentAsset());
    ui->label->setText(QStringLiteral("Imported raster: %1 (%2 x %3)")
                           .arg(fileInfo.fileName())
                           .arg(image.width())
                           .arg(image.height()));
}

void MainWindow::updateWindowTitle()
{
    const QString fileName = m_currentFilePath.isEmpty()
                                 ? QStringLiteral("Untitled")
                                 : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(QStringLiteral("AnimeAn - %1").arg(fileName));
}

void MainWindow::requestAttentionUpdate(AttentionChange change, int frame, int layer, int asset)
{
    if (!m_listMousePressed) {
        updateAttention(change, frame, layer, asset);
        return;
    }

    m_pendingAttentionChange = change;
    m_pendingAttention.frame = frame;
    m_pendingAttention.layer = layer;
    m_pendingAttention.asset = asset;
    m_hasPendingAttention = true;
}

void MainWindow::updateAttention(AttentionChange change, int frame, int layer, int asset)
{
    const SelectionAttention previous = m_attention;
    m_attention.frame = frame;
    m_attention.layer = layer;
    m_attention.asset = asset;
    AttentionUpdate update = constrainAttention(change);
    update.frame = update.frame || previous.frame != m_attention.frame;
    update.layer = update.layer || previous.layer != m_attention.layer;
    update.asset = update.asset || previous.asset != m_attention.asset;

    m_paintWidget->setCurrentFrame(m_attention.frame);
    m_paintWidget->setCurrentLayer(m_attention.layer);
    m_paintWidget->setCurrentAsset(m_attention.asset);

    if (update.frame) {
        refreshFrameList(m_attention.frame);
    }
    if (update.layer) {
        refreshLayerList(m_attention.layer);
    }
    if (update.asset) {
        refreshAssetList(m_attention.asset);
    }
}

MainWindow::AttentionUpdate MainWindow::constrainAttention(AttentionChange change)
{
    AttentionUpdate update;
    update.frame = change == AttentionChange::FrameChange;
    update.layer = true;
    update.asset = true;

    if (m_paintWidget->frameCount() <= 0) {
        m_attention.frame = -1;
    } else if (m_attention.frame < 0) {
        m_attention.frame = change == AttentionChange::AssetChange ? -1 : 0;
    } else if (m_attention.frame >= m_paintWidget->frameCount()) {
        m_attention.frame = m_paintWidget->frameCount() - 1;
    }

    if (m_attention.asset < 0 || m_attention.asset >= m_paintWidget->assetCount()) {
        m_attention.asset = -1;
    }
    if (m_attention.layer < 0 || m_attention.layer >= m_paintWidget->layerCount()) {
        m_attention.layer = -1;
    }

    const int layerAsset = m_paintWidget->model().assetIndexAt(m_attention.frame, m_attention.layer);

    if (change == AttentionChange::FrameChange) {
        m_attention.layer = topLayerForFrame(m_attention.frame);
        m_attention.asset = m_attention.layer >= 0
                                ? m_paintWidget->model().assetIndexAt(m_attention.frame, m_attention.layer)
                                : -1;
        return update;
    }

    if (change == AttentionChange::AssetChange) {
        m_attention.layer = m_attention.asset >= 0
                                ? firstLayerForAsset(m_attention.frame, m_attention.asset)
                                : -1;
        if (m_attention.asset >= 0 && m_attention.layer < 0) {
            m_attention.frame = -1;
        }
        return update;
    }

    if (change == AttentionChange::LayerChange) {
        if (layerAsset >= 0) {
            m_attention.asset = layerAsset;
        } else {
            m_attention.layer = -1;
            m_attention.asset = -1;
        }
    }
    return update;
}

int MainWindow::topLayerForFrame(int frame) const
{
    for (int i = 0; i < m_paintWidget->layerCount(); ++i) {
        if (m_paintWidget->model().assetIndexAt(frame, i) >= 0) {
            return i;
        }
    }
    return -1;
}

int MainWindow::firstLayerForAsset(int frame, int asset) const
{
    if (asset < 0) {
        return -1;
    }
    for (int i = 0; i < m_paintWidget->layerCount(); ++i) {
        if (m_paintWidget->model().assetIndexAt(frame, i) == asset) {
            return i;
        }
    }
    return -1;
}

void MainWindow::refreshLayerList(int selectedRow)
{
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_layerPanel->layerList());
    m_layerPanel->layerList()->clear();
    int selectedListRow = -1;
    for (int i = 0; i < m_paintWidget->layerCount(); ++i) {
        if (m_paintWidget->model().assetIndexAt(m_paintWidget->model().currentFrame(), i) < 0) {
            continue;
        }
        QListWidgetItem *item = new QListWidgetItem(m_paintWidget->layerName(i));
        item->setData(Qt::UserRole, i);
        m_layerPanel->layerList()->addItem(item);
        if (i == selectedRow) {
            selectedListRow = m_layerPanel->layerList()->count() - 1;
        }
    }
    if (m_layerPanel->layerList()->count() > 0) {
        if (selectedListRow >= 0) {
            m_layerPanel->layerList()->setCurrentRow(selectedListRow);
        } else {
            m_layerPanel->layerList()->clearSelection();
            m_layerPanel->layerList()->setCurrentRow(-1);
        }
    } else {
        m_layerPanel->layerList()->setCurrentRow(-1);
    }
    m_refreshingLists = false;
}

void MainWindow::refreshFrameList(int selectedRow)
{
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_framePanel->frameList());
    m_framePanel->frameList()->clear();
    for (int i = 0; i < m_paintWidget->frameCount(); ++i) {
        m_framePanel->frameList()->addItem(m_paintWidget->frameName(i));
    }
    if (m_framePanel->frameList()->count() > 0) {
        if (selectedRow < 0) {
            m_framePanel->frameList()->clearSelection();
            m_framePanel->frameList()->setCurrentRow(-1);
            m_refreshingLists = false;
            return;
        } else if (selectedRow >= m_framePanel->frameList()->count()) {
            selectedRow = m_framePanel->frameList()->count() - 1;
        }
        m_framePanel->frameList()->setCurrentRow(selectedRow);
    }
    m_refreshingLists = false;
}

void MainWindow::refreshAssetList(int selectedRow)
{
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_assetPanel->assetList());
    m_assetPanel->assetList()->clear();
    for (int i = 0; i < m_paintWidget->assetCount(); ++i) {
        m_assetPanel->assetList()->addItem(m_paintWidget->assetName(i));
    }
    if (selectedRow >= 0 && m_assetPanel->assetList()->count() > 0) {
        if (selectedRow >= m_assetPanel->assetList()->count()) {
            selectedRow = m_assetPanel->assetList()->count() - 1;
        }
        m_assetPanel->assetList()->setCurrentRow(selectedRow);
    } else {
        m_assetPanel->assetList()->clearSelection();
        m_assetPanel->assetList()->setCurrentRow(-1);
    }
    m_refreshingLists = false;
}
