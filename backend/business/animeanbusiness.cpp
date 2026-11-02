// Source: Copied from C:/Users/admin/Documents/VibeFrontend/animean/business/animeanbusiness.cpp
#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"
#include <cstdint>
#include <utility>
#include "animeanbusiness.h"


#include "projectio.h"

#include "algorithm/core/file.h"
#include "algorithm/core/painter.h"
#include "platform/signalslot.h"
#include "platform/timer.h"
#include "algorithm/core/json.h"





#include <algorithm>
#include <cmath>
#include <string>
using namespace AnimeCore;

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots

#endif
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#if 1



#endif
#include "pythonbind/python_bindings.h"
namespace py = pybind11;
#endif

namespace {

constexpr int kDefaultPlaybackFps = 12;

#ifdef ANIMEAN_WITH_PYTHON
py::dict pointToPythonDict(const PointF &point)
{
    py::dict pointInfo;
    pointInfo["x"] = point.x();
    pointInfo["y"] = point.y();
    return pointInfo;
}

py::object variantToPythonObject(const JsonVariant &value)
{
    const JsonValue &jv = value.toJsonValue(); if (jv.isBool()) return py::bool_(value.toBool()); if (jv.isInt64()) return py::int_(value.toLongLong()); if (jv.isDouble()) return py::float_(value.toDouble()); switch (0) {








        break;

    default:
        return py::str(value.toString().toStdString());
    }
}

bool isQuietHookOutput(const String &output)
{
    return output.isEmpty() || output.endsWith(String("(no output)"));
}

String pythonHookSendMessage(const py::dict &message)
{
    if (!Py_IsInitialized()) {
        return String();
    }
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
#endif

String fileNameForBuiltInTool(AnimeanBusiness::Tool tool)
{
    switch (tool) {
    case AnimeanBusiness::Tool::Pen:
        return String("pen.json");
    case AnimeanBusiness::Tool::Fill:
        return String("fill.json");
    case AnimeanBusiness::Tool::Eraser:
    case AnimeanBusiness::Tool::DeleteLine:
    case AnimeanBusiness::Tool::CutLine:
        return String("eraser.json");
    case AnimeanBusiness::Tool::Arrow:
        return String("arrow.json");
    case AnimeanBusiness::Tool::Connect:
        return String("connect.json");
    case AnimeanBusiness::Tool::Transfer:
        return String("transfer.json");
    }
    return String("pen.json");
}

String resolveToolControlPath(const String &fileName)
{
    const String appPath = CoreApplication::applicationDirPath();
    const StringList candidates = {
        Dir(appPath).filePath(String("childrenpanel/tool_controls/%1").arg(fileName)),
        Dir(appPath).filePath(String("tool_controls/%1").arg(fileName)),
#ifdef ANIMEAN_SOURCE_DIR
        Dir(String(ANIMEAN_SOURCE_DIR)).filePath(String("childrenpanel/tool_controls/%1").arg(fileName)),
#endif
    };

    for (const String &candidate : candidates) {
        if (FileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return String();
}

void setControlValue(JsonObject *control, const String &name, const JsonValue &value)
{
    if (control->value(String("name")).toString() == name) {
        control->insert(String("value"), value);
    }
}

JsonObject applyRuntimeStateToToolLayout(JsonObject layout,
                                          AnimeanBusiness::Tool tool,
                                          int smoothValue,
                                          int penWidth,
                                          AnimeanBusiness::FillScope fillScope)
{
    const String fillScopeValue = fillScope == AnimeanBusiness::FillScope::AllLayers
                                       ? String("all")
                                       : String("current");
    const String eraserModeValue =
        tool == AnimeanBusiness::Tool::DeleteLine ? String("line")
        : tool == AnimeanBusiness::Tool::CutLine ? String("cut")
                                                 : String("area");

    JsonArray controls = layout.value(String("controls")).toArray();
    for (int index = 0; index < controls.size(); ++index) {
        if (!controls.at(index).isObject()) {
            continue;
        }

        JsonObject control = controls.at(index).toObject();
        setControlValue(&control, String("smooth"), smoothValue);
        setControlValue(&control, String("pen_width"), penWidth);
        setControlValue(&control, String("fill_scope"), fillScopeValue);
        setControlValue(&control, String("eraser_mode"), eraserModeValue);
        controls.replace(index, control);
    }

    layout.insert(String("controls"), controls);
    return layout;
}

bool readJsonObjectFromFile(const String &fileName, JsonObject *object, String *errorOut)
{
    File file(fileName);
    if (!file.open(IODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = String("Failed to read file:\n%1").arg(file.errorString());
        }
        return false;
    }

    JsonParseError parseError;
    const JsonDocument document = JsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != JsonParseError::NoError || !document.isObject()) {
        if (errorOut) {
            *errorOut = String("File format error:\n%1").arg(parseError.errorString());
        }
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObjectToFile(const JsonObject &object, const String &fileName, String *errorOut)
{
    SaveFile file(fileName);
    if (!file.open(IODevice::WriteOnly)) {
        if (errorOut) {
            *errorOut = String("Failed to write file:\n%1").arg(file.errorString());
        }
        return false;
    }

    const JsonDocument document(object);
    file.write(document.toJson(JsonDocument::Indented));
    if (!file.commit()) {
        if (errorOut) {
            *errorOut = String("Failed to save file:\n%1").arg(file.errorString());
        }
        return false;
    }

    return true;
}

void renderSceneFrameContent(Painter &painter,
                             const AnimeSceneModel &model,
                             int frameIndex,
                             double scale)
{
    if (frameIndex < 0 || frameIndex >= model.frameCount()) {
        return;
    }
    for (int layer = 0; layer < model.layerCount(); ++layer) {
        if (!model.layerVisible(layer) || model.layerInternal(layer)) {
            continue;
        }
        const AnimeVectorImageModel *image = model.imageAt(frameIndex, layer);
        if (!image) {
            continue;
        }
        if (image->hasRaster()) {
            painter.drawImage(PointF(image->raster().topLeft), Image(image->raster().image));
        }
        for (const AnimeVectorFillRegion &region : image->fillRegions()) {
            if (region.path.isEmpty()) {
                continue;
            }
            painter.setPen(Pen(Color(region.color),
                                AnimeVectorLogic::kVectorRegionOverpaintWidth,
                                SolidLine,
                                RoundCap,
                                RoundJoin));
            painter.setBrush(Color(region.color));
            painter.drawPath(region.path);
        }
        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            const AnimeVectorStroke &stroke = node.stroke;
            if (stroke.path.isEmpty()) {
                continue;
            }
            const PenStyle penStyle =
                (stroke.penStyle >= SolidLine && stroke.penStyle <= DashDotDotLine)
                    ? static_cast<PenStyle>(stroke.penStyle)
                    : SolidLine;
            painter.setBrush(NoBrush);
            painter.setPen(Pen(Color(stroke.color),
                                AnimeVectorLogic::displayStrokeWidth(stroke.width, scale),
                                penStyle,
                                RoundCap,
                                RoundJoin));
            painter.drawPath(stroke.path);
        }
    }
}

} // namespace

AnimeanBusiness::AnimeanBusiness()
    : m_playbackTimer(new Timer(this))



{



    m_internalMainModel.setTextId(String("main_paint_view"));
    m_internalMainModel.setIntId(1);
    m_internalChildModel.setTextId(String("child_paint_view"));
    m_internalChildModel.setIntId(2);

    m_internalMainHistory.reset(String("Initial"), m_internalMainModel);
    m_internalChildHistory.reset(String("Initial"), m_internalChildModel);

    for (const String &viewName : {String("main"), String("child")}) {
        AnimeSceneModel &model = modelForView(viewName);
        SelectionAttention &attention = m_attentionByView[viewName];
        attention.frame = model.currentFrame();
        attention.layer = model.currentLayer();
        attention.asset = model.currentAsset();
        constrainAttention(model, &attention, AttentionChange::FrameChange);
        model.setCurrentFrame(attention.frame);
        model.setCurrentLayer(attention.layer);
        model.setCurrentAsset(attention.asset);

        HeadlessViewState &headless = m_headlessViewState[viewName];
        headless.tool = int(Tool::Pen);
        headless.pythonNotifiedFrame = model.currentFrame();
        headless.pythonNotifiedLayer = model.currentLayer();
        headless.pythonNotifiedLayerId = headless.pythonNotifiedLayer >= 0
                                             ? model.layerIdAt(headless.pythonNotifiedLayer)
                                             : 0;
    }

    m_playbackTimer->setInterval(1000 / kDefaultPlaybackFps);
    m_playbackTimer->timeout.connect(this, &AnimeanBusiness::advancePlaybackFrame);

#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&m_internalChildModel);
    registerAnimeanUiScene(&m_internalMainModel);
    registerAnimeanUiToolOptionsCallback([this]() {
        if (m_activeExtraTool.isEmpty()) {
            refreshToolOptions();
        } else {
            refreshExtraToolOptions();
        }
    });
    registerAnimeanUiRefreshCallback([this](bool frame, bool layer, bool asset, bool) {
        syncAllViewAttentionsFromModels();
        if (frame) {
            timelineChanged();
            const int childFrame = modelForView(String("child")).currentFrame();
            if (childFrame != m_lastAutoFitChildFrame) {
                m_lastAutoFitChildFrame = childFrame;
                requestAutoFitTextureControlView();
            }
        }
        if (layer) {
            layersChanged(String("main"), attentionForView(String("main")).layer);
            layersChanged(String("child"), attentionForView(String("child")).layer);
        }
        if (asset) {
            const String target = assetPanelTargetViewName();
            assetsChanged(target, attentionForView(target).asset);
        }
        syncEmbeddedPythonState();
    });
    registerAnimeanUiLockedToolsCallback([this](const String &view, const StringList &tools) {
        applyLockedTools(view, tools);
    });
    registerAnimeanUiDrawColorCallback([this](const Color &color) {
        setDrawingColor(color);
    });
    registerAnimeanUiDrawSettingsCallback([this](int stabilizer, int simplify, int corner) {
        setDrawSettings(stabilizer, simplify, corner);
    });
    registerAnimeanUiHistoryCallback([this](const String &op, const String &viewName, const String &label) {
        if (!viewName.isEmpty()
            && viewName != String("main")
            && viewName != String("child")) {
            return;
        }
        if (op == String("commit")) {
            commitHistory(label.isEmpty() ? String("Python") : label, viewName);
        } else if (op == String("undo")) {
            undo(viewName);
        } else if (op == String("redo")) {
            redo(viewName);
        }
    });
    if (Py_IsInitialized()) {
        syncEmbeddedPythonState();
    }
#endif
}

AnimeanBusiness::~AnimeanBusiness()
{
#ifdef ANIMEAN_WITH_PYTHON
    clearAnimeanUiHistoryCallback();
    clearAnimeanUiLockedToolsCallback();
    clearAnimeanUiDrawColorCallback();
    clearAnimeanUiToolOptionsCallback();
    clearAnimeanUiRefreshCallback();
    for (auto it = m_adapters.cbegin(); it != m_adapters.cend(); ++it) {
        if (it.value().model) {
            unregisterAnimeanUiScene(it.value().model);
        }
    }
    unregisterAnimeanUiScene(&m_internalChildModel);
    unregisterAnimeanUiScene(&m_internalMainModel);
#endif
    delete m_playbackTimer;
#ifdef ANIMEAN_WITH_PYTHON
#endif
}

String AnimeanBusiness::resolveViewName(const String &viewName) const
{
    if (viewName == String("child")) {
        return String("child");
    }
    if (viewName == String("main")) {
        return String("main");
    }
    return m_activeViewName == String("child")
               ? String("child")
               : String("main");
}

AnimePaintViewAdapter *AnimeanBusiness::adapterFor(const String &viewName)
{
    const String key = resolveViewName(viewName);
    auto it = m_adapters.find(key);
    return it == m_adapters.end() ? nullptr : &it.value();
}

const AnimePaintViewAdapter *AnimeanBusiness::adapterFor(const String &viewName) const
{
    const String key = resolveViewName(viewName);
    auto it = m_adapters.constFind(key);
    return it == m_adapters.constEnd() ? nullptr : &it.value();
}

void AnimeanBusiness::bindPaintView(const String &viewName, const AnimePaintViewAdapter &adapter)
{
    const String key = resolveViewName(viewName);
#ifdef ANIMEAN_WITH_PYTHON
    if (const AnimePaintViewAdapter *oldAdapter = adapterFor(key)) {
        if (oldAdapter->model) {
            unregisterAnimeanUiScene(oldAdapter->model);
        }
    }
    unregisterAnimeanUiScene(key == String("child") ? &m_internalChildModel
                                                            : &m_internalMainModel);
#endif
    m_adapters.insert(key, adapter);

    AnimeSceneModel &model = modelForView(key);
    model.setTextId(key == String("child")
                        ? String("child_paint_view")
                        : String("main_paint_view"));
    model.setIntId(key == String("child") ? 2 : 1);

#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&modelForView(String("child")));
    registerAnimeanUiScene(&modelForView(String("main")));
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif

    SelectionAttention &attention = m_attentionByView[key];
    attention.frame = model.currentFrame();
    attention.layer = model.currentLayer();
    attention.asset = model.currentAsset();
    constrainAttention(model, &attention, AttentionChange::FrameChange);

    if (key == String("main")) {
        mainPaintViewObjectChanged(mainPaintViewObject());
    } else if (key == String("child")) {
        childPaintViewObjectChanged(childPaintViewObject());
    }
    if (key == m_activeViewName) {
        activePaintViewObjectChanged(activePaintViewObject());
    }
}

void AnimeanBusiness::unbindPaintView(const String &viewName)
{
    const String key = resolveViewName(viewName);
#ifdef ANIMEAN_WITH_PYTHON
    if (const AnimePaintViewAdapter *oldAdapter = adapterFor(key)) {
        if (oldAdapter->model) {
            unregisterAnimeanUiScene(oldAdapter->model);
        }
    }
#endif
    m_adapters.remove(key);
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&modelForView(String("child")));
    registerAnimeanUiScene(&modelForView(String("main")));
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif
    if (key == String("main")) {
        mainPaintViewObjectChanged(mainPaintViewObject());
    } else if (key == String("child")) {
        childPaintViewObjectChanged(childPaintViewObject());
    }
    if (key == m_activeViewName) {
        activePaintViewObjectChanged(activePaintViewObject());
    }
}

void *AnimeanBusiness::paintViewObject(const String &viewName) const
{
    const String key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->viewObject) {
            return adapter->viewObject;
        }
    }
    return key == String("child") ? m_internalChildViewObject : m_internalMainViewObject;
}

void *AnimeanBusiness::mainPaintViewObject() const
{
    return paintViewObject(String("main"));
}

void *AnimeanBusiness::childPaintViewObject() const
{
    return paintViewObject(String("child"));
}

void *AnimeanBusiness::activePaintViewObject() const
{
    return paintViewObject(m_activeViewName);
}

String AnimeanBusiness::activeViewName() const
{
    return m_activeViewName;
}

String AnimeanBusiness::framePanelTargetViewName() const
{
    if (m_activeViewName == String("child") && !m_changeableTimeline) {
        return String("main");
    }
    return m_activeViewName;
}

String AnimeanBusiness::layerPanelTargetViewName() const
{
    if (m_activeViewName == String("child") && !m_changeableTexture) {
        return String("main");
    }
    return m_activeViewName;
}

String AnimeanBusiness::assetPanelTargetViewName() const
{
    return layerPanelTargetViewName();
}

bool AnimeanBusiness::changeableTimeline() const
{
    return m_changeableTimeline;
}

bool AnimeanBusiness::changeableTexture() const
{
    return m_changeableTexture;
}

void AnimeanBusiness::setChangeableTimeline(bool changeable)
{
    if (m_changeableTimeline == changeable) {
        return;
    }
    m_changeableTimeline = changeable;
    requestClearTimelineThumbnails();
    timelineChanged();
}

void AnimeanBusiness::setChangeableTexture(bool changeable)
{
    if (m_changeableTexture == changeable) {
        return;
    }
    m_changeableTexture = changeable;
    const String target = assetPanelTargetViewName();
    assetsChanged(target, attentionForView(target).asset);
}

void AnimeanBusiness::setActiveViewName(const String &viewName)
{
    const String resolved = resolveViewName(viewName);
    if (m_activeViewName == resolved) {
#ifdef ANIMEAN_WITH_PYTHON
        registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif
        return;
    }

    stopPlayback();
    m_activeViewName = resolved;
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif

    activeViewChanged(m_activeViewName);
    activePaintViewObjectChanged(activePaintViewObject());
    requestClearTimelineThumbnails();
    timelineChanged();
    layersChanged(String("main"), attentionForView(String("main")).layer);
    layersChanged(String("child"), attentionForView(String("child")).layer);
    const String assetTarget = assetPanelTargetViewName();
    assetsChanged(assetTarget, attentionForView(assetTarget).asset);
    historyChanged();
    syncEmbeddedPythonState();
    refreshToolLockState();
    setStatusMessage(String("Active paint view: %1").arg(m_activeViewName));
}

AnimeSceneModel &AnimeanBusiness::modelForView(const String &viewName)
{
    const String key = resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->model) {
            return *adapter->model;
        }
    }
    return key == String("child") ? m_internalChildModel : m_internalMainModel;
}

const AnimeSceneModel &AnimeanBusiness::modelForView(const String &viewName) const
{
    const String key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->model) {
            return *adapter->model;
        }
    }
    return key == String("child") ? m_internalChildModel : m_internalMainModel;
}

SceneHistory &AnimeanBusiness::historyForView(const String &viewName)
{
    const String key = resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->history) {
            return *adapter->history;
        }
    }
    return key == String("child") ? m_internalChildHistory : m_internalMainHistory;
}

const SceneHistory &AnimeanBusiness::historyForView(const String &viewName) const
{
    const String key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->history) {
            return *adapter->history;
        }
    }
    return key == String("child") ? m_internalChildHistory : m_internalMainHistory;
}

SelectionAttention &AnimeanBusiness::attentionForView(const String &viewName)
{
    return m_attentionByView[resolveViewName(viewName)];
}

SelectionAttention AnimeanBusiness::attentionForView(const String &viewName) const
{
    return m_attentionByView.value(resolveViewName(viewName));
}

void AnimeanBusiness::syncAllViewAttentionsFromModels()
{
    for (const String &viewName : {String("main"), String("child")}) {
        AnimeSceneModel &model = modelForView(viewName);
        SelectionAttention &attention = attentionForView(viewName);
        attention.frame = model.currentFrame();
        attention.layer = model.currentLayer();
        attention.asset = model.currentAsset();
        if (AnimePaintViewAdapter *adapter = adapterFor(viewName)) {
            if (adapter->setCurrentFrame) {
                adapter->setCurrentFrame(attention.frame);
            } else {
                notifyHeadlessFrameChangedIfNeeded(viewName);
            }
            if (adapter->setCurrentLayer) {
                adapter->setCurrentLayer(attention.layer);
            } else {
                notifyHeadlessLayerChangedIfNeeded(viewName);
            }
            if (adapter->setCurrentAsset) {
                adapter->setCurrentAsset(attention.asset);
            }
        } else {
            notifyHeadlessFrameChangedIfNeeded(viewName);
            notifyHeadlessLayerChangedIfNeeded(viewName);
        }
    }
}

void AnimeanBusiness::updateAttention(const String &viewName,
                                      AttentionChange change,
                                      int frame,
                                      int layer,
                                      int asset)
{
    const String key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);
    SelectionAttention &attention = attentionForView(key);
    const SelectionAttention previous = attention;
    attention.frame = frame;
    attention.layer = layer;
    attention.asset = asset;

    AttentionUpdate update = constrainAttention(model, &attention, change);
    update.frame = update.frame || previous.frame != attention.frame;
    update.layer = update.layer || previous.layer != attention.layer;
    update.asset = update.asset || previous.asset != attention.asset;

    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->setCurrentFrame) {
            adapter->setCurrentFrame(attention.frame);
        } else {
            model.setCurrentFrame(attention.frame);
            notifyHeadlessFrameChangedIfNeeded(key);
        }
        if (adapter->setCurrentLayer) {
            adapter->setCurrentLayer(attention.layer);
        } else {
            model.setCurrentLayer(attention.layer);
            notifyHeadlessLayerChangedIfNeeded(key);
        }
        if (adapter->setCurrentAsset) {
            adapter->setCurrentAsset(attention.asset);
        } else {
            model.setCurrentAsset(attention.asset);
        }
    } else {
        model.setCurrentFrame(attention.frame);
        notifyHeadlessFrameChangedIfNeeded(key);
        model.setCurrentLayer(attention.layer);
        notifyHeadlessLayerChangedIfNeeded(key);
        model.setCurrentAsset(attention.asset);
    }

    if (update.frame && key == framePanelTargetViewName()) {
        timelineChanged();
    }
    if (update.frame && key == String("child")) {
        requestAutoFitTextureControlView();
    }
    if (update.layer) {
        layersChanged(key, attention.layer);
    }
    if (update.asset && key == assetPanelTargetViewName()) {
        assetsChanged(key, attention.asset);
    }
    attentionUpdated(key, update.frame, update.layer, update.asset);
    syncEmbeddedPythonState();
}

Image AnimeanBusiness::renderFrameThumbnail(int frameIndex,
                                             const Size &size,
                                             const String &viewName)
{
    const String key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->renderFrameThumbnail) {
            return adapter->renderFrameThumbnail(frameIndex, size);
        }
    }

    if (size.width() <= 0 || size.height() <= 0) {
        return Image();
    }
    Image image(size, Image::Format_ARGB32_Premultiplied);
    image.fill(Color(255, 255, 255));

    const AnimeSceneModel &model = modelForView(key);
    if (frameIndex < 0 || frameIndex >= model.frameCount()) {
        return image;
    }

    const Size canvas = model.canvasSize();
    if (canvas.width() <= 0 || canvas.height() <= 0) {
        return image;
    }
    const double scale = std::min(double(size.width()) / canvas.width(),
                                 double(size.height()) / canvas.height());

    Painter painter(&image);
    painter.setRenderHint(Painter::Antialiasing);
    painter.setRenderHint(Painter::SmoothPixmapTransform);
    painter.translate((size.width() - canvas.width() * scale) * 0.5,
                      (size.height() - canvas.height() * scale) * 0.5);
    painter.scale(scale, scale);
    renderSceneFrameContent(painter, model, frameIndex, scale);
    painter.end();
    return image;
}

String AnimeanBusiness::statusMessage() const
{
    return m_statusMessage;
}

void AnimeanBusiness::setStatusMessage(const String &message)
{
    m_statusMessage = message;
    statusMessageChanged(m_statusMessage);
}

String AnimeanBusiness::currentFilePath() const
{
    return m_currentFilePath;
}

void AnimeanBusiness::setCurrentFilePath(const String &filePath)
{
    if (m_currentFilePath == filePath) {
        return;
    }
    m_currentFilePath = filePath;
    projectFilePathChanged(m_currentFilePath);
    windowTitleChanged(windowTitle());
}

String AnimeanBusiness::childFilePath() const
{
    return m_childFilePath;
}

void AnimeanBusiness::setChildFilePath(const String &filePath)
{
    if (m_childFilePath == filePath) {
        return;
    }
    m_childFilePath = filePath;
    childFilePathChanged(m_childFilePath);
    windowTitleChanged(windowTitle());
}

String AnimeanBusiness::windowTitle() const
{
    const String fileName = m_currentFilePath.isEmpty()
                                 ? String("Untitled")
                                 : FileInfo(m_currentFilePath).fileName();
    String title = String("AnimeAn - %1").arg(fileName);
    if (!m_childFilePath.isEmpty()) {
        title += String(" [texture: %1]").arg(FileInfo(m_childFilePath).fileName());
    }
    return title;
}

Size AnimeanBusiness::canvasSize(const String &viewName) const
{
    return modelForView(viewName).canvasSize();
}

String AnimeanBusiness::ensureProjectFileExtension(const String &fileName)
{
    return ::ensureProjectFileExtension(fileName);
}

String AnimeanBusiness::ensureTextureViewFileExtension(const String &fileName)
{
    return ::ensureTextureViewFileExtension(fileName);
}

String AnimeanBusiness::projectOpenFilter()
{
    return ::projectOpenFilter();
}

String AnimeanBusiness::projectSaveFilter()
{
    return ::projectSaveFilter();
}

String AnimeanBusiness::textureViewOpenFilter()
{
    return ::textureViewOpenFilter();
}

String AnimeanBusiness::textureViewSaveFilter()
{
    return ::textureViewSaveFilter();
}

void AnimeanBusiness::newProject(const Size &canvasSize)
{
    applyNewCanvasSize(canvasSize);
}

void AnimeanBusiness::applyNewCanvasSize(const Size &size)
{
    AnimeSceneModel &mainModel = modelForView(String("main"));
    if (Size(mainModel.canvasSize()) == size) {
        setStatusMessage(String("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
        return;
    }

    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setCanvasSize) {
            adapter->setCanvasSize(size);
        } else {
            mainModel.setCanvasSize(size);
        }
    } else {
        mainModel.setCanvasSize(size);
    }

    commitHistory(String("Canvas Size"), String("main"));
    canvasSizeChanged(String("main"), size);
    syncEmbeddedPythonState();
    refreshToolOptions();
    setStatusMessage(String("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
}

void AnimeanBusiness::installLoadedModel(const String &viewName, const AnimeSceneModel &model)
{
    const String key = resolveViewName(viewName);
    const bool isChild = key == String("child");
    AnimeSceneModel &targetModel = modelForView(key);
    targetModel = model;
    targetModel.setTextId(isChild ? String("child_paint_view")
                                  : String("main_paint_view"));
    targetModel.setIntId(isChild ? 2 : 1);
}

void AnimeanBusiness::activateLoadedModel(const String &viewName, const String &historyLabel)
{
    const String key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&model);
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->modelReplaced) {
            adapter->modelReplaced();
        }
    }
    resetHistory(historyLabel, key);
    updateAttention(key,
                    AttentionChange::FrameChange,
                    model.currentFrame(),
                    model.currentLayer(),
                    model.currentAsset());
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->requestUpdate) {
            adapter->requestUpdate();
        }
    }
    canvasSizeChanged(key, model.canvasSize());
}

void AnimeanBusiness::adoptLoadedModel(const String &viewName,
                                       const AnimeSceneModel &model,
                                       const String &historyLabel)
{
    installLoadedModel(viewName, model);
    activateLoadedModel(viewName, historyLabel);
}

bool AnimeanBusiness::saveProjectTo(const String &fileName, String *errorOut)
{
    const String projectFileName = ensureProjectFileExtension(fileName);
    const JsonObject json = projectToJson(modelForView(String("main")),
                                           modelForView(String("child")));
    if (!writeJsonObjectToFile(json, projectFileName, errorOut)) {
        return false;
    }

    m_currentFilePath = projectFileName;
    projectFilePathChanged(m_currentFilePath);
    windowTitleChanged(windowTitle());
    setStatusMessage(String("Saved project (main + texture): %1")
                         .arg(FileInfo(projectFileName).fileName()));
    return true;
}

bool AnimeanBusiness::loadProjectFrom(const String &fileName, String *errorOut)
{
    stopPlayback();
    JsonObject root;
    if (!readJsonObjectFromFile(fileName, &root, errorOut)) {
        return false;
    }

    AnimeSceneModel loadedMainModel;
    AnimeSceneModel loadedTextureModel;
    String error;
    AnimeCore::String coreError;
    if (!projectFromJson(root, &loadedMainModel, &loadedTextureModel, &coreError)) {
        error = coreError;
        if (errorOut) {
            *errorOut = error.isEmpty() ? String("Unsupported project file.") : error;
        }
        return false;
    }

    installLoadedModel(String("main"), loadedMainModel);
    installLoadedModel(String("child"), loadedTextureModel);
    activateLoadedModel(String("main"), String("Open Project"));
    activateLoadedModel(String("child"), String("Open Project"));

    m_childFilePath.clear();
    m_currentFilePath = fileName;
    childFilePathChanged(m_childFilePath);
    projectFilePathChanged(m_currentFilePath);
    windowTitleChanged(windowTitle());
    requestSurfaceMainView();
    setActiveViewName(String("main"));
    syncEmbeddedPythonState();
    refreshToolOptions();
    setStatusMessage(String("Opened project (main + texture): %1")
                         .arg(FileInfo(fileName).fileName()));
    return true;
}

bool AnimeanBusiness::openTextureViewFrom(const String &fileName, String *errorOut)
{
    stopPlayback();
    JsonObject root;
    if (!readJsonObjectFromFile(fileName, &root, errorOut)) {
        return false;
    }

    AnimeSceneModel loadedModel;
    String error;
    AnimeCore::String coreError;
    if (!textureViewFromJson(root, &loadedModel, &coreError)) {
        error = coreError;
        if (errorOut) {
            *errorOut = error.isEmpty() ? String("Unsupported texture file.") : error;
        }
        return false;
    }

    adoptLoadedModel(String("child"), loadedModel, String("Open Texture View"));
    m_childFilePath = fileName;
    childFilePathChanged(m_childFilePath);
    windowTitleChanged(windowTitle());
    requestSurfaceTextureView();
    setActiveViewName(String("child"));
    setStatusMessage(String("Opened texture view: %1").arg(FileInfo(fileName).fileName()));
    return true;
}

bool AnimeanBusiness::saveTextureViewTo(const String &fileName, String *errorOut)
{
    const String targetName = ensureTextureViewFileExtension(fileName);
    const JsonObject json = textureViewToJson(modelForView(String("child")));
    if (!writeJsonObjectToFile(json, targetName, errorOut)) {
        return false;
    }

    m_childFilePath = targetName;
    childFilePathChanged(m_childFilePath);
    windowTitleChanged(windowTitle());
    setStatusMessage(String("Saved texture board: %1").arg(FileInfo(targetName).fileName()));
    return true;
}

int AnimeanBusiness::layerCount(const String &viewName) const
{
    return modelForView(viewName).layerCount();
}

int AnimeanBusiness::currentLayer(const String &viewName) const
{
    return attentionForView(viewName).layer;
}

String AnimeanBusiness::layerName(int layerIndex, const String &viewName) const
{
    return modelForView(viewName).layerName(layerIndex);
}

bool AnimeanBusiness::isLayerVisible(int layerIndex, const String &viewName) const
{
    return modelForView(viewName).layerVisible(layerIndex);
}

Vector<AnimeLayerPanelRow> AnimeanBusiness::layerRowsForView(const String &viewName) const
{
    const AnimeSceneModel &model = modelForView(viewName);
    const int frame = model.currentFrame();

    Vector<AnimeLayerPanelRow> rows;
    std::function<int(const AnimeCore::Vector<AnimeLayerNode> &, int)> collect =
        [&](const AnimeCore::Vector<AnimeLayerNode> &nodes, int depth) {
        int shown = 0;
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                const int mark = rows.size();
                AnimeLayerPanelRow row;
                row.group = true;
                row.groupId = node.groupId;
                row.name = node.name;
                row.collapsed = node.collapsed;
                row.depth = depth;
                rows.append(row);
                if (collect(node.children, depth + 1) == 0) {
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
            AnimeLayerPanelRow row;
            row.layerIndex = index;
            row.name = model.layerName(index);
            row.visible = model.layerVisible(index);
            row.depth = depth;
            rows.append(row);
            ++shown;
        }
        return shown;
    };
    collect(model.layerTree(), 0);

    {
        Hash<int, int> rowForLayer;
        for (int i = 0; i < rows.size(); ++i) {
            if (!rows[i].group) {
                rowForLayer.insert(rows[i].layerIndex, i);
            }
        }
        Vector<int> parentRow(rows.size(), -1);
        Vector<Vector<int>> childRows(rows.size());
        bool nested = false;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].group) {
                continue;
            }
            const int parentId = model.layerParentId(rows[i].layerIndex);
            if (parentId <= 0) {
                continue;
            }
            const int parentIndex = model.layerIndexForId(parentId);
            const int at = parentIndex >= 0 ? rowForLayer.value(parentIndex, -1) : -1;
            if (at < 0 || at == i) {
                continue;
            }
            parentRow[i] = at;
            childRows[at].append(i);
            nested = true;
        }
        if (nested) {
            Vector<AnimeLayerPanelRow> nestedRows;
            nestedRows.reserve(rows.size());
            Vector<char> emitted(rows.size(), 0);
            std::function<void(int, int)> emitRow = [&](int i, int depth) {
                if (emitted[i]) {
                    return;
                }
                emitted[i] = 1;
                AnimeLayerPanelRow row = rows[i];
                row.depth = depth;
                row.child = parentRow[i] >= 0;
                row.parent = !childRows[i].isEmpty();
                if (row.child) {
                    row.name = String("└ ") + row.name;
                }
                nestedRows.append(row);
                for (int childIndex : childRows[i]) {
                    emitRow(childIndex, depth + 1);
                }
            };
            for (int i = 0; i < rows.size(); ++i) {
                if (parentRow[i] < 0) {
                    emitRow(i, rows[i].depth);
                }
            }
            for (int i = nestedRows.size() - 1; i >= 0; --i) {
                if (!nestedRows[i].group) {
                    continue;
                }
                const bool hasContent = i + 1 < nestedRows.size()
                                        && nestedRows[i + 1].depth > nestedRows[i].depth;
                if (!hasContent) {
                    nestedRows.remove(i);
                }
            }
            rows = nestedRows;
        }
    }

    return rows;
}

int AnimeanBusiness::applyLayerPanelStructure(const String &viewName,
                                              const Vector<AnimeLayerPanelRow> &rows,
                                              int movedColumnId)
{
    const String key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);
    Set<int> hostedRows;

    const auto parseLevel = [&](const auto &self,
                                int &idx,
                                int parentDepth,
                                bool insideLeaf) -> Vector<AnimeLayerNode> {
        Vector<AnimeLayerNode> nodes;
        while (idx < rows.size()) {
            const AnimeLayerPanelRow &row = rows[idx];
            if (row.depth <= parentDepth) {
                break;
            }
            ++idx;
            if (insideLeaf || row.child) {
                if (!row.group) {
                    const int childLayerId = model.layerIdAt(row.layerIndex);
                    if (childLayerId > 0) {
                        hostedRows.insert(childLayerId);
                    }
                }
                self(self, idx, row.depth, true);
                continue;
            }
            if (row.group) {
                AnimeLayerNode node;
                node.groupId = row.groupId;
                node.name = row.name;
                node.collapsed = row.collapsed;
                // The rows carry no tag: without reading it back, one drag strips
                // "automapping" off every unit group and orphans its config.
                node.tag = model.layerGroupTag(row.groupId);
                node.children = self(self, idx, row.depth, false);
                if (!node.children.isEmpty()) {
                    nodes.append(node);
                }
            } else {
                AnimeLayerNode node;
                node.layerId = model.layerIdAt(row.layerIndex);
                self(self, idx, row.depth, true);
                if (node.layerId > 0) {
                    nodes.append(node);
                }
            }
        }
        return nodes;
    };

    int idx = 0;
    Vector<AnimeLayerNode> captured = parseLevel(parseLevel, idx, -1, false);

    if (!hostedRows.isEmpty()) {
        const auto appendToGroup = [&](const auto &self,
                                       Vector<AnimeLayerNode> &nodes,
                                       int groupId,
                                       int layerId) -> bool {
            for (AnimeLayerNode &node : nodes) {
                if (!node.isGroup()) {
                    continue;
                }
                if (node.groupId == groupId) {
                    AnimeLayerNode leaf;
                    leaf.layerId = layerId;
                    node.children.append(leaf);
                    return true;
                }
                if (self(self, node.children, groupId, layerId)) {
                    return true;
                }
            }
            return false;
        };

        const auto restoreOriginalGroups = [&](const auto &self,
                                               const Vector<AnimeLayerNode> &original,
                                               int parentGroupId) -> void {
            for (const AnimeLayerNode &node : original) {
                if (node.isGroup()) {
                    self(self, node.children, node.groupId);
                    continue;
                }
                if (!hostedRows.contains(node.layerId)) {
                    continue;
                }
                if (parentGroupId <= 0
                    || !appendToGroup(appendToGroup, captured, parentGroupId, node.layerId)) {
                    AnimeLayerNode leaf;
                    leaf.layerId = node.layerId;
                    captured.append(leaf);
                }
            }
        };
        restoreOriginalGroups(restoreOriginalGroups, model.layerTree(), 0);
    }

    Vector<std::pair<int, int>> flatGroupAndLayerRoles;
    flatGroupAndLayerRoles.reserve(rows.size());
    for (const AnimeLayerPanelRow &row : rows) {
        const int groupId = row.group ? row.groupId : 0;
        const int layerRole = row.group ? -1 : row.layerIndex;
        flatGroupAndLayerRoles.append({groupId, layerRole});
    }

    const int landedOn = reorderLayerTree(key, captured, movedColumnId, flatGroupAndLayerRoles);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->requestUpdate) {
            adapter->requestUpdate();
        }
    }
    return landedOn;
}

int AnimeanBusiness::reorderLayerTree(const String &viewName,
                                      const Vector<AnimeLayerNode> &capturedTree,
                                      int movedColumnId,
                                      const Vector<std::pair<int, int>> &flatGroupAndLayerRoles)
{
    const String key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);

    int landedOn = -1;
    const int fromIndex = model.layerIndexForId(movedColumnId);
    if (fromIndex >= 0) {
        int position = -1;
        for (int i = 0; i < flatGroupAndLayerRoles.size(); ++i) {
            if (flatGroupAndLayerRoles[i].first == 0
                && flatGroupAndLayerRoles[i].second == fromIndex) {
                position = i;
                break;
            }
        }
        int toIndex = 0;
        for (int i = position - 1; i >= 0; --i) {
            if (flatGroupAndLayerRoles[i].first == 0) {
                toIndex = flatGroupAndLayerRoles[i].second + 1;
                break;
            }
        }
        if (fromIndex < toIndex) {
            --toIndex;
        }

        Vector<int> childIds;
        for (int childIndex : model.childLayerIndices(fromIndex)) {
            childIds.append(model.layerIdAt(childIndex));
        }

        if (position >= 0 && fromIndex != toIndex && model.moveLayer(fromIndex, toIndex)) {
            model.remapFillSourceLayersAfterMove(fromIndex, toIndex);
            landedOn = toIndex;

            int anchorId = movedColumnId;
            for (int childId : childIds) {
                const int anchorIndex = model.layerIndexForId(anchorId);
                const int childIndex = model.layerIndexForId(childId);
                if (anchorIndex < 0 || childIndex < 0) {
                    continue;
                }
                const int target = childIndex < anchorIndex ? anchorIndex : anchorIndex + 1;
                if (childIndex != target && model.moveLayer(childIndex, target)) {
                    model.remapFillSourceLayersAfterMove(childIndex, target);
                }
                anchorId = childId;
            }
            landedOn = model.layerIndexForId(movedColumnId);
        }
    }

    model.setLayerTree(capturedTree);
    commitHistory(String("Reorder Layers"), key);
    const SelectionAttention currentAttention = attentionForView(key);
    updateAttention(key,
                    AttentionChange::LayerChange,
                    currentAttention.frame,
                    landedOn >= 0 ? landedOn : currentAttention.layer,
                    currentAttention.asset);
    return landedOn;
}

int AnimeanBusiness::addLayer(const String &viewName)
{
    const String key = resolveViewName(viewName);
    int layerIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->addLayer) {
            layerIndex = adapter->addLayer();
        }
    }
    if (layerIndex < 0 && (!adapterFor(key) || !adapterFor(key)->addLayer)) {
        AnimeSceneModel &model = modelForView(key);
        layerIndex = model.addLayer();
        if (layerIndex >= 0) {
            commitHistory(String("Add Layer"), key);
        }
    }
    if (layerIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, current.asset);
    }
    return layerIndex;
}

bool AnimeanBusiness::deleteLayer(int layerIndex, const String &viewName)
{
    const String key = resolveViewName(viewName);
    bool removed = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->deleteLayer) {
            removed = adapter->deleteLayer(layerIndex);
        }
    }
    if (!removed && (!adapterFor(key) || !adapterFor(key)->deleteLayer)) {
        AnimeSceneModel &model = modelForView(key);
        if (model.deleteLayer(layerIndex)) {
            model.remapFillSourceLayersAfterDelete(layerIndex);
            commitHistory(String("Delete Layer"), key);
            notifyHeadlessLayerChangedIfNeeded(key);
            removed = true;
        }
    }
    if (removed) {
        const int count = layerCount(key);
        const int nextLayer = layerIndex < count ? layerIndex : count - 1;
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, nextLayer, current.asset);
    }
    return removed;
}

int AnimeanBusiness::deleteLayerGroup(int groupId, const String &viewName)
{
    const String key = resolveViewName(viewName);
    int deleted = 0;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->deleteLayerGroup) {
            deleted = adapter->deleteLayerGroup(groupId);
        }
    }
    if (deleted <= 0 && (!adapterFor(key) || !adapterFor(key)->deleteLayerGroup)) {
        AnimeSceneModel &model = modelForView(key);
        deleted = model.deleteLayerGroup(groupId);
        if (deleted > 0) {
            commitHistory(deleted == 1 ? String("Delete Group")
                                       : String("Delete Group (%1 layers)").arg(deleted),
                          key);
            notifyHeadlessLayerChangedIfNeeded(key);
        }
    }
    if (deleted > 0) {
        const SelectionAttention current = attentionForView(key);
        const int next = std::min(current.layer, layerCount(key) - 1);
        updateAttention(key, AttentionChange::LayerChange, current.frame, next, current.asset);
    }
    return deleted;
}

bool AnimeanBusiness::dissolveLayerGroup(int groupId, const String &viewName)
{
    const String key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);
    if (!model.dissolveLayerGroup(groupId)) {
        return false;
    }
    commitHistory(String("Ungroup Layers"), key);
    requestClearTimelineThumbnails();
    timelineChanged();
    layersChanged(String("main"), attentionForView(String("main")).layer);
    layersChanged(String("child"), attentionForView(String("child")).layer);
    const String assetTarget = assetPanelTargetViewName();
    assetsChanged(assetTarget, attentionForView(assetTarget).asset);
    return true;
}

bool AnimeanBusiness::moveLayer(int fromIndex, int toIndex, const String &viewName)
{
    const String key = resolveViewName(viewName);
    bool moved = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->moveLayer) {
            moved = adapter->moveLayer(fromIndex, toIndex);
        }
    }
    if (!moved && (!adapterFor(key) || !adapterFor(key)->moveLayer)) {
        AnimeSceneModel &model = modelForView(key);
        if (model.moveLayer(fromIndex, toIndex)) {
            model.remapFillSourceLayersAfterMove(fromIndex, toIndex);
            commitHistory(String("Move Layer"), key);
            notifyHeadlessLayerChangedIfNeeded(key);
            moved = true;
        }
    }
    if (moved) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, toIndex, current.asset);
    }
    return moved;
}

void AnimeanBusiness::setLayerGroupCollapsed(int groupId, bool collapsed, const String &viewName)
{
    const String key = resolveViewName(viewName);
    if (groupId > 0) {
        modelForView(key).setLayerGroupCollapsed(groupId, collapsed);
    }
}

bool AnimeanBusiness::setLayerVisible(int layerIndex, bool visible, const String &viewName)
{
    const String key = resolveViewName(viewName);
    bool handledByPython = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->sendLayerVisibilityMessage) {
            handledByPython = adapter->sendLayerVisibilityMessage(layerIndex, visible);
        } else {
            handledByPython = dispatchHeadlessLayerVisibilityHook(key, layerIndex, visible);
        }
    } else {
        handledByPython = dispatchHeadlessLayerVisibilityHook(key, layerIndex, visible);
    }
    if (!handledByPython) {
        AnimeSceneModel &model = modelForView(key);
        if (layerIndex < 0 || layerIndex >= model.layerCount()) {
            return false;
        }
        model.setLayerVisible(layerIndex, visible);
        requestClearTimelineThumbnails();
        if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
            if (adapter->requestUpdate) {
                adapter->requestUpdate();
            }
        }
        layersChanged(key, attentionForView(key).layer);
    }
    return true;
}

void AnimeanBusiness::selectLayer(int layerIndex, const String &viewName)
{
    const String key = resolveViewName(viewName);
    if (layerIndex < 0) {
        return;
    }
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, current.asset);
}

void AnimeanBusiness::unselectLayer(const String &viewName)
{
    const String key = resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::LayerChange, current.frame, -1, -1);
}

String AnimeanBusiness::toolControlName(Tool tool)
{
    switch (tool) {
    case Tool::Pen:
        return String("pen");
    case Tool::Eraser:
        return String("eraser");
    case Tool::DeleteLine:
        return String("delete_line");
    case Tool::CutLine:
        return String("cut_line");
    case Tool::Fill:
        return String("fill");
    case Tool::Arrow:
        return String("arrow");
    case Tool::Connect:
        return String("connect");
    case Tool::Transfer:
        return String("transfer");
    }
    return String("pen");
}

String AnimeanBusiness::toolLockName(Tool tool)
{
    switch (tool) {
    case Tool::DeleteLine:
    case Tool::CutLine:
        return String("eraser");
    default:
        return toolControlName(tool);
    }
}

Vector<AnimeanBusiness::Tool> AnimeanBusiness::lockableTools()
{
    return {
        Tool::Arrow,
        Tool::Pen,
        Tool::Eraser,
        Tool::Fill,
        Tool::Transfer,
        Tool::Connect,
    };
}

int AnimeanBusiness::currentTool() const
{
    if (const AnimePaintViewAdapter *adapter = adapterFor(m_activeViewName)) {
        if (adapter->currentTool) {
            return adapter->currentTool();
        }
    }
    return m_currentTool;
}

AnimeanBusiness::Tool AnimeanBusiness::currentToolEnum() const
{
    return static_cast<Tool>(currentTool());
}

String AnimeanBusiness::activeExtraTool() const
{
    return m_activeExtraTool;
}

String AnimeanBusiness::activeExtraToolProperty() const
{
    return m_activeExtraToolProperty;
}

int AnimeanBusiness::toolSmoothValue() const
{
    return m_toolSmoothValue;
}

int AnimeanBusiness::toolPenWidth() const
{
    return m_toolPenWidth;
}

bool AnimeanBusiness::toolFillAllLayers() const
{
    return m_toolFillAllLayers;
}

AnimeanBusiness::FillScope AnimeanBusiness::toolFillScope() const
{
    return m_toolFillAllLayers ? FillScope::AllLayers : FillScope::CurrentLayer;
}

Color AnimeanBusiness::drawingColor() const
{
    return m_drawingColor;
}

AnimeStrokeFitSettings AnimeanBusiness::strokeFitSettings() const
{
    return m_strokeFitSettings;
}

Vector<AnimeExtraToolDefinition> AnimeanBusiness::parseExtraToolsJson(const JsonArray &array)
{
    Vector<AnimeExtraToolDefinition> tools;
    for (const JsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const JsonObject object = value.toObject();
        AnimeExtraToolDefinition tool;
        tool.name = object.value(String("name")).toString();
        tool.title = object.value(String("title")).toString(tool.name);
        tool.property = object.value(String("property")).toString(tool.name);
        tool.handler = object.value(String("handler")).toString();
        tool.baseTool = object.value(String("base_tool")).toString();
        tool.page = object.value(String("page")).toString(String("mapping"));
        if (!tool.name.isEmpty()) {
            tools.append(tool);
        }
    }
    return tools;
}

Vector<AnimeExtraToolDefinition> AnimeanBusiness::loadExtraTools()
{
    m_extraTools.clear();
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("extra_tools")
                                         .attr("tools_json")()
                                         .cast<std::string>();
            JsonParseError parseError;
            const JsonDocument document = JsonDocument::fromJson(json, &parseError);
            if (parseError.error == JsonParseError::NoError && document.isArray()) {
                m_extraTools = parseExtraToolsJson(document.array());
            } else {
                setStatusMessage(String("extra_tools JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            setStatusMessage(String("extra_tools.py error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
    return m_extraTools;
}

Vector<AnimeExtraToolDefinition> AnimeanBusiness::extraTools() const
{
    return m_extraTools;
}

JsonObject AnimeanBusiness::loadBuiltInToolLayout(Tool tool,
                                                   int smoothValue,
                                                   int penWidth,
                                                   FillScope fillScope)
{
    const String path = resolveToolControlPath(fileNameForBuiltInTool(tool));
    if (path.isEmpty()) {
        return JsonObject();
    }

    File file(path);
    if (!file.open(IODevice::ReadOnly | IODevice::Text)) {
        return JsonObject();
    }

    JsonParseError parseError;
    const JsonDocument document = JsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != JsonParseError::NoError || !document.isObject()) {
        return JsonObject();
    }

    return applyRuntimeStateToToolLayout(document.object(), tool, smoothValue, penWidth, fillScope);
}

String AnimeanBusiness::optionsForToolJsonByName(const String &toolName) const
{
    Tool resolvedTool = Tool::Pen;
    for (Tool candidate : {Tool::Pen, Tool::Eraser, Tool::DeleteLine, Tool::CutLine,
                           Tool::Fill, Tool::Arrow, Tool::Connect, Tool::Transfer}) {
        if (toolControlName(candidate) == toolName.trimmed().toLower()) {
            resolvedTool = candidate;
            break;
        }
    }
    return optionsForToolJson(resolvedTool);
}

String AnimeanBusiness::optionsForToolJson(Tool tool) const
{
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            py::dict state;
            state["smooth"] = m_toolSmoothValue;
            state["pen_width"] = m_toolPenWidth;
            state["fill_scope"] = m_toolFillAllLayers ? "all" : "current";
            const std::string json = py::module_::import("toolcontrol")
                                         .attr("options_for_tool_json")(toolControlName(tool).toStdString(), state)
                                         .cast<std::string>();
            if (!json.empty()) {
                return String::fromStdString(json);
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                String("toolcontrol.py error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
    const JsonObject fallback = loadBuiltInToolLayout(tool,
                                                       m_toolSmoothValue,
                                                       m_toolPenWidth,
                                                       toolFillScope());
    if (fallback.isEmpty()) {
        return String("{}");
    }
    return String::fromUtf8(JsonDocument(fallback).toJson(JsonDocument::Compact));
}

JsonObject AnimeanBusiness::toolOptionsLayout(Tool tool) const
{
    JsonObject layout;
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            py::dict state;
            state["smooth"] = m_toolSmoothValue;
            state["pen_width"] = m_toolPenWidth;
            state["fill_scope"] = m_toolFillAllLayers ? "all" : "current";
            const std::string json = py::module_::import("toolcontrol")
                                         .attr("options_for_tool_json")(toolControlName(tool).toStdString(), state)
                                         .cast<std::string>();
            JsonParseError parseError;
            const JsonDocument document = JsonDocument::fromJson(json, &parseError);
            if (parseError.error == JsonParseError::NoError && document.isObject()) {
                layout = document.object();
            } else {
                const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                    String("toolcontrol JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                String("toolcontrol.py error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
    if (layout.isEmpty()) {
        layout = loadBuiltInToolLayout(tool, m_toolSmoothValue, m_toolPenWidth, toolFillScope());
    }
    return layout;
}

String AnimeanBusiness::optionsForExtraToolJson(const String &extraToolName) const
{
    if (extraToolName.isEmpty()) {
        return String("{}");
    }
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            py::dict state;
            state["smooth"] = m_toolSmoothValue;
            state["pen_width"] = m_toolPenWidth;
            state["fill_scope"] = m_toolFillAllLayers ? "all" : "current";
            const std::string json = py::module_::import("toolcontrol")
                                         .attr("options_for_extra_tool_json")(extraToolName.toStdString(), state)
                                         .cast<std::string>();
            if (!json.empty()) {
                return String::fromStdString(json);
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                String("toolcontrol.py error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
    return String("{}");
}

JsonObject AnimeanBusiness::extraToolOptionsLayout(const String &extraToolName) const
{
    JsonObject extraLayout;
    if (extraToolName.isEmpty()) {
        return extraLayout;
    }
    const String json = optionsForExtraToolJson(extraToolName);
    JsonParseError parseError;
    const JsonDocument document = JsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error == JsonParseError::NoError && document.isObject()) {
        extraLayout = document.object();
    }
    return extraLayout;
}

bool AnimeanBusiness::isToolLocked(int tool) const
{
    const Tool value = static_cast<Tool>(tool);
    if (value == Tool::Arrow) {
        return false;
    }
    const Set<String> locked = m_lockedToolsByView.value(m_activeViewName);
    return locked.contains(toolLockName(value));
}

Set<String> AnimeanBusiness::lockedToolsForView(const String &viewName) const
{
    return m_lockedToolsByView.value(resolveViewName(viewName));
}

void AnimeanBusiness::applyLockedTools(const String &viewName, const StringList &tools)
{
    Set<String> locked;
    for (const String &name : tools) {
        const String trimmed = name.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            locked.insert(trimmed);
        }
    }
    m_lockedToolsByView.insert(resolveViewName(viewName), locked);
    refreshToolLockState();
}

void AnimeanBusiness::refreshToolLockState()
{
    toolLockStateChanged();
    if (isToolLocked(currentTool())) {
        applyTool(int(Tool::Arrow), true);
    }
}

bool AnimeanBusiness::applyTool(int tool, bool reloadOptions)
{
    const Tool value = static_cast<Tool>(tool);
    if (value != Tool::Arrow && isToolLocked(tool)) {
        toolChanged(currentTool(), m_activeExtraTool);
        return false;
    }

    m_currentTool = tool;
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setTool) {
                adapter->setTool(tool);
            } else {
                setHeadlessViewTool(viewKey, tool);
            }
            if (adapter->setStrokeProperty) {
                adapter->setStrokeProperty(String());
            } else {
                setHeadlessViewStrokeProperty(viewKey, String());
            }
        } else {
            setHeadlessViewTool(viewKey, tool);
            setHeadlessViewStrokeProperty(viewKey, String());
        }
    }

    m_activeExtraTool.clear();
    m_activeExtraToolProperty.clear();
    toolChanged(m_currentTool, m_activeExtraTool);
    if (reloadOptions) {
        refreshToolOptions();
    }
    return true;
}

void AnimeanBusiness::selectTool(int tool)
{
    applyTool(tool, true);
}

void AnimeanBusiness::selectExtraTool(const AnimeExtraToolDefinition &tool)
{
    static const Hash<String, Tool> baseTools = {
        {String("fill"), Tool::Fill},
        {String("arrow"), Tool::Arrow},
        {String("transfer"), Tool::Transfer},
    };
    const Tool baseTool = baseTools.value(tool.baseTool, Tool::Pen);
    m_currentTool = int(baseTool);

    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setTool) {
                adapter->setTool(int(baseTool));
            } else {
                setHeadlessViewTool(viewKey, int(baseTool));
            }
            if (adapter->setStrokeProperty) {
                adapter->setStrokeProperty(tool.property);
            } else {
                setHeadlessViewStrokeProperty(viewKey, tool.property);
            }
        } else {
            setHeadlessViewTool(viewKey, int(baseTool));
            setHeadlessViewStrokeProperty(viewKey, tool.property);
        }
    }

    if (AnimePaintViewAdapter *activeAdapter = adapterFor(m_activeViewName)) {
        if (activeAdapter->sendExtraToolMessage) {
            activeAdapter->sendExtraToolMessage(tool.name, tool.property);
        } else {
            dispatchHeadlessExtraToolHook(m_activeViewName, tool.name, tool.property);
        }
    } else {
        dispatchHeadlessExtraToolHook(m_activeViewName, tool.name, tool.property);
    }

    m_activeExtraTool = tool.name;
    m_activeExtraToolProperty = tool.property;
    toolChanged(m_currentTool, m_activeExtraTool);
    refreshExtraToolOptions();

#ifdef ANIMEAN_WITH_PYTHON
    if (!tool.handler.isEmpty() && Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            py::module_::import("extra_tools")
                .attr("run_tool_handler")(tool.handler.toStdString(),
                                          tool.name.toStdString(),
                                          tool.property.toStdString());
        } catch (const py::error_already_set &error) {
            setStatusMessage(String("extra tool error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
}

bool AnimeanBusiness::selectExtraToolByName(const String &extraToolName)
{
    for (const AnimeExtraToolDefinition &tool : m_extraTools) {
        if (tool.name == extraToolName) {
            selectExtraTool(tool);
            return true;
        }
    }
    AnimeExtraToolDefinition fallback;
    fallback.name = extraToolName;
    fallback.title = extraToolName;
    fallback.property = extraToolName;
    selectExtraTool(fallback);
    return true;
}

void AnimeanBusiness::selectColor(const Color &color)
{
    const bool onFill = currentToolEnum() == Tool::Fill;
    applyTool(int(onFill ? Tool::Fill : Tool::Pen), false);
    setDrawingColor(color);
}

void AnimeanBusiness::setDrawingColor(const Color &color)
{
    m_drawingColor = color;
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setDrawingColor) {
                adapter->setDrawingColor(color);
            }
        }
    }
    drawingColorChanged(m_drawingColor);
}

void AnimeanBusiness::setFillScope(int scope)
{
    setFillAllLayers(static_cast<FillScope>(scope) == FillScope::AllLayers);
}

void AnimeanBusiness::setFillAllLayers(bool allLayers)
{
    m_toolFillAllLayers = allLayers;
    const int scopeValue = int(toolFillScope());
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setFillScope) {
                adapter->setFillScope(scopeValue);
            }
        }
    }
    toolOptionsStateChanged();
}

void AnimeanBusiness::selectEraserMode(int tool)
{
    applyTool(tool, false);
}

void AnimeanBusiness::setSmoothValue(int value)
{
    m_toolSmoothValue = std::max(0, std::min(100, value));
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setSmoothValue) {
                adapter->setSmoothValue(m_toolSmoothValue);
            }
        }
    }
    toolOptionsStateChanged();
}

void AnimeanBusiness::setPenWidth(int width)
{
    m_toolPenWidth = std::max(1, std::min(50, width));
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setPenWidth) {
                adapter->setPenWidth(m_toolPenWidth);
            }
        }
    }
    toolOptionsStateChanged();
}

void AnimeanBusiness::setDrawSettings(int stabilizer, int simplify, int corner)
{
    m_toolSmoothValue = std::max(0, std::min(100, stabilizer));
    m_strokeFitSettings.simplify = std::max(0, std::min(100, simplify));
    m_strokeFitSettings.corner = std::max(0, std::min(100, corner));
    for (const String &viewKey : {String("main"), String("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setSmoothValue) {
                adapter->setSmoothValue(m_toolSmoothValue);
            }
            if (adapter->setStrokeFitSettings) {
                adapter->setStrokeFitSettings(m_strokeFitSettings);
            }
        }
    }
    toolOptionsStateChanged();
    refreshToolOptions();
}

void AnimeanBusiness::sendToolOption(const String &hook,
                                     const String &name,
                                     const String &type,
                                     const JsonVariant &value,
                                     int row,
                                     int startColumn,
                                     int endColumn)
{
    bool dispatchedByAdapter = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(m_activeViewName)) {
        if (adapter->sendToolOptionMessage) {
            adapter->sendToolOptionMessage(hook, name, type, value, row, startColumn, endColumn);
            dispatchedByAdapter = true;
        }
    }
    if (!dispatchedByAdapter) {
        dispatchHeadlessToolOptionHook(m_activeViewName, hook, name, type, value, row, startColumn, endColumn);
    }

    if (hook == String("color")) {
        const Color color(value.toString());
        if (color.isValid()) {
            selectColor(color);
        }
    } else if (hook == String("fill_scope")) {
        const bool allLayers = (value.toString() == String("all")
                                || value.toInt() == int(FillScope::AllLayers));
        setFillScope(int(allLayers ? FillScope::AllLayers : FillScope::CurrentLayer));
    } else if (hook == String("eraser_mode")) {
        const String mode = value.toString();
        Tool eraserTool = Tool::Eraser;
        if (mode == String("line") || value.toInt() == int(Tool::DeleteLine)) {
            eraserTool = Tool::DeleteLine;
        } else if (mode == String("cut") || value.toInt() == int(Tool::CutLine)) {
            eraserTool = Tool::CutLine;
        }
        selectEraserMode(int(eraserTool));
    } else if (hook == String("smooth")) {
        setSmoothValue(value.toInt());
    } else if (hook == String("pen_width")) {
        setPenWidth(value.toInt());
    }
}

void AnimeanBusiness::sendPadEvent(const String &pad, const String &phase, double x, double y)
{
    if (AnimePaintViewAdapter *adapter = adapterFor(m_activeViewName)) {
        if (adapter->sendPadMessage) {
            adapter->sendPadMessage(pad, phase, x, y);
            return;
        }
    }
    dispatchHeadlessPadHook(m_activeViewName, pad, phase, x, y);
}

void AnimeanBusiness::refreshToolOptions()
{
    if (!m_activeExtraTool.isEmpty()) {
        refreshExtraToolOptions();
        return;
    }
    const Tool tool = static_cast<Tool>(m_currentTool);
    const JsonObject layout = toolOptionsLayout(tool);
    toolOptionsLayoutChanged(layout, m_currentTool, false);
}

void AnimeanBusiness::refreshExtraToolOptions()
{
    if (m_activeExtraTool.isEmpty()) {
        return;
    }
    const JsonObject extraLayout = extraToolOptionsLayout(m_activeExtraTool);
    toolOptionsLayoutChanged(extraLayout, m_currentTool, true);
}

int AnimeanBusiness::assetCount(const String &viewName) const
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).assetCount();
}

int AnimeanBusiness::currentAsset(const String &viewName) const
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return attentionForView(key).asset;
}

String AnimeanBusiness::assetName(int assetIndex, const String &viewName) const
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).assetName(assetIndex);
}

StringList AnimeanBusiness::visibleAssetNames(const String &viewName) const
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    StringList names;
    for (int i = 0; i < model.assetCount(); ++i) {
        if (model.assetInternal(i)) {
            continue;
        }
        names.append(model.assetName(i));
    }
    return names;
}

int AnimeanBusiness::addAsset(AnimeColumnType type, const String &name, const String &viewName)
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    int assetIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->addAsset) {
            assetIndex = adapter->addAsset(type, name);
        }
    }
    if (assetIndex < 0 && (!adapterFor(key) || !adapterFor(key)->addAsset)) {
        AnimeSceneModel &model = modelForView(key);
        assetIndex = model.addAsset(type, name);
        if (assetIndex >= 0) {
            commitHistory(String("Add Asset"), key);
        }
    }
    if (assetIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::AssetChange, current.frame, current.layer, assetIndex);
    }
    return assetIndex;
}

void AnimeanBusiness::selectAsset(int assetIndex, const String &viewName)
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::AssetChange, current.frame, current.layer, assetIndex);
}

void AnimeanBusiness::unselectAsset(const String &viewName)
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::AssetChange, current.frame, -1, -1);
}

int AnimeanBusiness::addLayerForAsset(int assetIndex, const String &viewName)
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    int layerIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->addLayerForAsset) {
            layerIndex = adapter->addLayerForAsset(assetIndex);
        }
    }
    if (layerIndex < 0 && (!adapterFor(key) || !adapterFor(key)->addLayerForAsset)) {
        AnimeSceneModel &model = modelForView(key);
        layerIndex = model.addLayerForAsset(model.currentFrame(), assetIndex);
        if (layerIndex >= 0) {
            commitHistory(String("Add Layer for Asset"), key);
        }
    }
    if (layerIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, assetIndex);
    }
    return layerIndex;
}

bool AnimeanBusiness::assignAssetToLayer(int layerIndex, int assetIndex, const String &viewName)
{
    const String key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    bool assigned = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->assignAssetToLayer) {
            assigned = adapter->assignAssetToLayer(layerIndex, assetIndex);
        }
    }
    if (!assigned && (!adapterFor(key) || !adapterFor(key)->assignAssetToLayer)) {
        AnimeSceneModel &model = modelForView(key);
        assigned = model.assignAssetToLayer(model.currentFrame(), layerIndex, assetIndex);
        if (assigned) {
            commitHistory(String("Assign Asset"), key);
        }
    }
    if (assigned) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, assetIndex);
    }
    return assigned;
}

String AnimeanBusiness::undoTargetViewName() const
{
    String target;
    std::uint64_t best = 0;
    for (const String &viewKey : {String("main"), String("child")}) {
        const SceneHistory &history = historyForView(viewKey);
        if (!history.canUndo()) {
            continue;
        }
        if (target.isEmpty() || history.currentSeq() > best) {
            target = viewKey;
            best = history.currentSeq();
        }
    }
    return target;
}

String AnimeanBusiness::redoTargetViewName() const
{
    String target;
    std::uint64_t best = 0;
    for (const String &viewKey : {String("main"), String("child")}) {
        const SceneHistory &history = historyForView(viewKey);
        if (!history.canRedo()) {
            continue;
        }
        if (target.isEmpty() || history.redoSeq() < best) {
            target = viewKey;
            best = history.redoSeq();
        }
    }
    return target;
}

bool AnimeanBusiness::canUndo() const
{
    return !undoTargetViewName().isEmpty();
}

bool AnimeanBusiness::canRedo() const
{
    return !redoTargetViewName().isEmpty();
}

StringList AnimeanBusiness::historyLabels(const String &viewName) const
{
    const SceneHistory &history = historyForView(viewName.isEmpty() ? m_activeViewName : viewName);
    StringList labels;
    labels.reserve(history.count());
    for (int i = 0; i < history.count(); ++i) {
        labels.append(history.labelAt(i));
    }
    return labels;
}

int AnimeanBusiness::currentHistoryIndex(const String &viewName) const
{
    const SceneHistory &history = historyForView(viewName.isEmpty() ? m_activeViewName : viewName);
    return history.currentIndex();
}

void AnimeanBusiness::commitHistory(const String &label, const String &viewName)
{
    const String key = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
    const String resolvedLabel = label.isEmpty() ? String("Edit") : label;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->commitHistory) {
            adapter->commitHistory(resolvedLabel);
            handleViewHistoryCommitted(key);
            requestClearTimelineThumbnails();
            return;
        }
    }
    historyForView(key).commit(resolvedLabel, modelForView(key));
    handleViewHistoryCommitted(key);
    requestClearTimelineThumbnails();
    historyChanged();
}

void AnimeanBusiness::resetHistory(const String &label, const String &viewName)
{
    const String key = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->resetHistory) {
            adapter->resetHistory(label);
            requestClearTimelineThumbnails();
            historyChanged();
            return;
        }
    }
    historyForView(key).reset(label, modelForView(key));
    notifyHeadlessHistoryRestore(key);
    requestClearTimelineThumbnails();
    historyChanged();
}

void AnimeanBusiness::handleViewHistoryCommitted(const String &viewName)
{
    const String committedKey = resolveViewName(viewName);
    for (const String &otherKey : {String("main"), String("child")}) {
        if (otherKey == committedKey) {
            continue;
        }
        if (AnimePaintViewAdapter *adapter = adapterFor(otherKey)) {
            if (adapter->dropRedoTail) {
                adapter->dropRedoTail();
                continue;
            }
        }
        historyForView(otherKey).truncateRedo();
    }
    requestClearTimelineThumbnails();
    historyChanged();
}

void AnimeanBusiness::applyHistoryRestore(const String &viewName)
{
    const String key = resolveViewName(viewName);
    stopPlayback();

    AnimeSceneModel &model = modelForView(key);
    if (key == String("child")) {
        model.setTextId(String("child_paint_view"));
        model.setIntId(2);
    } else {
        model.setTextId(String("main_paint_view"));
        model.setIntId(1);
    }

#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&model);
#endif

    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->modelReplaced) {
            adapter->modelReplaced();
        }
    }

    // Chronological undo/redo may land on the board the user is not looking at:
    // the main board behind the Texture page as much as a parked texture board.
    if (key == String("child")) {
        requestSurfaceTextureView();
    } else {
        requestSurfaceMainView();
    }
    setActiveViewName(key);

    updateAttention(key,
                    AttentionChange::LayerChange,
                    model.currentFrame(),
                    model.currentLayer(),
                    model.currentAsset());
    requestClearTimelineThumbnails();
    timelineChanged();
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->requestUpdate) {
            adapter->requestUpdate();
        }
    }
    historyRestored(key);
    historyChanged();
}

bool AnimeanBusiness::undo(const String &viewName)
{
    const String target = viewName.isEmpty() ? undoTargetViewName() : resolveViewName(viewName);
    if (target.isEmpty()) {
        return false;
    }
    SceneHistory &history = historyForView(target);
    if (!history.canUndo()) {
        return false;
    }
    const String undoneLabel = history.labelAt(history.currentIndex());
    bool ok = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(target)) {
        if (adapter->undoHistory) {
            ok = adapter->undoHistory();
        }
    }
    if (!ok && (!adapterFor(target) || !adapterFor(target)->undoHistory)) {
        ok = history.undo(&modelForView(target));
        if (ok) {
            notifyHeadlessHistoryRestore(target);
        }
    }
    if (ok) {
        applyHistoryRestore(target);
        setStatusMessage(String("Undo in %1: %2").arg(target, undoneLabel));
    }
    return ok;
}

bool AnimeanBusiness::redo(const String &viewName)
{
    const String target = viewName.isEmpty() ? redoTargetViewName() : resolveViewName(viewName);
    if (target.isEmpty()) {
        return false;
    }
    SceneHistory &history = historyForView(target);
    if (!history.canRedo()) {
        return false;
    }
    bool ok = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(target)) {
        if (adapter->redoHistory) {
            ok = adapter->redoHistory();
        }
    }
    if (!ok && (!adapterFor(target) || !adapterFor(target)->redoHistory)) {
        ok = history.redo(&modelForView(target));
        if (ok) {
            notifyHeadlessHistoryRestore(target);
        }
    }
    if (ok) {
        applyHistoryRestore(target);
        setStatusMessage(String("Redo in %1: %2")
                             .arg(target, history.labelAt(history.currentIndex())));
    }
    return ok;
}

bool AnimeanBusiness::goToHistory(int row, const String &viewName)
{
    const String target = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
    if (row < 0) {
        return false;
    }
    bool ok = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(target)) {
        if (adapter->goToHistory) {
            ok = adapter->goToHistory(row);
        }
    }
    if (!ok && (!adapterFor(target) || !adapterFor(target)->goToHistory)) {
        ok = historyForView(target).goTo(row, &modelForView(target));
        if (ok) {
            notifyHeadlessHistoryRestore(target);
        }
    }
    if (ok) {
        applyHistoryRestore(target);
    } else {
        historyChanged();
    }
    return ok;
}

int AnimeanBusiness::frameCount(const String &viewName) const
{
    const String key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).frameCount();
}

int AnimeanBusiness::currentFrame(const String &viewName) const
{
    const String key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    return attentionForView(key).frame;
}

int AnimeanBusiness::playbackFps(const String &viewName) const
{
    const String key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).playbackFps();
}

bool AnimeanBusiness::isPlaybackActive() const
{
    return m_playbackTimer && m_playbackTimer->isActive();
}

int AnimeanBusiness::playbackIndex() const
{
    return m_playbackIndex;
}

String AnimeanBusiness::playbackViewName() const
{
    return m_playbackViewName;
}

bool AnimeanBusiness::isPlaybackLoop() const
{
    return m_playbackLoop;
}

bool AnimeanBusiness::isOnionEnabled() const
{
    return m_onionEnabled;
}

bool AnimeanBusiness::isOnionLines() const
{
    return m_onionLines;
}

bool AnimeanBusiness::isOnionFills() const
{
    return m_onionFills;
}

bool AnimeanBusiness::isOnionAmLayers() const
{
    return m_onionAmLayers;
}

bool AnimeanBusiness::isOnionGuideLines() const
{
    return m_onionGuideLines;
}

Set<int> AnimeanBusiness::onionFrames() const
{
    return m_onionFrames;
}

StringList AnimeanBusiness::onionExcludeProperties() const
{
    return m_onionExcludeProperties;
}

String AnimeanBusiness::onionLayerTag() const
{
    return m_onionLayerTag;
}

AnimeTimelineState AnimeanBusiness::timelineState() const
{
    AnimeTimelineState state;
    state.viewName = framePanelTargetViewName();
    const AnimeSceneModel &model = modelForView(state.viewName);
    state.frameCount = model.frameCount();
    state.currentFrame = (isPlaybackActive() && m_playbackViewName == state.viewName)
                             ? m_playbackIndex
                             : attentionForView(state.viewName).frame;
    state.holds.reserve(state.frameCount);
    state.frameNames.reserve(state.frameCount);
    for (int i = 0; i < state.frameCount; ++i) {
        state.holds.append(model.isHoldFrame(i));
        state.frameNames.append(model.frameName(i));
    }
    state.fps = model.playbackFps();
    state.playbackActive = isPlaybackActive();
    state.loop = m_playbackLoop;
    state.onionAvailable = (state.viewName == String("main"));
    state.onionEnabled = m_onionEnabled;
    state.onionLines = m_onionLines;
    state.onionFills = m_onionFills;
    state.onionAmLayers = m_onionAmLayers;
    state.onionGuideLines = m_onionGuideLines;
    state.onionFrames = m_onionFrames;
    return state;
}

void AnimeanBusiness::selectFrame(int frameIndex)
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
}

void AnimeanBusiness::stepFrame(int delta)
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    const int frame = std::min(std::max(0, current.frame + delta),
                               std::max(0, frameCount(key) - 1));
    updateAttention(key, AttentionChange::FrameChange, frame, current.layer, current.asset);
}

void AnimeanBusiness::previousFrame()
{
    stepFrame(-1);
}

void AnimeanBusiness::nextFrame()
{
    stepFrame(1);
}

int AnimeanBusiness::addFrame()
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    int frameIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->addFrame) {
            frameIndex = adapter->addFrame();
        }
    }
    if (frameIndex < 0 && (!adapterFor(key) || !adapterFor(key)->addFrame)) {
        AnimeSceneModel &model = modelForView(key);
        frameIndex = model.addFrame();
        if (frameIndex >= 0) {
            commitHistory(String("Add Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        requestClearTimelineThumbnails();
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

int AnimeanBusiness::addHoldFrame()
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    int frameIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->insertHoldFrameAfter) {
            frameIndex = adapter->insertHoldFrameAfter(current.frame);
        }
    }
    if (frameIndex < 0 && (!adapterFor(key) || !adapterFor(key)->insertHoldFrameAfter)) {
        AnimeSceneModel &model = modelForView(key);
        frameIndex = model.insertHoldFrameAfter(current.frame);
        if (frameIndex >= 0) {
            commitHistory(String("Add Hold Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        requestClearTimelineThumbnails();
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

int AnimeanBusiness::duplicateFrame()
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    int frameIndex = -1;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->duplicateFrame) {
            frameIndex = adapter->duplicateFrame(current.frame);
        }
    }
    if (frameIndex < 0 && (!adapterFor(key) || !adapterFor(key)->duplicateFrame)) {
        AnimeSceneModel &model = modelForView(key);
        frameIndex = model.duplicateFrame(current.frame);
        if (frameIndex >= 0) {
            commitHistory(String("Duplicate Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        requestClearTimelineThumbnails();
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

bool AnimeanBusiness::deleteFrame()
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    const int row = current.frame;
    bool deleted = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->deleteFrame) {
            deleted = adapter->deleteFrame(row);
        }
    }
    if (!deleted && (!adapterFor(key) || !adapterFor(key)->deleteFrame)) {
        AnimeSceneModel &model = modelForView(key);
        if (model.deleteFrame(row)) {
            commitHistory(String("Delete Frame"), key);
            deleted = true;
        }
    }
    if (deleted) {
        requestClearTimelineThumbnails();
        const int count = frameCount(key);
        const int nextFrame = row < count ? row : count - 1;
        updateAttention(key, AttentionChange::FrameChange, nextFrame, current.layer, current.asset);
    }
    return deleted;
}

bool AnimeanBusiness::moveFrame(int fromIndex, int toIndex)
{
    stopPlayback();
    const String key = framePanelTargetViewName();
    requestClearTimelineThumbnails();
    bool moved = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->moveFrame) {
            moved = adapter->moveFrame(fromIndex, toIndex);
        }
    }
    if (!moved && (!adapterFor(key) || !adapterFor(key)->moveFrame)) {
        AnimeSceneModel &model = modelForView(key);
        if (model.moveFrame(fromIndex, toIndex)) {
            commitHistory(String("Move Frame"), key);
            moved = true;
        }
    }
    const AnimeSceneModel &model = modelForView(key);
    if (!moved) {
        updateAttention(key,
                        AttentionChange::FrameChange,
                        model.currentFrame(),
                        model.currentLayer(),
                        model.currentAsset());
        return false;
    }
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::FrameChange, toIndex, current.layer, current.asset);
    return true;
}

void AnimeanBusiness::setFps(int fps)
{
    const String key = framePanelTargetViewName();
    AnimeSceneModel &model = modelForView(key);
    const int current = model.playbackFps();
    if (fps != current) {
        model.setPlaybackFps(fps);
        commitHistory(String("Playback Rate"), key);
    }
    if (m_playbackTimer && m_playbackTimer->isActive()) {
        m_playbackTimer->setInterval(1000 / std::max(1, model.playbackFps()));
    }
    timelineChanged();
    setStatusMessage(String("Playback: %1 fps").arg(model.playbackFps()));
}

void AnimeanBusiness::startPlayback()
{
    if (m_playbackTimer->isActive()) {
        return;
    }

    const String key = framePanelTargetViewName();
    const int totalFrames = frameCount(key);
    if (totalFrames < 2) {
        setStatusMessage(String("Playback needs at least two frames."));
        return;
    }

    setStatusMessage(String("Prerendering %1 frames...").arg(totalFrames));
    String error;
    bool prerendered = true;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->buildPlaybackCache) {
            prerendered = adapter->buildPlaybackCache(totalFrames, &error);
        }
    }
    if (!prerendered) {
        setStatusMessage(String("Playback unavailable: %1").arg(error));
        return;
    }

    m_playbackViewName = key;
    m_playbackFrameCount = totalFrames;
    m_playbackIndex = std::min(std::max(attentionForView(key).frame, 0), totalFrames - 1);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->showPlaybackFrame) {
            adapter->showPlaybackFrame(m_playbackIndex);
        }
    }
    const int fps = std::max(1, modelForView(key).playbackFps());
    m_playbackTimer->setInterval(1000 / fps);
    m_playbackTimer->start();

    playbackStateChanged(true, m_playbackIndex, m_playbackViewName);
    setStatusMessage(String("Playing %1 frames of %2 at %3 fps (prerendered)")
                         .arg(totalFrames)
                         .arg(key)
                         .arg(1000 / m_playbackTimer->interval()));
}

void AnimeanBusiness::advancePlaybackFrame()
{
    if (m_playbackViewName.isEmpty() || m_playbackFrameCount <= 0) {
        return;
    }

    if (!m_playbackLoop && m_playbackIndex >= m_playbackFrameCount - 1) {
        stopPlayback();
        return;
    }
    m_playbackIndex = (m_playbackIndex + 1) % m_playbackFrameCount;
    if (AnimePaintViewAdapter *adapter = adapterFor(m_playbackViewName)) {
        if (adapter->showPlaybackFrame) {
            adapter->showPlaybackFrame(m_playbackIndex);
        }
    }
    playbackStateChanged(true, m_playbackIndex, m_playbackViewName);
}

void AnimeanBusiness::stopPlayback()
{
    if (m_playbackViewName.isEmpty()) {
        return;
    }

    m_playbackTimer->stop();
    const String viewKey = m_playbackViewName;
    const int pausedFrame = m_playbackIndex;
    m_playbackViewName.clear();
    m_playbackFrameCount = 0;

    if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
        if (adapter->endPlayback) {
            adapter->endPlayback();
        }
    }
    playbackStateChanged(false, pausedFrame, viewKey);

    const SelectionAttention current = attentionForView(viewKey);
    updateAttention(viewKey, AttentionChange::FrameChange, pausedFrame, current.layer, current.asset);
    setStatusMessage(String("Paused at frame %1 (vector view)").arg(pausedFrame + 1));
}

void AnimeanBusiness::setPlaybackLoop(bool loop)
{
    if (m_playbackLoop == loop) {
        return;
    }
    m_playbackLoop = loop;
    timelineChanged();
}

void AnimeanBusiness::setOnionEnabled(bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    m_onionEnabled = on;
    if (on && m_onionFrames.isEmpty()) {
        const int frame = attentionForView(String("main")).frame;
        if (frame > 0) {
            m_onionFrames.insert(frame - 1);
        }
        if (frame + 1 < frameCount(String("main"))) {
            m_onionFrames.insert(frame + 1);
        }
        if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
            if (adapter->setOnionFrames) {
                adapter->setOnionFrames(m_onionFrames);
            }
        }
    }
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionEnabled) {
            adapter->setOnionEnabled(on);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::setOnionLines(bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    m_onionLines = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionLines) {
            adapter->setOnionLines(on);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::setOnionFills(bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    m_onionFills = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionFills) {
            adapter->setOnionFills(on);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::setOnionAmLayers(bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    m_onionAmLayers = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionAmLayers) {
            adapter->setOnionAmLayers(on);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::setOnionGuideLines(bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    m_onionGuideLines = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionGuideLines) {
            adapter->setOnionGuideLines(on);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::toggleOnionFrame(int frame, bool on)
{
    if (framePanelTargetViewName() != String("main")) {
        return;
    }
    if (on) {
        m_onionFrames.insert(frame);
    } else {
        m_onionFrames.remove(frame);
    }
    if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
        if (adapter->setOnionFrames) {
            adapter->setOnionFrames(m_onionFrames);
        }
    }
    onionStateChanged();
    timelineChanged();
}

void AnimeanBusiness::pullOnionPolicy()
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized()) {
        return;
    }
    try {
        py::gil_scoped_acquire acquire;
        const std::string json = py::module_::import("python_hooks")
                                     .attr("onion_guide_properties_json")()
                                     .cast<std::string>();
        const JsonDocument document = JsonDocument::fromJson(json);
        StringList properties;
        for (const JsonValue &value : document.array()) {
            const String property = value.toString();
            if (!property.isEmpty()) {
                properties.append(property);
            }
        }
        m_onionExcludeProperties = properties;
        if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
            if (adapter->setOnionExcludeProperties) {
                adapter->setOnionExcludeProperties(properties);
            }
        }
    } catch (const py::error_already_set &error) {
        setStatusMessage(String("onion guide properties error: %1")
                             .arg(String::fromUtf8(error.what())));
    }
    try {
        py::gil_scoped_acquire acquire;
        const std::string json = py::module_::import("python_hooks")
                                     .attr("onion_layer_tag_json")()
                                     .cast<std::string>();
        const JsonArray array =
            JsonDocument::fromJson("[" + json + "]").array();
        m_onionLayerTag = array.isEmpty() ? String() : array.at(0).toString();
        if (AnimePaintViewAdapter *adapter = adapterFor(String("main"))) {
            if (adapter->setOnionLayerTag) {
                adapter->setOnionLayerTag(m_onionLayerTag);
            }
        }
    } catch (const py::error_already_set &error) {
        setStatusMessage(String("onion layer tag error: %1")
                             .arg(String::fromUtf8(error.what())));
    }
#endif
}

void AnimeanBusiness::syncEmbeddedPythonState()
{
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            py::dict globals = py::module_::import("__main__").attr("__dict__");
            py::object animeanPython = py::module_::import("animean_python");
            py::object animeModel = py::module_::import("animemodel");
            AnimeSceneModel &activeModel = modelForView(m_activeViewName);
            AnimeSceneModel &mainModel = modelForView(String("main"));
            AnimeSceneModel &childModel = modelForView(String("child"));
            globals["animean_python"] = animeanPython;
            globals["animemodel"] = animeModel;
            globals["ui"] = animeModel.attr("ui");
            globals["model"] = py::cast(&activeModel, py::return_value_policy::reference);
            globals["main_model"] = py::cast(&mainModel, py::return_value_policy::reference);
            globals["child_model"] = py::cast(&childModel, py::return_value_policy::reference);
            globals["active_view"] = m_activeViewName.toStdString();
            globals["current"] = animeModel.attr("current");
            globals["model_pybind"] = animeanPython.attr("model_pybind");
            globals["vectorlogic"] = animeanPython.attr("vectorlogic");
            globals["canvas_width"] = activeModel.canvasSize().width();
            globals["canvas_height"] = activeModel.canvasSize().height();
            globals["main_canvas_width"] = mainModel.canvasSize().width();
            globals["main_canvas_height"] = mainModel.canvasSize().height();
            globals["child_canvas_width"] = childModel.canvasSize().width();
            globals["child_canvas_height"] = childModel.canvasSize().height();
        } catch (const py::error_already_set &error) {
            pythonDebugMessage(String("[python register] error: %1").arg(String::fromUtf8(error.what())));
            setStatusMessage(String("Python state sync error: %1").arg(String::fromUtf8(error.what())));
        }
    }
#endif
    pythonStateSyncRequested();
}

String AnimeanBusiness::resolvePythonScriptPath(const String &scriptName)
{
    // A name with a folder in it is taken as given. A bare name tries the source
    // folders first, so on the development machine a script edited in the repo
    // wins over the copy beside the exe and over the working directory (the exe
    // folder when the shell starts from Explorer). A release folder has no
    // source tree and uses its own copies.
    const FileInfo directInfo(scriptName);
    const bool bareName = directInfo.fileName() == scriptName;
    if (!bareName && directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }

    const String appPath = CoreApplication::applicationDirPath();
    const StringList candidates = {
#ifdef ANIMEAN_PYFILE_DIR
        Dir(String(ANIMEAN_PYFILE_DIR)).filePath(scriptName),
#endif
#ifdef ANIMEAN_PYTHONBIND_DIR
        Dir(String(ANIMEAN_PYTHONBIND_DIR)).filePath(scriptName),
#endif
#ifdef ANIMEAN_SOURCE_DIR
        Dir(String(ANIMEAN_SOURCE_DIR)).filePath(scriptName),
#endif
        directInfo.absoluteFilePath(),
        Dir(appPath).filePath(scriptName),
        Dir(appPath).filePath(String("pyfile/%1").arg(scriptName)),
        Dir(appPath).filePath(String("../pyfile/%1").arg(scriptName)),
        Dir(appPath).filePath(String("../../pyfile/%1").arg(scriptName)),
    };

    for (const String &candidate : candidates) {
        if (FileInfo::exists(candidate)) {
            return FileInfo(candidate).absoluteFilePath();
        }
    }

    return String();
}

String AnimeanBusiness::runEmbeddedPythonCommand(const String &command, const String &scriptDir)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized()) {
        return String("Python disabled");
    }
    try {
        py::gil_scoped_acquire acquire;
        py::module_ sys = py::module_::import("sys");
        py::list sysPath = sys.attr("path");
        StringList searchDirs;
        if (!scriptDir.isEmpty()) {
            searchDirs.append(Dir::cleanPath(scriptDir));
        }
#ifdef ANIMEAN_PYFILE_DIR
        searchDirs.append(Dir::cleanPath(String(ANIMEAN_PYFILE_DIR)));
#endif
#ifdef ANIMEAN_PYTHONBIND_DIR
        searchDirs.append(Dir::cleanPath(String(ANIMEAN_PYTHONBIND_DIR)));
#endif
        for (const String &dir : searchDirs) {
            if (dir.isEmpty() || !FileInfo::exists(dir)) {
                continue;
            }
            const std::string cleanDir = Dir::cleanPath(dir).toStdString();
            if (!sysPath.contains(py::str(cleanDir))) {
                sysPath.insert(0, cleanDir);
            }
        }

        py::dict globals = py::module_::import("__main__").attr("__dict__");
        syncEmbeddedPythonState();

        const String runPrefix = String("run ");
        String scriptName;
        if (command.endsWith(String(".py"), CaseInsensitive)) {
            scriptName = command;
        } else if (command.startsWith(runPrefix, CaseInsensitive)) {
            scriptName = command.mid(runPrefix.size()).trimmed();
        }

        const bool runScript = !scriptName.isEmpty();
        const String scriptPath = runScript ? resolvePythonScriptPath(scriptName) : String();
        if (runScript && scriptPath.isEmpty()) {
            return String("Python script not found: %1").arg(scriptName);
        }

        py::object io = py::module_::import("io");
        py::object contextlib = py::module_::import("contextlib");
        py::object stdoutBuffer = io.attr("StringIO")();
        py::object stderrBuffer = io.attr("StringIO")();
        py::object stdoutRedirect = contextlib.attr("redirect_stdout")(stdoutBuffer);
        py::object stderrRedirect = contextlib.attr("redirect_stderr")(stderrBuffer);

        String errorText;
        stdoutRedirect.attr("__enter__")();
        stderrRedirect.attr("__enter__")();
        try {
            if (runScript) {
                globals["__file__"] = scriptPath.toStdString();
                const String normalizedPath = Dir::toNativeSeparators(scriptPath);
                const String scriptLiteral = String::fromStdString(
                    py::repr(py::str(normalizedPath.toStdString())).cast<std::string>());
                const String code = String(
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
        AnimeSceneModel &activeModel = modelForView(m_activeViewName);
        updateAttention(m_activeViewName,
                        AttentionChange::AssetChange,
                        activeModel.currentFrame(),
                        activeModel.currentLayer(),
                        activeModel.currentAsset());
        for (const String &viewKey : {String("main"), String("child")}) {
            if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
                if (adapter->requestUpdate) {
                    adapter->requestUpdate();
                }
            }
        }
        return output;
    } catch (const py::error_already_set &error) {
        return String("Python debug error: %1").arg(String::fromUtf8(error.what()));
    }
#else
    (void)(command);
    (void)(scriptDir);
    return String("Python disabled");
#endif
}

String AnimeanBusiness::runPythonInitializationScript(String *outputOut)
{
#ifdef ANIMEAN_WITH_PYTHON
    pythonDebugMessage(String("> initalize.py"));
    const String output = runEmbeddedPythonCommand(String("initalize.py")).trimmed();
    if (outputOut) {
        *outputOut = output;
    }
    pythonDebugMessage(output);
    loadExtraTools();
    pullOnionPolicy();
    return output;
#else
    if (outputOut) {
        *outputOut = String("Python disabled");
    }
    return String("Python disabled");
#endif
}

void AnimeanBusiness::setHeadlessViewTool(const String &viewName, int tool)
{
    const String key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const Tool oldTool = static_cast<Tool>(state.tool);
    const Tool newTool = static_cast<Tool>(tool);
    if ((oldTool == Tool::Arrow || oldTool == Tool::Connect || oldTool == Tool::Transfer)
        && newTool != oldTool) {
        dispatchHeadlessHandleHook(key, String("cancel"));
    }
    state.tool = tool;
    dispatchHeadlessHandleHook(key, String("arm"));
}

void AnimeanBusiness::setHeadlessViewStrokeProperty(const String &viewName, const String &property)
{
    const String key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const bool changed = state.strokeProperty != property;
    state.strokeProperty = property;
    state.activePythonTool = property.isEmpty() ? String() : String("extra");
    if (changed) {
        dispatchHeadlessHandleHook(key, String("arm"));
    }
}

void AnimeanBusiness::notifyHeadlessFrameChangedIfNeeded(const String &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    HeadlessViewState &state = m_headlessViewState[key];
    const int frame = model.currentFrame();
    if (frame == state.pythonNotifiedFrame) {
        return;
    }
    state.pythonNotifiedFrame = frame;
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("framechange"))) {
        return;
    }
    const int layer = model.currentLayer();
    const AnimeCell cell = model.cellAt(frame, layer);
    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frame;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "framechange";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
#endif
}

void AnimeanBusiness::notifyHeadlessLayerChangedIfNeeded(const String &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    HeadlessViewState &state = m_headlessViewState[key];
    const int layer = model.currentLayer();
    const int layerId = layer >= 0 ? model.layerIdAt(layer) : 0;
    if (layer == state.pythonNotifiedLayer && layerId == state.pythonNotifiedLayerId) {
        return;
    }
    const int previous = state.pythonNotifiedLayer;
    state.pythonNotifiedLayer = layer;
    state.pythonNotifiedLayerId = layerId;
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("layerchange"))) {
        return;
    }

    const int frameRow = model.currentFrame();
    const AnimeCell cell = model.cellAt(frameRow, layer);
    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "layerchange";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["layer"] = layer;
    message["layer_id"] = layerId;
    message["previous"] = previous == -2 ? -1 : previous;

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
#endif
}

void AnimeanBusiness::notifyHeadlessHistoryRestore(const String &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    HeadlessViewState &state = m_headlessViewState[key];
    state.pythonNotifiedFrame = model.currentFrame();
    state.pythonNotifiedLayer = model.currentLayer();
    state.pythonNotifiedLayerId =
        state.pythonNotifiedLayer >= 0 ? model.layerIdAt(state.pythonNotifiedLayer) : 0;
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("historyrestore"))) {
        return;
    }

    const int row = model.currentFrame();
    const int layer = model.currentLayer();
    const AnimeCell cell = model.cellAt(row, layer);
    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = row;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "historyrestore";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
#endif
}

void AnimeanBusiness::dispatchHeadlessHandleHook(const String &viewName, const String &phase)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("handle"))) {
        return;
    }
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    const HeadlessViewState &state = m_headlessViewState[key];
    const int frameRow = model.currentFrame();
    const int layer = model.currentLayer();
    const AnimeCell cell = model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    py::dict currentColor;
    currentColor["r"] = m_drawingColor.red();
    currentColor["g"] = m_drawingColor.green();
    currentColor["b"] = m_drawingColor.blue();
    currentColor["a"] = m_drawingColor.alpha();

    py::dict keys;
    keys["shift"] = false;
    keys["ctrl"] = false;
    keys["alt"] = false;
    keys["constrain"] = false;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "handle";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["phase"] = phase.toStdString();
    message["handle"] = "";
    message["color"] = currentColor;
    message["modifiers"] = keys;
    message["zoom"] = 1.0;

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
    (void)(phase);
#endif
}

void AnimeanBusiness::dispatchHeadlessExtraToolHook(const String &viewName,
                                                    const String &name,
                                                    const String &property)
{
#ifdef ANIMEAN_WITH_PYTHON
    const String key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const String previousTool = state.activePythonTool;
    const String previousProperty = state.strokeProperty;
    state.activePythonTool = name.isEmpty() ? String("extra") : name;
    state.strokeProperty = property;

    if (Py_IsInitialized() && animeanHookEventSubscribed(String("extra"))) {
        const AnimeSceneModel &model = modelForView(key);
        const int row = model.currentFrame();
        const int layer = model.currentLayer();
        const AnimeCell cell = model.cellAt(row, layer);
        py::gil_scoped_acquire acquire;
        py::dict cellInfo;
        cellInfo["row"] = row;
        cellInfo["layer"] = layer;
        cellInfo["asset"] = cell.assetIndex;
        cellInfo["frame_id"] = cell.frameId;

        const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
        py::dict message;
        message["event"] = "extra";
        message["view"] = key.toStdString();
        message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
        message["base_tool"] = baseToolName.toStdString();
        message["property"] = state.strokeProperty.toStdString();
        message["cell"] = cellInfo;
        message["stroke"] = py::dict();
        message["position"] = pointToPythonDict(PointF());
        message["delta"] = pointToPythonDict(PointF());

        const String output = pythonHookSendMessage(message);
        if (!isQuietHookOutput(output)) {
            pythonDebugMessage(output);
        }
    }

    state.activePythonTool = previousTool;
    state.strokeProperty = previousProperty;
#else
    (void)(viewName);
    (void)(name);
    (void)(property);
#endif
}

void AnimeanBusiness::dispatchHeadlessToolOptionHook(const String &viewName,
                                                     const String &hook,
                                                     const String &name,
                                                     const String &type,
                                                     const JsonVariant &value,
                                                     int row,
                                                     int startColumn,
                                                     int endColumn)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("option"))) {
        return;
    }
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    const HeadlessViewState &state = m_headlessViewState[key];
    const int frameRow = model.currentFrame();
    const int layer = model.currentLayer();
    const AnimeCell cell = model.cellAt(frameRow, layer);

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

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "option";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
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

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
    (void)(hook);
    (void)(name);
    (void)(type);
    (void)(value);
    (void)(row);
    (void)(startColumn);
    (void)(endColumn);
#endif
}

void AnimeanBusiness::dispatchHeadlessPadHook(const String &viewName,
                                              const String &pad,
                                              const String &phase,
                                              double x,
                                              double y)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("pad"))) {
        return;
    }
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    const HeadlessViewState &state = m_headlessViewState[key];
    const int frameRow = model.currentFrame();
    const int layer = model.currentLayer();
    const AnimeCell cell = model.cellAt(frameRow, layer);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layer;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "pad";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["pad"] = pad.toStdString();
    message["phase"] = phase.toStdString();
    message["value"] = pointToPythonDict(PointF(x, y));

    const String output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        pythonDebugMessage(output);
    }
#else
    (void)(viewName);
    (void)(pad);
    (void)(phase);
    (void)(x);
    (void)(y);
#endif
}

bool AnimeanBusiness::dispatchHeadlessLayerVisibilityHook(const String &viewName,
                                                          int layerIndex,
                                                          bool visible)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(String("visibility"))) {
        return false;
    }
    const String key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    const HeadlessViewState &state = m_headlessViewState[key];
    const int frameRow = model.currentFrame();
    const AnimeCell cell = model.cellAt(frameRow, layerIndex);

    py::gil_scoped_acquire acquire;
    py::dict cellInfo;
    cellInfo["row"] = frameRow;
    cellInfo["layer"] = layerIndex;
    cellInfo["asset"] = cell.assetIndex;
    cellInfo["frame_id"] = cell.frameId;

    const String baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "visibility";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(PointF());
    message["delta"] = pointToPythonDict(PointF());
    message["layer"] = layerIndex;
    message["visible"] = visible;

    const String output = pythonHookSendMessage(message);
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
    (void)(viewName);
    (void)(layerIndex);
    (void)(visible);
    return false;
#endif
}
