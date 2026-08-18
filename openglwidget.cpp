#include "openglwidget.h"

#include <QLineF>
#include <QGuiApplication>
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

#include <QTimer>

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
    case PaintOpenGLWidget::Tool::CutLine:
        return QStringLiteral("cut_line");
    case PaintOpenGLWidget::Tool::Fill:
        return QStringLiteral("fill");
    case PaintOpenGLWidget::Tool::Move:
        return QStringLiteral("move");
    case PaintOpenGLWidget::Tool::Arrow:
        return QStringLiteral("arrow");
    case PaintOpenGLWidget::Tool::Connect:
        return QStringLiteral("connect");
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
        // Defaulted lookups: py::dict has no dict-specific operator[], so a
        // missing key raises KeyError and would throw all the way out to the
        // catch below - discarding the hook's real output and reporting a
        // bogus "hook setup error" instead. The formatter must never be the
        // thing that fails after the hook already ran.
        const auto text = [&message](const char *key, const char *fallback) {
            if (!message.contains(py::str(key))) {
                return QString::fromUtf8(fallback);
            }
            return QString::fromUtf8(py::str(message[key]).cast<std::string>().c_str());
        };
        const QString event = text("event", "?");
        const QString tool = text("tool", "-");
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
        // Same as the wheel: the frame is re-mapped onto the new view and the
        // cache catches up once scrolling stops.
        schedulePlaybackCacheRefresh();
    }
    m_panOffset = QPointF(-horizontal, -vertical);
    clampPan();
    update();
}

void PaintOpenGLWidget::paintBackground(QPainter &painter, const QRectF &target) const
{
    switch (m_backgroundMode) {
    case BackgroundMode::Black:
        painter.fillRect(target, Qt::black);
        return;
    case BackgroundMode::Transparent: {
        // The usual grey/white chequer. Drawn in SCREEN space (this runs
        // before the zoom transform), so the squares stay a constant size on
        // screen the way every other application's transparency chequer does
        // - a chequer that zoomed with the drawing would read as content.
        constexpr int kSquare = 8;
        painter.save();
        painter.fillRect(target, QColor(255, 255, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(204, 204, 204));
        const int left = int(std::floor(target.left()));
        const int top = int(std::floor(target.top()));
        const int right = int(std::ceil(target.right()));
        const int bottom = int(std::ceil(target.bottom()));
        for (int y = top - (top % kSquare) - kSquare; y < bottom; y += kSquare) {
            for (int x = left - (left % kSquare) - kSquare; x < right; x += kSquare) {
                if (((x / kSquare) + (y / kSquare)) % 2 == 0) {
                    painter.drawRect(QRect(x, y, kSquare, kSquare));
                }
            }
        }
        painter.restore();
        return;
    }
    case BackgroundMode::White:
    default:
        painter.fillRect(target, Qt::white);
        return;
    }
}

void PaintOpenGLWidget::setBackgroundMode(BackgroundMode mode)
{
    if (m_backgroundMode == mode) {
        return;
    }
    m_backgroundMode = mode;
    update();
}

PaintOpenGLWidget::BackgroundMode PaintOpenGLWidget::backgroundMode() const
{
    return m_backgroundMode;
}

void PaintOpenGLWidget::setContentEditable(bool editable)
{
    m_contentEditable = editable;
}

bool PaintOpenGLWidget::contentEditable() const
{
    return m_contentEditable;
}

void PaintOpenGLWidget::setEditablePropertyFilter(const QStringList &properties)
{
    m_editableProperties = properties;
}

bool PaintOpenGLWidget::editingAllowed() const
{
    if (m_contentEditable) {
        return true;
    }
    // Locked: only the tools the allowlist names may still draw. An empty
    // allowlist locks everything, which is the honest reading of "no tool is
    // allowed" - failing OPEN here would make the protection silently
    // useless exactly when Python failed to register.
    return m_editableProperties.contains(m_strokeProperty);
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
    // The view transform lives in algorithm/viewscale.h - THE shared home of
    // screen<->canvas conversion. Do not re-derive it here or anywhere else.
    return AnimeViewScale::toDocument(screenPos, m_zoom, m_panOffset);
}

QRectF PaintOpenGLWidget::documentRect() const
{
    // The page comes from the DOCUMENT. It used to be the widget rect, which
    // made the paper resize with the window - the drawing area, the export
    // and every bounds-driven algorithm silently changed shape when the user
    // dragged a dock.
    const QSize canvas = m_model.canvasSize();
    return QRectF(0.0, 0.0, canvas.width(), canvas.height());
}

QRectF PaintOpenGLWidget::fillBoundsRect() const
{
    // What a bucket fill is clipped against, in DOCUMENT space - and it must
    // come from the ARTWORK, never from the viewport.
    //
    // This rect is only ever a CLIP, never the wall it reads as. The tracer's
    // wall is the ring of ink itself: computeVectorRegionFaces keeps only
    // faces of positive signed area, so an open shape, or a click outside all
    // ink, comes back empty however large this rect is. Taking it from the
    // window therefore could not help a fill succeed - it could only cut one
    // short. On the reference board it was literally the viewport, so the same
    // click on the same drawing stored a region four times smaller at 2x zoom,
    // and panning first froze a window-shaped rectangle of colour into the
    // file. On the page-bounded board the page cut off anything drawn past its
    // edge, which the artist can see and edit but could not fill.
    //
    // Unioned over ALL boundary layers regardless of the caller's scope: a
    // superset of whatever fillAt or fill_tool finally traces, so the seed
    // gate below can never refuse a click the tracer would have served.
    QRectF bounds = m_model.fillBoundaryBounds(m_model.currentFrame(), -1);
    if (!m_unboundedCanvas) {
        // The page is a property of the document, not of the view, so it stays
        // in the clip - this can only ever GROW what the bounded board allowed.
        bounds = bounds.isNull() ? documentRect() : bounds.united(documentRect());
    }
    if (bounds.isNull()) {
        return QRectF();   // no walls on this frame: there is nothing to fill
    }
    // Leave room for the halo that tucks a region under its own outline;
    // clipping tight to the ink would shave it back off.
    const qreal pad = AnimeVectorLogic::kVectorRegionOverpaintWidth;
    return bounds.adjusted(-pad, -pad, pad, pad);
}

void PaintOpenGLWidget::modelReplaced()
{
    // Loading a project and restoring a history snapshot both copy-assign the
    // whole model, and the page size rides along inside it. Without this the
    // view kept the OLD page: the pan stayed where the previous document put
    // it (possibly off the new one entirely) and the scroll bars kept the
    // previous range, since only a viewTransformChanged makes them resync.
    clampPan();
    update();
    notifyViewTransformChanged();
}

void PaintOpenGLWidget::setCanvasSize(const QSize &size)
{
    if (m_model.canvasSize() == size) {
        return;
    }
    m_model.setCanvasSize(size);
    clampPan();
    update();
    notifyViewTransformChanged();
}

void PaintOpenGLWidget::clampPan()
{
    if (m_unboundedCanvas) {
        return;
    }

    const qreal docWidth = documentRect().width() * m_zoom;
    const qreal docHeight = documentRect().height() * m_zoom;

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
    // Zooming no longer leaves playback. paintGL maps the cached frames from
    // the view they were rendered for onto the current one, so the animation
    // keeps running through the gesture; the cache is re-rendered once the
    // wheel stops, which is what makes it sharp again (and fills in anything
    // the old viewport did not cover).
    if (m_playbackActive) {
        schedulePlaybackCacheRefresh();
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
    // Solve toScreen(docAnchor) == anchor for the pan (algorithm/viewscale.h).
    m_panOffset = anchor - AnimeViewScale::toScreen(docAnchor, m_zoom, QPointF());
    clampPan();
    update();
    notifyViewTransformChanged();
    // The artist-mode key-point filter is zoom-dependent (a perceptual
    // threshold in SCREEN space), so a zoom while handles are up re-asks
    // Python which handles survive at the new magnification. Connect's
    // button offsets are screen-sized too, so its handles re-place as well.
    if ((m_tool == Tool::Arrow || m_tool == Tool::Connect)
        && !m_editHandles.isEmpty() && m_activeHandleDrag.isEmpty()) {
        sendPythonHandleMessage(QStringLiteral("view"), QString(), docAnchor);
    }
    event->accept();
}

void PaintOpenGLWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    if (m_playbackActive) {
        // A resize does not move the view, so the mapped blit still lands
        // correctly; it only means the cache covers less than the new viewport.
        // Re-rendering once the resize settles fills that in - much better than
        // dropping the user out of playback for dragging a dock.
        schedulePlaybackCacheRefresh();
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

void PaintOpenGLWidget::sendPythonViewButtonMessage(const QString &name, bool on)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("viewbutton"))) {
        return;
    }

    py::gil_scoped_acquire acquire;
    py::dict message;
    message["event"] = "viewbutton";
    message["view"] = m_viewName.toStdString();
    // Every sender carries these: python_hooks._matches filters on "tool" and
    // "property", so omitting them would make tool=... subscriptions to this
    // event silently unmatchable.
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["name"] = name.toStdString();
    message["on"] = on;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(name);
    Q_UNUSED(on);
#endif
}

void PaintOpenGLWidget::sendPythonLayerMenuMessage(const QString &action,
                                                   int groupId,
                                                   const QString &groupName,
                                                   int layerIndex,
                                                   const QString &layerName,
                                                   const QVector<int> &memberLayers)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("layermenu"))) {
        return;
    }

    py::gil_scoped_acquire acquire;
    py::list members;
    for (int index : memberLayers) {
        members.append(index);
    }

    py::dict message;
    message["event"] = "layermenu";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["action"] = action.toStdString();
    message["group"] = groupId;
    message["group_name"] = groupName.toStdString();
    message["layer"] = layerIndex;
    message["layer_name"] = layerName.toStdString();
    message["members"] = members;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(action);
    Q_UNUSED(groupId);
    Q_UNUSED(groupName);
    Q_UNUSED(layerIndex);
    Q_UNUSED(layerName);
    Q_UNUSED(memberLayers);
#endif
}

void PaintOpenGLWidget::sendPythonMenuMessage(const QString &menu,
                                              const QString &item,
                                              bool checked)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("menu"))) {
        return;
    }

    py::gil_scoped_acquire acquire;
    py::dict message;
    message["event"] = "menu";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["menu"] = menu.toStdString();
    message["name"] = item.toStdString();
    message["checked"] = checked;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(menu);
    Q_UNUSED(item);
    Q_UNUSED(checked);
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

    // Same canvas rect the built-in fill uses as its outer boundary - the
    // PAGE, in document space, which is the space `position` below is in.
    const QRectF fillBounds = fillBoundsRect();
    py::dict boundsInfo;
    boundsInfo["x"] = fillBounds.x();
    boundsInfo["y"] = fillBounds.y();
    boundsInfo["width"] = fillBounds.width();
    boundsInfo["height"] = fillBounds.height();

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
    if ((m_tool == Tool::Arrow || m_tool == Tool::Connect) && tool != m_tool) {
        // Leaving a handle-owning tool (Arrow's edit points, Connect's snap
        // hints and its accept/delete buttons) dismisses its handles - and
        // TELLS Python, so anything it drew into the overlay goes with them
        // AND its per-view session dies. The cancel fires even when no
        // handles happen to be up right now: Connect keeps a focused
        // connection whose handles vanish whenever the hover leaves every
        // snap target, and skipping the cancel then left a live session
        // whose recorded stroke index later edited an innocent stroke.
        sendPythonHandleMessage(QStringLiteral("cancel"), QString(), QPointF());
        m_editHandles.clear();
        m_activeHandleDrag.clear();
    }
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

void PaintOpenGLWidget::setStrokeFitSettings(const AnimeStrokeFitSettings &settings)
{
    m_fitSettings.simplify = std::max(0, std::min(100, settings.simplify));
    m_fitSettings.corner = std::max(0, std::min(100, settings.corner));
}

AnimeStrokeFitSettings PaintOpenGLWidget::strokeFitSettings() const
{
    return m_fitSettings;
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
        // The threshold is how far the HAND must travel before the direction
        // is unambiguous, so it is SCREEN px, converted here through the
        // shared wheel (algorithm/viewscale.h). Compared raw against these
        // document deltas it used to mean 40 screen px of travel at 8x zoom
        // and half a pixel at 0.1x - the lock armed instantly when zoomed out
        // and felt stuck when zoomed in.
        if (std::max(dx, dy)
            < AnimeViewScale::toDocumentLength(m_axisSnapThreshold, m_zoom)) {
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

void PaintOpenGLWidget::armHoldStill(const QPointF &anchor)
{
    m_holdStillAnchor = anchor;
    if (!m_holdStillTimer) {
        m_holdStillTimer = new QTimer(this);
        m_holdStillTimer->setSingleShot(true);
        connect(m_holdStillTimer, &QTimer::timeout, this,
                [this]() { engageStraightLine(); });
    }
    // Restarted only when the pen LEAVES the circle. Restarting on every move
    // would mean a hand that trembles - which is every hand - could never
    // finish the wait, and trembling is precisely who this gesture is for.
    m_holdStillTimer->start(kHoldStillMs);
}

void PaintOpenGLWidget::engageStraightLine()
{
    // The wait can outlive the stroke: a tool change, a frame change or an
    // undo abandons it without stopping the timer.
    if (!m_hasCurrentStroke || m_straightLineMode || m_points.isEmpty()) {
        return;
    }
    m_straightLineMode = true;
    m_straightLineStart = m_points.first();
    // The incremental fitter's frozen prefix is a set of INDICES into
    // m_points; collapsing the buffer invalidates every one of them, so the
    // state is retired here. Straight mode never fits again - that is the
    // point of it - and the release path skips the fit too.
    m_liveFit = AnimeLiveFitState();
    // An axis lock anchored mid-curve would pin the free end to a stale axis.
    // The straight line carries its own lock, measured from its own start.
    m_axisSnapState = AxisSnapState::Inactive;
    // No stabilizer lag left to cover: the line ends exactly at the cursor.
    m_hasRawPenPos = false;
    updateStraightLine(straightLineTip(QGuiApplication::keyboardModifiers(),
                                       m_hasHoverPos ? m_hoverPos : m_points.last()));
}

QPointF PaintOpenGLWidget::straightLineTip(Qt::KeyboardModifiers modifiers,
                                           const QPointF &cursor)
{
    if (!(modifiers & (Qt::AltModifier | Qt::ShiftModifier))) {
        m_axisSnapState = AxisSnapState::Inactive;
        return cursor;
    }
    if (m_axisSnapState == AxisSnapState::Inactive) {
        // The lock is measured from the segment's OWN origin. The curved path
        // anchors at the last captured sample, but a straight line has no
        // point history - its two ends are the whole stroke.
        m_axisSnapAnchor = m_straightLineStart;
        m_axisSnapAnchorIndex = 0;
        m_axisSnapState = AxisSnapState::Pending;
    }
    if (m_axisSnapState == AxisSnapState::Pending) {
        const qreal dx = std::abs(cursor.x() - m_straightLineStart.x());
        const qreal dy = std::abs(cursor.y() - m_straightLineStart.y());
        // Same screen-px threshold and the same LATCH as applyAxisSnap: once
        // the direction is decided it is held until the modifier is released.
        // Re-deciding per event made the line flip 90 degrees at mouse rate
        // whenever the cursor wandered near the 45-degree diagonal.
        if (std::max(dx, dy)
            < AnimeViewScale::toDocumentLength(m_axisSnapThreshold, m_zoom)) {
            return cursor;
        }
        m_axisSnapState = dx >= dy ? AxisSnapState::Horizontal : AxisSnapState::Vertical;
    }
    if (m_axisSnapState == AxisSnapState::Horizontal) {
        return QPointF(cursor.x(), m_straightLineStart.y());
    }
    if (m_axisSnapState == AxisSnapState::Vertical) {
        return QPointF(m_straightLineStart.x(), cursor.y());
    }
    return cursor;
}

void PaintOpenGLWidget::updateStraightLine(QPointF tip)
{
    if (!m_straightLineMode) {
        return;
    }
    if (QLineF(m_straightLineStart, tip).length() <= 1e-9) {
        // Nothing to collapse to yet (held still from the very first sample).
        // Keeping the press's own dot avoids inventing a zero-length segment,
        // a shape no other code path in the app produces.
        return;
    }
    m_points.clear();
    m_points.append(m_straightLineStart);
    m_points.append(tip);
    QPainterPath path(m_straightLineStart);
    path.lineTo(tip);
    m_currentStroke = AnimeVectorLogic::makeStrokeFromPath(
        path, m_points, m_currentStroke.color, m_currentStroke.width, 0);
    m_currentStroke.property = m_strokeProperty;
    // Same throttled "update" contract the curved path honours, so a
    // subscriber never runs at mouse rate.
    if (!m_updateHookThrottle.isValid() || m_updateHookThrottle.elapsed() >= kUpdateHookIntervalMs) {
        m_updateHookThrottle.start();
        pythonHookSendMessage(QStringLiteral("update"));
    }
    update();
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

    // Centre on the PAGE. Centring on the widget put the image wherever the
    // window happened to be, so the same file landed somewhere else after a
    // dock was dragged.
    const QPointF canvasCenter = documentRect().center();
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

int PaintOpenGLWidget::deleteLayerGroup(int groupId)
{
    const int deleted = m_model.deleteLayerGroup(groupId);
    if (deleted <= 0) {
        return deleted;
    }

    removeInvalidFillRegions();
    // ONE history entry for the whole group: deleting a mapping run is a
    // single act to the user, and undoing it layer by layer would be tedious
    // and would leave half-restored states in the list.
    commitHistory(deleted == 1 ? QStringLiteral("Delete Group")
                               : QStringLiteral("Delete Group (%1 layers)").arg(deleted));
    update();
    return deleted;
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

int PaintOpenGLWidget::addHoldFrame()
{
    const int row = m_model.addHoldFrame();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    if (row >= 0) {
        commitHistory(QStringLiteral("Add Hold Frame"));
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

void PaintOpenGLWidget::setEditHandles(const QVector<EditHandle> &handles)
{
    m_editHandles = handles;
    // Python may legitimately clear or rebuild the set mid-drag (it re-pushes
    // after every edit); the drag keeps its id and simply stops matching a
    // drawn handle if the set no longer contains it.
    update();
}

namespace {
constexpr qreal kEditHandleScreenPx = 9.0;   // drawn size
constexpr qreal kEditHandleHitPx = 7.0;      // half-size of the hit box
constexpr qreal kEditHandleButtonScale = 1.8; // button shapes (3, 4) grow by this
}

void PaintOpenGLWidget::paintEditHandles(QPainter &painter)
{
    // SCREEN space: handles keep a constant size at any zoom, like every
    // vector editor's - a handle is a tool, not a mark on the paper.
    // Conversion via algorithm/viewscale.h, the shared wheel.
    for (const EditHandle &handle : m_editHandles) {
        const QPointF screen = AnimeViewScale::toScreen(handle.pos, m_zoom, m_panOffset);
        const qreal half = kEditHandleScreenPx * 0.5;
        painter.setPen(QPen(QColor(30, 30, 30, 230), 1.2));
        painter.setBrush(handle.color);
        switch (handle.shape) {
        case 1:
            painter.drawEllipse(screen, half, half);
            break;
        case 2: {
            const QPointF diamond[4] = {
                screen + QPointF(0.0, -half - 1.5), screen + QPointF(half + 1.5, 0.0),
                screen + QPointF(0.0, half + 1.5), screen + QPointF(-half - 1.5, 0.0)};
            painter.drawPolygon(diamond, 4);
            break;
        }
        // Shapes 3 and 4 are BUTTONS (accept / delete), drawn bigger than
        // the point handles so a click target reads as one, with a matching
        // bigger hit box in editHandleAt.
        case 3: {
            const qreal button = half * kEditHandleButtonScale;
            painter.drawEllipse(screen, button, button);
            painter.setPen(QPen(QColor(250, 250, 250, 245), 2.0));
            const QPointF check[3] = {screen + QPointF(-button * 0.5, 0.05 * button),
                                      screen + QPointF(-button * 0.12, button * 0.45),
                                      screen + QPointF(button * 0.55, -button * 0.45)};
            painter.drawPolyline(check, 3);
            break;
        }
        case 4: {
            const qreal button = half * kEditHandleButtonScale;
            painter.drawEllipse(screen, button, button);
            painter.setPen(QPen(QColor(250, 250, 250, 245), 2.0));
            const qreal arm = button * 0.5;
            painter.drawLine(screen + QPointF(-arm, -arm), screen + QPointF(arm, arm));
            painter.drawLine(screen + QPointF(-arm, arm), screen + QPointF(arm, -arm));
            break;
        }
        default:
            painter.drawRect(QRectF(screen.x() - half, screen.y() - half,
                                    kEditHandleScreenPx, kEditHandleScreenPx));
            break;
        }
    }
}

QString PaintOpenGLWidget::editHandleAt(const QPointF &screenPos) const
{
    // Last drawn is on top, so scan backwards.
    for (int i = m_editHandles.size() - 1; i >= 0; --i) {
        if (!m_editHandles[i].interactive) {
            continue;
        }
        // Same doc->screen mapping the painter used (algorithm/viewscale.h):
        // the pick box must sit exactly where the handle was drawn.
        const QPointF screen =
            AnimeViewScale::toScreen(m_editHandles[i].pos, m_zoom, m_panOffset);
        const qreal hit = m_editHandles[i].shape >= 3
                              ? kEditHandleHitPx * kEditHandleButtonScale
                              : kEditHandleHitPx;
        if (std::abs(screen.x() - screenPos.x()) <= hit
            && std::abs(screen.y() - screenPos.y()) <= hit) {
            return m_editHandles[i].id;
        }
    }
    return QString();
}

void PaintOpenGLWidget::sendPythonHandleMessage(const QString &phase, const QString &handleId, const QPointF &pos)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(QStringLiteral("handle"))) {
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
    message["event"] = "handle";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(pos);
    message["delta"] = pointToPythonDict(QPointF());
    message["phase"] = phase.toStdString();
    message["handle"] = handleId.toStdString();
    // The perceptual key-point filter is zoom-dependent (the eye resolves a
    // fixed angular period, which maps through the zoom to document space).
    message["zoom"] = m_zoom;

    const QString output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(phase);
    Q_UNUSED(handleId);
    Q_UNUSED(pos);
#endif
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
                // penStyle is a generic per-stroke property (Qt::PenStyle).
                // Clamp out-of-range values to solid: 0 is NoPen, which would
                // silently make the stroke invisible.
                const Qt::PenStyle penStyle =
                    (stroke.penStyle >= Qt::SolidLine && stroke.penStyle <= Qt::DashDotDotLine)
                        ? static_cast<Qt::PenStyle>(stroke.penStyle)
                        : Qt::SolidLine;
                // Floored on-screen width: zoomed out, the raw width fades
                // into an invisible sub-pixel smear. Display only.
                QPen pen(stroke.color,
                         AnimeVectorLogic::displayStrokeWidth(stroke.width, m_zoom),
                         penStyle, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(pen);
                painter.drawPath(stroke.path);
            }
        }

        if (includeCurrentStroke && columnIndex == m_model.currentLayer() && m_hasCurrentStroke) {
            painter.setPen(QPen(m_currentStroke.color,
                                AnimeVectorLogic::displayStrokeWidth(m_currentStroke.width, m_zoom),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        // CONTENT ONLY, on transparent. The background is painted live under
        // the blit instead of being baked in here: baked, it froze whatever
        // mode was current when playback started (so the Background menu did
        // nothing while playing), and because the cache is blitted through a
        // pan/zoom correction the chequer would have travelled with the
        // drawing instead of staying screen-space.
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(m_panOffset);
        painter.scale(m_zoom, m_zoom);
        if (!m_unboundedCanvas) {
            painter.fillRect(documentRect(), Qt::white);
        }
        // Overlays and the in-progress stroke are editing aids, not content.
        paintSceneContent(painter, frame, false);
        painter.end();

        m_playbackFrames.append(image);
    }

    // Remember the view these pixels were rendered for. paintGL maps them onto
    // the CURRENT view from this, so zooming or scrolling during playback no
    // longer has to stop it.
    m_playbackCachePan = m_panOffset;
    m_playbackCacheZoom = m_zoom;
    m_playbackCacheFrameCount = frameCount;

    m_playbackActive = true;
    return true;
}

void PaintOpenGLWidget::schedulePlaybackCacheRefresh()
{
    if (!m_playbackActive || m_playbackCacheFrameCount <= 0) {
        return;
    }
    if (!m_playbackCacheTimer) {
        m_playbackCacheTimer = new QTimer(this);
        m_playbackCacheTimer->setSingleShot(true);
        connect(m_playbackCacheTimer, &QTimer::timeout, this, [this]() {
            if (!m_playbackActive || m_playbackCacheFrameCount <= 0) {
                return;
            }
            if (qFuzzyCompare(m_zoom, m_playbackCacheZoom)
                && m_panOffset == m_playbackCachePan) {
                return;   // the view came back to what the cache already holds
            }
            // Keep playing at the same frame: buildPlaybackCache resets the
            // index, and losing the user's position mid-playback would be a
            // worse artifact than the soft frame it is replacing.
            const int index = m_playbackIndex;
            QString error;
            if (buildPlaybackCache(m_playbackCacheFrameCount, &error)) {
                m_playbackIndex = std::min(index, int(m_playbackFrames.size()) - 1);
                update();
            } else {
                // Out of budget at the new size: stop rather than keep
                // blitting a cache that no longer matches anything.
                emit playbackInterrupted();
            }
        });
    }
    // Restarted on every tick, so a long gesture re-renders once at its end
    // rather than once per wheel notch.
    m_playbackCacheTimer->start(180);
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
    if (m_playbackCacheTimer) {
        m_playbackCacheTimer->stop();   // no re-render after playback is over
    }
    m_playbackCacheFrameCount = 0;
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
    // Bilinear filtering for every image drawn under the zoom transform -
    // raster layers and the playback cache. Without it a scaled image is
    // sampled nearest-neighbour and pixelates the moment zoom leaves 1.0.
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_playbackActive && m_playbackIndex >= 0 && m_playbackIndex < m_playbackFrames.size()) {
        // Always clear first: the widget can be larger than the cached frame
        // (a resize ends playback, but the repaint may arrive before that),
        // and uncovered pixels of a recreated FBO are undefined.
        if (m_unboundedCanvas) {
            paintBackground(painter, QRectF(rect()));
        } else {
            painter.fillRect(rect(), QColor(72, 72, 72));
        }
        // Map the cached pixels from the view they were rendered for onto the
        // view we are looking at now. A cache pixel p is document
        // (p - cachePan)/cacheZoom, which is on screen at
        // pan + doc*zoom - so one scale and one translate covers it.
        // Without this the frame could only ever be blitted 1:1, which is why
        // zooming used to have to drop out of playback.
        const qreal scale = m_playbackCacheZoom > 0.0 ? m_zoom / m_playbackCacheZoom : 1.0;
        if (!qFuzzyCompare(scale, qreal(1.0)) || m_panOffset != m_playbackCachePan) {
            painter.save();
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.translate(m_panOffset - m_playbackCachePan * scale);
            painter.scale(scale, scale);
            painter.drawImage(QPointF(0.0, 0.0), m_playbackFrames[m_playbackIndex]);
            painter.restore();
        } else {
            painter.drawImage(QPointF(0.0, 0.0), m_playbackFrames[m_playbackIndex]);
        }
        if (m_activeIndicator) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 120, 255), 3.0));
            painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        return;
    }

    if (m_unboundedCanvas) {
        // Infinite reference board: the whole viewport is paper.
        paintBackground(painter, rect());
    } else {
        painter.fillRect(rect(), QColor(72, 72, 72));
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_zoom, m_zoom);
    if (!m_unboundedCanvas) {
        const QRectF page = documentRect();
        painter.fillRect(page, Qt::white);
        // The page can now be smaller than the viewport, so say where it ends.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(140, 140, 140), 1.0 / m_zoom));
        painter.drawRect(page);
    }

    paintSceneContent(painter, m_model.currentFrame(), true);

    // Predictive preview: a thin, translucent, UNFILTERED line from the
    // stabilized tip to the physical pen tip. The stabilized ink trails the
    // pen by whatever lag compensation could not cancel; this line covers
    // that gap so the eye reads "the ink is at the tip" while the real
    // stroke settles in behind it. Never part of the document.
    // Straight mode has no gap to cover - its end IS the cursor - so the
    // preview would only draw a second line on top of the first.
    if (m_hasCurrentStroke && m_hasRawPenPos && !m_points.isEmpty()
        && m_inputFilter.active() && !m_straightLineMode) {
        const QPointF tip = m_points.last();
        // Under an axis lock the ink is constrained to the axis; a preview
        // pointing at the free cursor would wag a wrong-direction tail off
        // the locked line, so the target is projected onto the lock too.
        QPointF previewTarget = m_rawPenPos;
        if (m_axisSnapState == AxisSnapState::Horizontal) {
            previewTarget.setY(m_axisSnapAnchor.y());
        } else if (m_axisSnapState == AxisSnapState::Vertical) {
            previewTarget.setX(m_axisSnapAnchor.x());
        }
        if (QLineF(tip, previewTarget).length() > 0.5) {
            QColor preview = m_currentStroke.color;
            preview.setAlpha(110);
            const qreal previewWidth =
                std::max<qreal>(0.75 / m_zoom, m_currentStroke.width * 0.35);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(preview, previewWidth, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(tip, previewTarget);
        }
    }

    paintOverlayItems(painter);

    if ((m_tool == Tool::Eraser || m_tool == Tool::DeleteLine || m_tool == Tool::CutLine
         || m_tool == Tool::Connect)
        && m_hasHoverPos) {
        painter.setPen(QPen(QColor(220, 0, 180),
                            AnimeVectorLogic::displayStrokeWidth(1.5, m_zoom),
                            Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_hoverPos, m_eraserRadius, m_eraserRadius);
    }

    painter.restore();

    // Edit handles live in screen space, above everything on the paper.
    paintEditHandles(painter);

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
        const Qt::PenStyle overlayStyle =
            (item.penStyle >= Qt::SolidLine && item.penStyle <= Qt::DashDotDotLine)
                ? static_cast<Qt::PenStyle>(item.penStyle)
                : Qt::SolidLine;   // 0 is NoPen: never let a guide vanish
        painter.setPen(QPen(item.strokeColor,
                            AnimeVectorLogic::displayStrokeWidth(item.width, m_zoom),
                            overlayStyle, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);

        if (item.removable) {
            // ONE badge per id, not per item. A badge means "remove the thing
            // called <id>", and an id can legitimately arrive as several
            // items: a mapping area detected around a shape with a hole comes
            // back as one polygon per subpath, and every one of them was
            // getting its own x - several badges that all did the same thing.
            // Later parts only widen the extent the single badge sits on.
            int existing = -1;
            for (int i = 0; i < m_overlayHandles.size(); ++i) {
                if (m_overlayHandles[i].id == item.id) {
                    existing = i;
                    break;
                }
            }
            if (existing >= 0) {
                m_overlayHandles[existing].extent |= path.boundingRect();
                if (m_overlayHandles[existing].anchorIsExtent) {
                    m_overlayHandles[existing].rect =
                        overlayHandleRect(m_overlayHandles[existing].extent.topRight());
                }
                continue;
            }
            OverlayHandle handle;
            handle.id = item.id;
            handle.badgeColor = item.strokeColor;
            handle.extent = path.boundingRect();
            // Anchored on the item's END POINT, not on its bounding box. For a
            // roughly horizontal guide the box's top-right corner happens to
            // sit near the end and the badge looked right; for a vertical one
            // it is the START of the line instead, which is why the green V
            // guide carried its x at the wrong end. A CLOSED item (the mapping
            // area) has no meaningful end, so it keeps the box corner.
            handle.anchorIsExtent = item.closed;
            handle.rect = overlayHandleRect(item.closed ? handle.extent.topRight()
                                                        : item.points.last());
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

QRectF PaintOpenGLWidget::overlayHandleRect(const QPointF &anchor) const
{
    // Just above and to the right of the anchor.
    QRectF handleRect(anchor.x() + 4.0,
                      anchor.y() - kOverlayHandleSize - 4.0,
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

    // Content lock, first of two layers. This one stops a gesture from
    // STARTING; the mutating helpers (eraseAt, eraseBetween, deleteLineAt,
    // deleteLineBetween, cutLineAt, fillAt, moveCurrentLayerBy) each check
    // editingAllowed() too, and that is the layer that actually holds:
    // gating the press alone did NOT work, because the erase and move
    // gestures seed themselves in mouseMoveEvent when no press was recorded
    // and the erase tools also act on release, so a blocked press still let a
    // click or a drag rub out protected artwork.
    //
    // Arrow is NOT exempt. It looks like a selection tool but it is the point
    // editor: dragging a handle rewrites and commits stroke geometry, which is
    // exactly what a protected board must refuse.
    if (!editingAllowed()) {
        event->accept();
        return;
    }

    // Arrow and Connect both route presses through the handle machinery: a
    // press lands on a handle (Arrow drags it; Connect's buttons act on
    // release), or on the canvas ("pick" - Python decides what sits there:
    // Arrow grows edit handles, Connect snaps a vertex and, on the second
    // one, builds the connection). The model is only ever touched by Python.
    //
    // Handles are tested BEFORE the overlay x badges: a badge's hit rect is
    // clamped in document space and at high zoom covers a large screen area,
    // so testing it first made handles under it ungrabbable - the click
    // deleted a guide instead of starting the drag the user aimed at.
    if (m_tool == Tool::Arrow || m_tool == Tool::Connect) {
        const QString handleId = editHandleAt(event->position());
        if (!handleId.isEmpty()) {
            m_activeHandleDrag = handleId;
            sendPythonHandleMessage(QStringLiteral("press"), handleId, pos);
        } else if (removeOverlayItemAt(pos)) {
            // fallthrough handled: the badge consumed the click
        } else {
            sendPythonHandleMessage(QStringLiteral("pick"), QString(), pos);
        }
        event->accept();
        return;
    }

    if (removeOverlayItemAt(pos)) {
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

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine || m_tool == Tool::CutLine) {
        m_hasLastEraserPos = true;
        m_lastEraserPos = pos;
        if (m_tool == Tool::CutLine) {
            m_eraseGestureChanged = cutLineAt(pos);
        } else if (m_tool == Tool::DeleteLine) {
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
    m_liveFit = AnimeLiveFitState();   // fresh incremental-fit state per stroke
    // The fit's px budgets are SCREEN px (tremor, report rate and the eye
    // live there); capture the conversion once at pen-down so the whole
    // stroke - frozen prefix, tail and release - agrees on it.
    m_liveFitPixelScale = AnimeViewScale::pixelScale(m_zoom);
    // Realtime stabilization for this stroke: strength follows the smooth
    // slider. The filter is part of INPUT, not of fitting - the points the
    // stroke is built from are already the stabilized ones.
    m_inputFilter.configure(m_smoothValue / 100.0);
    // The filter runs on SCREEN coordinates: its speed-adaptive cutoff is
    // tuned in px/s of hand motion, which does not change with the zoom.
    // Document-space filtering made the stabilizer zoom-dependent.
    m_inputFilter.filter(event->position(), event->timestamp()); // seeds the state
    m_rawPenPos = pos;
    m_hasRawPenPos = true;
    appendPoint(pos);
    resetAxisSnap(event->modifiers(), pos);
    // A fresh stroke is always curved; the hold-still window opens with it, so
    // pressing and simply waiting is the shortest path to a straight line.
    m_straightLineMode = false;
    armHoldStill(pos);
    // Each stroke gets a fresh throttle window so its first "update" is
    // never swallowed by the previous stroke's timestamp.
    m_updateHookThrottle.invalidate();
    // The LIVE stroke is a polyline of the filtered points - already smooth,
    // and O(n) per move. The hybrid Bezier fit runs once, on release.
    m_currentStroke = makeStroke(m_points, m_penColor, m_penWidth, 0, true, false);
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
        // Connect snaps on HOVER: Python needs the cursor to show which
        // vertex a click would take, throttled so a fast mouse does not run
        // the interpreter at tablet rate. A content-locked board gets no
        // hints - presses are refused there, so advertising targets lies.
        if (m_tool == Tool::Connect && editingAllowed()
            && (!m_hoverHookThrottle.isValid()
                || m_hoverHookThrottle.elapsed() >= kUpdateHookIntervalMs)) {
            m_hoverHookThrottle.restart();
            sendPythonHandleMessage(QStringLiteral("hover"), QString(), m_hoverPos);
        }
        update();
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    if (m_tool == Tool::Arrow || m_tool == Tool::Connect) {
        if (!m_activeHandleDrag.isEmpty()) {
            sendPythonHandleMessage(QStringLiteral("move"), m_activeHandleDrag, m_hoverPos);
        }
        event->accept();
        return;
    }

    if (m_tool == Tool::CutLine) {
        // One cut per CLICK. Cutting continuously along a drag would chew
        // through every span the cursor passed and there would be no way to
        // take back just the last one.
        update();
        event->accept();
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

    if (m_straightLineMode) {
        // Straight mode owns the geometry: no capture floor, no stabilizer,
        // no fit. The user asked for a line, so the line ends exactly where
        // the cursor is - lag compensation and tremor filtering would only
        // put the endpoint somewhere they did not point at.
        updateStraightLine(straightLineTip(event->modifiers(), m_hoverPos));
        event->accept();
        return;
    }

    // Stabilize FIRST, snap SECOND: the axis snap is an exact constraint and
    // must win over the filter, not be smeared by it.
    m_rawPenPos = m_hoverPos;
    m_hasRawPenPos = true;
    // Stabilize in SCREEN space (hand speed is screen speed), then convert;
    // pan and zoom are constant during a drag, so the mapping commutes.
    const QPointF stabilized =
        mapToDocument(m_inputFilter.filter(event->position(), event->timestamp()));
    bool axisRetroChanged = false;
    const QPointF snappedPos = applyAxisSnap(event->modifiers(), stabilized, &axisRetroChanged);
    // Has the hand left the still circle? Measured on the RAW cursor, since
    // this asks about the hand, not about the ink the filter has settled on.
    // The radius is SCREEN px converted through algorithm/viewscale.h (the
    // shared home of screen<->canvas conversion): holding still means holding
    // still on the display, at any magnification. The anchor is kept in
    // document space so zooming mid-stroke cannot drag it out from under us.
    if (QLineF(m_holdStillAnchor, m_hoverPos).length()
        > AnimeViewScale::toDocumentLength(kHoldStillRadiusScreenPx, m_zoom)) {
        armHoldStill(m_hoverPos);
    }

    if (appendPoint(snappedPos) || axisRetroChanged) {
        updateCurrentStroke();
    } else {
        // Even a rejected point moved the raw tip; the preview line follows it.
        update();
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

    if (m_tool == Tool::Arrow || m_tool == Tool::Connect) {
        if (!m_activeHandleDrag.isEmpty()) {
            const QString handleId = m_activeHandleDrag;
            m_activeHandleDrag.clear();
            sendPythonHandleMessage(QStringLiteral("release"), handleId,
                                    mapToDocument(event->position()));
        }
        event->accept();
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine || m_tool == Tool::CutLine) {
        m_hoverPos = mapToDocument(event->position());
        m_hasHoverPos = true;
        bool cancelHistory = false;
        if (m_tool == Tool::CutLine) {
            // The cut already happened on PRESS; the release only reports it,
            // so a click-and-wander does not cut a second span under wherever
            // the button happened to come up.
            cancelHistory = pythonHookSendMessage(QStringLiteral("deletefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        } else if (m_tool == Tool::DeleteLine) {
            m_eraseGestureChanged = deleteLineAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(QStringLiteral("deletefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        } else {
            m_eraseGestureChanged = eraseAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(QStringLiteral("erasefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        }
        if (m_eraseGestureChanged && !cancelHistory) {
            commitHistory(m_tool == Tool::CutLine ? QStringLiteral("Cut Line")
                          : m_tool == Tool::DeleteLine ? QStringLiteral("Delete Line")
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

    if (m_straightLineMode) {
        // The segment already IS the committed geometry; the release only
        // pins its free end where the button came up. Like the curved path
        // below, it follows the lock that governed the last MOVE rather than
        // the live modifier state: letting go of Alt a moment before the
        // button would otherwise commit a diagonal the preview never showed.
        QPointF tip = mapToDocument(event->position());
        if (m_axisSnapState == AxisSnapState::Horizontal) {
            tip.setY(m_straightLineStart.y());
        } else if (m_axisSnapState == AxisSnapState::Vertical) {
            tip.setX(m_straightLineStart.x());
        } else {
            tip = straightLineTip(event->modifiers(), tip);
        }
        updateStraightLine(tip);
        m_axisSnapState = AxisSnapState::Inactive;
        finishCurrentStroke();
        event->accept();
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

    // A straight-line stroke is never fitted - not even by a mid-stroke pen
    // width change, which is the one caller that reaches here from outside the
    // move handler. Rebuilding it through the fitter would hand the frozen-
    // prefix machinery a buffer that straight mode rewrites wholesale.
    if (m_straightLineMode) {
        updateStraightLine(m_points.isEmpty() ? m_straightLineStart : m_points.last());
        return;
    }

    // Live preview shows the SAME curve the release will commit, built
    // incrementally: ink far behind the pen is fitted once and frozen (a
    // whole-stroke refit made the already-drawn path tremble under the
    // pen), only the tail refits, and it does so at display cadence, not
    // event cadence. Between fits the preview lags the pen by at most one
    // frame of ink; the release always fits the final points.
    if (!m_liveFitThrottle.isValid() || m_liveFitThrottle.elapsed() >= kLiveFitIntervalMs
        || m_points.size() < 3) {
        m_liveFitThrottle.start();
        AnimeStrokeFitSettings liveSettings = m_fitSettings;
        liveSettings.pixelScale = m_liveFitPixelScale;
        const QPainterPath live =
            AnimeVectorLogic::liveFitStrokePath(m_liveFit, m_points, liveSettings);
        m_currentStroke = AnimeVectorLogic::makeStrokeFromPath(
            live, m_points, m_currentStroke.color, m_currentStroke.width, 0);
        m_currentStroke.property = m_strokeProperty;
    }
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
    m_hasRawPenPos = false;
    if (m_holdStillTimer) {
        m_holdStillTimer->stop();   // no gesture may outlive its stroke
    }
    // The definitive result over the final points - the SAME frozen prefix
    // the user watched being drawn, plus the final tail fit, so what was
    // shown is exactly what commits. Not via updateCurrentStroke: that one
    // is throttled; this fit must never be skipped or stale. A straight-line
    // stroke skips it outright: its geometry is already exact, and refitting
    // is the very step the gesture exists to escape.
    if (!m_straightLineMode && !m_points.isEmpty()) {
        AnimeStrokeFitSettings liveSettings = m_fitSettings;
        liveSettings.pixelScale = m_liveFitPixelScale;
        const QPainterPath live =
            AnimeVectorLogic::liveFitStrokePath(m_liveFit, m_points, liveSettings, true);
        m_currentStroke = AnimeVectorLogic::makeStrokeFromPath(
            live, m_points, m_currentStroke.color, m_currentStroke.width, 0);
        m_currentStroke.property = m_strokeProperty;
    }
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
    m_straightLineMode = false;
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
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
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
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
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

int PaintOpenGLWidget::nearestStrokeToBrush(const VectorImageModel *image,
                                            const QPointF &from,
                                            const QPointF &to) const
{
    // A wide brush covers several lines at once. Picking "the first one found"
    // or "the topmost" then acts on whichever the loop happened to reach,
    // which from the user's side looks arbitrary - and for LineMode it deleted
    // ALL of them. The brush CENTRE decides instead: exactly one line goes,
    // the one the user aimed at.
    if (!image) {
        return -1;
    }
    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF hitBounds = QRectF(from, to).normalized()
                                 .adjusted(-imageRadius, -imageRadius, imageRadius, imageRadius);

    int best = -1;
    qreal bestDistance = 0.0;
    for (int i = 0; i < image->strokeCount(); ++i) {
        const VectorStroke &stroke = image->strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        const bool hit = QLineF(from, to).length() <= AnimeVectorLogic::epsilon()
                             ? AnimeVectorLogic::strokeHitsCircle(stroke, from, m_eraserRadius)
                             : AnimeVectorLogic::strokeHitsCapsule(stroke, from, to, m_eraserRadius);
        if (!hit) {
            continue;
        }
        const qreal distance = AnimeVectorLogic::strokeDistanceToBrush(stroke, from, to);
        // Ties go to the LATER index, which is the one drawn on top.
        if (best < 0 || distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

bool PaintOpenGLWidget::cutLineAt(const QPointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const qreal imageRadius = m_eraserRadius + m_penWidth;
    const QRectF hitBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                           imageRadius * 2.0, imageRadius * 2.0);
    if (!image->bounds().intersects(hitBounds)) {
        return false;
    }

    const int targetIndex = nearestStrokeToBrush(image, pos, pos);
    if (targetIndex < 0) {
        return false;
    }

    // Walls: every OTHER stroke of this layer. The target is excluded, so a
    // stroke that crosses itself is not chopped at its own crossing - the
    // user asked for crossings with the other lines.
    QVector<VectorStroke> walls;
    QVector<int> wallStrokeIndex;
    for (int i = 0; i < image->strokeCount(); ++i) {
        if (i == targetIndex) {
            continue;
        }
        walls.append(image->strokeAt(i));
        wallStrokeIndex.append(i);
    }

    const VectorStroke target = image->strokeAt(targetIndex);
    AnimeCutPlan plan;
    if (!AnimeVectorLogic::planCutAt(target, walls, pos, &plan)) {
        return false;
    }

    // A junction belongs to BOTH lines. Where a bound of the cut is a real
    // crossing, that wall is divided there too, so the point that just became
    // an endpoint of the target is an endpoint of its neighbour as well -
    // otherwise the wall still runs whole through a junction the drawing no
    // longer has. Only the one or two walls that BOUNDED this cut are
    // divided; every other stroke the target happens to cross is left alone.
    QHash<int, QVector<qreal>> wallCuts;
    QVector<qreal> selfCuts;
    for (const AnimeStrokeCrossing *bound : {&plan.low, &plan.high}) {
        if (bound->wallIndex >= 0 && bound->wallIndex < wallStrokeIndex.size()) {
            wallCuts[wallStrokeIndex[bound->wallIndex]].append(bound->wallArc);
        } else if (bound->wallIndex == AnimeStrokeCrossing::SelfCrossing) {
            // The other side of THIS junction is on the target itself, so the
            // survivor has to be divided there: the same rule as for a wall,
            // applied to the one line that is its own neighbour.
            selfCuts.append(bound->wallArc);
        }
    }

    // Every piece is cut straight out of the ORIGINAL stroke: all arcs are
    // consumed on the stroke they were measured on. Re-expressing a partner
    // arc in a survivor's own 0..1 (the old way) silently broke when pieces
    // stopped inheriting the parent's parameterization - their polyline is
    // re-sampled off the fitted path now, so the linear rescale landed the
    // junction pixels away from the crossing the user sees.
    QVector<qreal> boundaries;
    boundaries.append(plan.span.first);
    boundaries.append(plan.span.second);
    for (qreal arc : selfCuts) {
        // Interior arcs only, and not doubling a span bound - the same
        // endpoint rule splitStrokeAt applies (its endTolerance default).
        const qreal tolerance = 1e-3;
        if (arc > tolerance && arc < 1.0 - tolerance
            && std::abs(arc - plan.span.first) > tolerance
            && std::abs(arc - plan.span.second) > tolerance) {
            boundaries.append(arc);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());

    // A junction is ONE point of the drawing, and every line that meets it
    // must END on it - on literally the same coordinates. Exact splitting
    // puts each piece's cut end on its own fitted path, and two fitted
    // paths run a fit tolerance apart near a crossing: the sub-pixel gaps
    // that left were invisible to the eye but fatal to the fill, whose
    // planar graph merges vertices at 0.001 px. Both sides of a junction
    // recover the same polyline intersection point from their arcs, so
    // snapping every piece end there closes the drawing again; the nudge is
    // bounded by the fit tolerance and confined to the endpoint.
    const auto junctionPoint = [&target](qreal arc) {
        return AnimeVectorLogic::pointAtLength(target, arc * target.totalLength);
    };
    // A snap must stay a NUDGE: bounded by the piece's own length (a short
    // sliver must not have its ends cross) and by a sanity cap - a junction
    // farther out than any plausible fit deviation means something else went
    // wrong, and leaving the end in place beats teleporting ink. The fill's
    // dangling-tip heal still catches an unsnapped end.
    const auto snapEndBounded = [](VectorStroke piece, bool atEnd, const QPointF &junction) {
        constexpr qreal kJunctionSnapLimitPx = 4.0;
        if (piece.points.size() < 2) {
            return piece;
        }
        const QPointF end = atEnd ? piece.points.last() : piece.points.first();
        const qreal nudge = QLineF(end, junction).length();
        if (nudge <= 1e-12 || nudge > kJunctionSnapLimitPx
            || nudge > piece.totalLength * 0.5) {
            return piece;
        }
        return AnimeVectorLogic::snapStrokeEndpoint(piece, atEnd, junction);
    };
    const auto boundaryIsJunction = [&plan](qreal arc) {
        if (arc == plan.span.first) {
            return plan.low.isJunction();
        }
        if (arc == plan.span.second) {
            return plan.high.isJunction();
        }
        return true;   // the remaining boundaries are self-crossing partners
    };

    QVector<VectorStroke> pieces;
    qreal previous = 0.0;
    for (int i = 0; i <= boundaries.size(); ++i) {
        const qreal next = i < boundaries.size() ? boundaries[i] : 1.0;
        const qreal middle = (previous + next) * 0.5;
        const bool erased = middle >= plan.span.first && middle <= plan.span.second;
        if (next - previous > AnimeVectorLogic::epsilon() && !erased) {
            VectorStroke piece =
                AnimeVectorLogic::subStroke(target, previous, next, m_fitSettings);
            if (piece.points.size() >= 2) {
                if (i > 0 && boundaryIsJunction(previous)) {
                    piece = snapEndBounded(piece, false, junctionPoint(previous));
                }
                if (i < boundaries.size() && boundaryIsJunction(next)) {
                    piece = snapEndBounded(piece, true, junctionPoint(next));
                }
                pieces.append(piece);
            }
        }
        previous = next;
    }

    // Highest index first: every replacement renumbers the strokes after it,
    // so editing downwards keeps the indices collected above valid. Both
    // bounds landing on the SAME wall arrive as one entry with two arcs, so
    // that wall is divided into three in a single pass rather than having its
    // second arc measured against a stroke the first pass already replaced.
    QVector<int> editOrder = wallCuts.keys();
    std::sort(editOrder.begin(), editOrder.end(), std::greater<int>());
    bool changed = false;
    int currentTarget = targetIndex;
    for (int strokeIndex : editOrder) {
        const VectorStroke wall = image->strokeAt(strokeIndex);
        QVector<qreal> usedWallArcs;
        QVector<VectorStroke> wallPieces = AnimeVectorLogic::splitStrokeAt(
            wall, wallCuts[strokeIndex], m_fitSettings, 1e-3, &usedWallArcs);
        if (wallPieces.size() < 2) {
            continue;   // the crossing was already at one of the wall's ends
        }
        // The wall's side of each junction snaps to the crossing too. The
        // wall recovers the point from ITS arc; both strokes' arcs invert to
        // the same polyline intersection, so target and wall pieces end on
        // coordinates the fill graph reads as one vertex. splitStrokeAt
        // reports which arc made each boundary - pieces[k] and pieces[k+1]
        // share usedWallArcs[k] - so no proximity guessing between two
        // junctions on one wall.
        for (int k = 0; k + 1 < wallPieces.size() && k < usedWallArcs.size(); ++k) {
            const QPointF junction = AnimeVectorLogic::pointAtLength(
                wall, usedWallArcs[k] * wall.totalLength);
            wallPieces[k] = snapEndBounded(wallPieces[k], true, junction);
            wallPieces[k + 1] = snapEndBounded(wallPieces[k + 1], false, junction);
        }
        const int inserted = image->replaceStrokeWithPieces(strokeIndex, wallPieces);
        if (inserted <= 0) {
            continue;
        }
        changed = true;
        // One stroke out, `inserted` in, so everything after this index moves
        // by inserted - 1. Only edits BELOW the target move the target, and
        // the count is exact - no re-finding it by guesswork.
        if (strokeIndex < currentTarget) {
            currentTarget += inserted - 1;
        }
    }

    // No surviving piece means the cut spans the whole stroke (no crossing on
    // either side): removing it outright is the same rule, and going through
    // replaceStrokeWithPieces with an empty list would report "no change".
    if (pieces.isEmpty()) {
        image->removeStrokeAt(currentTarget);
        removeInvalidFillRegions();
        return true;
    }
    if (image->replaceStrokeWithPieces(currentTarget, pieces) > 0) {
        changed = true;
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed;
}

bool PaintOpenGLWidget::deleteLineAt(const QPointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    // ONE line per click, the one nearest the brush centre. It used to delete
    // every stroke the radius touched, so a wide brush aimed between two
    // parallel lines took them both.
    const int target = nearestStrokeToBrush(image, pos, pos);
    if (target < 0) {
        return false;
    }
    image->removeStrokeAt(target);
    removeInvalidFillRegions();
    return true;
}

bool PaintOpenGLWidget::deleteLineBetween(const QPointF &from, const QPointF &to)
{
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    // One line per drag STEP, nearest to the swept brush axis. A sweep still
    // takes out a run of lines - one per step - but a fat brush no longer
    // takes the neighbours of the line it is actually tracing.
    const int target = nearestStrokeToBrush(image, from, to);
    if (target < 0) {
        return false;
    }
    image->removeStrokeAt(target);
    removeInvalidFillRegions();
    return true;
}

bool PaintOpenGLWidget::fillAt(const QPointF &pos)
{
    if (!currentLayerAcceptsFill() || !editingAllowed()) {
        return false;
    }
    // `pos` is in DOCUMENT space (mousePressEvent maps it). The gate is the
    // artwork's own extent, so what a click can reach does not depend on the
    // window it was made through.
    if (!fillBoundsRect().contains(pos)) {
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
    if (delta.isNull() || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
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
    // The produced region is clipped to this rect, so a widget rect here made
    // a fill's SHAPE depend on the window size; it is the artwork's extent now.
    return AnimeVectorLogic::vectorRegionPathAt(seed, fillGraphSegments(scope, layerIndex),
                                                fillBoundsRect().toAlignedRect());
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
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_fitSettings));
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
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_fitSettings));
    }
    image->replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

PaintOpenGLWidget::VectorStroke PaintOpenGLWidget::makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id, bool filterInput, bool smoothPath) const
{
    return AnimeVectorLogic::makeStroke(points, color, width, id, filterInput, smoothPath, m_fitSettings);
}

bool PaintOpenGLWidget::appendPoint(const QPointF &point)
{
    if (m_points.isEmpty()) {
        m_points.append(point);
        return true;
    }

    // The capture floor is in SCREEN pixels: hand motion, not document
    // units, is what limits how much detail a sample can carry. Fixed in
    // document px it starved zoomed-in work - at 8x zoom the pen had to
    // travel 16 screen px between samples, so small drawn-in-close shapes
    // arrived with a handful of points. The zoom is clamped, not the
    // distance: beyond 16x the capture gains nothing (points are bounded by
    // the event rate anyway), and below 1/4x a coarser capture would start
    // visibly faceting big sweeping strokes.
    // Clamp range and conversion from algorithm/viewscale.h, the same one the
    // fitter's budgets use - they were two hand-written copies of it.
    if (QLineF(m_points.last(), point).length()
        >= m_minPointDistance * AnimeViewScale::pixelScale(m_zoom)) {
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
