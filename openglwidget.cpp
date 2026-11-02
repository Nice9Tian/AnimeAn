#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/path.h"
#include "algorithm/core/pathbool.h"
#include "backend/board/textpath.h"

#include "algorithm/core/string.h"
#include "openglwidget.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#ifdef ANIMEAN_WITH_PYTHON
#undef slots
#include <pybind11/embed.h>
#endif

using namespace AnimeCore;

namespace {
// The artwork's ground, not chrome: it stays white in both themes.
const Color kPaperColor(255, 255, 255);
const double kOverlayHandleSize = 14.0;
// Screen px around a badge's centre that always mean the badge: a drawn badge
// at ordinary zoom, a fingertip's worth of it when high zoom makes it huge.
const double kOverlayBadgeSurePx = 12.0;
// The zoom limits the wheel has always enforced, named so the programmatic
// paths (setViewTransform, fitViewToContent) cannot drift away from what a
// gesture is allowed to reach.
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 8.0;
double clampZoom(double zoom)
{
    return zoom < kMinZoom ? kMinZoom : (zoom > kMaxZoom ? kMaxZoom : zoom);
}
}

#ifdef ANIMEAN_WITH_PYTHON
#include "pythonbind/python_bindings.h"

namespace py = pybind11;

namespace {
String toolName(PaintOpenGLWidget::Tool tool)
{
    switch (tool) {
    case PaintOpenGLWidget::Tool::Pen:
        return String("pen");
    case PaintOpenGLWidget::Tool::Eraser:
        return String("eraser");
    case PaintOpenGLWidget::Tool::DeleteLine:
        return String("delete_line");
    case PaintOpenGLWidget::Tool::CutLine:
        return String("cut_line");
    case PaintOpenGLWidget::Tool::Fill:
        return String("fill");
    case PaintOpenGLWidget::Tool::Arrow:
        return String("arrow");
    case PaintOpenGLWidget::Tool::Connect:
        return String("connect");
    case PaintOpenGLWidget::Tool::Transfer:
        return String("transfer");
    }
    return String("pen");
}

py::dict pointToPythonDict(const PointF &point)
{
    py::dict pointInfo;
    pointInfo["x"] = point.x();
    pointInfo["y"] = point.y();
    return pointInfo;
}

py::object variantToPythonObject(const JsonVariant &value)
{
    const JsonValue &v = value.toJsonValue();
    if (v.isBool()) {
        return py::bool_(v.toBool());
    }
    if (v.isInt64()) {
        return py::int_(v.toInteger());
    }
    if (v.isDouble()) {
        return py::float_(v.toDouble());
    }
    return py::str(v.toString().toStdString());
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
bool isQuietHookOutput(const String &output)
{
    return output.isEmpty() || output.endsWith(String("(no output)"));
}

String pythonHookSendMessage(const py::dict &message)
{
    try {
        py::gil_scoped_acquire acquire;
        py::object hooks = py::module_::import("python_hooks");
        if (!hooks.attr("has_hooks")(message).cast<bool>()) {
            return String();
        }
        py::object io = py::module_::import("io");
        py::object contextlib = py::module_::import("contextlib");
        py::object stdoutBuffer = io.attr("StringIO")();
        py::object stderrBuffer = io.attr("StringIO")();
        py::object stdoutRedirect = contextlib.attr("redirect_stdout")(stdoutBuffer);
        py::object stderrRedirect = contextlib.attr("redirect_stderr")(stderrBuffer);

        stdoutRedirect.attr("__enter__")();
        stderrRedirect.attr("__enter__")();
        String errorText;
        try {
            hooks.attr("dispatch")(message);
        } catch (const py::error_already_set &error) {
            errorText = String::fromUtf8(error.what());
        }
        stderrRedirect.attr("__exit__")(py::none(), py::none(), py::none());
        stdoutRedirect.attr("__exit__")(py::none(), py::none(), py::none());

        String output = String::fromUtf8(stdoutBuffer.attr("getvalue")().cast<std::string>().c_str());
        const String stderrOutput = String::fromUtf8(stderrBuffer.attr("getvalue")().cast<std::string>().c_str());
        if (!stderrOutput.isEmpty()) {
            output += stderrOutput;
        }
        if (!errorText.isEmpty()) {
            output += errorText;
        }
        if (output.trimmed().isEmpty()) {
            output = String("(no output)");
        }
        // Defaulted lookups: py::dict has no dict-specific operator[], so a
        // missing key raises KeyError and would throw all the way out to the
        // catch below - discarding the hook's real output and reporting a
        // bogus "hook setup error" instead. The formatter must never be the
        // thing that fails after the hook already ran.
        const auto text = [&message](const char *key, const char *fallback) {
            if (!message.contains(py::str(key))) {
                return String::fromUtf8(fallback);
            }
            return String::fromUtf8(py::str(message[key]).cast<std::string>().c_str());
        };
        const String event = text("event", "?");
        const String tool = text("tool", "-");
        return String("[python feedback] %1 tool=%2\n%3").arg(event, tool, output.trimmed());
    } catch (const py::error_already_set &error) {
        return String("[python feedback] hook setup error: %1").arg(String::fromUtf8(error.what()));
    }
}
}
#endif

PaintOpenGLWidget::PaintOpenGLWidget(void *parent)
    : PaintBoardBase(parent)
{
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(ClickFocus);
    m_history.reset(String("Initial"), m_model);
}

SceneHistory &PaintOpenGLWidget::history()
{
    return m_history;
}

const SceneHistory &PaintOpenGLWidget::history() const
{
    return m_history;
}

void PaintOpenGLWidget::commitHistory(const String &label)
{
    m_history.commit(label, m_model);
    // Any commit can have changed what a ghosted frame looks like, or how
    // many frames there are; the ghosts are pixels of the old scene.
    clearOnionCache();
    historyChanged();
    historyCommitted();
}

void PaintOpenGLWidget::dropRedoTail()
{
    if (m_history.truncateRedo()) {
        historyChanged();
    }
}

void PaintOpenGLWidget::resetHistory(const String &label)
{
    m_history.reset(label, m_model);
    // Scene content was (re)established outside the history flow (startup,
    // project open): let scripts re-sync any state they keep in scriptData.
    // That re-sync covers the frame and layer too, so they become the
    // notified baselines instead of triggering redundant framechange /
    // layerchange right afterwards.
    m_pythonNotifiedFrame = m_model.currentFrame();
    m_pythonNotifiedLayer = m_model.currentLayer();
    m_pythonNotifiedLayerId =
        m_pythonNotifiedLayer >= 0 ? m_model.layerIdAt(m_pythonNotifiedLayer) : 0;
    pythonHookSendMessage(String("historyrestore"));
    // The onion state is NOT covered by that re-sync: historyrestore carries no
    // onion state, and the frame pre-sync above is exactly what stops
    // framechange - and with it the onion notifier - from firing. A restore
    // moves the PLAYHEAD, which is what a ghost's past/ahead tint is read
    // against, and a project open swaps the whole document under an onion set
    // that survives the swap. Sent AFTER historyrestore so the script re-reads
    // its ghosts against state it has already restored. No baseline
    // invalidation: the notifier's own compare is against what Python was last
    // told, which historyrestore does not touch, so this stays silent when
    // nothing actually moved - including the "onion off" board, whose baseline
    // is deliberately valid from birth.
    notifyOnionChangedIfNeeded();
    clearOnionCache();
    historyChanged();
}

void PaintOpenGLWidget::stopOverlayDragMoveTimer()
{
    if (m_overlayDragMoveTimer) {
        m_overlayDragMoveTimer->stop();
    }
}

void PaintOpenGLWidget::flushPendingOverlayDragMove()
{
    if (m_activeOverlayDrag.isEmpty()) {
        return;
    }
    if (m_overlayDragHookThrottle.isValid()) {
        const std::int64_t elapsed = m_overlayDragHookThrottle.elapsed();
        if (elapsed < kUpdateHookIntervalMs) {
            m_overlayDragMoveTimer->start(int(kUpdateHookIntervalMs - elapsed));
            return;
        }
    }
    m_overlayDragHookThrottle.restart();
    sendPythonHandleMessage(String("move"), m_activeOverlayDrag,
                            m_pendingOverlayDragPos, m_pendingOverlayDragModifiers);
    update();
}

void PaintOpenGLWidget::cancelActiveOverlayDrag()
{
    // A context change (tool, stroke property, frame) invalidates an
    // in-flight overlay drag transaction: tell the owning tool to restore
    // its persisted baseline. One home for the teardown - the call sites
    // differ only in when they fire, never in how.
    stopOverlayDragMoveTimer();
    if (m_activeOverlayDrag.isEmpty()) {
        return;
    }
    const String overlayId = m_activeOverlayDrag;
    m_activeOverlayDrag.clear();
    sendPythonHandleMessage(String("cancel"), overlayId, PointF());
}

void PaintOpenGLWidget::notifyFrameChangedIfNeeded()
{
    // The frame lives on the model, and several paths mutate it without
    // going through setCurrentFrame first: ui.set_current writes the model
    // and the attention re-sync only calls setCurrentFrame with the already
    // current value, so a "previous frame" read from the model itself never
    // differs. Comparing against the widget's own last-notified frame makes
    // the notification correct no matter who moved the frame.
    const int frame = m_model.currentFrame();
    if (frame == m_pythonNotifiedFrame) {
        return;
    }
    cancelActiveOverlayDrag();
    m_pythonNotifiedFrame = frame;
    pythonHookSendMessage(String("framechange"));
    // Which ghosts are past and which are ahead depends on where the playhead
    // is, so a frame move re-colours the whole ghost set. Guarded inside the
    // notifier: with onion off this is free.
    notifyOnionChangedIfNeeded();
}

void PaintOpenGLWidget::notifyLayerChangedIfNeeded()
{
#ifdef ANIMEAN_WITH_PYTHON
    // Same contract as notifyFrameChangedIfNeeded: the current layer lives on
    // the model and several paths write it without passing through the widget
    // (ui.set_current, scene.set_current_layer, deleteLayer's fix-ups), so
    // the baseline is the widget's own last-notified value, never a
    // "previous" re-read from the model. The message carries the stable
    // column id alongside the index: indices shift on every move/delete,
    // and a tool keying per-layer state (an auto-mapping layer's config)
    // must key it on identity.
    const int layer = m_model.currentLayer();
    const int layerId = layer >= 0 ? m_model.layerIdAt(layer) : 0;
    if (layer == m_pythonNotifiedLayer && layerId == m_pythonNotifiedLayerId) {
        return;
    }
    const int previous = m_pythonNotifiedLayer;
    m_pythonNotifiedLayer = layer;
    m_pythonNotifiedLayerId = layerId;
    if (!animeanHookEventSubscribed(String("layerchange"))) {
        return;
    }

    const int frameRow = m_model.currentFrame();
    const AnimeCell cell = m_model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict message;
    message["event"] = "layerchange";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["layer"] = layer;
    message["layer_id"] = layerId;
    message["previous"] = previous == -2 ? -1 : previous;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#endif
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

    // historyrestore makes the restored Python state authoritative; do not
    // let the mouse release from an interrupted pre-restore overlay drag be
    // dispatched into that new state. Deliberately a silent clear, not
    // cancelActiveOverlayDrag(): a cancel would mutate the very state the
    // restore just made authoritative. The restore also covers the frame
    // and layer, so they become the notified baselines.
    m_activeOverlayDrag.clear();
    stopOverlayDragMoveTimer();
    m_pythonNotifiedFrame = m_model.currentFrame();
    m_pythonNotifiedLayer = m_model.currentLayer();
    m_pythonNotifiedLayerId =
        m_pythonNotifiedLayer >= 0 ? m_model.layerIdAt(m_pythonNotifiedLayer) : 0;
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    m_eraseGestureChanged = false;
    m_axisSnapState = AxisSnapState::Inactive;
    pythonHookSendMessage(String("historyrestore"));
    // Undo/redo can move the playhead (a history entry is a whole-model
    // snapshot), and past/ahead is read against the playhead - but the frame
    // pre-sync above is precisely what keeps framechange, and with it the onion
    // notifier, from firing. Same contract as resetHistory: after the restore,
    // and the notifier's own compare keeps it silent when the playhead landed
    // where Python already thinks it is.
    notifyOnionChangedIfNeeded();
    clearOnionCache();
    update();
    historyChanged();
    return true;
}

void PaintOpenGLWidget::setViewName(const String &name)
{
    m_viewName = name;
}

String PaintOpenGLWidget::viewName() const
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

double PaintOpenGLWidget::zoom() const
{
    return m_zoom;
}

PointF PaintOpenGLWidget::panOffset() const
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
    m_panOffset = PointF(-horizontal, -vertical);
    clampPan();
    update();
    // A scroll bar only ever moves because a hand moved it: syncScrollBars
    // writes the bars back with its own guard up, so nothing programmatic
    // reaches here. No notifyViewTransformChanged() though - that would run
    // straight back into the bar that called this.
    userTransformed();
}

void PaintOpenGLWidget::setViewTransform(double zoom, const PointF &pan)
{
    if (m_playbackActive) {
        // Same as the wheel: the cached frames are re-mapped onto the new view
        // and re-rendered once it settles.
        schedulePlaybackCacheRefresh();
    }
    m_zoom = clampZoom(zoom);
    m_panOffset = pan;
    clampPan();
    update();
    notifyViewTransformChanged();
}

void PaintOpenGLWidget::paintBackground(Painter &painter, const RectF &target) const
{
    switch (m_backgroundMode) {
    case BackgroundMode::Black:
        painter.fillRect(target, Color(0, 0, 0));
        return;
    case BackgroundMode::Transparent: {
        // The usual grey/white chequer. Drawn in SCREEN space (this runs
        // before the zoom transform), so the squares stay a constant size on
        // screen the way every other application's transparency chequer does
        // - a chequer that zoomed with the drawing would read as content.
        constexpr int kSquare = 8;
        painter.save();
        painter.fillRect(target, Color(255, 255, 255));
        painter.setPen(NoPen);
        painter.setBrush(Color(204, 204, 204));
        const int left = int(std::floor(target.left()));
        const int top = int(std::floor(target.top()));
        const int right = int(std::ceil(target.right()));
        const int bottom = int(std::ceil(target.bottom()));
        for (int y = top - (top % kSquare) - kSquare; y < bottom; y += kSquare) {
            for (int x = left - (left % kSquare) - kSquare; x < right; x += kSquare) {
                if (((x / kSquare) + (y / kSquare)) % 2 == 0) {
                    painter.drawRect(Rect(x, y, kSquare, kSquare));
                }
            }
        }
        painter.restore();
        return;
    }
    case BackgroundMode::White:
    default:
        painter.fillRect(target, Color(255, 255, 255));
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

void PaintOpenGLWidget::setEditablePropertyFilter(const StringList &properties)
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

void PaintOpenGLWidget::setFillPaintMode(bool on)
{
    m_fillPaintMode = on;
}

bool PaintOpenGLWidget::fillPaintMode() const
{
    return m_fillPaintMode;
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

PointF PaintOpenGLWidget::mapToDocument(const PointF &screenPos) const
{
    // The view transform lives in algorithm/viewscale.h - THE shared home of
    // screen<->canvas conversion. Do not re-derive it here or anywhere else.
    return AnimeViewScale::toDocument(screenPos, m_zoom, m_panOffset);
}

RectF PaintOpenGLWidget::documentRect() const
{
    // The page comes from the DOCUMENT. It used to be the widget rect, which
    // made the paper resize with the window - the drawing area, the export
    // and every bounds-driven algorithm silently changed shape when the user
    // dragged a dock.
    const Size canvas = m_model.canvasSize();
    return RectF(0.0, 0.0, canvas.width(), canvas.height());
}

RectF PaintOpenGLWidget::fillBoundsRect() const
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
    RectF bounds = m_model.fillBoundaryBounds(m_model.currentFrame(), -1);
    if (!m_unboundedCanvas) {
        // The page is a property of the document, not of the view, so it stays
        // in the clip - this can only ever GROW what the bounded board allowed.
        bounds = bounds.isNull() ? documentRect() : bounds.united(documentRect());
    }
    if (bounds.isNull()) {
        return RectF();   // no walls on this frame: there is nothing to fill
    }
    // Leave room for the halo that tucks a region under its own outline;
    // clipping tight to the ink would shave it back off.
    const double pad = AnimeVectorLogic::kVectorRegionOverpaintWidth;
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
    clearOnionCache();
    update();
    notifyViewTransformChanged();
}

void PaintOpenGLWidget::setCanvasSize(const Size &size)
{
    if (Size(m_model.canvasSize()) == size) {
        return;
    }
    m_model.setCanvasSize(size);
    clampPan();
    update();
    notifyViewTransformChanged();
}

RectF PaintOpenGLWidget::reachableRect() const
{
    RectF reach = documentRect();
    const RectF content = m_model.contentBounds(m_model.currentFrame());
    if (!content.isNull()) {
        reach = reach.isNull() ? content : reach.united(content);
    }
    if (reach.isNull()) {
        return reach;
    }
    // Half a viewport of slack on every side, so ink at the very edge can be
    // brought to the middle of the screen to work on rather than being pinned
    // against the frame. Screen px -> document via algorithm/viewscale.h.
    const double padX = AnimeViewScale::toDocumentLength(width() * 0.5, m_zoom);
    const double padY = AnimeViewScale::toDocumentLength(height() * 0.5, m_zoom);
    return reach.adjusted(-padX, -padY, padX, padY);
}

RectF PaintOpenGLWidget::visibleContentBounds() const
{
    // Same column filter the paint pass uses (invisible columns are skipped)
    // plus the internal ones, which are machinery rather than artwork - so this
    // bounds exactly what the user can see on this frame.
    RectF bounds;
    const int frame = m_model.currentFrame();
    if (frame < 0) {
        return bounds;
    }
    for (int layer = 0; layer < m_model.layerCount(); ++layer) {
        if (!m_model.layerVisible(layer) || m_model.layerInternal(layer)) {
            continue;
        }
        const VectorImageModel *image = m_model.imageAt(frame, layer);
        if (!image) {
            continue;
        }
        const RectF cell = image->bounds();
        if (cell.isNull()) {
            continue;
        }
        bounds = bounds.isNull() ? cell : bounds.united(cell);
    }
    return bounds;
}

bool PaintOpenGLWidget::fitViewToContent(bool cover)
{
    if (width() <= 0 || height() <= 0) {
        return false;
    }

    RectF target = visibleContentBounds();
    if (target.isNull()) {
        // An empty frame still has a page worth framing. The page is a property
        // of the DOCUMENT, so the unbounded board has one too - what "unbounded"
        // drops is the pan clamp, not the paper.
        target = documentRect();
    }
    if (target.isNull()) {
        // A document with no canvas size either: nothing to frame, and a guess
        // would be worse than leaving the view where the user left it.
        return false;
    }
    // A single flat stroke has no height and a dot no size at all; either would
    // divide the viewport by zero. Give the degenerate axis the other one's
    // span (or a document unit when both are flat) about the same centre.
    if (target.width() <= 0.0 || target.height() <= 0.0) {
        const PointF centre = target.center();
        const double span = std::max<double>(std::max(target.width(), target.height()), 1.0);
        target.setSize(SizeF(target.width() > 0.0 ? target.width() : span,
                              target.height() > 0.0 ? target.height() : span));
        target.moveCenter(centre);
    }

    const double scaleX = double(width()) / target.width();
    const double scaleY = double(height()) / target.height();
    // COVER: the content spans the frame on BOTH axes and the longer one runs
    // off the edges (bleed). CONTAIN would leave a margin on one axis.
    const double zoom = clampZoom(cover ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY));
    // Centre it: solve toScreen(centre) == viewport centre for the pan
    // (algorithm/viewscale.h owns the conversion, here as everywhere else).
    const PointF pan = PointF(width() * 0.5, height() * 0.5)
                        - PointF(AnimeViewScale::toScreen(target.center(), zoom, PointF()));
    setViewTransform(zoom, pan);
    return true;
}

void PaintOpenGLWidget::clampPan()
{
    if (m_unboundedCanvas) {
        return;
    }

    // The travel limit follows the REACHABLE area (page + artwork + slack),
    // not the page. Clamped to the page, zooming in walled the view inside
    // the paper: anything drawn past its edge could not be scrolled to, so it
    // could not be edited either.
    const RectF reach = reachableRect();
    if (reach.isNull()) {
        return;
    }
    // In screen space the reachable area spans [reach.left*zoom + pan,
    // reach.right*zoom + pan]; the pan may move until that span's far edge
    // meets the near edge of the viewport, and no further.
    const double left = reach.left() * m_zoom;
    const double right = reach.right() * m_zoom;
    const double top = reach.top() * m_zoom;
    const double bottom = reach.bottom() * m_zoom;

    const double minX = std::min<double>(width() - right, -left);
    const double maxX = std::max<double>(width() - right, -left);
    const double minY = std::min<double>(height() - bottom, -top);
    const double maxY = std::max<double>(height() - bottom, -top);

    m_panOffset.setX(std::min(maxX, std::max(minX, m_panOffset.x())));
    m_panOffset.setY(std::min(maxY, std::max(minY, m_panOffset.y())));
}

void PaintOpenGLWidget::notifyViewTransformChanged()
{
    // The pen's ring is sized in SCREEN px, so a zoom resizes it. Every view
    // change funnels through here, which makes this the one place to rebuild
    // it from.
    updateBrushCursor();
    viewTransformChanged();
}

void PaintOpenGLWidget::wheelEvent(WheelEvent *event)
{
    // Zooming no longer leaves playback. paintGL maps the cached frames from
    // the view they were rendered for onto the current one, so the animation
    // keeps running through the gesture; the cache is re-rendered once the
    // wheel stops, which is what makes it sharp again (and fills in anything
    // the old viewport did not cover).
    if (m_playbackActive) {
        schedulePlaybackCacheRefresh();
    }

    const double steps = event->angleDelta().y() / 120.0;
    if (fuzzyIsNull(steps)) {
        PaintBoardBase::wheelEvent(event);
        return;
    }

    const double factor = std::pow(1.25, steps);
    const double newZoom = clampZoom(m_zoom * factor);
    if (fuzzyCompare(newZoom, m_zoom)) {
        event->accept();
        return;
    }

    // Zoom around the cursor: keep the document point under it fixed.
    const PointF anchor = event->position();
    const PointF docAnchor = mapToDocument(anchor);
    m_zoom = newZoom;
    // Solve toScreen(docAnchor) == anchor for the pan (algorithm/viewscale.h).
    m_panOffset = anchor - PointF(AnimeViewScale::toScreen(docAnchor, m_zoom, PointF()));
    clampPan();
    update();
    notifyViewTransformChanged();
    userTransformed();
    // The artist-mode key-point filter is zoom-dependent (a perceptual
    // threshold in SCREEN space), so a zoom while handles are up re-asks
    // Python which handles survive at the new magnification. Connect's
    // button offsets are screen-sized too, so its handles re-place as well.
    if ((m_tool == Tool::Arrow || m_tool == Tool::Connect)
        && !m_editHandles.isEmpty() && m_activeHandleDrag.isEmpty()) {
        sendPythonHandleMessage(String("view"), String(), docAnchor);
    }
    event->accept();
}

void PaintOpenGLWidget::resizeEvent(ResizeEvent *event)
{
    PaintBoardBase::resizeEvent(event);
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

namespace {
// A circular arrow, ~20 px across: white so it reads over ink, outlined in
// black so it reads over paper. Painted rather than shipped as a resource -
// it has to follow the device pixel ratio of whatever screen it lands on.
Image buildRotateCursorPixmap(double dpr)
{
    constexpr double kSide = 24.0;
    Image pixmap(int(std::ceil(kSide * dpr)), int(std::ceil(kSide * dpr)),
                 Image::Format_ARGB32_Premultiplied);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Color(0, 0, 0, 0));

    const PointF centre(kSide * 0.5, kSide * 0.5);
    const double radius = 6.5;
    const RectF ring(centre.x() - radius, centre.y() - radius, radius * 2.0, radius * 2.0);
    Path arc;
    arc.arcMoveTo(ring, 55.0);
    arc.arcTo(ring, 55.0, 250.0);

    // Arc angles run counter-clockwise in a y-DOWN pixmap, so the end
    // point and its tangent are (cos, -sin) and (-sin, -cos).
    const double endAngle = degreesToRadians(55.0 + 250.0);
    const PointF tip(centre.x() + radius * std::cos(endAngle),
                      centre.y() - radius * std::sin(endAngle));
    const PointF tangent(-std::sin(endAngle), -std::cos(endAngle));
    const PointF normal(-tangent.y(), tangent.x());
    Vector<PointF> head;
    head << tip + tangent * 4.2
         << tip + normal * 3.2 - tangent * 1.4
         << tip - normal * 3.2 - tangent * 1.4;

    {
        Painter painter(&pixmap);
        painter.setRenderHint(Painter::Antialiasing, true);
        // Drawn twice, wide black under narrow white: the same halo trick the
        // pen ring uses, and for the same reason.
        for (int pass = 0; pass < 2; ++pass) {
            const Color color = pass == 0 ? Color(20, 20, 20, 235)
                                           : Color(255, 255, 255, 245);
            const double width = pass == 0 ? 3.4 : 1.4;
            painter.setPen(Pen(color, width, SolidLine, RoundCap, RoundJoin));
            painter.setBrush(NoBrush);
            painter.drawPath(arc);
            painter.setBrush(color);
            painter.drawPolygon(head);
        }
    }
    return pixmap;
}

Cursor cursorForScriptName(const String &name, double dpr)
{
    if (name == String("rotate")) {
        return Cursor(buildRotateCursorPixmap(dpr), 12, 12);
    }
    static const Hash<String, CursorShape> shapes = {
        {String("arrow"), ArrowCursor},
        {String("size_all"), SizeAllCursor},
        {String("size_h"), SizeHorCursor},
        {String("size_v"), SizeVerCursor},
        {String("size_bdiag"), SizeBDiagCursor},
        {String("size_fdiag"), SizeFDiagCursor},
        {String("cross"), CrossCursor},
    };
    return Cursor(shapes.value(name, ArrowCursor));
}
}   // namespace

void PaintOpenGLWidget::setScriptCursor(const String &name)
{
    if (m_scriptCursor == name) {
        return;
    }
    m_scriptCursor = name;
    updateBrushCursor();
}

void PaintOpenGLWidget::updateBrushCursor()
{
    // Only the pen wears a custom pointer of its own. Every other tool keeps
    // the arrow - including the erasers, whose dashed ring is a different
    // affordance and free to be far larger than a cursor bitmap may be -
    // unless its Python half named one for the region under the cursor.
    if (m_tool != Tool::Pen) {
        m_penRingOnCanvas = false;
        if (!m_scriptCursor.isEmpty()) {
            setCursor(cursorForScriptName(m_scriptCursor, devicePixelRatioF()));
            return;
        }
        unsetCursor();
        return;
    }

    // The ring is the mark the pen will leave, so its screen size is the
    // stroke width through the view transform - conversion from
    // algorithm/viewscale.h, the shared home of screen<->canvas.
    const double diameter = AnimeViewScale::toScreenLength(m_penWidth, m_zoom);

    if (diameter > kMaxCursorRingPx) {
        // Too big to hand to the window system. Blank the pointer and let
        // paintGL draw the ring: the arrow still goes, which is the point.
        m_penRingOnCanvas = true;
        setCursor(BlankCursor);
        return;
    }
    m_penRingOnCanvas = false;

    // Leave room for the outline itself, and never go below a few pixels or
    // there is nothing to aim with.
    const double ringPen = 1.25;
    const int side = std::max(9, int(std::ceil(diameter + ringPen * 2.0 + 2.0)));
    const double dpr = devicePixelRatioF();
    Image pixmap(int(std::ceil(side * dpr)), int(std::ceil(side * dpr)),
                 Image::Format_ARGB32_Premultiplied);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Color(0, 0, 0, 0));
    {
        Painter painter(&pixmap);
        painter.setRenderHint(Painter::Antialiasing, true);
        const PointF centre(side * 0.5, side * 0.5);
        const double radius = diameter * 0.5;
        // Drawn twice: a light halo under a dark line, so the ring stays
        // legible over black ink and over white paper alike.
        painter.setBrush(NoBrush);
        painter.setPen(Pen(Color(255, 255, 255, 200), ringPen * 2.0));
        painter.drawEllipse(centre, radius, radius);
        painter.setPen(Pen(Color(20, 20, 20, 230), ringPen));
        painter.drawEllipse(centre, radius, radius);
        // A dot at the hotspot: with a wide pen the ring alone leaves the
        // exact point ambiguous, which is where the stroke actually starts.
        if (diameter >= 12.0) {
            painter.setPen(Pen(Color(255, 255, 255, 200), 2.0));
            painter.drawPoint(centre);
            painter.setPen(Pen(Color(20, 20, 20, 230), 1.0));
            painter.drawPoint(centre);
        }
    }
    setCursor(Cursor(pixmap, side / 2, side / 2));
}

void PaintOpenGLWidget::leaveEvent(Event *event)
{
    // The brush ring is drawn at the last hovered point, and nothing used to
    // retract it: moving the pointer from one board to the other left a ring
    // stuck against the edge of the board being left, on both boards at once.
    // It is a cursor, so it goes when the cursor goes.
    //
    // Safe during a stroke: the board keeps delivering moves to the widget that
    // holds the mouse grab, and each one sets m_hoverPos again, so a drag
    // that wanders outside the viewport keeps its ring.
    if (m_hasHoverPos) {
        m_hasHoverPos = false;
        update();
    }
    // A script tool that draws something after the pointer (a preview, a
    // snap hint) hears that the pointer is gone, so it can take that down.
    if (!m_activePythonTool.isEmpty()) {
        sendPythonHandleMessage(String("leave"), String(), PointF());
    }
    PaintBoardBase::leaveEvent(event);
}

void PaintOpenGLWidget::focusInEvent(FocusEvent *event)
{
    // Focus is handed to a click target BEFORE delivering the press. When that
    // click also ends playback (focusGained -> activate view -> stop), the
    // press must still be swallowed: by the time it arrives the playback flag
    // is already cleared, and without this it would draw into the frame the
    // user could not even see.
    if (m_playbackActive && event->reason() == MouseFocusReason) {
        m_swallowNextPress = true;
    }
    PaintBoardBase::focusInEvent(event);
    m_hasInputFocus = true;
    update();
    focusGained();
}

void PaintOpenGLWidget::focusOutEvent(FocusEvent *event)
{
    PaintBoardBase::focusOutEvent(event);
    m_hasInputFocus = false;
    update();
}

void PaintOpenGLWidget::setPenColor(const Color &color)
{
    // Picking a colour in the tool panel means "draw with this": it arms the
    // pen FIRST (which announces the tool change, so the colour policy in
    // pyfile/tool_colors.py hands the pen its remembered colour) and applies
    // the pick SECOND, so the pick wins over what was restored. Assigning
    // m_tool directly skipped the announcement and left the policy stale.
    setTool(Tool::Pen);
    setDrawingColor(color);
}

void PaintOpenGLWidget::setDrawingColor(const Color &color)
{
    // Pure mechanism: "draw with this colour". WHICH colour each tool gets
    // is policy and lives in pyfile/tool_colors.py - the per-tool cache was
    // briefly kept here and moved out on the house rule that generic
    // mechanisms are C++ and tool behaviour is Python.
    m_penColor = color;
    updateBrushCursor();
}

void PaintOpenGLWidget::setPenWidth(double width)
{
    if (width < 1.0) {
        width = 1.0;
    } else if (width > 50.0) {
        width = 50.0;
    }

    m_penWidth = width;
    updateBrushCursor();   // the ring IS the width
    if (m_hasCurrentStroke) {
        updateCurrentStroke();
    }
    update();
}

void PaintOpenGLWidget::setStrokeProperty(const String &property)
{
    const bool changed = m_strokeProperty != property;
    if (changed) {
        // Extra tools commonly share Tool::Pen, so their actual identity
        // change is visible here rather than in setTool(). Cancel while the
        // old property is still installed for an accurate hook message.
        cancelActiveOverlayDrag();
    }
    m_strokeProperty = property;
    m_activePythonTool = property.isEmpty() ? String() : String("extra");
    if (changed) {
        // The property is the script tool's identity, so a change of it is a
        // change of armed tool - announced like setTool's, so the colour
        // policy in Python sees both halves of "this tool is now armed".
        sendPythonHandleMessage(String("arm"), String(), PointF());
    }
    if (m_hasCurrentStroke) {
        m_currentStroke.property = property;
    }
}

void PaintOpenGLWidget::sendPythonExtraToolMessage(const String &name, const String &property)
{
    const String previousTool = m_activePythonTool;
    const String previousProperty = m_strokeProperty;
    m_activePythonTool = name.isEmpty() ? String("extra") : name;
    m_strokeProperty = property;
    pythonHookSendMessage(String("extra"));
    m_activePythonTool = previousTool;
    m_strokeProperty = previousProperty;
}

void PaintOpenGLWidget::sendPythonToolOptionMessage(const String &hook, const String &name, const String &type, const JsonVariant &value, int row, int startColumn, int endColumn)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("option"))) {
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
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["option"] = optionInfo;
    message["name"] = name.toStdString();
    message["type"] = type.toStdString();
    message["value"] = variantToPythonObject(value);
    message["hook"] = hook.toStdString();
    message["row"] = row;
    message["start_column"] = startColumn;
    message["end_column"] = endColumn;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)hook;
    (void)name;
    (void)type;
    (void)value;
    (void)row;
    (void)startColumn;
    (void)endColumn;
#endif
}

void PaintOpenGLWidget::sendPythonPadMessage(const String &pad, const String &phase, double x, double y)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("pad"))) {
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
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["pad"] = pad.toStdString();
    message["phase"] = phase.toStdString();
    message["value"] = pointToPythonDict(PointF(x, y));

    const String output = ::pythonHookSendMessage(message);
    // Pad moves stream continuously; quiet dispatches would flood the debug
    // dock with a line per mouse move.
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)pad;
    (void)phase;
    (void)x;
    (void)y;
#endif
}

bool PaintOpenGLWidget::sendPythonLayerVisibilityMessage(int layerIndex, bool visible)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("visibility"))) {
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
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["layer"] = layerIndex;
    message["visible"] = visible;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
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
    (void)layerIndex;
    (void)visible;
    return false;
#endif
}

void PaintOpenGLWidget::sendPythonViewButtonMessage(const String &name, bool on)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("viewbutton"))) {
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

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)name;
    (void)on;
#endif
}

void PaintOpenGLWidget::sendPythonLayerMenuMessage(const String &action,
                                                   int groupId,
                                                   const String &groupName,
                                                   int layerIndex,
                                                   const String &layerName,
                                                   const Vector<int> &memberLayers)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("layermenu"))) {
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

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)action;
    (void)groupId;
    (void)groupName;
    (void)layerIndex;
    (void)layerName;
    (void)memberLayers;
#endif
}

void PaintOpenGLWidget::sendPythonMenuMessage(const String &menu,
                                              const String &item,
                                              bool checked)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("menu"))) {
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

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)menu;
    (void)item;
    (void)checked;
#endif
}

bool PaintOpenGLWidget::sendPythonFillRequestMessage(const PointF &pos)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("fillrequest"))) {
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
    const RectF fillBounds = fillBoundsRect();
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
    message["delta"] = pointToPythonDict(PointF());
    message["fill_scope"] = (m_fillScope == FillScope::AllLayers ? "all" : "current");
    message["color"] = colorInfo;
    message["bounds"] = boundsInfo;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
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
    (void)pos;
    return false;
#endif
}

void PaintOpenGLWidget::setTool(Tool tool)
{
    if (tool != m_tool) {
        // Overlay moves are previewed in Python before release. Tell the
        // owning tool to restore its persisted baseline when a tool switch
        // interrupts that transaction.
        cancelActiveOverlayDrag();
    }
    if ((m_tool == Tool::Arrow || m_tool == Tool::Connect || m_tool == Tool::Transfer)
        && tool != m_tool) {
        // Leaving a handle-owning tool (Arrow's edit points, Connect's snap
        // hints and its accept/delete buttons) dismisses its handles - and
        // TELLS Python, so anything it drew into the overlay goes with them
        // AND its per-view session dies. The cancel fires even when no
        // handles happen to be up right now: Connect keeps a focused
        // connection whose handles vanish whenever the hover leaves every
        // snap target, and skipping the cancel then left a live session
        // whose recorded stroke index later edited an innocent stroke.
        sendPythonHandleMessage(String("cancel"), String(), PointF());
        m_editHandles.clear();
        m_activeHandleDrag.clear();
    }
    m_tool = tool;
    // EVERY tool announces that it was armed. Scripts hang tool-scoped state
    // off this: Transfer builds its box and its 8 grips, tool_colors.py hands
    // the tool back the colour it was last used with (a script tool that
    // paints itself pink must not repaint the pen).
    sendPythonHandleMessage(String("arm"), String(), PointF());
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    // A pointer named by the tool being LEFT must not survive into the next
    // one: the request belongs to a gesture, not to the widget.
    m_scriptCursor.clear();
    updateBrushCursor();   // only the pen wears a ring; everything else the arrow
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

void PaintOpenGLWidget::setAxisSnapThreshold(double threshold)
{
    m_axisSnapThreshold = std::max<double>(1.0, threshold);
}

double PaintOpenGLWidget::axisSnapThreshold() const
{
    return m_axisSnapThreshold;
}

void PaintOpenGLWidget::resetAxisSnap(KeyboardModifiers modifiers, const PointF &anchor)
{
    m_axisSnapAnchor = anchor;
    m_axisSnapAnchorIndex = m_points.isEmpty() ? 0 : int(m_points.size()) - 1;
    m_axisSnapState = (modifiers & (AltModifier | ShiftModifier))
                          ? AxisSnapState::Pending
                          : AxisSnapState::Inactive;
}

PointF PaintOpenGLWidget::applyAxisSnap(KeyboardModifiers modifiers, const PointF &point, bool *retroChanged)
{
    if (retroChanged) {
        *retroChanged = false;
    }

    if (!(modifiers & (AltModifier | ShiftModifier))) {
        m_axisSnapState = AxisSnapState::Inactive;
        return point;
    }

    if (m_axisSnapState == AxisSnapState::Inactive) {
        // Alt engaged mid-stroke: constrain from the latest drawn point on.
        resetAxisSnap(modifiers, m_points.isEmpty() ? point : PointF(m_points.last()));
    }

    if (m_axisSnapState == AxisSnapState::Pending) {
        const double dx = std::abs(point.x() - m_axisSnapAnchor.x());
        const double dy = std::abs(point.y() - m_axisSnapAnchor.y());
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
        return PointF(point.x(), m_axisSnapAnchor.y());
    }
    if (m_axisSnapState == AxisSnapState::Vertical) {
        return PointF(m_axisSnapAnchor.x(), point.y());
    }
    return point;
}

void PaintOpenGLWidget::armHoldStill(const PointF &anchor)
{
    m_holdStillAnchor = anchor;
    if (!m_holdStillTimer) {
        m_holdStillTimer = std::make_unique<Timer>();
        m_holdStillTimer->setSingleShot(true);
        m_holdStillTimer->timeout.connect([this]() { engageStraightLine(); });
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
    updateStraightLine(straightLineTip(m_lastMouseModifiers,
                                       m_hasHoverPos ? m_hoverPos : PointF(m_points.last())));
}

PointF PaintOpenGLWidget::straightLineTip(KeyboardModifiers modifiers,
                                           const PointF &cursor)
{
    if (!(modifiers & (AltModifier | ShiftModifier))) {
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
        const double dx = std::abs(cursor.x() - m_straightLineStart.x());
        const double dy = std::abs(cursor.y() - m_straightLineStart.y());
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
        return PointF(cursor.x(), m_straightLineStart.y());
    }
    if (m_axisSnapState == AxisSnapState::Vertical) {
        return PointF(m_straightLineStart.x(), cursor.y());
    }
    return cursor;
}

void PaintOpenGLWidget::updateStraightLine(PointF tip)
{
    if (!m_straightLineMode) {
        return;
    }
    if (LineF(m_straightLineStart, tip).length() <= 1e-9) {
        // Nothing to collapse to yet (held still from the very first sample).
        // Keeping the press's own dot avoids inventing a zero-length segment,
        // a shape no other code path in the app produces.
        return;
    }
    m_points.clear();
    m_points.append(m_straightLineStart);
    m_points.append(tip);
    Path path(m_straightLineStart);
    path.lineTo(tip);
    m_currentStroke = AnimeVectorLogic::makeStrokeFromPath(
        path, m_points, m_currentStroke.color, m_currentStroke.width, 0);
    m_currentStroke.property = m_strokeProperty;
    // Same throttled "update" contract the curved path honours, so a
    // subscriber never runs at mouse rate.
    if (!m_updateHookThrottle.isValid() || m_updateHookThrottle.elapsed() >= kUpdateHookIntervalMs) {
        m_updateHookThrottle.start();
        pythonHookSendMessage(String("update"));
    }
    update();
}

void PaintOpenGLWidget::setCurrentLayer(int layerIndex)
{
    m_model.setCurrentLayer(layerIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    notifyLayerChangedIfNeeded();
    update();
}

void PaintOpenGLWidget::setCurrentFrame(int frameIndex)
{
    if (frameIndex != m_model.currentFrame()) {
        // Cancel while the old frame is still current so Python can restore
        // the state against which the drag began.
        cancelActiveOverlayDrag();
    }
    m_model.setCurrentFrame(frameIndex);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    notifyFrameChangedIfNeeded();
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

String PaintOpenGLWidget::layerName(int layerIndex) const
{
    return m_model.layerName(layerIndex);
}

String PaintOpenGLWidget::frameName(int frameIndex) const
{
    return m_model.frameName(frameIndex);
}

String PaintOpenGLWidget::assetName(int assetIndex) const
{
    return m_model.assetName(assetIndex);
}

int PaintOpenGLWidget::importRasterLayer(const Image &image, const String &layerName)
{
    if (image.isNull()) {
        return -1;
    }

    // Centre on the PAGE. Centring on the widget put the image wherever the
    // window happened to be, so the same file landed somewhere else after a
    // dock was dragged.
    const PointF canvasCenter = documentRect().center();
    const PointF rasterCenter(image.width() * 0.5, image.height() * 0.5);
    const PointF topLeft = canvasCenter - rasterCenter;
    const int columnIndex = m_model.addRasterLayer(layerName, m_model.currentFrame(), image, topLeft);
    if (columnIndex >= 0) {
        m_points.clear();
        m_hasCurrentStroke = false;
        m_hasLastEraserPos = false;
        commitHistory(String("Import Raster"));
        update();
    }
    return columnIndex;
}

int PaintOpenGLWidget::importVectorLineLayer(const Vector<ImportedVectorFrame> &frames, const String &layerName)
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
            const double width = std::max(double(1.0), importedStroke.width);
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
    commitHistory(String("Import Vector Lines"));
    assetListChanged(assetIndex);
    layerListChanged(layerIndex);
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
        commitHistory(String("Add Layer"));
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
    commitHistory(String("Delete Layer"));
    // The columns shifted: the same current index can now be a different
    // column, which the id-aware baseline detects.
    notifyLayerChangedIfNeeded();
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
    commitHistory(deleted == 1 ? String("Delete Group")
                               : String("Delete Group (%1 layers)").arg(deleted));
    notifyLayerChangedIfNeeded();
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
    commitHistory(String("Move Layer"));
    notifyLayerChangedIfNeeded();
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
        commitHistory(String("Add Frame"));
    }
    update();
    return row;
}

int PaintOpenGLWidget::insertHoldFrameAfter(int row)
{
    const int inserted = m_model.insertHoldFrameAfter(row);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    if (inserted >= 0) {
        commitHistory(String("Add Hold Frame"));
    }
    update();
    return inserted;
}

int PaintOpenGLWidget::duplicateFrame(int row)
{
    const int duplicated = m_model.duplicateFrame(row);
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    if (duplicated >= 0) {
        commitHistory(String("Duplicate Frame"));
    }
    update();
    return duplicated;
}

bool PaintOpenGLWidget::deleteFrame(int frameIndex)
{
    if (!m_model.deleteFrame(frameIndex)) {
        return false;
    }

    commitHistory(String("Delete Frame"));
    update();
    return true;
}

bool PaintOpenGLWidget::moveFrame(int fromIndex, int toIndex)
{
    if (!m_model.moveFrame(fromIndex, toIndex)) {
        return false;
    }

    commitHistory(String("Move Frame"));
    update();
    return true;
}

AnimeSceneModel &PaintOpenGLWidget::model()
{
    return m_model;
}

int PaintOpenGLWidget::addAsset(AnimeColumnType type, const String &name)
{
    const int assetIndex = m_model.addAsset(type, name);
    if (assetIndex >= 0) {
        commitHistory(String("Add Asset"));
        assetListChanged(assetIndex);
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
        commitHistory(String("Assign Asset"));
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
        commitHistory(String("Add Layer for Asset"));
        update();
    }
    return layerIndex;
}

const AnimeSceneModel &PaintOpenGLWidget::model() const
{
    return m_model;
}

void PaintOpenGLWidget::setOverlayItems(const Vector<OverlayItem> &items)
{
    m_overlayItems = items;
    // A live overlay drag survives a re-push AS LONG AS its item is still in
    // the list (the dragged item re-pushes itself at its new position every
    // move); if the item vanished, the drag has nothing to move.
    if (!m_activeOverlayDrag.isEmpty()) {
        bool present = false;
        for (const OverlayItem &item : m_overlayItems) {
            if (item.id == m_activeOverlayDrag && item.draggable) {
                present = true;
                break;
            }
        }
        if (!present) {
            m_activeOverlayDrag.clear();
            stopOverlayDragMoveTimer();
        }
    }
    update();
}

void PaintOpenGLWidget::setEditHandles(const Vector<EditHandle> &handles)
{
    m_editHandles = handles;
    // Python may legitimately clear or rebuild the set mid-drag (it re-pushes
    // after every edit); the drag keeps its id and simply stops matching a
    // drawn handle if the set no longer contains it.
    update();
}

namespace {
constexpr double kEditHandleScreenPx = 9.0;   // drawn size
constexpr double kEditHandleHitPx = 7.0;      // half-size of the hit box
constexpr double kEditHandleButtonScale = 1.8; // button shapes (3, 4) grow by this
}

void PaintOpenGLWidget::setFieldViewSpec(const FieldViewSpec &spec)
{
    m_fieldViewSpec = spec;
    update();
}

void PaintOpenGLWidget::clearFieldViewSpec()
{
    if (!m_fieldViewSpec.active) {
        m_fieldViewSpec = FieldViewSpec();
        return;
    }
    m_fieldViewSpec = FieldViewSpec();
    update();
}

void PaintOpenGLWidget::setFieldLookup(
    std::function<std::uint64_t(const String &key)> versionLookup,
    std::function<const AnimeSceneModel *(const String &viewName)> sceneLookup)
{
    m_fieldVersionLookup = std::move(versionLookup);
    m_fieldSceneLookup = std::move(sceneLookup);
}

void PaintOpenGLWidget::paintEditHandles(Painter &painter)
{
    // SCREEN space: handles keep a constant size at any zoom, like every
    // vector editor's - a handle is a tool, not a mark on the paper.
    // Conversion via algorithm/viewscale.h, the shared wheel.
    for (const EditHandle &handle : m_editHandles) {
        const PointF screen = AnimeViewScale::toScreen(handle.pos, m_zoom, m_panOffset);
        const double half = kEditHandleScreenPx * 0.5;
        painter.setPen(Pen(Color(30, 30, 30, 230), 1.2));
        painter.setBrush(handle.color);
        switch (handle.shape) {
        case 1:
            painter.drawEllipse(screen, half, half);
            break;
        case 2: {
            const PointF diamond[4] = {
                screen + PointF(0.0, -half - 1.5), screen + PointF(half + 1.5, 0.0),
                screen + PointF(0.0, half + 1.5), screen + PointF(-half - 1.5, 0.0)};
            painter.drawPolygon(diamond, 4);
            break;
        }
        // Shapes 3 and 4 are BUTTONS (accept / delete), drawn bigger than
        // the point handles so a click target reads as one, with a matching
        // bigger hit box in editHandleAt.
        case 3: {
            const double button = half * kEditHandleButtonScale;
            painter.drawEllipse(screen, button, button);
            painter.setPen(Pen(Color(250, 250, 250, 245), 2.0));
            const PointF check[3] = {screen + PointF(-button * 0.5, 0.05 * button),
                                      screen + PointF(-button * 0.12, button * 0.45),
                                      screen + PointF(button * 0.55, -button * 0.45)};
            painter.drawPolyline(check, 3);
            break;
        }
        case 4: {
            const double button = half * kEditHandleButtonScale;
            painter.drawEllipse(screen, button, button);
            painter.setPen(Pen(Color(250, 250, 250, 245), 2.0));
            const double arm = button * 0.5;
            painter.drawLine(screen + PointF(-arm, -arm), screen + PointF(arm, arm));
            painter.drawLine(screen + PointF(-arm, arm), screen + PointF(arm, -arm));
            break;
        }
        default:
            painter.drawRect(RectF(screen.x() - half, screen.y() - half,
                                    kEditHandleScreenPx, kEditHandleScreenPx));
            break;
        }
    }
}

String PaintOpenGLWidget::editHandleAt(const PointF &screenPos) const
{
    // Last drawn is on top, so scan backwards.
    for (int i = m_editHandles.size() - 1; i >= 0; --i) {
        if (!m_editHandles[i].interactive) {
            continue;
        }
        // Same doc->screen mapping the painter used (algorithm/viewscale.h):
        // the pick box must sit exactly where the handle was drawn.
        const PointF screen =
            AnimeViewScale::toScreen(m_editHandles[i].pos, m_zoom, m_panOffset);
        const double hit = m_editHandles[i].shape >= 3
                              ? kEditHandleHitPx * kEditHandleButtonScale
                              : kEditHandleHitPx;
        if (std::abs(screen.x() - screenPos.x()) <= hit
            && std::abs(screen.y() - screenPos.y()) <= hit) {
            return m_editHandles[i].id;
        }
    }
    return String();
}

String PaintOpenGLWidget::sendPythonHandleMessage(const String &phase,
                                                  const String &handleId,
                                                  const PointF &pos,
                                                  KeyboardModifiers modifiers)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!animeanHookEventSubscribed(String("handle"))) {
        return String();
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
    message["delta"] = pointToPythonDict(PointF());
    message["phase"] = phase.toStdString();
    message["handle"] = handleId.toStdString();
    // The colour in force right now: the per-tool colour cache
    // (pyfile/tool_colors.py) records it the first time a tool is armed, so
    // a tool that never sets a colour of its own still keeps one instead of
    // inheriting whatever the previous tool painted with.
    py::dict currentColor;
    currentColor["r"] = m_penColor.red();
    currentColor["g"] = m_penColor.green();
    currentColor["b"] = m_penColor.blue();
    currentColor["a"] = m_penColor.alpha();
    message["color"] = currentColor;
    // Modifier keys ride along so a tool can constrain its own gesture the
    // way the pen constrains a stroke (aspect-locked scaling, axis-locked
    // translation). Any of Alt/Ctrl/Shift reads as "constrain" - which one
    // is a house convention, so the flags travel separately too.
    py::dict keys;
    keys["shift"] = bool(modifiers & ShiftModifier);
    keys["ctrl"] = bool(modifiers & ControlModifier);
    keys["alt"] = bool(modifiers & AltModifier);
    keys["constrain"] = bool(modifiers & (ShiftModifier | ControlModifier | AltModifier));
    message["modifiers"] = keys;
    // The perceptual key-point filter is zoom-dependent (the eye resolves a
    // fixed angular period, which maps through the zoom to document space).
    message["zoom"] = m_zoom;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }

    // A "pick" may CLAIM the gesture: Python answers with the id of the
    // thing it grabbed, and this press becomes a drag under that id.
    try {
        if (message.contains(py::str("grab"))) {
            const std::string grabbed = message[py::str("grab")].cast<std::string>();
            if (!grabbed.empty()) {
                return String::fromStdString(grabbed);
            }
        }
    } catch (const py::error_already_set &) {
    } catch (const py::cast_error &) {
    }
    return String();
#else
    (void)phase;
    (void)handleId;
    (void)modifiers;
    (void)pos;
    return String();
#endif
}

namespace {
void paintFieldColumnContent(Painter &painter,
                             const AnimeVectorImageModel &image,
                             double zoom,
                             double widthScale,
                             const StringList *skipProperties)
{
    painter.setPen(NoPen);
    for (const AnimeVectorFillRegion &fill : image.fillRegions()) {
        painter.setBrush(Color(fill.color));
        painter.drawPath(fill.path);
    }
    painter.setBrush(NoBrush);
    for (const AnimeVectorStrokeNode &node : image.strokeNodes()) {
        const AnimeVectorStroke &stroke = node.stroke;
        if (skipProperties && !stroke.property.isEmpty()
            && skipProperties->contains(String(stroke.property))) {
            continue;
        }
        const PenStyle penStyle =
            (stroke.penStyle >= SolidLine && stroke.penStyle <= DashDotDotLine)
                ? static_cast<PenStyle>(stroke.penStyle)
                : SolidLine;
        Pen pen(Color(stroke.color),
                AnimeVectorLogic::displayStrokeWidth(stroke.width * widthScale, zoom),
                penStyle, RoundCap, RoundJoin);
        painter.setPen(pen);
        painter.drawPath(stroke.path);
    }
}
} // namespace

// The layers a script asked for, drawn again inside a field scope: the page
// shows this stretch of the op list through the displacement field it holds.
void PaintOpenGLWidget::paintFieldScope(Painter &painter)
{
    if (!m_fieldViewSpec.active || m_fieldViewSpec.fieldKey.isEmpty()) {
        return;
    }
    RecordingDevice *device = recordingDevice();
    if (!device) {
        return;
    }
    const AnimeSceneModel *sourceModel = nullptr;
    if (m_fieldSceneLookup) {
        sourceModel = m_fieldSceneLookup(m_fieldViewSpec.sourceView);
    }
#ifdef ANIMEAN_WITH_PYTHON
    if (!sourceModel && !m_fieldViewSpec.sourceView.isEmpty()) {
        sourceModel = animeanUiSceneForViewName(m_fieldViewSpec.sourceView);
    }
#endif
    if (!sourceModel && (m_fieldViewSpec.sourceView.isEmpty() || m_fieldViewSpec.sourceView == m_viewName)) {
        sourceModel = &m_model;
    }

    const std::uint64_t version = m_fieldVersionLookup ? m_fieldVersionLookup(m_fieldViewSpec.fieldKey) : 0;
    Vector<FieldScopePass> passes;
    passes.reserve(m_fieldViewSpec.passes.size());
    for (const FieldPass &p : m_fieldViewSpec.passes) {
        FieldScopePass outPass;
        outPass.tagLo = p.tagLo;
        outPass.tagHi = p.tagHi;
        outPass.opacity = p.opacity;
        outPass.tintR = p.tintR;
        outPass.tintG = p.tintG;
        outPass.tintB = p.tintB;
        outPass.tintA = p.tintA;
        passes.append(outPass);
    }

    device->beginFieldScope(painter.state(),
                            m_fieldViewSpec.fieldKey.toStdString(),
                            version,
                            m_fieldViewSpec.rect,
                            m_fieldViewSpec.maxEdge,
                            passes);

    if (sourceModel) {
        const AnimeScene &sourceScene = sourceModel->scene();
        const double widthScale = m_fieldViewSpec.widthScale;
        const StringList *skipProps =
            m_fieldViewSpec.skipProperties.isEmpty() ? nullptr : &m_fieldViewSpec.skipProperties;
        for (int columnIndex : m_fieldViewSpec.columns) {
            if (columnIndex < 0 || columnIndex >= sourceScene.xsheet.columns.size()) {
                continue;
            }
            const AnimeColumn &column = sourceScene.xsheet.columns[columnIndex];
            const AnimeCell cell = column.cellAt(m_fieldViewSpec.frame);
            const VectorImageModel *image = sourceModel->imageForCell(cell);
            if (!image) {
                continue;
            }
            painter.setOpacity(column.opacity);
            paintFieldColumnContent(painter, *image, m_zoom, widthScale, skipProps);
        }
        painter.setOpacity(1.0);
    }

    device->endFieldScope();
}

void PaintOpenGLWidget::paintSceneContent(Painter &painter, int frameIndex, bool includeCurrentStroke,
                                          const SceneContentFilter *filter)
{
    const bool drawStrokes = !filter || filter->strokes;
    const bool drawGuideStrokes = !filter || filter->guideStrokes;
    const bool drawFills = !filter || filter->fills;
    const bool drawRaster = !filter || filter->raster;
    const StringList *guideProperties = filter ? filter->guideProperties : nullptr;
    const String *excludeLayerTag =
        (filter && filter->excludeLayerTag && !filter->excludeLayerTag->isEmpty())
            ? filter->excludeLayerTag
            : nullptr;
    const AnimeScene &scene = m_model.scene();
    Hash<std::int64_t, Image> nextRasterCache;
    const auto paintColumn = [&](int columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (!filter && m_fieldViewSpec.active && m_fieldViewSpec.hideColumns.contains(columnIndex)) {
            return;
        }
        if (!column.visible) {
            return;
        }
        // The LAYER gate, ahead of every content class: a column that sits in
        // a group carrying the excluded tag contributes nothing at all.
        if (excludeLayerTag && m_model.groupIdForLayer(columnIndex, *excludeLayerTag) > 0) {
            return;
        }

        const AnimeCell cell = column.cellAt(frameIndex);
        const VectorImageModel *image = m_model.imageForCell(cell);
        painter.setOpacity(column.opacity);
        if (image) {
            if (drawRaster && image->hasRaster()) {
                const AnimeRasterImage &raster = image->raster();
                // Reuse the same Image wrapper across frames so its Image::cacheKey()
                // stays stable for RecordingDevice's incremental prefix hash.
                const std::int64_t cacheKey = raster.image.cacheKey();
                Image rasterImage = m_rasterCache.value(cacheKey);
                if (rasterImage.isNull() && !raster.image.isNull()) {
                    rasterImage = Image(raster.image);
                }
                if (!rasterImage.isNull()) {
                    nextRasterCache.insert(cacheKey, rasterImage);
                }
                painter.drawImage(PointF(raster.topLeft), rasterImage);
            }
            if (drawFills) {
                painter.setPen(NoPen);
                for (const AnimeVectorFillRegion &fill : image->fillRegions()) {
                    painter.setBrush(Color(fill.color));
                    painter.drawPath(fill.path);
                }
            }
            painter.setBrush(NoBrush);
            for (const VectorStrokeNode &node : image->strokeNodes()) {
                const VectorStroke &stroke = node.stroke;
                const bool isGuide = guideProperties && !stroke.property.isEmpty()
                                     && guideProperties->contains(String(stroke.property));
                if (!(isGuide ? drawGuideStrokes : drawStrokes)) {
                    continue;
                }
                // penStyle is a generic per-stroke property (PenStyle).
                // Clamp out-of-range values to solid: 0 is NoPen, which would
                // silently make the stroke invisible.
                const PenStyle penStyle =
                    (stroke.penStyle >= SolidLine && stroke.penStyle <= DashDotDotLine)
                        ? static_cast<PenStyle>(stroke.penStyle)
                        : SolidLine;
                // Floored on-screen width: zoomed out, the raw width fades
                // into an invisible sub-pixel smear. Display only.
                Pen pen(Color(stroke.color),
                        AnimeVectorLogic::displayStrokeWidth(stroke.width, m_zoom),
                        penStyle, RoundCap, RoundJoin);
                painter.setPen(pen);
                painter.drawPath(stroke.path);
            }
        }

        if (includeCurrentStroke && drawStrokes && columnIndex == m_model.currentLayer()
            && m_hasCurrentStroke) {
            painter.setPen(Pen(Color(m_currentStroke.color),
                               AnimeVectorLogic::displayStrokeWidth(m_currentStroke.width, m_zoom),
                               SolidLine, RoundCap, RoundJoin));
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
    if (!filter) {
        m_rasterCache.swap(nextRasterCache);
    } else {
        for (auto it = nextRasterCache.cbegin(); it != nextRasterCache.cend(); ++it) {
            m_rasterCache.insert(it.key(), it.value());
        }
    }
    painter.setOpacity(1.0);
}

bool PaintOpenGLWidget::buildPlaybackCache(int frameCount, String *error)
{
    m_playbackFrames.clear();
    m_playbackIndex = -1;
    m_playbackActive = false;

    if (frameCount <= 0 || width() <= 0 || height() <= 0) {
        if (error) {
            *error = String("nothing to prerender");
        }
        return false;
    }

    const double ratio = devicePixelRatioF();
    const Size pixelSize(std::max(1, int(std::lround(width() * ratio))),
                          std::max(1, int(std::lround(height() * ratio))));
    const std::int64_t bytesPerFrame = std::int64_t(pixelSize.width()) * pixelSize.height() * 4;
    const std::int64_t budget = 512LL * 1024 * 1024;
    if (bytesPerFrame * frameCount > budget) {
        if (error) {
            *error = String("prerendering %1 frames at %2x%3 needs %4 MB (limit %5 MB)")
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
        Image image(pixelSize, Image::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        // CONTENT ONLY, on transparent. The background is painted live under
        // the blit instead of being baked in here: baked, it froze whatever
        // mode was current when playback started (so the Background menu did
        // nothing while playing), and because the cache is blitted through a
        // pan/zoom correction the chequer would have travelled with the
        // drawing instead of staying screen-space.
        image.fill(Color(0, 0, 0, 0));

        Painter painter(&image);
        painter.setRenderHint(Painter::Antialiasing);
        painter.setRenderHint(Painter::SmoothPixmapTransform);
        painter.translate(m_panOffset);
        painter.scale(m_zoom, m_zoom);
        if (!m_unboundedCanvas) {
            painter.fillRect(documentRect(), Color(255, 255, 255));
        }
        // Overlays and the in-progress stroke are editing aids, not content.
        SceneContentFilter filter;
        paintSceneContent(painter, frame, false, &filter);
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
        m_playbackCacheTimer = std::make_unique<Timer>();
        m_playbackCacheTimer->setSingleShot(true);
        m_playbackCacheTimer->timeout.connect([this]() {
            if (!m_playbackActive || m_playbackCacheFrameCount <= 0) {
                return;
            }
            if (fuzzyCompare(m_zoom, m_playbackCacheZoom)
                && m_panOffset == m_playbackCachePan) {
                return;   // the view came back to what the cache already holds
            }
            // Keep playing at the same frame: buildPlaybackCache resets the
            // index, and losing the user's position mid-playback would be a
            // worse artifact than the soft frame it is replacing.
            const int index = m_playbackIndex;
            String error;
            if (buildPlaybackCache(m_playbackCacheFrameCount, &error)) {
                m_playbackIndex = std::min(index, int(m_playbackFrames.size()) - 1);
                update();
            } else {
                // Out of budget at the new size: stop rather than keep
                // blitting a cache that no longer matches anything.
                playbackInterrupted();
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

void PaintOpenGLWidget::setOnionEnabled(bool enabled)
{
    if (m_onionEnabled == enabled) {
        return;
    }
    m_onionEnabled = enabled;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

bool PaintOpenGLWidget::onionEnabled() const
{
    return m_onionEnabled;
}

void PaintOpenGLWidget::setOnionFrames(const Set<int> &frames)
{
    if (m_onionFrames == frames) {
        return;
    }
    m_onionFrames = frames;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

Set<int> PaintOpenGLWidget::onionFrames() const
{
    return m_onionFrames;
}

void PaintOpenGLWidget::setOnionGuideLines(bool include)
{
    if (m_onionGuideLines == include) {
        return;
    }
    m_onionGuideLines = include;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

bool PaintOpenGLWidget::onionGuideLines() const
{
    return m_onionGuideLines;
}

void PaintOpenGLWidget::setOnionLines(bool include)
{
    if (m_onionLines == include) {
        return;
    }
    m_onionLines = include;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

bool PaintOpenGLWidget::onionLines() const
{
    return m_onionLines;
}

void PaintOpenGLWidget::setOnionFills(bool include)
{
    if (m_onionFills == include) {
        return;
    }
    m_onionFills = include;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

bool PaintOpenGLWidget::onionFills() const
{
    return m_onionFills;
}

void PaintOpenGLWidget::setOnionAmLayers(bool include)
{
    if (m_onionAmLayers == include) {
        return;
    }
    m_onionAmLayers = include;
    clearOnionCache();
    notifyOnionChangedIfNeeded();
    update();
}

bool PaintOpenGLWidget::onionAmLayers() const
{
    return m_onionAmLayers;
}

void PaintOpenGLWidget::setOnionExcludeProperties(const StringList &properties)
{
    if (m_onionExcludeProperties == properties) {
        return;
    }
    m_onionExcludeProperties = properties;
    clearOnionCache();
    update();
}

StringList PaintOpenGLWidget::onionExcludeProperties() const
{
    return m_onionExcludeProperties;
}

void PaintOpenGLWidget::setOnionLayerTag(const String &tag)
{
    if (m_onionLayerTag == tag) {
        return;
    }
    m_onionLayerTag = tag;
    clearOnionCache();
    update();
}

String PaintOpenGLWidget::onionLayerTag() const
{
    return m_onionLayerTag;
}

void PaintOpenGLWidget::notifyOnionChangedIfNeeded()
{
#ifdef ANIMEAN_WITH_PYTHON
    // A ghost is a rendering of the LAYER STACK, so everything a Python tool
    // draws through ui.set_overlay (auto_mapping's H/V axes, additional lines)
    // is missing from it - Guide Line alone can therefore change nothing
    // visible. This message is the mechanism that lets the owning script draw
    // its own ghost overlays: it reports the ghost set and where the playhead
    // sits, and the script decides what that means.
    const bool enabled = m_onionEnabled;
    // OFF collapses to one canonical baseline so scrubbing frames on a board
    // with no ghosts never crosses the language boundary.
    const bool guides = enabled && m_onionGuideLines;
    const bool lines = enabled && m_onionLines;
    const bool fills = enabled && m_onionFills;
    const bool amLayers = enabled && m_onionAmLayers;
    const int current = enabled ? m_model.currentFrame() : -1;
    Vector<int> frames;
    if (enabled) {
        frames = m_onionFrames.values();
        std::sort(frames.begin(), frames.end());
    }
    if (m_pythonNotifiedOnionValid
        && m_pythonNotifiedOnionEnabled == enabled
        && m_pythonNotifiedOnionGuides == guides
        && m_pythonNotifiedOnionLines == lines
        && m_pythonNotifiedOnionFills == fills
        && m_pythonNotifiedOnionAmLayers == amLayers
        && m_pythonNotifiedOnionCurrent == current
        && m_pythonNotifiedOnionFrames == frames) {
        return;
    }
    m_pythonNotifiedOnionValid = true;
    m_pythonNotifiedOnionEnabled = enabled;
    m_pythonNotifiedOnionGuides = guides;
    m_pythonNotifiedOnionLines = lines;
    m_pythonNotifiedOnionFills = fills;
    m_pythonNotifiedOnionAmLayers = amLayers;
    m_pythonNotifiedOnionCurrent = current;
    m_pythonNotifiedOnionFrames = frames;

    if (!animeanHookEventSubscribed(String("onion"))) {
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

    py::list frameList;
    for (int frame : frames) {
        frameList.append(frame);
    }

    py::dict message;
    message["event"] = "onion";
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["enabled"] = enabled;
    message["guides"] = guides;
    message["lines"] = lines;
    message["fills"] = fills;
    message["am_layers"] = amLayers;
    message["frames"] = frameList;
    message["current"] = frameRow;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#endif
}

void PaintOpenGLWidget::clearOnionCache()
{
    m_onionCache.clear();
}

void PaintOpenGLWidget::scheduleOnionCacheRefresh()
{
    if (!m_onionCacheTimer) {
        m_onionCacheTimer = std::make_unique<Timer>();
        m_onionCacheTimer->setSingleShot(true);
        m_onionCacheTimer->timeout.connect([this]() {
            if (!m_onionEnabled || m_onionCache.isEmpty()) {
                return;
            }
            if (fuzzyCompare(m_zoom, m_onionCacheZoom)
                && m_panOffset == m_onionCachePan
                && size() == m_onionCacheSize) {
                return;   // the view came back to what the cache already holds
            }
            clearOnionCache();
            update();
        });
    }
    // Restarted on every tick, so a long gesture re-renders once at its end.
    m_onionCacheTimer->start(180);
}

int PaintOpenGLWidget::maxOnionGhosts() const
{
    const double ratio = devicePixelRatioF();
    const std::int64_t bytesPerGhost =
        std::max<std::int64_t>(1, std::int64_t(std::max(1, int(std::lround(width() * ratio))))
                                * std::max(1, int(std::lround(height() * ratio))) * 4);
    // Half the playback cache's budget: the two can be live at the same time,
    // and the ghosts are the set the user did not ask to have prerendered.
    const std::int64_t budget = 256LL * 1024 * 1024;
    return int(std::min<std::int64_t>(std::max<std::int64_t>(1, budget / bytesPerGhost), kMaxOnionGhosts));
}

Image PaintOpenGLWidget::onionGhost(int frameIndex, bool past)
{
    const auto cached = m_onionCache.constFind(frameIndex);
    if (cached != m_onionCache.constEnd() && cached->past == past) {
        return cached->image;
    }

    if (m_onionCache.isEmpty()) {
        // First ghost of a fresh set fixes the transform the whole set shares.
        m_onionCachePan = m_panOffset;
        m_onionCacheZoom = m_zoom;
        m_onionCacheSize = size();
    } else if (cached == m_onionCache.constEnd() && m_onionCache.size() >= maxOnionGhosts()) {
        // Re-tinting a frame already in the set must not evict the set: the
        // playhead crossing one ghost would otherwise re-render all of them.
        m_onionCache.clear();
        m_onionCachePan = m_panOffset;
        m_onionCacheZoom = m_zoom;
        m_onionCacheSize = size();
    }

    const double ratio = devicePixelRatioF();
    const Size pixelSize(std::max(1, int(std::lround(m_onionCacheSize.width() * ratio))),
                          std::max(1, int(std::lround(m_onionCacheSize.height() * ratio))));
    Image image(pixelSize, Image::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        // A failed allocation must not be remembered: a cached null would keep
        // this frame invisible until something else drops the whole set.
        return Image();
    }
    image.setDevicePixelRatio(ratio);
    image.fill(Color(0, 0, 0, 0));

    Painter painter(&image);
    painter.setRenderHint(Painter::Antialiasing);
    painter.setRenderHint(Painter::SmoothPixmapTransform);
    painter.translate(m_onionCachePan);
    painter.scale(m_onionCacheZoom, m_onionCacheZoom);
    // No page fill: a ghost is artwork, and a white ground would bury the
    // frame it is supposed to sit under.
    SceneContentFilter filter;
    // Raster content is drawn artwork, so it answers to LINE rather than
    // carrying a switch of its own.
    filter.strokes = m_onionLines;
    filter.raster = m_onionLines;
    filter.fills = m_onionFills;
    filter.guideProperties = &m_onionExcludeProperties;
    filter.guideStrokes = m_onionGuideLines;
    // AM LAYER is a gate on the LAYER, not on a content class: off, an
    // auto-mapping layer's columns leave the ghost entirely, whatever LINE
    // and FILL say. With no tag registered there is nothing to gate.
    if (!m_onionAmLayers) {
        filter.excludeLayerTag = &m_onionLayerTag;
    }
    paintSceneContent(painter, frameIndex, false, &filter);
    // One flat tint: a ghost says WHEN, so the ink's own colours would only
    // compete with the frame that is being drawn.
    painter.setWorldTransform(Transform());
    painter.setCompositionMode(Painter::CompositionMode_SourceIn);
    painter.fillRect(Rect(Point(0, 0), m_onionCacheSize),
                     AnimeTheme::color(past ? AnimeTheme::Role::OnionPast
                                            : AnimeTheme::Role::OnionAhead));
    painter.end();

    OnionGhostImage ghost;
    ghost.image = image;
    ghost.past = past;
    m_onionCache.insert(frameIndex, ghost);
    return ghost.image;
}

void PaintOpenGLWidget::paintOnionGhosts(Painter &painter)
{
    // Playback already shows every frame in turn; ghosting on top of it would
    // be noise, and the prerender pass has the pixels anyway.
    if (!m_onionEnabled || m_playbackActive || m_onionFrames.isEmpty()) {
        return;
    }
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const int current = m_model.currentFrame();
    const int frameCount = m_model.frameCount();
    Vector<int> frames;
    frames.reserve(m_onionFrames.size());
    for (int frame : m_onionFrames) {
        if (frame >= 0 && frame < frameCount && frame != current) {
            frames.append(frame);
        }
    }
    if (frames.isEmpty()) {
        return;
    }
    std::sort(frames.begin(), frames.end(), [current](int a, int b) {
        const int da = std::abs(a - current);
        const int db = std::abs(b - current);
        if (da != db) {
            return da < db;
        }
        return a < b;
    });
    const int ghostBudget = maxOnionGhosts();
    if (frames.size() > ghostBudget) {
        // Each ghost is a full-viewport image; past this many the memory
        // costs more than the extra frames say, and the ones dropped are the
        // faintest anyway.
        frames.resize(ghostBudget);
    }
    // Far to near, so the frame nearest the playhead lands on top.
    std::reverse(frames.begin(), frames.end());

    const bool stale = !m_onionCache.isEmpty()
                       && (!fuzzyCompare(m_zoom, m_onionCacheZoom)
                           || m_panOffset != m_onionCachePan
                           || size() != m_onionCacheSize);
    if (stale) {
        scheduleOnionCacheRefresh();
    }

    painter.save();
    // The ghosts carry their own transform, so they are blitted in widget
    // space; while the cache is stale they are re-mapped onto the current
    // view exactly the way the playback cache is.
    painter.setWorldTransform(Transform());
    const double scale = m_onionCacheZoom > 0.0 ? m_zoom / m_onionCacheZoom : 1.0;
    if (stale) {
        painter.translate(m_panOffset - m_onionCachePan * scale);
        painter.scale(scale, scale);
    }
    for (int frame : frames) {
        const Image ghost = onionGhost(frame, frame < current);
        if (ghost.isNull()) {
            continue;
        }
        const int distance = std::abs(frame - current);
        painter.setOpacity(std::max<double>(0.12, 0.42 / (1.0 + 0.35 * (distance - 1))));
        painter.drawImage(PointF(0.0, 0.0), ghost);
    }
    painter.restore();
}

Image PaintOpenGLWidget::renderFrameThumbnail(int frameIndex, const Size &size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return Image();
    }
    Image image(size, Image::Format_ARGB32_Premultiplied);
    image.fill(Color(255, 255, 255));
    if (frameIndex < 0 || frameIndex >= m_model.frameCount()) {
        return image;
    }

    const RectF page = documentRect();
    if (page.width() <= 0.0 || page.height() <= 0.0) {
        return image;
    }
    const double scale = std::min(size.width() / page.width(), size.height() / page.height());

    Painter painter(&image);
    painter.setRenderHint(Painter::Antialiasing);
    painter.setRenderHint(Painter::SmoothPixmapTransform);
    painter.translate((size.width() - page.width() * scale) * 0.5,
                      (size.height() - page.height() * scale) * 0.5);
    painter.scale(scale, scale);
    painter.translate(-page.topLeft());
    // Stroke widths are floored against the magnification they will be SEEN
    // at, and that is the thumbnail's, not the board's.
    const double previousZoom = m_zoom;
    m_zoom = scale;
    SceneContentFilter filter;
    paintSceneContent(painter, frameIndex, false, &filter);
    m_zoom = previousZoom;
    painter.end();
    return image;
}

void PaintOpenGLWidget::paintGL()
{
    Painter painter(this);
    painter.setRenderHint(Painter::Antialiasing);
    // Bilinear filtering for every image drawn under the zoom transform -
    // raster layers and the playback cache. Without it a scaled image is
    // sampled nearest-neighbour and pixelates the moment zoom leaves 1.0.
    painter.setRenderHint(Painter::SmoothPixmapTransform);

    if (m_playbackActive && m_playbackIndex >= 0 && m_playbackIndex < m_playbackFrames.size()) {
        // Always clear first: the widget can be larger than the cached frame
        // (a resize ends playback, but the repaint may arrive before that),
        // and uncovered pixels of a recreated FBO are undefined.
        if (m_unboundedCanvas) {
            paintBackground(painter, RectF(rect()));
        } else {
            painter.fillRect(rect(), AnimeTheme::surroundFor(kPaperColor));
        }
        // Map the cached pixels from the view they were rendered for onto the
        // view we are looking at now. A cache pixel p is document
        // (p - cachePan)/cacheZoom, which is on screen at
        // pan + doc*zoom - so one scale and one translate covers it.
        // Without this the frame could only ever be blitted 1:1, which is why
        // zooming used to have to drop out of playback.
        const double scale = m_playbackCacheZoom > 0.0 ? m_zoom / m_playbackCacheZoom : 1.0;
        if (!fuzzyCompare(scale, double(1.0)) || m_panOffset != m_playbackCachePan) {
            painter.save();
            painter.setRenderHint(Painter::SmoothPixmapTransform, true);
            painter.translate(m_panOffset - m_playbackCachePan * scale);
            painter.scale(scale, scale);
            painter.drawImage(PointF(0.0, 0.0), m_playbackFrames[m_playbackIndex]);
            painter.restore();
        } else {
            painter.drawImage(PointF(0.0, 0.0), m_playbackFrames[m_playbackIndex]);
        }
        paintActiveIndicator(painter);
        return;
    }

    if (m_unboundedCanvas) {
        // Infinite reference board: the whole viewport is paper.
        paintBackground(painter, rect());
    } else {
        painter.fillRect(rect(), AnimeTheme::surroundFor(kPaperColor));
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_zoom, m_zoom);
    if (!m_unboundedCanvas) {
        const RectF page = documentRect();
        // The paper is white in both themes: it is the artwork's ground, not
        // chrome, and its colour is what the drawing is composed against.
        painter.fillRect(page, kPaperColor);
        // The page can now be smaller than the viewport, so say where it ends.
        painter.setBrush(NoBrush);
        painter.setPen(Pen(AnimeTheme::color(AnimeTheme::Role::Divider), 1.0 / m_zoom));
        painter.drawRect(page);
    }

    // Ghosts sit on the paper and under the frame being drawn.
    paintOnionGhosts(painter);

    paintSceneContent(painter, m_model.currentFrame(), true);
    paintFieldScope(painter);

    // Predictive preview: a thin, translucent, UNFILTERED line from the
    // stabilized tip to the physical pen tip. The stabilized ink trails the
    // pen by whatever lag compensation could not cancel; this line covers
    // that gap so the eye reads "the ink is at the tip" while the real
    // stroke settles in behind it. Never part of the document.
    // Straight mode has no gap to cover - its end IS the cursor - so the
    // preview would only draw a second line on top of the first.
    if (m_hasCurrentStroke && m_hasRawPenPos && !m_points.isEmpty()
        && m_inputFilter.active() && !m_straightLineMode) {
        const PointF tip = m_points.last();
        // Under an axis lock the ink is constrained to the axis; a preview
        // pointing at the free cursor would wag a wrong-direction tail off
        // the locked line, so the target is projected onto the lock too.
        PointF previewTarget = m_rawPenPos;
        if (m_axisSnapState == AxisSnapState::Horizontal) {
            previewTarget.setY(m_axisSnapAnchor.y());
        } else if (m_axisSnapState == AxisSnapState::Vertical) {
            previewTarget.setX(m_axisSnapAnchor.x());
        }
        if (LineF(tip, previewTarget).length() > 0.5) {
            Color preview = m_currentStroke.color;
            preview.setAlpha(110);
            const double previewWidth =
                std::max<double>(0.75 / m_zoom, m_currentStroke.width * 0.35);
            painter.setBrush(NoBrush);
            painter.setPen(Pen(preview, previewWidth, SolidLine, RoundCap));
            painter.drawLine(tip, previewTarget);
        }
    }

    paintOverlayItems(painter);

    if ((m_tool == Tool::Eraser || m_tool == Tool::DeleteLine || m_tool == Tool::CutLine
         || m_tool == Tool::Connect)
        && m_hasHoverPos) {
        painter.setPen(Pen(Color(220, 0, 180),
                           AnimeVectorLogic::displayStrokeWidth(1.5, m_zoom),
                           DashLine));
        painter.setBrush(NoBrush);
        painter.drawEllipse(m_hoverPos, m_eraserRadius, m_eraserRadius);
    }

    // The pen's ring is normally the CURSOR itself (updateBrushCursor), which
    // cannot lag the pointer and needs no repaint. This is the fallback for
    // when it grows past what a cursor bitmap may be: the pointer is blanked
    // and the ring is drawn here instead, in document units like the ink.
    if (m_tool == Tool::Pen && m_hasHoverPos && m_penRingOnCanvas) {
        const double radius = m_penWidth * 0.5;
        painter.setPen(Pen(Color(40, 40, 40, 170),
                           AnimeVectorLogic::displayStrokeWidth(0.75, m_zoom)));
        painter.setBrush(NoBrush);
        painter.drawEllipse(m_hoverPos, radius, radius);
    }

    painter.restore();

    // Edit handles live in screen space, above everything on the paper.
    paintEditHandles(painter);

    paintActiveIndicator(painter);
}

// The active-view indicator hugs the viewport, not the document. It stays on
// the active board when focus leaves, but only lights up while keys reach it.
void PaintOpenGLWidget::paintActiveIndicator(Painter &painter) const
{
    if (!m_activeIndicator) {
        return;
    }
    painter.setBrush(NoBrush);
    painter.setPen(Pen(AnimeTheme::color(m_hasInputFocus ? AnimeTheme::Role::Accent : AnimeTheme::Role::BoardFrameIdle), 3.0));
    painter.drawRect(RectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
}

void PaintOpenGLWidget::paintOverlayItems(Painter &painter)
{
    m_overlayHandles.clear();

    for (const OverlayItem &item : m_overlayItems) {
        if (item.points.size() < 2) {
            continue;
        }

        Path path(item.points.first());
        for (int i = 1; i < item.points.size(); ++i) {
            path.lineTo(item.points[i]);
        }
        if (item.closed) {
            path.closeSubpath();
            painter.setBrush(item.fillColor);
        } else {
            painter.setBrush(NoBrush);
        }
        const PenStyle overlayStyle =
            (item.penStyle >= SolidLine && item.penStyle <= DashDotDotLine)
                ? static_cast<PenStyle>(item.penStyle)
                : SolidLine;   // 0 is NoPen: never let a guide vanish
        painter.setPen(Pen(item.strokeColor,
                           AnimeVectorLogic::displayStrokeWidth(item.width, m_zoom),
                           overlayStyle, RoundCap, RoundJoin));
        painter.drawPath(path);

        if (item.removable || item.confirmable) {
            // ONE badge set per id, not per item. An id can legitimately
            // arrive as several items: a mapping area detected around a shape
            // with a hole comes back as one polygon per subpath. Later parts
            // only widen the extent that this id's x/check set sits on.
            int existing = -1;
            for (int i = 0; i < m_overlayHandles.size(); ++i) {
                if (m_overlayHandles[i].id == item.id) {
                    existing = i;
                    break;
                }
            }
            if (existing >= 0) {
                for (OverlayHandle &handle : m_overlayHandles) {
                    if (handle.id != item.id) {
                        continue;
                    }
                    handle.extent |= path.boundingRect();
                    if (handle.anchorIsExtent) {
                        handle.rect = overlayHandleRect(handle.extent.topRight(),
                                                        handle.accept ? 1 : 0,
                                                        item.confirmable ? 2 : 1);
                    }
                }
                continue;
            }
            const auto appendHandle = [&](bool accept) {
                OverlayHandle handle;
                handle.id = item.id;
                handle.badgeColor = item.strokeColor;
                handle.extent = path.boundingRect();
                handle.accept = accept;
                // A pending guide asks for its check/x pair in the upper-right
                // corner. Ordinary open removable guides retain their old
                // end-point badge position.
                handle.anchorIsExtent = item.closed || item.confirmable;
                handle.rect = overlayHandleRect(handle.anchorIsExtent
                                                    ? handle.extent.topRight()
                                                    : item.points.last(),
                                                accept ? 1 : 0,
                                                item.confirmable ? 2 : 1);
                m_overlayHandles.append(handle);
            };
            if (item.removable) {
                appendHandle(false);
            }
            if (item.confirmable) {
                appendHandle(true);
            }
        }
    }

    struct LabelsAtExit {
        const PaintOpenGLWidget *self;
        Painter &painter;
        ~LabelsAtExit() { self->paintOverlayLabels(painter); }
    } labelsAtExit{this, painter};
    for (const OverlayHandle &handle : m_overlayHandles) {
        Color badge = handle.badgeColor;
        badge.setAlpha(215);
        painter.setPen(NoPen);
        painter.setBrush(badge);
        painter.drawRoundedRect(handle.rect, 3.0, 3.0);

        const RectF inner = handle.rect.adjusted(4.0, 4.0, -4.0, -4.0);
        painter.setPen(Pen(Color(255, 255, 255), 1.8, SolidLine, RoundCap));
        if (handle.accept) {
            painter.drawLine(PointF(inner.left(), inner.center().y()),
                             PointF(inner.center().x() - 1.0, inner.bottom()));
            painter.drawLine(PointF(inner.center().x() - 1.0, inner.bottom()),
                             inner.topRight());
        } else {
            painter.drawLine(inner.topLeft(), inner.bottomRight());
            painter.drawLine(inner.topRight(), inner.bottomLeft());
        }
    }
}

void PaintOpenGLWidget::paintOverlayLabels(Painter &painter) const
{
    constexpr int kFontPx = 12;
    constexpr double kPadding = 6.0;
    constexpr double kTail = 9.0;
    constexpr double kEdge = 4.0;

    // In screen pixels, so a note stays readable at every zoom.
    painter.save();
    painter.setWorldTransform(Transform());
    const TextMetrics metrics = textMetrics(kFontPx);
    const RectF board = RectF(rect()).adjusted(kEdge, kEdge, -kEdge, -kEdge);

    for (const OverlayItem &item : m_overlayItems) {
        if (item.label.isEmpty() || item.points.isEmpty()) {
            continue;
        }
        const StringList lines = item.label.split(Latin1Char('\n'));
        double width = 0.0;
        for (const String &line : lines) {
            width = std::max(width, textWidth(line, kFontPx));
        }
        const SizeF size(width + 2.0 * kPadding,
                         lines.size() * metrics.height + 2.0 * kPadding);
        const PointF anchor = AnimeViewScale::toScreen(item.points.first(), m_zoom, m_panOffset);

        // Below the point, or above it when the board's bottom edge is in the way:
        // an item's x badge sits up and to the right of its end point, and a
        // bubble on top of it would hide a badge that still takes the click.
        RectF box(PointF(anchor.x() - size.width() / 2.0, anchor.y() + kTail), size);
        if (box.bottom() > board.bottom()) {
            box.moveBottom(anchor.y() - kTail);
        }
        box.moveLeft(std::clamp(box.left(), board.left(),
                                std::max(board.left(), board.right() - box.width())));
        box.moveTop(std::clamp(box.top(), board.top(),
                               std::max(board.top(), board.bottom() - box.height())));

        Path bubble;
        bubble.addRoundedRect(box, 5.0, 5.0);
        const double inset = std::min(2.0 * kTail, box.width() / 2.0);
        const double foot = std::clamp(anchor.x(), box.left() + inset, box.right() - inset);
        const double edge = anchor.y() >= box.center().y() ? box.bottom() - 1.0 : box.top() + 1.0;
        Path tail;
        tail.addPolygon(Vector<PointF>({PointF(foot - kTail / 2.0, edge),
                                        PointF(foot + kTail / 2.0, edge), anchor}));
        tail.closeSubpath();
        painter.setPen(Pen(item.strokeColor, item.width));
        painter.setBrush(item.fillColor);
        painter.drawPath(united(bubble, tail));

        painter.setPen(NoPen);
        painter.setBrush(item.fillColor.lightness() < 128 ? Color(255, 255, 255) : Color(0, 0, 0));
        double baseline = box.top() + kPadding + metrics.ascent;
        for (const String &line : lines) {
            painter.drawPath(textPath(line, kFontPx, PointF(box.left() + kPadding, baseline)));
            baseline += metrics.height;
        }
    }
    painter.restore();
}

RectF PaintOpenGLWidget::overlayHandleRect(const PointF &anchor, int slot, int slotCount) const
{
    // Just above and to the right of the anchor; slot 1 (the check badge)
    // sits one step to the left of slot 0 (the x badge).
    const double slotStep = kOverlayHandleSize + 4.0;
    RectF handleRect(anchor.x() + 4.0 - slot * slotStep,
                      anchor.y() - kOverlayHandleSize - 4.0,
                      kOverlayHandleSize,
                      kOverlayHandleSize);
    // The rect is built, drawn and hit-tested in DOCUMENT coordinates, so it
    // must be clamped against the visible viewport expressed in document
    // space (at zoom 1 on a bounded view this equals the old widget-pixel
    // clamp). Off-view items keep a reachable badge at the viewport edge.
    // The slot offset is part of the UNCLAMPED layout and the clamp bounds
    // shift per slot, so a badge pair pinned to either edge stays side by
    // side and fully visible - clamping first and offsetting afterwards
    // pushed a left-edge check badge outside the viewport, leaving the
    // pending transaction cancellable but never confirmable.
    const PointF docTopLeft = mapToDocument(PointF(0.0, 0.0));
    const PointF docBottomRight = mapToDocument(PointF(width(), height()));
    const double maxRight = docBottomRight.x() - 2.0 - slot * slotStep;
    const double minLeft = docTopLeft.x() + 2.0 + (slotCount - 1 - slot) * slotStep;
    if (handleRect.right() > maxRight) {
        handleRect.moveRight(maxRight);
    }
    if (handleRect.left() < minLeft) {
        handleRect.moveLeft(minLeft);
    }
    if (handleRect.top() < docTopLeft.y() + 2.0) {
        handleRect.moveTop(docTopLeft.y() + 2.0);
    }
    if (handleRect.bottom() > docBottomRight.y() - 2.0) {
        handleRect.moveBottom(docBottomRight.y() - 2.0);
    }
    return handleRect;
}

int PaintOpenGLWidget::overlayActionHandleAt(const PointF &pos) const
{
    for (int i = m_overlayHandles.size() - 1; i >= 0; --i) {
        if (m_overlayHandles[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool PaintOpenGLWidget::overlayActionItemAt(const PointF &pos)
{
    const int index = overlayActionHandleAt(pos);
    if (index < 0) {
        return false;
    }
    const OverlayHandle &handle = m_overlayHandles[index];
    sendOverlayActionMessage(handle.id,
                             handle.accept ? String("accept")
                                           : String("remove"));
    return true;
}

String PaintOpenGLWidget::draggableOverlayItemAt(const PointF &screenPos,
                                                  double *screenDistance) const
{
    // Same grab range as the edit handles, measured in SCREEN pixels so the
    // item stays grabbable when zoomed out. Distance is to the item's
    // segments, not just its vertices - a sparse ring must not have holes in
    // its hit area.
    const double hit = kEditHandleHitPx;
    for (int i = m_overlayItems.size() - 1; i >= 0; --i) {
        const OverlayItem &item = m_overlayItems[i];
        if (!item.draggable || item.points.size() < 2) {
            continue;
        }
        const int count = item.points.size() + (item.closed ? 1 : 0);
        double bestSq = std::numeric_limits<double>::max();
        for (int k = 0; k + 1 < count; ++k) {
            const PointF a = AnimeViewScale::toScreen(
                item.points[k % item.points.size()], m_zoom, m_panOffset);
            const PointF b = AnimeViewScale::toScreen(
                item.points[(k + 1) % item.points.size()], m_zoom, m_panOffset);
            const PointF delta = b - a;
            const double lengthSq = delta.x() * delta.x() + delta.y() * delta.y();
            double t = 0.0;
            if (lengthSq > 0.0) {
                t = ((screenPos.x() - a.x()) * delta.x()
                     + (screenPos.y() - a.y()) * delta.y()) / lengthSq;
                t = std::min(1.0, std::max(0.0, t));
            }
            const PointF closest = a + delta * t;
            const PointF gap = screenPos - closest;
            bestSq = std::min(bestSq, gap.x() * gap.x() + gap.y() * gap.y());
        }
        if (bestSq <= hit * hit) {
            if (screenDistance) {
                *screenDistance = std::sqrt(bestSq);
            }
            return item.id;
        }
    }
    return String();
}

bool PaintOpenGLWidget::pressOverlayOrBadge(const PointF &screenPos,
                                            const PointF &docPos,
                                            KeyboardModifiers modifiers)
{
    // A press can land on an action badge AND within a draggable overlay
    // item's grab band at once, and each fixed order has a real failure:
    // badge-first lets the badge's document-space rect (large on screen at
    // high zoom, clamped over content at the viewport edge) swallow the grab
    // and fire accept/remove on the guide the user meant to move, while
    // draggable-first makes a low-zoom badge sitting on its own guide
    // permanently unclickable. So the tie is broken by how deep INSIDE the
    // drawn badge the pointer is (screen px): the inner half of the glyph is
    // always a deliberate badge click, and on the outer fringe the badge
    // only wins when the pointer is deeper into it than it is close to the
    // draggable geometry. (Comparing against the badge's centre point
    // instead handicapped the badge by up to half its size and left the
    // glyph's line-facing strip dead at every zoom.)
    const int badgeIndex = overlayActionHandleAt(docPos);
    double dragDistance = 0.0;
    const String overlayId = draggableOverlayItemAt(screenPos, &dragDistance);
    bool badgeWins = badgeIndex >= 0;
    if (badgeWins && !overlayId.isEmpty()) {
        const RectF badgeRect = m_overlayHandles[badgeIndex].rect;
        const double depthDoc = std::min(
            std::min(docPos.x() - badgeRect.left(), badgeRect.right() - docPos.x()),
            std::min(docPos.y() - badgeRect.top(), badgeRect.bottom() - docPos.y()));
        const double depthScreen = depthDoc * m_zoom;
        const double halfScreen = 0.5 * std::min(badgeRect.width(), badgeRect.height()) * m_zoom;
        const PointF offCentre = (docPos - badgeRect.center()) * m_zoom;
        badgeWins = std::max(std::abs(offCentre.x()), std::abs(offCentre.y())) <= kOverlayBadgeSurePx
                    || depthScreen >= 0.5 * halfScreen || depthScreen >= dragDistance;
    }
    if (badgeWins) {
        const OverlayHandle &handle = m_overlayHandles[badgeIndex];
        sendOverlayActionMessage(handle.id,
                                 handle.accept ? String("accept")
                                               : String("remove"));
        return true;
    }
    if (!overlayId.isEmpty()) {
        stopOverlayDragMoveTimer();
        m_activeOverlayDrag = overlayId;
        // Each drag gets a fresh throttle window so its first "move" is never
        // swallowed by the previous overlay drag's timestamp.
        m_overlayDragHookThrottle.invalidate();
        sendPythonHandleMessage(String("press"), overlayId, docPos, modifiers);
        return true;
    }
    return false;
}

void PaintOpenGLWidget::sendOverlayActionMessage(const String &overlayId,
                                                 const String &action)
{
#ifdef ANIMEAN_WITH_PYTHON
    const String eventName = action == String("accept")
                                  ? String("overlayaction")
                                  : String("overlayremove");
    if (!animeanHookEventSubscribed(eventName)) {
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
    overlayInfo["action"] = action.toStdString();

    py::dict message;
    message["event"] = eventName.toStdString();
    message["view"] = m_viewName.toStdString();
    message["tool"] = (m_activePythonTool.isEmpty() ? toolName(m_tool) : m_activePythonTool).toStdString();
    message["base_tool"] = toolName(m_tool).toStdString();
    message["property"] = m_strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["overlay"] = overlayInfo;

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)overlayId;
    (void)action;
#endif
}

void PaintOpenGLWidget::mousePressEvent(MouseEvent *event)
{
    m_lastMouseModifiers = event->modifiers();
    if (m_playbackActive || m_swallowNextPress) {
        // While prerendered frames are on screen a click means "stop
        // playing", not "draw into the frame you cannot currently see".
        m_swallowNextPress = false;
        if (m_playbackActive) {
            playbackInterrupted();
        }
        event->accept();
        return;
    }

    if (event->button() == MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position();
        event->accept();
        return;
    }

    // The secondary button belongs to a script tool when one is armed: C++
    // only reports it (phase "context", with the drag in progress as the
    // handle, if any) and what it means - ending a gesture, cancelling a drag
    // - is the tool's business. Built-in tools keep ignoring it.
    if (event->button() == RightButton && !m_activePythonTool.isEmpty()) {
        if (editingAllowed()) {
            sendPythonHandleMessage(String("context"), m_activeHandleDrag,
                                    mapToDocument(event->position()), event->modifiers());
        }
        event->accept();
        return;
    }

    if (event->button() != LeftButton) {
        PaintBoardBase::mousePressEvent(event);
        return;
    }

    const PointF pos = mapToDocument(event->position());
    m_hoverPos = pos;
    m_hasHoverPos = true;

    // Content lock, first of two layers. This one stops a gesture from
    // STARTING; the mutating helpers (eraseAt, eraseBetween, deleteLineAt,
    // deleteLineBetween, cutLineAt, fillAt) each check editingAllowed() too,
    // and that is the layer that actually holds: gating the press alone did
    // NOT work, because the erase gestures seed themselves in mouseMoveEvent
    // when no press was recorded and the erase tools also act on release, so
    // a blocked press still let a click or a drag rub out protected artwork.
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
    if (m_tool == Tool::Arrow || m_tool == Tool::Connect || m_tool == Tool::Transfer) {
        const String handleId = editHandleAt(event->position());
        if (!handleId.isEmpty()) {
            m_activeHandleDrag = handleId;
            sendPythonHandleMessage(String("press"), handleId, pos, event->modifiers());
        } else if (pressOverlayOrBadge(event->position(), pos, event->modifiers())) {
            // A badge fired, or a draggable overlay item (Auto Mapping's
            // guides and nearest-point anchor) started its
            // drag - see pressOverlayOrBadge for how an overlapping
            // badge/grab press is decided.
        } else {
            // A pick that CLAIMS something (Transfer's box body, Arrow's
            // default-mode outline) turns this press into a drag under the
            // id Python answered with. Nothing claimed: it stays a click.
            const String grabbed = sendPythonHandleMessage(String("pick"), String(),
                                                            pos, event->modifiers());
            if (!grabbed.isEmpty()) {
                m_activeHandleDrag = grabbed;
            }
        }
        event->accept();
        return;
    }

    // Overlay badges and draggable items are live under EVERY tool; an
    // overlapping badge/grab press is decided in pressOverlayOrBadge.
    if (pressOverlayOrBadge(event->position(), pos, event->modifiers())) {
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
            const bool cancelHistory = pythonHookSendMessage(String("fillfinish"), pos);
            if (!cancelHistory) {
                commitHistory(String("Fill"));
            }
        }
        update();
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

    if (m_model.currentLayer() >= 0 && !columnAcceptsGesture()) {
        event->accept();
        return;
    }

    const int assetCountBefore = m_model.assetCount();
    // An EMPTY cell is materialized here, and imageAt stamps the asset type
    // onto the column - so asking for a Vector cell on a Fill column would
    // silently turn that column into a line layer. The pen only ever reaches a
    // Fill column in fill-paint mode, where the stroke is about to become a
    // region fill on that very layer.
    const AnimeColumn *pressColumn = currentColumn();
    const AnimeColumnType pressCellType =
        (pressColumn && pressColumn->type == AnimeColumnType::Fill) ? AnimeColumnType::Fill
                                                                   : AnimeColumnType::Vector;
    if (!currentImage(true, pressCellType)) {
        event->accept();
        return;
    }
    if (m_model.assetCount() != assetCountBefore) {
        assetListChanged(m_model.currentAsset());
        layerListChanged(m_model.currentLayer());
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

void PaintOpenGLWidget::mouseMoveEvent(MouseEvent *event)
{
    m_lastMouseModifiers = event->modifiers();
    if (m_panning && (event->buttons() & MiddleButton)) {
        m_panOffset += event->position() - m_lastPanPos;
        m_lastPanPos = event->position();
        clampPan();
        update();
        notifyViewTransformChanged();
        userTransformed();
        event->accept();
        return;
    }

    m_hoverPos = mapToDocument(event->position());
    m_hasHoverPos = true;

    if (!(event->buttons() & LeftButton)) {
        // Connect snaps on HOVER: Python needs the cursor to show which
        // vertex a click would take. Transfer reads the same phase to name
        // the pointer for the region under it (a grip, the body, the rotate
        // ring outside a corner). Any armed script tool hears it too: what
        // follows the pointer between clicks is its own business. All of them
        // are throttled so a fast mouse does not run the interpreter at
        // tablet rate, and carry the modifier keys so a held key can change
        // what the hover offers. A content-locked board gets no hints -
        // presses are refused there, so advertising targets lies.
        if ((m_tool == Tool::Connect || m_tool == Tool::Transfer || !m_activePythonTool.isEmpty())
            && editingAllowed()
            && (!m_hoverHookThrottle.isValid()
                || m_hoverHookThrottle.elapsed() >= kUpdateHookIntervalMs)) {
            m_hoverHookThrottle.restart();
            sendPythonHandleMessage(String("hover"), String(), m_hoverPos, event->modifiers());
        }
        update();
        PaintBoardBase::mouseMoveEvent(event);
        return;
    }

    if (!m_activeOverlayDrag.isEmpty()) {
        // Throttled like the other pointer-rate hooks: the owning tool
        // re-renders its overlays on every message, which must not run at
        // mouse rate. A move swallowed by the throttle arms a single-shot
        // timer for the end of the window so pausing mid-drag still delivers
        // the latest position.
        if (!m_overlayDragHookThrottle.isValid()
            || m_overlayDragHookThrottle.elapsed() >= kUpdateHookIntervalMs) {
            stopOverlayDragMoveTimer();
            m_overlayDragHookThrottle.restart();
            sendPythonHandleMessage(String("move"), m_activeOverlayDrag, m_hoverPos,
                                    event->modifiers());
        } else {
            m_pendingOverlayDragPos = m_hoverPos;
            m_pendingOverlayDragModifiers = event->modifiers();
            if (!m_overlayDragMoveTimer) {
                m_overlayDragMoveTimer = std::make_unique<Timer>();
                m_overlayDragMoveTimer->setSingleShot(true);
                m_overlayDragMoveTimer->timeout.connect([this]() { flushPendingOverlayDragMove(); });
            }
            if (!m_overlayDragMoveTimer->isActive()) {
                const std::int64_t remaining =
                    std::max<std::int64_t>(1, kUpdateHookIntervalMs - m_overlayDragHookThrottle.elapsed());
                m_overlayDragMoveTimer->start(int(remaining));
            }
        }
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::Arrow || m_tool == Tool::Connect || m_tool == Tool::Transfer) {
        if (!m_activeHandleDrag.isEmpty()) {
            sendPythonHandleMessage(String("move"), m_activeHandleDrag, m_hoverPos,
                                    event->modifiers());
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

    if (!m_hasCurrentStroke) {
        PaintBoardBase::mouseMoveEvent(event);
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
    const PointF stabilized =
        mapToDocument(m_inputFilter.filter(event->position(), event->timestamp()));
    bool axisRetroChanged = false;
    const PointF snappedPos = applyAxisSnap(event->modifiers(), stabilized, &axisRetroChanged);
    // Has the hand left the still circle? Measured on the RAW cursor, since
    // this asks about the hand, not about the ink the filter has settled on.
    // The radius is SCREEN px converted through algorithm/viewscale.h (the
    // shared home of screen<->canvas conversion): holding still means holding
    // still on the display, at any magnification. The anchor is kept in
    // document space so zooming mid-stroke cannot drag it out from under us.
    if (LineF(m_holdStillAnchor, m_hoverPos).length()
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

void PaintOpenGLWidget::mouseReleaseEvent(MouseEvent *event)
{
    m_lastMouseModifiers = event->modifiers();
    if (event->button() == MiddleButton) {
        m_panning = false;
        event->accept();
        return;
    }

    if (event->button() != LeftButton) {
        PaintBoardBase::mouseReleaseEvent(event);
        return;
    }

    if (!m_activeOverlayDrag.isEmpty()) {
        stopOverlayDragMoveTimer();
        const String overlayId = m_activeOverlayDrag;
        m_activeOverlayDrag.clear();
        const PointF docPos = mapToDocument(event->position());
        // One final, un-throttled move first: the throttle may have swallowed
        // up to kUpdateHookIntervalMs of pointer travel, and not every owner
        // recomputes from the release position - the committed state must be
        // where the button actually came up.
        sendPythonHandleMessage(String("move"), overlayId, docPos,
                                event->modifiers());
        sendPythonHandleMessage(String("release"), overlayId, docPos,
                                event->modifiers());
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::Arrow || m_tool == Tool::Connect || m_tool == Tool::Transfer) {
        if (!m_activeHandleDrag.isEmpty()) {
            const String handleId = m_activeHandleDrag;
            m_activeHandleDrag.clear();
            sendPythonHandleMessage(String("release"), handleId,
                                    mapToDocument(event->position()), event->modifiers());
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
            cancelHistory = pythonHookSendMessage(String("deletefinish"), m_hoverPos, PointF(), m_eraseGestureChanged);
        } else if (m_tool == Tool::DeleteLine) {
            m_eraseGestureChanged = deleteLineAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(String("deletefinish"), m_hoverPos, PointF(), m_eraseGestureChanged);
        } else {
            m_eraseGestureChanged = eraseAt(m_hoverPos) || m_eraseGestureChanged;
            cancelHistory = pythonHookSendMessage(String("erasefinish"), m_hoverPos, PointF(), m_eraseGestureChanged);
        }
        if (m_eraseGestureChanged && !cancelHistory) {
            commitHistory(m_tool == Tool::CutLine ? String("Cut Line")
                          : m_tool == Tool::DeleteLine ? String("Delete Line")
                                                       : String("Erase"));
        }
        update();
        m_hasLastEraserPos = false;
        m_eraseGestureChanged = false;
        event->accept();
        return;
    }

    if (!m_hasCurrentStroke) {
        PaintBoardBase::mouseReleaseEvent(event);
        return;
    }

    if (m_straightLineMode) {
        // The segment already IS the committed geometry; the release only
        // pins its free end where the button came up. Like the curved path
        // below, it follows the lock that governed the last MOVE rather than
        // the live modifier state: letting go of Alt a moment before the
        // button would otherwise commit a diagonal the preview never showed.
        PointF tip = mapToDocument(event->position());
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
    PointF releasePoint = mapToDocument(event->position());
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
        updateStraightLine(m_points.isEmpty() ? m_straightLineStart : PointF(m_points.last()));
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
        const AnimeCore::Path live =
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
        pythonHookSendMessage(String("update"));
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
        const AnimeCore::Path live =
            AnimeVectorLogic::liveFitStrokePath(m_liveFit, m_points, liveSettings, true);
        m_currentStroke = AnimeVectorLogic::makeStrokeFromPath(
            live, m_points, m_currentStroke.color, m_currentStroke.width, 0);
        m_currentStroke.property = m_strokeProperty;
    }
    if (!m_currentStroke.points.isEmpty()) {
        const int assetCountBefore = m_model.assetCount();
        // Same cell-type care as the press: creating the cell here with the
        // Vector type would stamp it onto a Fill column the gesture was
        // deliberately allowed onto (see mousePressEvent).
        const AnimeColumn *strokeColumn = currentColumn();
        const AnimeColumnType strokeCellType =
            (strokeColumn && strokeColumn->type == AnimeColumnType::Fill) ? AnimeColumnType::Fill
                                                                         : AnimeColumnType::Vector;
        if (VectorImageModel *image = currentImage(true, strokeCellType)) {
            const int strokeIndex = image->strokeCount();
            m_currentStroke.property = m_strokeProperty;
            image->addStroke(m_currentStroke);
            const bool cancelHistory =
                pythonHookSendMessage(String("linefinish"), PointF(), PointF(), true, strokeIndex);
            removeInvalidFillRegions();
            if (m_model.assetCount() != assetCountBefore) {
                assetListChanged(m_model.currentAsset());
                layerListChanged(m_model.currentLayer());
            }
            // Committed after the linefinish hooks so tool scripts that adjust
            // the model (e.g. guide capture) fold into the same history entry;
            // hooks that undid the stroke entirely veto the commit instead.
            if (!cancelHistory) {
                commitHistory(m_strokeProperty.isEmpty() ? String("Stroke") : m_strokeProperty);
            }
        }
    }
    m_hasCurrentStroke = false;
    m_straightLineMode = false;
    m_points.clear();
    update();
}

bool PaintOpenGLWidget::pythonHookSendMessage(const String &event, const PointF &pos, const PointF &delta, bool changed, int strokeIndex)
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
    if (m_hasCurrentStroke || event == String("linefinish")) {
        message["stroke"] = strokeToPythonDict(m_currentStroke, strokeIndex);
    } else {
        message["stroke"] = py::dict();
    }
    message["position"] = pointToPythonDict(pos);
    message["delta"] = pointToPythonDict(delta);

    const String output = ::pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
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
    (void)event;
    (void)pos;
    (void)delta;
    (void)changed;
    (void)strokeIndex;
    return false;
#endif
}

bool PaintOpenGLWidget::eraseAt(const PointF &pos)
{
    // Fill regions first, and outside the stroke gate: a Fill column carries
    // no strokes, so the bounds test below would refuse the gesture and the
    // eraser would come up empty on the very layer it was remapped for.
    const bool fillsErased = eraseFillRegionsBetween(pos, pos);
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !columnAcceptsGesture())) {
        return fillsErased;
    }

    const double imageRadius = m_eraserRadius + m_penWidth;
    const RectF eraserBounds(pos.x() - imageRadius, pos.y() - imageRadius,
                              imageRadius * 2.0, imageRadius * 2.0);
    if (!image->bounds().intersects(eraserBounds)) {
        return fillsErased;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeAt(i, pos) || changed;
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed || fillsErased;
}

bool PaintOpenGLWidget::eraseBetween(const PointF &from, const PointF &to)
{
    const bool fillsErased = eraseFillRegionsBetween(from, to);
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !columnAcceptsGesture())) {
        return fillsErased;
    }

    const double imageRadius = m_eraserRadius + m_penWidth;
    const double left = std::min(from.x(), to.x()) - imageRadius;
    const double top = std::min(from.y(), to.y()) - imageRadius;
    const double right = std::max(from.x(), to.x()) + imageRadius;
    const double bottom = std::max(from.y(), to.y()) + imageRadius;
    const RectF eraserBounds(PointF(left, top), PointF(right, bottom));
    if (!image->bounds().intersects(eraserBounds)) {
        return fillsErased;
    }

    bool changed = false;
    for (int i = image->strokeCount() - 1; i >= 0; --i) {
        changed = eraseStrokeBetween(i, from, to) || changed;
    }
    if (changed) {
        removeInvalidFillRegions();
    }
    return changed || fillsErased;
}

int PaintOpenGLWidget::nearestStrokeToBrush(const VectorImageModel *image,
                                            const PointF &from,
                                            const PointF &to) const
{
    // A wide brush covers several lines at once. Picking "the first one found"
    // or "the topmost" then acts on whichever the loop happened to reach,
    // which from the user's side looks arbitrary - and for LineMode it deleted
    // ALL of them. The brush CENTRE decides instead: exactly one line goes,
    // the one the user aimed at.
    if (!image) {
        return -1;
    }
    const double imageRadius = m_eraserRadius + m_penWidth;
    const RectF hitBounds = RectF(from, to).normalized()
                                 .adjusted(-imageRadius, -imageRadius, imageRadius, imageRadius);

    int best = -1;
    double bestDistance = 0.0;
    for (int i = 0; i < image->strokeCount(); ++i) {
        const VectorStroke &stroke = image->strokeAt(i);
        if (!stroke.bounds.intersects(hitBounds)) {
            continue;
        }
        const bool hit = LineF(from, to).length() <= AnimeVectorLogic::epsilon()
                             ? AnimeVectorLogic::strokeHitsCircle(stroke, from, m_eraserRadius)
                             : AnimeVectorLogic::strokeHitsCapsule(stroke, from, to, m_eraserRadius);
        if (!hit) {
            continue;
        }
        const double distance = AnimeVectorLogic::strokeDistanceToBrush(stroke, from, to);
        // Ties go to the LATER index, which is the one drawn on top.
        if (best < 0 || distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

bool PaintOpenGLWidget::cutLineAt(const PointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || !editingAllowed() || (m_model.currentLayer() >= 0 && !currentColumnEditable())) {
        return false;
    }

    const double imageRadius = m_eraserRadius + m_penWidth;
    const RectF hitBounds(pos.x() - imageRadius, pos.y() - imageRadius,
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
    AnimeCore::Vector<VectorStroke> walls;
    Vector<int> wallStrokeIndex;
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
    Hash<int, Vector<double>> wallCuts;
    Vector<double> selfCuts;
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
    Vector<double> boundaries;
    boundaries.append(plan.span.first);
    boundaries.append(plan.span.second);
    for (double arc : selfCuts) {
        // Interior arcs only, and not doubling a span bound - the same
        // endpoint rule splitStrokeAt applies (its endTolerance default).
        const double tolerance = 1e-3;
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
    const auto junctionPoint = [&target](double arc) {
        return AnimeVectorLogic::pointAtLength(target, arc * target.totalLength);
    };
    // A snap must stay a NUDGE: bounded by the piece's own length (a short
    // sliver must not have its ends cross) and by a sanity cap - a junction
    // farther out than any plausible fit deviation means something else went
    // wrong, and leaving the end in place beats teleporting ink. The fill's
    // dangling-tip heal still catches an unsnapped end.
    const auto snapEndBounded = [](VectorStroke piece, bool atEnd, const PointF &junction) {
        constexpr double kJunctionSnapLimitPx = 4.0;
        if (piece.points.size() < 2) {
            return piece;
        }
        const PointF end = atEnd ? piece.points.last() : piece.points.first();
        const double nudge = LineF(end, junction).length();
        if (nudge <= 1e-12 || nudge > kJunctionSnapLimitPx
            || nudge > piece.totalLength * 0.5) {
            return piece;
        }
        return AnimeVectorLogic::snapStrokeEndpoint(piece, atEnd, junction);
    };
    const auto boundaryIsJunction = [&plan](double arc) {
        if (arc == plan.span.first) {
            return plan.low.isJunction();
        }
        if (arc == plan.span.second) {
            return plan.high.isJunction();
        }
        return true;   // the remaining boundaries are self-crossing partners
    };

    AnimeCore::Vector<VectorStroke> pieces;
    double previous = 0.0;
    for (int i = 0; i <= boundaries.size(); ++i) {
        const double next = i < boundaries.size() ? boundaries[i] : 1.0;
        const double middle = (previous + next) * 0.5;
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
    Vector<int> editOrder = wallCuts.keys();
    std::sort(editOrder.begin(), editOrder.end(), std::greater<int>());
    bool changed = false;
    int currentTarget = targetIndex;
    for (int strokeIndex : editOrder) {
        const VectorStroke wall = image->strokeAt(strokeIndex);
        AnimeCore::Vector<double> usedWallArcs;
        AnimeCore::Vector<VectorStroke> wallPieces = AnimeVectorLogic::splitStrokeAt(
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
            const PointF junction = AnimeVectorLogic::pointAtLength(
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

bool PaintOpenGLWidget::deleteLineAt(const PointF &pos)
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

bool PaintOpenGLWidget::deleteLineBetween(const PointF &from, const PointF &to)
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

bool PaintOpenGLWidget::fillAt(const PointF &pos)
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

    const Path fillPath = vectorRegionPathAt(pos, boundaryScope, originalLayer);
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
        const bool sameProperty = String(existing.property) == m_strokeProperty;
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
        assetListChanged(m_model.currentAsset());
        layerListChanged(m_model.currentLayer());
    }
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

Vector<LineF> PaintOpenGLWidget::fillGraphSegments(FillScope scope, int layerIndex) const
{
    const bool allLayers = scope == FillScope::AllLayers;
    return m_model.fillBoundarySegments(m_model.currentFrame(), allLayers ? -1 : layerIndex);
}

Path PaintOpenGLWidget::vectorRegionPathAt(const PointF &seed, FillScope scope, int layerIndex) const
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

bool PaintOpenGLWidget::eraseStrokeAt(int strokeIndex, const PointF &pos)
{
    VectorImageModel *image = currentImage(false);
    if (!image || strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
        return false;
    }

    const VectorStroke stroke = image->strokeAt(strokeIndex);
    const double effectiveRadius = m_eraserRadius + stroke.width * 0.5;
    const RectF eraserBounds(pos.x() - effectiveRadius, pos.y() - effectiveRadius,
                              effectiveRadius * 2.0, effectiveRadius * 2.0);
    if (!stroke.bounds.intersects(eraserBounds)) {
        return false;
    }

    const AnimeCore::Vector<AnimeVectorRange> keepRanges = AnimeVectorLogic::keepRangesForCircle(stroke, pos, m_eraserRadius);
    const double epsilon = AnimeVectorLogic::epsilon();
    if (keepRanges.size() == 1 && keepRanges.first().first <= epsilon &&
        keepRanges.first().second >= 1.0 - epsilon) {
        return false;
    }

    AnimeCore::Vector<VectorStroke> pieces;
    for (const AnimeVectorRange &range : keepRanges) {
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_fitSettings));
    }
    image->replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

bool PaintOpenGLWidget::eraseStrokeBetween(int strokeIndex, const PointF &from, const PointF &to)
{
    VectorImageModel *image = currentImage(false);
    if (!image || strokeIndex < 0 || strokeIndex >= image->strokeCount()) {
        return false;
    }

    const VectorStroke stroke = image->strokeAt(strokeIndex);
    const double effectiveRadius = m_eraserRadius + stroke.width * 0.5;
    const double left = std::min(from.x(), to.x()) - effectiveRadius;
    const double top = std::min(from.y(), to.y()) - effectiveRadius;
    const double right = std::max(from.x(), to.x()) + effectiveRadius;
    const double bottom = std::max(from.y(), to.y()) + effectiveRadius;
    const RectF eraserBounds(PointF(left, top), PointF(right, bottom));
    if (!stroke.bounds.intersects(eraserBounds)) {
        return false;
    }

    const AnimeCore::Vector<AnimeVectorRange> keepRanges = AnimeVectorLogic::keepRangesForCapsule(stroke, from, to, m_eraserRadius);
    const double epsilon = AnimeVectorLogic::epsilon();
    if (keepRanges.size() == 1 && keepRanges.first().first <= epsilon &&
        keepRanges.first().second >= 1.0 - epsilon) {
        return false;
    }

    AnimeCore::Vector<VectorStroke> pieces;
    for (const AnimeVectorRange &range : keepRanges) {
        pieces.append(AnimeVectorLogic::subStroke(stroke, range.first, range.second, m_fitSettings));
    }
    image->replaceStrokeWithPieces(strokeIndex, pieces);
    return true;
}

PaintOpenGLWidget::VectorStroke PaintOpenGLWidget::makeStroke(const Vector<PointF> &points, const Color &color, double width, int id, bool filterInput, bool smoothPath) const
{
    return AnimeVectorLogic::makeStroke(points, color, width, id, filterInput, smoothPath, m_fitSettings);
}

bool PaintOpenGLWidget::appendPoint(const PointF &point)
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
    if (LineF(m_points.last(), point).length()
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

bool PaintOpenGLWidget::columnAcceptsGesture() const
{
    if (currentColumnEditable()) {
        return true;
    }
    // The ONE relaxation, and it is opt-in: a Fill column stays uneditable for
    // every other caller, because pen and erase have no strokes to work on
    // there. In fill-paint mode the gesture means something else entirely -
    // the policy that turned the mode on converts the committed stroke into
    // region fills - so the gesture has to be allowed to happen first.
    if (!m_fillPaintMode) {
        return false;
    }
    const AnimeColumn *column = currentColumn();
    return column && !column->locked && column->type == AnimeColumnType::Fill;
}

bool PaintOpenGLWidget::eraseFillRegionsBetween(const PointF &from, const PointF &to)
{
    if (!m_fillPaintMode || !editingAllowed()) {
        return false;
    }
    const AnimeColumn *column = currentColumn();
    if (!column || column->locked || column->type != AnimeColumnType::Fill) {
        return false;
    }
    VectorImageModel *image = currentImage(false);
    if (!image) {
        return false;
    }

    // Sampled ALONG the segment, not tested at its end alone: a move event
    // covers whatever distance the hand travelled since the last one, and a
    // small region the cursor crossed in between would otherwise survive a
    // stroke drawn straight through it.
    const double step = std::max<double>(1.0, m_eraserRadius);
    const double length = LineF(from, to).length();
    const int steps = length <= step ? 0 : int(length / step);
    bool changed = false;
    for (int sample = 0; sample <= steps; ++sample) {
        const double t = steps == 0 ? 1.0 : double(sample) / double(steps);
        const PointF pos = from + (to - from) * t;
        // Descending: removeFillRegionAt shifts everything above the index it
        // drops, and the lower ones still to visit keep theirs.
        for (int index = image->fillCount() - 1; index >= 0; --index) {
            if (!image->fillRegions().at(index).path.contains(pos)) {
                continue;
            }
            image->removeFillRegionAt(index);
            changed = true;
        }
    }
    return changed;
}
