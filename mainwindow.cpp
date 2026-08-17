#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "childrenpanel/assetpanel.h"
#include "childrenpanel/childpaintwindow.h"
#include "childrenpanel/framepanel.h"
#include "childrenpanel/forcepad.h"
#include "childrenpanel/historypanel.h"
#include "childrenpanel/layerpanel.h"
#include "childrenpanel/newprojectdialog.h"

#include <QComboBox>
#include "clipreader.h"
#include "openglwidget.h"
#include "paintviewcontainer.h"
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
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <functional>
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
// Timeline playback rate; the prerendered frames are blitted at this cadence.
constexpr int kPlaybackFps = 12;

// Layer panel item roles. Qt::UserRole stays the layer index (it is what the
// rest of this file already reads); groups carry -1 there and their group id
// here, so "is this row a group" is one lookup.
constexpr int kGroupIdRole = Qt::UserRole + 1;

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
    case PaintOpenGLWidget::Tool::CutLine:
        return QStringLiteral("cut_line");
    case PaintOpenGLWidget::Tool::Fill:
        return QStringLiteral("fill");
    case PaintOpenGLWidget::Tool::Move:
        return QStringLiteral("move");
    case PaintOpenGLWidget::Tool::Arrow:
        return QStringLiteral("arrow");
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
    // Queued: the dialog is modal, so running it from the constructor would
    // block before the window is laid out or shown - the user would meet a
    // floating dialog with no app behind it, and it could not centre itself.
    QMetaObject::invokeMethod(this, [this]() {
        promptForNewCanvasOnStartup();
    }, Qt::QueuedConnection);
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
                // modelReplaced, not update: a script can change the page
                // (set_canvas_size) as easily as it can change a stroke, and
                // a bare repaint would leave the pan and the scroll bars on
                // the old one - with the new area unreachable by scrolling.
                paintView->modelReplaced();
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
            overlayItem.penStyle = item.penStyle;
            overlayItem.removable = item.removable;
            converted.append(overlayItem);
        }
        target->setOverlayItems(converted);
    });
    registerAnimeanUiEditHandleCallback([this](const QString &view, const QVector<AnimeanEditHandle> &handles) {
        PaintOpenGLWidget *target = m_paintWidget;
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            if (paintView->viewName() == view) {
                target = paintView;
                break;
            }
        }
        QVector<PaintOpenGLWidget::EditHandle> converted;
        converted.reserve(handles.size());
        for (const AnimeanEditHandle &handle : handles) {
            PaintOpenGLWidget::EditHandle editHandle;
            editHandle.id = handle.id;
            editHandle.pos = handle.pos;
            editHandle.shape = handle.shape;
            editHandle.color = handle.color;
            converted.append(editHandle);
        }
        target->setEditHandles(converted);
    });
    registerAnimeanUiDrawColorCallback([this](const QColor &color) {
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            paintView->setDrawingColor(color);
        }
    });
    registerAnimeanUiDrawSettingsCallback([this](int stabilizer, int simplify, int corner) {
        m_toolSmoothValue = stabilizer;
        AnimeStrokeFitSettings settings;
        settings.simplify = simplify;
        settings.corner = corner;
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            paintView->setSmoothValue(stabilizer);
            paintView->setStrokeFitSettings(settings);
        }
        // The tool panel shows the stabilizer as its "Smooth" slider, so a
        // change made in the Draw Setting window has to be reflected there.
        refreshToolOptions();
    });
    registerAnimeanUiHistoryCallback([this](const QString &op, const QString &viewName, const QString &label) {
        PaintOpenGLWidget *named = nullptr;
        for (PaintOpenGLWidget *paintView : m_paintViews) {
            if (paintView->viewName() == viewName) {
                named = paintView;
                break;
            }
        }
        if (!viewName.isEmpty() && !named) {
            // Unknown view name: doing nothing beats acting on a guess.
            appendPythonDebugMessage(QStringLiteral("[history] unknown view '%1'").arg(viewName));
            return;
        }
        if (op == QStringLiteral("commit")) {
            PaintOpenGLWidget *view = named ? named : activePaintWidget();
            view->commitHistory(label.isEmpty() ? QStringLiteral("Python") : label);
        } else if (op == QStringLiteral("undo")) {
            PaintOpenGLWidget *view = named ? named : undoTargetView();
            if (view && view->undoHistory()) {
                applyHistoryRestore(view);
            }
        } else if (op == QStringLiteral("redo")) {
            PaintOpenGLWidget *view = named ? named : redoTargetView();
            if (view && view->redoHistory()) {
                applyHistoryRestore(view);
            }
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
    clearAnimeanUiHistoryCallback();
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

    // Three-section workspace: child view over Tools on the left, panels on
    // the right, the main view anchored as the central widget (only a central
    // widget absorbs window growth correctly), and the timeline (left+middle)
    // + python debug (right) along the bottom.
    createMainPaintView();
    createChildPaintDock();
    m_activePaintWidget = m_paintWidget;
    m_paintViews = {m_paintWidget, m_childPaintWidget};
    m_paintWidget->setActiveIndicator(true);
    createListDocks();
    createToolDocks();
    // After createToolDocks: importing extra_tools there pulls in the tool
    // modules, whose import-time registrations fill the view-button registry.
    populateChildViewButtons();
    setupPythonDebugDock();
    createTextureFileMenu();
    // After the View menu below would be too late to matter, but after the
    // tool modules are imported is what counts: their import-time
    // registrations are what these menus are built from.
    createScriptMenus();
    // Re-baseline both histories now that the views carry their fixed scene
    // identities; the constructor-time baseline predates setTextId/setIntId
    // and undoing into it would corrupt the main/child identity invariant.
    m_paintWidget->resetHistory(QStringLiteral("Initial"));
    m_childPaintWidget->resetHistory(QStringLiteral("Initial"));
    createHistoryDock();
    createForcePadDock();

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(m_childPaintWindow->toggleViewAction());
    viewMenu->addSeparator();
    for (QDockWidget *dock : {m_toolsDock, m_toolOptDock, m_layerDock, m_assetDock,
                              m_frameDock, m_historyDock, m_forcePadDock, m_pythonDebugDock}) {
        if (dock) {
            viewMenu->addAction(dock->toggleViewAction());
        }
    }

    // NOTE: deliberately NO horizontal resizeDocks on the bottom band —
    // measured against Qt 6.9.1 it pins the band at its minimum height for
    // good and stops the right column from recovering after a shrink. The
    // only sizing nudge is vertical, inside the left column: the child view
    // gets the larger share above the Tools dock.
    resizeDocks({m_childPaintWindow}, {380}, Qt::Vertical);
}

void MainWindow::createMainPaintView()
{
    PaintViewContainer *container = new PaintViewContainer(this);
    container->setMinimumSize(320, 240);
    if (QWidget *central = takeCentralWidget()) {
        central->deleteLater();
    }
    setCentralWidget(container);

    m_paintWidget = container->paintWidget();
    m_paintWidget->setViewName(QStringLiteral("main"));
    m_paintWidget->model().setTextId(QStringLiteral("main_paint_view"));
    m_paintWidget->model().setIntId(1);
}

void MainWindow::showMainPaintView()
{
    // The main view is the central widget: always visible, just activate it.
    setActivePaintView(m_paintWidget);
}

void MainWindow::startPlayback()
{
    if (m_playbackTimer->isActive()) {
        return;
    }

    PaintOpenGLWidget *view = framePanelTarget();
    const int frameCount = view->frameCount();
    if (frameCount < 2) {
        setStatusText(QStringLiteral("Playback needs at least two frames."));
        return;
    }

    // Prerender every frame to pixels first: playback then only blits images,
    // so the frame rate does not depend on how heavy the vector art is.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setStatusText(QStringLiteral("Prerendering %1 frames...").arg(frameCount));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QString error;
    const bool prerendered = view->buildPlaybackCache(frameCount, &error);
    QApplication::restoreOverrideCursor();
    if (!prerendered) {
        setStatusText(QStringLiteral("Playback unavailable: %1").arg(error));
        return;
    }

    m_playbackView = view;
    m_playbackFrameCount = frameCount;
    m_playbackIndex = std::min(std::max(attentionFor(view).frame, 0), frameCount - 1);
    view->showPlaybackFrame(m_playbackIndex);
    m_framePanel->playButton()->setEnabled(false);
    m_framePanel->pauseButton()->setEnabled(true);
    // The rate comes from the document being played, not from a constant.
    m_playbackTimer->setInterval(1000 / view->model().playbackFps());
    m_playbackTimer->start();
    setStatusText(QStringLiteral("Playing %1 frames of %2 at %3 fps (prerendered)")
                      .arg(frameCount)
                      .arg(view->viewName())
                      .arg(1000 / m_playbackTimer->interval()));
}

void MainWindow::advancePlaybackFrame()
{
    if (!m_playbackView || m_playbackFrameCount <= 0) {
        return;
    }

    m_playbackIndex = (m_playbackIndex + 1) % m_playbackFrameCount;
    m_playbackView->showPlaybackFrame(m_playbackIndex);

    // Move the timeline highlight only: the model stays untouched while the
    // prerendered frames are on screen.
    if (m_playbackView == framePanelTarget()) {
        m_refreshingLists = true;
        const QSignalBlocker blocker(m_framePanel->frameList());
        m_framePanel->frameList()->setCurrentRow(m_playbackIndex);
        m_refreshingLists = false;
    }
}

void MainWindow::stopPlayback()
{
    if (!m_playbackView) {
        return;
    }

    m_playbackTimer->stop();
    PaintOpenGLWidget *view = m_playbackView;
    const int pausedFrame = m_playbackIndex;
    m_playbackView = nullptr;
    m_playbackFrameCount = 0;
    m_framePanel->playButton()->setEnabled(true);
    m_framePanel->pauseButton()->setEnabled(false);

    view->endPlayback();
    // Land the editable state on the frame the user paused at, back in vector.
    updateAttention(view,
                    AttentionChange::FrameChange,
                    pausedFrame,
                    attentionFor(view).layer,
                    attentionFor(view).asset);
    setStatusText(QStringLiteral("Paused at frame %1 (vector view)").arg(pausedFrame + 1));
}

void MainWindow::createForcePadDock()
{
    m_forcePadPanel = new ForcePadPanel(this);
    m_forcePadDock = new QDockWidget(QStringLiteral("Repulsion Pad"), this);
    m_forcePadDock->setObjectName(QStringLiteral("ForcePadDock"));
    m_forcePadDock->setWidget(m_forcePadPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_forcePadDock);
    // Hidden by default: it is a special-purpose tool surface; the View menu
    // toggle brings it up.
    m_forcePadDock->hide();

    // The pad itself is a generic vector input. Routing its phases into the
    // Python hook system ("pad" event, pad name "force_pad") is the only tool
    // coupling; the repulsion semantics live in pyfile/repulsion_tool.py.
    connect(m_forcePadPanel, &ForcePadPanel::padPressed, this, [this](double x, double y) {
        activePaintWidget()->sendPythonPadMessage(QStringLiteral("force_pad"), QStringLiteral("press"), x, y);
    });
    connect(m_forcePadPanel, &ForcePadPanel::padMoved, this, [this](double x, double y) {
        activePaintWidget()->sendPythonPadMessage(QStringLiteral("force_pad"), QStringLiteral("move"), x, y);
    });
    connect(m_forcePadPanel, &ForcePadPanel::padReleased, this, [this](double x, double y) {
        activePaintWidget()->sendPythonPadMessage(QStringLiteral("force_pad"), QStringLiteral("release"), x, y);
    });

#ifdef ANIMEAN_WITH_PYTHON
    // Lets Python recenter the latched handle when its baseline dies
    // (ui.set_pad_value("force_pad", 0, 0)).
    registerAnimeanUiPadValueCallback([this](const QString &pad, double x, double y) {
        if (pad == QStringLiteral("force_pad") && m_forcePadPanel) {
            m_forcePadPanel->setValue(QPointF(x, y));
        }
    });
#endif
}

void MainWindow::createHistoryDock()
{
    m_historyPanel = new HistoryPanel(this);
    m_historyDock = new QDockWidget(QStringLiteral("History"), this);
    m_historyDock->setObjectName(QStringLiteral("HistoryDock"));
    m_historyDock->setWidget(m_historyPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);

    m_undoAction = new QAction(QStringLiteral("Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        PaintOpenGLWidget *view = undoTargetView();
        if (!view) {
            return;
        }
        const QString undoneLabel = view->history().labelAt(view->history().currentIndex());
        if (view->undoHistory()) {
            applyHistoryRestore(view);
            setStatusText(QStringLiteral("Undo in %1: %2").arg(view->viewName(), undoneLabel));
        }
    });
    ui->menuEdit->addAction(m_undoAction);

    m_redoAction = new QAction(QStringLiteral("Redo"), this);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        PaintOpenGLWidget *view = redoTargetView();
        if (view && view->redoHistory()) {
            applyHistoryRestore(view);
            setStatusText(QStringLiteral("Redo in %1: %2")
                              .arg(view->viewName(),
                                   view->history().labelAt(view->history().currentIndex())));
        }
    });
    ui->menuEdit->addAction(m_redoAction);

    for (PaintOpenGLWidget *view : m_paintViews) {
        connect(view, &PaintOpenGLWidget::historyChanged,
                this, &MainWindow::scheduleHistoryRefresh);
        // Global-inverse redo semantics: a new edit anywhere invalidates the
        // redo future everywhere, so Ctrl+Y can never resurrect an operation
        // whose chronological context is gone.
        connect(view, &PaintOpenGLWidget::historyCommitted, this, [this, view]() {
            for (PaintOpenGLWidget *other : m_paintViews) {
                if (other != view) {
                    other->dropRedoTail();
                }
            }
        });
    }

    connect(m_historyPanel->historyList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_refreshingHistory || row < 0) {
            return;
        }
        PaintOpenGLWidget *view = activePaintWidget();
        if (view->goToHistory(row)) {
            applyHistoryRestore(view);
        } else {
            scheduleHistoryRefresh();
        }
    });

    refreshHistoryList();
}

// Undo follows true chronology across both views: pick the view whose current
// entry is the most recent commit anywhere.
PaintOpenGLWidget *MainWindow::undoTargetView() const
{
    PaintOpenGLWidget *target = nullptr;
    quint64 best = 0;
    for (PaintOpenGLWidget *view : m_paintViews) {
        const SceneHistory &history = view->history();
        if (!history.canUndo()) {
            continue;
        }
        if (!target || history.currentSeq() > best) {
            target = view;
            best = history.currentSeq();
        }
    }
    return target;
}

// Redo is the mirror: re-apply the oldest not-yet-redone commit first.
PaintOpenGLWidget *MainWindow::redoTargetView() const
{
    PaintOpenGLWidget *target = nullptr;
    quint64 best = 0;
    for (PaintOpenGLWidget *view : m_paintViews) {
        const SceneHistory &history = view->history();
        if (!history.canRedo()) {
            continue;
        }
        if (!target || history.redoSeq() < best) {
            target = view;
            best = history.redoSeq();
        }
    }
    return target;
}

void MainWindow::scheduleHistoryRefresh()
{
    // Rebuilding the list synchronously from inside its own signal handlers
    // (or mid mouse-press) shifts the viewport under the click; coalesce and
    // defer to the event loop instead.
    if (m_historyRefreshQueued) {
        return;
    }
    m_historyRefreshQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_historyRefreshQueued = false;
        refreshHistoryList();
    }, Qt::QueuedConnection);
}

void MainWindow::refreshHistoryList()
{
    if (!m_historyPanel) {
        return;
    }

    PaintOpenGLWidget *view = activePaintWidget();
    const SceneHistory &history = view->history();
    QListWidget *list = m_historyPanel->historyList();

    m_refreshingHistory = true;
    const QSignalBlocker blocker(list);
    list->clear();
    for (int i = 0; i < history.count(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1. %2").arg(i + 1).arg(history.labelAt(i)));
        if (i > history.currentIndex()) {
            item->setForeground(Qt::gray);
        }
        list->addItem(item);
    }
    list->setCurrentRow(history.currentIndex());
    list->scrollToItem(list->currentItem());
    m_refreshingHistory = false;

    if (m_historyDock) {
        m_historyDock->setWindowTitle(QStringLiteral("History - %1").arg(view->viewName()));
    }
    if (m_undoAction) {
        m_undoAction->setEnabled(undoTargetView() != nullptr);
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(redoTargetView() != nullptr);
    }
}

void MainWindow::applyHistoryRestore(PaintOpenGLWidget *view)
{
    if (!view) {
        return;
    }

    stopPlayback();  // restored content invalidates the prerendered frames

    // Old snapshots may predate the identity assignment; the view's scene
    // identity is an invariant, never part of the undone state.
    if (view == m_childPaintWidget) {
        view->model().setTextId(QStringLiteral("child_paint_view"));
        view->model().setIntId(2);
    } else {
        view->model().setTextId(QStringLiteral("main_paint_view"));
        view->model().setIntId(1);
    }
    // The snapshot carries the page size too, so undoing across a canvas
    // change has to move the view with it.
    view->modelReplaced();

    // Chronological undo/redo may land on the other — possibly hidden — view;
    // surface and activate it so the user sees what just changed instead of
    // an apparently dead Undo key.
    if (view == m_childPaintWidget && m_childPaintWindow) {
        m_childPaintWindow->show();
        m_childPaintWindow->raise();
    }
    setActivePaintView(view);

    // LayerChange keeps the snapshot's layer/asset selection (merely clamped);
    // FrameChange would discard it and jump to the frame's top layer.
    updateAttention(view,
                    AttentionChange::LayerChange,
                    view->model().currentFrame(),
                    view->model().currentLayer(),
                    view->model().currentAsset());
    // updateAttention always rebuilds the layer/asset lists, but skips the
    // frame list when the frame INDEX is unchanged — even though a restore may
    // have changed the frame COUNT. Rebuild just that one unconditionally.
    refreshFrameList(attentionFor(framePanelTarget()).frame);
    view->update();
}

void MainWindow::createChildPaintDock()
{
    m_childPaintWindow = new ChildPaintWindow(this);
    m_childPaintWidget = m_childPaintWindow->paintWidget();
    m_childPaintWidget->model().setTextId(QStringLiteral("child_paint_view"));
    m_childPaintWidget->model().setIntId(2);
    // Added to the left area BEFORE the Tools dock, so the left column reads
    // child view on top, tools underneath.
    addDockWidget(Qt::LeftDockWidgetArea, m_childPaintWindow);
    connect(m_childPaintWindow, &ChildPaintWindow::scriptButtonToggled, this,
            [this](const QString &name, bool on) {
                m_childPaintWidget->sendPythonViewButtonMessage(name, on);
            });
}

#ifdef ANIMEAN_WITH_PYTHON
void MainWindow::fillScriptMenu(QMenu *menu, const QString &menuName, const QJsonArray &items)
{
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("action"));
        if (kind == QStringLiteral("separator")) {
            menu->addSeparator();
            continue;
        }
        const QString title = object.value(QStringLiteral("title")).toString();
        if (kind == QStringLiteral("submenu")) {
            QMenu *child = menu->addMenu(title);
            fillScriptMenu(child, menuName, object.value(QStringLiteral("items")).toArray());
            continue;
        }
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        QAction *action = menu->addAction(title.isEmpty() ? name : title);
        action->setEnabled(object.value(QStringLiteral("enabled")).toBool(true));
        if (kind == QStringLiteral("settings")) {
            // Declarative rather than a call back into C++ from the handler:
            // opening a modal window from inside a menu trigger that Python
            // dispatched would re-enter the interpreter while the menu is
            // still up.
            const QString target = object.value(QStringLiteral("settings")).toString(name);
            connect(action, &QAction::triggered, this, [this, target, title]() {
                openScriptSettings(target, title);
            });
            continue;
        }
        if (kind == QStringLiteral("check") || kind == QStringLiteral("radio")) {
            action->setCheckable(true);
            action->setChecked(object.value(QStringLiteral("checked")).toBool(false));
        }
        connect(action, &QAction::triggered, this, [this, menuName, name](bool checked) {
            activePaintWidget()->sendPythonMenuMessage(menuName, name, checked);
            // The handler may have changed what the panels show.
            refreshPanelTargets();
        });
    }
}

void MainWindow::rebuildScriptMenu(QMenu *menu, const QString &menuName)
{
    // Rebuilt every time the menu opens, from Python. That is what keeps a
    // check mark honest: the state lives in the script, and asking at open
    // time means C++ never has to mirror it (and never drifts from it).
    menu->clear();
    try {
        const std::string json = py::module_::import("python_hooks")
                                     .attr("menus_json")()
                                     .cast<std::string>();
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("name")).toString() != menuName) {
                continue;
            }
            fillScriptMenu(menu, menuName, object.value(QStringLiteral("items")).toArray());
            return;
        }
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("menu error: %1").arg(QString::fromUtf8(error.what())));
    }
}
#endif

void MainWindow::refreshToolOptions()
{
    // Rebuilds the tool options panel from the current tool state. Needed
    // because the Draw Setting window and the panel's own Smooth slider are
    // two views of the SAME stabilizer value.
    if (m_reloadToolOptions) {
        m_reloadToolOptions(m_currentToolForOptions);
    }
}

void MainWindow::openScriptSettings(const QString &name, const QString &title)
{
#ifdef ANIMEAN_WITH_PYTHON
    // The window is built from the SAME control schema the tool options panel
    // uses, and its changes travel down the SAME option hook, so a settings
    // window costs Python a layout description and nothing else.
    QJsonObject layout;
    try {
        const std::string json = py::module_::import("python_hooks")
                                     .attr("settings_layout_json")(name.toStdString())
                                     .cast<std::string>();
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
        layout = document.object();
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("settings error: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    if (layout.isEmpty()) {
        setStatusText(QStringLiteral("No settings registered for '%1'.").arg(name));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title.isEmpty() ? name : title);
    QVBoxLayout *box = new QVBoxLayout(&dialog);
    ToolOptPanel *panel = new ToolOptPanel(&dialog);
    connect(panel, &ToolOptPanel::optionChanged, this,
            [this](const QString &hook, const QString &optionName, const QString &type,
                   const QVariant &value, int row, int startColumn, int endColumn) {
        activePaintWidget()->sendPythonToolOptionMessage(hook, optionName, type, value,
                                                         row, startColumn, endColumn);
        for (PaintOpenGLWidget *view : m_paintViews) {
            view->update();
        }
    });
    panel->configureLayout(layout);
    box->addWidget(panel, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    box->addWidget(buttons);

    // Applied live, so there is nothing to confirm: every change has already
    // gone through the option hook by the time this closes.
    dialog.exec();
#else
    Q_UNUSED(name);
    Q_UNUSED(title);
#endif
}

void MainWindow::createScriptMenus()
{
#ifdef ANIMEAN_WITH_PYTHON
    // Same shape as the extra-tools and view-button queries: Python owns the
    // menus, C++ renders whatever it is given.
    try {
        const std::string json = py::module_::import("python_hooks")
                                     .attr("menus_json")()
                                     .cast<std::string>();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            setStatusText(QStringLiteral("menus JSON error: %1").arg(parseError.errorString()));
            return;
        }
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            const QString title = object.value(QStringLiteral("title")).toString(name);
            QMenu *menu = menuBar()->addMenu(title);
            connect(menu, &QMenu::aboutToShow, this, [this, menu, name]() {
                rebuildScriptMenu(menu, name);
            });
            rebuildScriptMenu(menu, name);
        }
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("menus error: %1").arg(QString::fromUtf8(error.what())));
    }
#endif
}

void MainWindow::populateChildViewButtons()
{
#ifdef ANIMEAN_WITH_PYTHON
    // Same shape as the extra-tools query: Python owns the definitions, C++
    // renders whatever it is given.
    try {
        const std::string json = py::module_::import("python_hooks")
                                     .attr("view_buttons_json")("child")
                                     .cast<std::string>();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            setStatusText(QStringLiteral("view_buttons JSON error: %1").arg(parseError.errorString()));
            return;
        }
        QVector<ChildPaintWindow::ScriptButtonDefinition> definitions;
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            ChildPaintWindow::ScriptButtonDefinition definition;
            definition.name = object.value(QStringLiteral("name")).toString();
            definition.title = object.value(QStringLiteral("title")).toString();
            definition.tooltip = object.value(QStringLiteral("tooltip")).toString();
            definition.checkable = object.value(QStringLiteral("checkable")).toBool(true);
            if (!definition.name.isEmpty()) {
                definitions.append(definition);
            }
        }
        m_childPaintWindow->setScriptButtons(definitions);
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("view_buttons error: %1").arg(QString::fromUtf8(error.what())));
    }
#endif
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
    stopPlayback();
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
    m_childPaintWidget->modelReplaced();
    m_childPaintWidget->resetHistory(QStringLiteral("Open Texture View"));
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
    if (!m_childPaintWidget || !m_childPaintWindow) {
        return false;
    }

    // Make the texture view renderable for the framebuffer grab WITHOUT
    // changing the active editing view: exporting is read-only, and a
    // cancelled export must leave the active view untouched. The active-view
    // border is UI chrome — hide it during the grab so it is not baked into
    // the exported image.
    m_childPaintWindow->show();
    m_childPaintWindow->raise();
    m_childPaintWidget->setActiveIndicator(false);
    QCoreApplication::processEvents();
    const QImage image = m_childPaintWidget->grabFramebuffer();
    m_childPaintWidget->setActiveIndicator(activePaintWidget() == m_childPaintWidget);
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

    stopPlayback();  // the cache belongs to the view that was playing
    m_activePaintWidget = view;
    m_hasPendingAttention = false;
    // Keep keyboard focus in lockstep with the active view: otherwise clicking
    // the still-focused other canvas produces no focusInEvent, the active view
    // never follows, and panel/tool operations land in the wrong scene.
    view->setFocus(Qt::OtherFocusReason);
    for (PaintOpenGLWidget *paintView : m_paintViews) {
        paintView->setActiveIndicator(paintView == view);
    }
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&view->model());
#endif
    refreshPanelTargets();
    scheduleHistoryRefresh();
    syncEmbeddedPythonState();
    if (m_forcePadDock) {
        // The pad acts on the active view; say so where the user is looking.
        m_forcePadDock->setWindowTitle(QStringLiteral("Repulsion Pad - %1").arg(view->viewName()));
    }
    setStatusText(QStringLiteral("Active paint view: %1").arg(view->viewName()));
}

void MainWindow::refreshPanelTargets()
{
    refreshFrameList(attentionFor(framePanelTarget()).frame);
    refreshLayerList(attentionFor(layerPanelTarget()).layer);
    refreshAssetList(attentionFor(assetPanelTarget()).asset);
    // The rate is per document, so it follows whichever view the Frames panel
    // is pointed at - and it has to resync after a load or an undo too.
    refreshFpsCombo();
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
    // High-frequency hooks (pad drags, per-move events) must not grow the
    // document without bound across a session.
    m_pythonDebugOutput->setMaximumBlockCount(2000);

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
        // The DOCUMENT's page, not the widget's size: these bound region
        // detection and anything else script-side that asks "how big is the
        // canvas", and neither should move when a dock is dragged.
        globals["canvas_width"] = view->model().canvasSize().width();
        globals["canvas_height"] = view->model().canvasSize().height();
        globals["main_canvas_width"] = m_paintWidget->model().canvasSize().width();
        globals["main_canvas_height"] = m_paintWidget->model().canvasSize().height();
        globals["child_canvas_width"] = m_childPaintWidget->model().canvasSize().width();
        globals["child_canvas_height"] = m_childPaintWidget->model().canvasSize().height();
    } catch (const py::error_already_set &error) {
        appendPythonDebugMessage(QStringLiteral("[python register] error: %1").arg(QString::fromUtf8(error.what())));
        setStatusText(QStringLiteral("Python state sync error: %1").arg(QString::fromUtf8(error.what())));
    }
#endif
}

void MainWindow::setupConnections()
{
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newProject);
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

        connect(view, &PaintOpenGLWidget::playbackInterrupted,
                this, &MainWindow::stopPlayback);
    }

    connect(m_childPaintWindow, &ChildPaintWindow::changableTimelineToggled, this, [this](bool) {
        refreshPanelTargets();
    });
    connect(m_childPaintWindow, &ChildPaintWindow::changableLayerToggled, this, [this](bool) {
        refreshPanelTargets();
    });

    connect(m_layerPanel->layerList(), &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        // Group rows carry no layer of their own; selecting one must not
        // move attention (and must not be read as "layer 0" either).
        if (m_refreshingLists || !item || item->data(0, kGroupIdRole).toInt() != 0) {
            return;
        }
        const int layerIndex = item->data(0, Qt::UserRole).toInt();
        if (layerIndex < 0) {
            return;
        }
        PaintOpenGLWidget *view = layerPanelTarget();
        requestAttentionUpdate(view, AttentionChange::LayerChange,
                               attentionFor(view).frame, layerIndex, attentionFor(view).asset);
    });

    // Collapsing a group is a document edit, not a view whim: it is what the
    // H/V group's "collapsed by default" means, so it has to survive a
    // refresh, a save and a history restore.
    auto rememberExpansion = [this](QTreeWidgetItem *item, bool collapsed) {
        if (m_refreshingLists || !item) {
            return;
        }
        const int groupId = item->data(0, kGroupIdRole).toInt();
        if (groupId > 0) {
            layerPanelTarget()->model().setLayerGroupCollapsed(groupId, collapsed);
        }
    };
    connect(m_layerPanel->layerList(), &QTreeWidget::itemExpanded, this,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, false); });
    connect(m_layerPanel->layerList(), &QTreeWidget::itemCollapsed, this,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, true); });

    connect(m_layerPanel->layerList(), &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int) {
        if (m_refreshingLists || !item || item->data(0, kGroupIdRole).toInt() != 0) {
            return;
        }
        const int layerIndex = item->data(0, Qt::UserRole).toInt();
        if (layerIndex < 0) {
            return;
        }
        const bool visible = item->checkState(0) == Qt::Checked;
        PaintOpenGLWidget *view = layerPanelTarget();
        // Deferred so the Python hook (which may rebuild this very list) never
        // runs inside the itemChanged emission.
        QMetaObject::invokeMethod(this, [this, view, layerIndex, visible]() {
            // UI click -> Python decides -> commands come back through the
            // bindings. The direct model write is the no-hook fallback.
            if (!view->sendPythonLayerVisibilityMessage(layerIndex, visible)) {
                view->model().setLayerVisible(layerIndex, visible);
                view->update();
                refreshLayerList(attentionFor(view).layer);
            }
        }, Qt::QueuedConnection);
    });

    connect(m_framePanel->frameList(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            // Picking a frame by hand means the user is done watching; without
            // this the next tick would snap the highlight back.
            stopPlayback();
            PaintOpenGLWidget *view = framePanelTarget();
            requestAttentionUpdate(view, AttentionChange::FrameChange,
                                   row, attentionFor(view).layer, attentionFor(view).asset);
        }
    });

    connect(m_layerPanel->layerList(), &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showLayerContextMenu(pos); });

    connect(m_layerPanel->addButton(), &QPushButton::clicked, this, [this]() {
        PaintOpenGLWidget *view = layerPanelTarget();
        const int layerIndex = view->addLayer();
        updateAttention(view, AttentionChange::LayerChange,
                        attentionFor(view).frame, layerIndex, attentionFor(view).asset);
    });

    connect(m_layerPanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem *item = m_layerPanel->layerList()->currentItem();
        PaintOpenGLWidget *view = layerPanelTarget();
        if (item && item->data(0, kGroupIdRole).toInt() > 0) {
            // Remove Layer on a group removes the group AND its layers - that
            // is what "delete" means for the thing the user is pointing at.
            // Dropping only the grouping lives on the right-click menu as
            // "Ungroup", so the non-destructive option is still one click away.
            const int groupId = item->data(0, kGroupIdRole).toInt();
            if (view->deleteLayerGroup(groupId) > 0) {
                const int next = std::min(attentionFor(view).layer, view->layerCount() - 1);
                updateAttention(view, AttentionChange::LayerChange,
                                attentionFor(view).frame, next, attentionFor(view).asset);
            }
            return;
        }
        const int layerIndex = item ? item->data(0, Qt::UserRole).toInt() : -1;
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
        stopPlayback();  // editing the timeline invalidates the prerender
        PaintOpenGLWidget *view = framePanelTarget();
        const int frameIndex = view->addFrame();
        updateAttention(view, AttentionChange::FrameChange,
                        frameIndex, attentionFor(view).layer, attentionFor(view).asset);
    });

    connect(m_framePanel->addHoldButton(), &QPushButton::clicked, this, [this]() {
        stopPlayback();
        PaintOpenGLWidget *view = framePanelTarget();
        const int frameIndex = view->addHoldFrame();
        updateAttention(view, AttentionChange::FrameChange,
                        frameIndex, attentionFor(view).layer, attentionFor(view).asset);
    });

    connect(m_framePanel->deleteButton(), &QPushButton::clicked, this, [this]() {
        stopPlayback();
        const int row = m_framePanel->frameList()->currentRow();
        PaintOpenGLWidget *view = framePanelTarget();
        if (view->deleteFrame(row)) {
            const int nextFrame = row < view->frameCount() ? row : view->frameCount() - 1;
            updateAttention(view, AttentionChange::FrameChange,
                            nextFrame, attentionFor(view).layer, attentionFor(view).asset);
        }
    });

    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setInterval(1000 / kPlaybackFps);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::advancePlaybackFrame);
    connect(m_framePanel->playButton(), &QPushButton::clicked, this, &MainWindow::startPlayback);
    connect(m_framePanel->pauseButton(), &QPushButton::clicked, this, &MainWindow::stopPlayback);

    // The rate belongs to the document, so both the preset list and a typed
    // number land in the model; the panel then re-reads it, which normalises
    // whatever was typed back into the canonical text.
    auto applyFps = [this]() {
        if (m_refreshingLists) {
            return;
        }
        PaintOpenGLWidget *view = framePanelTarget();
        const int current = view->model().playbackFps();
        const int fps = FramePanel::fpsForComboText(m_framePanel->fpsCombo()->currentText(), current);
        if (fps != current) {
            view->model().setPlaybackFps(fps);
            view->commitHistory(QStringLiteral("Playback Rate"));
        }
        refreshFpsCombo();
        if (m_playbackTimer->isActive()) {
            m_playbackTimer->setInterval(1000 / fps);
        }
        setStatusText(QStringLiteral("Playback: %1 fps").arg(fps));
    };
    connect(m_framePanel->fpsCombo(), &QComboBox::activated, this, [applyFps](int) { applyFps(); });
    connect(m_framePanel->fpsCombo()->lineEdit(), &QLineEdit::editingFinished, this, applyFps);
    refreshFpsCombo();

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

    // NOT rowsMoved. QListModel overrides moveRows, so the old flat list really
    // did emit it; QTreeModel does not, and QTreeWidget::dropEvent implements
    // an internal move itself as takeItem + insertItem - which emits
    // rowsRemoved then rowsInserted and NEVER rowsMoved. Listening for the
    // signal Qt does not send left the panel reshaping itself on a drop while
    // the model was never told, so the row snapped back on the next refresh
    // and layer reordering by drag silently stopped working.
    connect(m_layerPanel->layerList()->model(), &QAbstractItemModel::rowsInserted,
            this, [this](const QModelIndex &parent, int first, int last) {
        if (m_refreshingLists || !m_layerDropInProgress || first != last) {
            return;
        }
        QTreeWidget *tree = m_layerPanel->layerList();
        QTreeWidgetItem *parentItem = parent.isValid() ? tree->itemFromIndex(parent) : nullptr;
        const int count = parentItem ? parentItem->childCount() : tree->topLevelItemCount();
        if (first < 0 || first >= count) {
            return;
        }
        QTreeWidgetItem *movedItem = parentItem ? parentItem->child(first)
                                                : tree->topLevelItem(first);
        m_layerDropInProgress = false;
        // Qt has already reshaped the widget; the model has to be told what
        // the new shape means. Deferred so the model edit never runs inside
        // the view's own drop handling - and re-found by column id there,
        // because a refresh in between would delete this pointer.
        const int movedColumnId = movedItem && movedItem->data(0, kGroupIdRole).toInt() == 0
                                      ? layerPanelTarget()->model().layerIdAt(
                                            movedItem->data(0, Qt::UserRole).toInt())
                                      : 0;
        QMetaObject::invokeMethod(this, [this, movedColumnId]() {
            applyLayerPanelStructure(movedColumnId);
        }, Qt::QueuedConnection);
    });

    connect(m_framePanel->frameList()->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        stopPlayback();  // reordering frames invalidates the prerender
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
        showMainPaintView();
        importRaster(m_paintWidget);
    });
    connect(ui->actionImport_OpenToonz_Lines, &QAction::triggered, this, [this]() {
        showMainPaintView();
        importOpenToonzLines(m_paintWidget);
    });
    connect(ui->actionImport_Clip_Studio_Paint, &QAction::triggered, this, [this]() {
        showMainPaintView();
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
            // Arms the rowsInserted handler: QTreeWidget reorders itself with
            // takeItem + insertItem, and an insert only means "a drop landed"
            // while one is actually in flight.
            if (event->type() == QEvent::Drop) {
                m_layerDropInProgress = true;
            }
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

    m_toolOptPanel = toolOptPanel;
    auto loadToolOptions = [this, toolOptPanel](PaintOpenGLWidget::Tool tool) {
        m_currentToolForOptions = int(tool);
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
    m_reloadToolOptions = [loadToolOptions](int tool) {
        loadToolOptions(static_cast<PaintOpenGLWidget::Tool>(tool));
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
        QJsonObject extraLayout;
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::dict state;
            state["smooth"] = m_toolSmoothValue;
            state["pen_width"] = m_toolPenWidth;
            state["fill_scope"] = m_toolFillAllLayers ? "all" : "current";
            const std::string json = py::module_::import("toolcontrol")
                                         .attr("options_for_extra_tool_json")(tool.name.toStdString(), state)
                                         .cast<std::string>();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                extraLayout = document.object();
            }
        } catch (const py::error_already_set &error) {
            setStatusText(QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
#endif
        toolOptPanel->configureLayout(extraLayout);
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

void MainWindow::newProject()
{
    // Named "Canvas Size...", not "New": it resizes the page of the document
    // that is already open and deliberately does NOT start a blank one. A
    // menu entry called New that leaves the artwork and the file path in
    // place would be a trap - the next Ctrl+S would write the old drawing
    // back over the file the user thought they had left behind.
    NewProjectDialog dialog(m_paintWidget->model().canvasSize(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    applyNewCanvasSize(dialog.canvasSize());
}

void MainWindow::applyNewCanvasSize(const QSize &size)
{
    if (m_paintWidget->model().canvasSize() == size) {
        // Nothing changed, so nothing is committed. A commit here would be a
        // phantom history entry that also drops the redo future of BOTH views
        // (a new edit anywhere clears the whole redo tail), so pressing Enter
        // on the dialog without touching it would silently cost the user
        // their redo stack.
        setStatusText(QStringLiteral("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
        return;
    }
    m_paintWidget->setCanvasSize(size);
    // The page belongs to the document, so changing it is an edit: it has to
    // be undoable and it has to be saved.
    m_paintWidget->commitHistory(QStringLiteral("Canvas Size"));
    syncEmbeddedPythonState();
    setStatusText(QStringLiteral("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
}

void MainWindow::promptForNewCanvasOnStartup()
{
    NewProjectDialog dialog(m_paintWidget->model().canvasSize(), this);
    // Cancel keeps the default page rather than closing the app: the user
    // asked to be shown a size, not to be blocked by one.
    if (dialog.exec() == QDialog::Accepted) {
        applyNewCanvasSize(dialog.canvasSize());
    }
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
    stopPlayback();
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
    // Force the main view's scene identity: a .animean saved from the texture
    // view carries textId "child_paint_view"/intId 2, which would otherwise
    // collide with the child scene once loaded into the main model.
    m_paintWidget->model().setTextId(QStringLiteral("main_paint_view"));
    m_paintWidget->model().setIntId(1);
    m_paintWidget->modelReplaced();   // the loaded document brings its own page
    m_paintWidget->resetHistory(QStringLiteral("Open Project"));
    m_currentFilePath = fileName;
    updateWindowTitle();
    showMainPaintView();
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
        // The PAGE, not the viewport: "fit to canvas" has to mean the document,
    // and the message below quotes this same size back to the user.
    const QSizeF canvasSize = view->documentRect().size();
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
                    .arg(qRound(canvasSize.width()))
                    .arg(qRound(canvasSize.height())),
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
    // The PAGE, not the viewport: "fit to canvas" has to mean the document,
    // and the message below quotes this same size back to the user.
    const QSizeF canvasSize = view->documentRect().size();
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
                .arg(qRound(canvasSize.width()))
                .arg(qRound(canvasSize.height())),
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

QVector<QTreeWidgetItem *> MainWindow::layerPanelItems() const
{
    // Display order, depth first, independent of what is expanded.
    QVector<QTreeWidgetItem *> items;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
        items.append(item);
        for (int i = 0; i < item->childCount(); ++i) {
            walk(item->child(i));
        }
    };
    QTreeWidget *tree = m_layerPanel->layerList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        walk(tree->topLevelItem(i));
    }
    return items;
}

void MainWindow::showLayerContextMenu(const QPoint &pos)
{
#ifdef ANIMEAN_WITH_PYTHON
    QTreeWidget *tree = m_layerPanel->layerList();
    QTreeWidgetItem *item = tree->itemAt(pos);
    if (!item) {
        return;
    }
    PaintOpenGLWidget *view = layerPanelTarget();

    const int groupId = item->data(0, kGroupIdRole).toInt();
    const int layerIndex = groupId > 0 ? -1 : item->data(0, Qt::UserRole).toInt();
    // A group's members, flattened, so a provider can inspect what it holds
    // without needing its own view of the tree.
    QVector<int> members;
    if (groupId > 0) {
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *node) {
            for (int i = 0; i < node->childCount(); ++i) {
                QTreeWidgetItem *child = node->child(i);
                if (child->data(0, kGroupIdRole).toInt() > 0) {
                    walk(child);
                } else {
                    members.append(child->data(0, Qt::UserRole).toInt());
                }
            }
        };
        walk(item);
    }

    // Python decides what this row offers; C++ only renders and reports.
    QJsonArray entries;
    try {
        py::dict context;
        context["view"] = view->viewName().toStdString();
        context["kind"] = groupId > 0 ? "group" : "layer";
        context["group"] = groupId;
        context["group_name"] = item->text(0).toStdString();
        context["layer"] = layerIndex;
        context["layer_name"] = layerIndex >= 0 ? view->layerName(layerIndex).toStdString()
                                                : std::string();
        py::list memberList;
        for (int index : members) {
            memberList.append(index);
        }
        context["members"] = memberList;

        const std::string json = py::module_::import("python_hooks")
                                     .attr("menu_items_json")(context)
                                     .cast<std::string>();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
        // A script problem costs the script's entries and nothing else - the
        // built-in group actions below must not depend on Python being well.
        if (parseError.error == QJsonParseError::NoError && document.isArray()) {
            entries = document.array();
        }
    } catch (const py::error_already_set &error) {
        setStatusText(QStringLiteral("layer menu error: %1").arg(QString::fromUtf8(error.what())));
    }
    QMenu menu(this);
    if (groupId > 0) {
        // Built in, not script-provided: ungrouping is a generic layer
        // operation with no tool semantics, so C++ owns it. It is the
        // counterpart to Remove Layer, which deletes the group's contents.
        QAction *ungroup = menu.addAction(QStringLiteral("Ungroup (keep the layers)"));
        connect(ungroup, &QAction::triggered, this, [this, view, groupId]() {
            if (view->model().dissolveLayerGroup(groupId)) {
                view->commitHistory(QStringLiteral("Ungroup Layers"));
                refreshPanelTargets();
            }
        });
        QAction *deleteGroup = menu.addAction(QStringLiteral("Delete Group and Layers"));
        connect(deleteGroup, &QAction::triggered, this, [this, view, groupId]() {
            if (view->deleteLayerGroup(groupId) > 0) {
                const int next = std::min(attentionFor(view).layer, view->layerCount() - 1);
                updateAttention(view, AttentionChange::LayerChange,
                                attentionFor(view).frame, next, attentionFor(view).asset);
            }
        });
        if (!entries.isEmpty()) {
            menu.addSeparator();
        }
    }
    if (groupId <= 0 && entries.isEmpty()) {
        return;
    }
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        QAction *action = menu.addAction(object.value(QStringLiteral("title")).toString(name));
        action->setEnabled(object.value(QStringLiteral("enabled")).toBool(true));
        const QString groupName = item->text(0);
        const QString layerName = layerIndex >= 0 ? view->layerName(layerIndex) : QString();
        connect(action, &QAction::triggered, this,
                [this, view, name, groupId, groupName, layerIndex, layerName, members]() {
            view->sendPythonLayerMenuMessage(name, groupId, groupName,
                                             layerIndex, layerName, members);
            // The handler may have edited the document (that is the point),
            // so resync rather than trusting the panel to still be right.
            refreshPanelTargets();
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(tree->viewport()->mapToGlobal(pos));
    }
#else
    Q_UNUSED(pos);
#endif
}

void MainWindow::applyLayerPanelStructure(int movedColumnId)
{
    PaintOpenGLWidget *view = layerPanelTarget();
    AnimeSceneModel &model = view->model();
    QTreeWidget *tree = m_layerPanel->layerList();

    // Capture the widget as a node tree BEFORE touching the columns. Leaves
    // are recorded by stable column id, so the reorder below - which shifts
    // every index after it - cannot invalidate what we captured.
    std::function<QVector<AnimeLayerNode>(QTreeWidgetItem *)> capture =
        [&](QTreeWidgetItem *parent) {
        QVector<AnimeLayerNode> nodes;
        const int count = parent ? parent->childCount() : tree->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            QTreeWidgetItem *item = parent ? parent->child(i) : tree->topLevelItem(i);
            AnimeLayerNode node;
            const int groupId = item->data(0, kGroupIdRole).toInt();
            if (groupId > 0) {
                node.groupId = groupId;
                node.name = item->text(0);
                node.collapsed = !item->isExpanded();
                node.children = capture(item);
                if (node.children.isEmpty()) {
                    continue;
                }
            } else {
                node.layerId = model.layerIdAt(item->data(0, Qt::UserRole).toInt());
                if (node.layerId <= 0) {
                    continue;
                }
            }
            nodes.append(node);
        }
        return nodes;
    };
    const QVector<AnimeLayerNode> captured = capture(nullptr);

    // Z-order follows the panel: the dragged layer lands right after the leaf
    // shown above it, exactly as the flat list behaved. The item is re-found
    // by column id rather than held as a pointer, because any refresh between
    // the drop and this queued call deletes every item in the widget.
    int landedOn = -1;
    const int fromIndex = model.layerIndexForId(movedColumnId);
    if (fromIndex >= 0) {
        const QVector<QTreeWidgetItem *> items = layerPanelItems();
        int position = -1;
        for (int i = 0; i < items.size(); ++i) {
            if (items[i]->data(0, kGroupIdRole).toInt() == 0
                && items[i]->data(0, Qt::UserRole).toInt() == fromIndex) {
                position = i;
                break;
            }
        }
        int toIndex = 0;
        for (int i = position - 1; i >= 0; --i) {
            if (items[i]->data(0, kGroupIdRole).toInt() == 0) {
                toIndex = items[i]->data(0, Qt::UserRole).toInt() + 1;
                break;
            }
        }
        if (fromIndex < toIndex) {
            --toIndex;
        }
        if (position >= 0 && fromIndex != toIndex && model.moveLayer(fromIndex, toIndex)) {
            model.remapFillSourceLayersAfterMove(fromIndex, toIndex);
            landedOn = toIndex;
        }
    }

    model.setLayerTree(captured);
    view->commitHistory(QStringLiteral("Reorder Layers"));
    updateAttention(view, AttentionChange::LayerChange, attentionFor(view).frame,
                    landedOn >= 0 ? landedOn : attentionFor(view).layer,
                    attentionFor(view).asset);
}

void MainWindow::refreshLayerList(int selectedRow)
{
    PaintOpenGLWidget *view = layerPanelTarget();
    QTreeWidget *tree = m_layerPanel->layerList();
    const AnimeSceneModel &model = view->model();
    const int frame = model.currentFrame();

    // Work out what the panel should show BEFORE touching it, so the common
    // case can be applied in place.
    struct Row {
        bool group = false;
        int layerIndex = -1;
        int groupId = 0;
        QString name;
        bool visible = true;
        bool collapsed = false;
        int depth = 0;
    };
    QVector<Row> rows;
    // A layer shows when it is not a script-owned working layer and it has a
    // cell on the current frame - the same two tests the flat list used, now
    // applied while walking the group tree instead of the column vector.
    std::function<int(const QVector<AnimeLayerNode> &, int)> collect =
        [&](const QVector<AnimeLayerNode> &nodes, int depth) {
        int shown = 0;
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                const int mark = rows.size();
                Row row;
                row.group = true;
                row.groupId = node.groupId;
                row.name = node.name;
                row.collapsed = node.collapsed;
                row.depth = depth;
                rows.append(row);
                if (collect(node.children, depth + 1) == 0) {
                    // Every member is hidden on this frame; an empty group
                    // would suggest content that is not there.
                    rows.remove(mark);
                    continue;
                }
                ++shown;
                continue;
            }
            const int index = model.layerIndexForId(node.layerId);
            if (index < 0 || model.layerInternal(index)
                || model.assetIndexAt(frame, index) < 0) {
                continue;
            }
            Row row;
            row.layerIndex = index;
            row.name = view->layerName(index);
            row.visible = model.layerVisible(index);
            row.depth = depth;
            rows.append(row);
            ++shown;
        }
        return shown;
    };
    collect(model.layerTree(), 0);

    const QTreeWidgetItem *currentItem = tree->currentItem();
    const int previousLayer = currentItem ? currentItem->data(0, Qt::UserRole).toInt() : -1;

    m_refreshingLists = true;
    const QSignalBlocker blocker(tree);

    // Same rows in the same shape means this refresh carries an ATTRIBUTE
    // change - a visibility toggle, a rename - and rebuilding the widget for
    // it is both wasteful and destructive: clear() drops the viewport's
    // scroll position (and every expanded state), so a user reading row 80 of
    // 100 was thrown to wherever the current layer happened to sit the moment
    // they ticked a checkbox.
    QVector<QTreeWidgetItem *> existing = layerPanelItems();
    bool sameRows = existing.size() == rows.size();
    for (int i = 0; sameRows && i < rows.size(); ++i) {
        const QTreeWidgetItem *item = existing[i];
        // DEPTH is part of the comparison. Without it a widget whose nesting
        // disagreed with the model still counted as "same rows", so the
        // in-place path preserved the wrong shape forever instead of
        // rebuilding it from the document.
        int depth = 0;
        for (const QTreeWidgetItem *parent = item->parent(); parent; parent = parent->parent()) {
            ++depth;
        }
        sameRows = item->data(0, kGroupIdRole).toInt() == rows[i].groupId
                   && item->data(0, Qt::UserRole).toInt() == rows[i].layerIndex
                   && depth == rows[i].depth;
    }

    int restoreScroll = -1;
    if (sameRows) {
        for (int i = 0; i < rows.size(); ++i) {
            QTreeWidgetItem *item = existing[i];
            if (item->text(0) != rows[i].name) {
                item->setText(0, rows[i].name);
            }
            if (rows[i].group) {
                if (item->isExpanded() == rows[i].collapsed) {
                    item->setExpanded(!rows[i].collapsed);
                }
                continue;
            }
            const Qt::CheckState state = rows[i].visible ? Qt::Checked : Qt::Unchecked;
            if (item->checkState(0) != state) {
                item->setCheckState(0, state);
            }
        }
    } else {
        const int scroll = tree->verticalScrollBar()->value();
        tree->clear();
        QVector<QTreeWidgetItem *> stack;   // stack[d] = the open parent at depth d
        for (const Row &row : rows) {
            QTreeWidgetItem *item = new QTreeWidgetItem;
            item->setText(0, row.name);
            item->setData(0, Qt::UserRole, row.layerIndex);
            item->setData(0, kGroupIdRole, row.groupId);
            if (row.group) {
                // Groups are containers: no visibility of their own, and not
                // a drop target for reordering.
                item->setFlags((item->flags() | Qt::ItemIsDragEnabled)
                               & ~Qt::ItemIsUserCheckable);
                QFont font = item->font(0);
                font.setBold(true);
                item->setFont(0, font);
            } else {
                // A layer may be dragged, but nothing may be dropped INTO it:
                // the model has no way to express a layer inside a layer, and
                // the widget happily drew one (complete with an expander) that
                // no refresh could ever repair.
                item->setFlags((item->flags() | Qt::ItemIsUserCheckable)
                               & ~Qt::ItemIsDropEnabled);
                item->setCheckState(0, row.visible ? Qt::Checked : Qt::Unchecked);
            }
            if (row.depth > 0 && row.depth - 1 < stack.size() && stack[row.depth - 1]) {
                stack[row.depth - 1]->addChild(item);
            } else {
                tree->addTopLevelItem(item);
            }
            stack.resize(row.depth + 1);
            stack[row.depth] = row.group ? item : nullptr;
            if (row.group) {
                item->setExpanded(!row.collapsed);
            }
        }
        if (previousLayer == selectedRow) {
            // The rows changed but the user's selection did not, so this is
            // not a "go look over here" event - put the viewport back where
            // they left it. Applied after the selection below, which would
            // otherwise scroll the selection into view and undo it.
            restoreScroll = scroll;
        }
    }

    QTreeWidgetItem *selectedItem = nullptr;
    if (selectedRow >= 0) {
        for (QTreeWidgetItem *item : layerPanelItems()) {
            if (item->data(0, kGroupIdRole).toInt() == 0
                && item->data(0, Qt::UserRole).toInt() == selectedRow) {
                selectedItem = item;
                break;
            }
        }
    }
    if (selectedItem) {
        // A selected layer inside a collapsed group has to be reachable.
        for (QTreeWidgetItem *parent = selectedItem->parent(); parent; parent = parent->parent()) {
            parent->setExpanded(true);
        }
        if (tree->currentItem() != selectedItem) {
            tree->setCurrentItem(selectedItem);
        }
    } else {
        tree->clearSelection();
        tree->setCurrentItem(nullptr);
    }
    if (restoreScroll >= 0) {
        tree->verticalScrollBar()->setValue(restoreScroll);
    }
    m_refreshingLists = false;
}

void MainWindow::refreshFpsCombo()
{
    QComboBox *combo = m_framePanel->fpsCombo();
    const int fps = framePanelTarget()->model().playbackFps();
    const QString text = FramePanel::comboTextForFps(fps);
    if (combo->currentText() == text) {
        return;
    }
    const bool wasRefreshing = m_refreshingLists;
    m_refreshingLists = true;
    const QSignalBlocker blocker(combo);
    const int index = combo->findText(text);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    } else {
        combo->setCurrentIndex(-1);
        combo->setEditText(text);
    }
    m_refreshingLists = wasRefreshing;
}

void MainWindow::refreshFrameList(int selectedRow)
{
    PaintOpenGLWidget *view = framePanelTarget();
    QListWidget *list = m_framePanel->frameList();
    m_refreshingLists = true;
    const QSignalBlocker blocker(list);
    // Same reasoning as refreshLayerList: a refresh that leaves the selection
    // where it was must leave the viewport where it was too.
    const int previousRow = list->currentRow();
    const int scroll = list->verticalScrollBar()->value();
    list->clear();
    for (int i = 0; i < view->frameCount(); ++i) {
        // "O" marks a HELD frame - one that shows the row above's drawing
        // rather than one of its own. Derived from the cells every refresh,
        // so it disappears the moment the frame stops holding.
        const bool hold = view->model().isHoldFrame(i);
        list->addItem(hold ? QStringLiteral("%1   O").arg(view->frameName(i))
                           : view->frameName(i));
        if (hold) {
            list->item(list->count() - 1)->setToolTip(
                QStringLiteral("Held: shows the same drawing as frame %1. "
                               "Editing either one changes both.").arg(i));
        }
    }
    if (list->count() > 0) {
        if (selectedRow < 0) {
            list->clearSelection();
            list->setCurrentRow(-1);
            list->verticalScrollBar()->setValue(scroll);
            m_refreshingLists = false;
            return;
        } else if (selectedRow >= list->count()) {
            selectedRow = list->count() - 1;
        }
        list->setCurrentRow(selectedRow);
        if (previousRow == selectedRow) {
            list->verticalScrollBar()->setValue(scroll);
        }
    }
    m_refreshingLists = false;
}

void MainWindow::refreshAssetList(int selectedRow)
{
    PaintOpenGLWidget *view = assetPanelTarget();
    QListWidget *list = m_assetPanel->assetList();
    m_refreshingLists = true;
    const QSignalBlocker blocker(list);
    // Same reasoning as refreshLayerList.
    const int previousRow = list->currentRow();
    const int scroll = list->verticalScrollBar()->value();
    list->clear();
    for (int i = 0; i < view->assetCount(); ++i) {
        // Backing assets of script-owned working layers stay out of the
        // panel. They only ever live at the end of the asset list, so the
        // row == asset-index mapping below survives the skip.
        if (view->model().assetInternal(i)) {
            continue;
        }
        list->addItem(view->assetName(i));
    }
    if (selectedRow >= 0 && list->count() > 0) {
        if (selectedRow >= list->count()) {
            selectedRow = list->count() - 1;
        }
        list->setCurrentRow(selectedRow);
        if (previousRow == selectedRow) {
            list->verticalScrollBar()->setValue(scroll);
        }
    } else {
        list->clearSelection();
        list->setCurrentRow(-1);
        list->verticalScrollBar()->setValue(scroll);
    }
    m_refreshingLists = false;
}


