// Non-Windows side of textglyphs.h: Fontconfig finds the fonts, FreeType reads
// the outlines.

#include "backend/board/textglyphs.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <fontconfig/fontconfig.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace AnimeCore {

namespace {

// The interface fonts, most wanted first. The Windows build uses Segoe UI, then
// Microsoft YaHei UI for Chinese and Yu Gothic UI for Japanese; a machine that
// has those fonts draws the same text here. The families after each one stand
// in for it where it is missing.
constexpr const char *kFontFamilies[] = {
    "Segoe UI",
    "Noto Sans",
    "DejaVu Sans",
    "Microsoft YaHei UI",
    "Noto Sans CJK SC",
    "Yu Gothic UI",
    "Noto Sans CJK JP",
};

struct FontFile {
    std::string path;
    int faceIndex = 0;

    bool operator==(const FontFile &other) const noexcept
    {
        return faceIndex == other.faceIndex && path == other.path;
    }
};

// A font carries its family name in several languages; any of them counts.
bool hasFamilyName(FcPattern *font, const char *family)
{
    FcChar8 *name = nullptr;
    for (int n = 0; FcPatternGetString(font, FC_FAMILY, n, &name) == FcResultMatch; ++n) {
        if (FcStrCmpIgnoreCase(name, reinterpret_cast<const FcChar8 *>(family)) == 0) {
            return true;
        }
    }
    return false;
}

// The regular outline font Fontconfig picks for this family. With
// `exactFamily`, nothing when the system has no font of that name (Fontconfig
// otherwise answers with its closest guess).
bool findFont(const char *family, bool exactFamily, FontFile *out)
{
    FcPattern *pattern = FcPatternCreate();
    if (!pattern) {
        return false;
    }
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family));
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    FcPatternAddBool(pattern, FC_OUTLINE, FcTrue);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match) {
        return false;
    }

    bool found = false;
    FcChar8 *file = nullptr;
    int index = 0;
    const bool familyOk = !exactFamily || hasFamilyName(match, family);
    if (familyOk && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
        out->path = reinterpret_cast<const char *>(file);
        out->faceIndex = index;
        found = true;
    }
    FcPatternDestroy(match);
    return found;
}

std::vector<FontFile> interfaceFontFiles()
{
    std::vector<FontFile> files;
    auto addOnce = [&files](const FontFile &file) {
        for (const FontFile &have : files) {
            if (have == file) {
                return;
            }
        }
        files.push_back(file);
    };

    FontFile file;
    for (const char *family : kFontFamilies) {
        if (findFont(family, true, &file)) {
            addOnce(file);
        }
    }
    // Whatever the system calls its sans-serif font goes last, so there is
    // something to draw with on a machine that has none of the named ones.
    if (findFont("sans-serif", false, &file)) {
        addOnce(file);
    }
    return files;
}

PointF outlinePoint(const FT_Vector *v) noexcept
{
    // 26.6 fixed point, y up; paths here have y down.
    return PointF(static_cast<double>(v->x) / 64.0, -static_cast<double>(v->y) / 64.0);
}

struct OutlineSink {
    Path path;
    bool open = false;
};

int outlineMoveTo(const FT_Vector *to, void *user)
{
    auto *sink = static_cast<OutlineSink *>(user);
    if (sink->open) {
        sink->path.closeSubpath();
    }
    sink->path.moveTo(outlinePoint(to));
    sink->open = true;
    return 0;
}

int outlineLineTo(const FT_Vector *to, void *user)
{
    static_cast<OutlineSink *>(user)->path.lineTo(outlinePoint(to));
    return 0;
}

int outlineConicTo(const FT_Vector *control, const FT_Vector *to, void *user)
{
    static_cast<OutlineSink *>(user)->path.quadTo(outlinePoint(control), outlinePoint(to));
    return 0;
}

int outlineCubicTo(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
    static_cast<OutlineSink *>(user)->path.cubicTo(outlinePoint(c1), outlinePoint(c2), outlinePoint(to));
    return 0;
}

Path outlineToPath(FT_Outline *outline)
{
    OutlineSink sink;
    sink.path.setFillRule(WindingFill);

    FT_Outline_Funcs funcs{};
    funcs.move_to = outlineMoveTo;
    funcs.line_to = outlineLineTo;
    funcs.conic_to = outlineConicTo;
    funcs.cubic_to = outlineCubicTo;
    if (FT_Outline_Decompose(outline, &funcs, &sink) != 0) {
        return Path();
    }
    if (sink.open) {
        sink.path.closeSubpath();
    }
    return sink.path;
}

// The FreeType library and one face per interface font, opened on first use
// and kept for the life of the program.
class FreeTypeFonts
{
public:
    static FreeTypeFonts &instance()
    {
        static FreeTypeFonts fonts;
        return fonts;
    }

    const std::vector<FT_Face> &faces() const noexcept { return m_faces; }

private:
    FreeTypeFonts()
    {
        if (FT_Init_FreeType(&m_library) != 0) {
            m_library = nullptr;
            return;
        }
        for (const FontFile &file : interfaceFontFiles()) {
            FT_Face face = nullptr;
            if (FT_New_Face(m_library, file.path.c_str(), file.faceIndex, &face) != 0) {
                continue;
            }
            if (!FT_IS_SCALABLE(face)) {
                FT_Done_Face(face);
                continue;
            }
            m_faces.push_back(face);
        }
    }

    ~FreeTypeFonts()
    {
        for (FT_Face face : m_faces) {
            FT_Done_Face(face);
        }
        if (m_library) {
            FT_Done_FreeType(m_library);
        }
    }

    FT_Library m_library = nullptr;
    std::vector<FT_Face> m_faces;
};

// The first face that has this character; the first face (whose glyph 0 is the
// "missing" box) when none has.
FT_Face faceForCodepoint(const std::vector<FT_Face> &faces, std::uint32_t codepoint, FT_UInt *glyphIndex)
{
    for (FT_Face face : faces) {
        const FT_UInt index = FT_Get_Char_Index(face, codepoint);
        if (index != 0) {
            *glyphIndex = index;
            return face;
        }
    }
    *glyphIndex = 0;
    return faces.front();
}

} // namespace

namespace TextGlyphs {

// Whole pixels, as the Windows side reports them, so a label takes the same
// room on both systems when the fonts are the same.
TextMetrics metrics(int pixelSize)
{
    const std::vector<FT_Face> &faces = FreeTypeFonts::instance().faces();
    if (faces.empty() || FT_Set_Pixel_Sizes(faces.front(), 0, static_cast<FT_UInt>(pixelSize)) != 0) {
        return TextMetrics{};
    }
    const FT_Size_Metrics &m = faces.front()->size->metrics;
    TextMetrics out;
    out.ascent = std::round(static_cast<double>(m.ascender) / 64.0);
    out.descent = std::round(-static_cast<double>(m.descender) / 64.0);
    out.height = out.ascent + out.descent;
    return out;
}

Glyph glyph(int pixelSize, std::uint32_t codepoint)
{
    Glyph built;
    const std::vector<FT_Face> &faces = FreeTypeFonts::instance().faces();
    if (faces.empty()) {
        return built;
    }

    FT_UInt glyphIndex = 0;
    const FT_Face face = faceForCodepoint(faces, codepoint, &glyphIndex);
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0
        || FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
        return built;
    }

    built.advance = std::round(static_cast<double>(face->glyph->advance.x) / 64.0);
    if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        built.outline = outlineToPath(&face->glyph->outline);
    }
    return built;
}

} // namespace TextGlyphs

} // namespace AnimeCore
