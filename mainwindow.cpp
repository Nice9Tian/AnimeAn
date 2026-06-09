#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "childrenpanel/assetpanel.h"
#include "childrenpanel/framepanel.h"
#include "childrenpanel/layerpanel.h"
#include "openglwidget.h"
#include "projectio.h"
#include "selectionattention.h"
#include "childrenpanel/tooloptpanel.h"
#include "childrenpanel/toolspanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
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
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
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
    }
    return QStringLiteral("pen");
}

QJsonArray fallbackToolControls(PaintOpenGLWidget::Tool tool, int smoothValue, int penWidth, PaintOpenGLWidget::FillScope fillScope)
{
    const QString fillScopeValue = fillScope == PaintOpenGLWidget::FillScope::AllLayers
                                       ? QStringLiteral("all")
                                       : QStringLiteral("current");
    const QString eraserModeValue = tool == PaintOpenGLWidget::Tool::DeleteLine
                                        ? QStringLiteral("line")
                                        : QStringLiteral("area");
    const QByteArray json = QStringLiteral(R"([
        {
            "name": "color",
            "type": "button_row",
            "title": "Color",
            "hook": "color",
            "options": [
                {"title": "Black", "value": "black", "state": {"color": "black"}},
                {"title": "Blue", "value": "blue", "state": {"color": "blue"}},
                {"title": "Green", "value": "green", "state": {"color": "green"}}
            ]
        },
        {
            "name": "smooth",
            "type": "slider",
            "title": "Smooth",
            "hook": "smooth",
            "min": 0,
            "max": 100,
            "value": %1
        },
        {
            "name": "pen_width",
            "type": "slider",
            "title": "Width",
            "hook": "pen_width",
            "min": 1,
            "max": 50,
            "value": %2
        }
    ])").arg(smoothValue).arg(penWidth).toUtf8();
    const QByteArray fillJson = QStringLiteral(R"([
        {
            "name": "color",
            "type": "button_row",
            "title": "Color",
            "hook": "color",
            "options": [
                {"title": "Black", "value": "black", "state": {"color": "black"}},
                {"title": "Blue", "value": "blue", "state": {"color": "blue"}},
                {"title": "Green", "value": "green", "state": {"color": "green"}}
            ]
        },
        {
            "name": "fill_scope",
            "type": "list",
            "title": "Fill Scope",
            "hook": "fill_scope",
            "value": "%1",
            "height": 62,
            "options": [
                {"title": "ALL", "value": "all"},
                {"title": "Current", "value": "current"}
            ]
        }
    ])").arg(fillScopeValue).toUtf8();
    const QByteArray eraserJson = QStringLiteral(R"([
        {
            "name": "eraser_mode",
            "type": "list",
            "title": "Eraser Mode",
            "hook": "eraser_mode",
            "value": "%1",
            "height": 62,
            "options": [
                {"title": "LineMode", "value": "line"},
                {"title": "AreaMode", "value": "area"}
            ]
        }
    ])").arg(eraserModeValue).toUtf8();

    if (tool == PaintOpenGLWidget::Tool::Fill) {
        return QJsonDocument::fromJson(fillJson).array();
    }
    if (tool == PaintOpenGLWidget::Tool::Eraser || tool == PaintOpenGLWidget::Tool::DeleteLine) {
        return QJsonDocument::fromJson(eraserJson).array();
    }
    return QJsonDocument::fromJson(json).array();
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

    m_toolsDock = new QDockWidget(QStringLiteral("Tools"), this);
    m_toolsDock->setWidget(toolsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);

    m_toolOptDock = new QDockWidget(QStringLiteral("Tool Options"), this);
    m_toolOptDock->setWidget(toolOptPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_toolOptDock);
    splitDockWidget(m_toolOptDock, m_layerDock, Qt::Vertical);
    splitDockWidget(m_layerDock, m_assetDock, Qt::Vertical);

    auto loadToolOptions = [this, toolOptPanel](PaintOpenGLWidget::Tool tool) {
        QJsonArray controls;
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
            if (parseError.error == QJsonParseError::NoError && document.isArray()) {
                controls = document.array();
            } else {
                ui->label->setText(QStringLiteral("toolcontrol JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            ui->label->setText(QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
#endif
        if (controls.isEmpty()) {
            controls = fallbackToolControls(tool,
                                            m_toolSmoothValue,
                                            m_toolPenWidth,
                                            m_toolFillAllLayers ? PaintOpenGLWidget::FillScope::AllLayers
                                                                : PaintOpenGLWidget::FillScope::CurrentLayer);
        }
        toolOptPanel->configureControls(controls);
    };

    auto applyTool = [this, toolsPanel, toolOptPanel, loadToolOptions](PaintOpenGLWidget::Tool tool, bool reloadOptions) {
        m_paintWidget->setTool(tool);
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

    connect(toolOptPanel, &ToolOptPanel::colorSelected, this, [this, applyTool](const QColor &color) {
        if (m_paintWidget->tool() == PaintOpenGLWidget::Tool::Fill) {
            m_paintWidget->setDrawingColor(color);
            applyTool(PaintOpenGLWidget::Tool::Fill, false);
            return;
        }

        m_paintWidget->setPenColor(color);
        applyTool(PaintOpenGLWidget::Tool::Pen, false);
    });

    connect(toolOptPanel, &ToolOptPanel::fillScopeSelected, this, [this](PaintOpenGLWidget::FillScope scope) {
        m_toolFillAllLayers = scope == PaintOpenGLWidget::FillScope::AllLayers;
        m_paintWidget->setFillScope(scope);
    });

    connect(toolOptPanel, &ToolOptPanel::eraserModeSelected, this, [applyTool](PaintOpenGLWidget::Tool tool) {
        applyTool(tool, false);
    });

    connect(toolOptPanel, &ToolOptPanel::smoothValueChanged, this, [this](int value) {
        m_toolSmoothValue = value;
        m_paintWidget->setSmoothValue(value);
    });

    connect(toolOptPanel, &ToolOptPanel::penWidthChanged, this, [this](int value) {
        m_toolPenWidth = value;
        m_paintWidget->setPenWidth(value);
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
    ui->label->setText(QStringLiteral("Saved: %1").arg(QFileInfo(fileName).fileName()));
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
    m_currentFilePath = fileName;
    updateWindowTitle();
    updateAttention(AttentionChange::FrameChange,
                    m_paintWidget->model().currentFrame(),
                    m_paintWidget->model().currentLayer(),
                    m_paintWidget->model().currentAsset());
    m_paintWidget->update();
    ui->label->setText(QStringLiteral("Opened: %1").arg(QFileInfo(fileName).fileName()));
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

void MainWindow::setStatusText(const QString &text)
{
    ui->label->setText(text);
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
    AttentionUpdate update = constrainAttention(m_paintWidget->model(), &m_attention, change);
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


