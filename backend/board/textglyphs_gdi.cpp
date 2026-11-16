// Windows side of textglyphs.h: TrueType outlines through GDI.

#include "backend/board/textglyphs.h"

#include "algorithm/core/containers.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

// One memory device context and the interface fonts at each size asked for.
class GdiFonts
{
public:
    static GdiFonts &instance()
    {
        static GdiFonts fonts;
        return fonts;
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

        m_fonts.insert(px, set);
        return *m_fonts.constFind(px);
    }

    HDC dc() const noexcept { return m_dc; }

private:
    GdiFonts() = default;

    ~GdiFonts()
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
};

// The first font of the list that has this character, the first font when none has.
HFONT fontForCodepoint(HDC dc, const FontSet &set, std::uint32_t codepoint)
{
    HFONT chosenFont = set.fonts[0];
    if (codepoint > 0xFFFFu) {
        return chosenFont;
    }
    const wchar_t wch = static_cast<wchar_t>(codepoint);
    for (int i = 0; i < kFontFamilyCount; ++i) {
        if (!set.fonts[i]) {
            continue;
        }
        SelectObject(dc, set.fonts[i]);
        WORD glyphIndex = 0xFFFF;
        const DWORD res = GetGlyphIndicesW(dc, &wch, 1, &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS);
        if (res != GDI_ERROR && glyphIndex != 0xFFFF && glyphIndex != 0) {
            chosenFont = set.fonts[i];
            break;
        }
    }
    return chosenFont;
}

} // namespace

namespace TextGlyphs {

TextMetrics metrics(int pixelSize)
{
    return GdiFonts::instance().fontSetForSize(pixelSize).metrics;
}

Glyph glyph(int pixelSize, std::uint32_t codepoint)
{
    Glyph built;
    GdiFonts &gdi = GdiFonts::instance();
    const FontSet &set = gdi.fontSetForSize(pixelSize);
    const HDC dc = gdi.dc();
    if (!dc) {
        return built;
    }

    const HFONT chosenFont = fontForCodepoint(dc, set, codepoint);
    if (!chosenFont) {
        return built;
    }

    SelectObject(dc, chosenFont);
    GLYPHMETRICS gm{};
    const MAT2 mat = identityMat2();
    const UINT format = GGO_NATIVE | GGO_UNHINTED;
    const DWORD bytes = GetGlyphOutlineW(dc, static_cast<UINT>(codepoint), format, &gm, 0, nullptr, &mat);
    if (bytes == GDI_ERROR) {
        return built;
    }

    built.advance = static_cast<double>(gm.gmCellIncX);
    if (bytes > 0) {
        std::vector<std::uint8_t> buffer(bytes);
        if (GetGlyphOutlineW(dc, static_cast<UINT>(codepoint), format, &gm, bytes, buffer.data(), &mat)
            != GDI_ERROR) {
            built.outline = parseGlyphOutline(buffer.data(), bytes);
        }
    }
    return built;
}

} // namespace TextGlyphs

} // namespace AnimeCore
