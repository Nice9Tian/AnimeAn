#ifndef ANIMECORE_TEXTGLYPHS_H
#define ANIMECORE_TEXTGLYPHS_H

// Where glyph outlines come from. textpath.cpp lays text out, scales it and
// caches; the system's font machinery only hands over one glyph of the
// interface font at a whole pixel size. Internal to backend/board:
// textglyphs_gdi.cpp is the Windows side, textglyphs_freetype.cpp the other.

#include "algorithm/core/path.h"
#include "backend/board/textpath.h"

#include <cstdint>

namespace AnimeCore {
namespace TextGlyphs {

struct Glyph {
    // y grows downwards, the origin is the pen position on the baseline.
    Path outline;
    double advance = 0.0;
};

// Ascent, descent and their sum for the first interface font at this size.
// All zero when the system has no font to offer.
TextMetrics metrics(int pixelSize);

// One printable code point at this size, from the first interface font that
// has it; the first font's "missing" box when none has.
Glyph glyph(int pixelSize, std::uint32_t codepoint);

} // namespace TextGlyphs
} // namespace AnimeCore

#endif // ANIMECORE_TEXTGLYPHS_H
