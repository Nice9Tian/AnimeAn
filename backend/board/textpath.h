#ifndef ANIMECORE_TEXTPATH_H
#define ANIMECORE_TEXTPATH_H

#include "algorithm/core/geometry.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"

namespace AnimeCore {

struct TextMetrics {
    double ascent = 0.0;
    double descent = 0.0;
    double height = 0.0;
};

TextMetrics textMetrics(double pixelSize);
double textWidth(const String &text, double pixelSize);
Path textPath(const String &text, double pixelSize, const PointF &baseline);

} // namespace AnimeCore

using AnimeCore::TextMetrics;
using AnimeCore::textMetrics;
using AnimeCore::textWidth;
using AnimeCore::textPath;

#endif // ANIMECORE_TEXTPATH_H
