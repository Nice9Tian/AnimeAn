// Source: Copied from C:/Users/admin/Documents/VibeFrontend/animean/business/animeanbusiness.cpp
#include "animeanbusiness.h"

#include "projectio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPainter>
#include <QPen>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
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
#include "pythonbind/python_bindings.h"
namespace py = pybind11;
#endif

namespace {

constexpr int kDefaultPlaybackFps = 12;

#ifdef ANIMEAN_WITH_PYTHON
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

bool isQuietHookOutput(const QString &output)
{
    return output.isEmpty() || output.endsWith(QStringLiteral("(no output)"));
}

QString pythonHookSendMessage(const py::dict &message)
{
    if (!Py_IsInitialized()) {
        return QString();
    }
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
#endif

QString fileNameForBuiltInTool(AnimeanBusiness::Tool tool)
{
    switch (tool) {
    case AnimeanBusiness::Tool::Pen:
        return QStringLiteral("pen.json");
    case AnimeanBusiness::Tool::Fill:
        return QStringLiteral("fill.json");
    case AnimeanBusiness::Tool::Eraser:
    case AnimeanBusiness::Tool::DeleteLine:
    case AnimeanBusiness::Tool::CutLine:
        return QStringLiteral("eraser.json");
    case AnimeanBusiness::Tool::Arrow:
        return QStringLiteral("arrow.json");
    case AnimeanBusiness::Tool::Connect:
        return QStringLiteral("connect.json");
    case AnimeanBusiness::Tool::Transfer:
        return QStringLiteral("transfer.json");
    }
    return QStringLiteral("pen.json");
}

QString resolveToolControlPath(const QString &fileName)
{
    const QString appPath = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appPath).filePath(QStringLiteral("childrenpanel/tool_controls/%1").arg(fileName)),
        QDir(appPath).filePath(QStringLiteral("tool_controls/%1").arg(fileName)),
#ifdef ANIMEAN_SOURCE_DIR
        QDir(QStringLiteral(ANIMEAN_SOURCE_DIR)).filePath(QStringLiteral("childrenpanel/tool_controls/%1").arg(fileName)),
#endif
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void setControlValue(QJsonObject *control, const QString &name, const QJsonValue &value)
{
    if (control->value(QStringLiteral("name")).toString() == name) {
        control->insert(QStringLiteral("value"), value);
    }
}

QJsonObject applyRuntimeStateToToolLayout(QJsonObject layout,
                                          AnimeanBusiness::Tool tool,
                                          int smoothValue,
                                          int penWidth,
                                          AnimeanBusiness::FillScope fillScope)
{
    const QString fillScopeValue = fillScope == AnimeanBusiness::FillScope::AllLayers
                                       ? QStringLiteral("all")
                                       : QStringLiteral("current");
    const QString eraserModeValue =
        tool == AnimeanBusiness::Tool::DeleteLine ? QStringLiteral("line")
        : tool == AnimeanBusiness::Tool::CutLine ? QStringLiteral("cut")
                                                 : QStringLiteral("area");

    QJsonArray controls = layout.value(QStringLiteral("controls")).toArray();
    for (int index = 0; index < controls.size(); ++index) {
        if (!controls.at(index).isObject()) {
            continue;
        }

        QJsonObject control = controls.at(index).toObject();
        setControlValue(&control, QStringLiteral("smooth"), smoothValue);
        setControlValue(&control, QStringLiteral("pen_width"), penWidth);
        setControlValue(&control, QStringLiteral("fill_scope"), fillScopeValue);
        setControlValue(&control, QStringLiteral("eraser_mode"), eraserModeValue);
        controls.replace(index, control);
    }

    layout.insert(QStringLiteral("controls"), controls);
    return layout;
}

bool readJsonObjectFromFile(const QString &fileName, QJsonObject *object, QString *errorOut)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to read file:\n%1").arg(file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("File format error:\n%1").arg(parseError.errorString());
        }
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObjectToFile(const QJsonObject &object, const QString &fileName, QString *errorOut)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to write file:\n%1").arg(file.errorString());
        }
        return false;
    }

    const QJsonDocument document(object);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to save file:\n%1").arg(file.errorString());
        }
        return false;
    }

    return true;
}

void renderSceneFrameContent(QPainter &painter,
                             const AnimeSceneModel &model,
                             int frameIndex,
                             qreal scale)
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
            painter.drawImage(image->raster().topLeft, image->raster().image);
        }
        for (const AnimeVectorFillRegion &region : image->fillRegions()) {
            if (region.path.isEmpty()) {
                continue;
            }
            painter.setPen(QPen(region.color,
                                AnimeVectorLogic::kVectorRegionOverpaintWidth,
                                Qt::SolidLine,
                                Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(region.color);
            painter.drawPath(region.path);
        }
        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            const AnimeVectorStroke &stroke = node.stroke;
            if (stroke.path.isEmpty()) {
                continue;
            }
            const Qt::PenStyle penStyle =
                (stroke.penStyle >= Qt::SolidLine && stroke.penStyle <= Qt::DashDotDotLine)
                    ? static_cast<Qt::PenStyle>(stroke.penStyle)
                    : Qt::SolidLine;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(stroke.color,
                                AnimeVectorLogic::displayStrokeWidth(stroke.width, scale),
                                penStyle,
                                Qt::RoundCap,
                                Qt::RoundJoin));
            painter.drawPath(stroke.path);
        }
    }
}

} // namespace

AnimeanBusiness::AnimeanBusiness(QObject *parent)
    : QObject(parent)
    , m_internalMainViewObject(new QObject(this))
    , m_internalChildViewObject(new QObject(this))
    , m_playbackTimer(new QTimer(this))
{
    m_internalMainViewObject->setObjectName(QStringLiteral("main_paint_view_object"));
    m_internalChildViewObject->setObjectName(QStringLiteral("child_paint_view_object"));

    m_internalMainModel.setTextId(QStringLiteral("main_paint_view"));
    m_internalMainModel.setIntId(1);
    m_internalChildModel.setTextId(QStringLiteral("child_paint_view"));
    m_internalChildModel.setIntId(2);

    m_internalMainHistory.reset(QStringLiteral("Initial"), m_internalMainModel);
    m_internalChildHistory.reset(QStringLiteral("Initial"), m_internalChildModel);

    for (const QString &viewName : {QStringLiteral("main"), QStringLiteral("child")}) {
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
    connect(m_playbackTimer, &QTimer::timeout, this, &AnimeanBusiness::advancePlaybackFrame);

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
            emit timelineChanged();
            const int childFrame = modelForView(QStringLiteral("child")).currentFrame();
            if (childFrame != m_lastAutoFitChildFrame) {
                m_lastAutoFitChildFrame = childFrame;
                emit requestAutoFitTextureControlView();
            }
        }
        if (layer) {
            emit layersChanged(QStringLiteral("main"), attentionForView(QStringLiteral("main")).layer);
            emit layersChanged(QStringLiteral("child"), attentionForView(QStringLiteral("child")).layer);
        }
        if (asset) {
            const QString target = assetPanelTargetViewName();
            emit assetsChanged(target, attentionForView(target).asset);
        }
        syncEmbeddedPythonState();
    });
    registerAnimeanUiLockedToolsCallback([this](const QString &view, const QStringList &tools) {
        applyLockedTools(view, tools);
    });
    registerAnimeanUiDrawColorCallback([this](const QColor &color) {
        setDrawingColor(color);
    });
    registerAnimeanUiDrawSettingsCallback([this](int stabilizer, int simplify, int corner) {
        setDrawSettings(stabilizer, simplify, corner);
    });
    registerAnimeanUiHistoryCallback([this](const QString &op, const QString &viewName, const QString &label) {
        if (!viewName.isEmpty()
            && viewName != QStringLiteral("main")
            && viewName != QStringLiteral("child")) {
            return;
        }
        if (op == QStringLiteral("commit")) {
            commitHistory(label.isEmpty() ? QStringLiteral("Python") : label, viewName);
        } else if (op == QStringLiteral("undo")) {
            undo(viewName);
        } else if (op == QStringLiteral("redo")) {
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
}

QString AnimeanBusiness::resolveViewName(const QString &viewName) const
{
    if (viewName == QStringLiteral("child")) {
        return QStringLiteral("child");
    }
    if (viewName == QStringLiteral("main")) {
        return QStringLiteral("main");
    }
    return m_activeViewName == QStringLiteral("child")
               ? QStringLiteral("child")
               : QStringLiteral("main");
}

AnimePaintViewAdapter *AnimeanBusiness::adapterFor(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    auto it = m_adapters.find(key);
    return it == m_adapters.end() ? nullptr : &it.value();
}

const AnimePaintViewAdapter *AnimeanBusiness::adapterFor(const QString &viewName) const
{
    const QString key = resolveViewName(viewName);
    auto it = m_adapters.constFind(key);
    return it == m_adapters.constEnd() ? nullptr : &it.value();
}

void AnimeanBusiness::bindPaintView(const QString &viewName, const AnimePaintViewAdapter &adapter)
{
    const QString key = resolveViewName(viewName);
#ifdef ANIMEAN_WITH_PYTHON
    if (const AnimePaintViewAdapter *oldAdapter = adapterFor(key)) {
        if (oldAdapter->model) {
            unregisterAnimeanUiScene(oldAdapter->model);
        }
    }
    unregisterAnimeanUiScene(key == QStringLiteral("child") ? &m_internalChildModel
                                                            : &m_internalMainModel);
#endif
    m_adapters.insert(key, adapter);

    AnimeSceneModel &model = modelForView(key);
    model.setTextId(key == QStringLiteral("child")
                        ? QStringLiteral("child_paint_view")
                        : QStringLiteral("main_paint_view"));
    model.setIntId(key == QStringLiteral("child") ? 2 : 1);

#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&modelForView(QStringLiteral("child")));
    registerAnimeanUiScene(&modelForView(QStringLiteral("main")));
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif

    SelectionAttention &attention = m_attentionByView[key];
    attention.frame = model.currentFrame();
    attention.layer = model.currentLayer();
    attention.asset = model.currentAsset();
    constrainAttention(model, &attention, AttentionChange::FrameChange);

    if (key == QStringLiteral("main")) {
        emit mainPaintViewObjectChanged(mainPaintViewObject());
    } else if (key == QStringLiteral("child")) {
        emit childPaintViewObjectChanged(childPaintViewObject());
    }
    if (key == m_activeViewName) {
        emit activePaintViewObjectChanged(activePaintViewObject());
    }
}

void AnimeanBusiness::unbindPaintView(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
#ifdef ANIMEAN_WITH_PYTHON
    if (const AnimePaintViewAdapter *oldAdapter = adapterFor(key)) {
        if (oldAdapter->model) {
            unregisterAnimeanUiScene(oldAdapter->model);
        }
    }
#endif
    m_adapters.remove(key);
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&modelForView(QStringLiteral("child")));
    registerAnimeanUiScene(&modelForView(QStringLiteral("main")));
    registerAnimeanUiScene(&modelForView(m_activeViewName));
#endif
    if (key == QStringLiteral("main")) {
        emit mainPaintViewObjectChanged(mainPaintViewObject());
    } else if (key == QStringLiteral("child")) {
        emit childPaintViewObjectChanged(childPaintViewObject());
    }
    if (key == m_activeViewName) {
        emit activePaintViewObjectChanged(activePaintViewObject());
    }
}

QObject *AnimeanBusiness::paintViewObject(const QString &viewName) const
{
    const QString key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->viewObject) {
            return adapter->viewObject;
        }
    }
    return key == QStringLiteral("child") ? m_internalChildViewObject : m_internalMainViewObject;
}

QObject *AnimeanBusiness::mainPaintViewObject() const
{
    return paintViewObject(QStringLiteral("main"));
}

QObject *AnimeanBusiness::childPaintViewObject() const
{
    return paintViewObject(QStringLiteral("child"));
}

QObject *AnimeanBusiness::activePaintViewObject() const
{
    return paintViewObject(m_activeViewName);
}

QString AnimeanBusiness::activeViewName() const
{
    return m_activeViewName;
}

QString AnimeanBusiness::framePanelTargetViewName() const
{
    if (m_activeViewName == QStringLiteral("child") && !m_changeableTimeline) {
        return QStringLiteral("main");
    }
    return m_activeViewName;
}

QString AnimeanBusiness::layerPanelTargetViewName() const
{
    if (m_activeViewName == QStringLiteral("child") && !m_changeableTexture) {
        return QStringLiteral("main");
    }
    return m_activeViewName;
}

QString AnimeanBusiness::assetPanelTargetViewName() const
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
    emit requestClearTimelineThumbnails();
    emit timelineChanged();
}

void AnimeanBusiness::setChangeableTexture(bool changeable)
{
    if (m_changeableTexture == changeable) {
        return;
    }
    m_changeableTexture = changeable;
    const QString target = assetPanelTargetViewName();
    emit assetsChanged(target, attentionForView(target).asset);
}

void AnimeanBusiness::setActiveViewName(const QString &viewName)
{
    const QString resolved = resolveViewName(viewName);
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

    emit activeViewChanged(m_activeViewName);
    emit activePaintViewObjectChanged(activePaintViewObject());
    emit requestClearTimelineThumbnails();
    emit timelineChanged();
    emit layersChanged(QStringLiteral("main"), attentionForView(QStringLiteral("main")).layer);
    emit layersChanged(QStringLiteral("child"), attentionForView(QStringLiteral("child")).layer);
    const QString assetTarget = assetPanelTargetViewName();
    emit assetsChanged(assetTarget, attentionForView(assetTarget).asset);
    emit historyChanged();
    syncEmbeddedPythonState();
    refreshToolLockState();
    setStatusMessage(QStringLiteral("Active paint view: %1").arg(m_activeViewName));
}

AnimeSceneModel &AnimeanBusiness::modelForView(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->model) {
            return *adapter->model;
        }
    }
    return key == QStringLiteral("child") ? m_internalChildModel : m_internalMainModel;
}

const AnimeSceneModel &AnimeanBusiness::modelForView(const QString &viewName) const
{
    const QString key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->model) {
            return *adapter->model;
        }
    }
    return key == QStringLiteral("child") ? m_internalChildModel : m_internalMainModel;
}

SceneHistory &AnimeanBusiness::historyForView(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->history) {
            return *adapter->history;
        }
    }
    return key == QStringLiteral("child") ? m_internalChildHistory : m_internalMainHistory;
}

const SceneHistory &AnimeanBusiness::historyForView(const QString &viewName) const
{
    const QString key = resolveViewName(viewName);
    if (const AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->history) {
            return *adapter->history;
        }
    }
    return key == QStringLiteral("child") ? m_internalChildHistory : m_internalMainHistory;
}

SelectionAttention &AnimeanBusiness::attentionForView(const QString &viewName)
{
    return m_attentionByView[resolveViewName(viewName)];
}

SelectionAttention AnimeanBusiness::attentionForView(const QString &viewName) const
{
    return m_attentionByView.value(resolveViewName(viewName));
}

void AnimeanBusiness::syncAllViewAttentionsFromModels()
{
    for (const QString &viewName : {QStringLiteral("main"), QStringLiteral("child")}) {
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

void AnimeanBusiness::updateAttention(const QString &viewName,
                                      AttentionChange change,
                                      int frame,
                                      int layer,
                                      int asset)
{
    const QString key = resolveViewName(viewName);
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
        emit timelineChanged();
    }
    if (update.frame && key == QStringLiteral("child")) {
        emit requestAutoFitTextureControlView();
    }
    if (update.layer) {
        emit layersChanged(key, attention.layer);
    }
    if (update.asset && key == assetPanelTargetViewName()) {
        emit assetsChanged(key, attention.asset);
    }
    emit attentionUpdated(key, update.frame, update.layer, update.asset);
    syncEmbeddedPythonState();
}

QImage AnimeanBusiness::renderFrameThumbnail(int frameIndex,
                                             const QSize &size,
                                             const QString &viewName)
{
    const QString key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->renderFrameThumbnail) {
            return adapter->renderFrameThumbnail(frameIndex, size);
        }
    }

    if (size.width() <= 0 || size.height() <= 0) {
        return QImage();
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    const AnimeSceneModel &model = modelForView(key);
    if (frameIndex < 0 || frameIndex >= model.frameCount()) {
        return image;
    }

    const QSize canvas = model.canvasSize();
    if (canvas.width() <= 0 || canvas.height() <= 0) {
        return image;
    }
    const qreal scale = std::min(qreal(size.width()) / canvas.width(),
                                 qreal(size.height()) / canvas.height());

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate((size.width() - canvas.width() * scale) * 0.5,
                      (size.height() - canvas.height() * scale) * 0.5);
    painter.scale(scale, scale);
    renderSceneFrameContent(painter, model, frameIndex, scale);
    painter.end();
    return image;
}

QString AnimeanBusiness::statusMessage() const
{
    return m_statusMessage;
}

void AnimeanBusiness::setStatusMessage(const QString &message)
{
    m_statusMessage = message;
    emit statusMessageChanged(m_statusMessage);
}

QString AnimeanBusiness::currentFilePath() const
{
    return m_currentFilePath;
}

void AnimeanBusiness::setCurrentFilePath(const QString &filePath)
{
    if (m_currentFilePath == filePath) {
        return;
    }
    m_currentFilePath = filePath;
    emit projectFilePathChanged(m_currentFilePath);
    emit windowTitleChanged(windowTitle());
}

QString AnimeanBusiness::childFilePath() const
{
    return m_childFilePath;
}

void AnimeanBusiness::setChildFilePath(const QString &filePath)
{
    if (m_childFilePath == filePath) {
        return;
    }
    m_childFilePath = filePath;
    emit childFilePathChanged(m_childFilePath);
    emit windowTitleChanged(windowTitle());
}

QString AnimeanBusiness::windowTitle() const
{
    const QString fileName = m_currentFilePath.isEmpty()
                                 ? QStringLiteral("Untitled")
                                 : QFileInfo(m_currentFilePath).fileName();
    QString title = QStringLiteral("AnimeAn - %1").arg(fileName);
    if (!m_childFilePath.isEmpty()) {
        title += QStringLiteral(" [texture: %1]").arg(QFileInfo(m_childFilePath).fileName());
    }
    return title;
}

QSize AnimeanBusiness::canvasSize(const QString &viewName) const
{
    return modelForView(viewName).canvasSize();
}

QString AnimeanBusiness::ensureProjectFileExtension(const QString &fileName)
{
    return ::ensureProjectFileExtension(fileName);
}

QString AnimeanBusiness::ensureTextureViewFileExtension(const QString &fileName)
{
    return ::ensureTextureViewFileExtension(fileName);
}

QString AnimeanBusiness::projectOpenFilter()
{
    return ::projectOpenFilter();
}

QString AnimeanBusiness::projectSaveFilter()
{
    return ::projectSaveFilter();
}

QString AnimeanBusiness::textureViewOpenFilter()
{
    return ::textureViewOpenFilter();
}

QString AnimeanBusiness::textureViewSaveFilter()
{
    return ::textureViewSaveFilter();
}

void AnimeanBusiness::newProject(const QSize &canvasSize)
{
    applyNewCanvasSize(canvasSize);
}

void AnimeanBusiness::applyNewCanvasSize(const QSize &size)
{
    AnimeSceneModel &mainModel = modelForView(QStringLiteral("main"));
    if (mainModel.canvasSize() == size) {
        setStatusMessage(QStringLiteral("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
        return;
    }

    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setCanvasSize) {
            adapter->setCanvasSize(size);
        } else {
            mainModel.setCanvasSize(size);
        }
    } else {
        mainModel.setCanvasSize(size);
    }

    commitHistory(QStringLiteral("Canvas Size"), QStringLiteral("main"));
    emit canvasSizeChanged(QStringLiteral("main"), size);
    syncEmbeddedPythonState();
    refreshToolOptions();
    setStatusMessage(QStringLiteral("Canvas: %1 x %2 px").arg(size.width()).arg(size.height()));
}

void AnimeanBusiness::installLoadedModel(const QString &viewName, const AnimeSceneModel &model)
{
    const QString key = resolveViewName(viewName);
    const bool isChild = key == QStringLiteral("child");
    AnimeSceneModel &targetModel = modelForView(key);
    targetModel = model;
    targetModel.setTextId(isChild ? QStringLiteral("child_paint_view")
                                  : QStringLiteral("main_paint_view"));
    targetModel.setIntId(isChild ? 2 : 1);
}

void AnimeanBusiness::activateLoadedModel(const QString &viewName, const QString &historyLabel)
{
    const QString key = resolveViewName(viewName);
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
    emit canvasSizeChanged(key, model.canvasSize());
}

void AnimeanBusiness::adoptLoadedModel(const QString &viewName,
                                       const AnimeSceneModel &model,
                                       const QString &historyLabel)
{
    installLoadedModel(viewName, model);
    activateLoadedModel(viewName, historyLabel);
}

bool AnimeanBusiness::saveProjectTo(const QString &fileName, QString *errorOut)
{
    const QString projectFileName = ensureProjectFileExtension(fileName);
    const QJsonObject json = projectToJson(modelForView(QStringLiteral("main")),
                                           modelForView(QStringLiteral("child")));
    if (!writeJsonObjectToFile(json, projectFileName, errorOut)) {
        return false;
    }

    m_currentFilePath = projectFileName;
    emit projectFilePathChanged(m_currentFilePath);
    emit windowTitleChanged(windowTitle());
    setStatusMessage(QStringLiteral("Saved project (main + texture): %1")
                         .arg(QFileInfo(projectFileName).fileName()));
    return true;
}

bool AnimeanBusiness::loadProjectFrom(const QString &fileName, QString *errorOut)
{
    stopPlayback();
    QJsonObject root;
    if (!readJsonObjectFromFile(fileName, &root, errorOut)) {
        return false;
    }

    AnimeSceneModel loadedMainModel;
    AnimeSceneModel loadedTextureModel;
    QString error;
    if (!projectFromJson(root, &loadedMainModel, &loadedTextureModel, &error)) {
        if (errorOut) {
            *errorOut = error.isEmpty() ? QStringLiteral("Unsupported project file.") : error;
        }
        return false;
    }

    installLoadedModel(QStringLiteral("main"), loadedMainModel);
    installLoadedModel(QStringLiteral("child"), loadedTextureModel);
    activateLoadedModel(QStringLiteral("main"), QStringLiteral("Open Project"));
    activateLoadedModel(QStringLiteral("child"), QStringLiteral("Open Project"));

    m_childFilePath.clear();
    m_currentFilePath = fileName;
    emit childFilePathChanged(m_childFilePath);
    emit projectFilePathChanged(m_currentFilePath);
    emit windowTitleChanged(windowTitle());
    setActiveViewName(QStringLiteral("main"));
    syncEmbeddedPythonState();
    refreshToolOptions();
    setStatusMessage(QStringLiteral("Opened project (main + texture): %1")
                         .arg(QFileInfo(fileName).fileName()));
    return true;
}

bool AnimeanBusiness::openTextureViewFrom(const QString &fileName, QString *errorOut)
{
    stopPlayback();
    QJsonObject root;
    if (!readJsonObjectFromFile(fileName, &root, errorOut)) {
        return false;
    }

    AnimeSceneModel loadedModel;
    QString error;
    if (!textureViewFromJson(root, &loadedModel, &error)) {
        if (errorOut) {
            *errorOut = error.isEmpty() ? QStringLiteral("Unsupported texture file.") : error;
        }
        return false;
    }

    adoptLoadedModel(QStringLiteral("child"), loadedModel, QStringLiteral("Open Texture View"));
    m_childFilePath = fileName;
    emit childFilePathChanged(m_childFilePath);
    emit windowTitleChanged(windowTitle());
    emit requestSurfaceTextureView();
    setActiveViewName(QStringLiteral("child"));
    setStatusMessage(QStringLiteral("Opened texture view: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

bool AnimeanBusiness::saveTextureViewTo(const QString &fileName, QString *errorOut)
{
    const QString targetName = ensureTextureViewFileExtension(fileName);
    const QJsonObject json = textureViewToJson(modelForView(QStringLiteral("child")));
    if (!writeJsonObjectToFile(json, targetName, errorOut)) {
        return false;
    }

    m_childFilePath = targetName;
    emit childFilePathChanged(m_childFilePath);
    emit windowTitleChanged(windowTitle());
    setStatusMessage(QStringLiteral("Saved texture board: %1").arg(QFileInfo(targetName).fileName()));
    return true;
}

int AnimeanBusiness::layerCount(const QString &viewName) const
{
    return modelForView(viewName).layerCount();
}

int AnimeanBusiness::currentLayer(const QString &viewName) const
{
    return attentionForView(viewName).layer;
}

QString AnimeanBusiness::layerName(int layerIndex, const QString &viewName) const
{
    return modelForView(viewName).layerName(layerIndex);
}

bool AnimeanBusiness::isLayerVisible(int layerIndex, const QString &viewName) const
{
    return modelForView(viewName).layerVisible(layerIndex);
}

QVector<AnimeLayerPanelRow> AnimeanBusiness::layerRowsForView(const QString &viewName) const
{
    const AnimeSceneModel &model = modelForView(viewName);
    const int frame = model.currentFrame();

    QVector<AnimeLayerPanelRow> rows;
    std::function<int(const QVector<AnimeLayerNode> &, int)> collect =
        [&](const QVector<AnimeLayerNode> &nodes, int depth) {
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
        QHash<int, int> rowForLayer;
        for (int i = 0; i < rows.size(); ++i) {
            if (!rows[i].group) {
                rowForLayer.insert(rows[i].layerIndex, i);
            }
        }
        QVector<int> parentRow(rows.size(), -1);
        QVector<QVector<int>> childRows(rows.size());
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
            QVector<AnimeLayerPanelRow> nestedRows;
            nestedRows.reserve(rows.size());
            QVector<char> emitted(rows.size(), 0);
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
                    row.name = QStringLiteral("└ ") + row.name;
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

int AnimeanBusiness::reorderLayerTree(const QString &viewName,
                                      const QVector<AnimeLayerNode> &capturedTree,
                                      int movedColumnId,
                                      const QVector<QPair<int, int>> &flatGroupAndLayerRoles)
{
    const QString key = resolveViewName(viewName);
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

        QVector<int> childIds;
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
    commitHistory(QStringLiteral("Reorder Layers"), key);
    const SelectionAttention currentAttention = attentionForView(key);
    updateAttention(key,
                    AttentionChange::LayerChange,
                    currentAttention.frame,
                    landedOn >= 0 ? landedOn : currentAttention.layer,
                    currentAttention.asset);
    return landedOn;
}

int AnimeanBusiness::addLayer(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Add Layer"), key);
        }
    }
    if (layerIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, current.asset);
    }
    return layerIndex;
}

bool AnimeanBusiness::deleteLayer(int layerIndex, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Delete Layer"), key);
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

int AnimeanBusiness::deleteLayerGroup(int groupId, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
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
            commitHistory(deleted == 1 ? QStringLiteral("Delete Group")
                                       : QStringLiteral("Delete Group (%1 layers)").arg(deleted),
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

bool AnimeanBusiness::dissolveLayerGroup(int groupId, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    AnimeSceneModel &model = modelForView(key);
    if (!model.dissolveLayerGroup(groupId)) {
        return false;
    }
    commitHistory(QStringLiteral("Ungroup Layers"), key);
    emit requestClearTimelineThumbnails();
    emit timelineChanged();
    emit layersChanged(QStringLiteral("main"), attentionForView(QStringLiteral("main")).layer);
    emit layersChanged(QStringLiteral("child"), attentionForView(QStringLiteral("child")).layer);
    const QString assetTarget = assetPanelTargetViewName();
    emit assetsChanged(assetTarget, attentionForView(assetTarget).asset);
    return true;
}

bool AnimeanBusiness::moveLayer(int fromIndex, int toIndex, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Move Layer"), key);
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

void AnimeanBusiness::setLayerGroupCollapsed(int groupId, bool collapsed, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    if (groupId > 0) {
        modelForView(key).setLayerGroupCollapsed(groupId, collapsed);
    }
}

bool AnimeanBusiness::setLayerVisible(int layerIndex, bool visible, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
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
        if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
            if (adapter->requestUpdate) {
                adapter->requestUpdate();
            }
        }
        emit layersChanged(key, attentionForView(key).layer);
    }
    return true;
}

void AnimeanBusiness::selectLayer(int layerIndex, const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    if (layerIndex < 0) {
        return;
    }
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, current.asset);
}

void AnimeanBusiness::unselectLayer(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::LayerChange, current.frame, -1, -1);
}

QString AnimeanBusiness::toolControlName(Tool tool)
{
    switch (tool) {
    case Tool::Pen:
        return QStringLiteral("pen");
    case Tool::Eraser:
        return QStringLiteral("eraser");
    case Tool::DeleteLine:
        return QStringLiteral("delete_line");
    case Tool::CutLine:
        return QStringLiteral("cut_line");
    case Tool::Fill:
        return QStringLiteral("fill");
    case Tool::Arrow:
        return QStringLiteral("arrow");
    case Tool::Connect:
        return QStringLiteral("connect");
    case Tool::Transfer:
        return QStringLiteral("transfer");
    }
    return QStringLiteral("pen");
}

QString AnimeanBusiness::toolLockName(Tool tool)
{
    switch (tool) {
    case Tool::DeleteLine:
    case Tool::CutLine:
        return QStringLiteral("eraser");
    default:
        return toolControlName(tool);
    }
}

QVector<AnimeanBusiness::Tool> AnimeanBusiness::lockableTools()
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

QString AnimeanBusiness::activeExtraTool() const
{
    return m_activeExtraTool;
}

QString AnimeanBusiness::activeExtraToolProperty() const
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

QColor AnimeanBusiness::drawingColor() const
{
    return m_drawingColor;
}

AnimeStrokeFitSettings AnimeanBusiness::strokeFitSettings() const
{
    return m_strokeFitSettings;
}

QVector<AnimeExtraToolDefinition> AnimeanBusiness::parseExtraToolsJson(const QJsonArray &array)
{
    QVector<AnimeExtraToolDefinition> tools;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        AnimeExtraToolDefinition tool;
        tool.name = object.value(QStringLiteral("name")).toString();
        tool.title = object.value(QStringLiteral("title")).toString(tool.name);
        tool.property = object.value(QStringLiteral("property")).toString(tool.name);
        tool.handler = object.value(QStringLiteral("handler")).toString();
        tool.baseTool = object.value(QStringLiteral("base_tool")).toString();
        tool.page = object.value(QStringLiteral("page")).toString(QStringLiteral("mapping"));
        if (!tool.name.isEmpty()) {
            tools.append(tool);
        }
    }
    return tools;
}

QVector<AnimeExtraToolDefinition> AnimeanBusiness::loadExtraTools()
{
    m_extraTools.clear();
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("extra_tools")
                                         .attr("tools_json")()
                                         .cast<std::string>();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isArray()) {
                m_extraTools = parseExtraToolsJson(document.array());
            } else {
                setStatusMessage(QStringLiteral("extra_tools JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            setStatusMessage(QStringLiteral("extra_tools.py error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
    return m_extraTools;
}

QVector<AnimeExtraToolDefinition> AnimeanBusiness::extraTools() const
{
    return m_extraTools;
}

QJsonObject AnimeanBusiness::loadBuiltInToolLayout(Tool tool,
                                                   int smoothValue,
                                                   int penWidth,
                                                   FillScope fillScope)
{
    const QString path = resolveToolControlPath(fileNameForBuiltInTool(tool));
    if (path.isEmpty()) {
        return QJsonObject();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }

    return applyRuntimeStateToToolLayout(document.object(), tool, smoothValue, penWidth, fillScope);
}

QString AnimeanBusiness::optionsForToolJsonByName(const QString &toolName) const
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

QString AnimeanBusiness::optionsForToolJson(Tool tool) const
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
                return QString::fromStdString(json);
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
    const QJsonObject fallback = loadBuiltInToolLayout(tool,
                                                       m_toolSmoothValue,
                                                       m_toolPenWidth,
                                                       toolFillScope());
    if (fallback.isEmpty()) {
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(QJsonDocument(fallback).toJson(QJsonDocument::Compact));
}

QJsonObject AnimeanBusiness::toolOptionsLayout(Tool tool) const
{
    QJsonObject layout;
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
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                layout = document.object();
            } else {
                const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                    QStringLiteral("toolcontrol JSON error: %1").arg(parseError.errorString()));
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
    if (layout.isEmpty()) {
        layout = loadBuiltInToolLayout(tool, m_toolSmoothValue, m_toolPenWidth, toolFillScope());
    }
    return layout;
}

QString AnimeanBusiness::optionsForExtraToolJson(const QString &extraToolName) const
{
    if (extraToolName.isEmpty()) {
        return QStringLiteral("{}");
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
                return QString::fromStdString(json);
            }
        } catch (const py::error_already_set &error) {
            const_cast<AnimeanBusiness *>(this)->setStatusMessage(
                QStringLiteral("toolcontrol.py error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
    return QStringLiteral("{}");
}

QJsonObject AnimeanBusiness::extraToolOptionsLayout(const QString &extraToolName) const
{
    QJsonObject extraLayout;
    if (extraToolName.isEmpty()) {
        return extraLayout;
    }
    const QString json = optionsForExtraToolJson(extraToolName);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
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
    const QSet<QString> locked = m_lockedToolsByView.value(m_activeViewName);
    return locked.contains(toolLockName(value));
}

QSet<QString> AnimeanBusiness::lockedToolsForView(const QString &viewName) const
{
    return m_lockedToolsByView.value(resolveViewName(viewName));
}

void AnimeanBusiness::applyLockedTools(const QString &viewName, const QStringList &tools)
{
    QSet<QString> locked;
    for (const QString &name : tools) {
        const QString trimmed = name.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            locked.insert(trimmed);
        }
    }
    m_lockedToolsByView.insert(resolveViewName(viewName), locked);
    refreshToolLockState();
}

void AnimeanBusiness::refreshToolLockState()
{
    emit toolLockStateChanged();
    if (isToolLocked(currentTool())) {
        applyTool(int(Tool::Arrow), true);
    }
}

bool AnimeanBusiness::applyTool(int tool, bool reloadOptions)
{
    const Tool value = static_cast<Tool>(tool);
    if (value != Tool::Arrow && isToolLocked(tool)) {
        emit toolChanged(currentTool(), m_activeExtraTool);
        return false;
    }

    m_currentTool = tool;
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setTool) {
                adapter->setTool(tool);
            } else {
                setHeadlessViewTool(viewKey, tool);
            }
            if (adapter->setStrokeProperty) {
                adapter->setStrokeProperty(QString());
            } else {
                setHeadlessViewStrokeProperty(viewKey, QString());
            }
        } else {
            setHeadlessViewTool(viewKey, tool);
            setHeadlessViewStrokeProperty(viewKey, QString());
        }
    }

    m_activeExtraTool.clear();
    m_activeExtraToolProperty.clear();
    emit toolChanged(m_currentTool, m_activeExtraTool);
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
    static const QHash<QString, Tool> baseTools = {
        {QStringLiteral("fill"), Tool::Fill},
        {QStringLiteral("arrow"), Tool::Arrow},
        {QStringLiteral("transfer"), Tool::Transfer},
    };
    const Tool baseTool = baseTools.value(tool.baseTool, Tool::Pen);
    m_currentTool = int(baseTool);

    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
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
    emit toolChanged(m_currentTool, m_activeExtraTool);
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
            setStatusMessage(QStringLiteral("extra tool error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
}

bool AnimeanBusiness::selectExtraToolByName(const QString &extraToolName)
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

void AnimeanBusiness::selectColor(const QColor &color)
{
    const bool onFill = currentToolEnum() == Tool::Fill;
    applyTool(int(onFill ? Tool::Fill : Tool::Pen), false);
    setDrawingColor(color);
}

void AnimeanBusiness::setDrawingColor(const QColor &color)
{
    m_drawingColor = color;
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setDrawingColor) {
                adapter->setDrawingColor(color);
            }
        }
    }
    emit drawingColorChanged(m_drawingColor);
}

void AnimeanBusiness::setFillScope(int scope)
{
    setFillAllLayers(static_cast<FillScope>(scope) == FillScope::AllLayers);
}

void AnimeanBusiness::setFillAllLayers(bool allLayers)
{
    m_toolFillAllLayers = allLayers;
    const int scopeValue = int(toolFillScope());
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setFillScope) {
                adapter->setFillScope(scopeValue);
            }
        }
    }
    emit toolOptionsStateChanged();
}

void AnimeanBusiness::selectEraserMode(int tool)
{
    applyTool(tool, false);
}

void AnimeanBusiness::setSmoothValue(int value)
{
    m_toolSmoothValue = std::max(0, std::min(100, value));
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setSmoothValue) {
                adapter->setSmoothValue(m_toolSmoothValue);
            }
        }
    }
    emit toolOptionsStateChanged();
}

void AnimeanBusiness::setPenWidth(int width)
{
    m_toolPenWidth = std::max(1, std::min(50, width));
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setPenWidth) {
                adapter->setPenWidth(m_toolPenWidth);
            }
        }
    }
    emit toolOptionsStateChanged();
}

void AnimeanBusiness::setDrawSettings(int stabilizer, int simplify, int corner)
{
    m_toolSmoothValue = std::max(0, std::min(100, stabilizer));
    m_strokeFitSettings.simplify = std::max(0, std::min(100, simplify));
    m_strokeFitSettings.corner = std::max(0, std::min(100, corner));
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
        if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
            if (adapter->setSmoothValue) {
                adapter->setSmoothValue(m_toolSmoothValue);
            }
            if (adapter->setStrokeFitSettings) {
                adapter->setStrokeFitSettings(m_strokeFitSettings);
            }
        }
    }
    emit toolOptionsStateChanged();
    refreshToolOptions();
}

void AnimeanBusiness::sendToolOption(const QString &hook,
                                     const QString &name,
                                     const QString &type,
                                     const QVariant &value,
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

    if (hook == QStringLiteral("color")) {
        const QColor color = value.canConvert<QColor>() ? value.value<QColor>() : QColor(value.toString());
        if (color.isValid()) {
            selectColor(color);
        }
    } else if (hook == QStringLiteral("fill_scope")) {
        const bool allLayers = (value.toString() == QStringLiteral("all")
                                || value.toInt() == int(FillScope::AllLayers));
        setFillScope(int(allLayers ? FillScope::AllLayers : FillScope::CurrentLayer));
    } else if (hook == QStringLiteral("eraser_mode")) {
        const QString mode = value.toString();
        Tool eraserTool = Tool::Eraser;
        if (mode == QStringLiteral("line") || value.toInt() == int(Tool::DeleteLine)) {
            eraserTool = Tool::DeleteLine;
        } else if (mode == QStringLiteral("cut") || value.toInt() == int(Tool::CutLine)) {
            eraserTool = Tool::CutLine;
        }
        selectEraserMode(int(eraserTool));
    } else if (hook == QStringLiteral("smooth")) {
        setSmoothValue(value.toInt());
    } else if (hook == QStringLiteral("pen_width")) {
        setPenWidth(value.toInt());
    }
}

void AnimeanBusiness::sendPadEvent(const QString &pad, const QString &phase, double x, double y)
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
    const QJsonObject layout = toolOptionsLayout(tool);
    emit toolOptionsLayoutChanged(layout, m_currentTool, false);
}

void AnimeanBusiness::refreshExtraToolOptions()
{
    if (m_activeExtraTool.isEmpty()) {
        return;
    }
    const QJsonObject extraLayout = extraToolOptionsLayout(m_activeExtraTool);
    emit toolOptionsLayoutChanged(extraLayout, m_currentTool, true);
}

int AnimeanBusiness::assetCount(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).assetCount();
}

int AnimeanBusiness::currentAsset(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return attentionForView(key).asset;
}

QString AnimeanBusiness::assetName(int assetIndex, const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).assetName(assetIndex);
}

QStringList AnimeanBusiness::visibleAssetNames(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    QStringList names;
    for (int i = 0; i < model.assetCount(); ++i) {
        if (model.assetInternal(i)) {
            continue;
        }
        names.append(model.assetName(i));
    }
    return names;
}

int AnimeanBusiness::addAsset(AnimeColumnType type, const QString &name, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Add Asset"), key);
        }
    }
    if (assetIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::AssetChange, current.frame, current.layer, assetIndex);
    }
    return assetIndex;
}

void AnimeanBusiness::selectAsset(int assetIndex, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::AssetChange, current.frame, current.layer, assetIndex);
}

void AnimeanBusiness::unselectAsset(const QString &viewName)
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::AssetChange, current.frame, -1, -1);
}

int AnimeanBusiness::addLayerForAsset(int assetIndex, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Add Layer for Asset"), key);
        }
    }
    if (layerIndex >= 0) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, assetIndex);
    }
    return layerIndex;
}

bool AnimeanBusiness::assignAssetToLayer(int layerIndex, int assetIndex, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? assetPanelTargetViewName() : resolveViewName(viewName);
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
            commitHistory(QStringLiteral("Assign Asset"), key);
        }
    }
    if (assigned) {
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::LayerChange, current.frame, layerIndex, assetIndex);
    }
    return assigned;
}

QString AnimeanBusiness::undoTargetViewName() const
{
    QString target;
    quint64 best = 0;
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
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

QString AnimeanBusiness::redoTargetViewName() const
{
    QString target;
    quint64 best = 0;
    for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
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

QStringList AnimeanBusiness::historyLabels(const QString &viewName) const
{
    const SceneHistory &history = historyForView(viewName.isEmpty() ? m_activeViewName : viewName);
    QStringList labels;
    labels.reserve(history.count());
    for (int i = 0; i < history.count(); ++i) {
        labels.append(history.labelAt(i));
    }
    return labels;
}

int AnimeanBusiness::currentHistoryIndex(const QString &viewName) const
{
    const SceneHistory &history = historyForView(viewName.isEmpty() ? m_activeViewName : viewName);
    return history.currentIndex();
}

void AnimeanBusiness::commitHistory(const QString &label, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
    const QString resolvedLabel = label.isEmpty() ? QStringLiteral("Edit") : label;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->commitHistory) {
            adapter->commitHistory(resolvedLabel);
            handleViewHistoryCommitted(key);
            emit requestClearTimelineThumbnails();
            return;
        }
    }
    historyForView(key).commit(resolvedLabel, modelForView(key));
    handleViewHistoryCommitted(key);
    emit requestClearTimelineThumbnails();
    emit historyChanged();
}

void AnimeanBusiness::resetHistory(const QString &label, const QString &viewName)
{
    const QString key = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->resetHistory) {
            adapter->resetHistory(label);
            emit historyChanged();
            return;
        }
    }
    historyForView(key).reset(label, modelForView(key));
    notifyHeadlessHistoryRestore(key);
    emit requestClearTimelineThumbnails();
    emit historyChanged();
}

void AnimeanBusiness::handleViewHistoryCommitted(const QString &viewName)
{
    const QString committedKey = resolveViewName(viewName);
    for (const QString &otherKey : {QStringLiteral("main"), QStringLiteral("child")}) {
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
    emit historyChanged();
}

void AnimeanBusiness::applyHistoryRestore(const QString &viewName)
{
    const QString key = resolveViewName(viewName);
    stopPlayback();

    AnimeSceneModel &model = modelForView(key);
    if (key == QStringLiteral("child")) {
        model.setTextId(QStringLiteral("child_paint_view"));
        model.setIntId(2);
    } else {
        model.setTextId(QStringLiteral("main_paint_view"));
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

    if (key == QStringLiteral("child")) {
        emit requestSurfaceTextureView();
    }
    setActiveViewName(key);

    updateAttention(key,
                    AttentionChange::LayerChange,
                    model.currentFrame(),
                    model.currentLayer(),
                    model.currentAsset());
    emit requestClearTimelineThumbnails();
    emit timelineChanged();
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->requestUpdate) {
            adapter->requestUpdate();
        }
    }
    emit historyRestored(key);
    emit historyChanged();
}

bool AnimeanBusiness::undo(const QString &viewName)
{
    const QString target = viewName.isEmpty() ? undoTargetViewName() : resolveViewName(viewName);
    if (target.isEmpty()) {
        return false;
    }
    SceneHistory &history = historyForView(target);
    if (!history.canUndo()) {
        return false;
    }
    const QString undoneLabel = history.labelAt(history.currentIndex());
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
        setStatusMessage(QStringLiteral("Undo in %1: %2").arg(target, undoneLabel));
    }
    return ok;
}

bool AnimeanBusiness::redo(const QString &viewName)
{
    const QString target = viewName.isEmpty() ? redoTargetViewName() : resolveViewName(viewName);
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
        setStatusMessage(QStringLiteral("Redo in %1: %2")
                             .arg(target, history.labelAt(history.currentIndex())));
    }
    return ok;
}

bool AnimeanBusiness::goToHistory(int row, const QString &viewName)
{
    const QString target = viewName.isEmpty() ? m_activeViewName : resolveViewName(viewName);
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
        emit historyChanged();
    }
    return ok;
}

int AnimeanBusiness::frameCount(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    return modelForView(key).frameCount();
}

int AnimeanBusiness::currentFrame(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
    return attentionForView(key).frame;
}

int AnimeanBusiness::playbackFps(const QString &viewName) const
{
    const QString key = viewName.isEmpty() ? framePanelTargetViewName() : resolveViewName(viewName);
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

QString AnimeanBusiness::playbackViewName() const
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

QSet<int> AnimeanBusiness::onionFrames() const
{
    return m_onionFrames;
}

QStringList AnimeanBusiness::onionExcludeProperties() const
{
    return m_onionExcludeProperties;
}

QString AnimeanBusiness::onionLayerTag() const
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
    state.onionAvailable = (state.viewName == QStringLiteral("main"));
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
    const QString key = framePanelTargetViewName();
    const SelectionAttention current = attentionForView(key);
    updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
}

void AnimeanBusiness::stepFrame(int delta)
{
    stopPlayback();
    const QString key = framePanelTargetViewName();
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
    const QString key = framePanelTargetViewName();
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
            commitHistory(QStringLiteral("Add Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        emit requestClearTimelineThumbnails();
        const SelectionAttention current = attentionForView(key);
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

int AnimeanBusiness::addHoldFrame()
{
    stopPlayback();
    const QString key = framePanelTargetViewName();
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
            commitHistory(QStringLiteral("Add Hold Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        emit requestClearTimelineThumbnails();
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

int AnimeanBusiness::duplicateFrame()
{
    stopPlayback();
    const QString key = framePanelTargetViewName();
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
            commitHistory(QStringLiteral("Duplicate Frame"), key);
        }
    }
    if (frameIndex >= 0) {
        emit requestClearTimelineThumbnails();
        updateAttention(key, AttentionChange::FrameChange, frameIndex, current.layer, current.asset);
    }
    return frameIndex;
}

bool AnimeanBusiness::deleteFrame()
{
    stopPlayback();
    const QString key = framePanelTargetViewName();
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
            commitHistory(QStringLiteral("Delete Frame"), key);
            deleted = true;
        }
    }
    if (deleted) {
        emit requestClearTimelineThumbnails();
        const int count = frameCount(key);
        const int nextFrame = row < count ? row : count - 1;
        updateAttention(key, AttentionChange::FrameChange, nextFrame, current.layer, current.asset);
    }
    return deleted;
}

bool AnimeanBusiness::moveFrame(int fromIndex, int toIndex)
{
    stopPlayback();
    const QString key = framePanelTargetViewName();
    emit requestClearTimelineThumbnails();
    bool moved = false;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->moveFrame) {
            moved = adapter->moveFrame(fromIndex, toIndex);
        }
    }
    if (!moved && (!adapterFor(key) || !adapterFor(key)->moveFrame)) {
        AnimeSceneModel &model = modelForView(key);
        if (model.moveFrame(fromIndex, toIndex)) {
            commitHistory(QStringLiteral("Move Frame"), key);
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
    const QString key = framePanelTargetViewName();
    AnimeSceneModel &model = modelForView(key);
    const int current = model.playbackFps();
    if (fps != current) {
        model.setPlaybackFps(fps);
        commitHistory(QStringLiteral("Playback Rate"), key);
    }
    if (m_playbackTimer && m_playbackTimer->isActive()) {
        m_playbackTimer->setInterval(1000 / std::max(1, model.playbackFps()));
    }
    emit timelineChanged();
    setStatusMessage(QStringLiteral("Playback: %1 fps").arg(model.playbackFps()));
}

void AnimeanBusiness::startPlayback()
{
    if (m_playbackTimer->isActive()) {
        return;
    }

    const QString key = framePanelTargetViewName();
    const int totalFrames = frameCount(key);
    if (totalFrames < 2) {
        setStatusMessage(QStringLiteral("Playback needs at least two frames."));
        return;
    }

    setStatusMessage(QStringLiteral("Prerendering %1 frames...").arg(totalFrames));
    QString error;
    bool prerendered = true;
    if (AnimePaintViewAdapter *adapter = adapterFor(key)) {
        if (adapter->buildPlaybackCache) {
            prerendered = adapter->buildPlaybackCache(totalFrames, &error);
        }
    }
    if (!prerendered) {
        setStatusMessage(QStringLiteral("Playback unavailable: %1").arg(error));
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

    emit playbackStateChanged(true, m_playbackIndex, m_playbackViewName);
    setStatusMessage(QStringLiteral("Playing %1 frames of %2 at %3 fps (prerendered)")
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
    emit playbackStateChanged(true, m_playbackIndex, m_playbackViewName);
}

void AnimeanBusiness::stopPlayback()
{
    if (m_playbackViewName.isEmpty()) {
        return;
    }

    m_playbackTimer->stop();
    const QString viewKey = m_playbackViewName;
    const int pausedFrame = m_playbackIndex;
    m_playbackViewName.clear();
    m_playbackFrameCount = 0;

    if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
        if (adapter->endPlayback) {
            adapter->endPlayback();
        }
    }
    emit playbackStateChanged(false, pausedFrame, viewKey);

    const SelectionAttention current = attentionForView(viewKey);
    updateAttention(viewKey, AttentionChange::FrameChange, pausedFrame, current.layer, current.asset);
    setStatusMessage(QStringLiteral("Paused at frame %1 (vector view)").arg(pausedFrame + 1));
}

void AnimeanBusiness::setPlaybackLoop(bool loop)
{
    if (m_playbackLoop == loop) {
        return;
    }
    m_playbackLoop = loop;
    emit timelineChanged();
}

void AnimeanBusiness::setOnionEnabled(bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    m_onionEnabled = on;
    if (on && m_onionFrames.isEmpty()) {
        const int frame = attentionForView(QStringLiteral("main")).frame;
        if (frame > 0) {
            m_onionFrames.insert(frame - 1);
        }
        if (frame + 1 < frameCount(QStringLiteral("main"))) {
            m_onionFrames.insert(frame + 1);
        }
        if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
            if (adapter->setOnionFrames) {
                adapter->setOnionFrames(m_onionFrames);
            }
        }
    }
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionEnabled) {
            adapter->setOnionEnabled(on);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
}

void AnimeanBusiness::setOnionLines(bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    m_onionLines = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionLines) {
            adapter->setOnionLines(on);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
}

void AnimeanBusiness::setOnionFills(bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    m_onionFills = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionFills) {
            adapter->setOnionFills(on);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
}

void AnimeanBusiness::setOnionAmLayers(bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    m_onionAmLayers = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionAmLayers) {
            adapter->setOnionAmLayers(on);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
}

void AnimeanBusiness::setOnionGuideLines(bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    m_onionGuideLines = on;
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionGuideLines) {
            adapter->setOnionGuideLines(on);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
}

void AnimeanBusiness::toggleOnionFrame(int frame, bool on)
{
    if (framePanelTargetViewName() != QStringLiteral("main")) {
        return;
    }
    if (on) {
        m_onionFrames.insert(frame);
    } else {
        m_onionFrames.remove(frame);
    }
    if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
        if (adapter->setOnionFrames) {
            adapter->setOnionFrames(m_onionFrames);
        }
    }
    emit onionStateChanged();
    emit timelineChanged();
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
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
        QStringList properties;
        for (const QJsonValue &value : document.array()) {
            const QString property = value.toString();
            if (!property.isEmpty()) {
                properties.append(property);
            }
        }
        m_onionExcludeProperties = properties;
        if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
            if (adapter->setOnionExcludeProperties) {
                adapter->setOnionExcludeProperties(properties);
            }
        }
    } catch (const py::error_already_set &error) {
        setStatusMessage(QStringLiteral("onion guide properties error: %1")
                             .arg(QString::fromUtf8(error.what())));
    }
    try {
        py::gil_scoped_acquire acquire;
        const std::string json = py::module_::import("python_hooks")
                                     .attr("onion_layer_tag_json")()
                                     .cast<std::string>();
        const QJsonArray array =
            QJsonDocument::fromJson("[" + QByteArray::fromStdString(json) + "]").array();
        m_onionLayerTag = array.isEmpty() ? QString() : array.at(0).toString();
        if (AnimePaintViewAdapter *adapter = adapterFor(QStringLiteral("main"))) {
            if (adapter->setOnionLayerTag) {
                adapter->setOnionLayerTag(m_onionLayerTag);
            }
        }
    } catch (const py::error_already_set &error) {
        setStatusMessage(QStringLiteral("onion layer tag error: %1")
                             .arg(QString::fromUtf8(error.what())));
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
            AnimeSceneModel &mainModel = modelForView(QStringLiteral("main"));
            AnimeSceneModel &childModel = modelForView(QStringLiteral("child"));
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
            emit pythonDebugMessage(QStringLiteral("[python register] error: %1").arg(QString::fromUtf8(error.what())));
            setStatusMessage(QStringLiteral("Python state sync error: %1").arg(QString::fromUtf8(error.what())));
        }
    }
#endif
    emit pythonStateSyncRequested();
}

QString AnimeanBusiness::resolvePythonScriptPath(const QString &scriptName)
{
    const QFileInfo directInfo(scriptName);
    if (directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }

    const QString appPath = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appPath).filePath(scriptName),
        QDir(appPath).filePath(QStringLiteral("pyfile/%1").arg(scriptName)),
        QDir(appPath).filePath(QStringLiteral("../pyfile/%1").arg(scriptName)),
        QDir(appPath).filePath(QStringLiteral("../../pyfile/%1").arg(scriptName)),
#ifdef ANIMEAN_PYFILE_DIR
        QDir(QStringLiteral(ANIMEAN_PYFILE_DIR)).filePath(scriptName),
#endif
#ifdef ANIMEAN_PYTHONBIND_DIR
        QDir(QStringLiteral(ANIMEAN_PYTHONBIND_DIR)).filePath(scriptName),
#endif
#ifdef ANIMEAN_SOURCE_DIR
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

QString AnimeanBusiness::runEmbeddedPythonCommand(const QString &command, const QString &scriptDir)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized()) {
        return QStringLiteral("Python disabled");
    }
    try {
        py::gil_scoped_acquire acquire;
        py::module_ sys = py::module_::import("sys");
        py::list sysPath = sys.attr("path");
        QStringList searchDirs;
        if (!scriptDir.isEmpty()) {
            searchDirs.append(QDir::cleanPath(scriptDir));
        }
#ifdef ANIMEAN_PYFILE_DIR
        searchDirs.append(QDir::cleanPath(QStringLiteral(ANIMEAN_PYFILE_DIR)));
#endif
#ifdef ANIMEAN_PYTHONBIND_DIR
        searchDirs.append(QDir::cleanPath(QStringLiteral(ANIMEAN_PYTHONBIND_DIR)));
#endif
        for (const QString &dir : searchDirs) {
            if (dir.isEmpty() || !QFileInfo::exists(dir)) {
                continue;
            }
            const std::string cleanDir = QDir::cleanPath(dir).toStdString();
            if (!sysPath.contains(py::str(cleanDir))) {
                sysPath.insert(0, cleanDir);
            }
        }

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
        AnimeSceneModel &activeModel = modelForView(m_activeViewName);
        updateAttention(m_activeViewName,
                        AttentionChange::AssetChange,
                        activeModel.currentFrame(),
                        activeModel.currentLayer(),
                        activeModel.currentAsset());
        for (const QString &viewKey : {QStringLiteral("main"), QStringLiteral("child")}) {
            if (AnimePaintViewAdapter *adapter = adapterFor(viewKey)) {
                if (adapter->requestUpdate) {
                    adapter->requestUpdate();
                }
            }
        }
        return output;
    } catch (const py::error_already_set &error) {
        return QStringLiteral("Python debug error: %1").arg(QString::fromUtf8(error.what()));
    }
#else
    Q_UNUSED(command);
    Q_UNUSED(scriptDir);
    return QStringLiteral("Python disabled");
#endif
}

QString AnimeanBusiness::runPythonInitializationScript(QString *outputOut)
{
#ifdef ANIMEAN_WITH_PYTHON
    emit pythonDebugMessage(QStringLiteral("> initalize.py"));
    const QString output = runEmbeddedPythonCommand(QStringLiteral("initalize.py")).trimmed();
    if (outputOut) {
        *outputOut = output;
    }
    emit pythonDebugMessage(output);
    loadExtraTools();
    pullOnionPolicy();
    return output;
#else
    if (outputOut) {
        *outputOut = QStringLiteral("Python disabled");
    }
    return QStringLiteral("Python disabled");
#endif
}

void AnimeanBusiness::setHeadlessViewTool(const QString &viewName, int tool)
{
    const QString key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const Tool oldTool = static_cast<Tool>(state.tool);
    const Tool newTool = static_cast<Tool>(tool);
    if ((oldTool == Tool::Arrow || oldTool == Tool::Connect || oldTool == Tool::Transfer)
        && newTool != oldTool) {
        dispatchHeadlessHandleHook(key, QStringLiteral("cancel"));
    }
    state.tool = tool;
    dispatchHeadlessHandleHook(key, QStringLiteral("arm"));
}

void AnimeanBusiness::setHeadlessViewStrokeProperty(const QString &viewName, const QString &property)
{
    const QString key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const bool changed = state.strokeProperty != property;
    state.strokeProperty = property;
    state.activePythonTool = property.isEmpty() ? QString() : QStringLiteral("extra");
    if (changed) {
        dispatchHeadlessHandleHook(key, QStringLiteral("arm"));
    }
}

void AnimeanBusiness::notifyHeadlessFrameChangedIfNeeded(const QString &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const QString key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    HeadlessViewState &state = m_headlessViewState[key];
    const int frame = model.currentFrame();
    if (frame == state.pythonNotifiedFrame) {
        return;
    }
    state.pythonNotifiedFrame = frame;
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("framechange"))) {
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "framechange";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
#endif
}

void AnimeanBusiness::notifyHeadlessLayerChangedIfNeeded(const QString &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const QString key = resolveViewName(viewName);
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
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("layerchange"))) {
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "layerchange";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["layer"] = layer;
    message["layer_id"] = layerId;
    message["previous"] = previous == -2 ? -1 : previous;

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
#endif
}

void AnimeanBusiness::notifyHeadlessHistoryRestore(const QString &viewName)
{
#ifdef ANIMEAN_WITH_PYTHON
    const QString key = resolveViewName(viewName);
    const AnimeSceneModel &model = modelForView(key);
    HeadlessViewState &state = m_headlessViewState[key];
    state.pythonNotifiedFrame = model.currentFrame();
    state.pythonNotifiedLayer = model.currentLayer();
    state.pythonNotifiedLayerId =
        state.pythonNotifiedLayer >= 0 ? model.layerIdAt(state.pythonNotifiedLayer) : 0;
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("historyrestore"))) {
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "historyrestore";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
#endif
}

void AnimeanBusiness::dispatchHeadlessHandleHook(const QString &viewName, const QString &phase)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("handle"))) {
        return;
    }
    const QString key = resolveViewName(viewName);
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "handle";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["phase"] = phase.toStdString();
    message["handle"] = "";
    message["color"] = currentColor;
    message["modifiers"] = keys;
    message["zoom"] = 1.0;

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
    Q_UNUSED(phase);
#endif
}

void AnimeanBusiness::dispatchHeadlessExtraToolHook(const QString &viewName,
                                                    const QString &name,
                                                    const QString &property)
{
#ifdef ANIMEAN_WITH_PYTHON
    const QString key = resolveViewName(viewName);
    HeadlessViewState &state = m_headlessViewState[key];
    const QString previousTool = state.activePythonTool;
    const QString previousProperty = state.strokeProperty;
    state.activePythonTool = name.isEmpty() ? QStringLiteral("extra") : name;
    state.strokeProperty = property;

    if (Py_IsInitialized() && animeanHookEventSubscribed(QStringLiteral("extra"))) {
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

        const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
        py::dict message;
        message["event"] = "extra";
        message["view"] = key.toStdString();
        message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
        message["base_tool"] = baseToolName.toStdString();
        message["property"] = state.strokeProperty.toStdString();
        message["cell"] = cellInfo;
        message["stroke"] = py::dict();
        message["position"] = pointToPythonDict(QPointF());
        message["delta"] = pointToPythonDict(QPointF());

        const QString output = pythonHookSendMessage(message);
        if (!isQuietHookOutput(output)) {
            emit pythonDebugMessage(output);
        }
    }

    state.activePythonTool = previousTool;
    state.strokeProperty = previousProperty;
#else
    Q_UNUSED(viewName);
    Q_UNUSED(name);
    Q_UNUSED(property);
#endif
}

void AnimeanBusiness::dispatchHeadlessToolOptionHook(const QString &viewName,
                                                     const QString &hook,
                                                     const QString &name,
                                                     const QString &type,
                                                     const QVariant &value,
                                                     int row,
                                                     int startColumn,
                                                     int endColumn)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("option"))) {
        return;
    }
    const QString key = resolveViewName(viewName);
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "option";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
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

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
    Q_UNUSED(hook);
    Q_UNUSED(name);
    Q_UNUSED(type);
    Q_UNUSED(value);
    Q_UNUSED(row);
    Q_UNUSED(startColumn);
    Q_UNUSED(endColumn);
#endif
}

void AnimeanBusiness::dispatchHeadlessPadHook(const QString &viewName,
                                              const QString &pad,
                                              const QString &phase,
                                              double x,
                                              double y)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("pad"))) {
        return;
    }
    const QString key = resolveViewName(viewName);
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "pad";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["pad"] = pad.toStdString();
    message["phase"] = phase.toStdString();
    message["value"] = pointToPythonDict(QPointF(x, y));

    const QString output = pythonHookSendMessage(message);
    if (!isQuietHookOutput(output)) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(viewName);
    Q_UNUSED(pad);
    Q_UNUSED(phase);
    Q_UNUSED(x);
    Q_UNUSED(y);
#endif
}

bool AnimeanBusiness::dispatchHeadlessLayerVisibilityHook(const QString &viewName,
                                                          int layerIndex,
                                                          bool visible)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!Py_IsInitialized() || !animeanHookEventSubscribed(QStringLiteral("visibility"))) {
        return false;
    }
    const QString key = resolveViewName(viewName);
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

    const QString baseToolName = toolControlName(static_cast<Tool>(state.tool));
    py::dict message;
    message["event"] = "visibility";
    message["view"] = key.toStdString();
    message["tool"] = (state.activePythonTool.isEmpty() ? baseToolName : state.activePythonTool).toStdString();
    message["base_tool"] = baseToolName.toStdString();
    message["property"] = state.strokeProperty.toStdString();
    message["cell"] = cellInfo;
    message["stroke"] = py::dict();
    message["position"] = pointToPythonDict(QPointF());
    message["delta"] = pointToPythonDict(QPointF());
    message["layer"] = layerIndex;
    message["visible"] = visible;

    const QString output = pythonHookSendMessage(message);
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
    Q_UNUSED(viewName);
    Q_UNUSED(layerIndex);
    Q_UNUSED(visible);
    return false;
#endif
}
