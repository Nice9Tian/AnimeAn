#include "algorithm/core/pathbool.h"

#include "algorithm/core/pathops.h"

namespace AnimeCore {

namespace {

PolyFillRule ruleOf(const Path &path)
{
    return path.fillRule() == WindingFill ? PolyFillRule::Winding : PolyFillRule::OddEven;
}

// A closed subpath repeats its first vertex at the end; the polygon layer wants
// it once. Filling closes open subpaths implicitly, stroking must not, which is
// why the two kinds are kept apart here.
void flatten(const Path &path, Polygons *closed, Polygons *open)
{
    for (const Polygon &subpath : path.toSubpathPolygons()) {
        if (subpath.size() < 2) {
            continue;
        }
        const PointF &first = subpath.first();
        const PointF &last = subpath.last();
        if (first.x() == last.x() && first.y() == last.y()) {
            Polygon polygon = subpath;
            polygon.removeLast();
            closed->append(polygon);
        } else {
            open->append(subpath);
        }
    }
}

Polygons fillPolygons(const Path &path)
{
    Polygons closed;
    Polygons open;
    flatten(path, &closed, &open);
    closed.append(open);
    return closed;
}

Path pathFrom(const Polygons &polygons, Path::FillRule rule = OddEvenFill)
{
    Path path;
    path.setFillRule(rule);
    for (const Polygon &polygon : polygons) {
        if (polygon.isEmpty()) {
            continue;
        }
        path.moveTo(polygon.first());
        for (int i = 1; i < polygon.size(); ++i) {
            path.lineTo(polygon.at(i));
        }
        path.closeSubpath();
    }
    return path;
}

} // namespace

Path simplified(const Path &path)
{
    return pathFrom(polySimplified(fillPolygons(path), ruleOf(path)));
}

Path united(const Path &a, const Path &b)
{
    return pathFrom(polyUnited(fillPolygons(a), ruleOf(a), fillPolygons(b), ruleOf(b)));
}

Path subtracted(const Path &a, const Path &b)
{
    return pathFrom(polySubtracted(fillPolygons(a), ruleOf(a), fillPolygons(b), ruleOf(b)));
}

Path intersected(const Path &a, const Path &b)
{
    return pathFrom(polyIntersected(fillPolygons(a), ruleOf(a), fillPolygons(b), ruleOf(b)));
}

bool contains(const Path &outer, const Path &inner)
{
    return polyContains(fillPolygons(outer), ruleOf(outer), fillPolygons(inner), ruleOf(inner));
}

bool intersects(const Path &a, const Path &b)
{
    return polyIntersects(fillPolygons(a), ruleOf(a), fillPolygons(b), ruleOf(b));
}

Path strokeOutline(const Path &path, double width)
{
    Polygons closed;
    Polygons open;
    flatten(path, &closed, &open);
    return pathFrom(polyStrokeOutline(open, closed, width, CapStyle::Round, JoinStyle::Round),
                    WindingFill);
}

} // namespace AnimeCore
