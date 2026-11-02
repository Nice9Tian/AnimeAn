#include "recordingdevice.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>

namespace {

constexpr std::ptrdiff_t kCacheKeyLimit = 4096;

std::uint64_t fnv1a64(const void *data, std::size_t len, std::uint64_t seed = 14695981039346656037ULL)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < len; ++i) {
        h ^= static_cast<std::uint64_t>(bytes[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

void appendInt(std::string &out, int val)
{
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        out.append(buf, static_cast<std::size_t>(ptr - buf));
    } else {
        out.push_back('0');
    }
}

void appendDouble(std::string &out, double val)
{
    if (!std::isfinite(val) || val == 0.0) {
        out.push_back('0');
        return;
    }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        out.append(buf, static_cast<std::size_t>(ptr - buf));
    } else {
        out.push_back('0');
    }
}

void appendUint64(std::string &out, std::uint64_t val)
{
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        out.append(buf, static_cast<std::size_t>(ptr - buf));
    } else {
        out.push_back('0');
    }
}

void appendJsonString(std::string &out, const std::string &utf8)
{
    out.push_back('"');
    for (unsigned char c : utf8) {
        switch (c) {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        case '\n':
            out.append("\\n");
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (c < 0x20) {
                char hex[7];
                std::snprintf(hex, sizeof(hex), "\\u%04x", unsigned(c));
                out.append(hex, 6);
            } else {
                out.push_back(char(c));
            }
            break;
        }
    }
    out.push_back('"');
}

int mapCapStyle(AnimeCore::PenCapStyle cap)
{
    switch (cap) {
    case AnimeCore::FlatCap:
        return 0;
    case AnimeCore::SquareCap:
        return 1;
    case AnimeCore::RoundCap:
    default:
        return 2;
    }
}

int mapJoinStyle(AnimeCore::PenJoinStyle join)
{
    switch (join) {
    case AnimeCore::MiterJoin:
        return 0;
    case AnimeCore::BevelJoin:
        return 1;
    case AnimeCore::RoundJoin:
    default:
        return 2;
    }
}

const char *mapCompositionMode(AnimeCore::CompositionMode mode)
{
    switch (mode) {
    case AnimeCore::CompositionMode_SourceIn:
        return "source-in";
    case AnimeCore::CompositionMode_SourceOver:
    default:
        return "source-over";
    }
}

void appendPathArray(std::string &out, const AnimeCore::Path &path)
{
    out.push_back('[');
    bool first = true;
    const auto comma = [&]() {
        if (!first) {
            out.push_back(',');
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
        const AnimeCore::Path::Element el = path.elementAt(i);
        if (el.type == AnimeCore::Path::MoveToElement) {
            if (hasSubpath && segCount >= 2 && lastX == startX && lastY == startY) {
                comma();
                out.push_back('3');
            }
            comma();
            out.push_back('0');
            out.push_back(',');
            appendDouble(out, el.x);
            out.push_back(',');
            appendDouble(out, el.y);
            hasSubpath = true;
            startX = el.x;
            startY = el.y;
            lastX = el.x;
            lastY = el.y;
            segCount = 0;
        } else if (el.type == AnimeCore::Path::LineToElement) {
            comma();
            out.push_back('1');
            out.push_back(',');
            appendDouble(out, el.x);
            out.push_back(',');
            appendDouble(out, el.y);
            lastX = el.x;
            lastY = el.y;
            ++segCount;
        } else if (el.type == AnimeCore::Path::CurveToElement && i + 2 < count) {
            const AnimeCore::Path::Element c2 = path.elementAt(i + 1);
            const AnimeCore::Path::Element end = path.elementAt(i + 2);
            comma();
            out.push_back('2');
            out.push_back(',');
            appendDouble(out, el.x);
            out.push_back(',');
            appendDouble(out, el.y);
            out.push_back(',');
            appendDouble(out, c2.x);
            out.push_back(',');
            appendDouble(out, c2.y);
            out.push_back(',');
            appendDouble(out, end.x);
            out.push_back(',');
            appendDouble(out, end.y);
            lastX = end.x;
            lastY = end.y;
            ++segCount;
            i += 2;
        }
    }
    if (hasSubpath && segCount >= 2 && lastX == startX && lastY == startY) {
        comma();
        out.push_back('3');
    }
    out.push_back(']');
}

std::string serializePenOp(const AnimeCore::Pen &pen)
{
    std::string out;
    out.reserve(64);
    out.append("[\"P\",");
    if (pen.style() == AnimeCore::NoPen || pen.color().alpha() == 0) {
        out.append("0,0,0,0,0,0,0,2,0,null]");
        return out;
    }

    const AnimeCore::Color c = pen.color();
    appendInt(out, c.red());
    out.push_back(',');
    appendInt(out, c.green());
    out.push_back(',');
    appendInt(out, c.blue());
    out.push_back(',');
    appendInt(out, c.alpha());
    out.push_back(',');
    appendDouble(out, pen.widthF());
    out.push_back(',');
    appendInt(out, mapCapStyle(pen.capStyle()));
    out.push_back(',');
    appendInt(out, mapJoinStyle(pen.joinStyle()));
    out.push_back(',');
    appendDouble(out, pen.miterLimit());
    out.push_back(',');
    out.push_back(pen.isCosmetic() ? '1' : '0');
    out.push_back(',');

    if (pen.style() == AnimeCore::SolidLine) {
        out.append("null]");
    } else {
        const AnimeCore::Vector<double> dashes = pen.dashPattern();
        if (dashes.isEmpty()) {
            out.append("null]");
        } else {
            out.push_back('[');
            for (std::ptrdiff_t i = 0; i < dashes.size(); ++i) {
                if (i > 0) {
                    out.push_back(',');
                }
                appendDouble(out, dashes[i]);
            }
            out.append("]]");
        }
    }
    return out;
}

std::string serializeBrushOp(const AnimeCore::Brush &brush)
{
    std::string out;
    out.reserve(32);
    out.append("[\"B\",");
    if (brush.style() == AnimeCore::NoBrush || brush.color().alpha() == 0) {
        out.append("0,0,0,0]");
        return out;
    }
    const AnimeCore::Color c = brush.color();
    appendInt(out, c.red());
    out.push_back(',');
    appendInt(out, c.green());
    out.push_back(',');
    appendInt(out, c.blue());
    out.push_back(',');
    appendInt(out, c.alpha());
    out.push_back(']');
    return out;
}

} // namespace

RecordingDevice::RecordingDevice() = default;

RecordingDevice::~RecordingDevice() = default;

void RecordingDevice::setImageSink(ImageSink sink, ImageForget forget)
{
    m_imageSink = std::move(sink);
    m_imageForget = std::move(forget);
}

void RecordingDevice::evictImages()
{
    if (m_sentImageBytes <= m_imageBudgetBytes) {
        return;
    }
    AnimeCore::Vector<std::pair<std::uint64_t, std::string>> idle;
    for (auto it = m_sentImages.cbegin(); it != m_sentImages.cend(); ++it) {
        if (it.value().lastFrame != m_frameNumber) {
            idle.append({it.value().lastFrame, it.key()});
        }
    }
    std::sort(idle.begin(), idle.end());
    for (const auto &entry : idle) {
        if (m_sentImageBytes <= m_imageBudgetBytes) {
            break;
        }
        m_sentImageBytes -= m_sentImages.value(entry.second).bytes;
        m_sentImages.remove(entry.second);
        if (m_imageForget) {
            m_imageForget(entry.second);
        }
    }
    m_cacheKeyToHash.clear();
}

void RecordingDevice::forgetSentImages()
{
    m_sentImages.clear();
    m_sentImageBytes = 0;
}

void RecordingDevice::resetFrame()
{
    ++m_frameNumber;
    m_ops.clear();

    m_hasEmittedTransform = false;
    m_emittedTransformBytes.clear();

    m_hasEmittedPen = false;
    m_emittedPenBytes.clear();

    m_hasEmittedBrush = false;
    m_emittedBrushBytes.clear();

    m_emittedOpacity = 1.0;
    m_emittedMode = AnimeCore::CompositionMode_SourceOver;
    m_emittedSmooth = -1;
}

void RecordingDevice::appendOp(std::string json)
{
    RecordedOp op;
    op.hash = fnv1a64(json.data(), json.size());
    op.json = std::move(json);
    m_ops.append(std::move(op));
}

// The page draws a field scope into its own target, so pen, brush and opacity
// are said again inside it and again after it.
void RecordingDevice::forgetEmittedStyle()
{
    m_hasEmittedPen = false;
    m_emittedPenBytes.clear();
    m_hasEmittedBrush = false;
    m_emittedBrushBytes.clear();
    m_emittedOpacity = -1.0;
}

void RecordingDevice::beginFieldScope(const AnimeCore::PaintState &state,
                                      const std::string &key,
                                      std::uint64_t version,
                                      const AnimeCore::RectF &rect,
                                      double maxEdge,
                                      const AnimeCore::Vector<FieldScopePass> &passes)
{
    syncCommonState(state);

    std::string out;
    out.reserve(96 + static_cast<std::size_t>(passes.size()) * 24);
    out.append("[\"F\",");
    appendJsonString(out, key);
    out.push_back(',');
    appendUint64(out, version);
    out.push_back(',');
    appendDouble(out, rect.x());
    out.push_back(',');
    appendDouble(out, rect.y());
    out.push_back(',');
    appendDouble(out, rect.width());
    out.push_back(',');
    appendDouble(out, rect.height());
    out.push_back(',');
    appendDouble(out, maxEdge);
    out.append(",[");
    for (int i = 0; i < passes.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out.push_back('[');
        appendDouble(out, passes[i].tagLo);
        out.push_back(',');
        appendDouble(out, passes[i].tagHi);
        out.push_back(',');
        appendDouble(out, passes[i].opacity);
        out.push_back(',');
        appendInt(out, passes[i].tintR);
        out.push_back(',');
        appendInt(out, passes[i].tintG);
        out.push_back(',');
        appendInt(out, passes[i].tintB);
        out.push_back(',');
        appendInt(out, passes[i].tintA);
        out.push_back(']');
    }
    out.append("]]");
    appendOp(std::move(out));
    forgetEmittedStyle();
}

void RecordingDevice::endFieldScope()
{
    appendOp(std::string("[\"f\"]"));
    forgetEmittedStyle();
}

void RecordingDevice::syncTransform(const AnimeCore::Transform &transform)
{
    std::string out;
    out.reserve(64);
    out.append("[\"T\",");
    appendDouble(out, transform.m11());
    out.push_back(',');
    appendDouble(out, transform.m12());
    out.push_back(',');
    appendDouble(out, transform.m21());
    out.push_back(',');
    appendDouble(out, transform.m22());
    out.push_back(',');
    appendDouble(out, transform.dx());
    out.push_back(',');
    appendDouble(out, transform.dy());
    out.push_back(']');

    if (!m_hasEmittedTransform || out != m_emittedTransformBytes) {
        m_hasEmittedTransform = true;
        m_emittedTransformBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingDevice::syncOpacity(double opacity)
{
    if (std::abs(opacity - m_emittedOpacity) <= 1e-9) {
        return;
    }
    m_emittedOpacity = opacity;
    std::string out;
    out.reserve(24);
    out.append("[\"O\",");
    appendDouble(out, opacity);
    out.push_back(']');
    appendOp(std::move(out));
}

void RecordingDevice::syncCompositionMode(AnimeCore::CompositionMode mode)
{
    if (mode == m_emittedMode) {
        return;
    }
    m_emittedMode = mode;
    std::string out;
    out.reserve(32);
    out.append("[\"M\",\"");
    out.append(mapCompositionMode(mode));
    out.append("\"]");
    appendOp(std::move(out));
}

void RecordingDevice::syncPen(const AnimeCore::Pen &pen)
{
    std::string out = serializePenOp(pen);
    if (!m_hasEmittedPen || out != m_emittedPenBytes) {
        m_hasEmittedPen = true;
        m_emittedPenBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingDevice::syncBrush(const AnimeCore::Brush &brush)
{
    std::string out = serializeBrushOp(brush);
    if (!m_hasEmittedBrush || out != m_emittedBrushBytes) {
        m_hasEmittedBrush = true;
        m_emittedBrushBytes = out;
        appendOp(std::move(out));
    }
}

void RecordingDevice::syncSmooth(bool smoothImages)
{
    const int smooth = smoothImages ? 1 : 0;
    if (smooth == m_emittedSmooth) {
        return;
    }
    m_emittedSmooth = smooth;
    std::string out;
    out.reserve(12);
    out.append("[\"H\",");
    out.push_back(smooth ? '1' : '0');
    out.push_back(']');
    appendOp(std::move(out));
}

void RecordingDevice::syncCommonState(const AnimeCore::PaintState &state)
{
    syncTransform(state.transform);
    syncOpacity(state.opacity);
    syncCompositionMode(state.compositionMode);
}

void RecordingDevice::syncStateForShape(const AnimeCore::PaintState &state, bool usePen, bool useBrush)
{
    syncCommonState(state);
    syncPen(usePen ? state.pen : AnimeCore::Pen(AnimeCore::NoPen));
    syncBrush(useBrush ? state.brush : AnimeCore::Brush(AnimeCore::NoBrush));
}

void RecordingDevice::syncStateForImage(const AnimeCore::PaintState &state)
{
    syncCommonState(state);
    syncSmooth(state.smoothImages);
}

void RecordingDevice::drawRects(const AnimeCore::PaintState &state, const AnimeCore::RectF *rects, int count)
{
    if (!rects || count <= 0) {
        return;
    }
    syncStateForShape(state, true, true);
    for (int i = 0; i < count; ++i) {
        const AnimeCore::RectF &r = rects[i];
        std::string out;
        out.reserve(64);
        out.append("[\"R\",");
        appendDouble(out, r.x());
        out.push_back(',');
        appendDouble(out, r.y());
        out.push_back(',');
        appendDouble(out, r.width());
        out.push_back(',');
        appendDouble(out, r.height());
        out.push_back(']');
        appendOp(std::move(out));
    }
}

void RecordingDevice::emitPathOp(const AnimeCore::Path &path)
{
    if (path.isEmpty()) {
        return;
    }
    std::string out;
    out.reserve(static_cast<std::size_t>(32 + path.elementCount() * 24));
    out.append("[\"S\",");
    appendPathArray(out, path);
    out.push_back(',');
    out.push_back(path.fillRule() == AnimeCore::WindingFill ? '1' : '0');
    out.push_back(']');
    appendOp(std::move(out));
}

void RecordingDevice::drawLines(const AnimeCore::PaintState &state, const AnimeCore::LineF *lines, int count)
{
    if (!lines || count <= 0) {
        return;
    }
    syncStateForShape(state, true, false);
    AnimeCore::Path path;
    for (int i = 0; i < count; ++i) {
        path.moveTo(lines[i].p1());
        path.lineTo(lines[i].p2());
    }
    emitPathOp(path);
}

void RecordingDevice::drawEllipse(const AnimeCore::PaintState &state, const AnimeCore::RectF &rect)
{
    syncStateForShape(state, true, true);
    AnimeCore::Path path;
    path.addEllipse(rect);
    emitPathOp(path);
}

void RecordingDevice::drawPath(const AnimeCore::PaintState &state, const AnimeCore::Path &path)
{
    if (path.isEmpty()) {
        return;
    }
    syncStateForShape(state, true, true);
    emitPathOp(path);
}

void RecordingDevice::drawPoints(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count)
{
    if (!points || count <= 0) {
        return;
    }
    syncStateForShape(state, true, false);
    AnimeCore::Path path;
    for (int i = 0; i < count; ++i) {
        path.moveTo(points[i]);
        path.lineTo(points[i] + AnimeCore::PointF(0.001, 0.0));
    }
    emitPathOp(path);
}

void RecordingDevice::drawPolygon(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count,
                                  AnimeCore::PolygonMode mode)
{
    if (!points || count <= 0) {
        return;
    }
    const bool closed = (mode != AnimeCore::PolygonMode::Polyline);
    syncStateForShape(state, true, closed);

    std::string out;
    out.reserve(static_cast<std::size_t>(32 + count * 24));
    out.append("[\"S\",[0,");
    appendDouble(out, points[0].x());
    out.push_back(',');
    appendDouble(out, points[0].y());
    for (int i = 1; i < count; ++i) {
        out.append(",1,");
        appendDouble(out, points[i].x());
        out.push_back(',');
        appendDouble(out, points[i].y());
    }
    if (closed) {
        out.append(",3");
    }
    out.append("],");
    out.push_back(mode == AnimeCore::PolygonMode::Winding ? '1' : '0');
    out.push_back(']');
    appendOp(std::move(out));
}

std::string RecordingDevice::ensureImageSent(const AnimeCore::Image &image)
{
    const std::int64_t cacheKey = image.cacheKey();
    auto cached = m_cacheKeyToHash.constFind(cacheKey);
    if (cached != m_cacheKeyToHash.constEnd() && m_sentImages.contains(*cached)) {
        m_sentImages[*cached].lastFrame = m_frameNumber;
        return *cached;
    }

    const AnimeCore::Image rgba = (image.format() == AnimeCore::Image::Format_RGBA8888)
                                      ? image
                                      : image.convertToFormat(AnimeCore::Image::Format_RGBA8888);

    const int w = rgba.width();
    const int h = rgba.height();
    std::uint64_t hash = 14695981039346656037ULL;
    hash = fnv1a64(&w, sizeof(w), hash);
    hash = fnv1a64(&h, sizeof(h), hash);
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        hash = fnv1a64(rgba.constScanLine(y), rowBytes, hash);
    }

    char hexBuf[16];
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::uint64_t tmp = hash;
    for (int i = 15; i >= 0; --i) {
        hexBuf[i] = kHexDigits[tmp & 0x0fULL];
        tmp >>= 4;
    }
    const std::string key(hexBuf, 16);
    // A new ghost is a new Image each time, whether or not its pixels are new.
    if (m_cacheKeyToHash.size() >= kCacheKeyLimit) {
        m_cacheKeyToHash.clear();
    }
    m_cacheKeyToHash.insert(cacheKey, key);

    if (!m_sentImages.contains(key)) {
        const std::int64_t bytes = std::int64_t(w) * h * 4;
        m_sentImages.insert(key, SentImage{bytes, m_frameNumber});
        m_sentImageBytes += bytes;
        if (m_imageSink) {
            m_imageSink(key, rgba);
        }
    } else {
        m_sentImages[key].lastFrame = m_frameNumber;
    }
    return key;
}

void RecordingDevice::drawImage(const AnimeCore::PaintState &state, const AnimeCore::RectF &target,
                                const AnimeCore::Image &image, const AnimeCore::RectF &source)
{
    if (image.isNull() || target.isEmpty()) {
        return;
    }
    syncStateForImage(state);
    const std::string key = ensureImageSent(image);
    const AnimeCore::RectF src = source.isEmpty()
                                     ? AnimeCore::RectF(0.0, 0.0, image.width(), image.height())
                                     : source;

    std::string out;
    out.reserve(96);
    out.append("[\"I\",\"");
    out.append(key);
    out.append("\",");
    appendDouble(out, src.x());
    out.push_back(',');
    appendDouble(out, src.y());
    out.push_back(',');
    appendDouble(out, src.width());
    out.push_back(',');
    appendDouble(out, src.height());
    out.push_back(',');
    appendDouble(out, target.x());
    out.push_back(',');
    appendDouble(out, target.y());
    out.push_back(',');
    appendDouble(out, target.width());
    out.push_back(',');
    appendDouble(out, target.height());
    out.push_back(']');
    appendOp(std::move(out));
}
