#include "backend/board/textpath.h"

#include "algorithm/core/containers.h"
#include "backend/board/textglyphs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AnimeCore {

namespace {

// Metrics and glyphs are asked of the system once per whole pixel size and kept.
class TextCache
{
public:
    static TextCache &instance()
    {
        static TextCache cache;
        return cache;
    }

    const TextMetrics &metrics(int px)
    {
        auto it = m_metrics.constFind(px);
        if (it != m_metrics.constEnd()) {
            return *it;
        }

        TextMetrics found = TextGlyphs::metrics(px);
        if (found.height <= 0.0) {
            // No font to measure: the usual proportions of one.
            found.ascent = static_cast<double>(px);
            found.descent = std::max(1.0, std::round(px * 0.25));
            found.height = found.ascent + found.descent;
        }

        m_metrics.insert(px, found);
        return *m_metrics.constFind(px);
    }

    const TextGlyphs::Glyph &glyph(int px, std::uint32_t codepoint)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32)
            | static_cast<std::uint64_t>(codepoint);
        auto it = m_glyphs.constFind(key);
        if (it != m_glyphs.constEnd()) {
            return *it;
        }

        // Control characters draw nothing and take no room.
        TextGlyphs::Glyph built;
        if (codepoint >= 0x20u) {
            built = TextGlyphs::glyph(px, codepoint);
        }

        m_glyphs.insert(key, built);
        return *m_glyphs.constFind(key);
    }

private:
    TextCache() = default;

    Hash<int, TextMetrics> m_metrics;
    Hash<std::uint64_t, TextGlyphs::Glyph> m_glyphs;
};

int normalizedPixelSize(double pixelSize, double *outScale) noexcept
{
    if (pixelSize <= 0.0 || !std::isfinite(pixelSize)) {
        if (outScale) {
            *outScale = 0.0;
        }
        return 0;
    }
    const int px = std::max(1, round(pixelSize));
    if (outScale) {
        *outScale = pixelSize / static_cast<double>(px);
    }
    return px;
}

std::uint32_t nextCodepoint(const String &text, int *index) noexcept
{
    const Char first = text.at(*index);
    std::uint32_t cp = first.unicode();
    if (first.isHighSurrogate() && *index + 1 < text.size()) {
        const Char second = text.at(*index + 1);
        if (second.isLowSurrogate()) {
            cp = 0x10000u + (((cp - 0xD800u) << 10) | (second.unicode() - 0xDC00u));
            ++(*index);
        }
    }
    return cp;
}

} // namespace

TextMetrics textMetrics(double pixelSize)
{
    double scale = 1.0;
    const int px = normalizedPixelSize(pixelSize, &scale);
    if (px <= 0) {
        return TextMetrics{};
    }
    const TextMetrics &raw = TextCache::instance().metrics(px);
    return TextMetrics{
        raw.ascent * scale,
        raw.descent * scale,
        raw.height * scale,
    };
}

double textWidth(const String &text, double pixelSize)
{
    double scale = 1.0;
    const int px = normalizedPixelSize(pixelSize, &scale);
    if (px <= 0 || text.isEmpty()) {
        return 0.0;
    }
    double total = 0.0;
    TextCache &cache = TextCache::instance();
    for (int i = 0; i < text.size(); ++i) {
        const std::uint32_t cp = nextCodepoint(text, &i);
        total += cache.glyph(px, cp).advance * scale;
    }
    return total;
}

Path textPath(const String &text, double pixelSize, const PointF &baseline)
{
    Path result;
    result.setFillRule(WindingFill);
    double scale = 1.0;
    const int px = normalizedPixelSize(pixelSize, &scale);
    if (px <= 0 || text.isEmpty()) {
        return result;
    }

    double penX = baseline.x();
    const double penY = baseline.y();
    TextCache &cache = TextCache::instance();
    for (int i = 0; i < text.size(); ++i) {
        const std::uint32_t cp = nextCodepoint(text, &i);
        const TextGlyphs::Glyph &g = cache.glyph(px, cp);
        if (!g.outline.isEmpty()) {
            const Path placed = (scale == 1.0)
                                    ? g.outline.translated(penX, penY)
                                    : g.outline.transformed(Transform(scale, 0.0, 0.0, scale, penX, penY));
            result.addPath(placed);
        }
        penX += g.advance * scale;
    }
    return result;
}

} // namespace AnimeCore
