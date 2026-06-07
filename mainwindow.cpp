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
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDropEvent>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QImageReader>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
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
