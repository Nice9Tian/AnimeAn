#include "recordingengine.h"

#include <QGradient>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QTextItem>

#include <charconv>
#include <cmath>
#include <cstring>

namespace {

quint64 fnv1a64(const void *data, size_t len, quint64 seed = 14695981039346656037ULL)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    quint64 h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<quint64>(bytes[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

void appendInt(QByteArray &out, int val)
{
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        out.append(buf, int(ptr - buf));
    } else {
        out.append('0');
    }
}

void appendDouble(QByteArray &out, double val)
{
    if (!std::isfinite(val) || val == 0.0) {
        out.append('0');
        return;
    }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        out.append(buf, int(ptr - buf));
    } else {
        out.append('0');
    }
}

int mapCapStyle(Qt::PenCapStyle cap)
{
    switch (cap) {
    case Qt::FlatCap:
        return 0;
    case Qt::SquareCap:
        return 1;
    case Qt::RoundCap:
    default:
        return 2;
    }
}

int mapJoinStyle(Qt::PenJoinStyle join)
{
    switch (join) {
    case Qt::MiterJoin:
    case Qt::SvgMiterJoin:
        return 0;
    case Qt::BevelJoin:
        return 1;
    case Qt::RoundJoin:
    default:
        return 2;
    }
}

const char *mapCompositionMode(QPainter::CompositionMode mode)
{
    switch (mode) {
    case QPainter::CompositionMode_SourceOver:
        return "source-over";
    case QPainter::CompositionMode_DestinationOver:
        return "destination-over";
    case QPainter::CompositionMode_Clear:
        return "clear";
    case QPainter::CompositionMode_Source:
        return "source";
    case QPainter::CompositionMode_Destination:
        return "destination";
    case QPainter::CompositionMode_SourceIn:
        return "source-in";
    case QPainter::CompositionMode_DestinationIn:
        return "destination-in";
    case QPainter::CompositionMode_SourceOut:
        return "source-out";
    case QPainter::CompositionMode_DestinationOut:
        return "destination-out";
    case QPainter::CompositionMode_SourceAtop:
        return "source-atop";
    case QPainter::CompositionMode_DestinationAtop:
        return "destination-atop";
    case QPainter::CompositionMode_Xor:
        return "xor";
    case QPainter::CompositionMode_Plus:
        return "lighter";
    case QPainter::CompositionMode_Multiply:
        return "multiply";
    case QPainter::CompositionMode_Screen:
        return "screen";
    case QPainter::CompositionMode_Overlay:
        return "overlay";
    case QPainter::CompositionMode_Darken:
        return "darken";
    case QPainter::CompositionMode_Lighten:
        return "lighten";
    case QPainter::CompositionMode_ColorDodge:
        return "color-dodge";
    case QPainter::CompositionMode_ColorBurn:
        return "color-burn";
    case QPainter::CompositionMode_HardLight:
        return "hard-light";
    case QPainter::CompositionMode_SoftLight:
        return "soft-light";
    case QPainter::CompositionMode_Difference:
        return "difference";
    case QPainter::CompositionMode_Exclusion:
        return "exclusion";
    default:
        return "source-over";
    }
}

void appendPathArray(QByteArray &out, const QPainterPath &path)
{
    out.append('[');
    bool first = true;
    const auto comma = [&]() {
        if (!first) {
            out.append(',');
        }
        first = false;
    };

    const int count = path.elementCount();
    bool hasSubpath = false;
    double startX = 0.0;
    double startY = 0.0;
    double lastX = 0.0;
    double lastY = 0.0;
    int segCount = 0;

    for (int i = 0; i < count; ++i) {
        const QPainterPath::Element el = path.elementAt(i);
        if (el.type == QPainterPath::MoveToElement) {
            if (hasSubpath && segCount >= 2 && lastX == startX && lastY == startY) {
                comma();
                out.append('3');
            }
            comma();
            out.append('0');
            out.append(',');
            appendDouble(out, el.x);
            out.append(',');
            appendDouble(out, el.y);
            hasSubpath = true;
            startX = el.x;
            startY = el.y;
            lastX = el.x;
            lastY = el.y;
            segCount = 0;
        } else if (el.type == QPainterPath::LineToElement) {
            comma();
            out.append('1');
            out.append(',');
            appendDouble(out, el.x);
            out.append(',');
            appendDouble(out, el.y);
            lastX = el.x;
            lastY = el.y;
            ++segCount;
        } else if (el.type == QPainterPath::CurveToElement && i + 2 < count) {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            comma();
            out.append('2');
            out.append(',');
            appendDouble(out, el.x);
            out.append(',');
            appendDouble(out, el.y);
            out.append(',');
            appendDouble(out, c2.x);
            out.append(',');
            appendDouble(out, c2.y);
            out.append(',');
            appendDouble(out, end.x);
            out.append(',');
            appendDouble(out, end.y);
            lastX = end.x;
            lastY = end.y;
            ++segCount;
            i += 2;
        }
    }
    if (hasSubpath && segCount >= 2 && lastX == startX && lastY == startY) {
        comma();
        out.append('3');
    }
    out.append(']');
}

QByteArray serializePenOp(const QPen &pen)
{
    QByteArray out;
    out.reserve(64);
    out.append("[\"P\",");
    if (pen.style() == Qt::NoPen || pen.color().alpha() == 0) {
        out.append("0,0,0,0,0,0,0,2,0,null]");
        return out;
    }

    const QColor c = pen.color();
    appendInt(out, c.red());
    out.append(',');
    appendInt(out, c.green());
    out.append(',');
    appendInt(out, c.blue());
    out.append(',');
    appendInt(out, c.alpha());
    out.append(',');
    appendDouble(out, pen.widthF());
    out.append(',');
    appendInt(out, mapCapStyle(pen.capStyle()));
    out.append(',');
    appendInt(out, mapJoinStyle(pen.joinStyle()));
    out.append(',');
    appendDouble(out, pen.miterLimit());
    out.append(',');
    out.append(pen.isCosmetic() ? '1' : '0');
    out.append(',');

    if (pen.style() == Qt::SolidLine) {
        out.append("null]");
    } else {
        const QList<qreal> dashes = pen.dashPattern();
        if (dashes.isEmpty()) {
            out.append("null]");
        } else {
            out.append('[');
            for (int i = 0; i < dashes.size(); ++i) {
                if (i > 0) {
                    out.append(',');
                }
                appendDouble(out, dashes[i]);
            }
            out.append("]]");
        }
    }
    return out;
}

QByteArray serializeBrushOp(const QBrush &brush)
{
    const QGradient *grad = brush.gradient();
    if (grad && (grad->type() == QGradient::LinearGradient || grad->type() == QGradient::RadialGradient)) {
        QByteArray out;
        out.reserve(128);
        out.append("[\"G\",");
        if (grad->type() == QGradient::LinearGradient) {
            const auto *lg = static_cast<const QLinearGradient *>(grad);
            out.append("\"linear\",[");
            appendDouble(out, lg->start().x());
            out.append(',');
            appendDouble(out, lg->start().y());
            out.append(',');
            appendDouble(out, lg->finalStop().x());
            out.append(',');
            appendDouble(out, lg->finalStop().y());
            out.append("],[");
        } else {
            const auto *rg = static_cast<const QRadialGradient *>(grad);
            out.append("\"radial\",[");
            appendDouble(out, rg->focalPoint().x());
            out.append(',');
            appendDouble(out, rg->focalPoint().y());
            out.append(',');
            appendDouble(out, rg->focalRadius());
            out.append(',');
            appendDouble(out, rg->center().x());
            out.append(',');
            appendDouble(out, rg->center().y());
            out.append(',');
            appendDouble(out, rg->centerRadius());
            out.append("],[");
        }
        const QGradientStops stops = grad->stops();
        for (int i = 0; i < stops.size(); ++i) {
            if (i > 0) {
                out.append(',');
            }
            const QGradientStop &s = stops[i];
            out.append('[');
            appendDouble(out, s.first);
            out.append(',');
            appendInt(out, s.second.red());
            out.append(',');
            appendInt(out, s.second.green());
            out.append(',');
            appendInt(out, s.second.blue());
            out.append(',');
            appendInt(out, s.second.alpha());
            out.append(']');
        }
        out.append("]]");
        return out;
    }

    QByteArray out;
    out.reserve(32);
    out.append("[\"B\",");
    if (brush.style() == Qt::NoBrush || brush.color().alpha() == 0) {
        out.append("0,0,0,0]");
        return out;
    }
    const QColor c = brush.color();
    appendInt(out, c.red());
    out.append(',');
    appendInt(out, c.green());
    out.append(',');
    appendInt(out, c.blue());
    out.append(',');
    appendInt(out, c.alpha());
    out.append(']');
    return out;
}

} // namespace

RecordingPaintEngine::RecordingPaintEngine()
    : QPaintEngine(QPaintEngine::AllFeatures)
{
}

RecordingPaintEngine::~RecordingPaintEngine() = default;

void RecordingPaintEngine::setImageSink(ImageSink sink)
{
    m_imageSink = std::move(sink);
}

void RecordingPaintEngine::resetFrame()
{
    m_ops.clear();
    m_currentTransform = QTransform();
    m_currentPen = QPen();
    m_currentBrush = QBrush(Qt::NoBrush);
    m_currentOpacity = 1.0;
    m_currentMode = QPainter::CompositionMode_SourceOver;
    m_currentHints = QPainter::RenderHints();
    m_lastClipPath = QPainterPath();

    m_hasEmittedTransform = false;
    m_emittedTransformBytes.clear();

    m_hasEmittedPen = false;
    m_emittedPenBytes.clear();

    m_hasEmittedBrush = false;
    m_emittedBrushBytes.clear();

    m_emittedOpacity = 1.0;
    m_emittedMode = QPainter::CompositionMode_SourceOver;
    m_emittedClipEnabled = false;
    m_emittedClipBytes.clear();
    m_emittedSmooth = -1;
}

bool RecordingPaintEngine::begin(QPaintDevice * /*pdev*/)
{
    resetFrame();
    setActive(true);
    return true;
}

bool RecordingPaintEngine::end()
{
    setActive(false);
    return true;
}

void RecordingPaintEngine::appendOp(QByteArray json)
{
    RecordedOp op;
    op.hash = fnv1a64(json.constData(), size_t(json.size()));
    op.json = std::move(json);
    m_ops.append(std::move(op));
}

void RecordingPaintEngine::syncTransform()
{
    QByteArray out;
    out.reserve(64);
    out.append("[\"T\",");
    appendDouble(out, m_currentTransform.m11());
    out.append(',');
    appendDouble(out, m_currentTransform.m12());
    out.append(',');
    appendDouble(out, m_currentTransform.m21());
    out.append(',');
    appendDouble(out, m_currentTransform.m22());
    out.append(',');
    appendDouble(out, m_currentTransform.dx());
    out.append(',');
    appendDouble(out, m_currentTransform.dy());
    out.append(']');

    if (!m_hasEmittedTransform || out != m_emittedTransformBytes) {
        m_hasEmittedTransform = true;
        m_emittedTransformBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::syncOpacity()
{
    if (std::abs(m_currentOpacity - m_emittedOpacity) <= 1e-9) {
        return;
    }
    m_emittedOpacity = m_currentOpacity;
    QByteArray out;
    out.reserve(24);
    out.append("[\"O\",");
    appendDouble(out, m_currentOpacity);
    out.append(']');
    appendOp(std::move(out));
}

void RecordingPaintEngine::syncCompositionMode()
{
    if (m_currentMode == m_emittedMode) {
        return;
    }
    m_emittedMode = m_currentMode;
    QByteArray out;
    out.reserve(32);
    out.append("[\"M\",\"");
    out.append(mapCompositionMode(m_currentMode));
    out.append("\"]");
    appendOp(std::move(out));
}

void RecordingPaintEngine::syncPen(const QPen &pen)
{
    QByteArray out = serializePenOp(pen);
    if (!m_hasEmittedPen || out != m_emittedPenBytes) {
        m_hasEmittedPen = true;
        m_emittedPenBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::syncBrush(const QBrush &brush)
{
    QByteArray out = serializeBrushOp(brush);
    if (!m_hasEmittedBrush || out != m_emittedBrushBytes) {
        m_hasEmittedBrush = true;
        m_emittedBrushBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::syncSmooth()
{
    const int smooth = (m_currentHints & QPainter::SmoothPixmapTransform) ? 1 : 0;
    if (smooth == m_emittedSmooth) {
        return;
    }
    m_emittedSmooth = smooth;
    QByteArray out;
    out.reserve(12);
    out.append("[\"H\",");
    out.append(smooth ? '1' : '0');
    out.append(']');
    appendOp(std::move(out));
}

void RecordingPaintEngine::syncCommonState()
{
    syncTransform();
    syncOpacity();
    syncCompositionMode();
}

void RecordingPaintEngine::syncStateForShape(bool usePen, bool useBrush)
{
    syncCommonState();
    syncPen(usePen ? m_currentPen : QPen(Qt::NoPen));
    syncBrush(useBrush ? m_currentBrush : QBrush(Qt::NoBrush));
}

void RecordingPaintEngine::syncStateForImage()
{
    syncCommonState();
    syncSmooth();
}

void RecordingPaintEngine::updateClip(const QPaintEngineState &state)
{
    const QPaintEngine::DirtyFlags flags = state.state();
    const bool clipDisabled = !state.isClipEnabled()
                              || ((flags & (QPaintEngine::DirtyClipPath | QPaintEngine::DirtyClipRegion))
                                  && state.clipOperation() == Qt::NoClip);
    if (clipDisabled) {
        if (m_emittedClipEnabled) {
            m_emittedClipEnabled = false;
            m_emittedClipBytes.clear();
            appendOp(QByteArrayLiteral("[\"K\",null,0]"));
        }
        return;
    }

    QPainterPath path;
    if (flags & QPaintEngine::DirtyClipPath) {
        path = state.clipPath();
        m_lastClipPath = path;
    } else if (flags & QPaintEngine::DirtyClipRegion) {
        path.addRegion(state.clipRegion());
        m_lastClipPath = path;
    } else {
        path = m_lastClipPath;
    }

    syncTransform();
    QByteArray out;
    out.reserve(64 + path.elementCount() * 24);
    out.append("[\"K\",");
    appendPathArray(out, path);
    out.append(',');
    out.append(path.fillRule() == Qt::WindingFill ? '1' : '0');
    out.append(']');

    if (!m_emittedClipEnabled || out != m_emittedClipBytes) {
        m_emittedClipEnabled = true;
        m_emittedClipBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::updateState(const QPaintEngineState &state)
{
    const QPaintEngine::DirtyFlags flags = state.state();
    if (flags & QPaintEngine::DirtyTransform) {
        m_currentTransform = state.transform();
    }
    if (flags & QPaintEngine::DirtyPen) {
        m_currentPen = state.pen();
    }
    if (flags & QPaintEngine::DirtyBrush) {
        m_currentBrush = state.brush();
    }
    if (flags & QPaintEngine::DirtyOpacity) {
        m_currentOpacity = state.opacity();
    }
    if (flags & QPaintEngine::DirtyCompositionMode) {
        m_currentMode = state.compositionMode();
    }
    if (flags & QPaintEngine::DirtyHints) {
        m_currentHints = state.renderHints();
    }
    if (flags & (QPaintEngine::DirtyClipPath | QPaintEngine::DirtyClipRegion | QPaintEngine::DirtyClipEnabled)) {
        updateClip(state);
    }
}

void RecordingPaintEngine::drawRects(const QRect *rects, int rectCount)
{
    if (!rects || rectCount <= 0) {
        return;
    }
    syncStateForShape(true, true);
    for (int i = 0; i < rectCount; ++i) {
        const QRect &r = rects[i];
        QByteArray out;
        out.reserve(48);
        out.append("[\"R\",");
        appendInt(out, r.x());
        out.append(',');
        appendInt(out, r.y());
        out.append(',');
        appendInt(out, r.width());
        out.append(',');
        appendInt(out, r.height());
        out.append(']');
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::drawRects(const QRectF *rects, int rectCount)
{
    if (!rects || rectCount <= 0) {
        return;
    }
    syncStateForShape(true, true);
    for (int i = 0; i < rectCount; ++i) {
        const QRectF &r = rects[i];
        QByteArray out;
        out.reserve(64);
        out.append("[\"R\",");
        appendDouble(out, r.x());
        out.append(',');
        appendDouble(out, r.y());
        out.append(',');
        appendDouble(out, r.width());
        out.append(',');
        appendDouble(out, r.height());
        out.append(']');
        appendOp(std::move(out));
    }
}

void RecordingPaintEngine::emitPathOp(const QPainterPath &path)
{
    if (path.isEmpty()) {
        return;
    }
    QByteArray out;
    out.reserve(32 + path.elementCount() * 24);
    out.append("[\"S\",");
    appendPathArray(out, path);
    out.append(',');
    out.append(path.fillRule() == Qt::WindingFill ? '1' : '0');
    out.append(']');
    appendOp(std::move(out));
}

void RecordingPaintEngine::drawLines(const QLine *lines, int lineCount)
{
    if (!lines || lineCount <= 0) {
        return;
    }
    syncStateForShape(true, false);
    QPainterPath path;
    for (int i = 0; i < lineCount; ++i) {
        path.moveTo(lines[i].p1());
        path.lineTo(lines[i].p2());
    }
    emitPathOp(path);
}

void RecordingPaintEngine::drawLines(const QLineF *lines, int lineCount)
{
    if (!lines || lineCount <= 0) {
        return;
    }
    syncStateForShape(true, false);
    QPainterPath path;
    for (int i = 0; i < lineCount; ++i) {
        path.moveTo(lines[i].p1());
        path.lineTo(lines[i].p2());
    }
    emitPathOp(path);
}

void RecordingPaintEngine::drawEllipse(const QRectF &r)
{
    syncStateForShape(true, true);
    QPainterPath path;
    path.addEllipse(r);
    emitPathOp(path);
}

void RecordingPaintEngine::drawEllipse(const QRect &r)
{
    drawEllipse(QRectF(r));
}

void RecordingPaintEngine::drawPath(const QPainterPath &path)
{
    if (path.isEmpty()) {
        return;
    }
    syncStateForShape(true, true);
    emitPathOp(path);
}

void RecordingPaintEngine::drawPoints(const QPointF *points, int pointCount)
{
    if (!points || pointCount <= 0) {
        return;
    }
    syncStateForShape(true, false);
    QPainterPath path;
    for (int i = 0; i < pointCount; ++i) {
        path.moveTo(points[i]);
        path.lineTo(points[i] + QPointF(0.001, 0.0));
    }
    emitPathOp(path);
}

void RecordingPaintEngine::drawPoints(const QPoint *points, int pointCount)
{
    if (!points || pointCount <= 0) {
        return;
    }
    syncStateForShape(true, false);
    QPainterPath path;
    for (int i = 0; i < pointCount; ++i) {
        const QPointF pt(points[i]);
        path.moveTo(pt);
        path.lineTo(pt + QPointF(0.001, 0.0));
    }
    emitPathOp(path);
}

void RecordingPaintEngine::drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode)
{
    if (!points || pointCount <= 0) {
        return;
    }
    const bool closed = (mode != QPaintEngine::PolylineMode);
    syncStateForShape(true, closed);

    QByteArray out;
    out.reserve(32 + pointCount * 24);
    out.append("[\"S\",[0,");
    appendDouble(out, points[0].x());
    out.append(',');
    appendDouble(out, points[0].y());
    for (int i = 1; i < pointCount; ++i) {
        out.append(",1,");
        appendDouble(out, points[i].x());
        out.append(',');
        appendDouble(out, points[i].y());
    }
    if (closed) {
        out.append(",3");
    }
    out.append("],");
    out.append(mode == QPaintEngine::WindingMode ? '1' : '0');
    out.append(']');
    appendOp(std::move(out));
}

void RecordingPaintEngine::drawPolygon(const QPoint *points, int pointCount, PolygonDrawMode mode)
{
    if (!points || pointCount <= 0) {
        return;
    }
    QVector<QPointF> pts;
    pts.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i) {
        pts.append(QPointF(points[i]));
    }
    drawPolygon(pts.constData(), pointCount, mode);
}

QString RecordingPaintEngine::ensureImageSent(const QImage &image)
{
    const qint64 cacheKey = image.cacheKey();
    auto cached = m_cacheKeyToHash.constFind(cacheKey);
    if (cached != m_cacheKeyToHash.constEnd() && m_sentImageKeys.contains(*cached)) {
        return *cached;
    }

    const QImage rgba = (image.format() == QImage::Format_RGBA8888)
                            ? image
                            : image.convertToFormat(QImage::Format_RGBA8888);

    const int w = rgba.width();
    const int h = rgba.height();
    quint64 hash = 14695981039346656037ULL;
    hash = fnv1a64(&w, sizeof(w), hash);
    hash = fnv1a64(&h, sizeof(h), hash);
    const size_t rowBytes = size_t(w) * 4;
    for (int y = 0; y < h; ++y) {
        hash = fnv1a64(rgba.constScanLine(y), rowBytes, hash);
    }

    const QString key = QString::number(hash, 16).rightJustified(16, QLatin1Char('0'));
    m_cacheKeyToHash.insert(cacheKey, key);

    if (!m_sentImageKeys.contains(key)) {
        m_sentImageKeys.insert(key);
        if (m_imageSink) {
            m_imageSink(key, rgba);
        }
    }
    return key;
}

void RecordingPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr)
{
    if (pm.isNull()) {
        return;
    }
    drawImage(r, pm.toImage(), sr);
}

void RecordingPaintEngine::drawImage(const QRectF &r, const QImage &pm, const QRectF &sr,
                                     Qt::ImageConversionFlags /*flags*/)
{
    if (pm.isNull() || r.isEmpty()) {
        return;
    }
    syncStateForImage();
    const QString key = ensureImageSent(pm);
    const QRectF source = sr.isEmpty() ? QRectF(0.0, 0.0, pm.width(), pm.height()) : sr;

    QByteArray out;
    out.reserve(96);
    out.append("[\"I\",\"");
    out.append(key.toUtf8());
    out.append("\",");
    appendDouble(out, source.x());
    out.append(',');
    appendDouble(out, source.y());
    out.append(',');
    appendDouble(out, source.width());
    out.append(',');
    appendDouble(out, source.height());
    out.append(',');
    appendDouble(out, r.x());
    out.append(',');
    appendDouble(out, r.y());
    out.append(',');
    appendDouble(out, r.width());
    out.append(',');
    appendDouble(out, r.height());
    out.append(']');
    appendOp(std::move(out));
}

void RecordingPaintEngine::drawTiledPixmap(const QRectF &r, const QPixmap &pixmap, const QPointF & /*s*/)
{
    if (pixmap.isNull() || r.isEmpty()) {
        return;
    }
    drawImage(r, pixmap.toImage(), QRectF(0.0, 0.0, pixmap.width(), pixmap.height()));
}

void RecordingPaintEngine::drawTextItem(const QPointF &p, const QTextItem &textItem)
{
    if (textItem.text().isEmpty()) {
        return;
    }
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addText(p, textItem.font(), textItem.text());
    if (path.isEmpty()) {
        return;
    }
    syncCommonState();
    syncPen(QPen(Qt::NoPen));
    syncBrush(QBrush(m_currentPen.color()));
    emitPathOp(path);
}
