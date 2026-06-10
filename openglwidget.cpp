#include "openglwidget.h"

#include <QLineF>
#include <QImage>
#include <QPainter>
#include <QDebug>

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

#ifdef ANIMEAN_WITH_PYTHON
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
    update();
    return true;
}

int PaintOpenGLWidget::addFrame()
{
    const int row = m_model.addFrame();
    m_points.clear();
    m_hasCurrentStroke = false;
    m_hasLastEraserPos = false;
    update();
    return row;
}

bool PaintOpenGLWidget::deleteFrame(int frameIndex)
{
    if (!m_model.deleteFrame(frameIndex)) {
        return false;
    }

    update();
    return true;
}

bool PaintOpenGLWidget::moveFrame(int fromIndex, int toIndex)
{
    if (!m_model.moveFrame(fromIndex, toIndex)) {
        return false;
    }

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
        update();
    }
    return layerIndex;
}

const AnimeSceneModel &PaintOpenGLWidget::model() const
{
    return m_model;
}

void PaintOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    const AnimeScene &scene = m_model.scene();
    for (int columnIndex = scene.xsheet.columns.size() - 1; columnIndex >= 0; --columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (!column.visible) {
            continue;
        }

        const AnimeCell cell = column.cellAt(m_model.currentFrame());
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

        if (columnIndex == m_model.currentLayer() && m_hasCurrentStroke) {
            painter.setPen(QPen(m_currentStroke.color, m_currentStroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(m_currentStroke.path);
        }
    }
    painter.setOpacity(1.0);

    if ((m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) && m_hasHoverPos) {
        painter.setPen(QPen(QColor(220, 0, 180), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_hoverPos, m_eraserRadius, m_eraserRadius);
    }
}

void PaintOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const QPointF pos = event->position();
    m_hoverPos = pos;
    m_hasHoverPos = true;
    if (m_tool == Tool::Fill) {
        if (fillAt(pos)) {
            pythonHookSendMessage(QStringLiteral("fillfinish"), pos);
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
    m_currentStroke = makeStroke(m_points, m_penColor, m_penWidth);
    m_hasCurrentStroke = true;
    update();
    event->accept();
}

void PaintOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hoverPos = event->position();
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

    if (appendPoint(m_hoverPos)) {
        updateCurrentStroke();
    }

    event->accept();
}

void PaintOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_tool == Tool::Eraser || m_tool == Tool::DeleteLine) {
        m_hoverPos = event->position();
        m_hasHoverPos = true;
        if (m_tool == Tool::DeleteLine) {
            m_eraseGestureChanged = deleteLineAt(m_hoverPos) || m_eraseGestureChanged;
            pythonHookSendMessage(QStringLiteral("deletefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        } else {
            m_eraseGestureChanged = eraseAt(m_hoverPos) || m_eraseGestureChanged;
            pythonHookSendMessage(QStringLiteral("erasefinish"), m_hoverPos, QPointF(), m_eraseGestureChanged);
        }
        update();
        m_hasLastEraserPos = false;
        m_eraseGestureChanged = false;
        event->accept();
        return;
    }

    if (m_tool == Tool::Move) {
        if (m_hasLastMovePos) {
            const QPointF pos = event->position();
            const QPointF delta = pos - m_lastMovePos;
            m_moveGestureChanged = moveCurrentLayerBy(delta) || m_moveGestureChanged;
            m_lastMovePos = pos;
            update();
            pythonHookSendMessage(QStringLiteral("movefinish"), pos, delta, m_moveGestureChanged);
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

    appendPoint(event->position());
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
    pythonHookSendMessage(QStringLiteral("update"));
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
            pythonHookSendMessage(QStringLiteral("linefinish"), QPointF(), QPointF(), true, strokeIndex);
            removeInvalidFillRegions();
            if (m_model.assetCount() != assetCountBefore) {
                emit assetListChanged(m_model.currentAsset());
                emit layerListChanged(m_model.currentLayer());
            }
        }
    }
    m_hasCurrentStroke = false;
    m_points.clear();
    update();
}

void PaintOpenGLWidget::pythonHookSendMessage(const QString &event, const QPointF &pos, const QPointF &delta, bool changed, int strokeIndex)
{
#ifdef ANIMEAN_WITH_PYTHON
    if (!changed) {
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

    py::dict message;
    message["event"] = event.toStdString();
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
    if (!output.isEmpty()) {
        emit pythonDebugMessage(output);
    }
#else
    Q_UNUSED(event);
    Q_UNUSED(pos);
    Q_UNUSED(delta);
    Q_UNUSED(changed);
    Q_UNUSED(strokeIndex);
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
        if (!sameReferMode || !sameSourceLayer || !existing.path.contains(pos)) {
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
    QVector<QLineF> segments;

    const AnimeScene &scene = m_model.scene();
    const int frame = m_model.currentFrame();
    const bool allLayers = scope == FillScope::AllLayers;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        if (!allLayers && layerIndex >= 0 && columnIndex != layerIndex) {
            continue;
        }

        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (column.type == AnimeColumnType::Fill) {
            continue;
        }
        if (!column.visible) {
            continue;
        }

        const AnimeCell cell = column.cellAt(frame);
        const VectorImageModel *image = m_model.imageForCell(cell);
        if (!image) {
            continue;
        }

        for (const VectorStrokeNode &node : image->strokeNodes()) {
            const VectorStroke &stroke = node.stroke;
            segments += AnimeVectorLogic::segmentsFromPath(stroke.path);
        }
    }
    return segments;
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
