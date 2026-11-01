#ifndef ANIMECORE_PATHOPS_H
#define ANIMECORE_PATHOPS_H

#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"

namespace AnimeCore {

using Polygon = Vector<PointF>;
using Polygons = Vector<Polygon>;

enum class PolyFillRule { OddEven, Winding };
enum class CapStyle { Flat, Square, Round };
enum class JoinStyle { Miter, Bevel, Round };

Polygons polySimplified(const Polygons &, PolyFillRule);
Polygons polyUnited(const Polygons &a, PolyFillRule, const Polygons &b, PolyFillRule);
Polygons polySubtracted(const Polygons &a, PolyFillRule, const Polygons &b, PolyFillRule);
Polygons polyIntersected(const Polygons &a, PolyFillRule, const Polygons &b, PolyFillRule);
Polygons polyStrokeOutline(const Polygons &open, const Polygons &closed, double width, CapStyle, JoinStyle);
bool polyContains(const Polygons &outer, PolyFillRule, const Polygons &inner, PolyFillRule);
bool polyIntersects(const Polygons &a, PolyFillRule, const Polygons &b, PolyFillRule);

} // namespace AnimeCore

#endif // ANIMECORE_PATHOPS_H
