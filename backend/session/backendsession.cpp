#include "session/backendsession.h"

#include "algorithm/beziersplit.h"
#include "board/boardsurface.h"
#include "business/animeanbusiness.h"
#include "clipreader.cpp"
#include "net/wsserver.h"
#include "openglwidget.h"
#include "theme.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineF>
#include <QMetaObject>
#include <QPixmap>
#include <QRectF>
#include <QSettings>
#include <QSize>
#include <QSizeF>
#include <QTimer>
#include <QTransform>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
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

QString cursorImageKey(const QImage &rgba)
{
    quint64 h = 14695981039346656037ULL;
    const auto mix = [&h](const uchar *data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<quint64>(data[i]);
            h *= 1099511628211ULL;
        }
    };
    const int w = rgba.width();
    const int height = rgba.height();
    mix(reinterpret_cast<const uchar *>(&w), sizeof(w));
    mix(reinterpret_cast<const uchar *>(&height), sizeof(height));
    for (int y = 0; y < height; ++y) {
        mix(rgba.constScanLine(y), static_cast<size_t>(w) * 4);
    }
    return QStringLiteral("cur_%1").arg(h, 16, 16, QLatin1Char('0'));
}

void mapQtCursorShape(Qt::CursorShape shape, QString *outName, QString *outCss)
{
    switch (shape) {
    case Qt::ArrowCursor:
        *outName = QStringLiteral("arrow");
        *outCss = QStringLiteral("default");
        break;
    case Qt::CrossCursor:
        *outName = QStringLiteral("cross");
        *outCss = QStringLiteral("crosshair");
        break;
    case Qt::WaitCursor:
    case Qt::BusyCursor:
        *outName = QStringLiteral("wait");
        *outCss = QStringLiteral("wait");
        break;
    case Qt::IBeamCursor:
        *outName = QStringLiteral("ibeam");
        *outCss = QStringLiteral("text");
        break;
    case Qt::SizeVerCursor:
    case Qt::SplitVCursor:
        *outName = QStringLiteral("size_v");
        *outCss = QStringLiteral("ns-resize");
        break;
    case Qt::SizeHorCursor:
    case Qt::SplitHCursor:
        *outName = QStringLiteral("size_h");
        *outCss = QStringLiteral("ew-resize");
        break;
    case Qt::SizeBDiagCursor:
        *outName = QStringLiteral("size_bdiag");
        *outCss = QStringLiteral("nesw-resize");
        break;
    case Qt::SizeFDiagCursor:
        *outName = QStringLiteral("size_fdiag");
        *outCss = QStringLiteral("nwse-resize");
        break;
    case Qt::SizeAllCursor:
        *outName = QStringLiteral("size_all");
        *outCss = QStringLiteral("move");
        break;
    case Qt::BlankCursor:
        *outName = QStringLiteral("blank");
        *outCss = QStringLiteral("none");
        break;
    case Qt::PointingHandCursor:
        *outName = QStringLiteral("pointing_hand");
        *outCss = QStringLiteral("pointer");
        break;
    case Qt::ForbiddenCursor:
        *outName = QStringLiteral("forbidden");
        *outCss = QStringLiteral("not-allowed");
        break;
    case Qt::OpenHandCursor:
        *outName = QStringLiteral("open_hand");
        *outCss = QStringLiteral("grab");
        break;
    case Qt::ClosedHandCursor:
        *outName = QStringLiteral("closed_hand");
        *outCss = QStringLiteral("grabbing");
        break;
    default:
        *outName = QStringLiteral("arrow");
        *outCss = QStringLiteral("default");
        break;
    }
}

AnimeanBusiness::Tool parseToolValue(const QJsonValue &val, bool *okOut = nullptr)
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
    const QString s = val.toString().trimmed().toLower();
    if (s == QStringLiteral("pen")) {
        return AnimeanBusiness::Tool::Pen;
    }
    if (s == QStringLiteral("eraser") || s == QStringLiteral("area")) {
        return AnimeanBusiness::Tool::Eraser;
    }
    if (s == QStringLiteral("delete_line") || s == QStringLiteral("deleteline") || s == QStringLiteral("line")) {
        return AnimeanBusiness::Tool::DeleteLine;
    }
    if (s == QStringLiteral("cut_line") || s == QStringLiteral("cutline") || s == QStringLiteral("cut")) {
        return AnimeanBusiness::Tool::CutLine;
    }
    if (s == QStringLiteral("fill")) {
        return AnimeanBusiness::Tool::Fill;
    }
    if (s == QStringLiteral("arrow")) {
        return AnimeanBusiness::Tool::Arrow;
    }
    if (s == QStringLiteral("connect")) {
        return AnimeanBusiness::Tool::Connect;
    }
    if (s == QStringLiteral("transfer")) {
        return AnimeanBusiness::Tool::Transfer;
    }
    if (okOut) {
        *okOut = false;
    }
    return AnimeanBusiness::Tool::Pen;
}

QString toolEnumDisplayName(AnimeanBusiness::Tool tool)
{
    switch (tool) {
    case AnimeanBusiness::Tool::Pen:
        return QStringLiteral("Pen");
    case AnimeanBusiness::Tool::Eraser:
        return QStringLiteral("Eraser");
    case AnimeanBusiness::Tool::DeleteLine:
        return QStringLiteral("DeleteLine");
    case AnimeanBusiness::Tool::CutLine:
        return QStringLiteral("CutLine");
    case AnimeanBusiness::Tool::Fill:
        return QStringLiteral("Fill");
    case AnimeanBusiness::Tool::Arrow:
        return QStringLiteral("Arrow");
    case AnimeanBusiness::Tool::Connect:
        return QStringLiteral("Connect");
    case AnimeanBusiness::Tool::Transfer:
        return QStringLiteral("Transfer");
    }
    return QStringLiteral("Pen");
}

QColor parseColorParams(const QJsonObject &params)
{
    if (params.contains(QStringLiteral("color"))) {
        const QJsonValue cVal = params.value(QStringLiteral("color"));
        if (cVal.isString()) {
            const QColor c(cVal.toString());
            if (c.isValid()) {
                return c;
            }
        } else if (cVal.isObject()) {
            const QJsonObject cObj = cVal.toObject();
            return QColor(cObj.value(QStringLiteral("r")).toInt(0),
                          cObj.value(QStringLiteral("g")).toInt(0),
                          cObj.value(QStringLiteral("b")).toInt(0),
                          cObj.value(QStringLiteral("a")).toInt(255));
        }
    }
    if (params.contains(QStringLiteral("hex"))) {
        const QColor c(params.value(QStringLiteral("hex")).toString());
        if (c.isValid()) {
            return c;
        }
    }
    if (params.contains(QStringLiteral("r"))
        || params.contains(QStringLiteral("g"))
        || params.contains(QStringLiteral("b"))) {
        return QColor(params.value(QStringLiteral("r")).toInt(0),
                      params.value(QStringLiteral("g")).toInt(0),
                      params.value(QStringLiteral("b")).toInt(0),
                      params.value(QStringLiteral("a")).toInt(255));
    }
    return QColor();
}

QJsonObject makeMenuItem(const QString &id,
                         const QString &text,
                         const QString &shortcut,
                         const QString &kind,
                         bool checked = false,
                         bool enabled = true,
                         const QJsonArray &children = QJsonArray())
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("text"), text},
        {QStringLiteral("shortcut"), shortcut},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("checked"), checked},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("children"), children},
    };
}

const QStringList &allStateKeys()
{
    static const QStringList keys = {
        QStringLiteral("window"),
        QStringLiteral("theme"),
        QStringLiteral("menus"),
        QStringLiteral("tools"),
        QStringLiteral("toolOptions"),
        QStringLiteral("drawColor"),
        QStringLiteral("drawSettings"),
        QStringLiteral("layers/main"),
        QStringLiteral("layers/child"),
        QStringLiteral("assets/main"),
        QStringLiteral("assets/child"),
        QStringLiteral("timeline"),
        QStringLiteral("history"),
        QStringLiteral("windows"),
        QStringLiteral("cursor/main"),
        QStringLiteral("cursor/child"),
        QStringLiteral("viewport/main"),
        QStringLiteral("viewport/child"),
        QStringLiteral("freeze"),
        QStringLiteral("pad"),
    };
    return keys;
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

    QPointF cubic[4];
    AnimeBezierSplit::quadToCubic(p0, p1, p2, cubic);
    sampleCount = std::max(2, sampleCount);
    for (int i = 1; i <= sampleCount; ++i) {
        points->append(AnimeBezierSplit::evaluateCubic(cubic, static_cast<qreal>(i) / sampleCount));
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

QStringList extractPathsParam(const QJsonObject &params)
{
    QStringList out;
    if (params.contains(QStringLiteral("paths"))) {
        const QJsonValue v = params.value(QStringLiteral("paths"));
        if (v.isArray()) {
            for (const QJsonValue &item : v.toArray()) {
                const QString s = item.toString().trimmed();
                if (!s.isEmpty()) {
                    out.append(s);
                }
            }
        } else if (v.isString()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) {
                out.append(s);
            }
        }
    }
    if (out.isEmpty()) {
        const QString single = params.value(QStringLiteral("path"))
                                   .toString(params.value(QStringLiteral("fileName"))
                                                 .toString(params.value(QStringLiteral("filePath")).toString()))
                                   .trimmed();
        if (!single.isEmpty()) {
            out.append(single);
        }
    }
    return out;
}

} // namespace

BackendSession::BackendSession(WsServer *ws, QObject *parent)
    : QObject(parent)
    , m_ws(ws)
    , m_business(new AnimeanBusiness(this))
    , m_mainBoard(std::make_unique<PaintOpenGLWidget>())
    , m_childBoard(std::make_unique<PaintOpenGLWidget>())
    , m_lastEvent(QJsonObject{{QStringLiteral("name"), QString()}, {QStringLiteral("args"), QJsonObject()}})
{
    initWindowInfos();
    loadTimelineSettings();
    m_cursors.insert(QStringLiteral("main"), CursorState());
    m_cursors.insert(QStringLiteral("child"), CursorState());
    m_padValues.insert(QStringLiteral("force_pad"), m_forcePadValue);

    m_mainBoard->setViewName(QStringLiteral("main"));
    m_mainBoard->model().setTextId(QStringLiteral("main_paint_view"));
    m_mainBoard->model().setIntId(1);
    m_mainBoard->setActiveIndicator(true);
    m_mainBoard->resizeSurface(800, 600, 1.0);

    m_childBoard->setViewName(QStringLiteral("child"));
    m_childBoard->setUnboundedCanvas(true);
    m_childBoard->model().setTextId(QStringLiteral("child_paint_view"));
    m_childBoard->model().setIntId(2);
    m_childBoard->setActiveIndicator(false);
    m_childBoard->resizeSurface(400, 400, 1.0);

    setupBoardSinks(QStringLiteral("main"), m_mainBoard.get());
    setupBoardSinks(QStringLiteral("child"), m_childBoard.get());

    bindPaintViewToBusiness(QStringLiteral("main"), m_mainBoard.get());
    bindPaintViewToBusiness(QStringLiteral("child"), m_childBoard.get());

    setupBusinessConnections();
    registerPythonUiCallbacks();

    m_mainBoard->resetHistory(QStringLiteral("Initial"));
    m_childBoard->resetHistory(QStringLiteral("Initial"));

    if (m_ws) {
        connect(m_ws, &WsServer::clientConnected, this, &BackendSession::onClientConnected);
        connect(m_ws, &WsServer::clientDisconnected, this, &BackendSession::onClientDisconnected);
        connect(m_ws, &WsServer::textReceived, this, [this](const QByteArray &data) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                return;
            }
            try {
                onMessageReceived(doc.object());
            } catch (const std::exception &error) {
                m_business->setStatusMessage(QString::fromUtf8(error.what()));
                markDirty(QStringLiteral("window"));
            } catch (...) {
                m_business->setStatusMessage(QStringLiteral("Backend error while handling a message"));
                markDirty(QStringLiteral("window"));
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

void BackendSession::setStatusText(const QString &text)
{
    m_business->setStatusMessage(text);
    markDirty(QStringLiteral("window"));
}

void BackendSession::initializePythonStartup()
{
#ifdef ANIMEAN_WITH_PYTHON
    registerPythonUiCallbacks();
    m_business->syncEmbeddedPythonState();
    appendPythonDebug(QStringLiteral(
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
    m_mainBoard->resetHistory(QStringLiteral("Initial"));
    m_childBoard->resetHistory(QStringLiteral("Initial"));
    m_business->refreshToolOptions();
#endif
}

void BackendSession::loadTimelineSettings()
{
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));

    const QString area = settings.value(QStringLiteral("dockArea"), QStringLiteral("bottom")).toString();
    if (area == QStringLiteral("left") || area == QStringLiteral("right") || area == QStringLiteral("bottom")) {
        m_timelineDockArea = area;
    } else {
        m_timelineDockArea = QStringLiteral("bottom");
    }

    m_timelineCollapsed = settings.value(QStringLiteral("collapsed"), false).toBool();
    m_timelineFloating = settings.value(QStringLiteral("floating"), false).toBool();
    m_timelineVisible = settings.value(QStringLiteral("visible"), true).toBool();

    settings.endGroup();
}

void BackendSession::saveTimelineSettings() const
{
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));
    settings.setValue(QStringLiteral("dockArea"), m_timelineDockArea);
    settings.setValue(QStringLiteral("floating"), m_timelineFloating);
    settings.setValue(QStringLiteral("collapsed"), m_timelineCollapsed);
    settings.setValue(QStringLiteral("visible"), m_timelineVisible);
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
    auto captureForHost = [this](const char *host, QVector<QPair<QString, QString>> *out) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string(host))
                                         .cast<std::string>();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
                return;
            }
            for (const QJsonValue &value : document.array()) {
                const QJsonObject object = value.toObject();
                const QString name = object.value(QStringLiteral("name")).toString();
                if (name.isEmpty()) {
                    continue;
                }
                const QString title = object.value(QStringLiteral("title")).toString(name);
                out->append(qMakePair(name, title));
            }
        } catch (const py::error_already_set &error) {
            m_business->setStatusMessage(QStringLiteral("menus error: %1").arg(QString::fromUtf8(error.what())));
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
            if (info.name == QStringLiteral("paint")) {
                info.current = QStringLiteral("texture");
                break;
            }
        }
        markDirty(QStringLiteral("windows"));
    }
    emitUiEvent(QStringLiteral("textureView.surface"));
    m_business->setActiveViewName(QStringLiteral("child"));
}

void BackendSession::initWindowInfos()
{
    m_windowInfos.clear();

    auto addWin = [this](const QString &name,
                         const QString &title,
                         bool visible,
                         const QStringList &pages,
                         const QString &current) {
        AnimeanWindowInfo info;
        info.name = name;
        info.title = title;
        info.visible = visible;
        info.pages = pages;
        info.current = current;
        m_windowInfos.append(info);
    };

    addWin(QStringLiteral("paint"),
           QStringLiteral("Paint"),
           true,
           {QStringLiteral("drawing"), QStringLiteral("texture")},
           QStringLiteral("drawing"));
    addWin(QStringLiteral("tools"),
           QStringLiteral("Tools"),
           true,
           {QStringLiteral("painting"), QStringLiteral("mapping")},
           QStringLiteral("painting"));
    addWin(QStringLiteral("tool_options"),
           QStringLiteral("Tool Options"),
           true,
           {QStringLiteral("options")},
           QStringLiteral("options"));
    addWin(QStringLiteral("layers"),
           QStringLiteral("Layers"),
           true,
           {QStringLiteral("main"), QStringLiteral("child")},
           QStringLiteral("main"));
    addWin(QStringLiteral("assets"),
           QStringLiteral("Assets"),
           false,
           {QStringLiteral("assets")},
           QStringLiteral("assets"));
    addWin(QStringLiteral("history"),
           QStringLiteral("History"),
           false,
           {QStringLiteral("history")},
           QStringLiteral("history"));
    addWin(QStringLiteral("repulsion_pad"),
           QStringLiteral("Repulsion Pad"),
           false,
           {QStringLiteral("repulsion_pad")},
           QStringLiteral("repulsion_pad"));
    addWin(QStringLiteral("python_debug"),
           QStringLiteral("Python Debug"),
           false,
           {QStringLiteral("python_debug")},
           QStringLiteral("python_debug"));
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
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
        QStringList allowed;
        for (const QJsonValue &value : document.array()) {
            const QString property = value.toString();
            if (!property.isEmpty()) {
                allowed.append(property);
            }
        }
        m_childBoard->setEditablePropertyFilter(allowed);
    } catch (const py::error_already_set &error) {
        m_business->setStatusMessage(QStringLiteral("protected properties error: %1")
                                         .arg(QString::fromUtf8(error.what())));
    }
#endif
}

void BackendSession::setupBoardSinks(const QString &viewName, PaintOpenGLWidget *paintView)
{
    if (!paintView) {
        return;
    }
    paintView->setCanSendFrame([this]() { return m_ws && m_ws->hasClient(); });
    paintView->setFrameSink([this, viewName](const BoardFrame &frame) {
        handleFrameSink(viewName, frame);
    });
    paintView->setImageSink([this](const QString &key, const QImage &rgba) {
        handleImageSink(key, rgba);
    });
    paintView->setCursorSink([this, viewName](const QCursor &cursor) {
        handleCursorSink(viewName, cursor);
    });
    handleCursorSink(viewName, paintView->cursor());
}

void BackendSession::bindPaintViewToBusiness(const QString &viewName, PaintOpenGLWidget *paintView)
{
    if (!m_business || !paintView) {
        return;
    }

    AnimePaintViewAdapter adapter;
    adapter.viewObject = paintView;
    adapter.model = &paintView->model();
    adapter.history = &paintView->history();

    adapter.modelReplaced = [paintView]() { paintView->modelReplaced(); };
    adapter.setCanvasSize = [paintView](const QSize &size) { paintView->setCanvasSize(size); };
    adapter.requestUpdate = [paintView]() { paintView->update(); };
    adapter.setCurrentFrame = [paintView](int frameIndex) { paintView->setCurrentFrame(frameIndex); };
    adapter.setCurrentLayer = [paintView](int layerIndex) { paintView->setCurrentLayer(layerIndex); };
    adapter.setCurrentAsset = [paintView](int assetIndex) { paintView->setCurrentAsset(assetIndex); };

    adapter.commitHistory = [paintView](const QString &label) { paintView->commitHistory(label); };
    adapter.resetHistory = [paintView](const QString &label) { paintView->resetHistory(label); };
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

    adapter.addAsset = [paintView](AnimeColumnType type, const QString &name) {
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
    adapter.setStrokeProperty = [paintView](const QString &property) {
        paintView->setStrokeProperty(property);
    };
    adapter.setDrawingColor = [paintView](const QColor &color) {
        paintView->setDrawingColor(color);
    };
    adapter.setPenWidth = [paintView](qreal width) { paintView->setPenWidth(width); };
    adapter.setFillScope = [paintView](int scope) {
        paintView->setFillScope(static_cast<PaintOpenGLWidget::FillScope>(scope));
    };
    adapter.setSmoothValue = [paintView](int value) { paintView->setSmoothValue(value); };
    adapter.setStrokeFitSettings = [paintView](const AnimeStrokeFitSettings &settings) {
        paintView->setStrokeFitSettings(settings);
    };
    adapter.sendExtraToolMessage = [paintView](const QString &name, const QString &property) {
        paintView->sendPythonExtraToolMessage(name, property);
    };
    adapter.sendToolOptionMessage = [paintView](const QString &hook,
                                                const QString &name,
                                                const QString &type,
                                                const QVariant &value,
                                                int row,
                                                int startColumn,
                                                int endColumn) {
        paintView->sendPythonToolOptionMessage(hook, name, type, value, row, startColumn, endColumn);
    };
    adapter.sendPadMessage = [paintView](const QString &pad,
                                         const QString &phase,
                                         double x,
                                         double y) {
        paintView->sendPythonPadMessage(pad, phase, x, y);
    };

    adapter.buildPlaybackCache = [paintView](int frameCount, QString *error) {
        return paintView->buildPlaybackCache(frameCount, error);
    };
    adapter.showPlaybackFrame = [paintView](int frameIndex) {
        paintView->showPlaybackFrame(frameIndex);
    };
    adapter.endPlayback = [paintView]() { paintView->endPlayback(); };

    adapter.setOnionEnabled = [paintView](bool on) { paintView->setOnionEnabled(on); };
    adapter.setOnionFrames = [paintView](const QSet<int> &frames) {
        paintView->setOnionFrames(frames);
    };
    adapter.setOnionLines = [paintView](bool on) { paintView->setOnionLines(on); };
    adapter.setOnionFills = [paintView](bool on) { paintView->setOnionFills(on); };
    adapter.setOnionAmLayers = [paintView](bool on) { paintView->setOnionAmLayers(on); };
    adapter.setOnionGuideLines = [paintView](bool on) { paintView->setOnionGuideLines(on); };
    adapter.setOnionExcludeProperties = [paintView](const QStringList &properties) {
        paintView->setOnionExcludeProperties(properties);
    };
    adapter.setOnionLayerTag = [paintView](const QString &tag) {
        paintView->setOnionLayerTag(tag);
    };

    adapter.renderFrameThumbnail = [paintView](int frameIndex, const QSize &size) {
        return paintView->renderFrameThumbnail(frameIndex, size);
    };

    m_business->bindPaintView(viewName, adapter);

    connect(paintView, &PaintOpenGLWidget::focusGained, this, [this, viewName]() {
        m_business->setActiveViewName(viewName);
    });
    connect(paintView, &PaintOpenGLWidget::layerListChanged, this, [this, viewName, paintView](int selectedLayer) {
        m_business->updateAttention(viewName,
                                    AttentionChange::LayerChange,
                                    paintView->model().currentFrame(),
                                    selectedLayer,
                                    paintView->model().currentAsset());
    });
    connect(paintView, &PaintOpenGLWidget::assetListChanged, this, [this, viewName](int selectedAsset) {
        const SelectionAttention current = m_business->attentionForView(viewName);
        m_business->updateAttention(viewName,
                                    AttentionChange::AssetChange,
                                    current.frame,
                                    current.layer,
                                    selectedAsset);
    });
    connect(paintView, &PaintOpenGLWidget::pythonDebugMessage, this, &BackendSession::appendPythonDebug);
    connect(paintView, &PaintOpenGLWidget::playbackInterrupted, m_business, &AnimeanBusiness::stopPlayback);
    connect(paintView, &PaintOpenGLWidget::historyChanged, this, [this]() {
        markDirty(QStringLiteral("history"));
        markDirty(QStringLiteral("menus"));
    });
    connect(paintView, &PaintOpenGLWidget::historyCommitted, this, [this, viewName]() {
        m_business->handleViewHistoryCommitted(viewName);
        markDirty(QStringLiteral("history"));
        markDirty(QStringLiteral("menus"));
    });
    connect(paintView, &PaintOpenGLWidget::viewTransformChanged, this, [this, viewName]() {
        markDirty(viewName == QStringLiteral("child") ? QStringLiteral("viewport/child")
                                                      : QStringLiteral("viewport/main"));
    });
    connect(paintView, &PaintOpenGLWidget::userTransformed, this, [this, viewName]() {
        markDirty(viewName == QStringLiteral("child") ? QStringLiteral("viewport/child")
                                                      : QStringLiteral("viewport/main"));
    });
}

void BackendSession::setupBusinessConnections()
{
    connect(m_business, &AnimeanBusiness::statusMessageChanged, this, [this](const QString &) {
        markDirty(QStringLiteral("window"));
    });
    connect(m_business, &AnimeanBusiness::pythonDebugMessage, this, &BackendSession::appendPythonDebug);
    connect(m_business, &AnimeanBusiness::projectFilePathChanged, this, [this](const QString &) {
        markDirty(QStringLiteral("window"));
    });
    connect(m_business, &AnimeanBusiness::childFilePathChanged, this, [this](const QString &) {
        markDirty(QStringLiteral("window"));
    });
    connect(m_business, &AnimeanBusiness::windowTitleChanged, this, [this](const QString &) {
        markDirty(QStringLiteral("window"));
    });
    connect(m_business, &AnimeanBusiness::canvasSizeChanged, this, [this](const QString &viewName, const QSize &) {
        markDirty(QStringLiteral("window"));
        markDirty(viewName == QStringLiteral("child") ? QStringLiteral("viewport/child")
                                                      : QStringLiteral("viewport/main"));
    });
    connect(m_business, &AnimeanBusiness::activeViewChanged, this, [this](const QString &activeView) {
        if (m_mainBoard) {
            m_mainBoard->setActiveIndicator(activeView == QStringLiteral("main"));
        }
        if (m_childBoard) {
            m_childBoard->setActiveIndicator(activeView == QStringLiteral("child"));
        }
        markDirty(QStringLiteral("window"));
        markDirty(QStringLiteral("tools"));
        markDirty(QStringLiteral("history"));
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("layers/main"));
        markDirty(QStringLiteral("layers/child"));
        markDirty(QStringLiteral("assets/main"));
        markDirty(QStringLiteral("assets/child"));
    });
    connect(m_business, &AnimeanBusiness::requestSurfaceTextureView, this, [this]() {
        emitUiEvent(QStringLiteral("textureView.surface"));
    });
    connect(m_business, &AnimeanBusiness::requestAutoFitTextureControlView, this, [this]() {
        if (m_childBoard) {
            m_childBoard->fitViewToContent(false);
            m_textureAutoFitFrame = m_childBoard->model().currentFrame();
        }
    });
    connect(m_business, &AnimeanBusiness::layersChanged, this, [this](const QString &viewName, int) {
        if (viewName == QStringLiteral("child")) {
            markDirty(QStringLiteral("layers/child"));
        } else {
            markDirty(QStringLiteral("layers/main"));
        }
    });
    connect(m_business, &AnimeanBusiness::assetsChanged, this, [this](const QString &viewName, int) {
        if (viewName == QStringLiteral("child")) {
            markDirty(QStringLiteral("assets/child"));
        } else {
            markDirty(QStringLiteral("assets/main"));
        }
    });
    connect(m_business, &AnimeanBusiness::timelineChanged, this, [this]() {
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("layers/main"));
        markDirty(QStringLiteral("layers/child"));
    });
    connect(m_business, &AnimeanBusiness::playbackStateChanged, this, [this](bool, int, const QString &) {
        markDirty(QStringLiteral("timeline"));
    });
    connect(m_business, &AnimeanBusiness::onionStateChanged, this, [this]() {
        markDirty(QStringLiteral("timeline"));
    });
    connect(m_business, &AnimeanBusiness::toolChanged, this, [this](int, const QString &) {
        markDirty(QStringLiteral("tools"));
    });
    connect(m_business, &AnimeanBusiness::toolLockStateChanged, this, [this]() {
        markDirty(QStringLiteral("tools"));
    });
    connect(m_business, &AnimeanBusiness::toolOptionsLayoutChanged,
            this, [this](const QJsonObject &layout, int, bool) {
        m_lastToolOptions = layout;
        markDirty(QStringLiteral("toolOptions"));
    });
    connect(m_business, &AnimeanBusiness::toolOptionsStateChanged, this, [this]() {
        markDirty(QStringLiteral("drawSettings"));
    });
    connect(m_business, &AnimeanBusiness::drawingColorChanged, this, [this](const QColor &) {
        markDirty(QStringLiteral("drawColor"));
    });
    connect(m_business, &AnimeanBusiness::historyChanged, this, [this]() {
        markDirty(QStringLiteral("history"));
        markDirty(QStringLiteral("menus"));
    });
    connect(m_business, &AnimeanBusiness::historyRestored, this, [this](const QString &viewName) {
        markDirty(QStringLiteral("history"));
        markDirty(QStringLiteral("menus"));
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("layers/main"));
        markDirty(QStringLiteral("layers/child"));
        markDirty(QStringLiteral("assets/main"));
        markDirty(QStringLiteral("assets/child"));
        if (PaintOpenGLWidget *board = boardForView(viewName)) {
            board->update();
        }
    });

    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() {
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        if (m_mainBoard) {
            m_mainBoard->update();
        }
        if (m_childBoard) {
            m_childBoard->update();
        }
        markDirty(QStringLiteral("theme"));
        markDirty(QStringLiteral("menus"));
        markDirty(QStringLiteral("history"));
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
        if (frame) {
            markDirty(QStringLiteral("timeline"));
            if (m_childBoard && m_childBoard->model().currentFrame() != m_textureAutoFitFrame) {
                m_childBoard->fitViewToContent(false);
                m_textureAutoFitFrame = m_childBoard->model().currentFrame();
            }
        }
        if (layer) {
            markDirty(QStringLiteral("layers/main"));
            markDirty(QStringLiteral("layers/child"));
        }
        if (asset) {
            markDirty(QStringLiteral("assets/main"));
            markDirty(QStringLiteral("assets/child"));
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
    windows.show = [this](const QString &name, bool on) {
        if (name == QStringLiteral("paint")) {
            return;
        }
        if (name == QStringLiteral("timeline")) {
            m_timelineVisible = on;
            saveTimelineSettings();
            markDirty(QStringLiteral("windows"));
            markDirty(QStringLiteral("timeline"));
            markDirty(QStringLiteral("menus"));
            emitUiEvent(QStringLiteral("window.show"),
                        QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("visible"), on}});
            return;
        }
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == name) {
                info.visible = on;
                markDirty(QStringLiteral("windows"));
                markDirty(QStringLiteral("menus"));
                emitUiEvent(QStringLiteral("window.show"),
                            QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("visible"), on}});
                return;
            }
        }
    };
    windows.select = [this](const QString &name, const QString &page) {
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == name) {
                if (info.pages.contains(page)) {
                    info.current = page;
                }
                if (name == QStringLiteral("paint")) {
                    if (page == QStringLiteral("texture")) {
                        m_business->setActiveViewName(QStringLiteral("child"));
                    } else if (page == QStringLiteral("drawing")) {
                        m_business->setActiveViewName(QStringLiteral("main"));
                    }
                }
                markDirty(QStringLiteral("windows"));
                if (name == QStringLiteral("tools")) {
                    markDirty(QStringLiteral("tools"));
                }
                emitUiEvent(QStringLiteral("window.select"),
                            QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("page"), page}});
                return;
            }
        }
    };
    registerAnimeanUiWindowsCallback(std::move(windows));

    registerAnimeanUiOverlayCallback([this](const QString &view, const QVector<AnimeanOverlayItem> &items) {
        PaintOpenGLWidget *target = boardForView(view);
        if (!target) {
            return;
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
            overlayItem.confirmable = item.confirmable;
            overlayItem.draggable = item.draggable;
            converted.append(overlayItem);
        }
        target->setOverlayItems(converted);
    });

    registerAnimeanUiEditHandleCallback([this](const QString &view, const QVector<AnimeanEditHandle> &handles) {
        PaintOpenGLWidget *target = boardForView(view);
        if (!target) {
            return;
        }
        QVector<PaintOpenGLWidget::EditHandle> converted;
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

    registerAnimeanUiCursorCallback([this](const QString &view, const QString &name) {
        PaintOpenGLWidget *target = boardForView(view);
        if (target) {
            target->setScriptCursor(name);
        }
    });

    registerAnimeanUiFillPaintModeCallback([this](const QString &view, bool on) {
        if (view == QStringLiteral("main") && m_mainBoard) {
            m_mainBoard->setFillPaintMode(on);
            return;
        }
        if (view == QStringLiteral("child") && m_childBoard) {
            m_childBoard->setFillPaintMode(on);
            return;
        }
        appendPythonDebug(QStringLiteral("[fill paint] unknown view '%1'").arg(view));
    });

    registerAnimeanUiLockedToolsCallback([this](const QString &view, const QStringList &tools) {
        m_business->applyLockedTools(view, tools);
    });

    registerAnimeanUiDrawColorCallback([this](const QColor &color) {
        m_business->setDrawingColor(color);
    });

    registerAnimeanUiDrawSettingsCallback([this](int stabilizer, int simplify, int corner) {
        m_business->setDrawSettings(stabilizer, simplify, corner);
    });

    registerAnimeanUiHistoryCallback([this](const QString &op, const QString &viewName, const QString &label) {
        if (!viewName.isEmpty()
            && viewName != QStringLiteral("main")
            && viewName != QStringLiteral("child")) {
            appendPythonDebug(QStringLiteral("[history] unknown view '%1'").arg(viewName));
            return;
        }
        if (op == QStringLiteral("commit")) {
            m_business->commitHistory(label.isEmpty() ? QStringLiteral("Python") : label, viewName);
        } else if (op == QStringLiteral("undo")) {
            m_business->undo(viewName);
        } else if (op == QStringLiteral("redo")) {
            m_business->redo(viewName);
        }
    });

    registerAnimeanUiPadValueCallback([this](const QString &pad, double x, double y) {
        const QPointF pt(x, y);
        m_padValues.insert(pad, pt);
        if (pad == QStringLiteral("force_pad")) {
            m_forcePadValue = pt;
        }
        markDirty(QStringLiteral("pad"));
    });
#endif
}

PaintOpenGLWidget *BackendSession::boardForView(const QString &viewName) const
{
    if (viewName == QStringLiteral("child")) {
        return m_childBoard.get();
    }
    if (viewName == QStringLiteral("main")) {
        return m_mainBoard.get();
    }
    return (m_business && m_business->activeViewName() == QStringLiteral("child"))
               ? m_childBoard.get()
               : m_mainBoard.get();
}

QString BackendSession::resolveViewName(const QString &viewName) const
{
    if (viewName == QStringLiteral("child")) {
        return QStringLiteral("child");
    }
    if (viewName == QStringLiteral("main")) {
        return QStringLiteral("main");
    }
    return (m_business && m_business->activeViewName() == QStringLiteral("child"))
               ? QStringLiteral("child")
               : QStringLiteral("main");
}

void BackendSession::onClientConnected()
{
    for (auto it = m_cachedImages.cbegin(); it != m_cachedImages.cend(); ++it) {
        sendImagePacket(it.key(), it.value());
    }

    m_dirtyKeys.clear();
    for (const QString &key : allStateKeys()) {
        sendStateKey(key);
    }

    if (m_mainBoard) {
        m_mainBoard->acknowledgeFrame(m_lastBoardFrameId.value(QStringLiteral("main"), 0));
        m_mainBoard->resizeSurface(m_mainBoard->width(),
                                   m_mainBoard->height(),
                                   m_mainBoard->devicePixelRatioF());
    }
    if (m_childBoard) {
        m_childBoard->acknowledgeFrame(m_lastBoardFrameId.value(QStringLiteral("child"), 0));
        m_childBoard->resizeSurface(m_childBoard->width(),
                                    m_childBoard->height(),
                                    m_childBoard->devicePixelRatioF());
    }
}

void BackendSession::onClientDisconnected()
{
    // Keep session alive and wait for next connection.
}

void BackendSession::onMessageReceived(const QJsonObject &msg)
{
    const QString t = msg.value(QStringLiteral("t"))
                          .toString(msg.value(QStringLiteral("type")).toString());
    if (t == QStringLiteral("call")) {
        handleCallMessage(msg);
    } else if (t == QStringLiteral("input")) {
        handleInputMessage(msg);
    } else if (t == QStringLiteral("ack")) {
        handleAckMessage(msg);
    }
}

void BackendSession::handleCallMessage(const QJsonObject &msg)
{
    const QJsonValue idVal = msg.value(QStringLiteral("id"));
    const QString method = msg.value(QStringLiteral("method")).toString();
    const QJsonObject params = msg.value(QStringLiteral("params")).toObject();

    // Ensure any pending board frame is unblocked so mutations from this call produce frames.
    if (m_mainBoard) {
        m_mainBoard->acknowledgeFrame(m_lastBoardFrameId.value(QStringLiteral("main"), 0));
    }
    if (m_childBoard) {
        m_childBoard->acknowledgeFrame(m_lastBoardFrameId.value(QStringLiteral("child"), 0));
    }

    QJsonObject reply;
    reply.insert(QStringLiteral("t"), QStringLiteral("reply"));
    reply.insert(QStringLiteral("id"), idVal);

    try {
        const QJsonObject result = dispatchCall(method, params);
        if (result.value(QStringLiteral("__error__")).toBool(false)) {
            reply.insert(QStringLiteral("ok"), false);
            reply.insert(QStringLiteral("error"),
                         result.value(QStringLiteral("error")).toString(QStringLiteral("Call failed")));
        } else {
            reply.insert(QStringLiteral("ok"), true);
            reply.insert(QStringLiteral("result"), result);
        }
    } catch (const std::exception &ex) {
        reply.insert(QStringLiteral("ok"), false);
        reply.insert(QStringLiteral("error"), QString::fromUtf8(ex.what()));
    } catch (...) {
        reply.insert(QStringLiteral("ok"), false);
        reply.insert(QStringLiteral("error"), QStringLiteral("Unknown exception during call"));
    }

    sendJson(reply);
}

void BackendSession::handleInputMessage(const QJsonObject &msg)
{
    const QString view = resolveViewName(msg.value(QStringLiteral("view")).toString(QStringLiteral("main")));
    PaintOpenGLWidget *board = boardForView(view);
    if (!board) {
        return;
    }

    board->acknowledgeFrame(m_lastBoardFrameId.value(view, 0));

    const QString kind = msg.value(QStringLiteral("kind")).toString();
    const qreal x = msg.value(QStringLiteral("x")).toDouble(0.0);
    const qreal y = msg.value(QStringLiteral("y")).toDouble(0.0);
    const Qt::KeyboardModifiers modifiers =
        static_cast<Qt::KeyboardModifiers>(msg.value(QStringLiteral("modifiers")).toInt(0));
    const quint64 timestamp = quint64(std::max(0.0, msg.value(QStringLiteral("time")).toDouble(0.0)));

    if (kind == QStringLiteral("press")) {
        Qt::MouseButton button = static_cast<Qt::MouseButton>(msg.value(QStringLiteral("button")).toInt(int(Qt::LeftButton)));
        if (button == Qt::NoButton && !msg.contains(QStringLiteral("button"))) {
            button = Qt::LeftButton;
        }
        Qt::MouseButtons buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(int(button)));
        if (buttons == Qt::NoButton && !msg.contains(QStringLiteral("buttons"))) {
            buttons = button;
        }
        board->injectMouse(QEvent::MouseButtonPress, QPointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == QStringLiteral("move")) {
        Qt::MouseButton button = static_cast<Qt::MouseButton>(msg.value(QStringLiteral("button")).toInt(int(Qt::NoButton)));
        Qt::MouseButtons buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(int(Qt::LeftButton)));
        if (msg.contains(QStringLiteral("buttons"))) {
            buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(0));
        }
        board->injectMouse(QEvent::MouseMove, QPointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == QStringLiteral("release")) {
        Qt::MouseButton button = static_cast<Qt::MouseButton>(msg.value(QStringLiteral("button")).toInt(int(Qt::LeftButton)));
        if (button == Qt::NoButton && !msg.contains(QStringLiteral("button"))) {
            button = Qt::LeftButton;
        }
        Qt::MouseButtons buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(int(Qt::NoButton)));
        board->injectMouse(QEvent::MouseButtonRelease, QPointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == QStringLiteral("doubleClick") || kind == QStringLiteral("dblclick")) {
        Qt::MouseButton button = static_cast<Qt::MouseButton>(msg.value(QStringLiteral("button")).toInt(int(Qt::LeftButton)));
        Qt::MouseButtons buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(int(button)));
        board->injectMouse(QEvent::MouseButtonDblClick, QPointF(x, y), button, buttons, modifiers, timestamp);
    } else if (kind == QStringLiteral("wheel")) {
        const int dx = msg.value(QStringLiteral("dx")).toInt(0);
        const int dy = msg.value(QStringLiteral("dy")).toInt(0);
        const Qt::MouseButtons buttons = static_cast<Qt::MouseButtons>(msg.value(QStringLiteral("buttons")).toInt(0));
        board->injectWheel(QPointF(x, y), QPoint(dx, dy), modifiers, buttons);
    } else if (kind == QStringLiteral("enter")) {
        board->injectEnter();
    } else if (kind == QStringLiteral("leave")) {
        board->injectLeave();
    } else if (kind == QStringLiteral("resize")) {
        const int w = msg.value(QStringLiteral("w")).toInt(msg.value(QStringLiteral("width")).toInt(board->width()));
        const int h = msg.value(QStringLiteral("h")).toInt(msg.value(QStringLiteral("height")).toInt(board->height()));
        const qreal dpr = msg.value(QStringLiteral("dpr")).toDouble(1.0);
        board->resizeSurface(w, h, dpr);
        markDirty(view == QStringLiteral("child") ? QStringLiteral("viewport/child")
                                                  : QStringLiteral("viewport/main"));
    } else if (kind == QStringLiteral("focus")) {
        const bool focused = msg.value(QStringLiteral("focused")).toBool(true);
        if (focused) {
            board->injectFocus();
        }
    }
}

void BackendSession::handleAckMessage(const QJsonObject &msg)
{
    const QString view = resolveViewName(msg.value(QStringLiteral("view")).toString(QStringLiteral("main")));
    PaintOpenGLWidget *board = boardForView(view);
    if (!board) {
        return;
    }
    const uint64_t frameId = static_cast<uint64_t>(msg.value(QStringLiteral("frame")).toInteger(0));
    board->acknowledgeFrame(frameId);
}

QJsonObject BackendSession::dispatchCall(const QString &method, const QJsonObject &params)
{
    auto makeError = [](const QString &err) {
        return QJsonObject{
            {QStringLiteral("__error__"), true},
            {QStringLiteral("error"), err},
        };
    };

    const QString view = params.value(QStringLiteral("view"))
                             .toString(params.value(QStringLiteral("viewName")).toString(QStringLiteral("main")));

    if (method == QStringLiteral("project.new")) {
        const QSize current = m_business->canvasSize(QStringLiteral("main"));
        int w = params.value(QStringLiteral("width")).toInt(params.value(QStringLiteral("w")).toInt(current.width()));
        int h = params.value(QStringLiteral("height")).toInt(params.value(QStringLiteral("h")).toInt(current.height()));
        if (params.value(QStringLiteral("canvasSize")).isObject()) {
            const QJsonObject cs = params.value(QStringLiteral("canvasSize")).toObject();
            w = cs.value(QStringLiteral("width")).toInt(cs.value(QStringLiteral("w")).toInt(w));
            h = cs.value(QStringLiteral("height")).toInt(cs.value(QStringLiteral("h")).toInt(h));
        }
        w = std::max(1, w);
        h = std::max(1, h);
        m_business->newProject(QSize(w, h));
        return QJsonObject{{QStringLiteral("width"), w}, {QStringLiteral("height"), h}};
    }

    if (method == QStringLiteral("project.open")) {
        const QString fileName = params.value(QStringLiteral("path"))
                                     .toString(params.value(QStringLiteral("fileName"))
                                                   .toString(params.value(QStringLiteral("filePath")).toString()));
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        QString error;
        const bool ok = (view == QStringLiteral("child"))
                            ? m_business->openTextureViewFrom(fileName, &error)
                            : m_business->loadProjectFrom(fileName, &error);
        if (!ok) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to open file") : error);
        }
        const QString openedPath = (view == QStringLiteral("child"))
                                       ? m_business->childFilePath()
                                       : m_business->currentFilePath();
        return QJsonObject{
            {QStringLiteral("path"), openedPath},
            {QStringLiteral("fileName"), openedPath},
        };
    }

    if (method == QStringLiteral("project.save")) {
        QString fileName = params.value(QStringLiteral("path"))
                               .toString(params.value(QStringLiteral("fileName"))
                                             .toString(params.value(QStringLiteral("filePath")).toString()));
        if (fileName.isEmpty()) {
            fileName = (view == QStringLiteral("child"))
                           ? m_business->childFilePath()
                           : m_business->currentFilePath();
        }
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("No file path specified for save"));
        }
        QString error;
        const bool ok = (view == QStringLiteral("child"))
                            ? m_business->saveTextureViewTo(fileName, &error)
                            : m_business->saveProjectTo(fileName, &error);
        if (!ok) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to save file") : error);
        }
        const QString savedPath = (view == QStringLiteral("child"))
                                      ? m_business->childFilePath()
                                      : m_business->currentFilePath();
        return QJsonObject{
            {QStringLiteral("path"), savedPath},
            {QStringLiteral("fileName"), savedPath},
        };
    }

    if (method == QStringLiteral("project.saveAs")) {
        const QString fileName = params.value(QStringLiteral("path"))
                                     .toString(params.value(QStringLiteral("fileName"))
                                                   .toString(params.value(QStringLiteral("filePath")).toString()));
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        QString error;
        const bool ok = (view == QStringLiteral("child"))
                            ? m_business->saveTextureViewTo(fileName, &error)
                            : m_business->saveProjectTo(fileName, &error);
        if (!ok) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to save file") : error);
        }
        const QString savedPath = (view == QStringLiteral("child"))
                                      ? m_business->childFilePath()
                                      : m_business->currentFilePath();
        return QJsonObject{
            {QStringLiteral("path"), savedPath},
            {QStringLiteral("fileName"), savedPath},
        };
    }

    if (method == QStringLiteral("file.import")) {
        return handleFileImportCall(params);
    }

    if (method == QStringLiteral("file.texture")) {
        return handleFileTextureCall(params);
    }

    if (method == QStringLiteral("layer.contextMenu")) {
        // Same order as MainWindow::showLayerContextMenu: the group's own two
        // actions, then whatever Python registered for this target.
        const QString viewName = params.value(QStringLiteral("view")).toString(QStringLiteral("main"));
        PaintOpenGLWidget *board = boardForView(viewName);
        if (!board) {
            return makeError(QStringLiteral("Unknown view"));
        }
        const int groupId = params.value(QStringLiteral("groupId")).toInt(0);
        const int layerIndex = groupId > 0 ? -1 : params.value(QStringLiteral("layer")).toInt(-1);
        const bool hasItem = groupId > 0 || layerIndex >= 0;
        QJsonArray entries;
#ifdef ANIMEAN_WITH_PYTHON
        py::dict context;
        context["view"] = viewName.toStdString();
        context["kind"] = !hasItem ? "panel" : (groupId > 0 ? "group" : "layer");
        context["group"] = groupId;
        context["group_name"] = params.value(QStringLiteral("groupName")).toString().toStdString();
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
        for (const QJsonValue &member : params.value(QStringLiteral("members")).toArray()) {
            memberList.append(member.toInt());
        }
        context["members"] = memberList;
        const std::string json = py::module_::import("python_hooks").attr("menu_items_json")(context).cast<std::string>();
        entries = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
#endif
        QJsonArray items;
        const auto addItem = [&items](const QString &id, const QString &text, const QString &kind, bool enabled) {
            items.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("text"), text},
                                     {QStringLiteral("kind"), kind}, {QStringLiteral("enabled"), enabled}});
        };
        if (groupId > 0) {
            addItem(QStringLiteral("ungroup"), QStringLiteral("Ungroup (keep the layers)"), QStringLiteral("action"), true);
            addItem(QStringLiteral("deleteGroup"), QStringLiteral("Delete Group and Layers"), QStringLiteral("action"), true);
            if (!entries.isEmpty()) {
                addItem(QStringLiteral("sep"), QString(), QStringLiteral("separator"), true);
            }
        }
        for (const QJsonValue &value : entries) {
            const QJsonObject object = value.toObject();
            const QString kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("action"));
            if (kind == QStringLiteral("separator")) {
                addItem(QStringLiteral("sep"), QString(), kind, true);
                continue;
            }
            const QString name = object.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            const QString title = object.value(QStringLiteral("title")).toString(name);
            const bool enabled = object.value(QStringLiteral("enabled")).toBool(true);
            if (kind == QStringLiteral("settings")) {
                addItem(QStringLiteral("settings:") + object.value(QStringLiteral("settings")).toString(name), title,
                        QStringLiteral("action"), enabled);
            } else {
                addItem(QStringLiteral("script:") + name, title, QStringLiteral("action"), enabled);
            }
        }
        return QJsonObject{{QStringLiteral("items"), items}};
    }

    if (method == QStringLiteral("layer.contextAction")) {
        const QString viewName = params.value(QStringLiteral("view")).toString(QStringLiteral("main"));
        PaintOpenGLWidget *board = boardForView(viewName);
        if (!board) {
            return makeError(QStringLiteral("Unknown view"));
        }
        const QString id = params.value(QStringLiteral("id")).toString();
        const int groupId = params.value(QStringLiteral("groupId")).toInt(0);
        const int layerIndex = groupId > 0 ? -1 : params.value(QStringLiteral("layer")).toInt(-1);
        if (id == QStringLiteral("ungroup")) {
            m_business->dissolveLayerGroup(groupId, viewName);
        } else if (id == QStringLiteral("deleteGroup")) {
            m_business->deleteLayerGroup(groupId, viewName);
        } else if (id.startsWith(QStringLiteral("settings:"))) {
            openScriptSettings(id.mid(9), params.value(QStringLiteral("title")).toString());
        } else if (id.startsWith(QStringLiteral("script:"))) {
            QVector<int> members;
            for (const QJsonValue &member : params.value(QStringLiteral("members")).toArray()) {
                members.append(member.toInt());
            }
            board->sendPythonLayerMenuMessage(id.mid(7), groupId, params.value(QStringLiteral("groupName")).toString(),
                                              layerIndex, layerIndex >= 0 ? board->layerName(layerIndex) : QString(), members);
        } else {
            return makeError(QStringLiteral("Unknown layer action"));
        }
        markDirty(viewName == QStringLiteral("child") ? QStringLiteral("layers/child") : QStringLiteral("layers/main"));
        markDirty(QStringLiteral("timeline"));
        return QJsonObject{};
    }

    if (method == QStringLiteral("layer.add")) {
        const int layerIndex = m_business->addLayer(view);
        return QJsonObject{{QStringLiteral("layer"), layerIndex}};
    }

    if (method == QStringLiteral("layer.delete")) {
        const int layerIndex = params.value(QStringLiteral("layerIndex"))
                                   .toInt(params.value(QStringLiteral("layer"))
                                              .toInt(params.value(QStringLiteral("index"))
                                                         .toInt(m_business->currentLayer(view))));
        const bool removed = m_business->deleteLayer(layerIndex, view);
        return QJsonObject{
            {QStringLiteral("deleted"), removed},
            {QStringLiteral("layer"), m_business->currentLayer(view)},
        };
    }

    if (method == QStringLiteral("layer.deleteGroup")) {
        const int groupId = params.value(QStringLiteral("groupId"))
                                .toInt(params.value(QStringLiteral("group_id"))
                                           .toInt(params.value(QStringLiteral("id")).toInt(0)));
        const int deleted = m_business->deleteLayerGroup(groupId, view);
        return QJsonObject{{QStringLiteral("deleted"), deleted}};
    }

    if (method == QStringLiteral("layer.dissolveGroup")) {
        const int groupId = params.value(QStringLiteral("groupId"))
                                .toInt(params.value(QStringLiteral("group_id"))
                                           .toInt(params.value(QStringLiteral("id")).toInt(0)));
        const bool dissolved = m_business->dissolveLayerGroup(groupId, view);
        return QJsonObject{{QStringLiteral("dissolved"), dissolved}};
    }

    if (method == QStringLiteral("layer.move")) {
        const int fromIndex = params.value(QStringLiteral("fromIndex"))
                                  .toInt(params.value(QStringLiteral("from")).toInt(-1));
        const int toIndex = params.value(QStringLiteral("toIndex"))
                                .toInt(params.value(QStringLiteral("to")).toInt(-1));
        const bool moved = m_business->moveLayer(fromIndex, toIndex, view);
        return QJsonObject{{QStringLiteral("moved"), moved}};
    }

    if (method == QStringLiteral("layer.setVisible")) {
        const int layerIndex = params.value(QStringLiteral("layerIndex"))
                                   .toInt(params.value(QStringLiteral("layer"))
                                              .toInt(params.value(QStringLiteral("index"))
                                                         .toInt(m_business->currentLayer(view))));
        const bool visible = params.value(QStringLiteral("visible"))
                                 .toBool(params.value(QStringLiteral("on")).toBool(true));
        const bool ok = m_business->setLayerVisible(layerIndex, visible, view);
        return QJsonObject{{QStringLiteral("visible"), visible}, {QStringLiteral("updated"), ok}};
    }

    if (method == QStringLiteral("layer.select")) {
        const int layerIndex = params.value(QStringLiteral("layerIndex"))
                                   .toInt(params.value(QStringLiteral("layer"))
                                              .toInt(params.value(QStringLiteral("index")).toInt(-1)));
        m_business->selectLayer(layerIndex, view);
        return QJsonObject{{QStringLiteral("selected"), m_business->currentLayer(view)}};
    }

    if (method == QStringLiteral("layer.unselect")) {
        m_business->unselectLayer(view);
        return QJsonObject{{QStringLiteral("selected"), m_business->currentLayer(view)}};
    }

    if (method == QStringLiteral("layer.setCollapsed")) {
        const int groupId = params.value(QStringLiteral("groupId"))
                                .toInt(params.value(QStringLiteral("group_id"))
                                           .toInt(params.value(QStringLiteral("id")).toInt(0)));
        const bool collapsed = params.value(QStringLiteral("collapsed"))
                                   .toBool(params.value(QStringLiteral("on")).toBool(false));
        m_business->setLayerGroupCollapsed(groupId, collapsed, view);
        markDirty(view == QStringLiteral("child") ? QStringLiteral("layers/child")
                                                  : QStringLiteral("layers/main"));
        return QJsonObject{{QStringLiteral("groupId"), groupId}, {QStringLiteral("collapsed"), collapsed}};
    }

    if (method == QStringLiteral("tool.select")) {
        QJsonValue toolVal = params.value(QStringLiteral("tool"));
        if (toolVal.isUndefined()) {
            toolVal = params.value(QStringLiteral("name"));
        }
        bool ok = false;
        const AnimeanBusiness::Tool tool = parseToolValue(toolVal, &ok);
        if (!ok) {
            return makeError(QStringLiteral("Unknown tool: %1").arg(toolVal.toVariant().toString()));
        }
        m_business->selectTool(int(tool));
        return QJsonObject{
            {QStringLiteral("tool"), AnimeanBusiness::toolControlName(m_business->currentToolEnum())},
            {QStringLiteral("toolEnum"), toolEnumDisplayName(m_business->currentToolEnum())},
        };
    }

    if (method == QStringLiteral("tool.selectExtra")) {
        const QString name = params.value(QStringLiteral("extraToolName"))
                                 .toString(params.value(QStringLiteral("name"))
                                               .toString(params.value(QStringLiteral("tool")).toString()));
        if (name.isEmpty()) {
            return makeError(QStringLiteral("extraToolName is required"));
        }
        m_business->selectExtraToolByName(name);
        return QJsonObject{{QStringLiteral("activeExtraTool"), m_business->activeExtraTool()}};
    }

    if (method == QStringLiteral("tool.option") || method == QStringLiteral("settings.option")) {
        const QString hook = params.value(QStringLiteral("hook")).toString();
        const QString name = params.value(QStringLiteral("name"))
                                 .toString(params.value(QStringLiteral("optionName")).toString());
        const QString type = params.value(QStringLiteral("type")).toString();
        const QVariant value = params.value(QStringLiteral("value")).toVariant();
        const int row = params.value(QStringLiteral("row")).toInt(-1);
        const int startColumn = params.value(QStringLiteral("startColumn"))
                                    .toInt(params.value(QStringLiteral("start_column")).toInt(-1));
        const int endColumn = params.value(QStringLiteral("endColumn"))
                                  .toInt(params.value(QStringLiteral("end_column")).toInt(-1));
        m_business->sendToolOption(hook, name, type, value, row, startColumn, endColumn);
        if (m_mainBoard) {
            m_mainBoard->update();
        }
        if (m_childBoard) {
            m_childBoard->update();
        }
        return QJsonObject{{QStringLiteral("hook"), hook}, {QStringLiteral("name"), name}};
    }

    if (method == QStringLiteral("tool.eraserMode")) {
        QJsonValue toolVal = params.value(QStringLiteral("tool"));
        if (toolVal.isUndefined()) {
            toolVal = params.value(QStringLiteral("mode"));
        }
        const AnimeanBusiness::Tool tool = parseToolValue(toolVal);
        m_business->selectEraserMode(int(tool));
        return QJsonObject{
            {QStringLiteral("tool"), AnimeanBusiness::toolControlName(m_business->currentToolEnum())},
        };
    }

    if (method == QStringLiteral("color.set")) {
        const QColor color = parseColorParams(params);
        if (!color.isValid()) {
            return makeError(QStringLiteral("Invalid color"));
        }
        if (params.value(QStringLiteral("select")).toBool(false)) {
            m_business->selectColor(color);
        } else {
            m_business->setDrawingColor(color);
        }
        return buildDrawColorState();
    }

    if (method == QStringLiteral("draw.setSmooth")) {
        const int val = params.value(QStringLiteral("value"))
                            .toInt(params.value(QStringLiteral("smooth")).toInt(m_business->toolSmoothValue()));
        m_business->setSmoothValue(val);
        return QJsonObject{{QStringLiteral("smooth"), m_business->toolSmoothValue()}};
    }

    if (method == QStringLiteral("draw.setPenWidth")) {
        const int width = params.value(QStringLiteral("width"))
                              .toInt(params.value(QStringLiteral("penWidth"))
                                         .toInt(params.value(QStringLiteral("value"))
                                                    .toInt(m_business->toolPenWidth())));
        m_business->setPenWidth(width);
        return QJsonObject{{QStringLiteral("penWidth"), m_business->toolPenWidth()}};
    }

    if (method == QStringLiteral("draw.setSettings")) {
        const int stabilizer = params.value(QStringLiteral("stabilizer"))
                                   .toInt(params.value(QStringLiteral("smooth")).toInt(m_business->toolSmoothValue()));
        const int simplify = params.value(QStringLiteral("simplify"))
                                 .toInt(m_business->strokeFitSettings().simplify);
        const int corner = params.value(QStringLiteral("corner"))
                               .toInt(m_business->strokeFitSettings().corner);
        m_business->setDrawSettings(stabilizer, simplify, corner);
        return buildDrawSettingsState();
    }

    if (method == QStringLiteral("fill.setScope")) {
        bool allLayers = false;
        if (params.contains(QStringLiteral("allLayers"))) {
            allLayers = params.value(QStringLiteral("allLayers")).toBool();
        } else {
            const QJsonValue scopeVal = params.value(QStringLiteral("scope"));
            if (scopeVal.isBool()) {
                allLayers = scopeVal.toBool();
            } else if (scopeVal.isDouble()) {
                allLayers = (scopeVal.toInt(0) == int(AnimeanBusiness::FillScope::AllLayers));
            } else {
                const QString s = scopeVal.toString().trimmed().toLower();
                allLayers = (s == QStringLiteral("all") || s == QStringLiteral("alllayers") || s == QStringLiteral("all_layers"));
            }
        }
        m_business->setFillAllLayers(allLayers);
        return QJsonObject{
            {QStringLiteral("fillScope"), m_business->toolFillAllLayers() ? QStringLiteral("all") : QStringLiteral("current")},
            {QStringLiteral("fillAllLayers"), m_business->toolFillAllLayers()},
        };
    }

    if (method == QStringLiteral("asset.add")) {
        AnimeColumnType colType = AnimeColumnType::Vector;
        const QJsonValue typeVal = params.value(QStringLiteral("type"));
        if (typeVal.isDouble()) {
            colType = static_cast<AnimeColumnType>(typeVal.toInt(int(AnimeColumnType::Vector)));
        } else if (typeVal.isString() && typeVal.toString().trimmed().compare(QStringLiteral("raster"), Qt::CaseInsensitive) == 0) {
            colType = AnimeColumnType::Raster;
        }
        const QString name = params.value(QStringLiteral("name")).toString();
        const int assetIndex = m_business->addAsset(colType, name, view);
        return QJsonObject{{QStringLiteral("asset"), assetIndex}};
    }

    if (method == QStringLiteral("asset.select")) {
        const int assetIndex = params.value(QStringLiteral("assetIndex"))
                                   .toInt(params.value(QStringLiteral("asset"))
                                              .toInt(params.value(QStringLiteral("index")).toInt(-1)));
        m_business->selectAsset(assetIndex, view);
        return QJsonObject{{QStringLiteral("selected"), m_business->currentAsset(view)}};
    }

    if (method == QStringLiteral("asset.unselect")) {
        m_business->unselectAsset(view);
        return QJsonObject{{QStringLiteral("selected"), m_business->currentAsset(view)}};
    }

    if (method == QStringLiteral("asset.addLayer")) {
        const int assetIndex = params.value(QStringLiteral("assetIndex"))
                                   .toInt(params.value(QStringLiteral("asset"))
                                              .toInt(params.value(QStringLiteral("index"))
                                                         .toInt(m_business->currentAsset(view))));
        const int layerIndex = m_business->addLayerForAsset(assetIndex, view);
        return QJsonObject{{QStringLiteral("layer"), layerIndex}};
    }

    if (method == QStringLiteral("asset.assign")) {
        const int layerIndex = params.value(QStringLiteral("layerIndex"))
                                   .toInt(params.value(QStringLiteral("layer")).toInt(m_business->currentLayer(view)));
        const int assetIndex = params.value(QStringLiteral("assetIndex"))
                                   .toInt(params.value(QStringLiteral("asset")).toInt(m_business->currentAsset(view)));
        const bool assigned = m_business->assignAssetToLayer(layerIndex, assetIndex, view);
        return QJsonObject{{QStringLiteral("assigned"), assigned}};
    }

    if (method == QStringLiteral("history.undo")) {
        const QString explicitView = params.value(QStringLiteral("view"))
                                         .toString(params.value(QStringLiteral("viewName")).toString());
        const bool ok = m_business->undo(explicitView);
        return QJsonObject{{QStringLiteral("undone"), ok}, {QStringLiteral("canUndo"), m_business->canUndo()}};
    }

    if (method == QStringLiteral("history.redo")) {
        const QString explicitView = params.value(QStringLiteral("view"))
                                         .toString(params.value(QStringLiteral("viewName")).toString());
        const bool ok = m_business->redo(explicitView);
        return QJsonObject{{QStringLiteral("redone"), ok}, {QStringLiteral("canRedo"), m_business->canRedo()}};
    }

    if (method == QStringLiteral("history.goTo")) {
        const int row = params.value(QStringLiteral("row"))
                            .toInt(params.value(QStringLiteral("index")).toInt(-1));
        const QString explicitView = params.value(QStringLiteral("view"))
                                         .toString(params.value(QStringLiteral("viewName")).toString());
        const bool ok = m_business->goToHistory(row, explicitView);
        return QJsonObject{
            {QStringLiteral("moved"), ok},
            {QStringLiteral("currentIndex"), m_business->currentHistoryIndex(explicitView)},
        };
    }

    if (method == QStringLiteral("frame.select")) {
        const int frameIndex = params.value(QStringLiteral("frameIndex"))
                                   .toInt(params.value(QStringLiteral("frame"))
                                              .toInt(params.value(QStringLiteral("index")).toInt(0)));
        m_business->selectFrame(frameIndex);
        return QJsonObject{{QStringLiteral("currentFrame"), m_business->currentFrame()}};
    }

    if (method == QStringLiteral("frame.step")) {
        const int delta = params.value(QStringLiteral("delta")).toInt(1);
        m_business->stepFrame(delta);
        return QJsonObject{{QStringLiteral("currentFrame"), m_business->currentFrame()}};
    }

    if (method == QStringLiteral("frame.add")) {
        const int frameIndex = m_business->addFrame();
        return QJsonObject{{QStringLiteral("frame"), frameIndex}};
    }

    if (method == QStringLiteral("frame.addHold")) {
        const int frameIndex = m_business->addHoldFrame();
        return QJsonObject{{QStringLiteral("frame"), frameIndex}};
    }

    if (method == QStringLiteral("frame.duplicate")) {
        const int frameIndex = m_business->duplicateFrame();
        return QJsonObject{{QStringLiteral("frame"), frameIndex}};
    }

    if (method == QStringLiteral("frame.delete")) {
        const bool deleted = m_business->deleteFrame();
        return QJsonObject{{QStringLiteral("deleted"), deleted}};
    }

    if (method == QStringLiteral("frame.move")) {
        const int fromIndex = params.value(QStringLiteral("fromIndex"))
                                  .toInt(params.value(QStringLiteral("from")).toInt(-1));
        const int toIndex = params.value(QStringLiteral("toIndex"))
                                .toInt(params.value(QStringLiteral("to")).toInt(-1));
        const bool moved = m_business->moveFrame(fromIndex, toIndex);
        return QJsonObject{{QStringLiteral("moved"), moved}};
    }

    if (method == QStringLiteral("frame.setFps")) {
        const int fps = params.value(QStringLiteral("fps")).toInt(12);
        m_business->setFps(fps);
        return QJsonObject{{QStringLiteral("fps"), m_business->playbackFps()}};
    }

    if (method == QStringLiteral("playback.start")) {
        m_business->startPlayback();
        return QJsonObject{{QStringLiteral("playbackActive"), m_business->isPlaybackActive()}};
    }

    if (method == QStringLiteral("playback.stop")) {
        m_business->stopPlayback();
        return QJsonObject{{QStringLiteral("playbackActive"), m_business->isPlaybackActive()}};
    }

    if (method == QStringLiteral("playback.setLoop")) {
        const bool loop = params.value(QStringLiteral("loop"))
                              .toBool(params.value(QStringLiteral("on")).toBool(true));
        m_business->setPlaybackLoop(loop);
        return QJsonObject{{QStringLiteral("loop"), m_business->isPlaybackLoop()}};
    }

    if (method == QStringLiteral("onion.setEnabled")) {
        const bool on = params.value(QStringLiteral("on"))
                            .toBool(params.value(QStringLiteral("enabled")).toBool(false));
        m_business->setOnionEnabled(on);
        return QJsonObject{{QStringLiteral("onionEnabled"), m_business->isOnionEnabled()}};
    }

    if (method == QStringLiteral("onion.setLines")) {
        const bool on = params.value(QStringLiteral("on"))
                            .toBool(params.value(QStringLiteral("lines")).toBool(true));
        m_business->setOnionLines(on);
        return QJsonObject{{QStringLiteral("onionLines"), m_business->isOnionLines()}};
    }

    if (method == QStringLiteral("onion.setFills")) {
        const bool on = params.value(QStringLiteral("on"))
                            .toBool(params.value(QStringLiteral("fills")).toBool(true));
        m_business->setOnionFills(on);
        return QJsonObject{{QStringLiteral("onionFills"), m_business->isOnionFills()}};
    }

    if (method == QStringLiteral("onion.setAmLayers")) {
        const bool on = params.value(QStringLiteral("on"))
                            .toBool(params.value(QStringLiteral("amLayers")).toBool(true));
        m_business->setOnionAmLayers(on);
        return QJsonObject{{QStringLiteral("onionAmLayers"), m_business->isOnionAmLayers()}};
    }

    if (method == QStringLiteral("onion.setGuideLines")) {
        const bool on = params.value(QStringLiteral("on"))
                            .toBool(params.value(QStringLiteral("guideLines")).toBool(false));
        m_business->setOnionGuideLines(on);
        return QJsonObject{{QStringLiteral("onionGuideLines"), m_business->isOnionGuideLines()}};
    }

    if (method == QStringLiteral("onion.toggleFrame")) {
        const int frame = params.value(QStringLiteral("frame"))
                              .toInt(params.value(QStringLiteral("frameIndex")).toInt(0));
        const bool currentlyOn = m_business->onionFrames().contains(frame);
        const bool on = params.contains(QStringLiteral("on"))
                            ? params.value(QStringLiteral("on")).toBool()
                            : !currentlyOn;
        m_business->toggleOnionFrame(frame, on);
        return QJsonObject{{QStringLiteral("frame"), frame}, {QStringLiteral("on"), on}};
    }

    if (method == QStringLiteral("view.setActive")) {
        m_business->setActiveViewName(view);
        return QJsonObject{{QStringLiteral("activeView"), m_business->activeViewName()}};
    }

    if (method == QStringLiteral("menu.trigger")) {
        const QString id = params.value(QStringLiteral("id")).toString();
        QString error;
        if (!triggerMenuById(id, params, &error)) {
            return makeError(error.isEmpty() ? QStringLiteral("Menu trigger failed") : error);
        }
        return QJsonObject{{QStringLiteral("triggered"), true}};
    }

    if (method == QStringLiteral("pad.event")) {
        const QString pad = params.value(QStringLiteral("pad")).toString(QStringLiteral("force_pad"));
        const QString phase = params.value(QStringLiteral("phase")).toString(QStringLiteral("move"));
        const double x = params.value(QStringLiteral("x")).toDouble(0.0);
        const double y = params.value(QStringLiteral("y")).toDouble(0.0);
        m_padValues.insert(pad, QPointF(x, y));
        if (pad == QStringLiteral("force_pad")) {
            m_forcePadValue = QPointF(x, y);
        }
        markDirty(QStringLiteral("pad"));
        m_business->sendPadEvent(pad, phase, x, y);
        return QJsonObject{{QStringLiteral("pad"), pad}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}};
    }

    if (method == QStringLiteral("theme.set")) {
        const QJsonValue modeVal = params.contains(QStringLiteral("mode"))
                                       ? params.value(QStringLiteral("mode"))
                                       : params.value(QStringLiteral("theme"));
        AnimeTheme::Mode targetMode = AnimeTheme::Mode::Dark;
        if (modeVal.isDouble()) {
            targetMode = modeVal.toInt(0) == int(AnimeTheme::Mode::Light)
                             ? AnimeTheme::Mode::Light
                             : AnimeTheme::Mode::Dark;
        } else {
            const QString s = modeVal.toString().trimmed().toLower();
            targetMode = (s == QStringLiteral("light")) ? AnimeTheme::Mode::Light : AnimeTheme::Mode::Dark;
        }
        AnimeTheme::setMode(targetMode);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(QStringLiteral("theme"));
        markDirty(QStringLiteral("menus"));
        return buildThemeState();
    }

    if (method == QStringLiteral("python.run")) {
        const QString command = params.value(QStringLiteral("command"))
                                    .toString(params.value(QStringLiteral("code"))
                                                  .toString(params.value(QStringLiteral("script")).toString()));
        const QString scriptDir = params.value(QStringLiteral("scriptDir")).toString();
        const QString output = m_business->runEmbeddedPythonCommand(command, scriptDir);
        appendPythonDebug(QStringLiteral("> %1\n%2").arg(command.trimmed(), output.trimmed()));
        return QJsonObject{{QStringLiteral("output"), output}};
    }

    if (method == QStringLiteral("board.resetView")) {
        PaintOpenGLWidget *board = boardForView(view);
        if (!board) {
            return makeError(QStringLiteral("Board not found"));
        }
        if (board->viewName() == QStringLiteral("child")) {
            if (!board->fitViewToContent(true)) {
                board->setViewTransform(1.0, QPointF(0.0, 0.0));
            }
        } else {
            board->setViewTransform(1.0, QPointF(0.0, 0.0));
        }
        return QJsonObject{{QStringLiteral("view"), board->viewName()}};
    }

    if (method == QStringLiteral("board.scroll")) {
        PaintOpenGLWidget *board = boardForView(view);
        if (!board) {
            return makeError(QStringLiteral("Board not found"));
        }
        const QString axis = params.value(QStringLiteral("axis")).toString().trimmed().toLower();
        if (axis != QStringLiteral("h") && axis != QStringLiteral("v")) {
            return makeError(QStringLiteral("Invalid axis: %1").arg(axis));
        }
        const int value = int(std::lround(params.value(QStringLiteral("value")).toDouble(0.0)));
        const QJsonObject vp = buildViewportState(board->viewName());
        const int curH = vp.value(QStringLiteral("h")).toObject().value(QStringLiteral("value")).toInt(
            int(std::lround(-board->panOffset().x())));
        const int curV = vp.value(QStringLiteral("v")).toObject().value(QStringLiteral("value")).toInt(
            int(std::lround(-board->panOffset().y())));
        const int nextH = (axis == QStringLiteral("h")) ? value : curH;
        const int nextV = (axis == QStringLiteral("v")) ? value : curV;
        board->setScrollPosition(nextH, nextV);
        markDirty(board->viewName() == QStringLiteral("child") ? QStringLiteral("viewport/child")
                                                               : QStringLiteral("viewport/main"));
        return QJsonObject{
            {QStringLiteral("view"), board->viewName()},
            {QStringLiteral("h"), int(std::lround(-board->panOffset().x()))},
            {QStringLiteral("v"), int(std::lround(-board->panOffset().y()))},
        };
    }

    if (method == QStringLiteral("timeline.setVisible")) {
        m_timelineVisible = params.contains(QStringLiteral("visible"))
                                ? params.value(QStringLiteral("visible")).toBool()
                                : params.value(QStringLiteral("on")).toBool(!m_timelineVisible);
        saveTimelineSettings();
        markDirty(QStringLiteral("windows"));
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("menus"));
        return QJsonObject{{QStringLiteral("visible"), m_timelineVisible}};
    }

    if (method == QStringLiteral("timeline.setCollapsed")) {
        m_timelineCollapsed = params.contains(QStringLiteral("collapsed"))
                                  ? params.value(QStringLiteral("collapsed")).toBool()
                                  : params.value(QStringLiteral("on")).toBool(!m_timelineCollapsed);
        saveTimelineSettings();
        markDirty(QStringLiteral("windows"));
        markDirty(QStringLiteral("timeline"));
        return QJsonObject{{QStringLiteral("collapsed"), m_timelineCollapsed}};
    }

    if (method == QStringLiteral("timeline.setFloating")) {
        m_timelineFloating = params.contains(QStringLiteral("floating"))
                                 ? params.value(QStringLiteral("floating")).toBool()
                                 : params.value(QStringLiteral("on")).toBool(!m_timelineFloating);
        saveTimelineSettings();
        markDirty(QStringLiteral("windows"));
        markDirty(QStringLiteral("timeline"));
        return QJsonObject{{QStringLiteral("floating"), m_timelineFloating}};
    }

    if (method == QStringLiteral("timeline.setDockArea") || method == QStringLiteral("timeline.setLayout")) {
        if (params.contains(QStringLiteral("dockArea")) || params.contains(QStringLiteral("area"))) {
            const QString area = params.value(QStringLiteral("dockArea"))
                                     .toString(params.value(QStringLiteral("area")).toString())
                                     .trimmed()
                                     .toLower();
            if (area == QStringLiteral("left") || area == QStringLiteral("right") || area == QStringLiteral("bottom")) {
                m_timelineDockArea = area;
            }
        }
        if (params.contains(QStringLiteral("visible"))) {
            m_timelineVisible = params.value(QStringLiteral("visible")).toBool();
        }
        if (params.contains(QStringLiteral("collapsed"))) {
            m_timelineCollapsed = params.value(QStringLiteral("collapsed")).toBool();
        }
        if (params.contains(QStringLiteral("floating"))) {
            m_timelineFloating = params.value(QStringLiteral("floating")).toBool();
        }
        saveTimelineSettings();
        markDirty(QStringLiteral("windows"));
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("menus"));
        return QJsonObject{
            {QStringLiteral("visible"), m_timelineVisible},
            {QStringLiteral("collapsed"), m_timelineCollapsed},
            {QStringLiteral("floating"), m_timelineFloating},
            {QStringLiteral("dockArea"), m_timelineDockArea},
        };
    }

    return makeError(QStringLiteral("Unknown method: %1").arg(method));
}

QJsonObject BackendSession::handleFileImportCall(const QJsonObject &params)
{
    auto makeError = [](const QString &err) {
        return QJsonObject{
            {QStringLiteral("__error__"), true},
            {QStringLiteral("error"), err},
        };
    };

    const QString rawKind = params.value(QStringLiteral("kind"))
                                .toString(params.value(QStringLiteral("type")).toString())
                                .trimmed();
    QString normKind = rawKind.toLower();

    bool isTextureImport = false;
    if (normKind.startsWith(QStringLiteral("file.texture.import."))) {
        normKind = normKind.mid(QStringLiteral("file.texture.import.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(QStringLiteral("texture.import."))) {
        normKind = normKind.mid(QStringLiteral("texture.import.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(QStringLiteral("file.texture."))) {
        normKind = normKind.mid(QStringLiteral("file.texture.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(QStringLiteral("texture."))) {
        normKind = normKind.mid(QStringLiteral("texture.").size());
        isTextureImport = true;
    } else if (normKind.startsWith(QStringLiteral("file.import."))) {
        normKind = normKind.mid(QStringLiteral("file.import.").size());
    } else if (normKind.startsWith(QStringLiteral("import."))) {
        normKind = normKind.mid(QStringLiteral("import.").size());
    }

    const QString requestedView = params.value(QStringLiteral("view"))
                                      .toString(params.value(QStringLiteral("viewName")).toString());
    if (isTextureImport || requestedView == QStringLiteral("child")) {
        surfaceTextureView();
    }
    const QString targetView = (isTextureImport || requestedView == QStringLiteral("child"))
                                   ? QStringLiteral("child")
                                   : resolveViewName(requestedView.isEmpty() ? QStringLiteral("main") : requestedView);
    PaintOpenGLWidget *board = boardForView(targetView);
    if (!board) {
        return makeError(QStringLiteral("Board not found"));
    }

    const QStringList paths = extractPathsParam(params);
    if (paths.isEmpty()) {
        return makeError(QStringLiteral("paths is required"));
    }

    const bool fitToCanvas = params.value(QStringLiteral("fitToCanvas"))
                                 .toBool(params.value(QStringLiteral("scaleToCanvas")).toBool(true));

    QJsonArray importedLayers;

    if (normKind == QStringLiteral("raster") || normKind == QStringLiteral("image")) {
        for (const QString &fileName : paths) {
            QImageReader reader(fileName);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (image.isNull()) {
                return makeError(QStringLiteral("Failed to load image:\n%1").arg(reader.errorString()));
            }
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const QFileInfo fileInfo(fileName);
            const int layerIndex = board->importRasterLayer(image, fileInfo.completeBaseName());
            if (layerIndex < 0) {
                return makeError(QStringLiteral("Failed to import raster layer."));
            }
            importedLayers.append(layerIndex);
            m_business->updateAttention(targetView,
                                        AttentionChange::LayerChange,
                                        board->model().currentFrame(),
                                        layerIndex,
                                        board->model().currentAsset());
            m_business->setStatusMessage(QStringLiteral("Imported raster into %1: %2 (%3 x %4)")
                                             .arg(board->viewName())
                                             .arg(fileInfo.fileName())
                                             .arg(image.width())
                                             .arg(image.height()));
        }
    } else if (normKind == QStringLiteral("opentoonz")
               || normKind == QStringLiteral("opentoonzlines")
               || normKind == QStringLiteral("opentoonz_lines")
               || normKind == QStringLiteral("pli")
               || normKind == QStringLiteral("toonz")) {
#ifndef ANIMEAN_WITH_PYTHON
        return makeError(QStringLiteral("Python support is disabled, so OpenToonz line import is unavailable."));
#else
        for (const QString &fileName : paths) {
            try {
                py::gil_scoped_acquire acquire;
                py::module_ toonzTool = py::module_::import("toonz_to_dict");
                py::object level = toonzTool.attr("read_vector_level")(QDir::toNativeSeparators(fileName).toStdString());
                py::dict levelData = level.attr("to_dict")().cast<py::dict>();
                QVector<PaintOpenGLWidget::ImportedVectorFrame> frames = importedFramesFromOpenToonzLevel(levelData);
                if (frames.isEmpty()) {
                    return makeError(QStringLiteral("No strokes were found in this .pli file."));
                }
                const QRectF sourceBounds = importedVectorFramesBounds(frames);
                const QSizeF canvasSize = board->documentRect().size();
                const QRectF canvasBounds(QPointF(0.0, 0.0), canvasSize);
                const bool needsScale = sourceBounds.isValid()
                                        && sourceBounds.width() > 0.0
                                        && sourceBounds.height() > 0.0
                                        && (!canvasBounds.contains(sourceBounds)
                                            || !qFuzzyCompare(sourceBounds.width(), canvasSize.width())
                                            || !qFuzzyCompare(sourceBounds.height(), canvasSize.height()));
                if (needsScale && fitToCanvas) {
                    scaleImportedVectorFramesToCanvas(&frames, canvasSize);
                }
                const QFileInfo fileInfo(fileName);
                const int layerIndex = board->importVectorLineLayer(frames, fileInfo.completeBaseName());
                if (layerIndex < 0) {
                    return makeError(QStringLiteral("Failed to import OpenToonz line layer."));
                }
                importedLayers.append(layerIndex);
                m_business->updateAttention(targetView,
                                            AttentionChange::LayerChange,
                                            board->model().currentFrame(),
                                            layerIndex,
                                            board->model().currentAsset());
                m_business->setStatusMessage(QStringLiteral("Imported OpenToonz lines into %1: %2 (%3 frame(s))")
                                                 .arg(board->viewName())
                                                 .arg(fileInfo.fileName())
                                                 .arg(frames.size()));
            } catch (const py::error_already_set &error) {
                return makeError(QStringLiteral("OpenToonz import failed:\n%1").arg(QString::fromUtf8(error.what())));
            }
        }
#endif
    } else if (normKind == QStringLiteral("csp")
               || normKind == QStringLiteral("clip")
               || normKind == QStringLiteral("clipstudio")
               || normKind == QStringLiteral("clipstudiopaint")
               || normKind == QStringLiteral("clip_studio_paint")) {
        for (const QString &fileName : paths) {
            const ClipReader::ClipDocument document = ClipReader::readClipFile(fileName);
            if (!document.valid) {
                return makeError(document.error.isEmpty()
                                     ? QStringLiteral("Unable to read this .clip file.")
                                     : document.error);
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
                return makeError(QStringLiteral("No usable vector strokes were found in this .clip file."));
            }
            frames.append(frame);
            const QRectF sourceBounds = importedVectorFramesBounds(frames);
            const QSizeF canvasSize = board->documentRect().size();
            const QRectF canvasBounds(QPointF(0.0, 0.0), canvasSize);
            const bool needsScale = sourceBounds.isValid()
                                    && sourceBounds.width() > 0.0
                                    && sourceBounds.height() > 0.0
                                    && (!canvasBounds.contains(sourceBounds)
                                        || !qFuzzyCompare(sourceBounds.width(), canvasSize.width())
                                        || !qFuzzyCompare(sourceBounds.height(), canvasSize.height()));
            if (needsScale && fitToCanvas) {
                scaleImportedVectorFramesToCanvas(&frames, canvasSize);
            }
            const QFileInfo fileInfo(fileName);
            const int layerIndex = board->importVectorLineLayer(frames, fileInfo.completeBaseName());
            if (layerIndex < 0) {
                return makeError(QStringLiteral("Failed to import the Clip Studio Paint vector layer."));
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
            m_business->setStatusMessage(QStringLiteral("Imported Clip Studio Paint into %1: %2 (%3 stroke(s))")
                                             .arg(board->viewName())
                                             .arg(fileInfo.fileName())
                                             .arg(strokeCount));
        }
    } else {
        return makeError(QStringLiteral("Unknown import kind: %1").arg(rawKind));
    }

    markDirty(targetView == QStringLiteral("child") ? QStringLiteral("layers/child")
                                                    : QStringLiteral("layers/main"));
    markDirty(targetView == QStringLiteral("child") ? QStringLiteral("assets/child")
                                                    : QStringLiteral("assets/main"));
    markDirty(QStringLiteral("timeline"));
    markDirty(QStringLiteral("history"));
    markDirty(QStringLiteral("menus"));
    markDirty(QStringLiteral("window"));

    return QJsonObject{
        {QStringLiteral("kind"), normKind},
        {QStringLiteral("view"), targetView},
        {QStringLiteral("layers"), importedLayers},
        {QStringLiteral("layer"), importedLayers.isEmpty() ? -1 : importedLayers.last().toInt(-1)},
        {QStringLiteral("count"), importedLayers.size()},
    };
}

QJsonObject BackendSession::handleFileTextureCall(const QJsonObject &params)
{
    auto makeError = [](const QString &err) {
        return QJsonObject{
            {QStringLiteral("__error__"), true},
            {QStringLiteral("error"), err},
        };
    };

    const QString rawKind = params.value(QStringLiteral("kind"))
                                .toString(params.value(QStringLiteral("action")).toString())
                                .trimmed();
    QString normKind = rawKind.toLower();
    if (normKind.startsWith(QStringLiteral("file.texture."))) {
        normKind = normKind.mid(QStringLiteral("file.texture.").size());
    } else if (normKind.startsWith(QStringLiteral("texture."))) {
        normKind = normKind.mid(QStringLiteral("texture.").size());
    }

    if (normKind.startsWith(QStringLiteral("import."))
        || normKind == QStringLiteral("raster")
        || normKind == QStringLiteral("image")
        || normKind == QStringLiteral("opentoonz")
        || normKind == QStringLiteral("pli")
        || normKind == QStringLiteral("csp")
        || normKind == QStringLiteral("clip")) {
        QJsonObject forwarded = params;
        forwarded.insert(QStringLiteral("view"), QStringLiteral("child"));
        forwarded.insert(QStringLiteral("kind"), normKind);
        return handleFileImportCall(forwarded);
    }

    const QStringList paths = extractPathsParam(params);
    QString fileName = paths.isEmpty() ? QString() : paths.first();

    if (normKind == QStringLiteral("open") || normKind == QStringLiteral("opentextureview")) {
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        QString error;
        if (!m_business->openTextureViewFrom(fileName, &error)) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to open texture view") : error);
        }
        surfaceTextureView();
        return QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("open")},
            {QStringLiteral("path"), m_business->childFilePath()},
            {QStringLiteral("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == QStringLiteral("save") || normKind == QStringLiteral("savetextureview")) {
        if (fileName.isEmpty()) {
            const QString current = m_business->childFilePath();
            if (!current.isEmpty()
                && QFileInfo(current).suffix().compare(QStringLiteral("textureview"), Qt::CaseInsensitive) == 0) {
                fileName = current;
            }
        }
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        QString error;
        if (!m_business->saveTextureViewTo(fileName, &error)) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to save texture view") : error);
        }
        return QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("save")},
            {QStringLiteral("path"), m_business->childFilePath()},
            {QStringLiteral("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == QStringLiteral("saveas")
        || normKind == QStringLiteral("save_as")
        || normKind == QStringLiteral("savetextureviewas")) {
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        QString error;
        if (!m_business->saveTextureViewTo(fileName, &error)) {
            return makeError(error.isEmpty() ? QStringLiteral("Failed to save texture view") : error);
        }
        return QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("saveAs")},
            {QStringLiteral("path"), m_business->childFilePath()},
            {QStringLiteral("fileName"), m_business->childFilePath()},
        };
    }

    if (normKind == QStringLiteral("exportimage")
        || normKind == QStringLiteral("export_image")
        || normKind == QStringLiteral("export")
        || normKind == QStringLiteral("exporttextureimage")
        || normKind == QStringLiteral("image")
        || normKind == QStringLiteral("png")) {
        if (fileName.isEmpty()) {
            return makeError(QStringLiteral("path is required"));
        }
        if (!m_childBoard) {
            return makeError(QStringLiteral("Texture board not found"));
        }
        if (QFileInfo(fileName).suffix().isEmpty()) {
            fileName += QStringLiteral(".png");
        }
        QSize exportSize = m_childBoard->size();
        if (exportSize.width() <= 0 || exportSize.height() <= 0) {
            exportSize = m_childBoard->model().canvasSize();
        }
        const QImage image = m_childBoard->renderFrameThumbnail(m_childBoard->model().currentFrame(), exportSize);
        if (image.isNull()) {
            return makeError(QStringLiteral("The texture view could not be captured."));
        }
        if (!image.save(fileName, "PNG")) {
            return makeError(QStringLiteral("Failed to write the image to:\n%1").arg(fileName));
        }
        m_business->setStatusMessage(QStringLiteral("Exported texture image: %1 (%2 x %3)")
                                         .arg(QFileInfo(fileName).fileName())
                                         .arg(image.width())
                                         .arg(image.height()));
        return QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("exportImage")},
            {QStringLiteral("path"), fileName},
            {QStringLiteral("fileName"), fileName},
            {QStringLiteral("width"), image.width()},
            {QStringLiteral("height"), image.height()},
        };
    }

    return makeError(QStringLiteral("Unknown texture kind: %1").arg(rawKind));
}

bool BackendSession::triggerMenuById(const QString &id, const QJsonObject &params, QString *errorOut)
{
    // Direct (menuName, name) script menu call if provided in params
    if (params.contains(QStringLiteral("menuName")) || params.contains(QStringLiteral("menu"))) {
        const QString menuName = params.value(QStringLiteral("menuName"))
                                     .toString(params.value(QStringLiteral("menu")).toString());
        const QString itemName = params.value(QStringLiteral("name"))
                                     .toString(params.value(QStringLiteral("itemName")).toString(id));
        const QString host = params.value(QStringLiteral("host"))
                                 .toString(params.value(QStringLiteral("view")).toString(QStringLiteral("main")));
        const bool checked = params.value(QStringLiteral("checked")).toBool(false);
        PaintOpenGLWidget *owner = boardForView(host);
        if (owner) {
            owner->sendPythonMenuMessage(menuName, itemName, checked);
            markDirty(QStringLiteral("menus"));
            markDirty(QStringLiteral("toolOptions"));
            markDirty(QStringLiteral("timeline"));
            markDirty(QStringLiteral("layers/main"));
            markDirty(QStringLiteral("layers/child"));
            markDirty(QStringLiteral("assets/main"));
            markDirty(QStringLiteral("assets/child"));
            return true;
        }
    }

    if (id == QStringLiteral("file.new")) {
        emitUiEvent(QStringLiteral("dialog.canvasSize"),
                    QJsonObject{
                        {QStringLiteral("width"), m_business->canvasSize().width()},
                        {QStringLiteral("height"), m_business->canvasSize().height()},
                    });
        return true;
    }
    if (id == QStringLiteral("file.open")) {
        emitUiEvent(QStringLiteral("dialog.openProject"));
        return true;
    }
    if (id == QStringLiteral("file.save")) {
        if (m_business->currentFilePath().isEmpty()
            || QFileInfo(m_business->currentFilePath()).suffix().compare(QStringLiteral("anproj"), Qt::CaseInsensitive) != 0) {
            emitUiEvent(QStringLiteral("dialog.saveProjectAs"));
            return true;
        }
        return m_business->saveProjectTo(m_business->currentFilePath(), errorOut);
    }
    if (id == QStringLiteral("file.saveAs")) {
        emitUiEvent(QStringLiteral("dialog.saveProjectAs"));
        return true;
    }
    if (id == QStringLiteral("file.texture.save")) {
        if (!m_business->childFilePath().isEmpty()
            && QFileInfo(m_business->childFilePath()).suffix().compare(QStringLiteral("textureview"), Qt::CaseInsensitive) == 0) {
            return m_business->saveTextureViewTo(m_business->childFilePath(), errorOut);
        }
        emitUiEvent(QStringLiteral("menu.action"), QJsonObject{{QStringLiteral("id"), id}});
        return true;
    }
    if (id.startsWith(QStringLiteral("file.import."))
        || id.startsWith(QStringLiteral("file.texture."))) {
        emitUiEvent(QStringLiteral("menu.action"), QJsonObject{{QStringLiteral("id"), id}});
        return true;
    }
    if (id == QStringLiteral("edit.undo")) {
        m_business->undo();
        return true;
    }
    if (id == QStringLiteral("edit.redo")) {
        m_business->redo();
        return true;
    }
    if (id == QStringLiteral("theme.dark")) {
        AnimeTheme::setMode(AnimeTheme::Mode::Dark);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(QStringLiteral("theme"));
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("theme.light")) {
        AnimeTheme::setMode(AnimeTheme::Mode::Light);
#ifdef ANIMEAN_WITH_PYTHON
        setAnimeanUiTheme(AnimeTheme::modeName(AnimeTheme::mode()));
#endif
        markDirty(QStringLiteral("theme"));
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("windows.texture_view")) {
        m_textureSubControlVisible = true;
        emitUiEvent(QStringLiteral("textureSubControl.surface"));
        markDirty(QStringLiteral("windows"));
        return true;
    }
    if (id == QStringLiteral("windows.timeline")) {
        m_timelineVisible = params.contains(QStringLiteral("checked"))
                                ? params.value(QStringLiteral("checked")).toBool()
                                : !m_timelineVisible;
        saveTimelineSettings();
        markDirty(QStringLiteral("windows"));
        markDirty(QStringLiteral("timeline"));
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id.startsWith(QStringLiteral("windows."))) {
        const QString winName = id.mid(QStringLiteral("windows.").size());
        for (AnimeanWindowInfo &info : m_windowInfos) {
            if (info.name == winName) {
                info.visible = params.contains(QStringLiteral("checked"))
                                   ? params.value(QStringLiteral("checked")).toBool()
                                   : !info.visible;
                markDirty(QStringLiteral("windows"));
                markDirty(QStringLiteral("menus"));
                return true;
            }
        }
    }
    if (id == QStringLiteral("child.setting.changeableTimeline")) {
        const bool next = params.contains(QStringLiteral("checked"))
                              ? params.value(QStringLiteral("checked")).toBool()
                              : !m_business->changeableTimeline();
        m_business->setChangeableTimeline(next);
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("child.setting.changeableTexture")) {
        const bool next = params.contains(QStringLiteral("checked"))
                              ? params.value(QStringLiteral("checked")).toBool()
                              : !m_business->changeableTexture();
        m_business->setChangeableTexture(next);
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("child.setting.background.white") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::White);
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("child.setting.background.black") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::Black);
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id == QStringLiteral("child.setting.background.transparent") && m_childBoard) {
        m_childBoard->setBackgroundMode(PaintOpenGLWidget::BackgroundMode::Transparent);
        markDirty(QStringLiteral("menus"));
        return true;
    }
    if (id.startsWith(QStringLiteral("settings:"))) {
        const QString target = id.mid(QStringLiteral("settings:").size());
        openScriptSettings(target, params.value(QStringLiteral("title")).toString(target));
        return true;
    }
    if (id.startsWith(QStringLiteral("script:"))) {
        const QStringList parts = id.split(QLatin1Char(':'));
        if (parts.size() >= 4) {
            const QString host = parts.at(1);
            const QString menuName = parts.at(2);
            const QString itemName = parts.mid(3).join(QLatin1Char(':'));
            const bool checked = params.value(QStringLiteral("checked")).toBool(false);
            PaintOpenGLWidget *owner = boardForView(host);
            if (owner) {
                owner->sendPythonMenuMessage(menuName, itemName, checked);
                markDirty(QStringLiteral("menus"));
                markDirty(QStringLiteral("toolOptions"));
                markDirty(QStringLiteral("timeline"));
                markDirty(QStringLiteral("layers/main"));
                markDirty(QStringLiteral("layers/child"));
                markDirty(QStringLiteral("assets/main"));
                markDirty(QStringLiteral("assets/child"));
                return true;
            }
        }
    }

    if (errorOut) {
        *errorOut = QStringLiteral("Unknown menu id: %1").arg(id);
    }
    return false;
}

void BackendSession::openScriptSettings(const QString &name, const QString &title)
{
    QJsonObject layout;
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("settings_layout_json")(name.toStdString())
                                         .cast<std::string>();
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
            layout = doc.object();
        } catch (const py::error_already_set &error) {
            m_business->setStatusMessage(QStringLiteral("settings error: %1").arg(QString::fromUtf8(error.what())));
            return;
        }
    }
#endif
    emitUiEvent(QStringLiteral("settings.open"),
                QJsonObject{
                    {QStringLiteral("name"), name},
                    {QStringLiteral("title"), title.isEmpty() ? name : title},
                    {QStringLiteral("layout"), layout},
                });
}

void BackendSession::emitUiEvent(const QString &name, const QJsonObject &args)
{
    m_lastEvent = QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("args"), args},
    };
    const QJsonObject packet{
        {QStringLiteral("t"), QStringLiteral("state")},
        {QStringLiteral("key"), QStringLiteral("event")},
        {QStringLiteral("value"), m_lastEvent},
    };
    sendJson(packet);
}

void BackendSession::handleFrameSink(const QString &viewName, const BoardFrame &frame)
{
    m_lastBoardFrameId.insert(viewName, frame.frame);
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    QByteArray payload;
    payload.reserve(frame.ops.size() + 128);
    payload.append("{\"t\":\"board\",\"view\":\"");
    payload.append(viewName.toUtf8());
    payload.append("\",\"frame\":");
    payload.append(QByteArray::number(static_cast<qulonglong>(frame.frame)));
    payload.append(",\"w\":");
    payload.append(QByteArray::number(frame.w));
    payload.append(",\"h\":");
    payload.append(QByteArray::number(frame.h));
    payload.append(",\"keep\":");
    payload.append(QByteArray::number(frame.keep));
    payload.append(",\"ops\":");
    payload.append(frame.ops.isEmpty() ? QByteArrayLiteral("[]") : frame.ops);
    payload.append('}');
    m_ws->sendText(payload);
}

void BackendSession::handleImageSink(const QString &key, const QImage &rgba)
{
    if (key.isEmpty() || rgba.isNull()) {
        return;
    }
    const QImage formatted = (rgba.format() == QImage::Format_RGBA8888)
                                 ? rgba
                                 : rgba.convertToFormat(QImage::Format_RGBA8888);
    m_cachedImages.insert(key, formatted);
    sendImagePacket(key, formatted);
}

void BackendSession::sendImagePacket(const QString &key, const QImage &rgba)
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
    const QJsonObject header{
        {QStringLiteral("t"), QStringLiteral("image")},
        {QStringLiteral("key"), key},
        {QStringLiteral("w"), w},
        {QStringLiteral("h"), h},
        {QStringLiteral("bytes"), expectedBytes},
    };
    sendJson(header);
    if (rgba.bytesPerLine() == w * 4) {
        m_ws->sendBinary(QByteArray(reinterpret_cast<const char *>(rgba.constBits()), expectedBytes));
    } else {
        QByteArray tight;
        tight.resize(expectedBytes);
        char *dst = tight.data();
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst + y * w * 4, rgba.constScanLine(y), static_cast<size_t>(w * 4));
        }
        m_ws->sendBinary(tight);
    }
}

void BackendSession::handleCursorSink(const QString &viewName, const QCursor &cursor)
{
    CursorState state;
    const QPixmap pixmap = cursor.pixmap();
    if (!pixmap.isNull()) {
        const QImage rgba = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
        const QString key = cursorImageKey(rgba);
        state.shape = QStringLiteral("bitmap");
        state.css = QStringLiteral("default");
        state.key = key;
        state.hx = cursor.hotSpot().x();
        state.hy = cursor.hotSpot().y();
        state.w = rgba.width();
        state.h = rgba.height();
        if (!m_cachedImages.contains(key)) {
            m_cachedImages.insert(key, rgba);
            sendImagePacket(key, rgba);
        }
    } else {
        mapQtCursorShape(cursor.shape(), &state.shape, &state.css);
    }
    m_cursors.insert(viewName, state);
    markDirty(viewName == QStringLiteral("child") ? QStringLiteral("cursor/child")
                                                  : QStringLiteral("cursor/main"));
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
        markDirty(QStringLiteral("freeze"));
    }
}

void BackendSession::appendPythonDebug(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    m_pythonDebugLog.append(trimmed);
    while (m_pythonDebugLog.size() > 200) {
        m_pythonDebugLog.removeFirst();
    }
    markDirty(QStringLiteral("window"));
}

void BackendSession::markDirty(const QString &key)
{
    m_dirtyKeys.insert(key);
    scheduleStateFlush();
}

void BackendSession::markAllDirty()
{
    for (const QString &key : allStateKeys()) {
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
    QTimer::singleShot(0, this, &BackendSession::flushDirtyState);
}

void BackendSession::flushDirtyState()
{
    m_stateFlushScheduled = false;
    if (m_dirtyKeys.isEmpty()) {
        return;
    }
    const QSet<QString> toSend = std::move(m_dirtyKeys);
    m_dirtyKeys.clear();

    if (!m_ws || !m_ws->hasClient()) {
        return;
    }

    for (const QString &key : allStateKeys()) {
        if (toSend.contains(key)) {
            sendStateKey(key);
        }
    }
}

void BackendSession::sendStateKey(const QString &key)
{
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    const QJsonObject packet{
        {QStringLiteral("t"), QStringLiteral("state")},
        {QStringLiteral("key"), key},
        {QStringLiteral("value"), buildStateValue(key)},
    };
    sendJson(packet);
}

void BackendSession::sendJson(const QJsonObject &obj)
{
    if (!m_ws || !m_ws->hasClient()) {
        return;
    }
    m_ws->sendText(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonValue BackendSession::buildStateValue(const QString &key) const
{
    if (key == QStringLiteral("window")) {
        return buildWindowState();
    }
    if (key == QStringLiteral("theme")) {
        return buildThemeState();
    }
    if (key == QStringLiteral("menus")) {
        return buildMenusState();
    }
    if (key == QStringLiteral("tools")) {
        return buildToolsState();
    }
    if (key == QStringLiteral("toolOptions")) {
        return buildToolOptionsState();
    }
    if (key == QStringLiteral("drawColor")) {
        return buildDrawColorState();
    }
    if (key == QStringLiteral("drawSettings")) {
        return buildDrawSettingsState();
    }
    if (key == QStringLiteral("layers/main")) {
        return buildLayersState(QStringLiteral("main"));
    }
    if (key == QStringLiteral("layers/child")) {
        return buildLayersState(QStringLiteral("child"));
    }
    if (key == QStringLiteral("assets/main")) {
        return buildAssetsState(QStringLiteral("main"));
    }
    if (key == QStringLiteral("assets/child")) {
        return buildAssetsState(QStringLiteral("child"));
    }
    if (key == QStringLiteral("timeline")) {
        return buildTimelineState();
    }
    if (key == QStringLiteral("history")) {
        return buildHistoryState();
    }
    if (key == QStringLiteral("windows")) {
        return buildWindowsState();
    }
    if (key == QStringLiteral("cursor/main")) {
        return buildCursorState(QStringLiteral("main"));
    }
    if (key == QStringLiteral("cursor/child")) {
        return buildCursorState(QStringLiteral("child"));
    }
    if (key == QStringLiteral("viewport/main")) {
        return buildViewportState(QStringLiteral("main"));
    }
    if (key == QStringLiteral("viewport/child")) {
        return buildViewportState(QStringLiteral("child"));
    }
    if (key == QStringLiteral("freeze")) {
        return m_pythonFreezeDepth > 0;
    }
    if (key == QStringLiteral("pad")) {
        return buildPadState();
    }
    if (key == QStringLiteral("event")) {
        return m_lastEvent;
    }
    return QJsonValue();
}

QJsonObject BackendSession::buildWindowState() const
{
    const QSize mainCanvas = m_business->canvasSize(QStringLiteral("main"));
    QJsonArray debugArr;
    for (const QString &line : m_pythonDebugLog) {
        debugArr.append(line);
    }
    return QJsonObject{
        {QStringLiteral("title"), m_business->windowTitle()},
        {QStringLiteral("status"), m_business->statusMessage()},
        {QStringLiteral("activeView"), m_business->activeViewName()},
        {QStringLiteral("currentFilePath"), m_business->currentFilePath()},
        {QStringLiteral("childFilePath"), m_business->childFilePath()},
        {QStringLiteral("canvasWidth"), mainCanvas.width()},
        {QStringLiteral("canvasHeight"), mainCanvas.height()},
        {QStringLiteral("pythonDebug"), debugArr},
    };
}

QJsonObject BackendSession::buildThemeState() const
{
    auto hex = [](AnimeTheme::Role role) {
        const QColor c = AnimeTheme::color(role);
        return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
    };
    QJsonObject roles{
        {QStringLiteral("window"), hex(AnimeTheme::Role::Window)},
        {QStringLiteral("surface"), hex(AnimeTheme::Role::Surface)},
        {QStringLiteral("surfaceAlt"), hex(AnimeTheme::Role::SurfaceAlt)},
        {QStringLiteral("text"), hex(AnimeTheme::Role::Text)},
        {QStringLiteral("textDim"), hex(AnimeTheme::Role::TextDim)},
        {QStringLiteral("divider"), hex(AnimeTheme::Role::Divider)},
        {QStringLiteral("accent"), hex(AnimeTheme::Role::Accent)},
        {QStringLiteral("accentHover"), hex(AnimeTheme::Role::AccentHover)},
        {QStringLiteral("accentActive"), hex(AnimeTheme::Role::AccentActive)},
        {QStringLiteral("chipRest"), hex(AnimeTheme::Role::ChipRest)},
        {QStringLiteral("chipRestFg"), hex(AnimeTheme::Role::ChipRestFg)},
        {QStringLiteral("chipHover"), hex(AnimeTheme::Role::ChipHover)},
        {QStringLiteral("chipHoverFg"), hex(AnimeTheme::Role::ChipHoverFg)},
        {QStringLiteral("viewportSurround"), hex(AnimeTheme::Role::ViewportSurround)},
        {QStringLiteral("onionPast"), hex(AnimeTheme::Role::OnionPast)},
        {QStringLiteral("onionAhead"), hex(AnimeTheme::Role::OnionAhead)},
    };
    QJsonObject out = roles;
    out.insert(QStringLiteral("mode"), AnimeTheme::modeName(AnimeTheme::mode()));
    out.insert(QStringLiteral("roles"), roles);
    return out;
}

QJsonArray BackendSession::buildScriptMenuItems(const QString &host,
                                                const QString &menuName,
                                                const QJsonArray &items) const
{
    QJsonArray out;
    int sepIndex = 0;
    for (const QJsonValue &val : items) {
        const QJsonObject obj = val.toObject();
        const QString kind = obj.value(QStringLiteral("kind")).toString(QStringLiteral("action"));
        if (kind == QStringLiteral("separator")) {
            out.append(makeMenuItem(QStringLiteral("%1.%2.sep%3").arg(host, menuName).arg(++sepIndex),
                                    QString(),
                                    QString(),
                                    QStringLiteral("separator")));
            continue;
        }
        const QString itemName = obj.value(QStringLiteral("name")).toString();
        const QString title = obj.value(QStringLiteral("title")).toString(itemName);
        const QString shortcut = obj.value(QStringLiteral("shortcut")).toString();
        const bool enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        const bool checked = obj.value(QStringLiteral("checked")).toBool(false);

        if (kind == QStringLiteral("submenu")) {
            const QJsonArray children =
                buildScriptMenuItems(host, menuName, obj.value(QStringLiteral("items")).toArray());
            out.append(makeMenuItem(QStringLiteral("submenu:%1:%2:%3").arg(host, menuName, itemName.isEmpty() ? title : itemName),
                                    title,
                                    shortcut,
                                    QStringLiteral("submenu"),
                                    false,
                                    enabled,
                                    children));
            continue;
        }
        if (kind == QStringLiteral("settings")) {
            const QString target = obj.value(QStringLiteral("settings")).toString(itemName);
            out.append(makeMenuItem(QStringLiteral("settings:%1").arg(target),
                                    title.isEmpty() ? itemName : title,
                                    shortcut,
                                    QStringLiteral("settings"),
                                    false,
                                    enabled));
            continue;
        }
        out.append(makeMenuItem(QStringLiteral("script:%1:%2:%3").arg(host, menuName, itemName),
                                title.isEmpty() ? itemName : title,
                                shortcut,
                                kind,
                                checked,
                                enabled));
    }
    return out;
}

QJsonArray BackendSession::buildMenusState() const
{
    QJsonArray menus;

    // 1. File menu (from mainwindow.ui + createMainTextureMenu + createTextureFileMenu)
    QJsonArray importChildren{
        makeMenuItem(QStringLiteral("file.import.raster"), QStringLiteral("Raster..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.import.opentoonz"), QStringLiteral("OpenToonz Lines..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.import.csp"), QStringLiteral("Clip Studio Paint..."), QString(), QStringLiteral("action")),
    };

    QJsonArray texImportChildren{
        makeMenuItem(QStringLiteral("file.texture.import.raster"), QStringLiteral("Raster..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.import.opentoonz"), QStringLiteral("OpenToonz Lines..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.import.csp"), QStringLiteral("Clip Studio Paint..."), QString(), QStringLiteral("action")),
    };

    const PaintOpenGLWidget::BackgroundMode bgMode =
        m_childBoard ? m_childBoard->backgroundMode() : PaintOpenGLWidget::BackgroundMode::White;
    QJsonArray bgChildren{
        makeMenuItem(QStringLiteral("child.setting.background.white"),
                     QStringLiteral("White"),
                     QString(),
                     QStringLiteral("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::White),
        makeMenuItem(QStringLiteral("child.setting.background.black"),
                     QStringLiteral("Black"),
                     QString(),
                     QStringLiteral("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Black),
        makeMenuItem(QStringLiteral("child.setting.background.transparent"),
                     QStringLiteral("Transparent"),
                     QString(),
                     QStringLiteral("radio"),
                     bgMode == PaintOpenGLWidget::BackgroundMode::Transparent),
    };

    QJsonArray childSettingChildren{
        makeMenuItem(QStringLiteral("child.setting.changeableTimeline"),
                     QStringLiteral("Changable Timeline"),
                     QString(),
                     QStringLiteral("check"),
                     m_business->changeableTimeline()),
        makeMenuItem(QStringLiteral("child.setting.changeableTexture"),
                     QStringLiteral("Changable Texture"),
                     QString(),
                     QStringLiteral("check"),
                     m_business->changeableTexture()),
        makeMenuItem(QStringLiteral("child.setting.sep1"), QString(), QString(), QStringLiteral("separator")),
        makeMenuItem(QStringLiteral("child.setting.background"),
                     QStringLiteral("Background"),
                     QString(),
                     QStringLiteral("submenu"),
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
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
            QHash<QString, QJsonObject> liveChildMenus;
            for (const QJsonValue &val : doc.array()) {
                const QJsonObject obj = val.toObject();
                const QString mName = obj.value(QStringLiteral("name")).toString();
                if (!mName.isEmpty()) {
                    liveChildMenus.insert(mName, obj);
                }
            }
            for (const QPair<QString, QString> &entry : m_childScriptMenus) {
                const QString &mName = entry.first;
                const QJsonObject obj = liveChildMenus.value(mName);
                const QString mTitle = obj.value(QStringLiteral("title")).toString(entry.second);
                const QJsonArray items =
                    buildScriptMenuItems(QStringLiteral("child"), mName, obj.value(QStringLiteral("items")).toArray());
                childSettingChildren.append(makeMenuItem(QStringLiteral("child.menu.%1").arg(mName),
                                                         mTitle,
                                                         QString(),
                                                         QStringLiteral("submenu"),
                                                         false,
                                                         true,
                                                         items));
            }
        } catch (...) {
        }
    }
#endif

    QJsonArray textureChildren{
        makeMenuItem(QStringLiteral("file.texture.import"),
                     QStringLiteral("Import"),
                     QString(),
                     QStringLiteral("submenu"),
                     false,
                     true,
                     texImportChildren),
        makeMenuItem(QStringLiteral("file.texture.sep1"), QString(), QString(), QStringLiteral("separator")),
        makeMenuItem(QStringLiteral("file.texture.open"), QStringLiteral("Open Texture View..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.save"), QStringLiteral("Save Texture View"), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.saveAs"), QStringLiteral("Save Texture View As..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.exportImage"), QStringLiteral("Export Texture View Image..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.texture.sep2"), QString(), QString(), QStringLiteral("separator")),
        makeMenuItem(QStringLiteral("file.texture.setting"),
                     QStringLiteral("Setting"),
                     QString(),
                     QStringLiteral("submenu"),
                     false,
                     true,
                     childSettingChildren),
    };

    QJsonArray fileChildren{
        makeMenuItem(QStringLiteral("file.new"), QStringLiteral("Canvas Size..."), QString(), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.open"), QStringLiteral("Open..."), QStringLiteral("Ctrl+O"), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.save"), QStringLiteral("Save"), QStringLiteral("Ctrl+S"), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.saveAs"), QStringLiteral("Save As..."), QStringLiteral("Ctrl+Shift+S"), QStringLiteral("action")),
        makeMenuItem(QStringLiteral("file.sep1"), QString(), QString(), QStringLiteral("separator")),
        makeMenuItem(QStringLiteral("file.import"),
                     QStringLiteral("Import"),
                     QString(),
                     QStringLiteral("submenu"),
                     false,
                     true,
                     importChildren),
        makeMenuItem(QStringLiteral("file.texture"),
                     QStringLiteral("Texture"),
                     QString(),
                     QStringLiteral("submenu"),
                     false,
                     true,
                     textureChildren),
    };
    menus.append(makeMenuItem(QStringLiteral("file"), QStringLiteral("File"), QString(), QStringLiteral("submenu"), false, true, fileChildren));

    // 2. Edit menu
    QJsonArray editChildren{
        makeMenuItem(QStringLiteral("edit.undo"),
                     QStringLiteral("Undo"),
                     QStringLiteral("Ctrl+Z"),
                     QStringLiteral("action"),
                     false,
                     m_business->canUndo()),
        makeMenuItem(QStringLiteral("edit.redo"),
                     QStringLiteral("Redo"),
                     QStringLiteral("Ctrl+Y"),
                     QStringLiteral("action"),
                     false,
                     m_business->canRedo()),
    };
    menus.append(makeMenuItem(QStringLiteral("edit"), QStringLiteral("Edit"), QString(), QStringLiteral("submenu"), false, true, editChildren));

    // 3..5. Python script menus for "main" (captured at createScriptMenus time: View, Auto Mapping, Draw)
#ifdef ANIMEAN_WITH_PYTHON
    if (Py_IsInitialized()) {
        try {
            py::gil_scoped_acquire acquire;
            const std::string json = py::module_::import("python_hooks")
                                         .attr("menus_json")(std::string("main"))
                                         .cast<std::string>();
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
            QHash<QString, QJsonObject> liveMainMenus;
            for (const QJsonValue &val : doc.array()) {
                const QJsonObject obj = val.toObject();
                const QString mName = obj.value(QStringLiteral("name")).toString();
                if (!mName.isEmpty()) {
                    liveMainMenus.insert(mName, obj);
                }
            }
            for (const QPair<QString, QString> &entry : m_mainScriptMenus) {
                const QString &mName = entry.first;
                const QJsonObject obj = liveMainMenus.value(mName);
                const QString mTitle = obj.value(QStringLiteral("title")).toString(entry.second);
                const QJsonArray items =
                    buildScriptMenuItems(QStringLiteral("main"), mName, obj.value(QStringLiteral("items")).toArray());
                menus.append(makeMenuItem(mName, mTitle, QString(), QStringLiteral("submenu"), false, true, items));
            }
        } catch (...) {
        }
    }
#endif

    // 6. Theme menu
    QJsonArray themeChildren{
        makeMenuItem(QStringLiteral("theme.dark"),
                     QStringLiteral("Dark"),
                     QString(),
                     QStringLiteral("radio"),
                     AnimeTheme::mode() == AnimeTheme::Mode::Dark),
        makeMenuItem(QStringLiteral("theme.light"),
                     QStringLiteral("Light"),
                     QString(),
                     QStringLiteral("radio"),
                     AnimeTheme::mode() == AnimeTheme::Mode::Light),
    };
    menus.append(makeMenuItem(QStringLiteral("theme"), QStringLiteral("Theme"), QString(), QStringLiteral("submenu"), false, true, themeChildren));

    // 7. Windows menu
    QJsonArray windowsChildren{
        makeMenuItem(QStringLiteral("windows.texture_view"),
                     QStringLiteral("Texture View"),
                     QString(),
                     QStringLiteral("action")),
        makeMenuItem(QStringLiteral("windows.sep1"), QString(), QString(), QStringLiteral("separator")),
    };
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        if (info.name == QStringLiteral("paint")) {
            continue;
        }
        windowsChildren.append(makeMenuItem(QStringLiteral("windows.%1").arg(info.name),
                                            info.title,
                                            QString(),
                                            QStringLiteral("check"),
                                            info.visible));
    }
    windowsChildren.append(makeMenuItem(QStringLiteral("windows.timeline"),
                                        QStringLiteral("Timeline"),
                                        QString(),
                                        QStringLiteral("check"),
                                        m_timelineVisible));
    menus.append(makeMenuItem(QStringLiteral("windows"), QStringLiteral("Windows"), QString(), QStringLiteral("submenu"), false, true, windowsChildren));

    return menus;
}

QJsonObject BackendSession::buildToolsState() const
{
    const AnimeanBusiness::Tool curTool = m_business->currentToolEnum();
    QJsonArray extraArr;
    for (const AnimeExtraToolDefinition &et : m_business->extraTools()) {
        extraArr.append(QJsonObject{
            {QStringLiteral("name"), et.name},
            {QStringLiteral("title"), et.title},
            {QStringLiteral("property"), et.property},
            {QStringLiteral("handler"), et.handler},
            {QStringLiteral("baseTool"), et.baseTool},
            {QStringLiteral("page"), et.page},
        });
    }

    auto setToSortedArray = [](const QSet<QString> &set) {
        QStringList list = set.values();
        list.sort();
        QJsonArray arr;
        for (const QString &s : list) {
            arr.append(s);
        }
        return arr;
    };

    const QJsonArray activeLocked = setToSortedArray(m_business->lockedToolsForView(m_business->activeViewName()));
    QJsonObject lockedByView{
        {QStringLiteral("main"), setToSortedArray(m_business->lockedToolsForView(QStringLiteral("main")))},
        {QStringLiteral("child"), setToSortedArray(m_business->lockedToolsForView(QStringLiteral("child")))},
    };

    QString toolsPage = QStringLiteral("painting");
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        if (info.name == QStringLiteral("tools")) {
            toolsPage = info.current;
            break;
        }
    }

    return QJsonObject{
        {QStringLiteral("tool"), AnimeanBusiness::toolControlName(curTool)},
        {QStringLiteral("current"), AnimeanBusiness::toolControlName(curTool)},
        {QStringLiteral("toolEnum"), toolEnumDisplayName(curTool)},
        {QStringLiteral("toolId"), int(curTool)},
        {QStringLiteral("activeExtraTool"), m_business->activeExtraTool()},
        {QStringLiteral("activeExtraToolProperty"), m_business->activeExtraToolProperty()},
        {QStringLiteral("extraTools"), extraArr},
        {QStringLiteral("lockedTools"), activeLocked},
        {QStringLiteral("lockedToolsByView"), lockedByView},
        {QStringLiteral("page"), toolsPage},
    };
}

QJsonObject BackendSession::buildToolOptionsState() const
{
    if (!m_lastToolOptions.isEmpty()) {
        return m_lastToolOptions;
    }
    if (!m_business->activeExtraTool().isEmpty()) {
        return m_business->extraToolOptionsLayout(m_business->activeExtraTool());
    }
    return m_business->toolOptionsLayout(m_business->currentToolEnum());
}

QJsonObject BackendSession::buildDrawColorState() const
{
    const QColor c = m_business->drawingColor();
    return QJsonObject{
        {QStringLiteral("r"), c.red()},
        {QStringLiteral("g"), c.green()},
        {QStringLiteral("b"), c.blue()},
        {QStringLiteral("a"), c.alpha()},
        {QStringLiteral("hex"), c.name(QColor::HexRgb)},
        {QStringLiteral("rgba"), c.name(QColor::HexArgb)},
    };
}

QJsonObject BackendSession::buildDrawSettingsState() const
{
    const AnimeStrokeFitSettings fit = m_business->strokeFitSettings();
    return QJsonObject{
        {QStringLiteral("smooth"), m_business->toolSmoothValue()},
        {QStringLiteral("stabilizer"), m_business->toolSmoothValue()},
        {QStringLiteral("penWidth"), m_business->toolPenWidth()},
        {QStringLiteral("simplify"), fit.simplify},
        {QStringLiteral("corner"), fit.corner},
        {QStringLiteral("fillScope"), m_business->toolFillAllLayers() ? QStringLiteral("all") : QStringLiteral("current")},
        {QStringLiteral("fillAllLayers"), m_business->toolFillAllLayers()},
    };
}

QJsonObject BackendSession::buildLayersState(const QString &viewName) const
{
    const QVector<AnimeLayerPanelRow> rows = m_business->layerRowsForView(viewName);
    QJsonArray rowsArr;
    for (const AnimeLayerPanelRow &row : rows) {
        rowsArr.append(QJsonObject{
            {QStringLiteral("group"), row.group},
            {QStringLiteral("group_id"), row.groupId},
            {QStringLiteral("layer"), row.layerIndex},
            {QStringLiteral("name"), row.name},
            {QStringLiteral("visible"), row.visible},
            {QStringLiteral("collapsed"), row.collapsed},
            {QStringLiteral("depth"), row.depth},
            {QStringLiteral("child"), row.child},
            {QStringLiteral("parent"), row.parent},
        });
    }
    return QJsonObject{
        {QStringLiteral("rows"), rowsArr},
        {QStringLiteral("selected"), m_business->currentLayer(viewName)},
    };
}

QJsonObject BackendSession::buildAssetsState(const QString &viewName) const
{
    const AnimeSceneModel &model = m_business->modelForView(viewName);
    QJsonArray items;
    QJsonArray names;
    for (int i = 0; i < model.assetCount(); ++i) {
        if (model.assetInternal(i)) {
            continue;
        }
        items.append(QJsonObject{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), model.assetName(i)},
            {QStringLiteral("type"), int(model.assetType(i))},
        });
        names.append(model.assetName(i));
    }
    return QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("assets"), items},
        {QStringLiteral("names"), names},
        {QStringLiteral("selected"), m_business->attentionForView(viewName).asset},
    };
}

QJsonObject BackendSession::buildTimelineState() const
{
    const AnimeTimelineState st = m_business->timelineState();
    QJsonArray framesArr;
    QJsonArray holdsArr;
    QJsonArray namesArr;
    for (int i = 0; i < st.frameCount; ++i) {
        const bool hold = (i < st.holds.size()) ? st.holds.at(i) : false;
        const QString name = (i < st.frameNames.size()) ? st.frameNames.at(i) : QString();
        framesArr.append(QJsonObject{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), name},
            {QStringLiteral("hold"), hold},
        });
        holdsArr.append(hold);
        namesArr.append(name);
    }

    QList<int> onionList = st.onionFrames.values();
    std::sort(onionList.begin(), onionList.end());
    QJsonArray onionFramesArr;
    for (int f : onionList) {
        onionFramesArr.append(f);
    }

    QJsonObject onionObj{
        {QStringLiteral("available"), st.onionAvailable},
        {QStringLiteral("enabled"), st.onionEnabled},
        {QStringLiteral("lines"), st.onionLines},
        {QStringLiteral("fills"), st.onionFills},
        {QStringLiteral("amLayers"), st.onionAmLayers},
        {QStringLiteral("guideLines"), st.onionGuideLines},
        {QStringLiteral("frames"), onionFramesArr},
    };

    const bool vertical = (m_timelineDockArea == QStringLiteral("left")
                           || m_timelineDockArea == QStringLiteral("right"));
    const bool leftAligned = (m_timelineDockArea == QStringLiteral("left"));

    return QJsonObject{
        {QStringLiteral("view"), st.viewName},
        {QStringLiteral("visible"), m_timelineVisible},
        {QStringLiteral("collapsed"), m_timelineCollapsed},
        {QStringLiteral("floating"), m_timelineFloating},
        {QStringLiteral("dockArea"), m_timelineDockArea},
        {QStringLiteral("vertical"), vertical},
        {QStringLiteral("leftAligned"), leftAligned},
        {QStringLiteral("frameCount"), st.frameCount},
        {QStringLiteral("frames"), framesArr},
        {QStringLiteral("holds"), holdsArr},
        {QStringLiteral("frameNames"), namesArr},
        {QStringLiteral("currentFrame"), st.currentFrame},
        {QStringLiteral("fps"), st.fps},
        {QStringLiteral("playbackActive"), st.playbackActive},
        {QStringLiteral("playing"), st.playbackActive},
        {QStringLiteral("loop"), st.loop},
        {QStringLiteral("onion"), onionObj},
        {QStringLiteral("onionAvailable"), st.onionAvailable},
        {QStringLiteral("onionEnabled"), st.onionEnabled},
        {QStringLiteral("onionLines"), st.onionLines},
        {QStringLiteral("onionFills"), st.onionFills},
        {QStringLiteral("onionAmLayers"), st.onionAmLayers},
        {QStringLiteral("onionGuideLines"), st.onionGuideLines},
        {QStringLiteral("onionFrames"), onionFramesArr},
    };
}

QJsonObject BackendSession::buildHistoryState() const
{
    const QString activeView = m_business->activeViewName();
    const QStringList labels = m_business->historyLabels(activeView);
    QJsonArray labelsArr;
    for (const QString &l : labels) {
        labelsArr.append(l);
    }
    return QJsonObject{
        {QStringLiteral("view"), activeView},
        {QStringLiteral("labels"), labelsArr},
        {QStringLiteral("items"), labelsArr},
        {QStringLiteral("currentIndex"), m_business->currentHistoryIndex(activeView)},
        {QStringLiteral("canUndo"), m_business->canUndo()},
        {QStringLiteral("canRedo"), m_business->canRedo()},
        {QStringLiteral("undoTargetView"), m_business->undoTargetViewName()},
        {QStringLiteral("redoTargetView"), m_business->redoTargetViewName()},
    };
}

QJsonObject BackendSession::buildWindowsState() const
{
    QJsonObject out;
    QJsonArray listArr;
    for (const AnimeanWindowInfo &info : m_windowInfos) {
        QJsonArray pagesArr;
        for (const QString &p : info.pages) {
            pagesArr.append(p);
        }
        const QJsonObject winObj{
            {QStringLiteral("name"), info.name},
            {QStringLiteral("title"), info.title},
            {QStringLiteral("visible"), info.visible},
            {QStringLiteral("pages"), pagesArr},
            {QStringLiteral("current"), info.current},
        };
        out.insert(info.name, winObj);
        listArr.append(winObj);
    }
    const bool vertical = (m_timelineDockArea == QStringLiteral("left")
                           || m_timelineDockArea == QStringLiteral("right"));
    const bool leftAligned = (m_timelineDockArea == QStringLiteral("left"));
    const QJsonObject timelineObj{
        {QStringLiteral("name"), QStringLiteral("timeline")},
        {QStringLiteral("title"), QStringLiteral("Timeline")},
        {QStringLiteral("visible"), m_timelineVisible},
        {QStringLiteral("collapsed"), m_timelineCollapsed},
        {QStringLiteral("floating"), m_timelineFloating},
        {QStringLiteral("dockArea"), m_timelineDockArea},
        {QStringLiteral("vertical"), vertical},
        {QStringLiteral("leftAligned"), leftAligned},
        {QStringLiteral("pages"), QJsonArray()},
        {QStringLiteral("current"), QString()},
    };
    out.insert(QStringLiteral("timeline"), timelineObj);
    out.insert(QStringLiteral("textureSubControlVisible"), m_textureSubControlVisible);
    out.insert(QStringLiteral("list"), listArr);
    return out;
}

QJsonObject BackendSession::buildCursorState(const QString &viewName) const
{
    const CursorState st = m_cursors.value(viewName);
    return QJsonObject{
        {QStringLiteral("shape"), st.shape},
        {QStringLiteral("css"), st.css},
        {QStringLiteral("key"), st.key},
        {QStringLiteral("hx"), st.hx},
        {QStringLiteral("hy"), st.hy},
        {QStringLiteral("w"), st.w},
        {QStringLiteral("h"), st.h},
    };
}

QJsonObject BackendSession::buildViewportState(const QString &viewName) const
{
    const PaintOpenGLWidget *paintWidget = boardForView(viewName);
    if (!paintWidget) {
        return QJsonObject();
    }
    if (paintWidget->unboundedCanvas()) {
        return QJsonObject{
            {QStringLiteral("h"), QJsonObject{
                {QStringLiteral("min"), 0},
                {QStringLiteral("max"), 0},
                {QStringLiteral("page"), paintWidget->width()},
                {QStringLiteral("value"), 0},
                {QStringLiteral("enabled"), false},
            }},
            {QStringLiteral("v"), QJsonObject{
                {QStringLiteral("min"), 0},
                {QStringLiteral("max"), 0},
                {QStringLiteral("page"), paintWidget->height()},
                {QStringLiteral("value"), 0},
                {QStringLiteral("enabled"), false},
            }},
        };
    }

    const qreal zoomFactor = paintWidget->zoom();
    const QRectF reach = paintWidget->reachableRect();
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

    return QJsonObject{
        {QStringLiteral("h"), QJsonObject{
            {QStringLiteral("min"), hMin},
            {QStringLiteral("max"), hMax},
            {QStringLiteral("page"), paintWidget->width()},
            {QStringLiteral("value"), hValue},
            {QStringLiteral("enabled"), hRange > 0},
        }},
        {QStringLiteral("v"), QJsonObject{
            {QStringLiteral("min"), vMin},
            {QStringLiteral("max"), vMax},
            {QStringLiteral("page"), paintWidget->height()},
            {QStringLiteral("value"), vValue},
            {QStringLiteral("enabled"), vRange > 0},
        }},
    };
}

QJsonObject BackendSession::buildPadState() const
{
    QJsonObject padsObj;
    for (auto it = m_padValues.cbegin(); it != m_padValues.cend(); ++it) {
        padsObj.insert(it.key(), QJsonObject{
            {QStringLiteral("x"), it.value().x()},
            {QStringLiteral("y"), it.value().y()},
        });
    }
    return QJsonObject{
        {QStringLiteral("x"), m_forcePadValue.x()},
        {QStringLiteral("y"), m_forcePadValue.y()},
        {QStringLiteral("pads"), padsObj},
    };
}
