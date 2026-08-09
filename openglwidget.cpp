#include "openglwidget.h"

#include <QLineF>
#include <QImage>
#include <QPainter>
#include <QDebug>

namespace {
const qreal kOverlayHandleSize = 14.0;
}

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
#endif

#include <algorithm>
#include <cmath>

#ifdef ANIMEAN_WITH_PYTHON
#include "pythonbind/python_bindings.h"

namespace py = pybind11;

namespace {
QString toolName(PaintOpenGLWidget::Tool tool)
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
    case PaintOpenGLWidget::Tool::Arrow:
        return QStringLiteral("arrow");
    }
    return QStringLiteral("pen");
}

py::dict pointToPythonDict(const QPointF &point)
{
    py::dict pointInfo;
    pointInfo["x"] = point.x();
    pointInfo["y"] = point.y();
    return pointInfo;
}

py::object variantToPythonObject(const QVariant &value)
{
    switch (value.userType()) {
    case QMetaType::Bool:
        return py::bool_(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return py::int_(value.toLongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return py::float_(value.toDouble());
    default:
        return py::str(value.toString().toStdString());
    }
}

py::dict strokeToPythonDict(const AnimeVectorStroke &stroke, int strokeIndex = -1)
{
    py::dict strokeInfo;
    strokeInfo["id"] = stroke.id;
    strokeInfo["property"] = stroke.property.toStdString();
    strokeInfo["width"] = stroke.width;
    strokeInfo["point_count"] = stroke.points.size();
    strokeInfo["total_length"] = stroke.totalLength;
    if (strokeIndex >= 0) {
        strokeInfo["index"] = strokeIndex;
    }
    return strokeInfo;
}

// Dispatches that produced no output at all print as "(no output)"; relaying
// those to the debug dock buries real feedback under one line per event.
bool isQuietHookOutput(const QString &output)
{
    return output.isEmpty() || output.endsWith(QStringLiteral("(no output)"));
}

QString pythonHookSendMessage(const py::dict &message)
{
    try {
        py::gil_scoped_acquire acquire;
        py::object hooks = py::module_::import("python_hooks");
        if (!hooks.attr("has_hooks")(message).cast<bool>()) {
            return QString();
        }
        py::object io = py::module_::import("io");
        py::object contextlib = py::module_::import("contextlib");
        py::object stdoutBuffer = io.attr("StringIO")();
        py::object stderrBuffer = io.attr("StringIO")();
        py::object stdoutRedirect = contextlib.attr("redirect_stdout")(stdoutBuffer);
        py::object stderrRedirect = contextlib.attr("redirect_stderr")(stderrBuffer);

        stdoutRedirect.attr("__enter__")();
        stderrRedirect.attr("__enter__")();
        QString errorText;
        try {
            hooks.attr("dispatch")(message);
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
        const QString event = QString::fromUtf8(py::str(message["event"]).cast<std::string>().c_str());
        const QString tool = QString::fromUtf8(py::str(message["tool"]).cast<std::string>().c_str());
        return QStringLiteral("[python feedback] %1 tool=%2\n%3").arg(event, tool, output.trimmed());
    } catch (const py::error_already_set &error) {
        return QStringLiteral("[python feedback] hook setup error: %1").arg(QString::fromUtf8(error.what()));
    }
}
}
#endif

PaintOpenGLWidget::PaintOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    m_history.reset(QStringLiteral("Initial"), m_model);
}

SceneHistory &PaintOpenGLWidget::history()
{
    return m_history;
}

const SceneHistory &PaintOpenGLWidget::history() const
{
    return m_history;
}

void PaintOpenGLWidget::commitHistory(const QString &label)
{
    m_history.commit(label, m_model);
    emit historyChanged();
    emit historyCommitted();
}

void PaintOpenGLWidget::dropRedoTail()
{
    if (m_history.truncateRedo()) {
        emit historyChanged();
    }
}

void PaintOpenGLWidget::resetHistory(const QString &label)
{
    m_history.reset(label, m_model);
    // Scene content was (re)established outside the history flow (startup,
    // project open): let scripts re-sync any state they keep in scriptData.
    pythonHookSendMessage(QStringLiteral("historyrestore"));
    emit historyChanged();
}

bool PaintOpenGLWidget::undoHistory()
{
    return goToHistory(m_history.currentIndex() - 1);
}

bool PaintOpenGLWidget::redoHistory()
{
    return goToHistory(m_history.currentIndex() + 1);
}

bool PaintOpenGLWidget::goToHistory(int index)
{
    if (!m_history.goTo(index, &m_model)) {
        return false;
    }

    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    m_hasLastMovePos = false;
    m_eraseGestureChanged = false;
    m_moveGestureChanged = false;
    m_axisSnapState = AxisSnapState::Inactive;
    pythonHookSendMessage(QStringLiteral("historyrestore"));
    update();
    emit historyChanged();
    return true;
}

void PaintOpenGLWidget::setViewName(const QString &name)
{
    m_viewName = name;
}

QString PaintOpenGLWidget::viewName() const
{
    return m_viewName;
}

void PaintOpenGLWidget::setActiveIndicator(bool active)
{
    if (m_activeIndicator == active) {
        return;
    }
    m_activeIndicator = active;
    update();
}

qreal PaintOpenGLWidget::zoom() const
{
    return m_zoom;
}

QPointF PaintOpenGLWidget::panOffset() const
{
    return m_panOffset;
}

void PaintOpenGLWidget::setScrollPosition(int horizontal, int vertical)
{
    if (m_playbackActive) {
        // Scrolling moves the view the cache was rendered for — same deal as
        // the wheel: leave playback first (the handler runs before the pan).
        emit playbackInterrupted();
    }
    m_panOffset = QPointF(-horizontal, -vertical);
    clampPan();
    update();
}

void PaintOpenGLWidget::setUnboundedCanvas(bool unbounded)
{
    if (m_unboundedCanvas == unbounded) {
        return;
    }
    m_unboundedCanvas = unbounded;
    clampPan();
    update();
    notifyViewTransformChanged();
}

bool PaintOpenGLWidget::unboundedCanvas() const
{
    return m_unboundedCanvas;
}

QPointF PaintOpenGLWidget::mapToDocument(const QPointF &screenPos) const
{
    return (screenPos - m_panOffset) / m_zoom;
}

void PaintOpenGLWidget::clampPan()
{
    if (m_unboundedCanvas) {
        return;
    }

    const qreal docWidth = width() * m_zoom;
    const qreal docHeight = height() * m_zoom;

    const qreal minX = std::min<qreal>(0.0, width() - docWidth);
    const qreal maxX = std::max<qreal>(0.0, width() - docWidth);
    const qreal minY = std::min<qreal>(0.0, height() - docHeight);
    const qreal maxY = std::max<qreal>(0.0, height() - docHeight);

    m_panOffset.setX(std::min(maxX, std::max(minX, m_panOffset.x())));
    m_panOffset.setY(std::min(maxY, std::max(minY, m_panOffset.y())));
}

void PaintOpenGLWidget::notifyViewTransformChanged()
{
    emit viewTransformChanged();
}

void PaintOpenGLWidget::wheelEvent(QWheelEvent *event)
{
    if (m_playbackActive) {
        // The cache was rendered at the current transform, so zooming has to
        // leave playback first (the handler runs before the zoom below).
        emit playbackInterrupted();
    }

    const qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    const qreal factor = std::pow(1.25, steps);
    const qreal newZoom = std::min<qreal>(8.0, std::max<qreal>(0.1, m_zoom * factor));
    if (qFuzzyCompare(newZoom, m_zoom)) {
        event->accept();
        return;
    }

    // Zoom around the cursor: keep the document point under it fixed.
    const QPointF anchor = event->position();
    const QPointF docAnchor = mapToDocument(anchor);
    m_zoom = newZoom;
    m_panOffset = anchor - docAnchor * m_zoom;
    clampPan();
    update();
    notifyViewTransformChanged();
    event->accept();
}

void PaintOpenGLWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    if (m_playbackActive) {
        // The cache was rendered for the old viewport size and pan; resizing
        // invalidates it, so leave playback rather than show a stale frame.
        emit playbackInterrupted();
    }
    clampPan();
    notifyViewTransformChanged();
}

void PaintOpenGLWidget::focusInEvent(QFocusEvent *event)
{
    // Qt hands focus to a click target BEFORE delivering the press. When that
    // click also ends playback (focusGained -> activate view -> stop), the
    // press must still be swallowed: by the time it arrives the playback flag
    // is already cleared, and without this it would draw into the frame the
    // user could not even see.
    if (m_playbackActive && event->reason() == Qt::MouseFocusReason) {
        m_swallowNextPress = true;
    }
    QOpenGLWidget::focusInEvent(event);
    emit focusGained();
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

void PaintOpenGLWidget::setStrokeProperty(const QString &property)
{
    m_strokeProperty = property;
    m_activePythonTool = property.isEmpty() ? QString() : QStringLiteral("extra");
    if (m_hasCurrentStroke) {
        m_currentStroke.property = property;
    }
}

void PaintOpenGLWidget::sendPythonExtraToolMessage(const QString &name, const QString &property)
{
    const QString previousTool = m_activePythonTool;
    const QString previousProperty = m_strokeProperty;
    m_activePythonTool = name.isEmpty() ? QStringLiteral("extra") : name;
    m_strokeProperty = property;
    pythonHookSendMessage(QStringLiteral("extra"));
    m_activePythonTool = previousTool;
    m_strokeProperty = previousProperty;
}

void PaintOpenGLWidget::sendPythonToolOptionMessage(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("option"))) {
        return;
    }
    const int frameRow = m_model.currentFrame();
    const int layer = m_model.currentLayer();
    const AnimeCell cell = m_model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict optionInfo;
    optionInfo["name"] = name.toStdString();
    optionInfo["type"] = type.toStdString();
    optionInfo["value"] = variantToPythonObject(value);
    optionInfo["hook"] = hook.toStdString();
    optionInfo["row"] = row;
    optionInfo["start_column"] = startColumn;
    optionInfo["end_column"] = endColumn;

    py::dict message;
    message["event"] = "option";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["option"] = optionInfo;
    message["name"] = name.toStdString();
    message["type"] = type.toStdString();
    message["value"] = variantToPythonObject(value);
    message["hook"] = hook.toStdString();
    message["row"] = row;
    message["start_column"] = startColumn;
    message["end_column"] = endColumn;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(hook);
    Q_UNUSED(name);
    Q_UNUSED(type);
    Q_UNUSED(value);
    Q_UNUSED(row);
    Q_UNUSED(startColumn);
    Q_UNUSED(endColumn);
#endif
}

void PaintOpenGLWidget::sendPythonPadMessage(const QString &pad, const QString &phase, double x, double y)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("pad"))) {
        return;
    }
    const int frameRow = m_model.currentFrame();
    const int layer = m_model.currentLayer();
    const AnimeCell cell = m_model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict message;
    message["event"] = "pad";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["pad"] = pad.toStdString();
    message["phase"] = phase.toStdString();
    message["value"] = pointToPythonDict(QPointF(x, y));

    const QString output = ::pythonHookSendMessage(message);
    // Pad moves stream continuously; quiet dispatches would flood the debug
    // dock with a line per mouse move.
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(pad);
    Q_UNUSED(phase);
    Q_UNUSED(x);
    Q_UNUSED(y);
#endif
}

bool PaintOpenGLWidget::sendPythonLayerVisibilityMessage(int layerIndex, bool visible)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("visibility"))) {
        return false;
    }

    const int frameRow = m_model.currentFrame();
    const AnimeCell cell = m_model.cellAt(frameRow, layerIndex);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layerIndex;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict message;
    message["event"] = "visibility";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["layer"] = layerIndex;
    message["visible"] = visible;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }

    try {
        if (message.contains(py::str("handled"))) {
            return message[py::str("handled")].cast<bool>();
        }
    } catch (const py::error_already_set &) {
    } catch (const py::cast_error &) {
    }
    return false;
#else
    Q_UNUSED(layerIndex);
    Q_UNUSED(visible);
    return false;
#endif
}

bool PaintOpenGLWidget::sendPythonFillRequestMessage(const QPointF &pos)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("fillrequest"))) {
        return false;
    }

    const int frameRow = m_model.currentFrame();
    const int layer = m_model.currentLayer();
    const AnimeCell cell = m_model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict colorInfo;
    colorInfo["r"] = m_penColor.red();
    colorInfo["g"] = m_penColor.green();
    colorInfo["b"] = m_penColor.blue();
    colorInfo["a"] = m_penColor.alpha();

    // Same canvas rect the built-in fill uses as its outer boundary.
    py::dict boundsInfo;
    boundsInfo["x"] = 0.0;
    boundsInfo["y"] = 0.0;
    boundsInfo["width"] = double(width());
    boundsInfo["height"] = double(height());

    py::dict message;
    message["event"] = "fillrequest";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(pos);
    message["delta"] = pointToPythonDict(QPointF());
    message["fill_scope"] = (m_fillScope == FillScope::AllLayers ? "all" : "current");
    message["color"] = colorInfo;
    message["bounds"] = boundsInfo;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }

    try {
        if (message.contains(py::str("handled"))) {
            return message[py::str("handled")].cast<bool>();
        }
    } catch (const py::error_already_set &) {
    } catch (const py::cast_error &) {
    }
    return false;
#else
    Q_UNUSED(pos);
    return false;
#endif
}

void PaintOpenGLWidget::setTool(Tool tool)
{
    m_tool = tool;
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    m_hasLastMovePos = false;
    update();
}

PaintOpenGLWidget::Tool PaintOpenGLWidget::tool() const
{
    return m_tool;
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

void PaintOpenGLWidget::setAxisSnapThreshold(qreal threshold)
{
    m_axisSnapThreshold = std::max<qreal>(1.0, threshold);
}

qreal PaintOpenGLWidget::axisSnapThreshold() const
{
    return m_axisSnapThreshold;
}

void PaintOpenGLWidget::resetAxisSnap(Qt::KeyboardModifiers modifiers, const QPointF &anchor)
{
    m_axisSnapAnchor = anchor;
    m_axisSnapAnchorIndex = m_points.isEmpty() ? 0 : int(m_points.size()) - 1;
    m_axisSnapState = (modifiers & (Qt::AltModifier | Qt::ShiftModifier))
                          ? AxisSnapState::Pending
                          : AxisSnapState::Inactive;
}

QPointF PaintOpenGLWidget::applyAxisSnap(Qt::KeyboardModifiers modifiers, const QPointF &point, bool *retroChanged)
{
    if (retroChanged) {
        *retroChanged = false;
    }

    if (!(modifiers & (Qt::AltModifier | Qt::ShiftModifier))) {
        m_axisSnapState = AxisSnapState::Inactive;
        return point;
    }

    if (m_axisSnapState == AxisSnapState::Inactive) {
        // Alt engaged mid-stroke: constrain from the latest drawn point on.
        resetAxisSnap(modifiers, m_points.isEmpty() ? point : m_points.last());
    }

    if (m_axisSnapState == AxisSnapState::Pending) {
        const qreal dx = std::abs(point.x() - m_axisSnapAnchor.x());
        const qreal dy = std::abs(point.y() - m_axisSnapAnchor.y());
        if (std::max(dx, dy) < m_axisSnapThreshold) {
            return point;
        }
        m_axisSnapState = dx >= dy ? AxisSnapState::Horizontal : AxisSnapState::Vertical;
        // Flatten the jitter collected while the direction was still ambiguous.
        for (int i = m_axisSnapAnchorIndex + 1; i < m_points.size(); ++i) {
            if (m_axisSnapState == AxisSnapState::Horizontal) {
                m_points[i].setY(m_axisSnapAnchor.y());
            } else {
                m_points[i].setX(m_axisSnapAnchor.x());
            }
            if (retroChanged) {
                *retroChanged = true;
            }
        }
    }

    if (m_axisSnapState == AxisSnapState::Horizontal) {
        return QPointF(point.x(), m_axisSnapAnchor.y());
    }
    if (m_axisSnapState == AxisSnapState::Vertical) {
        return QPointF(m_axisSnapAnchor.x(), point.y());
    }
    return point;
}

void PaintOpenGLWidget::setCurrentLayer(int layerIndex)
{
    m_model.setCurrentLayer(layerIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    m_hasLastMovePos = false;
    update();
}

void PaintOpenGLWidget::setCurrentFrame(int frameIndex)
{
    m_model.setCurrentFrame(frameIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    m_hasLastMovePos = false;
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
        commitHistory(QStringLiteral("Import Raster"));
        update();
    }
    return columnIndex;
}

int PaintOpenGLWidget::importVectorLineLayer(const QVector<ImportedVectorFrame> &frames, const QString &layerName)
{
    if (frames.isEmpty()) {
        return -1;
    }

    const int assetIndex = m_model.addAsset(AnimeColumnType::Vector, layerName);
    if (assetIndex < 0) {
        return -1;
    }

    int firstRow = -1;
    for (const ImportedVectorFrame &frame : frames) {
        if (frame.strokes.isEmpty()) {
            continue;
        }
        const int row = std::max(0, frame.row);
        const int frameId = row + 1;
        m_model.scene().xsheet.ensureFrameCount(row + 1);
        AnimeVectorImageModel *image = m_model.assetImage(assetIndex, frameId, true);
        if (!image) {
            continue;
        }
        for (const ImportedVectorStroke &importedStroke : frame.strokes) {
            if (importedStroke.path.isEmpty() || importedStroke.points.size() < 2) {
                continue;
            }
            const qreal width = std::max(qreal(1.0), importedStroke.width);
            image->addStroke(AnimeVectorLogic::makeStrokeFromPath(importedStroke.path,
                                                                  importedStroke.points,
                                                                  importedStroke.color,
                                                                  width,
                                                                  image->strokeCount() + 1));
        }
        if (firstRow < 0) {
            firstRow = row;
        }
    }

    if (firstRow < 0) {
        return -1;
    }

    const int layerIndex = m_model.addLayerForAsset(firstRow, assetIndex);
    if (layerIndex < 0) {
        return -1;
    }

    AnimeColumn &column = m_model.scene().xsheet.columns[layerIndex];
    for (const ImportedVectorFrame &frame : frames) {
        if (frame.strokes.isEmpty()) {
            continue;
        }
        AnimeCell cell;
        cell.assetIndex = assetIndex;
        cell.frameId = std::max(0, frame.row) + 1;
        column.setCell(std::max(0, frame.row), cell);
    }

    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    commitHistory(QStringLiteral("Import Vector Lines"));
    emit assetListChanged(assetIndex);
    emit layerListChanged(layerIndex);
    update();
    return layerIndex;
}

int PaintOpenGLWidget::addLayer()
{
    const int columnIndex = m_model.addLayer();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    if (columnIndex >= 0) {
        commitHistory(QStringLiteral("Add Layer"));
    }
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
    commitHistory(QStringLiteral("Delete Layer"));
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
    commitHistory(QStringLiteral("Move Layer"));
    update();
    return true;
}

int PaintOpenGLWidget::addFrame()
{
    const int row = m_model.addFrame();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    if (row >= 0) {
        commitHistory(QStringLiteral("Add Frame"));
    }
    update();
    return row;
}

bool PaintOpenGLWidget::deleteFrame(int frameIndex)
{
    if (!m_model.deleteFrame(frameIndex)) {
        return false;
    }

    commitHistory(QStringLiteral("Delete Frame"));
    update();
    return true;
}

bool PaintOpenGLWidget::moveFrame(int fromIndex, int toIndex)
{
    if (!m_model.moveFrame(fromIndex, toIndex)) {
        return false;
    }

    commitHistory(QStringLiteral("Move Frame"));
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
        commitHistory(QStringLiteral("Add Asset"));
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
        commitHistory(QStringLiteral("Assign Asset"));
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
        commitHistory(QStringLiteral("Add Layer for Asset"));
        update();
    }
    return layerIndex;
}

const AnimeSceneModel &PaintOpenGLWidget::model() const
{
    return m_model;
}

void PaintOpenGLWidget::setOverlayItems(const QVector<OverlayItem> &items)
{
    m_overlayItems = items;
    update();
}

void PaintOpenGLWidget::paintSceneContent(QPainter &painter, int frameIndex, bool includeCurrentStroke)
{
    const AnimeScene &scene = m_model.scene();
    const auto paintColumn = [&](int columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (!column.visible) {
            return;
        }

        const AnimeCell cell = column.cellAt(frameIndex);
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

        if (includeCurrentStroke && columnIndex == m_model.currentLayer() && m_hasCurrentStroke) {
            painter.setPen(QPen(m_currentStroke.color, m_currentStroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(m_currentStroke.path);
        }
    };

    for (int columnIndex = scene.xsheet.columns.size() - 1; columnIndex >= 0; --columnIndex) {
        if (scene.xsheet.columns[columnIndex].internal) {
            continue;
        }
        paintColumn(columnIndex);
    }
    // Script-owned working layers (tool previews) always draw on top of the
    // regular columns, wherever they sit in the column list.
    for (int columnIndex = scene.xsheet.columns.size() - 1; columnIndex >= 0; --columnIndex) {
        if (!scene.xsheet.columns[columnIndex].internal) {
            continue;
        }
        paintColumn(columnIndex);
    }
    painter.setOpacity(1.0);
}

bool PaintOpenGLWidget::buildPlaybackCache(int frameCount, QString *error)
{
    m_playbackFrames.clear();
    m_playbackIndex = -1;
    m_playbackActive = false;

    if (frameCount <= 0 || width() <= 0 || height() <= 0) {
        if (error) {
            *error = QStringLiteral("nothing to prerender");
        }
        return false;
    }

    const qreal ratio = devicePixelRatioF();
    const QSize pixelSize(std::max(1, int(std::lround(width() * ratio))),
                          std::max(1, int(std::lround(height() * ratio))));
    const qint64 bytesPerFrame = qint64(pixelSize.width()) * pixelSize.height() * 4;
    const qint64 budget = 512LL * 1024 * 1024;
    if (bytesPerFrame * frameCount > budget) {
        if (error) {
            *error = QStringLiteral("prerendering %1 frames at %2x%3 needs %4 MB (limit %5 MB)")
                         .arg(frameCount)
                         .arg(pixelSize.width())
                         .arg(pixelSize.height())
                         .arg(bytesPerFrame * frameCount / (1024 * 1024))
                         .arg(budget / (1024 * 1024));
        }
        return false;
    }

    m_playbackFrames.reserve(frameCount);
    for (int frame = 0; frame < frameCount; ++frame) {
        QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(m_unboundedCanvas ? Qt::white : QColor(72, 72, 72));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(m_panOffset);
        painter.scale(m_zoom, m_zoom);
        if (!m_unboundedCanvas) {
            painter.fillRect(rect(), Qt::white);
        }
        // Overlays and the in-progress stroke are editing aids, not content.
        paintSceneContent(painter, frame, false);
        painter.end();

        m_playbackFrames.append(image);
    }

    m_playbackActive = true;
    return true;
}

void PaintOpenGLWidget::showPlaybackFrame(int index)
{
    if (!m_playbackActive || index < 0 || index >= m_playbackFrames.size()) {
        return;
    }
    m_playbackIndex = index;
    update();
}

void PaintOpenGLWidget::endPlayback()
{
    m_playbackFrames.clear();
    m_playbackIndex = -1;
    m_playbackActive = false;
    update();
}

bool PaintOpenGLWidget::playbackActive() const
{
    return m_playbackActive;
}

void PaintOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_playbackActive && m_playbackIndex >= 0 && m_playbackIndex < m_playbackFrames.size()) {
        // Always clear first: the widget can be larger than the cached frame
        // (a resize ends playback, but the repaint may arrive before that),
        // and uncovered pixels of a recreated FBO are undefined.
        painter.fillRect(rect(), m_unboundedCanvas ? Qt::white : QColor(72, 72, 72));
        painter.drawImage(QPointF(0.0, 0.0), m_playbackFrames[m_playbackIndex]);
        if (m_activeIndicator) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 120, 255), 3.0));
            painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        return;
    }

    if (m_unboundedCanvas) {
        // Infinite reference board: the whole viewport is paper.
        painter.fillRect(rect(), Qt::white);
    } else {
        painter.fillRect(rect(), QColor(72, 72, 72));
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_zoom, m_zoom);
    if (!m_unboundedCanvas) {
        painter.fillRect(rect(), Qt::white);
    }

    paintSceneContent(painter, m_model.currentFrame(), true);

    paintOverlayItems(painter);

    if ((m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) && m_hasHoverPos) {
        painter.setPen(QPen(QColor(220, 0, 180), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_hoverPos, m_eraserRadius, m_eraserRadius);
    }

    painter.restore();

    // The active-view indicator hugs the viewport, not the document.
    if (m_activeIndicator) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 120, 255), 3.0));
        painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }
}

void PaintOpenGLWidget::paintOverlayItems(QPainter &painter)
{
    m_overlayHandles.clear();

    for (const OverlayItem &item : m_overlayItems) {
        if (item.points.size() < 2) {
            continue;
        }

        QPainterPath path(item.points.first());
        for (int i = 1; i < item.points.size(); ++i) {
            path.lineTo(item.points[i]);
        }
        if (item.closed) {
            path.closeSubpath();
            painter.setBrush(item.fillColor);
        } else {
            painter.setBrush(Qt::NoBrush);
        }
        painter.setPen(QPen(item.strokeColor, item.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);

        if (item.removable) {
            OverlayHandle handle;
            handle.id = item.id;
            handle.badgeColor = item.strokeColor;
            handle.rect = overlayHandleRect(path.boundingRect());
            m_overlayHandles.append(handle);
        }
    }

    for (const OverlayHandle &handle : m_overlayHandles) {
        QColor badge = handle.badgeColor;
        badge.setAlpha(215);
        painter.setPen(Qt::NoPen);
        painter.setBrush(badge);
        painter.drawRoundedRect(handle.rect, 3.0, 3.0);

        const QRectF inner = handle.rect.adjusted(4.0, 4.0, -4.0, -4.0);
        painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(inner.topLeft(), inner.bottomRight());
        painter.drawLine(inner.topRight(), inner.bottomLeft());
    }
}

QRectF PaintOpenGLWidget::overlayHandleRect(const QRectF &bounds) const
{
    QRectF handleRect(bounds.right() + 4.0,
                      bounds.top() - kOverlayHandleSize - 4.0,
                      kOverlayHandleSize,
                      kOverlayHandleSize);
    // The rect is built, drawn and hit-tested in DOCUMENT coordinates, so it
    // must be clamped against the visible viewport expressed in document
    // space (at zoom 1 on a bounded view this equals the old widget-pixel
    // clamp). Off-view items keep a reachable badge at the viewport edge.
    const QPointF docTopLeft = mapToDocument(QPointF(0.0, 0.0));
    const QPointF docBottomRight = mapToDocument(QPointF(width(), height()));
    if (handleRect.right() > docBottomRight.x() - 2.0) {
        handleRect.moveRight(docBottomRight.x() - 2.0);
    }
    if (handleRect.left() < docTopLeft.x() + 2.0) {
        handleRect.moveLeft(docTopLeft.x() + 2.0);
    }
    if (handleRect.top() < docTopLeft.y() + 2.0) {
        handleRect.moveTop(docTopLeft.y() + 2.0);
    }
    if (handleRect.bottom() > docBottomRight.y() - 2.0) {
        handleRect.moveBottom(docBottomRight.y() - 2.0);
    }
    return handleRect;
}

bool PaintOpenGLWidget::removeOverlayItemAt(const QPointF &pos)
{
    for (int i = m_overlayHandles.size() - 1; i >= 0; --i) {
        const OverlayHandle handle = m_overlayHandles[i];
        if (!handle.rect.contains(pos)) {
            continue;
        }

        sendOverlayRemoveMessage(handle.id);
        return true;
    }
    return false;
}

void PaintOpenGLWidget::sendOverlayRemoveMessage(const QString &overlayId)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("overlayremove"))) {
        return;
    }
    const int row = m_model.currentFrame();
    const int layer = m_model.currentLayer();
    const AnimeCell cell = m_model.cellAt(row, layer);
    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = row;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict overlayInfo;
    overlayInfo["id"] = overlayId.toStdString();

    py::dict message;
    message["event"] = "overlayremove";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["overlay"] = overlayInfo;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(overlayId);
#endif
}

void PaintOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_playbackActive || m_swallowNextPress) {
        // While prerendered frames are on screen a click means "stop
        // playing", not "draw into the frame you cannot currently see".
        m_swallowNextPress = false;
        if (m_playbackActive) {
            emit playbackInterrupted();
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const QPointF pos = mapToDocument(event->position());
    m_hoverPos = pos;
    m_hasHoverPos = true;

    if (removeOverlayItemAt(pos)) {
        event->accept();
        return;
    }

    // Arrow: pure focus/selection tool — clicking activates the view (via
    // ClickFocus) and must not touch the model.
    if (m_tool == Tool::Arrow) {
        event->accept();
        return;
    }

    if (m_tool == Tool::Fill) {
        // Fill policy lives in Python (pyfile/fill_tool.py): where the region
        // lands, scope upgrades and dedup are decided there. The built-in
        // fillAt below is the fallback for builds without Python hooks.
        if (sendPythonFillRequestMessage(pos)) {
            update();
            event->accept();
            return;
        }
        if (fillAt(pos)) {
            const bool cancelHistory = pythonHookSendMessage(QStringLiteral("fillfinish"), pos);
            if (!cancelHistory) {
                commitHistory(QStringLiteral("Fill"));
            }
        }
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::Move) {
        m_lastMovePos = pos;
        m_hasLastMovePos = true;
        event->accept();
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        m_hasLastEraserPos = true;
        m_lastEraserPos = pos;
        if (m_tool == Tool::DeleteLine) {
            m_eraseGestureChanged = deleteLineAt(pos);
        } else {
            m_eraseGestureChanged = eraseAt(pos);
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
    resetAxisSnap(event->modifiers(), pos);
    // Each stroke gets a fresh throttle window so its first "update" is
    // never swallowed by the previous stroke's timestamp.
    m_updateHookThrottle.invalidate();
    m_currentStroke = makeStroke(m_points, m_penColor, m_penWidth);
    m_currentStroke.property = m_strokeProperty;
    m_hasCurrentStroke = true;
    update();
    event->accept();
}

void PaintOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning && (event->buttons() & Qt::MiddleButton)) {
        m_panOffset += event->position() - m_lastPanPos;
        m_lastPanPos = event->position();
        clampPan();
        update();
        notifyViewTransformChanged();
        event->accept();
        return;
    }

    m_hoverPos = mapToDocument(event->position());
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
            m_eraseGestureChanged = deleteLineBetween(m_lastEraserPos, m_hoverPos) || m_eraseGestureChanged;
        } else {
            m_eraseGestureChanged = eraseBetween(m_lastEraserPos, m_hoverPos) || m_eraseGestureChanged;
        }

        m_lastEraserPos = m_hoverPos;
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::Move) {
        if (!m_hasLastMovePos) {
            m_lastMovePos = m_hoverPos;
            m_hasLastMovePos = true;
        }
        if (moveCurrentLayerBy(m_hoverPos - m_lastMovePos)) {
            m_moveGestureChanged = true;
            update();
        }
        m_lastMovePos = m_hoverPos;
        event->accept();
        return;
    }

    if (!m_hasCurrentStroke) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    bool axisRetroChanged = false;
    const QPointF snappedPos = applyAxisSnap(event->modifiers(), m_hoverPos, &axisRetroChanged);
    if (appendPoint(snappedPos) || axisRetroChanged) {
        updateCurrentStroke();
    }

    event->accept();
}

void PaintOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        m_hoverPos = mapToDocument(event->position());
        m_hasHoverPos = true;
        bool cancelHistory = false;
        if (m_tool == Tool::DeleteLine) {
            m_eraseGestureChanged = deleteLineAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(QStringLiteral("deletefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        } else {
            m_eraseGestureChanged = eraseAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(QStringLiteral("erasefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        }
        if (m_eraseGestureChanged && !cancelHistory) {
            commitHistory(m_tool == Tool::DeleteLine ? QStringLiteral("Delete Line")
                                                     : QStringLiteral("Erase"));
        }
        update();
        m_hasLastEraserPos = false;
        m_eraseGestureChanged = false;
        event->accept();
        return;
    }

    if (m_tool == Tool::Move) {
        bool cancelHistory = false;
        if (m_hasLastMovePos) {
            const QPointF pos = mapToDocument(event->position());
            const QPointF delta = pos - m_lastMovePos;
            m_moveGestureChanged = moveCurrentLayerBy(delta) || m_moveGestureChanged;
            m_lastMovePos = pos;
            update();
            cancelHistory = pythonHookSendMessage(QStringLiteral("movefinish"), pos, delta, m_moveGestureChanged);
        }
        if (m_moveGestureChanged && !cancelHistory) {
            commitHistory(QStringLiteral("Move"));
        }
        m_hasLastMovePos = false;
        m_moveGestureChanged = false;
        event->accept();
        return;
    }

    if (!m_hasCurrentStroke) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    // The release point must follow the axis lock that governed the last move,
    // NOT the live modifier state: releasing Alt just before the button (with
    // no move in between) would otherwise append the raw cursor position and
    // weld a perpendicular tail onto a line whose preview ended on-axis.
    QPointF releasePoint = mapToDocument(event->position());
    if (m_axisSnapState == AxisSnapState::Horizontal) {
        releasePoint.setY(m_axisSnapAnchor.y());
    } else if (m_axisSnapState == AxisSnapState::Vertical) {
        releasePoint.setX(m_axisSnapAnchor.x());
    } else {
        releasePoint = applyAxisSnap(event->modifiers(), releasePoint, nullptr);
    }
    appendPoint(releasePoint);
    m_axisSnapState = AxisSnapState::Inactive;
    finishCurrentStroke();
    event->accept();
}

void PaintOpenGLWidget::updateCurrentStroke()
{
    if (!m_hasCurrentStroke) {
        return;
    }

    m_currentStroke = makeStroke(m_points, m_currentStroke.color, m_currentStroke.width);
    m_currentStroke.property = m_strokeProperty;
    // "update" is a best-effort preview notification, throttled so a
    // subscriber never runs at tablet sample rate; "linefinish" remains the
    // only guaranteed drawing event.
    if (!m_updateHookThrottle.isValid() || m_updateHookThrottle.elapsed() >= kUpdateHookIntervalMs) {
        m_updateHookThrottle.start();
        pythonHookSendMessage(QStringLiteral("update"));
    }
    update();
}

void PaintOpenGLWidget::finishCurrentStroke()
{
    updateCurrentStroke();
    if (!m_currentStroke.points.isEmpty()) {
        const int assetCountBefore = m_model.assetCount();
        if (VectorImageModel *image = currentImage(true, AnimeColumnType::Vector)) {
            const int strokeIndex = image->strokeCount();
            m_currentStroke.property = m_strokeProperty;
            image->addStroke(m_currentStroke);
            const bool cancelHistory =
                pythonHookSendMessage(QStringLiteral("linefinish"), QPointF(), QPointF(), true, strokeIndex);
            removeInvalidFillRegions();
            if (m_model.assetCount() != assetCountBefore) {
                emit assetListChanged(m_model.currentAsset());
                emit layerListChanged(m_model.currentLayer());
            }
            // Committed after the linefinish hooks so tool scripts that adjust
            // the model (e.g. guide capture) fold into the same history entry;
            // hooks that undid the stroke entirely veto the commit instead.
            if (!cancelHistory) {
                commitHistory(m_strokeProperty.isEmpty() ? QStringLiteral("Stroke") : m_strokeProperty);
            }
        }
    }
    m_hasCurrentStroke = false;
    m_points.clear();
    update();
}

bool PaintOpenGLWidget::pythonHookSendMessage(const QString &event, const QPointF &pos, const QPointF &delta, bool changed, int strokeIndex)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!changed) {
        return false;
    }
    if (!animeanHookEventSubscribed(event)) {
        return false;
    }

    const int row = m_model.currentFrame();
    const int layer = m_model.currentLayer();
    const AnimeCell cell = m_model.cellAt(row, layer);
    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = row;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict message;
    message["event"] = event.toStdString();
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    if (m_hasCurrentStroke || event == QStringLiteral("linefinish")) {
        message["stroke"] = strokeToPythonDict(m_currentStroke, strokeIndex);
    } else {
        message["stroke"] = py::dict();
    }
    message["position"] = pointToPythonDict(pos);
    message["delta"] = pointToPythonDict(delta);

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }

    // Hooks may veto the caller's follow-up history commit (e.g. a tool click
    // that turned out to be a no-op) by setting message["cancel_history"].
    try {
        if (message.contains(py::str("cancel_history"))) {
            return message[py::str("cancel_history")].cast<bool>();
        }
    } catch (const py::error_already_set &) {
    } catch (const py::cast_error &) {
    }
    return false;
#else
    Q_UNUSED(event);
    Q_UNUSED(pos);
    Q_UNUSED(delta);
    Q_UNUSED(changed);
    Q_UNUSED(strokeIndex);
    return false;
#endif
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
        if (AnimeVectorLogic::strokeHitsCircle(stroke, pos, m_eraserRadius)) {
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
        if (AnimeVectorLogic::strokeHitsCapsule(stroke, from, to, m_eraserRadius)) {
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
        const bool sameProperty = existing.property == m_strokeProperty;
        if (!sameReferMode || !sameSourceLayer || !sameProperty || !existing.path.contains(pos)) {
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
    fill.property = m_strokeProperty;
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

bool PaintOpenGLWidget::moveCurrentLayerBy(const QPointF &delta)
{
    if (delta.isNull() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    VectorImageModel *image = currentImage(false);
    if (!image) {
        return false;
    }

    image->translate(delta);
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
    const bool allLayers = scope == FillScope::AllLayers;
    return m_model.fillBoundarySegments(m_model.currentFrame(), allLayers ? -1 : layerIndex);
}

QPainterPath PaintOpenGLWidget::vectorRegionPathAt(const QPointF &seed, FillScope scope, int layerIndex) const
{
    return AnimeVectorLogic::vectorRegionPathAt(seed, fillGraphSegments(scope, layerIndex), rect());
}

void PaintOpenGLWidget::removeInvalidFillRegions()
{
    AnimeScene &scene = m_model.scene();

    for (AnimeAsset &asset : scene.assets) {
        for (int frameId : asset.frameIds()) {
            VectorImageModel *image = asset.frame(frameId, false);
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

    const QVector<AnimeVectorRange> keepRanges = AnimeVectorLogic::keepRangesForCircle(stroke, pos, m_eraserRadius);
    const qreal epsilon = AnimeVectorLogic::epsilon();
    if (keepRanges.size() == 1 && keepRanges.first().first <= epsilon &&
        keepRanges.first().second >= 1.0 - epsilon) {
        return false;
    }

    QVector<VectorStroke> pieces;
    for (const AnimeVectorRange &range : keepRanges) {
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_smoothValue));
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

    const QVector<AnimeVectorRange> keepRanges = AnimeVectorLogic::keepRangesForCapsule(stroke, from, to, m_eraserRadius);
    const qreal epsilon = AnimeVectorLogic::epsilon();
    if (keepRanges.size() == 1 && keepRanges.first().first <= epsilon &&
        keepRanges.first().second >= 1.0 - epsilon) {
        return false;
    }

    QVector<VectorStroke> pieces;
    for (const AnimeVectorRange &range : keepRanges) {
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_smoothValue));
    }
    image->replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

PaintOpenGLWidget::VectorStroke PaintOpenGLWidget::makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id, bool filterInput, bool smoothPath) const
{
    return AnimeVectorLogic::makeStroke(points, color, width, id, filterInput, smoothPath, m_smoothValue);
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
