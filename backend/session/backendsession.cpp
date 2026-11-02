#include "algorithm/core/containers.h"
#include "algorithm/core/image.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"
#include <utility>
#include "session/backendsession.h"

#include "algorithm/core/file.h"
#include "algorithm/beziersplit.h"
#include "board/boardsurface.h"
#include "business/animeanbusiness.h"
#include "clipreader.h"
#include "net/wsserver.h"
#include "openglwidget.h"
#include "theme.h"

#include "algorithm/core/color.h"
#include "algorithm/core/settings.h"
#include "platform/eventloop.h"
#include "platform/signalslot.h"
#include "platform/timer.h"

#include "algorithm/core/json.h"
#include "algorithm/core/geometry.h"




#include <cstdint>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <string>
using namespace AnimeCore;

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots

#endif
#include <pybind11/embed.h>
#if 1



#endif
namespace py = pybind11;
#endif

namespace {

String cursorImageKey(const Image &rgba)
{
    std::uint64_t h = 14695981039346656037ULL;
    const auto mix = [&h](const unsigned char *data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<std::uint64_t>(data[i]);
            h *= 1099511628211ULL;
        }
    };
    const int w = rgba.width();
    const int height = rgba.height();
    mix(reinterpret_cast<const unsigned char *>(&w), sizeof(w));
    mix(reinterpret_cast<const unsigned char *>(&height), sizeof(height));
    for (int y = 0; y < height; ++y) {
        mix(rgba.constScanLine(y), static_cast<size_t>(w) * 4);
    }
    return String("cur_%1").arg(h, 16, 16, Latin1Char('0'));
}

void mapCursorShape(CursorShape shape, String *outName, String *outCss)
{
    switch (shape) {
    case ArrowCursor:
        *outName = String("arrow");
        *outCss = String("default");
        break;
    case CrossCursor:
        *outName = String("cross");
        *outCss = String("crosshair");
        break;
    case WaitCursor:
    case BusyCursor:
        *outName = String("wait");
        *outCss = String("wait");
        break;
    case IBeamCursor:
        *outName = String("ibeam");
        *outCss = String("text");
        break;
    case SizeVerCursor:
    case SplitVCursor:
        *outName = String("size_v");
        *outCss = String("ns-resize");
        break;
    case SizeHorCursor:
    case SplitHCursor:
        *outName = String("size_h");
        *outCss = String("ew-resize");
        break;
    case SizeBDiagCursor:
        *outName = String("size_bdiag");
        *outCss = String("nesw-resize");
        break;
    case SizeFDiagCursor:
        *outName = String("size_fdiag");
        *outCss = String("nwse-resize");
        break;
    case SizeAllCursor:
        *outName = String("size_all");
        *outCss = String("move");
        break;
    case BlankCursor:
        *outName = String("blank");
        *outCss = String("none");
        break;
    case PointingHandCursor:
        *outName = String("pointing_hand");
        *outCss = String("pointer");
        break;
    case ForbiddenCursor:
        *outName = String("forbidden");
        *outCss = String("not-allowed");
        break;
    case OpenHandCursor:
        *outName = String("open_hand");
        *outCss = String("grab");
        break;
    case ClosedHandCursor:
        *outName = String("closed_hand");
        *outCss = String("grabbing");
        break;
    default:
        *outName = String("arrow");
        *outCss = String("default");
        break;
    }
}

AnimeanBusiness::Tool parseToolValue(const JsonValue &val, bool *okOut = nullptr)
{
    if (okOut) {
        *okOut = true;
    }
    if (val.isDouble()) {
        const int idx = val.toInt(0);
        if (idx >= int(AnimeanBusiness::Tool::Pen) && idx <= int(AnimeanBusiness::Tool::Transfer)) {
            return static_cast<AnimeanBusiness::Tool>(idx);
        }
    }
    const String s = val.toString().trimmed().toLower();
    if (s == String("pen")) {
        return AnimeanBusiness::Tool::Pen;
    }
    if (s == String("eraser") || s == String("area")) {
        return AnimeanBusiness::Tool::Eraser;
    }
    if (s == String("delete_line") || s == String("deleteline") || s == String("line")) {
        return AnimeanBusiness::Tool::DeleteLine;
    }
    if (s == String("cut_line") || s == String("cutline") || s == String("cut")) {
        return AnimeanBusiness::Tool::CutLine;
    }
    if (s == String("fill")) {
        return AnimeanBusiness::Tool::Fill;
    }
    if (s == String("arrow")) {
        return AnimeanBusiness::Tool::Arrow;
    }
    if (s == String("connect")) {
        return AnimeanBusiness::Tool::Connect;
    }
    if (s == String("transfer")) {
        return AnimeanBusiness::Tool::Transfer;
    }
    if (okOut) {
        *okOut = false;
    }
    return AnimeanBusiness::Tool::Pen;
}

String toolEnumDisplayName(AnimeanBusiness::Tool tool)
{
    switch (tool) {
    case AnimeanBusiness::Tool::Pen:
        return String("Pen");
    case AnimeanBusiness::Tool::Eraser:
        return String("Eraser");
    case AnimeanBusiness::Tool::DeleteLine:
        return String("DeleteLine");
    case AnimeanBusiness::Tool::CutLine:
        return String("CutLine");
    case AnimeanBusiness::Tool::Fill:
        return String("Fill");
    case AnimeanBusiness::Tool::Arrow:
        return String("Arrow");
    case AnimeanBusiness::Tool::Connect:
        return String("Connect");
    case AnimeanBusiness::Tool::Transfer:
        return String("Transfer");
    }
    return String("Pen");
}

Color parseColorParams(const JsonObject &params)
{
    if (params.contains(String("color"))) {
        const JsonValue cVal = params.value(String("color"));
        if (cVal.isString()) {
            const Color c(cVal.toString());
            if (c.isValid()) {
                return c;
            }
        } else if (cVal.isObject()) {
            const JsonObject cObj = cVal.toObject();
            return Color(cObj.value(String("r")).toInt(0),
                          cObj.value(String("g")).toInt(0),
                          cObj.value(String("b")).toInt(0),
                          cObj.value(String("a")).toInt(255));
        }
    }
    if (params.contains(String("hex"))) {
        const Color c(params.value(String("hex")).toString());
        if (c.isValid()) {
            return c;
        }
    }
    if (params.contains(String("r"))
        || params.contains(String("g"))
        || params.contains(String("b"))) {
        return Color(params.value(String("r")).toInt(0),
                      params.value(String("g")).toInt(0),
                      params.value(String("b")).toInt(0),
                      params.value(String("a")).toInt(255));
    }
    return Color();
}

JsonObject makeMenuItem(const String &id,
                         const String &text,
                         const String &shortcut,
                         const String &kind,
                         bool checked = false,
                         bool enabled = true,
                         const JsonArray &children = JsonArray())
{
    return JsonObject{
        {String("id"), id},
        {String("text"), text},
        {String("shortcut"), shortcut},
        {String("kind"), kind},
        {String("checked"), checked},
        {String("enabled"), enabled},
        {String("children"), children},
    };
}

const StringList &allStateKeys()
{
    static const StringList keys = {
        String("window"),
        String("theme"),
        String("menus"),
        String("textureMenus"),
        String("tools"),
        String("toolOptions"),
        String("drawColor"),
        String("drawSettings"),
        String("layers/main"),
        String("layers/child"),
        String("assets/main"),
        String("assets/child"),
        String("timeline"),
        String("thumbnails"),
        String("history"),
        String("windows"),
        String("cursor/main"),
        String("cursor/child"),
        String("viewport/main"),
        String("viewport/child"),
        String("freeze"),
        String("pad"),
    };
    return keys;
}

int frameNameToRow(const String &frameName)
{
    String digits;
    for (const Char ch : frameName) {
        if (!ch.isDigit()) {
            break;
        }
        digits.append(ch);
    }

    bool ok = false;
    const int frameNumber = digits.toInt(&ok);
    return ok && frameNumber > 0 ? frameNumber - 1 : 0;
}

#ifdef ANIMEAN_WITH_PYTHON
double dictNumber(const py::dict &data, const char *key, double fallback = 0.0)
{
    if (!data.contains(py::str(key))) {
        return fallback;
    }
    try {
        return data[py::str(key)].cast<double>();
    } catch (const py::cast_error &) {
        return fallback;
    }
}

PointF pointFromDict(const py::dict &data)
{
    return PointF(dictNumber(data, "x"), dictNumber(data, "y"));
}

double thicknessFromDict(const py::dict &data)
{
    return dictNumber(data, "thick", 3.0);
}

void appendQuadraticMetricPoints(AnimeCore::Vector<AnimeCore::PointF> *points,
                                 const PointF &p0,
                                 const PointF &p1,
                                 const PointF &p2,
                                 int sampleCount)
{
    if (points->isEmpty()) {
        points->append(p0);
    }

    AnimeCore::PointF cubic[4];
    AnimeBezierSplit::quadToCubic(p0, p1, p2, cubic);
    sampleCount = std::max(2, sampleCount);
    for (int i = 1; i <= sampleCount; ++i) {
        points->append(AnimeBezierSplit::evaluateCubic(cubic, static_cast<double>(i) / sampleCount));
    }
}

Vector<PaintOpenGLWidget::ImportedVectorFrame> importedFramesFromOpenToonzLevel(const py::dict &levelData)
{
    Vector<PaintOpenGLWidget::ImportedVectorFrame> importedFrames;
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
        String frameName = String("1");
        if (frameDict.contains(py::str("frame"))) {
            const std::string frameNameText = frameDict[py::str("frame")].cast<std::string>();
            frameName = String::fromUtf8(frameNameText.c_str());
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
            double thicknessTotal = 0.0;
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
                const PointF p0 = pointFromDict(p0Dict);
                const PointF p1 = pointFromDict(p1Dict);
                const PointF p2 = pointFromDict(p2Dict);
                if (!hasPathStart) {
                    importedStroke.path.moveTo(p0);
                    hasPathStart = true;
                } else if (!fuzzyCompare(importedStroke.path.currentPosition().x(), p0.x()) ||
                           !fuzzyCompare(importedStroke.path.currentPosition().y(), p0.y())) {
                    importedStroke.path.lineTo(p0);
                }
                importedStroke.path.quadTo(p1, p2);
                const double approxLength = LineF(p0, p1).length() + LineF(p1, p2).length();
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
                                           ? std::max(double(1.0), thicknessTotal / thicknessCount)
                                           : std::max(double(1.0), dictNumber(strokeDict, "max_thickness", 3.0) * 0.2);
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

RectF importedVectorFramesBounds(const Vector<PaintOpenGLWidget::ImportedVectorFrame> &frames)
{
    RectF bounds;
    bool hasBounds = false;
    for (const PaintOpenGLWidget::ImportedVectorFrame &frame : frames) {
        for (const PaintOpenGLWidget::ImportedVectorStroke &stroke : frame.strokes) {
            const RectF strokeBounds = stroke.path.isEmpty()
                                            ? RectF()
                                            : RectF(stroke.path.boundingRect());
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

void scaleImportedVectorFramesToCanvas(Vector<PaintOpenGLWidget::ImportedVectorFrame> *frames, const SizeF &targetSize)
{
    if (!frames || targetSize.width() <= 0.0 || targetSize.height() <= 0.0) {
        return;
    }

    const RectF sourceBounds = importedVectorFramesBounds(*frames);
    if (!sourceBounds.isValid() || sourceBounds.width() <= 0.0 || sourceBounds.height() <= 0.0) {
        return;
    }

    const double scaleX = targetSize.width() / sourceBounds.width();
    const double scaleY = targetSize.height() / sourceBounds.height();
    const double scale = std::min(scaleX, scaleY);
    const PointF sourceCenter = sourceBounds.center();
    const PointF targetCenter(targetSize.width() * 0.5, targetSize.height() * 0.5);

    for (PaintOpenGLWidget::ImportedVectorFrame &frame : *frames) {
        for (PaintOpenGLWidget::ImportedVectorStroke &stroke : frame.strokes) {
            for (AnimeCore::PointF &point : stroke.points) {
                point = AnimeCore::PointF(targetCenter) + (point - AnimeCore::PointF(sourceCenter)) * scale;
            }
            const double dx = targetCenter.x() - sourceCenter.x() * scale;
            const double dy = targetCenter.y() - sourceCenter.y() * scale;
            const Transform transform(scale, 0.0, 0.0, scale, dx, dy);
            stroke.path = transform.map(Path(stroke.path));
            stroke.width = std::max(double(1.0), stroke.width * scale);
        }
    }
}

StringList extractPathsParam(const JsonObject &params)
{
    StringList out;
    if (params.contains(String("paths"))) {
        const JsonValue v = params.value(String("paths"));
        if (v.isArray()) {
            for (const JsonValue &item : v.toArray()) {
                const String s = item.toString().trimmed();
                if (!s.isEmpty()) {
                    out.append(s);
                }
            }
        } else if (v.isString()) {
            const String s = v.toString().trimmed();
            if (!s.isEmpty()) {
                out.append(s);
            }
        }
    }
    if (out.isEmpty()) {
        const String single = params.value(String("path"))
                                   .toString(params.value(String("fileName"))
                                                 .toString(params.value(String("filePath")).toString()))
                                   .trimmed();
        if (!single.isEmpty()) {
            out.append(single);
        }
    }
    return out;
}

} // namespace

BackendSession::BackendSession(WsServer *ws)
    : m_ws(ws)

    , m_business(std::make_unique<AnimeanBusiness>())
    , m_mainBoard(std::make_unique<PaintOpenGLWidget>())
    , m_childBoard(std::make_unique<PaintOpenGLWidget>())
    , m_lastEvent(JsonObject{{String("name"), String()}, {String("args"), JsonObject()}})
{
    initWindowInfos();
    loadTimelineSettings();
    m_cursors.insert(String("main"), CursorState());
    m_cursors.insert(String("child"), CursorState());
    m_padValues.insert(String("force_pad"), m_forcePadValue);

    m_mainBoard->setViewName(String("main"));
    m_mainBoard->model().setTextId(String("main_paint_view"));
    m_mainBoard->model().setIntId(1);
    m_mainBoard->setActiveIndicator(true);
    m_mainBoard->resizeSurface(800, 600, 1.0);

    m_childBoard->setViewName(String("child"));
    m_childBoard->setUnboundedCanvas(true);
    m_childBoard->model().setTextId(String("child_paint_view"));
    m_childBoard->model().setIntId(2);
    m_childBoard->setActiveIndicator(false);
    m_childBoard->resizeSurface(400, 400, 1.0);
    m_textureViewCentral.zoom = m_childBoard->zoom();
    m_textureViewCentral.pan = m_childBoard->panOffset();
    m_textureViewCentral.valid = true;
    m_textureViewCentral.autoFitSuspended = false;

    setupBoardSinks(String("main"), m_mainBoard.get());
    setupBoardSinks(String("child"), m_childBoard.get());

    bindPaintViewToBusiness(String("main"), m_mainBoard.get());
    bindPaintViewToBusiness(String("child"), m_childBoard.get());

    setupBusinessConnections();
    registerPythonUiCallbacks();

    m_mainBoard->resetHistory(String("Initial"));
    m_childBoard->resetHistory(String("Initial"));

    if (m_ws) {
        m_ws->clientConnected.connect([this]() { onClientConnected(); });
        m_ws->clientDisconnected.connect([this]() { onClientDisconnected(); });
        m_ws->textReceived.connect([this](const std::string &data) {
            JsonParseError err;
            const JsonDocument doc = JsonDocument::fromJson(data, &err);
            if (err.error != JsonParseError::NoError || !doc.isObject()) {
                return;
            }
            try {
                onMessageReceived(doc.object());
            } catch (const std::exception &error) {
                m_business->setStatusMessage(String::fromUtf8(error.what()));
                markDirty(String("window"));
            } catch (...) {
                m_business->setStatusMessage(String("Backend error while handling a message"));
                markDirty(String("window"));
            }
        });
    }
}

BackendSession::~BackendSession()
{
#ifdef ANIMEAN_WITH_PYTHON
    clearAnimeanUiPadValueCallback();
    clearAnimeanUiHistoryCallback();
    clearAnimeanUiLockedToolsCallback();
    clearAnimeanUiFillPaintModeCallback();
    clearAnimeanUiCursorCallback();
    clearAnimeanUiDrawColorCallback();
    clearAnimeanUiEditHandleCallback();
    clearAnimeanUiOverlayCallback();
    clearAnimeanUiFreezeCallback();
    clearAnimeanUiWindowsCallback();
    clearAnimeanUiRefreshCallback();
    clearAnimeanUiToolOptionsCallback();
    if (m_childBoard) {
        unregisterAnimeanUiScene(&m_childBoard->model());
    }
    if (m_mainBoard) {
        unregisterAnimeanUiScene(&m_mainBoard->model());
    }
#endif
}

void BackendSession::setStatusText(const String &text)
{
    m_business->setStatusMessage(text);
    markDirty(String("window"));
}

void BackendSession::initializePythonStartup()
{
#ifdef ANIMEAN_WITH_PYTHON
    registerPythonUiCallbacks();
    m_business->syncEmbeddedPythonState();
    appendPythonDebug(String(
        "[python register] animean_python, animemodel, ui, model, current, model_pybind, vectorlogic, canvas_width, canvas_height, "
        "main_model, child_model, active_view"));

    // Match MainWindow::setupDocks() -> createToolDocks() -> createScriptMenus() -> attachChildScriptMenus()
    // before runPythonInitializationScript() runs initalize.py.
    m_business->loadExtraTools();
    m_business->refreshToolOptions();
    captureScriptMenuNames();
    loadProtectedPropertiesForChild();

    m_business->runPythonInitializationScript();
    loadProtectedPropertiesForChild();
    m_mainBoard->resetHistory(String("Initial"));
    m_childBoard->resetHistory(String("Initial"));
    m_business->refreshToolOptions();
#endif
}

void BackendSession::loadTimelineSettings()
{
    Settings settings;
    settings.beginGroup(String("timeline"));

    const String area = settings.value(String("dockArea"), String("bottom")).toString();
    if (area == String("left") || area == String("right") || area == String("bottom")) {
        m_timelineDockArea = area;
    } else {
        m_timelineDockArea = String("bottom");
    }

    m_timelineCollapsed = settings.value(String("collapsed"), false).toBool();
    m_timelineFloating = settings.value(String("floating"), false).toBool();
    m_timelineVisible = settings.value(String("visible"), true).toBool();

    settings.endGroup();
}

void BackendSession::saveTimelineSettings() const
{
    Settings settings;
    settings.beginGroup(String("timeline"));
    settings.setValue(String("dockArea"), m_timelineDockArea);
    settings.setValue(String("floating"), m_timelineFloating);
    settings.setValue(String("collapsed"), m_timelineCollapsed);
    settings.setValue(String("visible"), m_timelineVisible);
    settings.endGroup();
}

void BackendSession::captureScriptMenuNames()
{
    m_mainScriptMenus.clear();
    m_childScriptMenus.clear();
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized()) {
        return;
    }
    auto captureForHost = [this](const char *host, Vector<std::pair<String, String>> *out) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string(host))
                                         .cast<std::string>();
            JsonParseError parseError;
            const JsonDocument document = JsonDocument::fromJson(json, &parseError);
            if (parseError.error != JsonParseError::NoError || !document.isArray()) {
                return;
            }
            for (const JsonValue &value : document.array()) {
                const JsonObject object = value.toObject();
                const String name = object.value(String("name")).toString();
                if (name.isEmpty()) {
                    continue;
                }
                const String title = object.value(String("title")).toString(name);
                out->append(std::make_pair(name, title));
            }
        } catch (const py::error_already_set &error) {
            m_business->setStatusMessage(String("menus error: %1").arg(String::fromUtf8(error.what())));
        }
    };
    captureForHost("main", &m_mainScriptMenus);
    captureForHost("child", &m_childScriptMenus);
#endif
}

void BackendSession::surfaceTextureView()
{
    if (!m_textureSubControlVisible) {
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == String("paint")) {
                info.current = String("texture");
                break;
            }
        }
        markDirty(String("windows"));
    }
    emitUiEvent(String("textureView.surface"));
    m_business->setActiveViewName(String("child"));
}

void BackendSession::surfaceMainView()
{
    for (AnimeanWindowInfo &info : m_windowInfos) {
        if (info.name == String("paint") && info.current != String("drawing")) {
            info.current = String("drawing");
            markDirty(String("windows"));
            emitUiEvent(String("window.select"),
                        JsonObject{{String("name"), info.name}, {String("page"), info.current}});
            break;
        }
    }
    m_business->setActiveViewName(String("main"));
}

BackendSession::TextureViewSlot *BackendSession::textureViewSlotFor(const String &home)
{
    if (home == String("central")) {
        return &m_textureViewCentral;
    }
    if (home == String("control")) {
        return &m_textureViewControl;
    }
    return nullptr;
}

void BackendSession::saveTextureViewSlot(const String &home)
{
    if (!m_childBoard) {
        return;
    }
    TextureViewSlot *slot = textureViewSlotFor(home.isEmpty() ? String("central") : home);
    if (!slot) {
        return;
    }
    slot->zoom = m_childBoard->zoom();
    slot->pan = m_childBoard->panOffset();
    slot->valid = true;
}

void BackendSession::restoreTextureViewSlot()
{
    if (!m_childBoard) {
        return;
    }
    TextureViewSlot *slot = textureViewSlotFor(m_textureHome);
    if (slot && slot->valid) {
        m_childBoard->setViewTransform(slot->zoom, slot->pan);
    }
    if (m_textureHome == String("control")) {
        if (slot) {
            slot->autoFitSuspended = false;
        }
        autoFitTextureControlView();
    }
    markDirty(String("viewport/child"));
}

void BackendSession::autoFitTextureControlView()
{
    if (!m_childBoard) {
        return;
    }
    if (!m_textureHome.isEmpty()) {
        if (m_textureHome != String("control") || m_textureViewControl.autoFitSuspended) {
            return;
        }
    }
    if (!m_childBoard->fitViewToContent(false)) {
        return;
    }
    m_textureViewControl.zoom = m_childBoard->zoom();
    m_textureViewControl.pan = m_childBoard->panOffset();
    m_textureViewControl.valid = true;
    m_textureAutoFitFrame = m_childBoard->model().currentFrame();
}

void BackendSession::initWindowInfos()
{
    m_windowInfos.clear();

    auto addWin = [this](const String &name,
                         const String &title,
                         bool visible,
                         const StringList &pages,
                         const String &current) {
        AnimeanWindowInfo info;
        info.name = name;
        info.title = title;
        info.visible = visible;
        info.pages = pages;
        info.current = current;
        m_windowInfos.append(info);
    };

    addWin(String("paint"),
           String("Paint"),
           true,
           {String("drawing"), String("texture")},
           String("drawing"));
    addWin(String("tools"),
           String("Tools"),
           true,
           {String("painting"), String("mapping")},
           String("painting"));
    addWin(String("tool_options"),
           String("Tool Options"),
           true,
           {String("options")},
           String("options"));
    addWin(String("layers"),
           String("Layers"),
           true,
           {String("main"), String("child")},
           String("main"));
    addWin(String("assets"),
           String("Assets"),
           false,
           {String("assets")},
           String("assets"));
    addWin(String("history"),
           String("History"),
           false,
           {String("history")},
           String("history"));
    addWin(String("repulsion_pad"),
           String("Repulsion Pad"),
           false,
           {String("repulsion_pad")},
           String("repulsion_pad"));
    addWin(String("python_debug"),
           String("Python Debug"),
           false,
           {String("python_debug")},
           String("python_debug"));
}

void BackendSession::loadProtectedPropertiesForChild()
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !m_childBoard) {
        return;
    }
    try {
        py::gil_scoped_acquire acquire;
        const std::string json = py::module_::import("python_hooks")
                                     .attr("protected_properties_json")(std::string("child"))
                                     .cast<std::string>();
        const JsonDocument document = JsonDocument::fromJson(json);
        StringList allowed;
        for (const JsonValue &value : document.array()) {
            const String property = value.toString();
            if (!property.isEmpty()) {
                allowed.append(property);
            }
        }
        m_childBoard->setEditablePropertyFilter(allowed);
    } catch (const py::error_already_set &error) {
        m_business->setStatusMessage(String("protected properties error: %1")
                                         .arg(String::fromUtf8(error.what())));
    }
#endif
}

void BackendSession::setupBoardSinks(const String &viewName, PaintOpenGLWidget *paintView)
{
    if (!paintView) {
        return;
    }
    paintView->setCanSendFrame([this]() { return m_ws && m_ws->hasClient(); });
    paintView->setFrameSink([this, viewName](const BoardFrame &frame) {
        handleFrameSink(viewName, frame);
    });
    paintView->setImageSink(
        [this, viewName](const AnimeCore::String &key, const AnimeCore::Image &rgba) {
            handleImageSink(viewName, key, rgba);
        },
        [this, viewName](const AnimeCore::String &key) {
            sendJson(JsonObject{
                {String("t"), String("image.forget")},
                {String("view"), viewName},
                {String("key"), key},
            });
        });
    paintView->setCursorSink([this, viewName](const AnimeCore::Cursor &cursor) {
        handleCursorSink(viewName, cursor);
    });
    handleCursorSink(viewName, paintView->cursor());
}

void BackendSession::bindPaintViewToBusiness(const String &viewName, PaintOpenGLWidget *paintView)
{
    if (!m_business || !paintView) {
        return;
    }

    AnimePaintViewAdapter adapter;
    adapter.viewObject = nullptr;
    adapter.model = &paintView->model();
    adapter.history = &paintView->history();

    adapter.modelReplaced = [paintView]() { paintView->modelReplaced(); };
    adapter.setCanvasSize = [paintView](const Size &size) { paintView->setCanvasSize(size); };
    adapter.requestUpdate = [paintView]() { paintView->update(); };
    adapter.setCurrentFrame = [paintView](int frameIndex) { paintView->setCurrentFrame(frameIndex); };
    adapter.setCurrentLayer = [paintView](int layerIndex) { paintView->setCurrentLayer(layerIndex); };
    adapter.setCurrentAsset = [paintView](int assetIndex) { paintView->setCurrentAsset(assetIndex); };

    adapter.commitHistory = [paintView](const String &label) { paintView->commitHistory(label); };
    adapter.resetHistory = [paintView](const String &label) { paintView->resetHistory(label); };
    adapter.dropRedoTail = [paintView]() { paintView->dropRedoTail(); };
    adapter.undoHistory = [paintView]() { return paintView->undoHistory(); };
    adapter.redoHistory = [paintView]() { return paintView->redoHistory(); };
    adapter.goToHistory = [paintView](int row) { return paintView->goToHistory(row); };

    adapter.addLayer = [paintView]() { return paintView->addLayer(); };
    adapter.deleteLayer = [paintView](int layerIndex) { return paintView->deleteLayer(layerIndex); };
    adapter.deleteLayerGroup = [paintView](int groupId) { return paintView->deleteLayerGroup(groupId); };
    adapter.moveLayer = [paintView](int fromIndex, int toIndex) {
        return paintView->moveLayer(fromIndex, toIndex);
    };
    adapter.sendLayerVisibilityMessage = [paintView](int layerIndex, bool visible) {
        return paintView->sendPythonLayerVisibilityMessage(layerIndex, visible);
    };

    adapter.addFrame = [paintView]() { return paintView->addFrame(); };
    adapter.insertHoldFrameAfter = [paintView](int frameIndex) {
        return paintView->insertHoldFrameAfter(frameIndex);
    };
    adapter.duplicateFrame = [paintView](int frameIndex) {
        return paintView->duplicateFrame(frameIndex);
    };
    adapter.deleteFrame = [paintView](int frameIndex) {
        return paintView->deleteFrame(frameIndex);
    };
    adapter.moveFrame = [paintView](int fromIndex, int toIndex) {
        return paintView->moveFrame(fromIndex, toIndex);
    };

    adapter.addAsset = [paintView](AnimeColumnType type, const String &name) {
        return paintView->addAsset(type, name);
    };
    adapter.assignAssetToLayer = [paintView](int layerIndex, int assetIndex) {
        return paintView->assignAssetToLayer(layerIndex, assetIndex);
    };
    adapter.addLayerForAsset = [paintView](int assetIndex) {
        return paintView->addLayerForAsset(assetIndex);
    };

    adapter.setTool = [paintView](int tool) {
        paintView->setTool(static_cast<PaintOpenGLWidget::Tool>(tool));
    };
    adapter.currentTool = [paintView]() { return int(paintView->tool()); };
    adapter.setStrokeProperty = [paintView](const String &property) {
        paintView->setStrokeProperty(property);
    };
    adapter.setDrawingColor = [paintView](const Color &color) {
        paintView->setDrawingColor(color);
    };
    adapter.setPenWidth = [paintView](double width) { paintView->setPenWidth(width); };
    adapter.setFillScope = [paintView](int scope) {
        paintView->setFillScope(static_cast<PaintOpenGLWidget::FillScope>(scope));
    };
    adapter.setSmoothValue = [paintView](int value) { paintView->setSmoothValue(value); };
    adapter.setStrokeFitSettings = [paintView](const AnimeStrokeFitSettings &settings) {
        paintView->setStrokeFitSettings(settings);
    };
    adapter.sendExtraToolMessage = [paintView](const String &name, const String &property) {
        paintView->sendPythonExtraToolMessage(name, property);
    };
    adapter.sendToolOptionMessage = [paintView](const String &hook,
                                                const String &name,
                                                const String &type,
                                                const JsonVariant &value,
                                                int row,
                                                int startColumn,
                                                int endColumn) {
        paintView->sendPythonToolOptionMessage(hook, name, type, value, row, startColumn, endColumn);
    };
    adapter.sendPadMessage = [paintView](const String &pad,
                                         const String &phase,
                                         double x,
                                         double y) {
        paintView->sendPythonPadMessage(pad, phase, x, y);
    };

    adapter.buildPlaybackCache = [paintView](int frameCount, AnimeCore::String *error) {
        return paintView->buildPlaybackCache(frameCount, error);
    };
    adapter.showPlaybackFrame = [paintView](int frameIndex) {
        paintView->showPlaybackFrame(frameIndex);
    };
    adapter.endPlayback = [paintView]() { paintView->endPlayback(); };

    adapter.setOnionEnabled = [paintView](bool on) { paintView->setOnionEnabled(on); };
    adapter.setOnionFrames = [paintView](const Set<int> &frames) {
        paintView->setOnionFrames(frames);
    };
    adapter.setOnionLines = [paintView](bool on) { paintView->setOnionLines(on); };
    adapter.setOnionFills = [paintView](bool on) { paintView->setOnionFills(on); };
    adapter.setOnionAmLayers = [paintView](bool on) { paintView->setOnionAmLayers(on); };
    adapter.setOnionGuideLines = [paintView](bool on) { paintView->setOnionGuideLines(on); };
    adapter.setOnionExcludeProperties = [paintView](const StringList &properties) {
        paintView->setOnionExcludeProperties(properties);
    };
    adapter.setOnionLayerTag = [paintView](const String &tag) {
        paintView->setOnionLayerTag(tag);
    };

    adapter.renderFrameThumbnail = [paintView](int frameIndex, const Size &size) {
        return paintView->renderFrameThumbnail(frameIndex, size);
    };

    m_business->bindPaintView(viewName, adapter);

    paintView->focusGained.connect([this, viewName]() {
        m_business->setActiveViewName(viewName);
    });
    paintView->layerListChanged.connect([this, viewName, paintView](int selectedLayer) {
        m_business->updateAttention(viewName,
                                    AttentionChange::LayerChange,
                                    paintView->model().currentFrame(),
                                    selectedLayer,
                                    paintView->model().currentAsset());
    });
    paintView->assetListChanged.connect([this, viewName](int selectedAsset) {
        const SelectionAttention current = m_business->attentionForView(viewName);
        m_business->updateAttention(viewName,
                                    AttentionChange::AssetChange,
                                    current.frame,
                                    current.layer,
                                    selectedAsset);
    });
    paintView->pythonDebugMessage.connect([this](const AnimeCore::String &msg) { appendPythonDebug(msg); });
    paintView->playbackInterrupted.connect([this]() { m_business->stopPlayback(); });
    paintView->historyChanged.connect([this]() {
        markDirty(String("history"));
        markDirty(String("menus"));
    });
    paintView->historyCommitted.connect([this, viewName]() {
        m_business->handleViewHistoryCommitted(viewName);
        markDirty(String("history"));
        markDirty(String("menus"));
    });
    paintView->viewTransformChanged.connect([this, viewName]() {
        markDirty(viewName == String("child") ? String("viewport/child")
                                                      : String("viewport/main"));
    });
    paintView->userTransformed.connect([this, viewName]() {
        if (viewName == String("child")) {
            if (TextureViewSlot *slot = textureViewSlotFor(m_textureHome)) {
                slot->autoFitSuspended = true;
            }
        }
        markDirty(viewName == String("child") ? String("viewport/child")
                                                      : String("viewport/main"));
    });
}

void BackendSession::setupBusinessConnections()
{
    m_business->statusMessageChanged.connect([this](const String &) {
        markDirty(String("window"));
    });
    m_business->pythonDebugMessage.connect([this](const String &message) { appendPythonDebug(message); });
    m_business->projectFilePathChanged.connect([this](const String &) {
        markDirty(String("window"));
    });
    m_business->childFilePathChanged.connect([this](const String &) {
        markDirty(String("window"));
    });
    m_business->windowTitleChanged.connect([this](const String &) {
        markDirty(String("window"));
    });
    m_business->canvasSizeChanged.connect([this](const String &viewName, const Size &) {
        markDirty(String("window"));
        markDirty(viewName == String("child") ? String("viewport/child")
                                                      : String("viewport/main"));
    });
    m_business->activeViewChanged.connect([this](const String &activeView) {
        if (m_mainBoard) {
            m_mainBoard->setActiveIndicator(activeView == String("main"));
        }
        if (m_childBoard) {
            m_childBoard->setActiveIndicator(activeView == String("child"));
        }
        markDirty(String("window"));
        markDirty(String("tools"));
        markDirty(String("history"));
        markDirty(String("timeline"));
        markDirty(String("layers/main"));
        markDirty(String("layers/child"));
        markDirty(String("assets/main"));
        markDirty(String("assets/child"));
    });
    m_business->requestSurfaceTextureView.connect([this]() {
        surfaceTextureView();
    });
    m_business->requestAutoFitTextureControlView.connect([this]() {
        autoFitTextureControlView();
    });
    m_business->requestClearTimelineThumbnails.connect([this]() {
        ++m_thumbnailRevision;
        for (auto it = m_cachedImages.begin(); it != m_cachedImages.end();) {
            if (it.key().startsWith(String("thumb_"))) {
                it = m_cachedImages.erase(it);
            } else {
                ++it;
            }
        }
        markDirty(String("thumbnails"));
    });
    m_business->layersChanged.connect([this](const String &viewName, int) {
        if (viewName == String("child")) {
            markDirty(String("layers/child"));
        } else {
            markDirty(String("layers/main"));
        }
    });
    m_business->assetsChanged.connect([this](const String &viewName, int) {
        if (viewName == String("child")) {
            markDirty(String("assets/child"));
        } else {
            markDirty(String("assets/main"));
        }
    });
    m_business->timelineChanged.connect([this]() {
        markDirty(String("timeline"));
        markDirty(String("layers/main"));
        markDirty(String("layers/child"));
    });
    m_business->playbackStateChanged.connect([this](bool, int, const String &) {
        markDirty(String("timeline"));
    });
    m_business->onionStateChanged.connect([this]() {
        markDirty(String("timeline"));
    });
    m_business->toolChanged.connect([this](int, const String &) {
        markDirty(String("tools"));
    });
    m_business->toolLockStateChanged.connect([this]() {
        markDirty(String("tools"));
    });
    m_business->toolOptionsLayoutChanged.connect(
            [this](const JsonObject &, int, bool) {
        markDirty(String("toolOptions"));
    });
    m_business->toolOptionsStateChanged.connect([this]() {
        markDirty(String("drawSettings"));
    });
    m_business->drawingColorChanged.connect([this](const Color &) {
        markDirty(String("drawColor"));
    });
    m_business->historyChanged.connect([this]() {
        markDirty(String("history"));
        markDirty(String("menus"));
    });
    m_business->historyRestored.connect([this](const String &viewName) {
        markDirty(String("history"));
        markDirty(String("menus"));
        markDirty(String("timeline"));
        markDirty(String("layers/main"));
        markDirty(String("layers/child"));
        markDirty(String("assets/main"));
        markDirty(String("assets/child"));
        if (PaintOpenGLWidget *board = boardForView(viewName)) {
            board->update();
        }
    });

    AnimeTheme::instance()->themeChanged.connect([this]() {
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        if (m_mainBoard) {
            m_mainBoard->update();
        }
        if (m_childBoard) {
            m_childBoard->update();
        }
        markDirty(String("theme"));
        markDirty(String("menus"));
        markDirty(String("history"));
    });
}

void BackendSession::registerPythonUiCallbacks()
{
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&m_childBoard->model());
    registerAnimeanUiScene(&m_mainBoard->model());
    registerAnimeanUiScene(&boardForView(m_business->activeViewName())->model());

    registerAnimeanUiToolOptionsCallback([this]() {
        m_business->refreshToolOptions();
    });

    registerAnimeanUiRefreshCallback([this](bool frame, bool layer, bool asset, bool widget) {
        m_business->syncAllViewAttentionsFromModels();
        if (frame || layer || widget) {
            m_business->requestClearTimelineThumbnails();
        }
        if (frame) {
            markDirty(String("timeline"));
            if (m_childBoard && m_childBoard->model().currentFrame() != m_textureAutoFitFrame) {
                autoFitTextureControlView();
            }
        }
        if (layer) {
            markDirty(String("layers/main"));
            markDirty(String("layers/child"));
        }
        if (asset) {
            markDirty(String("assets/main"));
            markDirty(String("assets/child"));
        }
        if (widget) {
            if (m_mainBoard) {
                m_mainBoard->modelReplaced();
            }
            if (m_childBoard) {
                m_childBoard->modelReplaced();
            }
        }
        m_business->syncEmbeddedPythonState();
    });

    registerAnimeanUiFreezeCallback([this](bool frozen) {
        setPythonUiFrozen(frozen);
    });

    AnimeanWindowsApi windows;
    windows.list = [this]() {
        return m_windowInfos;
    };
    windows.show = [this](const String &name, bool on) {
        if (name == String("paint")) {
            return;
        }
        if (name == String("timeline")) {
            m_timelineVisible = on;
            saveTimelineSettings();
            markDirty(String("windows"));
            markDirty(String("timeline"));
            markDirty(String("menus"));
            emitUiEvent(String("window.show"),
                        JsonObject{{String("name"), name}, {String("visible"), on}});
            return;
        }
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == name) {
                info.visible = on;
                markDirty(String("windows"));
                markDirty(String("menus"));
                emitUiEvent(String("window.show"),
                            JsonObject{{String("name"), name}, {String("visible"), on}});
                return;
            }
        }
    };
    windows.select = [this](const String &name, const String &page) {
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == name) {
                if (info.pages.contains(page)) {
                    info.current = page;
                }
                if (name == String("paint")) {
                    if (page == String("texture")) {
                        m_business->setActiveViewName(String("child"));
                    } else if (page == String("drawing")) {
                        m_business->setActiveViewName(String("main"));
                    }
                }
                markDirty(String("windows"));
                if (name == String("tools")) {
                    markDirty(String("tools"));
                }
                emitUiEvent(String("window.select"),
                            JsonObject{{String("name"), name}, {String("page"), page}});
                return;
            }
        }
    };
    registerAnimeanUiWindowsCallback(std::move(windows));

    registerAnimeanUiOverlayCallback([this](const String &view, const Vector<AnimeanOverlayItem> &items) {
        PaintOpenGLWidget *target = boardForView(view);
        if (!target) {
            return;
        }
        Vector<PaintOpenGLWidget::OverlayItem> converted;
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
            overlayItem.confirmable = item.confirmable;
            overlayItem.draggable = item.draggable;
            overlayItem.label = item.label;
            converted.append(overlayItem);
        }
        target->setOverlayItems(converted);
    });

    registerAnimeanUiEditHandleCallback([this](const String &view, const Vector<AnimeanEditHandle> &handles) {
        PaintOpenGLWidget *target = boardForView(view);
        if (!target) {
            return;
        }
        Vector<PaintOpenGLWidget::EditHandle> converted;
        converted.reserve(handles.size());
        for (const AnimeanEditHandle &handle : handles) {
            PaintOpenGLWidget::EditHandle editHandle;
            editHandle.id = handle.id;
            editHandle.pos = handle.pos;
            editHandle.shape = handle.shape;
            editHandle.color = handle.color;
            editHandle.interactive = handle.interactive;
            converted.append(editHandle);
        }
        target->setEditHandles(converted);
    });

    registerAnimeanUiCursorCallback([this](const String &view, const String &name) {
        PaintOpenGLWidget *target = boardForView(view);
        if (target) {
            target->setScriptCursor(name);
        }
    });

    registerAnimeanUiFillPaintModeCallback([this](const String &view, bool on) {
        if (view == String("main") && m_mainBoard) {
            m_mainBoard->setFillPaintMode(on);
            return;
        }
        if (view == String("child") && m_childBoard) {
            m_childBoard->setFillPaintMode(on);
            return;
        }
        appendPythonDebug(String("[fill paint] unknown view '%1'").arg(view));
    });

    registerAnimeanUiLockedToolsCallback([this](const String &view, const StringList &tools) {
        m_business->applyLockedTools(view, tools);
    });

    registerAnimeanUiDrawColorCallback([this](const Color &color) {
        m_business->setDrawingColor(color);
    });

    registerAnimeanUiDrawSettingsCallback([this](int stabilizer, int simplify, int corner) {
        m_business->setDrawSettings(stabilizer, simplify, corner);
    });

    registerAnimeanUiHistoryCallback([this](const String &op, const String &viewName, const String &label) {
        if (!viewName.isEmpty()
            && viewName != String("main")
            && viewName != String("child")) {
            appendPythonDebug(String("[history] unknown view '%1'").arg(viewName));
            return;
        }
        if (op == String("commit")) {
            m_business->commitHistory(label.isEmpty() ? String("Python") : label, viewName);
        } else if (op == String("undo")) {
            m_business->undo(viewName);
        } else if (op == String("redo")) {
            m_business->redo(viewName);
        }
    });

    registerAnimeanUiPadValueCallback([this](const String &pad, double x, double y) {
        const PointF pt(x, y);
        m_padValues.insert(pad, pt);
        if (pad == String("force_pad")) {
            m_forcePadValue = pt;
        }
        markDirty(String("pad"));
    });
#endif
}

PaintOpenGLWidget *BackendSession::boardForView(const String &viewName) const
{
    if (viewName == String("child")) {
        return m_childBoard.get();
    }
    if (viewName == String("main")) {
        return m_mainBoard.get();
    }
    return (m_business && m_business->activeViewName() == String("child"))
               ? m_childBoard.get()
               : m_mainBoard.get();
}

String BackendSession::resolveViewName(const String &viewName) const
{
    if (viewName == String("child")) {
        return String("child");
    }
    if (viewName == String("main")) {
        return String("main");
    }
    return (m_business && m_business->activeViewName() == String("child"))
               ? String("child")
               : String("main");
}

void BackendSession::onClientConnected()
{
    for (auto it = m_cachedImages.cbegin(); it != m_cachedImages.cend(); ++it) {
        sendImagePacket(it.key(), it.value());
    }

    m_dirtyKeys.clear();
    for (const String &key : allStateKeys()) {
        sendStateKey(key);
    }

    if (m_mainBoard) {
        m_mainBoard->forgetSentImages();
        m_mainBoard->acknowledgeFrame(m_lastBoardFrameId.value(String("main"), 0));
        m_mainBoard->resizeSurface(m_mainBoard->width(),
                                   m_mainBoard->height(),
                                   m_mainBoard->devicePixelRatioF());
    }
    if (m_childBoard) {
        m_childBoard->forgetSentImages();
        m_childBoard->acknowledgeFrame(m_lastBoardFrameId.value(String("child"), 0));
        m_childBoard->resizeSurface(m_childBoard->width(),
                                    m_childBoard->height(),
                                    m_childBoard->devicePixelRatioF());
    }
}

void BackendSession::onClientDisconnected()
{
    // Keep session alive and wait for next connection.
}

void BackendSession::onMessageReceived(const JsonObject &msg)
{
    const String t = msg.value(String("t"))
                          .toString(msg.value(String("type")).toString());
    if (t == String("call")) {
        handleCallMessage(msg);
    } else if (t == String("input")) {
        handleInputMessage(msg);
    } else if (t == String("ack")) {
        handleAckMessage(msg);
    }
}

void BackendSession::handleCallMessage(const JsonObject &msg)
{
    const JsonValue idVal = msg.value(String("id"));
    const String method = msg.value(String("method")).toString();
    const JsonObject params = msg.value(String("params")).toObject();

    // Ensure any pending board frame is unblocked so mutations from this call produce frames.
    if (m_mainBoard) {
        m_mainBoard->acknowledgeFrame(m_lastBoardFrameId.value(String("main"), 0));
    }
    if (m_childBoard) {
        m_childBoard->acknowledgeFrame(m_lastBoardFrameId.value(String("child"), 0));
    }

    JsonObject reply;
    reply.insert(String("t"), String("reply"));
    reply.insert(String("id"), idVal);

    try {
        const JsonObject result = dispatchCall(method, params);
        if (result.value(String("__error__")).toBool(false)) {
            reply.insert(String("ok"), false);
            reply.insert(String("error"),
                         result.value(String("error")).toString(String("Call failed")));
        } else {
            reply.insert(String("ok"), true);
            reply.insert(String("result"), result);
        }
    } catch (const std::exception &ex) {
        reply.insert(String("ok"), false);
        reply.insert(String("error"), String::fromUtf8(ex.what()));
    } catch (...) {
        reply.insert(String("ok"), false);
        reply.insert(String("error"), String("Unknown exception during call"));
    }

    sendJson(reply);
}

void BackendSession::handleInputMessage(const JsonObject &msg)
{
    const String view = resolveViewName(msg.value(String("view")).toString(String("main")));
    PaintOpenGLWidget *board = boardForView(view);
    if (!board) {
        return;
    }

    board->acknowledgeFrame(m_lastBoardFrameId.value(view, 0));

    const String kind = msg.value(String("kind")).toString();
    const double x = msg.value(String("x")).toDouble(0.0);
    const double y = msg.value(String("y")).toDouble(0.0);
    const AnimeCore::KeyboardModifiers modifiers =
        static_cast<AnimeCore::KeyboardModifiers>(msg.value(String("modifiers")).toInt(0));
    const std::uint64_t timestamp = std::uint64_t(std::max(0.0, msg.value(String("time")).toDouble(0.0)));

    if (kind == String("press")) {
        AnimeCore::MouseButton button = static_cast<AnimeCore::MouseButton>(msg.value(String("button")).toInt(int(AnimeCore::LeftButton)));
        if (button == AnimeCore::NoButton && !msg.contains(String("button"))) {
            button = AnimeCore::LeftButton;
        }
        AnimeCore::MouseButtons buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(int(button)));
        if (buttons == AnimeCore::NoButton && !msg.contains(String("buttons"))) {
            buttons = button;
        }
        board->injectMouse(AnimeCore::Event::MouseButtonPress, PointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == String("move")) {
        AnimeCore::MouseButton button = static_cast<AnimeCore::MouseButton>(msg.value(String("button")).toInt(int(AnimeCore::NoButton)));
        AnimeCore::MouseButtons buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(int(AnimeCore::LeftButton)));
        if (msg.contains(String("buttons"))) {
            buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(0));
        }
        board->injectMouse(AnimeCore::Event::MouseMove, PointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == String("release")) {
        AnimeCore::MouseButton button = static_cast<AnimeCore::MouseButton>(msg.value(String("button")).toInt(int(AnimeCore::LeftButton)));
        if (button == AnimeCore::NoButton && !msg.contains(String("button"))) {
            button = AnimeCore::LeftButton;
        }
        AnimeCore::MouseButtons buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(int(AnimeCore::NoButton)));
        board->injectMouse(AnimeCore::Event::MouseButtonRelease, PointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == String("doubleClick") || kind == String("dblclick")) {
        AnimeCore::MouseButton button = static_cast<AnimeCore::MouseButton>(msg.value(String("button")).toInt(int(AnimeCore::LeftButton)));
        AnimeCore::MouseButtons buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(int(button)));
        board->injectMouse(AnimeCore::Event::MouseButtonDblClick, PointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == String("wheel")) {
        const int dx = msg.value(String("dx")).toInt(0);
        const int dy = msg.value(String("dy")).toInt(0);
        const AnimeCore::MouseButtons buttons = static_cast<AnimeCore::MouseButtons>(msg.value(String("buttons")).toInt(0));
        board->injectWheel(PointF(x, y), Point(dx, dy), modifiers, buttons);
    } else if (kind == String("enter")) {
        board->injectEnter();
    } else if (kind == String("leave")) {
        board->injectLeave();
    } else if (kind == String("resize")) {
        const int w = msg.value(String("w")).toInt(msg.value(String("width")).toInt(board->width()));
        const int h = msg.value(String("h")).toInt(msg.value(String("height")).toInt(board->height()));
        const double dpr = msg.value(String("dpr")).toDouble(1.0);
        board->resizeSurface(w, h, dpr);
        if (view == String("child") && m_textureHome == String("control")) {
            autoFitTextureControlView();
        }
        markDirty(view == String("child") ? String("viewport/child")
                                                  : String("viewport/main"));
    } else if (kind == String("focus")) {
        const bool focused = msg.value(String("focused")).toBool(true);
        if (focused) {
            board->injectFocus();
        } else {
            board->injectBlur();
        }
    }
}

void BackendSession::handleAckMessage(const JsonObject &msg)
{
    const String view = resolveViewName(msg.value(String("view")).toString(String("main")));
    PaintOpenGLWidget *board = boardForView(view);
    if (!board) {
        return;
    }
    const uint64_t frameId = static_cast<uint64_t>(msg.value(String("frame")).toInteger(0));
    board->acknowledgeFrame(frameId);
}

JsonObject BackendSession::dispatchCall(const String &method, const JsonObject &params)
{
    auto makeError = [](const String &err) {
        return JsonObject{
            {String("__error__"), true},
            {String("error"), err},
        };
    };

    const String view = params.value(String("view"))
                             .toString(params.value(String("viewName")).toString(String("main")));

    if (method == String("project.new")) {
        const Size current = m_business->canvasSize(String("main"));
        int w = params.value(String("width")).toInt(params.value(String("w")).toInt(current.width()));
        int h = params.value(String("height")).toInt(params.value(String("h")).toInt(current.height()));
        if (params.value(String("canvasSize")).isObject()) {
            const JsonObject cs = params.value(String("canvasSize")).toObject();
            w = cs.value(String("width")).toInt(cs.value(String("w")).toInt(w));
            h = cs.value(String("height")).toInt(cs.value(String("h")).toInt(h));
        }
        w = std::max(1, w);
        h = std::max(1, h);
        m_business->newProject(Size(w, h));
        return JsonObject{{String("width"), w}, {String("height"), h}};
    }

    if (method == String("project.open")) {
        const String fileName = params.value(String("path"))
                                     .toString(params.value(String("fileName"))
                                                   .toString(params.value(String("filePath")).toString()));
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        String error;
        const bool ok = (view == String("child"))
                            ? [&]{ AnimeCore::String e; const bool r = m_business->openTextureViewFrom(fileName, &e); error = e; return r; }()
                            : [&]{ AnimeCore::String e; const bool r = m_business->loadProjectFrom(fileName, &e); error = e; return r; }();
        if (!ok) {
            return makeError(error.isEmpty() ? String("Failed to open file") : error);
        }
        const String openedPath = (view == String("child"))
                                       ? m_business->childFilePath()
                                       : m_business->currentFilePath();
        return JsonObject{
            {String("path"), openedPath},
            {String("fileName"), openedPath},
        };
    }

    if (method == String("project.save")) {
        String fileName = params.value(String("path"))
                               .toString(params.value(String("fileName"))
                                             .toString(params.value(String("filePath")).toString()));
        if (fileName.isEmpty()) {
            fileName = (view == String("child"))
                           ? m_business->childFilePath()
                           : m_business->currentFilePath();
        }
        if (fileName.isEmpty()) {
            return makeError(String("No file path specified for save"));
        }
        const String targetName = (view == String("child"))
                                      ? AnimeanBusiness::ensureTextureViewFileExtension(fileName)
                                      : AnimeanBusiness::ensureProjectFileExtension(fileName);
        const bool divergentExists = (targetName != fileName && FileInfo::exists(targetName));
        if (params.value(String("probe")).toBool(false)) {
            return JsonObject{
                {String("probe"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
                {String("needsConfirmOverwrite"), divergentExists},
            };
        }
        if (divergentExists && params.contains(String("overwrite")) && !params.value(String("overwrite")).toBool(true)) {
            return JsonObject{
                {String("cancelled"), true},
                {String("needsConfirmOverwrite"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
            };
        }
        String error;
        const bool ok = (view == String("child"))
                            ? [&]{ AnimeCore::String e; const bool r = m_business->saveTextureViewTo(fileName, &e); error = e; return r; }()
                            : [&]{ AnimeCore::String e; const bool r = m_business->saveProjectTo(fileName, &e); error = e; return r; }();
        if (!ok) {
            return makeError(error.isEmpty() ? String("Failed to save file") : error);
        }
        const String savedPath = (view == String("child"))
                                      ? m_business->childFilePath()
                                      : m_business->currentFilePath();
        return JsonObject{
            {String("path"), savedPath},
            {String("fileName"), savedPath},
        };
    }

    if (method == String("project.saveAs")) {
        const String fileName = params.value(String("path"))
                                     .toString(params.value(String("fileName"))
                                                   .toString(params.value(String("filePath")).toString()));
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        const String targetName = (view == String("child"))
                                      ? AnimeanBusiness::ensureTextureViewFileExtension(fileName)
                                      : AnimeanBusiness::ensureProjectFileExtension(fileName);
        const bool divergentExists = (targetName != fileName && FileInfo::exists(targetName));
        if (params.value(String("probe")).toBool(false)) {
            return JsonObject{
                {String("probe"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
                {String("needsConfirmOverwrite"), divergentExists},
            };
        }
        if (divergentExists && params.contains(String("overwrite")) && !params.value(String("overwrite")).toBool(true)) {
            return JsonObject{
                {String("cancelled"), true},
                {String("needsConfirmOverwrite"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
            };
        }
        String error;
        const bool ok = (view == String("child"))
                            ? [&]{ AnimeCore::String e; const bool r = m_business->saveTextureViewTo(fileName, &e); error = e; return r; }()
                            : [&]{ AnimeCore::String e; const bool r = m_business->saveProjectTo(fileName, &e); error = e; return r; }();
        if (!ok) {
            return makeError(error.isEmpty() ? String("Failed to save file") : error);
        }
        const String savedPath = (view == String("child"))
                                      ? m_business->childFilePath()
                                      : m_business->currentFilePath();
        return JsonObject{
            {String("path"), savedPath},
            {String("fileName"), savedPath},
        };
    }

    if (method == String("file.import")) {
        return handleFileImportCall(params);
    }

    if (method == String("file.texture")) {
        return handleFileTextureCall(params);
    }

    if (method == String("layer.contextMenu")) {
        // Same order as MainWindow::showLayerContextMenu: the group's own two
        // actions, then whatever Python registered for this target.
        const String viewName = params.value(String("view")).toString(String("main"));
        PaintOpenGLWidget *board = boardForView(viewName);
        if (!board) {
            return makeError(String("Unknown view"));
        }
        const int groupId = params.value(String("groupId")).toInt(0);
        const int layerIndex = groupId > 0 ? -1 : params.value(String("layer")).toInt(-1);
        const bool hasItem = groupId > 0 || layerIndex >= 0;
        JsonArray entries;
#ifdef ANIMEAN_WITH_PYTHON
        py::dict context;
        context["view"] = viewName.toStdString();
        context["kind"] = !hasItem ? "panel" : (groupId > 0 ? "group" : "layer");
        context["group"] = groupId;
        context["group_name"] = params.value(String("groupName")).toString().toStdString();
        context["layer"] = layerIndex;
        context["layer_name"] = layerIndex >= 0 ? board->layerName(layerIndex).toStdString() : std::string();
        context["layer_id"] = layerIndex >= 0 ? board->model().layerIdAt(layerIndex) : 0;
        context["tag"] = groupId > 0 ? board->model().layerGroupTag(groupId).toStdString() : std::string();
        const int ownerGroup = layerIndex >= 0 ? board->model().groupIdForLayer(layerIndex) : 0;
        context["owner_group"] = ownerGroup;
        context["owner_tag"] = ownerGroup > 0 ? board->model().layerGroupTag(ownerGroup).toStdString() : std::string();
        context["parent_layer_id"] = layerIndex >= 0 ? board->model().layerParentId(layerIndex) : 0;
        context["child_count"] = layerIndex >= 0 ? board->model().childLayerIndices(layerIndex).size() : 0;
        py::list memberList;
        for (const JsonValue &member : params.value(String("members")).toArray()) {
            memberList.append(member.toInt());
        }
        context["members"] = memberList;
        const std::string json = py::module_::import("python_hooks").attr("menu_items_json")(context).cast<std::string>();
        entries = JsonDocument::fromJson(json).array();
#endif
        JsonArray items;
        const auto addItem = [&items](const String &id, const String &text, const String &kind, bool enabled) {
            items.append(JsonObject{{String("id"), id}, {String("text"), text},
                                     {String("kind"), kind}, {String("enabled"), enabled}});
        };
        if (groupId > 0) {
            addItem(String("ungroup"), String("Ungroup (keep the layers)"), String("action"), true);
            addItem(String("deleteGroup"), String("Delete Group and Layers"), String("action"), true);
            if (!entries.isEmpty()) {
                addItem(String("sep"), String(), String("separator"), true);
            }
        }
        for (const JsonValue &value : entries) {
            const JsonObject object = value.toObject();
            const String kind = object.value(String("kind")).toString(String("action"));
            if (kind == String("separator")) {
                addItem(String("sep"), String(), kind, true);
                continue;
            }
            const String name = object.value(String("name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            const String title = object.value(String("title")).toString(name);
            const bool enabled = object.value(String("enabled")).toBool(true);
            if (kind == String("settings")) {
                addItem(String("settings:") + object.value(String("settings")).toString(name), title,
                        String("action"), enabled);
            } else {
                addItem(String("script:") + name, title, String("action"), enabled);
            }
        }
        return JsonObject{{String("items"), items}};
    }

    if (method == String("layer.contextAction")) {
        const String viewName = params.value(String("view")).toString(String("main"));
        PaintOpenGLWidget *board = boardForView(viewName);
        if (!board) {
            return makeError(String("Unknown view"));
        }
        const String id = params.value(String("id")).toString();
        const int groupId = params.value(String("groupId")).toInt(0);
        const int layerIndex = groupId > 0 ? -1 : params.value(String("layer")).toInt(-1);
        if (id == String("ungroup")) {
            m_business->dissolveLayerGroup(groupId, viewName);
        } else if (id == String("deleteGroup")) {
            m_business->deleteLayerGroup(groupId, viewName);
        } else if (id.startsWith(String("settings:"))) {
            openScriptSettings(id.mid(9), params.value(String("title")).toString());
        } else if (id.startsWith(String("script:"))) {
            Vector<int> members;
            for (const JsonValue &member : params.value(String("members")).toArray()) {
                members.append(member.toInt());
            }
            board->sendPythonLayerMenuMessage(id.mid(7), groupId, params.value(String("groupName")).toString(),
                                              layerIndex, layerIndex >= 0 ? String(board->layerName(layerIndex)) : String(), members);
        } else {
            return makeError(String("Unknown layer action"));
        }
        markDirty(viewName == String("child") ? String("layers/child") : String("layers/main"));
        markDirty(String("timeline"));
        return JsonObject{};
    }

    if (method == String("layer.add")) {
        const int layerIndex = m_business->addLayer(view);
        return JsonObject{{String("layer"), layerIndex}};
    }

    if (method == String("layer.delete")) {
        const int layerIndex = params.value(String("layerIndex"))
                                   .toInt(params.value(String("layer"))
                                              .toInt(params.value(String("index"))
                                                         .toInt(m_business->currentLayer(view))));
        const bool removed = m_business->deleteLayer(layerIndex, view);
        return JsonObject{
            {String("deleted"), removed},
            {String("layer"), m_business->currentLayer(view)},
        };
    }

    if (method == String("layer.deleteGroup")) {
        const int groupId = params.value(String("groupId"))
                                .toInt(params.value(String("group_id"))
                                           .toInt(params.value(String("id")).toInt(0)));
        const int deleted = m_business->deleteLayerGroup(groupId, view);
        return JsonObject{{String("deleted"), deleted}};
    }

    if (method == String("layer.dissolveGroup")) {
        const int groupId = params.value(String("groupId"))
                                .toInt(params.value(String("group_id"))
                                           .toInt(params.value(String("id")).toInt(0)));
        const bool dissolved = m_business->dissolveLayerGroup(groupId, view);
        return JsonObject{{String("dissolved"), dissolved}};
    }

    if (method == String("layer.move")) {
        const int fromIndex = params.value(String("fromIndex"))
                                  .toInt(params.value(String("from")).toInt(-1));
        const int toIndex = params.value(String("toIndex"))
                                .toInt(params.value(String("to")).toInt(-1));
        const bool moved = m_business->moveLayer(fromIndex, toIndex, view);
        return JsonObject{{String("moved"), moved}};
    }

    if (method == String("layer.setVisible")) {
        const int layerIndex = params.value(String("layerIndex"))
                                   .toInt(params.value(String("layer"))
                                              .toInt(params.value(String("index"))
                                                         .toInt(m_business->currentLayer(view))));
        const bool visible = params.value(String("visible"))
                                 .toBool(params.value(String("on")).toBool(true));
        const bool ok = m_business->setLayerVisible(layerIndex, visible, view);
        return JsonObject{{String("visible"), visible}, {String("updated"), ok}};
    }

    if (method == String("layer.select")) {
        const int layerIndex = params.value(String("layerIndex"))
                                   .toInt(params.value(String("layer"))
                                              .toInt(params.value(String("index")).toInt(-1)));
        m_business->selectLayer(layerIndex, view);
        return JsonObject{{String("selected"), m_business->currentLayer(view)}};
    }

    if (method == String("layer.unselect")) {
        m_business->unselectLayer(view);
        return JsonObject{{String("selected"), m_business->currentLayer(view)}};
    }

    if (method == String("layer.setCollapsed")) {
        const int groupId = params.value(String("groupId"))
                                .toInt(params.value(String("group_id"))
                                           .toInt(params.value(String("id")).toInt(0)));
        const bool collapsed = params.value(String("collapsed"))
                                   .toBool(params.value(String("on")).toBool(false));
        m_business->setLayerGroupCollapsed(groupId, collapsed, view);
        markDirty(view == String("child") ? String("layers/child")
                                                  : String("layers/main"));
        return JsonObject{{String("groupId"), groupId}, {String("collapsed"), collapsed}};
    }

    if (method == String("layer.applyStructure")) {
        const JsonArray rowsArr = params.value(String("rows")).toArray();
        Vector<AnimeLayerPanelRow> rows;
        rows.reserve(rowsArr.size());
        for (const JsonValue &val : rowsArr) {
            if (!val.isObject()) {
                continue;
            }
            const JsonObject obj = val.toObject();
            AnimeLayerPanelRow row;
            row.group = obj.value(String("group")).toBool(false);
            row.groupId = obj.value(String("groupId"))
                              .toInt(obj.value(String("group_id")).toInt(0));
            row.collapsed = obj.value(String("collapsed")).toBool(false);
            row.layerIndex = obj.value(String("layerIndex"))
                                 .toInt(obj.value(String("layer"))
                                            .toInt(obj.value(String("index")).toInt(-1)));
            row.name = obj.value(String("name")).toString();
            row.visible = obj.value(String("visible")).toBool(true);
            row.depth = obj.value(String("depth")).toInt(0);
            row.child = obj.value(String("child")).toBool(false);
            row.parent = obj.value(String("parent")).toBool(false);
            rows.append(row);
        }
        int movedColumnId = params.value(String("movedColumnId")).toInt(0);
        if (movedColumnId <= 0) {
            const int movedLayer = params.value(String("movedLayer"))
                                       .toInt(params.value(String("movedIndex")).toInt(-1));
            if (movedLayer >= 0) {
                movedColumnId = m_business->modelForView(view).layerIdAt(movedLayer);
            }
        }
        const int landedOn = m_business->applyLayerPanelStructure(view, rows, movedColumnId);
        markDirty(view == String("child") ? String("layers/child")
                                                  : String("layers/main"));
        markDirty(String("timeline"));
        return JsonObject{
            {String("landedOn"), landedOn},
            {String("selected"), m_business->currentLayer(view)},
        };
    }

    if (method == String("tool.select")) {
        JsonValue toolVal = params.value(String("tool"));
        if (toolVal.isUndefined()) {
            toolVal = params.value(String("name"));
        }
        bool ok = false;
        const AnimeanBusiness::Tool tool = parseToolValue(toolVal, &ok);
        if (!ok) {
            return makeError(String("Unknown tool: %1").arg(toolVal.toVariant().toString()));
        }
        m_business->selectTool(int(tool));
        return JsonObject{
            {String("tool"), AnimeanBusiness::toolControlName(m_business->currentToolEnum())},
            {String("toolEnum"), toolEnumDisplayName(m_business->currentToolEnum())},
        };
    }

    if (method == String("tool.selectExtra")) {
        const String name = params.value(String("extraToolName"))
                                 .toString(params.value(String("name"))
                                               .toString(params.value(String("tool")).toString()));
        if (name.isEmpty()) {
            return makeError(String("extraToolName is required"));
        }
        m_business->selectExtraToolByName(name);
        return JsonObject{{String("activeExtraTool"), m_business->activeExtraTool()}};
    }

    if (method == String("tool.option") || method == String("settings.option")) {
        const String hook = params.value(String("hook")).toString();
        const String name = params.value(String("name"))
                                 .toString(params.value(String("optionName")).toString());
        const String type = params.value(String("type")).toString();
        const JsonVariant value = params.value(String("value")).toVariant();
        const int row = params.value(String("row")).toInt(-1);
        const int startColumn = params.value(String("startColumn"))
                                    .toInt(params.value(String("start_column")).toInt(-1));
        const int endColumn = params.value(String("endColumn"))
                                  .toInt(params.value(String("end_column")).toInt(-1));
        m_business->sendToolOption(hook, name, type, value, row, startColumn, endColumn);
        if (m_mainBoard) {
            m_mainBoard->update();
        }
        if (m_childBoard) {
            m_childBoard->update();
        }
        return JsonObject{{String("hook"), hook}, {String("name"), name}};
    }

    if (method == String("tool.eraserMode")) {
        JsonValue toolVal = params.value(String("tool"));
        if (toolVal.isUndefined()) {
            toolVal = params.value(String("mode"));
        }
        const AnimeanBusiness::Tool tool = parseToolValue(toolVal);
        m_business->selectEraserMode(int(tool));
        return JsonObject{
            {String("tool"), AnimeanBusiness::toolControlName(m_business->currentToolEnum())},
        };
    }

    if (method == String("color.set")) {
        const Color color = parseColorParams(params);
        if (!color.isValid()) {
            return makeError(String("Invalid color"));
        }
        if (params.value(String("select")).toBool(false)) {
            m_business->selectColor(color);
        } else {
            m_business->setDrawingColor(color);
        }
        return buildDrawColorState();
    }

    if (method == String("draw.setSmooth")) {
        const int val = params.value(String("value"))
                            .toInt(params.value(String("smooth")).toInt(m_business->toolSmoothValue()));
        m_business->setSmoothValue(val);
        return JsonObject{{String("smooth"), m_business->toolSmoothValue()}};
    }

    if (method == String("draw.setPenWidth")) {
        const int width = params.value(String("width"))
                              .toInt(params.value(String("penWidth"))
                                         .toInt(params.value(String("value"))
                                                    .toInt(m_business->toolPenWidth())));
        m_business->setPenWidth(width);
        return JsonObject{{String("penWidth"), m_business->toolPenWidth()}};
    }

    if (method == String("draw.setSettings")) {
        const int stabilizer = params.value(String("stabilizer"))
                                   .toInt(params.value(String("smooth")).toInt(m_business->toolSmoothValue()));
        const int simplify = params.value(String("simplify"))
                                 .toInt(m_business->strokeFitSettings().simplify);
        const int corner = params.value(String("corner"))
                               .toInt(m_business->strokeFitSettings().corner);
        m_business->setDrawSettings(stabilizer, simplify, corner);
        return buildDrawSettingsState();
    }

    if (method == String("fill.setScope")) {
        bool allLayers = false;
        if (params.contains(String("allLayers"))) {
            allLayers = params.value(String("allLayers")).toBool();
        } else {
            const JsonValue scopeVal = params.value(String("scope"));
            if (scopeVal.isBool()) {
                allLayers = scopeVal.toBool();
            } else if (scopeVal.isDouble()) {
                allLayers = (scopeVal.toInt(0) == int(AnimeanBusiness::FillScope::AllLayers));
            } else {
                const String s = scopeVal.toString().trimmed().toLower();
                allLayers = (s == String("all") || s == String("alllayers") || s == String("all_layers"));
            }
        }
        m_business->setFillAllLayers(allLayers);
        return JsonObject{
            {String("fillScope"), m_business->toolFillAllLayers() ? String("all") : String("current")},
            {String("fillAllLayers"), m_business->toolFillAllLayers()},
        };
    }

    if (method == String("asset.add")) {
        AnimeColumnType colType = AnimeColumnType::Vector;
        const JsonValue typeVal = params.value(String("type"));
        if (typeVal.isDouble()) {
            colType = static_cast<AnimeColumnType>(typeVal.toInt(int(AnimeColumnType::Vector)));
        } else if (typeVal.isString() && typeVal.toString().trimmed().compare(String("raster"), CaseInsensitive) == 0) {
            colType = AnimeColumnType::Raster;
        }
        const String name = params.value(String("name")).toString();
        const int assetIndex = m_business->addAsset(colType, name, view);
        return JsonObject{{String("asset"), assetIndex}};
    }

    if (method == String("asset.select")) {
        const int assetIndex = params.value(String("assetIndex"))
                                   .toInt(params.value(String("asset"))
                                              .toInt(params.value(String("index")).toInt(-1)));
        m_business->selectAsset(assetIndex, view);
        return JsonObject{{String("selected"), m_business->currentAsset(view)}};
    }

    if (method == String("asset.unselect")) {
        m_business->unselectAsset(view);
        return JsonObject{{String("selected"), m_business->currentAsset(view)}};
    }

    if (method == String("asset.addLayer")) {
        const int assetIndex = params.value(String("assetIndex"))
                                   .toInt(params.value(String("asset"))
                                              .toInt(params.value(String("index"))
                                                         .toInt(m_business->currentAsset(view))));
        const int layerIndex = m_business->addLayerForAsset(assetIndex, view);
        return JsonObject{{String("layer"), layerIndex}};
    }

    if (method == String("asset.assign")) {
        const int layerIndex = params.value(String("layerIndex"))
                                   .toInt(params.value(String("layer")).toInt(m_business->currentLayer(view)));
        const int assetIndex = params.value(String("assetIndex"))
                                   .toInt(params.value(String("asset")).toInt(m_business->currentAsset(view)));
        const bool assigned = m_business->assignAssetToLayer(layerIndex, assetIndex, view);
        return JsonObject{{String("assigned"), assigned}};
    }

    if (method == String("history.undo")) {
        const String explicitView = params.value(String("view"))
                                         .toString(params.value(String("viewName")).toString());
        const bool ok = m_business->undo(explicitView);
        return JsonObject{{String("undone"), ok}, {String("canUndo"), m_business->canUndo()}};
    }

    if (method == String("history.redo")) {
        const String explicitView = params.value(String("view"))
                                         .toString(params.value(String("viewName")).toString());
        const bool ok = m_business->redo(explicitView);
        return JsonObject{{String("redone"), ok}, {String("canRedo"), m_business->canRedo()}};
    }

    if (method == String("history.goTo")) {
        const int row = params.value(String("row"))
                            .toInt(params.value(String("index")).toInt(-1));
        const String explicitView = params.value(String("view"))
                                         .toString(params.value(String("viewName")).toString());
        const bool ok = m_business->goToHistory(row, explicitView);
        return JsonObject{
            {String("moved"), ok},
            {String("currentIndex"), m_business->currentHistoryIndex(explicitView)},
        };
    }

    if (method == String("frame.select")) {
        const int frameIndex = params.value(String("frameIndex"))
                                   .toInt(params.value(String("frame"))
                                              .toInt(params.value(String("index")).toInt(0)));
        m_business->selectFrame(frameIndex);
        return JsonObject{{String("currentFrame"), m_business->currentFrame()}};
    }

    if (method == String("frame.step")) {
        const int delta = params.value(String("delta")).toInt(1);
        m_business->stepFrame(delta);
        return JsonObject{{String("currentFrame"), m_business->currentFrame()}};
    }

    if (method == String("frame.add")) {
        const int frameIndex = m_business->addFrame();
        return JsonObject{{String("frame"), frameIndex}};
    }

    if (method == String("frame.addHold")) {
        const int frameIndex = m_business->addHoldFrame();
        return JsonObject{{String("frame"), frameIndex}};
    }

    if (method == String("frame.duplicate")) {
        const int frameIndex = m_business->duplicateFrame();
        return JsonObject{{String("frame"), frameIndex}};
    }

    if (method == String("frame.delete")) {
        const bool deleted = m_business->deleteFrame();
        return JsonObject{{String("deleted"), deleted}};
    }

    if (method == String("frame.move")) {
        const int fromIndex = params.value(String("fromIndex"))
                                  .toInt(params.value(String("from")).toInt(-1));
        const int toIndex = params.value(String("toIndex"))
                                .toInt(params.value(String("to")).toInt(-1));
        const bool moved = m_business->moveFrame(fromIndex, toIndex);
        return JsonObject{{String("moved"), moved}};
    }

    if (method == String("frame.setFps")) {
        const int fps = params.value(String("fps")).toInt(12);
        m_business->setFps(fps);
        return JsonObject{{String("fps"), m_business->playbackFps()}};
    }

    if (method == String("playback.start")) {
        m_business->startPlayback();
        return JsonObject{{String("playbackActive"), m_business->isPlaybackActive()}};
    }

    if (method == String("playback.stop")) {
        m_business->stopPlayback();
        return JsonObject{{String("playbackActive"), m_business->isPlaybackActive()}};
    }

    if (method == String("playback.setLoop")) {
        const bool loop = params.value(String("loop"))
                              .toBool(params.value(String("on")).toBool(true));
        m_business->setPlaybackLoop(loop);
        return JsonObject{{String("loop"), m_business->isPlaybackLoop()}};
    }

    if (method == String("onion.setEnabled")) {
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("enabled")).toBool(false));
        m_business->setOnionEnabled(on);
        return JsonObject{{String("onionEnabled"), m_business->isOnionEnabled()}};
    }

    if (method == String("onion.setLines")) {
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("lines")).toBool(true));
        m_business->setOnionLines(on);
        return JsonObject{{String("onionLines"), m_business->isOnionLines()}};
    }

    if (method == String("onion.setFills")) {
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("fills")).toBool(true));
        m_business->setOnionFills(on);
        return JsonObject{{String("onionFills"), m_business->isOnionFills()}};
    }

    if (method == String("onion.setAmLayers")) {
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("amLayers")).toBool(true));
        m_business->setOnionAmLayers(on);
        return JsonObject{{String("onionAmLayers"), m_business->isOnionAmLayers()}};
    }

    if (method == String("onion.setGuideLines")) {
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("guideLines")).toBool(false));
        m_business->setOnionGuideLines(on);
        return JsonObject{{String("onionGuideLines"), m_business->isOnionGuideLines()}};
    }

    if (method == String("onion.toggleFrame")) {
        const int frame = params.value(String("frame"))
                              .toInt(params.value(String("frameIndex")).toInt(0));
        const bool currentlyOn = m_business->onionFrames().contains(frame);
        const bool on = params.contains(String("on"))
                            ? params.value(String("on")).toBool()
                            : !currentlyOn;
        m_business->toggleOnionFrame(frame, on);
        return JsonObject{{String("frame"), frame}, {String("on"), on}};
    }

    if (method == String("view.setActive")) {
        m_business->setActiveViewName(view);
        const String paintPage = (m_business->activeViewName() == String("child"))
                                     ? String("texture")
                                     : String("drawing");
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == String("paint")) {
                info.current = paintPage;
                markDirty(String("windows"));
                break;
            }
        }
        return JsonObject{{String("activeView"), m_business->activeViewName()}};
    }

    if (method == String("menu.refresh")) {
        markDirty(String("menus"));
        return JsonObject{{String("menus"), buildMenusState()}};
    }

    if (method == String("menu.trigger")) {
        const String id = params.value(String("id")).toString();
        String error;
        if (!triggerMenuById(id, params, &error)) {
            return makeError(error.isEmpty() ? String("Menu trigger failed") : error);
        }
        return JsonObject{{String("triggered"), true}};
    }

    if (method == String("pad.event")) {
        const String pad = params.value(String("pad")).toString(String("force_pad"));
        const String phase = params.value(String("phase")).toString(String("move"));
        const double x = params.value(String("x")).toDouble(0.0);
        const double y = params.value(String("y")).toDouble(0.0);
        m_padValues.insert(pad, PointF(x, y));
        if (pad == String("force_pad")) {
            m_forcePadValue = PointF(x, y);
        }
        markDirty(String("pad"));
        m_business->sendPadEvent(pad, phase, x, y);
        return JsonObject{{String("pad"), pad}, {String("x"), x}, {String("y"), y}};
    }

    if (method == String("theme.set")) {
        const JsonValue modeVal = params.contains(String("mode"))
                                       ? params.value(String("mode"))
                                       : params.value(String("theme"));
        AnimeTheme::Mode targetMode = AnimeTheme::Mode::Dark;
        if (modeVal.isDouble()) {
            targetMode = modeVal.toInt(0) == int(AnimeTheme::Mode::Light)
                             ? AnimeTheme::Mode::Light
                             : AnimeTheme::Mode::Dark;
        } else {
            const String s = modeVal.toString().trimmed().toLower();
            targetMode = (s == String("light")) ? AnimeTheme::Mode::Light : AnimeTheme::Mode::Dark;
        }
        AnimeTheme::setMode(targetMode);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(String("theme"));
        markDirty(String("menus"));
        return buildThemeState();
    }

    if (method == String("python.run")) {
        const String command = params.value(String("command"))
                                    .toString(params.value(String("code"))
                                                  .toString(params.value(String("script")).toString()));
        const String scriptDir = params.value(String("scriptDir")).toString();
        const String output = m_business->runEmbeddedPythonCommand(command, scriptDir);
        appendPythonDebug(String("> %1\n%2").arg(command.trimmed(), output.trimmed()));
        if (!buildTextureViewButtonsState().isEmpty()) {
            markDirty(String("windows"));
        }
        return JsonObject{{String("output"), output}};
    }

    if (method == String("board.resetView")) {
        PaintOpenGLWidget *board = boardForView(view);
        if (!board) {
            return makeError(String("Board not found"));
        }
        if (board->viewName() == String("child")) {
            if (!board->fitViewToContent(true)) {
                board->setViewTransform(1.0, PointF(0.0, 0.0));
            }
        } else {
            board->setViewTransform(1.0, PointF(0.0, 0.0));
        }
        return JsonObject{{String("view"), board->viewName()}};
    }

    if (method == String("board.scroll")) {
        PaintOpenGLWidget *board = boardForView(view);
        if (!board) {
            return makeError(String("Board not found"));
        }
        const String axis = params.value(String("axis")).toString().trimmed().toLower();
        if (axis != String("h") && axis != String("v")) {
            return makeError(String("Invalid axis: %1").arg(axis));
        }
        const int value = int(std::lround(params.value(String("value")).toDouble(0.0)));
        const JsonObject vp = buildViewportState(board->viewName());
        const int curH = vp.value(String("h")).toObject().value(String("value")).toInt(
            int(std::lround(-board->panOffset().x())));
        const int curV = vp.value(String("v")).toObject().value(String("value")).toInt(
            int(std::lround(-board->panOffset().y())));
        const int nextH = (axis == String("h")) ? value : curH;
        const int nextV = (axis == String("v")) ? value : curV;
        board->setScrollPosition(nextH, nextV);
        markDirty(board->viewName() == String("child") ? String("viewport/child")
                                                               : String("viewport/main"));
        return JsonObject{
            {String("view"), board->viewName()},
            {String("h"), int(std::lround(-board->panOffset().x()))},
            {String("v"), int(std::lround(-board->panOffset().y()))},
        };
    }

    if (method == String("timeline.setVisible")) {
        m_timelineVisible = params.contains(String("visible"))
                                ? params.value(String("visible")).toBool()
                                : params.value(String("on")).toBool(!m_timelineVisible);
        saveTimelineSettings();
        markDirty(String("windows"));
        markDirty(String("timeline"));
        markDirty(String("menus"));
        return JsonObject{{String("visible"), m_timelineVisible}};
    }

    if (method == String("timeline.setCollapsed")) {
        m_timelineCollapsed = params.contains(String("collapsed"))
                                  ? params.value(String("collapsed")).toBool()
                                  : params.value(String("on")).toBool(!m_timelineCollapsed);
        saveTimelineSettings();
        markDirty(String("windows"));
        markDirty(String("timeline"));
        return JsonObject{{String("collapsed"), m_timelineCollapsed}};
    }

    if (method == String("timeline.setFloating")) {
        m_timelineFloating = params.contains(String("floating"))
                                 ? params.value(String("floating")).toBool()
                                 : params.value(String("on")).toBool(!m_timelineFloating);
        saveTimelineSettings();
        markDirty(String("windows"));
        markDirty(String("timeline"));
        return JsonObject{{String("floating"), m_timelineFloating}};
    }

    if (method == String("timeline.setDockArea") || method == String("timeline.setLayout")) {
        if (params.contains(String("dockArea")) || params.contains(String("area"))) {
            const String area = params.value(String("dockArea"))
                                     .toString(params.value(String("area")).toString())
                                     .trimmed()
                                     .toLower();
            if (area == String("left") || area == String("right") || area == String("bottom")) {
                m_timelineDockArea = area;
            }
        }
        if (params.contains(String("visible"))) {
            m_timelineVisible = params.value(String("visible")).toBool();
        }
        if (params.contains(String("collapsed"))) {
            m_timelineCollapsed = params.value(String("collapsed")).toBool();
        }
        if (params.contains(String("floating"))) {
            m_timelineFloating = params.value(String("floating")).toBool();
        }
        saveTimelineSettings();
        markDirty(String("windows"));
        markDirty(String("timeline"));
        markDirty(String("menus"));
        return JsonObject{
            {String("visible"), m_timelineVisible},
            {String("collapsed"), m_timelineCollapsed},
            {String("floating"), m_timelineFloating},
            {String("dockArea"), m_timelineDockArea},
        };
    }

    if (method == String("texture.setHome")) {
        const String wanted = params.value(String("home"))
                                  .toString(params.value(String("where"))
                                                .toString(params.value(String("target")).toString()))
                                  .trimmed()
                                  .toLower();
        if (wanted != String("central")
            && wanted != String("control")
            && wanted != String("parked")) {
            return makeError(String("Invalid texture home: %1").arg(wanted));
        }
        if (wanted != m_textureHome) {
            saveTextureViewSlot(m_textureHome);
            m_textureHome = wanted;
            restoreTextureViewSlot();
        }
        if (params.contains(String("subControlVisible"))) {
            const bool vis = params.value(String("subControlVisible")).toBool();
            if (m_textureSubControlVisible != vis) {
                m_textureSubControlVisible = vis;
                markDirty(String("windows"));
            }
        } else if (wanted == String("control") && !m_textureSubControlVisible) {
            m_textureSubControlVisible = true;
            markDirty(String("windows"));
        } else if (wanted == String("parked") && m_textureSubControlVisible) {
            m_textureSubControlVisible = false;
            markDirty(String("windows"));
        }
        return JsonObject{
            {String("home"), m_textureHome},
            {String("zoom"), m_childBoard ? m_childBoard->zoom() : 1.0},
            {String("panX"), m_childBoard ? m_childBoard->panOffset().x() : 0.0},
            {String("panY"), m_childBoard ? m_childBoard->panOffset().y() : 0.0},
            {String("autoFitSuspended"),
             m_textureHome == String("control") ? m_textureViewControl.autoFitSuspended
                                                : m_textureViewCentral.autoFitSuspended},
            {String("viewButtons"), buildTextureViewButtonsState()},
        };
    }

    if (method == String("texture.viewButton") || method == String("view.button")) {
        const String targetView = params.value(String("view"))
                                      .toString(params.value(String("host")).toString(String("child")));
        const String name = params.value(String("name"))
                                .toString(params.value(String("id")).toString());
        if (name.isEmpty()) {
            return makeError(String("name is required"));
        }
        const bool on = params.value(String("on"))
                            .toBool(params.value(String("checked")).toBool(false));
        PaintOpenGLWidget *owner = boardForView(targetView);
        if (!owner) {
            return makeError(String("Unknown view"));
        }
        owner->sendPythonViewButtonMessage(name, on);
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        markDirty(String("toolOptions"));
        markDirty(String("timeline"));
        markDirty(String("layers/main"));
        markDirty(String("layers/child"));
        markDirty(String("assets/main"));
        markDirty(String("assets/child"));
        return JsonObject{
            {String("name"), name},
            {String("on"), on},
            {String("viewButtons"), buildTextureViewButtonsState()},
        };
    }

    if (method == String("timeline.thumbnails")) {
        const String targetView = params.contains(String("view"))
                                      ? resolveViewName(params.value(String("view")).toString())
                                      : m_business->framePanelTargetViewName();
        const int width = std::max(1, params.value(String("width")).toInt(83));
        const int height = std::max(1, params.value(String("height")).toInt(62));
        const int totalFrames = m_business->frameCount(targetView);
        Vector<int> requestedFrames;
        if (params.contains(String("frames")) && params.value(String("frames")).isArray()) {
            for (const JsonValue &v : params.value(String("frames")).toArray()) {
                const int idx = v.toInt(-1);
                if (idx >= 0 && idx < totalFrames && !requestedFrames.contains(idx)) {
                    requestedFrames.append(idx);
                }
            }
        } else {
            for (int i = 0; i < totalFrames; ++i) {
                requestedFrames.append(i);
            }
        }

        JsonObject thumbnailsObj;
        JsonArray framesArr;
        for (int frameIndex : requestedFrames) {
            const String key = String("thumb_%1_%2_%3x%4_%5")
                                   .arg(targetView)
                                   .arg(frameIndex)
                                   .arg(width)
                                   .arg(height)
                                   .arg(static_cast<unsigned long long>(m_thumbnailRevision));
            if (!m_cachedImages.contains(key)) {
                const Image rendered = m_business->renderFrameThumbnail(frameIndex, Size(width, height), targetView);
                if (!rendered.isNull()) {
                    const Image rgba = (rendered.format() == Image::Format_RGBA8888)
                                           ? rendered
                                           : rendered.convertToFormat(Image::Format_RGBA8888);
                    m_cachedImages.insert(key, rgba);
                    // A cached key is already at the client: it went out here or at connect.
                    sendImagePacket(key, rgba);
                }
            }
            if (m_cachedImages.contains(key)) {
                thumbnailsObj.insert(String::number(frameIndex), key);
                framesArr.append(JsonObject{{String("frame"), frameIndex}, {String("key"), key}});
            }
        }
        return JsonObject{
            {String("view"), targetView},
            {String("width"), width},
            {String("height"), height},
            {String("revision"), static_cast<std::int64_t>(m_thumbnailRevision)},
            {String("thumbnails"), thumbnailsObj},
            {String("frames"), framesArr},
        };
    }

    return makeError(String("Unknown method: %1").arg(method));
}

JsonObject BackendSession::handleFileImportCall(const JsonObject &params)
{
    auto makeError = [](const String &err) {
        return JsonObject{
            {String("__error__"), true},
            {String("error"), err},
        };
    };

    const String rawKind = params.value(String("kind"))
                                .toString(params.value(String("type")).toString())
                                .trimmed();
    String normKind = rawKind.toLower();

    bool isTextureImport = false;
    if (normKind.startsWith(String("file.texture.import."))) {
        normKind = normKind.mid(String("file.texture.import.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(String("texture.import."))) {
        normKind = normKind.mid(String("texture.import.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(String("file.texture."))) {
        normKind = normKind.mid(String("file.texture.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(String("texture."))) {
        normKind = normKind.mid(String("texture.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(String("file.import."))) {
        normKind = normKind.mid(String("file.import.").size());
    } else if (normKind.startsWith(String("import."))) {
        normKind = normKind.mid(String("import.").size());
    }

    const String requestedView = params.value(String("view"))
                                      .toString(params.value(String("viewName")).toString());
    // The main menu's Import is for the main board whatever is active; the texture
    // board has its own File menu.
    const String targetView = (isTextureImport || requestedView == String("child"))
                                   ? String("child")
                                   : (requestedView.isEmpty() ? String("main") : resolveViewName(requestedView));
    const bool probe = params.value(String("probe")).toBool(false);
    if (!probe) {
        if (targetView == String("child")) {
            surfaceTextureView();
        } else {
            surfaceMainView();
        }
    }
    PaintOpenGLWidget *board = boardForView(targetView);
    if (!board) {
        return makeError(String("Board not found"));
    }

    const StringList paths = extractPathsParam(params);
    if (paths.isEmpty()) {
        return makeError(String("paths is required"));
    }

    const bool fitToCanvas = params.value(String("fitToCanvas"))
                                 .toBool(params.value(String("scaleToCanvas")).toBool(true));

    JsonArray importedLayers;

    if (normKind == String("raster") || normKind == String("image")) {
        for (const String &fileName : paths) {
            Image image;
            image.load(fileName);
#ifdef ANIMEAN_WITH_PYTHON
            if (image.isNull() && Py_IsInitialized()) {
                try {
                    py::gil_scoped_acquire acquire;
                    py::module_ pilImage = py::module_::import("PIL.Image");
                    py::module_ pilImageOps = py::module_::import("PIL.ImageOps");
                    py::object opened = pilImage.attr("open")(Dir::toNativeSeparators(fileName).toStdString());
                    py::object transposed = pilImageOps.attr("exif_transpose")(opened);
                    py::object rgbaObj = transposed.attr("convert")("RGBA");
                    const int w = rgbaObj.attr("width").cast<int>();
                    const int h = rgbaObj.attr("height").cast<int>();
                    const std::string rawBytes = rgbaObj.attr("tobytes")().cast<std::string>();
                    opened.attr("close")();
                    if (w > 0 && h > 0
                        && rawBytes.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
                        image = Image(reinterpret_cast<const unsigned char *>(rawBytes.data()),
                                      w,
                                      h,
                                      w * 4,
                                      Image::Format_RGBA8888);
                    }
                } catch (...) {
                }
            }
#endif

            if (image.isNull()) {
                return makeError(String("Failed to load image:\n%1").arg(fileName));
            }
            image = image.convertToFormat(Image::Format_ARGB32_Premultiplied);
            const FileInfo fileInfo(fileName);
            const int layerIndex = board->importRasterLayer(image, fileInfo.completeBaseName());
            if (layerIndex < 0) {
                return makeError(String("Failed to import raster layer."));
            }
            importedLayers.append(layerIndex);
            m_business->updateAttention(targetView,
                                        AttentionChange::LayerChange,
                                        board->model().currentFrame(),
                                        layerIndex,
                                        board->model().currentAsset());
            m_business->setStatusMessage(String("Imported raster into %1: %2 (%3 x %4)")
                                             .arg(board->viewName())
                                             .arg(fileInfo.fileName())
                                             .arg(image.width())
                                             .arg(image.height()));
        }
    } else if (normKind == String("opentoonz")
               || normKind == String("opentoonzlines")
               || normKind == String("opentoonz_lines")
               || normKind == String("pli")
               || normKind == String("toonz")) {
#ifndef ANIMEAN_WITH_PYTHON
        return makeError(String("Python support is disabled, so OpenToonz line import is unavailable."));
#else
        for (const String &fileName : paths) {
            try {
                py::gil_scoped_acquire acquire;
                py::module_ toonzTool = py::module_::import("toonz_to_dict");
                py::object level = toonzTool.attr("read_vector_level")(Dir::toNativeSeparators(fileName).toStdString());
                py::dict levelData = level.attr("to_dict")().cast<py::dict>();
                Vector<PaintOpenGLWidget::ImportedVectorFrame> frames = importedFramesFromOpenToonzLevel(levelData);
                if (frames.isEmpty()) {
                    return makeError(String("No strokes were found in this .pli file."));
                }
                const RectF sourceBounds = importedVectorFramesBounds(frames);
                const SizeF canvasSize = board->documentRect().size();
                const RectF canvasBounds(PointF(0.0, 0.0), canvasSize);
                const bool needsScale = sourceBounds.isValid()
                                        && sourceBounds.width() > 0.0
                                        && sourceBounds.height() > 0.0
                                        && (!canvasBounds.contains(sourceBounds)
                                            || !fuzzyCompare(sourceBounds.width(), canvasSize.width())
                                            || !fuzzyCompare(sourceBounds.height(), canvasSize.height()));
                if (probe) {
                    return JsonObject{
                        {String("probe"), true},
                        {String("kind"), normKind},
                        {String("view"), targetView},
                        {String("needsScale"), needsScale},
                        {String("sourceBounds"), JsonObject{
                            {String("x"), int(std::lround(sourceBounds.x()))},
                            {String("y"), int(std::lround(sourceBounds.y()))},
                            {String("width"), int(std::lround(sourceBounds.width()))},
                            {String("height"), int(std::lround(sourceBounds.height()))},
                        }},
                        {String("canvasWidth"), int(std::lround(canvasSize.width()))},
                        {String("canvasHeight"), int(std::lround(canvasSize.height()))},
                    };
                }
                if (needsScale && fitToCanvas) {
                    scaleImportedVectorFramesToCanvas(&frames, canvasSize);
                }
                const FileInfo fileInfo(fileName);
                const int layerIndex = board->importVectorLineLayer(frames, fileInfo.completeBaseName());
                if (layerIndex < 0) {
                    return makeError(String("Failed to import OpenToonz line layer."));
                }
                importedLayers.append(layerIndex);
                m_business->updateAttention(targetView,
                                            AttentionChange::LayerChange,
                                            board->model().currentFrame(),
                                            layerIndex,
                                            board->model().currentAsset());
                m_business->setStatusMessage(String("Imported OpenToonz lines into %1: %2 (%3 frame(s))")
                                                 .arg(board->viewName())
                                                 .arg(fileInfo.fileName())
                                                 .arg(frames.size()));
            } catch (const py::error_already_set &error) {
                return makeError(String("OpenToonz import failed:\n%1").arg(String::fromUtf8(error.what())));
            }
        }
#endif
    } else if (normKind == String("csp")
               || normKind == String("clip")
               || normKind == String("clipstudio")
               || normKind == String("clipstudiopaint")
               || normKind == String("clip_studio_paint")) {
        for (const String &fileName : paths) {
            const ClipReader::ClipDocument document = ClipReader::readClipFile(fileName);
            if (!document.valid) {
                return makeError(document.error.isEmpty()
                                     ? String("Unable to read this .clip file.")
                                     : String(document.error));
            }
            Vector<PaintOpenGLWidget::ImportedVectorFrame> frames;
            PaintOpenGLWidget::ImportedVectorFrame frame;
            frame.row = 0;
            for (const ClipReader::ClipStroke &clipStroke : document.strokes) {
                if (clipStroke.points.size() < 2) {
                    continue;
                }
                PaintOpenGLWidget::ImportedVectorStroke stroke;
                stroke.points = clipStroke.points;
                stroke.color = clipStroke.color;
                stroke.width = std::max(double(1.0), clipStroke.width);
                stroke.path.moveTo(clipStroke.points.first());
                for (int i = 1; i < clipStroke.points.size(); ++i) {
                    stroke.path.lineTo(clipStroke.points[i]);
                }
                frame.strokes.append(stroke);
            }
            if (frame.strokes.isEmpty()) {
                return makeError(String("No usable vector strokes were found in this .clip file."));
            }
            frames.append(frame);
            const RectF sourceBounds = importedVectorFramesBounds(frames);
            const SizeF canvasSize = board->documentRect().size();
            const RectF canvasBounds(PointF(0.0, 0.0), canvasSize);
            const bool needsScale = sourceBounds.isValid()
                                    && sourceBounds.width() > 0.0
                                    && sourceBounds.height() > 0.0
                                    && (!canvasBounds.contains(sourceBounds)
                                        || !fuzzyCompare(sourceBounds.width(), canvasSize.width())
                                        || !fuzzyCompare(sourceBounds.height(), canvasSize.height()));
            if (probe) {
                return JsonObject{
                    {String("probe"), true},
                    {String("kind"), normKind},
                    {String("view"), targetView},
                    {String("needsScale"), needsScale},
                    {String("sourceBounds"), JsonObject{
                        {String("x"), int(std::lround(sourceBounds.x()))},
                        {String("y"), int(std::lround(sourceBounds.y()))},
                        {String("width"), int(std::lround(sourceBounds.width()))},
                        {String("height"), int(std::lround(sourceBounds.height()))},
                    }},
                    {String("canvasWidth"), int(std::lround(canvasSize.width()))},
                    {String("canvasHeight"), int(std::lround(canvasSize.height()))},
                };
            }
            if (needsScale && fitToCanvas) {
                scaleImportedVectorFramesToCanvas(&frames, canvasSize);
            }
            const FileInfo fileInfo(fileName);
            const int layerIndex = board->importVectorLineLayer(frames, fileInfo.completeBaseName());
            if (layerIndex < 0) {
                return makeError(String("Failed to import the Clip Studio Paint vector layer."));
            }
            int strokeCount = 0;
            for (const PaintOpenGLWidget::ImportedVectorFrame &imported : frames) {
                strokeCount += imported.strokes.size();
            }
            importedLayers.append(layerIndex);
            m_business->updateAttention(targetView,
                                        AttentionChange::LayerChange,
                                        board->model().currentFrame(),
                                        layerIndex,
                                        board->model().currentAsset());
            m_business->setStatusMessage(String("Imported Clip Studio Paint into %1: %2 (%3 stroke(s))")
                                             .arg(board->viewName())
                                             .arg(fileInfo.fileName())
                                             .arg(strokeCount));
        }
    } else {
        return makeError(String("Unknown import kind: %1").arg(rawKind));
    }

    markDirty(targetView == String("child") ? String("layers/child")
                                                    : String("layers/main"));
    markDirty(targetView == String("child") ? String("assets/child")
                                                    : String("assets/main"));
    markDirty(String("timeline"));
    markDirty(String("history"));
    markDirty(String("menus"));
    markDirty(String("window"));

    return JsonObject{
        {String("kind"), normKind},
        {String("view"), targetView},
        {String("layers"), importedLayers},
        {String("layer"), importedLayers.isEmpty() ? -1 : importedLayers.last().toInt(-1)},
        {String("count"), importedLayers.size()},
    };
}

JsonObject BackendSession::handleFileTextureCall(const JsonObject &params)
{
    auto makeError = [](const String &err) {
        return JsonObject{
            {String("__error__"), true},
            {String("error"), err},
        };
    };

    const String rawKind = params.value(String("kind"))
                                .toString(params.value(String("action")).toString())
                                .trimmed();
    String normKind = rawKind.toLower();
    if (normKind.startsWith(String("file.texture."))) {
        normKind = normKind.mid(String("file.texture.").size());
    } else if (normKind.startsWith(String("texture."))) {
        normKind = normKind.mid(String("texture.").size());
    }

    if (normKind.startsWith(String("import."))
        || normKind == String("raster")
        || normKind == String("image")
        || normKind == String("opentoonz")
        || normKind == String("pli")
        || normKind == String("csp")
        || normKind == String("clip")) {
        JsonObject forwarded = params;
        forwarded.insert(String("view"), String("child"));
        forwarded.insert(String("kind"), normKind);
        return handleFileImportCall(forwarded);
    }

    const StringList paths = extractPathsParam(params);
    String fileName = paths.isEmpty() ? String() : paths.first();

    if (normKind == String("open") || normKind == String("opentextureview")) {
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        String error;
        if (![&]{ AnimeCore::String e; const bool r = m_business->openTextureViewFrom(fileName, &e); error = e; return r; }()) {
            return makeError(error.isEmpty() ? String("Failed to open texture view") : error);
        }
        surfaceTextureView();
        return JsonObject{
            {String("kind"), String("open")},
            {String("path"), m_business->childFilePath()},
            {String("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == String("save") || normKind == String("savetextureview")) {
        if (fileName.isEmpty()) {
            const String current = m_business->childFilePath();
            if (!current.isEmpty()
                && FileInfo(current).suffix().compare(String("textureview"), CaseInsensitive) == 0) {
                fileName = current;
            }
        }
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        const String targetName = AnimeanBusiness::ensureTextureViewFileExtension(fileName);
        const bool divergentExists = (targetName != fileName && FileInfo::exists(targetName));
        if (params.value(String("probe")).toBool(false)) {
            return JsonObject{
                {String("probe"), true},
                {String("kind"), String("save")},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
                {String("needsConfirmOverwrite"), divergentExists},
            };
        }
        if (divergentExists && params.contains(String("overwrite")) && !params.value(String("overwrite")).toBool(true)) {
            return JsonObject{
                {String("cancelled"), true},
                {String("needsConfirmOverwrite"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
            };
        }
        String error;
        if (![&]{ AnimeCore::String e; const bool r = m_business->saveTextureViewTo(fileName, &e); error = e; return r; }()) {
            return makeError(error.isEmpty() ? String("Failed to save texture view") : error);
        }
        return JsonObject{
            {String("kind"), String("save")},
            {String("path"), m_business->childFilePath()},
            {String("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == String("saveas")
        || normKind == String("save_as")
        || normKind == String("savetextureviewas")) {
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        const String targetName = AnimeanBusiness::ensureTextureViewFileExtension(fileName);
        const bool divergentExists = (targetName != fileName && FileInfo::exists(targetName));
        if (params.value(String("probe")).toBool(false)) {
            return JsonObject{
                {String("probe"), true},
                {String("kind"), String("saveAs")},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
                {String("needsConfirmOverwrite"), divergentExists},
            };
        }
        if (divergentExists && params.contains(String("overwrite")) && !params.value(String("overwrite")).toBool(true)) {
            return JsonObject{
                {String("cancelled"), true},
                {String("needsConfirmOverwrite"), true},
                {String("path"), targetName},
                {String("fileName"), FileInfo(targetName).fileName()},
            };
        }
        String error;
        if (![&]{ AnimeCore::String e; const bool r = m_business->saveTextureViewTo(fileName, &e); error = e; return r; }()) {
            return makeError(error.isEmpty() ? String("Failed to save texture view") : error);
        }
        return JsonObject{
            {String("kind"), String("saveAs")},
            {String("path"), m_business->childFilePath()},
            {String("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == String("exportimage")
        || normKind == String("export_image")
        || normKind == String("export")
        || normKind == String("exporttextureimage")
        || normKind == String("image")
        || normKind == String("png")) {
        if (fileName.isEmpty()) {
            return makeError(String("path is required"));
        }
        if (!m_childBoard) {
            return makeError(String("Texture board not found"));
        }
        if (FileInfo(fileName).suffix().isEmpty()) {
            fileName += String(".png");
        }
        Size exportSize = m_childBoard->size();
        if (exportSize.width() <= 0 || exportSize.height() <= 0) {
            exportSize = m_childBoard->model().canvasSize();
        }
        const Image image = m_childBoard->renderFrameThumbnail(m_childBoard->model().currentFrame(), exportSize);
        if (image.isNull()) {
            return makeError(String("The texture view could not be captured."));
        }
        if (!image.save(fileName, "PNG")) {
            return makeError(String("Failed to write the image to:\n%1").arg(fileName));
        }
        m_business->setStatusMessage(String("Exported texture image: %1 (%2 x %3)")
                                         .arg(FileInfo(fileName).fileName())
                                         .arg(image.width())
                                         .arg(image.height()));
        return JsonObject{
            {String("kind"), String("exportImage")},
            {String("path"), fileName},
            {String("fileName"), fileName},
            {String("width"), image.width()},
            {String("height"), image.height()},
        };
    }

    return makeError(String("Unknown texture kind: %1").arg(rawKind));
}

bool BackendSession::triggerMenuById(const String &id, const JsonObject &params, String *errorOut)
{
    // Direct (menuName, name) script menu call if provided in params
    if (params.contains(String("menuName")) || params.contains(String("menu"))) {
        const String menuName = params.value(String("menuName"))
                                     .toString(params.value(String("menu")).toString());
        const String itemName = params.value(String("name"))
                                     .toString(params.value(String("itemName")).toString(id));
        const String host = params.value(String("host"))
                                 .toString(params.value(String("view")).toString(String("main")));
        const bool checked = params.value(String("checked")).toBool(false);
        PaintOpenGLWidget *owner = boardForView(host);
        if (owner) {
            owner->sendPythonMenuMessage(menuName, itemName, checked);
            markDirty(String("menus"));
            if (host == String("child")) {
                markDirty(String("textureMenus"));
            }
            markDirty(String("toolOptions"));
            markDirty(String("timeline"));
            markDirty(String("layers/main"));
            markDirty(String("layers/child"));
            markDirty(String("assets/main"));
            markDirty(String("assets/child"));
            return true;
        }
    }

    if (id == String("file.new")) {
        emitUiEvent(String("dialog.canvasSize"),
                    JsonObject{
                        {String("width"), m_business->canvasSize().width()},
                        {String("height"), m_business->canvasSize().height()},
                    });
        return true;
    }
    if (id == String("file.open")) {
        emitUiEvent(String("dialog.openProject"));
        return true;
    }
    if (id == String("file.save")) {
        if (m_business->currentFilePath().isEmpty()
            || FileInfo(m_business->currentFilePath()).suffix().compare(String("anproj"), CaseInsensitive) != 0) {
            emitUiEvent(String("dialog.saveProjectAs"));
            return true;
        }
        return [&]{ AnimeCore::String e; const bool r = m_business->saveProjectTo(m_business->currentFilePath(), errorOut ? &e : nullptr); if (errorOut) *errorOut = e; return r; }();
    }
    if (id == String("file.saveAs")) {
        emitUiEvent(String("dialog.saveProjectAs"));
        return true;
    }
    if (id == String("file.texture.save")) {
        if (!m_business->childFilePath().isEmpty()
            && FileInfo(m_business->childFilePath()).suffix().compare(String("textureview"), CaseInsensitive) == 0) {
            return [&]{ AnimeCore::String e; const bool r = m_business->saveTextureViewTo(m_business->childFilePath(), errorOut ? &e : nullptr); if (errorOut) *errorOut = e; return r; }();
        }
        emitUiEvent(String("menu.action"), JsonObject{{String("id"), id}});
        return true;
    }
    if (id.startsWith(String("file.import."))
        || id.startsWith(String("file.texture."))) {
        emitUiEvent(String("menu.action"), JsonObject{{String("id"), id}});
        return true;
    }
    if (id == String("edit.undo")) {
        m_business->undo();
        return true;
    }
    if (id == String("edit.redo")) {
        m_business->redo();
        return true;
    }
    if (id == String("theme.dark")) {
        AnimeTheme::setMode(AnimeTheme::Mode::Dark);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(String("theme"));
        markDirty(String("menus"));
        return true;
    }
    if (id == String("theme.light")) {
        AnimeTheme::setMode(AnimeTheme::Mode::Light);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(String("theme"));
        markDirty(String("menus"));
        return true;
    }
    if (id == String("windows.texture_view")) {
        m_textureSubControlVisible = true;
        emitUiEvent(String("textureSubControl.surface"));
        markDirty(String("windows"));
        return true;
    }
    if (id == String("windows.timeline")) {
        m_timelineVisible = params.contains(String("checked"))
                                ? params.value(String("checked")).toBool()
                                : !m_timelineVisible;
        saveTimelineSettings();
        markDirty(String("windows"));
        markDirty(String("timeline"));
        markDirty(String("menus"));
        return true;
    }
    if (id.startsWith(String("windows."))) {
        const String winName = id.mid(String("windows.").size());
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == winName) {
                info.visible = params.contains(String("checked"))
                                   ? params.value(String("checked")).toBool()
                                   : !info.visible;
                markDirty(String("windows"));
                markDirty(String("menus"));
                return true;
            }
        }
    }
    if (id == String("child.setting.changeableTimeline")) {
        const bool next = params.contains(String("checked"))
                              ? params.value(String("checked")).toBool()
                              : !m_business->changeableTimeline();
        m_business->setChangeableTimeline(next);
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        return true;
    }
    if (id == String("child.setting.changeableTexture")) {
        const bool next = params.contains(String("checked"))
                              ? params.value(String("checked")).toBool()
                              : !m_business->changeableTexture();
        m_business->setChangeableTexture(next);
        markDirty(String("window"));
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        return true;
    }
    if (id == String("child.setting.background.white") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::White);
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        return true;
    }
    if (id == String("child.setting.background.black") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::Black);
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        return true;
    }
    if (id == String("child.setting.background.transparent") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::Transparent);
        markDirty(String("menus"));
        markDirty(String("textureMenus"));
        return true;
    }
    if (id.startsWith(String("settings:"))) {
        const String target = id.mid(String("settings:").size());
        openScriptSettings(target, params.value(String("title")).toString(target));
        return true;
    }
    if (id.startsWith(String("script:"))) {
        const StringList parts = id.split(Latin1Char(':'));
        if (parts.size() >= 4) {
            const String host = parts.at(1);
            const String menuName = parts.at(2);
            const String itemName = parts.mid(3).join(Latin1Char(':'));
            const bool checked = params.value(String("checked")).toBool(false);
            PaintOpenGLWidget *owner = boardForView(host);
            if (owner) {
                owner->sendPythonMenuMessage(menuName, itemName, checked);
                markDirty(String("menus"));
                if (host == String("child")) {
                    markDirty(String("textureMenus"));
                }
                markDirty(String("toolOptions"));
                markDirty(String("timeline"));
                markDirty(String("layers/main"));
                markDirty(String("layers/child"));
                markDirty(String("assets/main"));
                markDirty(String("assets/child"));
                return true;
            }
        }
    }

    if (errorOut) {
        *errorOut = String("Unknown menu id: %1").arg(id);
    }
    return false;
}

void BackendSession::openScriptSettings(const String &name, const String &title)
{
    JsonObject layout;
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("settings_layout_json")(name.toStdString())
                                         .cast<std::string>();
            const JsonDocument doc = JsonDocument::fromJson(json);
            layout = doc.object();
        } catch (const py::error_already_set &error) {
            m_business->setStatusMessage(String("settings error: %1").arg(String::fromUtf8(error.what())));
            return;
        }
    }
#endif
    if (layout.isEmpty()) {
        m_business->setStatusMessage(String("No settings registered for '%1'.").arg(name));
        return;
    }
    emitUiEvent(String("settings.open"),
                JsonObject{
                    {String("name"), name},
                    {String("title"), title.isEmpty() ? name : title},
                    {String("layout"), layout},
                });
}

void BackendSession::emitUiEvent(const String &name, const JsonObject &args)
{
    m_lastEvent = JsonObject{
        {String("name"), name},
        {String("args"), args},
    };
    const JsonObject packet{
        {String("t"), String("state")},
        {String("key"), String("event")},
        {String("value"), m_lastEvent},
    };
    sendJson(packet);
}

void BackendSession::handleFrameSink(const String &viewName, const BoardFrame &frame)
{
    m_lastBoardFrameId.insert(viewName, frame.frame);
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    std::string payload;
    payload.reserve(frame.ops.size() + 128);
    payload.append("{\"t\":\"board\",\"view\":\"");
    payload.append(viewName.toUtf8());
    payload.append("\",\"frame\":");
    payload.append(std::to_string(static_cast<unsigned long long>(frame.frame)));
    payload.append(",\"w\":");
    payload.append(std::to_string(frame.w));
    payload.append(",\"h\":");
    payload.append(std::to_string(frame.h));
    payload.append(",\"keep\":");
    payload.append(std::to_string(frame.keep));
    payload.append(",\"ops\":");
    payload.append(frame.ops.empty() ? std::string("[]") : frame.ops);
    payload.append("}");
    m_ws->sendText(payload);
}

void BackendSession::handleImageSink(const String &view, const String &key, const AnimeCore::Image &rgbaImg)
{
    const Image rgba(rgbaImg); if (key.isEmpty() || rgba.isNull()) {
        return;
    }
    const Image formatted = (rgba.format() == Image::Format_RGBA8888)
                                 ? rgba
                                 : rgba.convertToFormat(Image::Format_RGBA8888);
    sendImagePacket(key, formatted, view);
}

void BackendSession::sendImagePacket(const String &key, const Image &rgba, const String &view)
{
    if (!m_ws || !m_ws->hasClient() || rgba.isNull()) {
        return;
    }
    const int w = rgba.width();
    const int h = rgba.height();
    const int expectedBytes = w * h * 4;
    if (expectedBytes <= 0) {
        return;
    }
    JsonObject header{
        {String("t"), String("image")},
        {String("key"), key},
        {String("w"), w},
        {String("h"), h},
        {String("bytes"), expectedBytes},
    };
    if (!view.isEmpty()) {
        header.insert(String("view"), view);
    }
    sendJson(header);
    if (rgba.bytesPerLine() == w * 4) {
        m_ws->sendBinary(std::string(reinterpret_cast<const char *>(rgba.constBits()), static_cast<std::size_t>(expectedBytes)));
    } else {
        std::string tight;
        tight.resize(expectedBytes);
        char *dst = tight.data();
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst + y * w * 4, rgba.constScanLine(y), static_cast<size_t>(w * 4));
        }
        m_ws->sendBinary(tight);
    }
}

void BackendSession::handleCursorSink(const String &viewName, const AnimeCore::Cursor &cursor)
{
    CursorState state;
    const AnimeCore::Image pixmap = cursor.pixmap();
    if (!pixmap.isNull()) {
        const Image rgba = Image(pixmap).convertToFormat(Image::Format_RGBA8888);
        const String key = cursorImageKey(rgba);
        state.shape = String("bitmap");
        state.css = String("default");
        state.key = key;
        state.hx = cursor.hotSpot().x();
        state.hy = cursor.hotSpot().y();
        state.w = rgba.width();
        state.h = rgba.height();
        // The bitmap has the screen's pixels and the hotspot is in logical ones.
        state.scale = pixmap.devicePixelRatio();
        if (!m_cachedImages.contains(key)) {
            m_cachedImages.insert(key, rgba);
            sendImagePacket(key, rgba);
        }
    } else {
        mapCursorShape(cursor.shape(), &state.shape, &state.css);
    }
    m_cursors.insert(viewName, state);
    markDirty(viewName == String("child") ? String("cursor/child")
                                                  : String("cursor/main"));
}

void BackendSession::setPythonUiFrozen(bool frozen)
{
    const bool wasFrozen = (m_pythonFreezeDepth > 0);
    if (frozen) {
        ++m_pythonFreezeDepth;
    } else {
        m_pythonFreezeDepth = std::max(0, m_pythonFreezeDepth - 1);
    }
    const bool isFrozen = (m_pythonFreezeDepth > 0);
    if (wasFrozen != isFrozen) {
        // Sent now: the script runs inside one call, so a dirty flush would only
        // happen after it has finished and unfrozen again.
        sendStateKey(String("freeze"));
    }
}

void BackendSession::appendPythonDebug(const String &message)
{
    const String trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    m_pythonDebugLog.append(trimmed);
    while (m_pythonDebugLog.size() > 2000) {
        m_pythonDebugLog.removeFirst();
    }
    markDirty(String("window"));
}

void BackendSession::markDirty(const String &key)
{
    m_dirtyKeys.insert(key);
    scheduleStateFlush();
}

void BackendSession::markAllDirty()
{
    for (const String &key : allStateKeys()) {
        m_dirtyKeys.insert(key);
    }
    scheduleStateFlush();
}

void BackendSession::scheduleStateFlush()
{
    if (m_stateFlushScheduled) {
        return;
    }
    m_stateFlushScheduled = true;
    Timer::singleShot(0, this, &BackendSession::flushDirtyState);
}

void BackendSession::flushDirtyState()
{
    m_stateFlushScheduled = false;
    if (m_dirtyKeys.isEmpty()) {
        return;
    }
    const Set<String> toSend = std::move(m_dirtyKeys);
    m_dirtyKeys.clear();

    if (!m_ws || !m_ws->hasClient()) {
        return;
    }

    for (const String &key : allStateKeys()) {
        if (toSend.contains(key)) {
            sendStateKey(key);
        }
    }
}

void BackendSession::sendStateKey(const String &key)
{
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    const JsonObject packet{
        {String("t"), String("state")},
        {String("key"), key},
        {String("value"), buildStateValue(key)},
    };
    sendJson(packet);
}

void BackendSession::sendJson(const JsonObject &obj)
{
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    m_ws->sendText(JsonDocument(obj).toJson(JsonDocument::Compact));
}

JsonValue BackendSession::buildStateValue(const String &key) const
{
    if (key == String("window")) {
        return buildWindowState();
    }
    if (key == String("theme")) {
        return buildThemeState();
    }
    if (key == String("menus")) {
        return buildMenusState();
    }
    if (key == String("textureMenus")) {
        return buildTextureMenusState();
    }
    if (key == String("tools")) {
        return buildToolsState();
    }
    if (key == String("toolOptions")) {
        return buildToolOptionsState();
    }
    if (key == String("thumbnails")) {
        return JsonObject{{String("revision"), static_cast<std::int64_t>(m_thumbnailRevision)}};
    }
    if (key == String("drawColor")) {
        return buildDrawColorState();
    }
    if (key == String("drawSettings")) {
        return buildDrawSettingsState();
    }
    if (key == String("layers/main")) {
        return buildLayersState(String("main"));
    }
    if (key == String("layers/child")) {
        return buildLayersState(String("child"));
    }
    if (key == String("assets/main")) {
        return buildAssetsState(String("main"));
    }
    if (key == String("assets/child")) {
        return buildAssetsState(String("child"));
    }
    if (key == String("timeline")) {
        return buildTimelineState();
    }
    if (key == String("history")) {
        return buildHistoryState();
    }
    if (key == String("windows")) {
        return buildWindowsState();
    }
    if (key == String("cursor/main")) {
        return buildCursorState(String("main"));
    }
    if (key == String("cursor/child")) {
        return buildCursorState(String("child"));
    }
    if (key == String("viewport/main")) {
        return buildViewportState(String("main"));
    }
    if (key == String("viewport/child")) {
        return buildViewportState(String("child"));
    }
    if (key == String("freeze")) {
        return m_pythonFreezeDepth > 0;
    }
    if (key == String("pad")) {
        return buildPadState();
    }
    if (key == String("event")) {
        return m_lastEvent;
    }
    return JsonValue();
}

JsonObject BackendSession::buildWindowState() const
{
    const Size mainCanvas = m_business->canvasSize(String("main"));
    JsonArray debugArr;
    for (const String &line : m_pythonDebugLog) {
        debugArr.append(line);
    }
    return JsonObject{
        {String("title"), m_business->windowTitle()},
        {String("status"), m_business->statusMessage()},
        {String("activeView"), m_business->activeViewName()},
        {String("assetView"), m_business->assetPanelTargetViewName()},
        {String("currentFilePath"), m_business->currentFilePath()},
        {String("childFilePath"), m_business->childFilePath()},
        {String("canvasWidth"), mainCanvas.width()},
        {String("canvasHeight"), mainCanvas.height()},
        {String("pythonDebug"), debugArr},
    };
}

JsonObject BackendSession::buildThemeState() const
{
    auto hex = [](AnimeTheme::Role role) {
        const Color c = AnimeTheme::color(role);
        return c.alpha() == 255 ? c.name(Color::HexRgb) : c.name(Color::HexArgb);
    };
    JsonObject roles{
        {String("window"), hex(AnimeTheme::Role::Window)},
        {String("surface"), hex(AnimeTheme::Role::Surface)},
        {String("surfaceAlt"), hex(AnimeTheme::Role::SurfaceAlt)},
        {String("text"), hex(AnimeTheme::Role::Text)},
        {String("textDim"), hex(AnimeTheme::Role::TextDim)},
        {String("divider"), hex(AnimeTheme::Role::Divider)},
        {String("accent"), hex(AnimeTheme::Role::Accent)},
        {String("accentHover"), hex(AnimeTheme::Role::AccentHover)},
        {String("accentActive"), hex(AnimeTheme::Role::AccentActive)},
        {String("chipRest"), hex(AnimeTheme::Role::ChipRest)},
        {String("chipRestFg"), hex(AnimeTheme::Role::ChipRestFg)},
        {String("chipHover"), hex(AnimeTheme::Role::ChipHover)},
        {String("chipHoverFg"), hex(AnimeTheme::Role::ChipHoverFg)},
        {String("viewportSurround"), hex(AnimeTheme::Role::ViewportSurround)},
        {String("onionPast"), hex(AnimeTheme::Role::OnionPast)},
        {String("onionAhead"), hex(AnimeTheme::Role::OnionAhead)},
    };
    JsonObject out = roles;
    out.insert(String("mode"), AnimeTheme::modeName(AnimeTheme::mode()));
    out.insert(String("roles"), roles);
    return out;
}

JsonArray BackendSession::buildScriptMenuItems(const String &host,
                                                const String &menuName,
                                                const JsonArray &items) const
{
    JsonArray out;
    int sepIndex = 0;
    for (const JsonValue &val : items) {
        const JsonObject obj = val.toObject();
        const String kind = obj.value(String("kind")).toString(String("action"));
        if (kind == String("separator")) {
            out.append(makeMenuItem(String("%1.%2.sep%3").arg(host, menuName).arg(++sepIndex),
                                    String(),
                                    String(),
                                    String("separator")));
            continue;
        }
        const String itemName = obj.value(String("name")).toString();
        const String title = obj.value(String("title")).toString(itemName);
        const String shortcut = obj.value(String("shortcut")).toString();
        const bool enabled = obj.value(String("enabled")).toBool(true);
        const bool checked = obj.value(String("checked")).toBool(false);

        if (kind == String("submenu")) {
            const JsonArray children =
                buildScriptMenuItems(host, menuName, obj.value(String("items")).toArray());
            out.append(makeMenuItem(String("submenu:%1:%2:%3").arg(host, menuName, itemName.isEmpty() ? title : itemName),
                                    title,
                                    shortcut,
                                    String("submenu"),
                                    false,
                                    enabled,
                                    children));
            continue;
        }
        if (kind == String("settings")) {
            const String target = obj.value(String("settings")).toString(itemName);
            out.append(makeMenuItem(String("settings:%1").arg(target),
                                    title.isEmpty() ? itemName : title,
                                    shortcut,
                                    String("settings"),
                                    false,
                                    enabled));
            continue;
        }
        out.append(makeMenuItem(String("script:%1:%2:%3").arg(host, menuName, itemName),
                                title.isEmpty() ? itemName : title,
                                shortcut,
                                kind,
                                checked,
                                enabled));
    }
    return out;
}

JsonArray BackendSession::buildMenusState() const
{
    JsonArray menus;

    // 1. File menu (from mainwindow.ui + createMainTextureMenu + createTextureFileMenu)
    JsonArray importChildren{
        makeMenuItem(String("file.import.raster"), String("Raster..."), String(), String("action")),
        makeMenuItem(String("file.import.opentoonz"), String("OpenToonz Lines..."), String(), String("action")),
        makeMenuItem(String("file.import.csp"), String("Clip Studio Paint..."), String(), String("action")),
    };

    JsonArray texImportChildren{
        makeMenuItem(String("file.texture.import.raster"), String("Raster..."), String(), String("action")),
        makeMenuItem(String("file.texture.import.opentoonz"), String("OpenToonz Lines..."), String(), String("action")),
        makeMenuItem(String("file.texture.import.csp"), String("Clip Studio Paint..."), String(), String("action")),
    };

    const PaintOpenGLWidget::BackgroundMode bgMode =
        m_childBoard ? m_childBoard->backgroundMode() : PaintOpenGLWidget::BackgroundMode::White;
    JsonArray bgChildren{
        makeMenuItem(String("child.setting.background.white"),
                     String("White"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::White),
        makeMenuItem(String("child.setting.background.black"),
                     String("Black"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Black),
        makeMenuItem(String("child.setting.background.transparent"),
                     String("Transparent"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Transparent),
    };

    JsonArray childSettingChildren{
        makeMenuItem(String("child.setting.changeableTimeline"),
                     String("Changable Timeline"),
                     String(),
                     String("check"),
                     m_business->changeableTimeline()),
        makeMenuItem(String("child.setting.changeableTexture"),
                     String("Changable Texture"),
                     String(),
                     String("check"),
                     m_business->changeableTexture()),
        makeMenuItem(String("child.setting.sep1"), String(), String(), String("separator")),
        makeMenuItem(String("child.setting.background"),
                     String("Background"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     bgChildren),
    };

#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string("child"))
                                         .cast<std::string>();
            const JsonDocument doc = JsonDocument::fromJson(json);
            Hash<String, JsonObject> liveChildMenus;
            for (const JsonValue &val : doc.array()) {
                const JsonObject obj = val.toObject();
                const String mName = obj.value(String("name")).toString();
                if (!mName.isEmpty()) {
                    liveChildMenus.insert(mName, obj);
                }
            }
            for (const std::pair<String, String> &entry : m_childScriptMenus) {
                const String &mName = entry.first;
                const JsonObject obj = liveChildMenus.value(mName);
                const String mTitle = obj.value(String("title")).toString(entry.second);
                const JsonArray items =
                    buildScriptMenuItems(String("child"), mName, obj.value(String("items")).toArray());
                childSettingChildren.append(makeMenuItem(String("child.menu.%1").arg(mName),
                                                         mTitle,
                                                         String(),
                                                         String("submenu"),
                                                         false,
                                                         true,
                                                         items));
            }
        } catch (...) {
        }
    }
#endif

    JsonArray textureChildren{
        makeMenuItem(String("file.texture.import"),
                     String("Import"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     texImportChildren),
        makeMenuItem(String("file.texture.sep1"), String(), String(), String("separator")),
        makeMenuItem(String("file.texture.open"), String("Open Texture View..."), String(), String("action")),
        makeMenuItem(String("file.texture.save"), String("Save Texture View"), String(), String("action")),
        makeMenuItem(String("file.texture.saveAs"), String("Save Texture View As..."), String(), String("action")),
        makeMenuItem(String("file.texture.exportImage"), String("Export Texture View Image..."), String(), String("action")),
        makeMenuItem(String("file.texture.sep2"), String(), String(), String("separator")),
        makeMenuItem(String("file.texture.setting"),
                     String("Setting"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     childSettingChildren),
    };

    JsonArray fileChildren{
        makeMenuItem(String("file.new"), String("Canvas Size..."), String(), String("action")),
        makeMenuItem(String("file.open"), String("Open..."), String("Ctrl+O"), String("action")),
        makeMenuItem(String("file.save"), String("Save"), String("Ctrl+S"), String("action")),
        makeMenuItem(String("file.saveAs"), String("Save As..."), String("Ctrl+Shift+S"), String("action")),
        makeMenuItem(String("file.sep1"), String(), String(), String("separator")),
        makeMenuItem(String("file.import"),
                     String("Import"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     importChildren),
        makeMenuItem(String("file.texture"),
                     String("Texture"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     textureChildren),
    };
    menus.append(makeMenuItem(String("file"), String("File"), String(), String("submenu"), false, true, fileChildren));

    // 2. Edit menu
    JsonArray editChildren{
        makeMenuItem(String("edit.undo"),
                     String("Undo"),
                     String("Ctrl+Z"),
                     String("action"),
                     false,
                     m_business->canUndo()),
        makeMenuItem(String("edit.redo"),
                     String("Redo"),
                     String("Ctrl+Y"),
                     String("action"),
                     false,
                     m_business->canRedo()),
    };
    menus.append(makeMenuItem(String("edit"), String("Edit"), String(), String("submenu"), false, true, editChildren));

    // 3..5. Python script menus for "main" (captured at createScriptMenus time: View, Auto Mapping, Draw)
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string("main"))
                                         .cast<std::string>();
            const JsonDocument doc = JsonDocument::fromJson(json);
            Hash<String, JsonObject> liveMainMenus;
            for (const JsonValue &val : doc.array()) {
                const JsonObject obj = val.toObject();
                const String mName = obj.value(String("name")).toString();
                if (!mName.isEmpty()) {
                    liveMainMenus.insert(mName, obj);
                }
            }
            for (const std::pair<String, String> &entry : m_mainScriptMenus) {
                const String &mName = entry.first;
                const JsonObject obj = liveMainMenus.value(mName);
                const String mTitle = obj.value(String("title")).toString(entry.second);
                const JsonArray items =
                    buildScriptMenuItems(String("main"), mName, obj.value(String("items")).toArray());
                menus.append(makeMenuItem(mName, mTitle, String(), String("submenu"), false, true, items));
            }
        } catch (...) {
        }
    }
#endif

    // 6. Theme menu
    JsonArray themeChildren{
        makeMenuItem(String("theme.dark"),
                     String("Dark"),
                     String(),
                     String("radio"),
                     AnimeTheme::mode() == AnimeTheme::Mode::Dark),
        makeMenuItem(String("theme.light"),
                     String("Light"),
                     String(),
                     String("radio"),
                     AnimeTheme::mode() == AnimeTheme::Mode::Light),
    };
    menus.append(makeMenuItem(String("theme"), String("Theme"), String(), String("submenu"), false, true, themeChildren));

    // 7. Windows menu
    JsonArray windowsChildren{
        makeMenuItem(String("windows.texture_view"),
                     String("Texture View"),
                     String(),
                     String("action")),
        makeMenuItem(String("windows.sep1"), String(), String(), String("separator")),
    };
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        if (info.name == String("paint")) {
            continue;
        }
        windowsChildren.append(makeMenuItem(String("windows.%1").arg(info.name),
                                            info.title,
                                            String(),
                                            String("check"),
                                            info.visible));
    }
    windowsChildren.append(makeMenuItem(String("windows.timeline"),
                                        String("Timeline"),
                                        String(),
                                        String("check"),
                                        m_timelineVisible));
    menus.append(makeMenuItem(String("windows"), String("Windows"), String(), String("submenu"), false, true, windowsChildren));

    return menus;
}

JsonArray BackendSession::buildTextureMenusState() const
{
    JsonArray menus;

    JsonArray texImportChildren{
        makeMenuItem(String("file.texture.import.raster"), String("Raster..."), String(), String("action")),
        makeMenuItem(String("file.texture.import.opentoonz"), String("OpenToonz Lines..."), String(), String("action")),
        makeMenuItem(String("file.texture.import.csp"), String("Clip Studio Paint..."), String(), String("action")),
    };

    JsonArray fileChildren{
        makeMenuItem(String("file.texture.import"),
                     String("Import"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     texImportChildren),
        makeMenuItem(String("file.texture.sep1"), String(), String(), String("separator")),
        makeMenuItem(String("file.texture.open"), String("Open Texture View..."), String(), String("action")),
        makeMenuItem(String("file.texture.save"), String("Save Texture View"), String(), String("action")),
        makeMenuItem(String("file.texture.saveAs"), String("Save Texture View As..."), String(), String("action")),
        makeMenuItem(String("file.texture.exportImage"), String("Export Texture View Image..."), String(), String("action")),
    };
    menus.append(makeMenuItem(String("file.texture"), String("File"), String(), String("submenu"), false, true, fileChildren));

    const PaintOpenGLWidget::BackgroundMode bgMode =
        m_childBoard ? m_childBoard->backgroundMode() : PaintOpenGLWidget::BackgroundMode::White;
    JsonArray bgChildren{
        makeMenuItem(String("child.setting.background.white"),
                     String("White"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::White),
        makeMenuItem(String("child.setting.background.black"),
                     String("Black"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Black),
        makeMenuItem(String("child.setting.background.transparent"),
                     String("Transparent"),
                     String(),
                     String("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Transparent),
    };

    JsonArray childSettingChildren{
        makeMenuItem(String("child.setting.changeableTimeline"),
                     String("Changable Timeline"),
                     String(),
                     String("check"),
                     m_business->changeableTimeline()),
        makeMenuItem(String("child.setting.changeableTexture"),
                     String("Changable Texture"),
                     String(),
                     String("check"),
                     m_business->changeableTexture()),
        makeMenuItem(String("child.setting.sep1"), String(), String(), String("separator")),
        makeMenuItem(String("child.setting.background"),
                     String("Background"),
                     String(),
                     String("submenu"),
                     false,
                     true,
                     bgChildren),
    };

    JsonArray topLevelChildMenus;
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string("child"))
                                         .cast<std::string>();
            const JsonDocument doc = JsonDocument::fromJson(json);
            Hash<String, JsonObject> liveChildMenus;
            for (const JsonValue &val : doc.array()) {
                const JsonObject obj = val.toObject();
                const String mName = obj.value(String("name")).toString();
                if (!mName.isEmpty()) {
                    liveChildMenus.insert(mName, obj);
                }
            }
            for (const std::pair<String, String> &entry : m_childScriptMenus) {
                const String &mName = entry.first;
                const JsonObject obj = liveChildMenus.value(mName);
                const String mTitle = obj.value(String("title")).toString(entry.second);
                const JsonArray items =
                    buildScriptMenuItems(String("child"), mName, obj.value(String("items")).toArray());
                childSettingChildren.append(makeMenuItem(String("child.menu.%1").arg(mName),
                                                         mTitle,
                                                         String(),
                                                         String("submenu"),
                                                         false,
                                                         true,
                                                         items));
                topLevelChildMenus.append(makeMenuItem(mName,
                                                       mTitle,
                                                       String(),
                                                       String("submenu"),
                                                       false,
                                                       true,
                                                       items));
            }
        } catch (...) {
        }
    }
#endif

    menus.append(makeMenuItem(String("child.setting"), String("Setting"), String(), String("submenu"), false, true, childSettingChildren));
    for (const JsonValue &menuVal : topLevelChildMenus) {
        menus.append(menuVal);
    }
    return menus;
}

JsonArray BackendSession::buildTextureViewButtonsState() const
{
    JsonArray out;
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("view_buttons_json")(std::string("child"))
                                         .cast<std::string>();
            JsonParseError parseError;
            const JsonDocument doc = JsonDocument::fromJson(json, &parseError);
            if (parseError.error == JsonParseError::NoError && doc.isArray()) {
                for (const JsonValue &val : doc.array()) {
                    const JsonObject obj = val.toObject();
                    const String name = obj.value(String("name")).toString();
                    if (name.isEmpty()) {
                        continue;
                    }
                    out.append(JsonObject{
                        {String("name"), name},
                        {String("title"), obj.value(String("title")).toString(name)},
                        {String("checkable"), obj.value(String("checkable")).toBool(false)},
                        {String("checked"), obj.value(String("checked")).toBool(false)},
                        {String("tooltip"), obj.value(String("tooltip")).toString()},
                    });
                }
            }
        } catch (...) {
        }
    }
#endif
    return out;
}

JsonObject BackendSession::buildToolsState() const
{
    const AnimeanBusiness::Tool curTool = m_business->currentToolEnum();
    JsonArray extraArr;
    for (const AnimeExtraToolDefinition &et : m_business->extraTools()) {
        extraArr.append(JsonObject{
            {String("name"), et.name},
            {String("title"), et.title},
            {String("property"), et.property},
            {String("handler"), et.handler},
            {String("baseTool"), et.baseTool},
            {String("page"), et.page},
        });
    }

    auto setToSortedArray = [](const Set<String> &set) {
        StringList list = set.values();
        list.sort();
        JsonArray arr;
        for (const String &s : list) {
            arr.append(s);
        }
        return arr;
    };

    const JsonArray activeLocked = setToSortedArray(m_business->lockedToolsForView(m_business->activeViewName()));
    JsonObject lockedByView{
        {String("main"), setToSortedArray(m_business->lockedToolsForView(String("main")))},
        {String("child"), setToSortedArray(m_business->lockedToolsForView(String("child")))},
    };

    String toolsPage = String("painting");
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        if (info.name == String("tools")) {
            toolsPage = info.current;
            break;
        }
    }

    return JsonObject{
        {String("tool"), AnimeanBusiness::toolControlName(curTool)},
        {String("current"), AnimeanBusiness::toolControlName(curTool)},
        {String("toolEnum"), toolEnumDisplayName(curTool)},
        {String("toolId"), int(curTool)},
        {String("activeExtraTool"), m_business->activeExtraTool()},
        {String("activeExtraToolProperty"), m_business->activeExtraToolProperty()},
        {String("extraTools"), extraArr},
        {String("lockedTools"), activeLocked},
        {String("lockedToolsByView"), lockedByView},
        {String("page"), toolsPage},
    };
}

JsonObject BackendSession::buildToolOptionsState() const
{
    // Built fresh: a layout kept from the last tool switch carries that moment's
    // values, and the web panel rebuilds from whatever it is sent.
    if (!m_business->activeExtraTool().isEmpty()) {
        return m_business->extraToolOptionsLayout(m_business->activeExtraTool());
    }
    return m_business->toolOptionsLayout(m_business->currentToolEnum());
}

JsonObject BackendSession::buildDrawColorState() const
{
    const Color c = m_business->drawingColor();
    return JsonObject{
        {String("r"), c.red()},
        {String("g"), c.green()},
        {String("b"), c.blue()},
        {String("a"), c.alpha()},
        {String("hex"), c.name(Color::HexRgb)},
        {String("rgba"), c.name(Color::HexArgb)},
    };
}

JsonObject BackendSession::buildDrawSettingsState() const
{
    const AnimeStrokeFitSettings fit = m_business->strokeFitSettings();
    return JsonObject{
        {String("smooth"), m_business->toolSmoothValue()},
        {String("stabilizer"), m_business->toolSmoothValue()},
        {String("penWidth"), m_business->toolPenWidth()},
        {String("simplify"), fit.simplify},
        {String("corner"), fit.corner},
        {String("fillScope"), m_business->toolFillAllLayers() ? String("all") : String("current")},
        {String("fillAllLayers"), m_business->toolFillAllLayers()},
    };
}

JsonObject BackendSession::buildLayersState(const String &viewName) const
{
    const Vector<AnimeLayerPanelRow> rows = m_business->layerRowsForView(viewName);
    JsonArray rowsArr;
    for (const AnimeLayerPanelRow &row : rows) {
        rowsArr.append(JsonObject{
            {String("group"), row.group},
            {String("group_id"), row.groupId},
            {String("layer"), row.layerIndex},
            {String("name"), row.name},
            {String("visible"), row.visible},
            {String("collapsed"), row.collapsed},
            {String("depth"), row.depth},
            {String("child"), row.child},
            {String("parent"), row.parent},
        });
    }
    return JsonObject{
        {String("rows"), rowsArr},
        {String("selected"), m_business->currentLayer(viewName)},
    };
}

JsonObject BackendSession::buildAssetsState(const String &viewName) const
{
    const AnimeSceneModel &model = m_business->modelForView(viewName);
    JsonArray items;
    JsonArray names;
    for (int i = 0; i < model.assetCount(); ++i) {
        if (model.assetInternal(i)) {
            continue;
        }
        items.append(JsonObject{
            {String("index"), i},
            {String("name"), model.assetName(i)},
            {String("type"), int(model.assetType(i))},
        });
        names.append(model.assetName(i));
    }
    return JsonObject{
        {String("items"), items},
        {String("assets"), items},
        {String("names"), names},
        {String("selected"), m_business->attentionForView(viewName).asset},
    };
}

JsonObject BackendSession::buildTimelineState() const
{
    const AnimeTimelineState st = m_business->timelineState();
    JsonArray framesArr;
    JsonArray holdsArr;
    JsonArray namesArr;
    for (int i = 0; i < st.frameCount; ++i) {
        const bool hold = (i < st.holds.size()) ? st.holds.at(i) : false;
        const String name = (i < st.frameNames.size()) ? String(st.frameNames.at(i)) : String();
        framesArr.append(JsonObject{
            {String("index"), i},
            {String("name"), name},
            {String("hold"), hold},
        });
        holdsArr.append(hold);
        namesArr.append(name);
    }

    Vector<int> onionList = st.onionFrames.values();
    std::sort(onionList.begin(), onionList.end());
    JsonArray onionFramesArr;
    for (int f : onionList) {
        onionFramesArr.append(f);
    }

    JsonObject onionObj{
        {String("available"), st.onionAvailable},
        {String("enabled"), st.onionEnabled},
        {String("lines"), st.onionLines},
        {String("fills"), st.onionFills},
        {String("amLayers"), st.onionAmLayers},
        {String("guideLines"), st.onionGuideLines},
        {String("frames"), onionFramesArr},
    };

    const bool vertical = (m_timelineDockArea == String("left")
                           || m_timelineDockArea == String("right"));
    const bool leftAligned = (m_timelineDockArea == String("left"));

    return JsonObject{
        {String("view"), st.viewName},
        {String("visible"), m_timelineVisible},
        {String("collapsed"), m_timelineCollapsed},
        {String("floating"), m_timelineFloating},
        {String("dockArea"), m_timelineDockArea},
        {String("vertical"), vertical},
        {String("leftAligned"), leftAligned},
        {String("frameCount"), st.frameCount},
        {String("frames"), framesArr},
        {String("holds"), holdsArr},
        {String("frameNames"), namesArr},
        {String("currentFrame"), st.currentFrame},
        {String("fps"), st.fps},
        {String("playbackActive"), st.playbackActive},
        {String("playing"), st.playbackActive},
        {String("loop"), st.loop},
        {String("onion"), onionObj},
        {String("onionAvailable"), st.onionAvailable},
        {String("onionEnabled"), st.onionEnabled},
        {String("onionLines"), st.onionLines},
        {String("onionFills"), st.onionFills},
        {String("onionAmLayers"), st.onionAmLayers},
        {String("onionGuideLines"), st.onionGuideLines},
        {String("onionFrames"), onionFramesArr},
    };
}

JsonObject BackendSession::buildHistoryState() const
{
    const String activeView = m_business->activeViewName();
    const StringList labels = m_business->historyLabels(activeView);
    JsonArray labelsArr;
    for (const String &l : labels) {
        labelsArr.append(l);
    }
    return JsonObject{
        {String("view"), activeView},
        {String("labels"), labelsArr},
        {String("items"), labelsArr},
        {String("currentIndex"), m_business->currentHistoryIndex(activeView)},
        {String("canUndo"), m_business->canUndo()},
        {String("canRedo"), m_business->canRedo()},
        {String("undoTargetView"), m_business->undoTargetViewName()},
        {String("redoTargetView"), m_business->redoTargetViewName()},
    };
}

JsonObject BackendSession::buildWindowsState() const
{
    JsonObject out;
    JsonArray listArr;
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        JsonArray pagesArr;
        for (const String &p : info.pages) {
            pagesArr.append(p);
        }
        const JsonObject winObj{
            {String("name"), info.name},
            {String("title"), info.title},
            {String("visible"), info.visible},
            {String("pages"), pagesArr},
            {String("current"), info.current},
        };
        out.insert(info.name, winObj);
        listArr.append(winObj);
    }
    const bool vertical = (m_timelineDockArea == String("left")
                           || m_timelineDockArea == String("right"));
    const bool leftAligned = (m_timelineDockArea == String("left"));
    const JsonObject timelineObj{
        {String("name"), String("timeline")},
        {String("title"), String("Timeline")},
        {String("visible"), m_timelineVisible},
        {String("collapsed"), m_timelineCollapsed},
        {String("floating"), m_timelineFloating},
        {String("dockArea"), m_timelineDockArea},
        {String("vertical"), vertical},
        {String("leftAligned"), leftAligned},
        {String("pages"), JsonArray()},
        {String("current"), String()},
    };
    out.insert(String("timeline"), timelineObj);
    out.insert(String("textureSubControlVisible"), m_textureSubControlVisible);
    const JsonArray viewButtons = buildTextureViewButtonsState();
    if (!viewButtons.isEmpty()) {
        out.insert(String("textureViewButtons"), viewButtons);
    }
    out.insert(String("list"), listArr);
    return out;
}

JsonObject BackendSession::buildCursorState(const String &viewName) const
{
    const CursorState st = m_cursors.value(viewName);
    return JsonObject{
        {String("shape"), st.shape},
        {String("css"), st.css},
        {String("key"), st.key},
        {String("hx"), st.hx},
        {String("hy"), st.hy},
        {String("w"), st.w},
        {String("h"), st.h},
        {String("scale"), st.scale},
    };
}

JsonObject BackendSession::buildViewportState(const String &viewName) const
{
    const PaintOpenGLWidget *paintWidget = boardForView(viewName);
    if (!paintWidget) {
        return JsonObject();
    }
    if (paintWidget->unboundedCanvas()) {
        return JsonObject{
            {String("h"), JsonObject{
                {String("min"), 0},
                {String("max"), 0},
                {String("page"), paintWidget->width()},
                {String("value"), 0},
                {String("enabled"), false},
            }},
            {String("v"), JsonObject{
                {String("min"), 0},
                {String("max"), 0},
                {String("page"), paintWidget->height()},
                {String("value"), 0},
                {String("enabled"), false},
            }},
        };
    }

    const double zoomFactor = paintWidget->zoom();
    const RectF reach = paintWidget->reachableRect();
    const int left = int(std::lround(reach.left() * zoomFactor));
    const int right = int(std::lround(reach.right() * zoomFactor));
    const int top = int(std::lround(reach.top() * zoomFactor));
    const int bottom = int(std::lround(reach.bottom() * zoomFactor));
    const int hMin = std::min(left, right - paintWidget->width());
    const int hMax = std::max(left, right - paintWidget->width());
    const int vMin = std::min(top, bottom - paintWidget->height());
    const int vMax = std::max(top, bottom - paintWidget->height());
    const int hRange = hMax - hMin;
    const int vRange = vMax - vMin;
    const int hValue = std::clamp(int(std::lround(-paintWidget->panOffset().x())), hMin, hMax);
    const int vValue = std::clamp(int(std::lround(-paintWidget->panOffset().y())), vMin, vMax);

    return JsonObject{
        {String("h"), JsonObject{
            {String("min"), hMin},
            {String("max"), hMax},
            {String("page"), paintWidget->width()},
            {String("value"), hValue},
            {String("enabled"), hRange > 0},
        }},
        {String("v"), JsonObject{
            {String("min"), vMin},
            {String("max"), vMax},
            {String("page"), paintWidget->height()},
            {String("value"), vValue},
            {String("enabled"), vRange > 0},
        }},
    };
}

JsonObject BackendSession::buildPadState() const
{
    JsonObject padsObj;
    for (auto it = m_padValues.cbegin(); it != m_padValues.cend(); ++it) {
        padsObj.insert(it.key(), JsonObject{
            {String("x"), it.value().x()},
            {String("y"), it.value().y()},
        });
    }
    return JsonObject{
        {String("x"), m_forcePadValue.x()},
        {String("y"), m_forcePadValue.y()},
        {String("pads"), padsObj},
    };
}
