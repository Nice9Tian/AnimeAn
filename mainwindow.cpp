#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "childrenpanel/assetpanel.h"
#include "childrenpanel/childpaintwindow.h"
#include "childrenpanel/framepanel.h"
#include "childrenpanel/layerpanel.h"
#include "clipreader.h"
#include "openglwidget.h"
#include "projectio.h"
#include "selectionattention.h"
#include "childrenpanel/tooloptpanel.h"
#include "childrenpanel/toolcontrolconfig.h"
#include "childrenpanel/toolspanel.h"
#include "pythonbind/python_bindings.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDropEvent>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSizeF>
#include <QRectF>
#include <QTransform>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <string>

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots
#define ANIMEAN_RESTORE_QT_SLOTS
#endif
#include <pybind11/embed.h>
#include <pybind11/eval.h>
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

QString toolControlName(PaintOpenGLWidget::Tool tool)
{
    switch (tool) {
    case PaintOpenGLWidget::Tool::Pen:
        return QStringLiteral("pen");
    case PaintOpenGLWidget::Tool::Eraser:
        return QStringLiteral("eraser");
    case PaintOpenGLWidget::Tool::DeleteLine:
        return QStringLiteral("delete_line");
    case PaintOpenGLWidget::Tool::Fill:
        return QStringLiteral("fill");
    case PaintOpenGLWidget::Tool::Move:
        return QStringLiteral("move");
    }
    return QStringLiteral("pen");
}

int frameNameToRow(const QString &frameName)
{
    QString digits;
    for (const QChar ch : frameName) {
        if (!ch.isDigit()) {
            break;
        }
        digits.append(ch);
    }

    bool ok = false;
    const int frameNumber = digits.toInt(&ok);
    return ok && frameNumber > 0 ? frameNumber - 1 : 0;
}

QVector<ToolsPanel::ExtraToolDefinition> parseExtraTools(const QJsonArray &array)
{
    QVector<ToolsPanel::ExtraToolDefinition> tools;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        ToolsPanel::ExtraToolDefinition tool;
        tool.name = object.value(QStringLiteral("name")).toString();
        tool.title = object.value(QStringLiteral("title")).toString(tool.name);
        tool.property = object.value(QStringLiteral("property")).toString(tool.name);
        tool.handler = object.value(QStringLiteral("handler")).toString();
        tool.baseTool = object.value(QStringLiteral("base_tool")).toString();
        if (!tool.name.isEmpty()) {
            tools.append(tool);
        }
    }
    return tools;
}

#ifdef ANIMEAN_WITH_PYTHON
qreal dictNumber(const py::dict &data, const char *key, qreal fallback = 0.0)
{
    if (!data.contains(py::str(key))) {
        return fallback;
    }
    try {
        return data[py::str(key)].cast<qreal>();
    } catch (const py::cast_error &) {
        return fallback;
    }
}

QPointF pointFromDict(const py::dict &data)
{
    return QPointF(dictNumber(data, "x"), dictNumber(data, "y"));
}

qreal thicknessFromDict(const py::dict &data)
{
    return dictNumber(data, "thick", 3.0);
}

void appendQuadraticMetricPoints(QVector<QPointF> *points,
                                 const QPointF &p0,
                                 const QPointF &p1,
                                 const QPointF &p2,
                                 int sampleCount)
{
    if (points->isEmpty()) {
        points->append(p0);
    }

    sampleCount = std::max(2, sampleCount);
    for (int i = 1; i <= sampleCount; ++i) {
        const qreal t = static_cast<qreal>(i) / sampleCount;
        const qreal omt = 1.0 - t;
        points->append((omt * omt) * p0 + (2.0 * omt * t) * p1 + (t * t) * p2);
    }
}

QVector<PaintOpenGLWidget::ImportedVectorFrame> importedFramesFromOpenToonzLevel(const py::dict &levelData)
{
    QVector<PaintOpenGLWidget::ImportedVectorFrame> importedFrames;
    if (!levelData.contains(py::str("frames"))) {
        return importedFrames;
    }

    py::iterable frames = levelData[py::str("frames")];
    for (py::handle frameHandle : frames) {
        if (!py::isinstance<py::dict>(frameHandle)) {
            continue;
        }
        py::dict frameDict = py::reinterpret_borrow<py::dict>(frameHandle);
        PaintOpenGLWidget::ImportedVectorFrame importedFrame;
        QString frameName = QStringLiteral("1");
        if (frameDict.contains(py::str("frame"))) {
            const std::string frameNameText = frameDict[py::str("frame")].cast<std::string>();
            frameName = QString::fromUtf8(frameNameText.c_str());
        }
        importedFrame.row = frameNameToRow(frameName);

        if (!frameDict.contains(py::str("strokes"))) {
            continue;
        }
        py::iterable strokes = frameDict[py::str("strokes")];
        for (py::handle strokeHandle : strokes) {
            if (!py::isinstance<py::dict>(strokeHandle)) {
                continue;
            }
            py::dict strokeDict = py::reinterpret_borrow<py::dict>(strokeHandle);
            if (!strokeDict.contains(py::str("quadratics"))) {
                continue;
            }

            PaintOpenGLWidget::ImportedVectorStroke importedStroke;
            bool hasPathStart = false;
            qreal thicknessTotal = 0.0;
            int thicknessCount = 0;
            py::iterable quadratics = strokeDict[py::str("quadratics")];
            for (py::handle quadraticHandle : quadratics) {
                if (!py::isinstance<py::dict>(quadraticHandle)) {
                    continue;
                }
                py::dict quadratic = py::reinterpret_borrow<py::dict>(quadraticHandle);
                if (!quadratic.contains(py::str("p0")) ||
                    !quadratic.contains(py::str("p1")) ||
                    !quadratic.contains(py::str("p2"))) {
                    continue;
                }

                py::dict p0Dict = py::reinterpret_borrow<py::dict>(quadratic[py::str("p0")]);
                py::dict p1Dict = py::reinterpret_borrow<py::dict>(quadratic[py::str("p1")]);
                py::dict p2Dict = py::reinterpret_borrow<py::dict>(quadratic[py::str("p2")]);
                const QPointF p0 = pointFromDict(p0Dict);
                const QPointF p1 = pointFromDict(p1Dict);
                const QPointF p2 = pointFromDict(p2Dict);
                if (!hasPathStart) {
                    importedStroke.path.moveTo(p0);
                    hasPathStart = true;
                } else if (!qFuzzyCompare(importedStroke.path.currentPosition().x(), p0.x()) ||
                           !qFuzzyCompare(importedStroke.path.currentPosition().y(), p0.y())) {
                    importedStroke.path.lineTo(p0);
                }
                importedStroke.path.quadTo(p1, p2);
                const qreal approxLength = QLineF(p0, p1).length() + QLineF(p1, p2).length();
                appendQuadraticMetricPoints(&importedStroke.points,
                                            p0,
                                            p1,
                                            p2,
                                            std::max(4, static_cast<int>(std::ceil(approxLength / 6.0))));
                thicknessTotal += thicknessFromDict(p0Dict) + thicknessFromDict(p1Dict) + thicknessFromDict(p2Dict);
                thicknessCount += 3;
            }

            if (importedStroke.points.size() >= 2) {
                importedStroke.width = thicknessCount > 0
                                           ? std::max(qreal(1.0), thicknessTotal / thicknessCount)
                                           : std::max(qreal(1.0), dictNumber(strokeDict, "max_thickness", 3.0) * 0.2);
                importedFrame.strokes.append(importedStroke);
            }
        }

        if (!importedFrame.strokes.isEmpty()) {
            importedFrames.append(importedFrame);
        }
    }
    return importedFrames;
}
#endif

QRectF importedVectorFramesBounds(const QVector<PaintOpenGLWidget::ImportedVectorFrame> &frames)
{
    QRectF bounds;
    bool hasBounds = false;
    for (const PaintOpenGLWidget::ImportedVectorFrame &frame : frames) {
        for (const PaintOpenGLWidget::ImportedVectorStroke &stroke : frame.strokes) {
            const QRectF strokeBounds = stroke.path.isEmpty()
                                            ? QRectF()
                                            : stroke.path.boundingRect();
            if (!strokeBounds.isValid()) {
                continue;
            }
            if (!hasBounds) {
                bounds = strokeBounds;
                hasBounds = true;
            } else {
                bounds = bounds.united(strokeBounds);
            }
        }
    }
    return bounds;
}

void scaleImportedVectorFramesToCanvas(QVector<PaintOpenGLWidget::ImportedVectorFrame> *frames, const QSizeF &targetSize)
{
    if (!frames || targetSize.width() <= 0.0 || targetSize.height() <= 0.0) {
        return;
    }

    const QRectF sourceBounds = importedVectorFramesBounds(*frames);
    if (!sourceBounds.isValid() || sourceBounds.width() <= 0.0 || sourceBounds.height() <= 0.0) {
        return;
    }

    const qreal scaleX = targetSize.width() / sourceBounds.width();
    const qreal scaleY = targetSize.height() / sourceBounds.height();
    const qreal scale = std::min(scaleX, scaleY);
    const QPointF sourceCenter = sourceBounds.center();
    const QPointF targetCenter(targetSize.width() * 0.5, targetSize.height() * 0.5);

    for (PaintOpenGLWidget::ImportedVectorFrame &frame : *frames) {
        for (PaintOpenGLWidget::ImportedVectorStroke &stroke : frame.strokes) {
            for (QPointF &point : stroke.points) {
                point = targetCenter + (point - sourceCenter) * scale;
            }
            const qreal dx = targetCenter.x() - sourceCenter.x() * scale;
            const qreal dy = targetCenter.y() - sourceCenter.y() * scale;
            const QTransform transform(scale, 0.0, 0.0, scale, dx, dy);
            stroke.path = transform.map(stroke.path);
            stroke.width = std::max(qreal(1.0), stroke.width * scale);
        }
    }
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupDocks();
    setupListDragDrop();

    for (PaintOpenGLWidget *view : m_paintViews) {
        updateAttention(view,
                        AttentionChange::FrameChange,
                        view->model().currentFrame(),
                        view->model().currentLayer(),
                        view->model().currentAsset());
    }
    setupConnections();
    updateWindowTitle();
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&m_childPaintWidget->model());
    registerAnimeanUiScene(&m_paintWidget->model());
    registerAnimeanUiRefreshCallback([this](bool frame, bool layer, bool asset, bool widget) {
        PaintOpenGLWidget *view = activePaintWidget();
        SelectionAttention &attention = attentionFor(view);
        attention.frame = view->model().currentFrame();
        attention.layer = view->model().currentLayer();
        attention.asset = view->model().currentAsset();
        view->setCurrentFrame(attention.frame);
        view->setCurrentLayer(attention.layer);
        view->setCurrentAsset(attention.asset);
        if (frame) {
            refreshFrameList(attentionFor(framePanelTarget()).frame);
        }
        if (layer) {
            refreshLayerList(attentionFor(layerPanelTarget()).layer);
        }
        if (asset) {
            refreshAssetList(attentionFor(assetPanelTarget()).asset);
        }
        if (widget) {
            for (PaintOpenGLWidget *paintView : m_paintViews) {
                paintView->update();
            }
        }
        syncEmbeddedPythonState();
    });
    registerAnimeanUiFreezeCallback([this](bool frozen) {
        setPythonUiFrozen(frozen);
    });
    registerAnimeanUiOverlayCallback([this](const QString &view, const QVector<AnimeanOverlayItem> &items) {
        PaintOpenGLWidget *target = m_paintWidget;
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            if (paintView->viewName() == view) {
                target = paintView;
                break;
            }
        }
        QVector<PaintOpenGLWidget::OverlayItem> converted;
        converted.reserve(items.size());
        for (const AnimeanOverlayItem &item : items) {
            PaintOpenGLWidget::OverlayItem overlayItem;
            overlayItem.id = item.id;
            overlayItem.points = item.points;
            overlayItem.closed = item.closed;
            overlayItem.strokeColor = item.strokeColor;
            overlayItem.fillColor = item.fillColor;
            overlayItem.width = item.width;
            overlayItem.removable = item.removable;
            converted.append(overlayItem);
        }
        target->setOverlayItems(converted);
    });
    registerAnimeanUiDrawColorCallback([this](const QColor &color) {
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            paintView->setDrawingColor(color);
        }
    });
    syncEmbeddedPythonState();
    appendPythonDebugMessage(QStringLiteral(
        "[python register] animean_python, animemodel, ui, model, current, model_pybind, vectorlogic, canvas_width, canvas_height, "
        "main_model, child_model, active_view"));
    runPythonInitializationScript();
#endif
}

MainWindow::~MainWindow()
{
#ifdef ANIMEAN_WITH_PYTHON
    clearAnimeanUiDrawColorCallback();
    clearAnimeanUiOverlayCallback();
    clearAnimeanUiFreezeCallback();
    clearAnimeanUiRefreshCallback();
    unregisterAnimeanUiScene(&m_childPaintWidget->model());
    unregisterAnimeanUiScene(&m_paintWidget->model());
#endif
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
    m_paintWidget->setViewName(QStringLiteral("main"));
    m_paintWidget->model().setTextId(QStringLiteral("main_paint_view"));
    m_paintWidget->model().setIntId(1);
    createChildPaintDock();
    m_activePaintWidget = m_paintWidget;
    m_paintViews = {m_paintWidget, m_childPaintWidget};
    createListDocks();
    createToolDocks();
    setupPythonDebugDock();
    createTextureFileMenu();
}

void MainWindow::createChildPaintDock()
{
    m_childPaintWindow = new ChildPaintWindow(this);
    m_childPaintWidget = m_childPaintWindow->paintWidget();
    m_childPaintWidget->model().setTextId(QStringLiteral("child_paint_view"));
    m_childPaintWidget->model().setIntId(2);
    addDockWidget(Qt::LeftDockWidgetArea, m_childPaintWindow);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(m_childPaintWindow->toggleViewAction());
}

void MainWindow::createTextureFileMenu()
{
    QMenu *textureMenu = menuBar()->addMenu(QStringLiteral("Texture View File"));

    QAction *importRasterAction = textureMenu->addAction(QStringLiteral("Import Raster into Texture View..."));
    connect(importRasterAction, &QAction::triggered, this, [this]() {
        showTextureView();
        importRaster(m_childPaintWidget);
    });

    QAction *importToonzAction = textureMenu->addAction(QStringLiteral("Import OpenToonz Lines into Texture View..."));
    connect(importToonzAction, &QAction::triggered, this, [this]() {
        showTextureView();
        importOpenToonzLines(m_childPaintWidget);
    });

    QAction *importClipAction = textureMenu->addAction(QStringLiteral("Import Clip Studio Paint into Texture View..."));
    connect(importClipAction, &QAction::triggered, this, [this]() {
        showTextureView();
        importClipStudioPaint(m_childPaintWidget);
    });

    textureMenu->addSeparator();

    QAction *openAction = textureMenu->addAction(QStringLiteral("Open Texture View..."));
    connect(openAction, &QAction::triggered, this, &MainWindow::openTextureView);

    QAction *saveAction = textureMenu->addAction(QStringLiteral("Save Texture View As..."));
    connect(saveAction, &QAction::triggered, this, [this]() { saveTextureViewAs(); });

    QAction *exportImageAction = textureMenu->addAction(QStringLiteral("Export Texture View Image..."));
    connect(exportImageAction, &QAction::triggered, this, [this]() { exportTextureImage(); });
}

void MainWindow::showTextureView()
{
    if (!m_childPaintWindow) {
        return;
    }
    m_childPaintWindow->show();
    m_childPaintWindow->raise();
    setActivePaintView(m_childPaintWidget);
}

void MainWindow::openTextureView()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Texture View"),
        QString(),
        projectFilter());
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Texture View"),
                             QStringLiteral("Failed to read file:\n%1").arg(file.errorString()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Texture View"),
                             QStringLiteral("Texture file format error:\n%1").arg(parseError.errorString()));
        return;
    }

    AnimeSceneModel loadedModel;
    QString error;
    if (!modelFromJson(document.object(), &loadedModel, &error)) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Texture View"),
                             error.isEmpty() ? QStringLiteral("Unsupported texture file.") : error);
        return;
    }

    m_childPaintWidget->model() = loadedModel;
    m_childPaintWidget->model().setTextId(QStringLiteral("child_paint_view"));
    m_childPaintWidget->model().setIntId(2);
    showTextureView();
    updateAttention(m_childPaintWidget,
                    AttentionChange::FrameChange,
                    m_childPaintWidget->model().currentFrame(),
                    m_childPaintWidget->model().currentLayer(),
                    m_childPaintWidget->model().currentAsset());
    m_childPaintWidget->update();
    setStatusText(QStringLiteral("Opened texture view: %1").arg(QFileInfo(fileName).fileName()));
}

bool MainWindow::saveTextureViewAs()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Texture View As"),
        QDir::home().filePath(QStringLiteral("texture.animean")),
        projectFilter());
    if (fileName.isEmpty()) {
        return false;
    }
    if (QFileInfo(fileName).suffix().isEmpty()) {
        fileName += QStringLiteral(".animean");
    }
    return writeModelToFile(m_childPaintWidget->model(), fileName, QStringLiteral("Save Texture View"));
}

bool MainWindow::writeModelToFile(const AnimeSceneModel &model, const QString &fileName, const QString &dialogTitle)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this,
                             dialogTitle,
                             QStringLiteral("Failed to write file:\n%1").arg(file.errorString()));
        return false;
    }

    const QJsonDocument document(modelToJson(model));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this,
                             dialogTitle,
                             QStringLiteral("Failed to save file:\n%1").arg(file.errorString()));
        return false;
    }

    setStatusText(QStringLiteral("Saved texture view: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

bool MainWindow::exportTextureImage()
{
    if (!m_childPaintWidget) {
        return false;
    }

    showTextureView();
    QCoreApplication::processEvents();
    const QImage image = m_childPaintWidget->grabFramebuffer();
    if (image.isNull()) {
        QMessageBox::warning(this,
                             QStringLiteral("Export Texture View Image"),
                             QStringLiteral("The texture view could not be captured. Make sure it is visible, then try again."));
        return false;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Texture View Image"),
        QDir::home().filePath(QStringLiteral("texture.png")),
        QStringLiteral("PNG Image (*.png);;All Files (*)"));
    if (fileName.isEmpty()) {
        return false;
    }
    if (QFileInfo(fileName).suffix().isEmpty()) {
        fileName += QStringLiteral(".png");
    }

    if (!image.save(fileName, "PNG")) {
        QMessageBox::warning(this,
                             QStringLiteral("Export Texture View Image"),
                             QStringLiteral("Failed to write the image to:\n%1").arg(fileName));
        return false;
    }

    setStatusText(QStringLiteral("Exported texture image: %1 (%2 x %3)")
                      .arg(QFileInfo(fileName).fileName())
                      .arg(image.width())
                      .arg(image.height()));
    return true;
}

PaintOpenGLWidget *MainWindow::activePaintWidget() const
{
    return m_activePaintWidget ? m_activePaintWidget : m_paintWidget;
}

PaintOpenGLWidget *MainWindow::framePanelTarget() const
{
    PaintOpenGLWidget *view = activePaintWidget();
    if (view == m_childPaintWidget && m_childPaintWindow && !m_childPaintWindow->changableTimeline()) {
        return m_paintWidget;
    }
    return view;
}

PaintOpenGLWidget *MainWindow::layerPanelTarget() const
{
    PaintOpenGLWidget *view = activePaintWidget();
    if (view == m_childPaintWidget && m_childPaintWindow && !m_childPaintWindow->changableLayer()) {
        return m_paintWidget;
    }
    return view;
}

PaintOpenGLWidget *MainWindow::assetPanelTarget() const
{
    return layerPanelTarget();
}

SelectionAttention &MainWindow::attentionFor(PaintOpenGLWidget *view)
{
    return m_attentionByView[view];
}

void MainWindow::setActivePaintView(PaintOpenGLWidget *view)
{
    if (!view || m_activePaintWidget == view) {
        return;
    }

    m_activePaintWidget = view;
    m_hasPendingAttention = false;
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&view->model());
#endif
    refreshPanelTargets();
    syncEmbeddedPythonState();
    setStatusText(QStringLiteral("Active paint view: %1").arg(view->viewName()));
}

void MainWindow::refreshPanelTargets()
{
    refreshFrameList(attentionFor(framePanelTarget()).frame);
    refreshLayerList(attentionFor(layerPanelTarget()).layer);
    refreshAssetList(attentionFor(assetPanelTarget()).asset);
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

void MainWindow::setupPythonDebugDock()
{
    m_pythonDebugDock = new QDockWidget(QStringLiteral("Python Debug"), this);
    m_pythonDebugDock->setObjectName(QStringLiteral("PythonDebugDock"));

    QWidget *panel = new QWidget(m_pythonDebugDock);
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_pythonDebugOutput = new QPlainTextEdit(panel);
    m_pythonDebugOutput->setObjectName(QStringLiteral("PythonDebugOutput"));
    m_pythonDebugOutput->setPlaceholderText(QStringLiteral("Python debug output"));
    m_pythonDebugOutput->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    QHBoxLayout *commandLayout = new QHBoxLayout;
    QLabel *commandLabel = new QLabel(QStringLiteral("Command"), panel);
    m_pythonDebugCommand = new QLineEdit(panel);
    m_pythonDebugCommand->setObjectName(QStringLiteral("PythonDebugCommand"));
    m_pythonDebugCommand->setText(QStringLiteral("bind_test.py"));
    QPushButton *runButton = new QPushButton(QStringLiteral("Run"), panel);

    commandLayout->addWidget(commandLabel);
    commandLayout->addWidget(m_pythonDebugCommand, 1);
    commandLayout->addWidget(runButton);
    layout->addWidget(m_pythonDebugOutput, 1);
    layout->addLayout(commandLayout);

    m_pythonDebugDock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, m_pythonDebugDock);

    connect(runButton, &QPushButton::clicked, this, [this]() {
        runPythonDebugCommand(m_pythonDebugCommand->text());
    });
    connect(m_pythonDebugCommand, &QLineEdit::returnPressed, this, [this]() {
        runPythonDebugCommand(m_pythonDebugCommand->text());
    });
}

void MainWindow::runPythonDebugCommand(const QString &command)
{
    if (!m_pythonDebugOutput) {
        return;
    }

    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_pythonDebugOutput->appendPlainText(QStringLiteral("> %1").arg(trimmed));
    m_pythonDebugOutput->appendPlainText(runEmbeddedPythonCommand(trimmed).trimmed());
    m_pythonDebugOutput->appendPlainText(QString());
    m_pythonDebugOutput->verticalScrollBar()->setValue(m_pythonDebugOutput->verticalScrollBar()->maximum());
}

void MainWindow::appendPythonDebugMessage(const QString &message)
{
    if (!m_pythonDebugOutput || message.trimmed().isEmpty()) {
        return;
    }

    m_pythonDebugOutput->appendPlainText(message.trimmed());
    m_pythonDebugOutput->appendPlainText(QString());
    m_pythonDebugOutput->verticalScrollBar()->setValue(m_pythonDebugOutput->verticalScrollBar()->maximum());
}

void MainWindow::runPythonInitializationScript()
{
#ifdef ANIMEAN_WITH_PYTHON
    appendPythonDebugMessage(QStringLiteral("> initalize.py"));
    appendPythonDebugMessage(runEmbeddedPythonCommand(QStringLiteral("initalize.py")).trimmed());
#endif
}

QString MainWindow::resolvePythonScriptPath(const QString &scriptName) const
{
    const QFileInfo directInfo(scriptName);
    if (directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }

    const QString appPath = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appPath).filePath(scriptName),
#ifdef ANIMEAN_WITH_PYTHON
        QDir(QStringLiteral(ANIMEAN_PYFILE_DIR)).filePath(scriptName),
        QDir(QStringLiteral(ANIMEAN_SOURCE_DIR)).filePath(scriptName),
#endif
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QString();
}

QString MainWindow::runEmbeddedPythonCommand(const QString &command)
{
#ifdef ANIMEAN_WITH_PYTHON
    try {
        py::gil_scoped_acquire acquire;
        py::dict globals = py::module_::import("__main__").attr("__dict__");
        syncEmbeddedPythonState();

        const QString runPrefix = QStringLiteral("run ");
        QString scriptName;
        if (command.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive)) {
            scriptName = command;
        } else if (command.startsWith(runPrefix, Qt::CaseInsensitive)) {
            scriptName = command.mid(runPrefix.size()).trimmed();
        }

        const bool runScript = !scriptName.isEmpty();
        const QString scriptPath = runScript ? resolvePythonScriptPath(scriptName) : QString();
        if (runScript && scriptPath.isEmpty()) {
            return QStringLiteral("Python script not found: %1").arg(scriptName);
        }

        py::object io = py::module_::import("io");
        py::object contextlib = py::module_::import("contextlib");
        py::object stdoutBuffer = io.attr("StringIO")();
        py::object stderrBuffer = io.attr("StringIO")();
        py::object stdoutRedirect = contextlib.attr("redirect_stdout")(stdoutBuffer);
        py::object stderrRedirect = contextlib.attr("redirect_stderr")(stderrBuffer);

        QString errorText;
        stdoutRedirect.attr("__enter__")();
        stderrRedirect.attr("__enter__")();
        try {
            if (runScript) {
                globals["__file__"] = scriptPath.toStdString();
                const QString normalizedPath = QDir::toNativeSeparators(scriptPath);
                const QString scriptLiteral = QString::fromStdString(
                    py::repr(py::str(normalizedPath.toStdString())).cast<std::string>());
                const QString code = QStringLiteral(
                    "with open(%1, 'r', encoding='utf-8') as __animean_file:\n"
                    "    __animean_code = __animean_file.read()\n"
                    "exec(compile(__animean_code, %1, 'exec'), globals())\n")
                                         .arg(scriptLiteral);
                py::exec(code.toStdString(), globals);
            } else {
                const char *runner =
                    "import ast\n"
                    "try:\n"
                    "    __animean_expr = ast.parse(__animean_command, mode='eval')\n"
                    "except SyntaxError:\n"
                    "    exec(__animean_command, globals())\n"
                    "else:\n"
                    "    __animean_value = eval(compile(__animean_expr, '<AnimeAn debug>', 'eval'), globals())\n"
                    "    if __animean_value is not None:\n"
                    "        print(repr(__animean_value))\n";
                globals["__animean_command"] = command.toStdString();
                py::exec(runner, globals);
            }
        } catch (const py::error_already_set &error) {
            errorText = QString::fromUtf8(error.what());
        }
        stderrRedirect.attr("__exit__")(py::none(), py::none(), py::none());
        stdoutRedirect.attr("__exit__")(py::none(), py::none(), py::none());

        QString output = QString::fromUtf8(stdoutBuffer.attr("getvalue")().cast<std::string>().c_str());
        const QString stderrOutput = QString::fromUtf8(stderrBuffer.attr("getvalue")().cast<std::string>().c_str());
        if (!stderrOutput.isEmpty()) {
            output += stderrOutput;
        }
        if (!errorText.isEmpty()) {
            output += errorText;
        }
        if (output.trimmed().isEmpty()) {
            output = QStringLiteral("(no output)");
        }
        PaintOpenGLWidget *view = activePaintWidget();
        updateAttention(view,
                        AttentionChange::AssetChange,
                        view->model().currentFrame(),
                        view->model().currentLayer(),
                        view->model().currentAsset());
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            paintView->update();
        }
        return output;
    } catch (const py::error_already_set &error) {
        return QStringLiteral("Python debug error: %1").arg(QString::fromUtf8(error.what()));
    }
#else
    Q_UNUSED(command);
    return QStringLiteral("Python disabled");
#endif
}

void MainWindow::syncEmbeddedPythonState()
{
#ifdef ANIMEAN_WITH_PYTHON
    try {
        py::gil_scoped_acquire acquire;
        py::dict globals = py::module_::import("__main__").attr("__dict__");
        py::object animeanPython = py::module_::import("animean_python");
        py::object animeModel = py::module_::import("animemodel");
        PaintOpenGLWidget *view = activePaintWidget();
        globals["animean_python"] = animeanPython;
        globals["animemodel"] = animeModel;
        globals["ui"] = animeModel.attr("ui");
        globals["model"] = py::cast(&view->model(), py::return_value_policy::reference);
        globals["main_model"] = py::cast(&m_paintWidget->model(), py::return_value_policy::reference);
        globals["child_model"] = py::cast(&m_childPaintWidget->model(), py::return_value_policy::reference);
        globals["active_view"] = view->viewName().toStdString();
        globals["current"] = animeModel.attr("current");
        globals["model_pybind"] = animeanPython.attr("model_pybind");
        globals["vectorlogic"] = animeanPython.attr("vectorlogic");
        globals["canvas_width"] = view->width();
        globals["canvas_height"] = view->height();
        globals["main_canvas_width"] = m_paintWidget->width();
        globals["main_canvas_height"] = m_paintWidget->height();
        globals["child_canvas_width"] = m_childPaintWidget->width();
        globals["child_canvas_height"] = m_childPaintWidget->height();
    } catch (const py::error_already_set &error) {
        appendPythonDebugMessage(QStringLiteral("[python register] error: %1").arg(QString::fromUtf8(error.what())));
        setStatusText(QStringLiteral("Python state sync error: %1").arg(QString::fromUtf8(error.what())));
    }
#endif
}

void MainWindow::setupConnections()
{
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openProject);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveProject);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveProjectAs);

    for (PaintOpenGLWidget *view : m_paintViews) {
        connect(view, &PaintOpenGLWidget::focusGained, this, [this, view]() {
            setActivePaintView(view);
        });

        connect(view, &PaintOpenGLWidget::layerListChanged, this, [this, view](int selectedLayer) {
            updateAttention(view,
                            AttentionChange::LayerChange,
                            view->model().currentFrame(),
                            selectedLayer,
                            view->model().currentAsset());
        });

        connect(view, &PaintOpenGLWidget::assetListChanged, this, [this, view](int selectedAsset) {
            updateAttention(view,
                            AttentionChange::AssetChange,
                            attentionFor(view).frame,
                            attentionFor(view).layer,
                            selectedAsset);
        });

        connect(view, &PaintOpenGLWidget::pythonDebugMessage,
                this, &MainWindow::appendPythonDebugMessage);
    }

    connect(m_childPaintWindow, &ChildPaintWindow::changableTimelineToggled, this, [this](bool) {
        refreshPanelTargets();
    });
    connect(m_childPaintWindow, &ChildPaintWindow::changableLayerToggled, this, [this](bool) {
        refreshPanelTargets();
    });

    connect(m_layerPanel->layerList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            QListWidgetItem *item = m_layerPanel->layerList()->item(row);
            const int layerIndex = item ? item->data(Qt::UserRole).toInt() : -1;
            PaintOpenGLWidget *view = layerPanelTarget();
            requestAttentionUpdate(view, AttentionChange::LayerChange,
                                   attentionFor(view).frame, layerIndex, attentionFor(view).asset);
        }
    });

    connect(m_framePanel->frameList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            PaintOpenGLWidget *view = framePanelTarget();
            requestAttentionUpdate(view, AttentionChange::FrameChange,
                                   row, attentionFor(view).layer, attentionFor(view).asset);
        }
    });

    connect(m_layerPanel->addButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = layerPanelTarget();
        const int layerIndex = view->addLayer();
        updateAttention(view, AttentionChange::LayerChange,
                        attentionFor(view).frame, layerIndex, attentionFor(view).asset);
    });

    connect(m_layerPanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = m_layerPanel->layerList()->currentItem();
        const int layerIndex = item ? item->data(Qt::UserRole).toInt() : -1;
        PaintOpenGLWidget *view = layerPanelTarget();
        if (view->deleteLayer(layerIndex)) {
            const int nextLayer = layerIndex < view->layerCount() ? layerIndex : view->layerCount() - 1;
            updateAttention(view, AttentionChange::LayerChange,
                            attentionFor(view).frame, nextLayer, attentionFor(view).asset);
        }
    });

    connect(m_layerPanel->unselectButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = layerPanelTarget();
        updateAttention(view, AttentionChange::LayerChange, attentionFor(view).frame, -1, -1);
    });

    connect(m_framePanel->addButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = framePanelTarget();
        const int frameIndex = view->addFrame();
        updateAttention(view, AttentionChange::FrameChange,
                        frameIndex, attentionFor(view).layer, attentionFor(view).asset);
    });

    connect(m_framePanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        const int row = m_framePanel->frameList()->currentRow();
        PaintOpenGLWidget *view = framePanelTarget();
        if (view->deleteFrame(row)) {
            const int nextFrame = row < view->frameCount() ? row : view->frameCount() - 1;
            updateAttention(view, AttentionChange::FrameChange,
                            nextFrame, attentionFor(view).layer, attentionFor(view).asset);
        }
    });

    connect(m_assetPanel->addButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = assetPanelTarget();
        const int assetIndex = view->addAsset();
        updateAttention(view, AttentionChange::AssetChange,
                        attentionFor(view).frame, attentionFor(view).layer, assetIndex);
    });

    connect(m_assetPanel->unselectButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = assetPanelTarget();
        updateAttention(view, AttentionChange::AssetChange, attentionFor(view).frame, -1, -1);
    });

    connect(m_assetPanel->assetList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists) {
            PaintOpenGLWidget *view = assetPanelTarget();
            requestAttentionUpdate(view, AttentionChange::AssetChange,
                                   attentionFor(view).frame, attentionFor(view).layer, row);
        }
    });

    connect(m_layerPanel->layerList()->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        PaintOpenGLWidget *view = layerPanelTarget();
        const int targetRow = movedRowTarget(sourceStart, destinationChild);
        QListWidgetItem *movedItem = m_layerPanel->layerList()->item(targetRow);
        if (!movedItem) {
            updateAttention(view,
                            AttentionChange::LayerChange,
                            view->model().currentFrame(),
                            view->model().currentLayer(),
                            view->model().currentAsset());
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

        if (!view->moveLayer(fromIndex, toIndex)) {
            updateAttention(view,
                            AttentionChange::LayerChange,
                            view->model().currentFrame(),
                            view->model().currentLayer(),
                            view->model().currentAsset());
            return;
        }
        updateAttention(view, AttentionChange::LayerChange,
                        attentionFor(view).frame, toIndex, attentionFor(view).asset);
    });

    connect(m_framePanel->frameList()->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        PaintOpenGLWidget *view = framePanelTarget();
        const int target = movedRowTarget(sourceStart, destinationChild);
        if (!view->moveFrame(sourceStart, target)) {
            updateAttention(view,
                            AttentionChange::FrameChange,
                            view->model().currentFrame(),
                            view->model().currentLayer(),
                            view->model().currentAsset());
            return;
        }
        updateAttention(view, AttentionChange::FrameChange,
                        target, attentionFor(view).layer, attentionFor(view).asset);
    });

    connect(ui->actionimport_Raster, &QAction::triggered, this, [this]() {
        setActivePaintView(m_paintWidget);
        importRaster(m_paintWidget);
    });
    connect(ui->actionImport_OpenToonz_Lines, &QAction::triggered, this, [this]() {
        setActivePaintView(m_paintWidget);
        importOpenToonzLines(m_paintWidget);
    });
    connect(ui->actionImport_Clip_Studio_Paint, &QAction::triggered, this, [this]() {
        setActivePaintView(m_paintWidget);
        importClipStudioPaint(m_paintWidget);
    });
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
                const bool shouldCommit = m_hasPendingAttention && !m_listDragActive && m_pendingAttentionView;
                const AttentionChange change = m_pendingAttentionChange;
                const SelectionAttention attention = m_pendingAttention;
                PaintOpenGLWidget *view = m_pendingAttentionView;
                m_listMousePressed = false;
                m_listDragActive = false;
                m_hasPendingAttention = false;
                if (shouldCommit) {
                    updateAttention(view, change, attention.frame, attention.layer, attention.asset);
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
            PaintOpenGLWidget *view = layerPanelTarget();
            const int layerIndex = view->addLayerForAsset(assetIndex);
            if (layerIndex >= 0) {
                updateAttention(view, AttentionChange::LayerChange,
                                attentionFor(view).frame, layerIndex, assetIndex);
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

    m_toolsDock = new QDockWidget(QStringLiteral("Tools"), this);
    m_toolsDock->setWidget(toolsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);

    m_toolOptDock = new QDockWidget(QStringLiteral("Tool Options"), this);
    m_toolOptDock->setWidget(toolOptPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_toolOptDock);
    splitDockWidget(m_toolOptDock, m_layerDock, Qt::Vertical);
    splitDockWidget(m_layerDock, m_assetDock, Qt::Vertical);

#ifdef ANIMEAN_WITH_PYTHON
    try {
        const std::string json = py::module_::import("extra_tools")
                                     .attr("tools_json")()
                                     .cast<std::string>();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isArray()) {
            toolsPanel->setExtraTools(parseExtraTools(document.array()));
        } else {
            setStatusText(QStringLiteral("extra_tools JSON error: %1").arg(parseError.errorString()));
        }
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("extra_tools.py error: %1").arg(QString::fromUtf8(error.what())));
    }
#endif

    auto loadToolOptions = [this, toolOptPanel](PaintOpenGLWidget::Tool tool) {
        QJsonObject layout;
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::dict state;
            state["smooth"] = m_toolSmoothValue;
            state["pen_width"] = m_toolPenWidth;
            state["fill_scope"] = m_toolFillAllLayers ? "all" : "current";
            const std::string json = py::module_::import("toolcontrol")
                                         .attr("options_for_tool_json")(toolControlName(tool).toStdString(), state)
                                         .cast<std::string>();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                layout = document.object();
            } else {
                setStatusText(QStringLiteral("toolcontrol JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            setStatusText(QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
#endif
        if (layout.isEmpty()) {
            layout = ToolControlConfig::loadBuiltInToolLayout(
                tool,
                m_toolSmoothValue,
                m_toolPenWidth,
                m_toolFillAllLayers ? PaintOpenGLWidget::FillScope::AllLayers
                                    : PaintOpenGLWidget::FillScope::CurrentLayer);
        }
        toolOptPanel->configureLayout(layout);
    };

    auto applyTool = [this, toolsPanel, toolOptPanel, loadToolOptions](PaintOpenGLWidget::Tool tool, bool reloadOptions) {
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setTool(tool);
            view->setStrokeProperty(QString());
        }
        toolsPanel->setTool(tool);
        toolOptPanel->setTool(tool);
        if (reloadOptions) {
            loadToolOptions(tool);
        }
    };

    auto selectTool = [applyTool](PaintOpenGLWidget::Tool tool) {
        applyTool(tool, true);
    };

    loadToolOptions(PaintOpenGLWidget::Tool::Pen);

    connect(toolsPanel, &ToolsPanel::toolSelected, this, selectTool);
    connect(toolsPanel, &ToolsPanel::extraToolSelected, this, [this, toolOptPanel](const ToolsPanel::ExtraToolDefinition &tool) {
        const PaintOpenGLWidget::Tool baseTool = tool.baseTool == QStringLiteral("fill")
                                                     ? PaintOpenGLWidget::Tool::Fill
                                                     : PaintOpenGLWidget::Tool::Pen;
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setTool(baseTool);
            view->setStrokeProperty(tool.property);
        }
        activePaintWidget()->sendPythonExtraToolMessage(tool.name, tool.property);
        toolOptPanel->setTool(baseTool);
        toolOptPanel->configureLayout(QJsonObject());
#ifdef ANIMEAN_WITH_PYTHON
        if (!tool.handler.isEmpty()) {
            try {
                py::module_::import("extra_tools")
                    .attr("run_tool_handler")(tool.handler.toStdString(), tool.name.toStdString(), tool.property.toStdString());
            } catch (const py::error_already_set &error) {
                setStatusText(QStringLiteral("extra tool error: %1").arg(QString::fromUtf8(error.what())));
            }
        }
#endif
    });

    connect(toolOptPanel, &ToolOptPanel::colorSelected, this, [this, applyTool](const QColor &color) {
        if (activePaintWidget()->tool() == PaintOpenGLWidget::Tool::Fill) {
            for (PaintOpenGLWidget *view : m_paintViews) {
                view->setDrawingColor(color);
            }
            applyTool(PaintOpenGLWidget::Tool::Fill, false);
            return;
        }

        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setPenColor(color);
            view->setStrokeProperty(QString());
        }
        applyTool(PaintOpenGLWidget::Tool::Pen, false);
    });

    connect(toolOptPanel, &ToolOptPanel::fillScopeSelected, this, [this](PaintOpenGLWidget::FillScope scope) {
        m_toolFillAllLayers = scope == PaintOpenGLWidget::FillScope::AllLayers;
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setFillScope(scope);
        }
    });

    connect(toolOptPanel, &ToolOptPanel::eraserModeSelected, this, [applyTool](PaintOpenGLWidget::Tool tool) {
        applyTool(tool, false);
    });

    connect(toolOptPanel, &ToolOptPanel::smoothValueChanged, this, [this](int value) {
        m_toolSmoothValue = value;
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setSmoothValue(value);
        }
    });

    connect(toolOptPanel, &ToolOptPanel::penWidthChanged, this, [this](int value) {
        m_toolPenWidth = value;
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->setPenWidth(value);
        }
    });

    connect(toolOptPanel, &ToolOptPanel::optionChanged, this, [this](const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn) {
        activePaintWidget()->sendPythonToolOptionMessage(hook, name, type, value, row, startColumn, endColumn);
    });
}

void MainWindow::createListDocks()
{
    m_layerPanel = new LayerPanel(this);
    m_framePanel = new FramePanel(this);
    m_assetPanel = new AssetPanel(this);

    m_layerDock = new QDockWidget(QStringLiteral("Layers"), this);
    m_layerDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    m_frameDock = new QDockWidget(QStringLiteral("Frames"), this);
    m_frameDock->setWidget(m_framePanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_frameDock);

    m_assetDock = new QDockWidget(QStringLiteral("Assets"), this);
    m_assetDock->setWidget(m_assetPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_assetDock);
}

void MainWindow::openProject()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Project"),
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
        QStringLiteral("Save Project As"),
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
                             QStringLiteral("Save Project"),
                             QStringLiteral("Failed to write file:\n%1").arg(file.errorString()));
        return false;
    }

    const QJsonDocument document(modelToJson(m_paintWidget->model()));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this,
                             QStringLiteral("Save Project"),
                             QStringLiteral("Failed to save file:\n%1").arg(file.errorString()));
        return false;
    }

    m_currentFilePath = fileName;
    updateWindowTitle();
    setStatusText(QStringLiteral("Saved: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

bool MainWindow::loadProjectFrom(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Project"),
                             QStringLiteral("Failed to read file:\n%1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Project"),
                             QStringLiteral("Project file format error:\n%1").arg(parseError.errorString()));
        return false;
    }

    AnimeSceneModel loadedModel;
    QString error;
    if (!modelFromJson(document.object(), &loadedModel, &error)) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Project"),
                             error.isEmpty() ? QStringLiteral("Unsupported project file.") : error);
        return false;
    }

    m_paintWidget->model() = loadedModel;
    if (m_paintWidget->model().textId().isEmpty()) {
        m_paintWidget->model().setTextId(QStringLiteral("main_paint_view"));
    }
    m_currentFilePath = fileName;
    updateWindowTitle();
    updateAttention(m_paintWidget,
                    AttentionChange::FrameChange,
                    m_paintWidget->model().currentFrame(),
                    m_paintWidget->model().currentLayer(),
                    m_paintWidget->model().currentAsset());
    m_paintWidget->update();
    setStatusText(QStringLiteral("Opened: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

void MainWindow::importRaster(PaintOpenGLWidget *view)
{
    if (!view) {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Raster into %1 View").arg(view->viewName()),
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
    const int layerIndex = view->importRasterLayer(image, fileInfo.completeBaseName());
    if (layerIndex < 0) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Raster"),
                             QStringLiteral("Failed to import raster layer."));
        return;
    }

    updateAttention(view,
                    AttentionChange::LayerChange,
                    view->model().currentFrame(),
                    layerIndex,
                    view->model().currentAsset());
    setStatusText(QStringLiteral("Imported raster into %1: %2 (%3 x %4)")
                      .arg(view->viewName())
                      .arg(fileInfo.fileName())
                      .arg(image.width())
                      .arg(image.height()));
}

void MainWindow::importOpenToonzLines(PaintOpenGLWidget *view)
{
#ifndef ANIMEAN_WITH_PYTHON
    Q_UNUSED(view);
    QMessageBox::warning(this,
                         QStringLiteral("Import OpenToonz Lines"),
                         QStringLiteral("Python support is disabled, so OpenToonz line import is unavailable."));
    return;
#else
    if (!view) {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import OpenToonz Lines into %1 View").arg(view->viewName()),
        QString(),
        QStringLiteral("OpenToonz Vector Levels (*.pli);;All Files (*)"));
    if (fileName.isEmpty()) {
        return;
    }

    try {
        py::gil_scoped_acquire acquire;
        py::module_ toonzTool = py::module_::import("toonz_to_dict");
        py::object level = toonzTool.attr("read_vector_level")(QDir::toNativeSeparators(fileName).toStdString());
        py::dict levelData = level.attr("to_dict")().cast<py::dict>();
        QVector<PaintOpenGLWidget::ImportedVectorFrame> frames = importedFramesFromOpenToonzLevel(levelData);
        if (frames.isEmpty()) {
            QMessageBox::warning(this,
                                 QStringLiteral("Import OpenToonz Lines"),
                                 QStringLiteral("No strokes were found in this .pli file."));
            return;
        }

        const QRectF sourceBounds = importedVectorFramesBounds(frames);
        const QSizeF canvasSize(view->width(), view->height());
        const QRectF canvasBounds(QPointF(0.0, 0.0), canvasSize);
        const bool needsScale = sourceBounds.isValid()
                                && sourceBounds.width() > 0.0
                                && sourceBounds.height() > 0.0
                                && (!canvasBounds.contains(sourceBounds)
                                    || !qFuzzyCompare(sourceBounds.width(), canvasSize.width())
                                    || !qFuzzyCompare(sourceBounds.height(), canvasSize.height()));
        if (needsScale) {
            const QMessageBox::StandardButton button = QMessageBox::question(
                this,
                QStringLiteral("Import OpenToonz Lines"),
                QStringLiteral("The imported drawing bounds are (%1, %2) %3 x %4, but the current canvas is %5 x %6.\n"
                               "Fit it to the current canvas?")
                    .arg(qRound(sourceBounds.x()))
                    .arg(qRound(sourceBounds.y()))
                    .arg(qRound(sourceBounds.width()))
                    .arg(qRound(sourceBounds.height()))
                    .arg(view->width())
                    .arg(view->height()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (button == QMessageBox::Yes) {
                scaleImportedVectorFramesToCanvas(&frames, canvasSize);
            }
        }

        const QFileInfo fileInfo(fileName);
        const int layerIndex = view->importVectorLineLayer(frames, fileInfo.completeBaseName());
        if (layerIndex < 0) {
            QMessageBox::warning(this,
                                 QStringLiteral("Import OpenToonz Lines"),
                                 QStringLiteral("Failed to import OpenToonz line layer."));
            return;
        }

        updateAttention(view,
                        AttentionChange::LayerChange,
                        view->model().currentFrame(),
                        layerIndex,
                        view->model().currentAsset());
        setStatusText(QStringLiteral("Imported OpenToonz lines into %1: %2 (%3 frame(s))")
                          .arg(view->viewName())
                          .arg(fileInfo.fileName())
                          .arg(frames.size()));
    } catch (const py::error_already_set &error) {
        QMessageBox::warning(this,
                             QStringLiteral("Import OpenToonz Lines"),
                             QStringLiteral("OpenToonz import failed:\n%1").arg(QString::fromUtf8(error.what())));
    }
#endif
}

void MainWindow::importClipStudioPaint(PaintOpenGLWidget *view)
{
    if (!view) {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Clip Studio Paint into %1 View").arg(view->viewName()),
        QString(),
        QStringLiteral("Clip Studio Paint (*.clip);;All Files (*)"));
    if (fileName.isEmpty()) {
        return;
    }

    const ClipReader::ClipDocument document = ClipReader::readClipFile(fileName);
    if (!document.valid) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Clip Studio Paint"),
                             document.error.isEmpty()
                                 ? QStringLiteral("Unable to read this .clip file.")
                                 : document.error);
        return;
    }

    QVector<PaintOpenGLWidget::ImportedVectorFrame> frames;
    PaintOpenGLWidget::ImportedVectorFrame frame;
    frame.row = 0;
    for (const ClipReader::ClipStroke &clipStroke : document.strokes) {
        if (clipStroke.points.size() < 2) {
            continue;
        }
        PaintOpenGLWidget::ImportedVectorStroke stroke;
        stroke.points = clipStroke.points;
        stroke.color = clipStroke.color;
        stroke.width = std::max(qreal(1.0), clipStroke.width);
        stroke.path.moveTo(clipStroke.points.first());
        for (int i = 1; i < clipStroke.points.size(); ++i) {
            stroke.path.lineTo(clipStroke.points[i]);
        }
        frame.strokes.append(stroke);
    }
    if (frame.strokes.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Clip Studio Paint"),
                             QStringLiteral("No usable vector strokes were found in this .clip file."));
        return;
    }
    frames.append(frame);

    const QRectF sourceBounds = importedVectorFramesBounds(frames);
    const QSizeF canvasSize(view->width(), view->height());
    const QRectF canvasBounds(QPointF(0.0, 0.0), canvasSize);
    const bool needsScale = sourceBounds.isValid()
                            && sourceBounds.width() > 0.0
                            && sourceBounds.height() > 0.0
                            && (!canvasBounds.contains(sourceBounds)
                                || !qFuzzyCompare(sourceBounds.width(), canvasSize.width())
                                || !qFuzzyCompare(sourceBounds.height(), canvasSize.height()));
    if (needsScale) {
        const QMessageBox::StandardButton button = QMessageBox::question(
            this,
            QStringLiteral("Import Clip Studio Paint"),
            QStringLiteral("The imported drawing bounds are (%1, %2) %3 x %4, but the current canvas is %5 x %6.\n"
                           "Fit it to the current canvas?")
                .arg(qRound(sourceBounds.x()))
                .arg(qRound(sourceBounds.y()))
                .arg(qRound(sourceBounds.width()))
                .arg(qRound(sourceBounds.height()))
                .arg(view->width())
                .arg(view->height()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (button == QMessageBox::Yes) {
            scaleImportedVectorFramesToCanvas(&frames, canvasSize);
        }
    }

    const QFileInfo fileInfo(fileName);
    const int layerIndex = view->importVectorLineLayer(frames, fileInfo.completeBaseName());
    if (layerIndex < 0) {
        QMessageBox::warning(this,
                             QStringLiteral("Import Clip Studio Paint"),
                             QStringLiteral("Failed to import the Clip Studio Paint vector layer."));
        return;
    }

    int strokeCount = 0;
    for (const PaintOpenGLWidget::ImportedVectorFrame &imported : frames) {
        strokeCount += imported.strokes.size();
    }
    updateAttention(view,
                    AttentionChange::LayerChange,
                    view->model().currentFrame(),
                    layerIndex,
                    view->model().currentAsset());
    setStatusText(QStringLiteral("Imported Clip Studio Paint into %1: %2 (%3 stroke(s))")
                      .arg(view->viewName())
                      .arg(fileInfo.fileName())
                      .arg(strokeCount));
}

void MainWindow::updateWindowTitle()
{
    const QString fileName = m_currentFilePath.isEmpty()
                                 ? QStringLiteral("Untitled")
                                 : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(QStringLiteral("AnimeAn - %1").arg(fileName));
}

void MainWindow::setStatusText(const QString &text)
{
    statusBar()->showMessage(text);
}

void MainWindow::setPythonUiFrozen(bool frozen)
{
    if (frozen) {
        if (m_pythonFreezeDepth++ > 0) {
            return;
        }
    } else {
        if (m_pythonFreezeDepth <= 0) {
            return;
        }
        --m_pythonFreezeDepth;
        if (m_pythonFreezeDepth > 0) {
            return;
        }
    }

    const bool enabled = !frozen;
    menuBar()->setEnabled(enabled);
    for (PaintOpenGLWidget *view : m_paintViews) {
        if (view) {
            view->setEnabled(enabled);
        }
    }
    const QList<QDockWidget *> docks = findChildren<QDockWidget *>();
    for (QDockWidget *dock : docks) {
        if (dock && dock != m_pythonDebugDock) {
            dock->setEnabled(enabled);
        }
    }
    if (m_pythonDebugDock) {
        m_pythonDebugDock->setEnabled(true);
    }
    if (m_pythonDebugOutput) {
        m_pythonDebugOutput->setEnabled(true);
    }
    if (m_pythonDebugCommand) {
        m_pythonDebugCommand->setEnabled(true);
    }

    if (frozen) {
        setStatusText(QStringLiteral("Python busy..."));
    } else {
        setStatusText(QStringLiteral("Python ready"));
    }

    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::requestAttentionUpdate(PaintOpenGLWidget *view, AttentionChange change, int frame, int layer, int asset)
{
    if (!m_listMousePressed) {
        updateAttention(view, change, frame, layer, asset);
        return;
    }

    m_pendingAttentionChange = change;
    m_pendingAttention.frame = frame;
    m_pendingAttention.layer = layer;
    m_pendingAttention.asset = asset;
    m_pendingAttentionView = view;
    m_hasPendingAttention = true;
}

void MainWindow::updateAttention(PaintOpenGLWidget *view, AttentionChange change, int frame, int layer, int asset)
{
    if (!view) {
        return;
    }

    SelectionAttention &attention = attentionFor(view);
    const SelectionAttention previous = attention;
    attention.frame = frame;
    attention.layer = layer;
    attention.asset = asset;
    AttentionUpdate update = constrainAttention(view->model(), &attention, change);
    update.frame = update.frame || previous.frame != attention.frame;
    update.layer = update.layer || previous.layer != attention.layer;
    update.asset = update.asset || previous.asset != attention.asset;

    view->setCurrentFrame(attention.frame);
    view->setCurrentLayer(attention.layer);
    view->setCurrentAsset(attention.asset);

    if (update.frame && view == framePanelTarget()) {
        refreshFrameList(attention.frame);
    }
    if (update.layer && view == layerPanelTarget()) {
        refreshLayerList(attention.layer);
    }
    if (update.asset && view == assetPanelTarget()) {
        refreshAssetList(attention.asset);
    }
    syncEmbeddedPythonState();
}

void MainWindow::refreshLayerList(int selectedRow)
{
    PaintOpenGLWidget *view = layerPanelTarget();
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_layerPanel->layerList());
    m_layerPanel->layerList()->clear();
    int selectedListRow = -1;
    for (int i = 0; i < view->layerCount(); ++i) {
        if (view->model().assetIndexAt(view->model().currentFrame(), i) < 0) {
            continue;
        }
        QListWidgetItem *item = new QListWidgetItem(view->layerName(i));
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
    PaintOpenGLWidget *view = framePanelTarget();
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_framePanel->frameList());
    m_framePanel->frameList()->clear();
    for (int i = 0; i < view->frameCount(); ++i) {
        m_framePanel->frameList()->addItem(view->frameName(i));
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
    PaintOpenGLWidget *view = assetPanelTarget();
    m_refreshingLists = true;
    const QSignalBlocker blocker(m_assetPanel->assetList());
    m_assetPanel->assetList()->clear();
    for (int i = 0; i < view->assetCount(); ++i) {
        m_assetPanel->assetList()->addItem(view->assetName(i));
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


