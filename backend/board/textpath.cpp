#include "backend/board/textpath.h"

#include "algorithm/core/containers.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AnimeCore {

namespace {

constexpr const wchar_t *kFontFamilies[] = {
    L"Segoe UI",
    L"Microsoft YaHei UI",
    L"Yu Gothic UI",
};
constexpr int kFontFamilyCount = static_cast<int>(sizeof(kFontFamilies) / sizeof(kFontFamilies[0]));

struct FontSet {
    HFONT fonts[kFontFamilyCount] = {};
    TextMetrics metrics;
};

struct CachedGlyph {
    Path outline;
    double advance = 0.0;
};

double fxToDouble(const FIXED &f) noexcept
{
    return static_cast<double>(f.value) + static_cast<double>(f.fract) / 65536.0;
}

PointF fxPoint(const POINTFX &p) noexcept
{
    return PointF(fxToDouble(p.x), -fxToDouble(p.y));
}

MAT2 identityMat2() noexcept
{
    MAT2 mat{};
    mat.eM11.value = 1;
    mat.eM22.value = 1;
    return mat;
}

Path parseGlyphOutline(const std::uint8_t *data, DWORD totalBytes)
{
    Path path;
    path.setFillRule(WindingFill);

    DWORD polyOffset = 0;
    while (polyOffset + sizeof(TTPOLYGONHEADER) <= totalBytes) {
        const auto *header = reinterpret_cast<const TTPOLYGONHEADER *>(data + polyOffset);
        if (header->cb < sizeof(TTPOLYGONHEADER) || polyOffset + header->cb > totalBytes) {
            break;
        }

        path.moveTo(fxPoint(header->pfxStart));

        DWORD curveOffset = sizeof(TTPOLYGONHEADER);
        while (curveOffset + offsetof(TTPOLYCURVE, apfx) <= header->cb) {
            const auto *curve = reinterpret_cast<const TTPOLYCURVE *>(
                data + polyOffset + curveOffset);
            const DWORD curveBytes = static_cast<DWORD>(
                offsetof(TTPOLYCURVE, apfx) + sizeof(POINTFX) * curve->cpfx);
            if (curve->cpfx == 0 || curveOffset + curveBytes > header->cb) {
                break;
            }

            if (curve->wType == TT_PRIM_LINE) {
                for (WORD i = 0; i < curve->cpfx; ++i) {
                    path.lineTo(fxPoint(curve->apfx[i]));
                }
            } else if (curve->wType == TT_PRIM_QSPLINE) {
                if (curve->cpfx == 1) {
                    path.lineTo(fxPoint(curve->apfx[0]));
                } else {
                    for (WORD i = 0; i + 1 < curve->cpfx; ++i) {
                        const PointF ctrl = fxPoint(curve->apfx[i]);
                        const PointF next = fxPoint(curve->apfx[i + 1]);
                        const PointF endPt = (i + 2 < curve->cpfx)
                                                 ? PointF(0.5 * (ctrl.x() + next.x()),
                                                          0.5 * (ctrl.y() + next.y()))
                                                 : next;
                        path.quadTo(ctrl, endPt);
                    }
                }
            } else if (curve->wType == TT_PRIM_CSPLINE) {
                for (WORD i = 0; i + 2 < curve->cpfx; i += 3) {
                    path.cubicTo(fxPoint(curve->apfx[i]),
                                 fxPoint(curve->apfx[i + 1]),
                                 fxPoint(curve->apfx[i + 2]));
                }
            }

            curveOffset += curveBytes;
        }

        path.closeSubpath();
        polyOffset += header->cb;
    }

    return path;
}

class GdiTextCache
{
public:
    static GdiTextCache &instance()
    {
        static GdiTextCache cache;
        return cache;
    }

    const FontSet &fontSetForSize(int px)
    {
        auto it = m_fonts.constFind(px);
        if (it != m_fonts.constEnd()) {
            return *it;
        }

        ensureDc();
        FontSet set;
        for (int i = 0; i < kFontFamilyCount; ++i) {
            set.fonts[i] = CreateFontW(-px,
                                       0,
                                       0,
                                       0,
                                       FW_NORMAL,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       DEFAULT_CHARSET,
                                       OUT_TT_ONLY_PRECIS,
                                       CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE,
                                       kFontFamilies[i]);
        }

        if (m_dc && set.fonts[0]) {
            SelectObject(m_dc, set.fonts[0]);
            TEXTMETRICW tm{};
            if (GetTextMetricsW(m_dc, &tm)) {
                set.metrics.ascent = static_cast<double>(tm.tmAscent);
                set.metrics.descent = static_cast<double>(tm.tmDescent);
                set.metrics.height = static_cast<double>(tm.tmHeight);
            }
        }
        if (set.metrics.height <= 0.0) {
            set.metrics.ascent = static_cast<double>(px);
            set.metrics.descent = std::max(1.0, std::round(px * 0.25));
            set.metrics.height = set.metrics.ascent + set.metrics.descent;
        }

        m_fonts.insert(px, set);
        return *m_fonts.constFind(px);
    }

    const CachedGlyph &glyph(int px, std::uint32_t codepoint)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32)
            | static_cast<std::uint64_t>(codepoint);
        auto it = m_glyphs.constFind(key);
        if (it != m_glyphs.constEnd()) {
            return *it;
        }

        const FontSet &set = fontSetForSize(px);
        CachedGlyph built;

        if (m_dc && codepoint >= 0x20u) {
            HFONT chosenFont = set.fonts[0];
            if (codepoint <= 0xFFFFu) {
                const wchar_t wch = static_cast<wchar_t>(codepoint);
                for (int i = 0; i < kFontFamilyCount; ++i) {
                    if (!set.fonts[i]) {
                        continue;
                    }
                    SelectObject(m_dc, set.fonts[i]);
                    WORD glyphIndex = 0xFFFF;
                    const DWORD res = GetGlyphIndicesW(
                        m_dc, &wch, 1, &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS);
                    if (res != GDI_ERROR && glyphIndex != 0xFFFF && glyphIndex != 0) {
                        chosenFont = set.fonts[i];
                        break;
                    }
                }
            }

            if (chosenFont) {
                SelectObject(m_dc, chosenFont);
                GLYPHMETRICS gm{};
                const MAT2 mat = identityMat2();
                const UINT format = GGO_NATIVE | GGO_UNHINTED;
                const DWORD bytes = GetGlyphOutlineW(
                    m_dc, static_cast<UINT>(codepoint), format, &gm, 0, nullptr, &mat);
                if (bytes != GDI_ERROR) {
                    built.advance = static_cast<double>(gm.gmCellIncX);
                    if (bytes > 0) {
                        std::vector<std::uint8_t> buffer(bytes);
                        if (GetGlyphOutlineW(m_dc,
                                             static_cast<UINT>(codepoint),
                                             format,
                                             &gm,
                                             bytes,
                                             buffer.data(),
                                             &mat) != GDI_ERROR) {
                            built.outline = parseGlyphOutline(buffer.data(), bytes);
                        }
                    }
                }
            }
        }

        m_glyphs.insert(key, built);
        return *m_glyphs.constFind(key);
    }

private:
    GdiTextCache() = default;

    ~GdiTextCache()
    {
        if (m_dc && m_origFont) {
            SelectObject(m_dc, m_origFont);
        }
        for (auto it = m_fonts.cbegin(); it != m_fonts.cend(); ++it) {
            for (int i = 0; i < kFontFamilyCount; ++i) {
                if (it->fonts[i]) {
                    DeleteObject(it->fonts[i]);
                }
            }
        }
        if (m_dc) {
            DeleteDC(m_dc);
        }
    }

    void ensureDc()
    {
        if (!m_dc) {
            m_dc = CreateCompatibleDC(nullptr);
            if (m_dc) {
                m_origFont = GetCurrentObject(m_dc, OBJ_FONT);
            }
        }
    }

    HDC m_dc = nullptr;
    HGDIOBJ m_origFont = nullptr;
    Hash<int, FontSet> m_fonts;
    Hash<std::uint64_t, CachedGlyph> m_glyphs;
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
    const TextMetrics &raw = GdiTextCache::instance().fontSetForSize(px).metrics;
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
    GdiTextCache &cache = GdiTextCache::instance();
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
    GdiTextCache &cache = GdiTextCache::instance();
    for (int i = 0; i < text.size(); ++i) {
        const std::uint32_t cp = nextCodepoint(text, &i);
        const CachedGlyph &g = cache.glyph(px, cp);
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
