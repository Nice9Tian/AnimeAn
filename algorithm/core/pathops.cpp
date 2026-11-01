#include "algorithm/core/pathops.h"

#include "clipper2/clipper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AnimeCore {

namespace {

// Power-of-two scale (1/4096 ~= 0.000244 px < 0.001 px) keeps dyadic
// coordinates bit-exact across double <-> int64_t conversions.
constexpr double kScale = 4096.0;
constexpr double kInvScale = 1.0 / kScale;
constexpr double kArcTolerancePx = 0.02;
constexpr double kTouchEps = 1e-6;
constexpr double kTouchEpsSq = kTouchEps * kTouchEps;

Clipper2Lib::FillRule toClipperFillRule(PolyFillRule rule) noexcept
{
    return rule == PolyFillRule::OddEven ? Clipper2Lib::FillRule::EvenOdd
                                     : Clipper2Lib::FillRule::NonZero;
}

Clipper2Lib::JoinType toClipperJoinType(JoinStyle join) noexcept
{
    switch (join) {
    case JoinStyle::Miter:
        return Clipper2Lib::JoinType::Miter;
    case JoinStyle::Bevel:
        return Clipper2Lib::JoinType::Bevel;
    case JoinStyle::Round:
        return Clipper2Lib::JoinType::Round;
    }
    return Clipper2Lib::JoinType::Round;
}

Clipper2Lib::EndType toClipperEndType(CapStyle cap) noexcept
{
    switch (cap) {
    case CapStyle::Flat:
        return Clipper2Lib::EndType::Butt;
    case CapStyle::Square:
        return Clipper2Lib::EndType::Square;
    case CapStyle::Round:
        return Clipper2Lib::EndType::Round;
    }
    return Clipper2Lib::EndType::Round;
}

Clipper2Lib::Point64 toPoint64(const PointF &p) noexcept
{
    return Clipper2Lib::Point64(std::llround(p.x() * kScale),
                                std::llround(p.y() * kScale));
}

PointF toPointF(const Clipper2Lib::Point64 &p) noexcept
{
    return PointF(static_cast<double>(p.x) * kInvScale,
                  static_cast<double>(p.y) * kInvScale);
}

Clipper2Lib::Paths64 toClosedPaths64(const Polygons &polys)
{
    Clipper2Lib::Paths64 out;
    out.reserve(static_cast<std::size_t>(polys.size()));
    for (const Polygon &poly : polys) {
        if (poly.size() < 3)
            continue;
        Clipper2Lib::Path64 path;
        path.reserve(static_cast<std::size_t>(poly.size()));
        for (const PointF &pt : poly) {
            if (!std::isfinite(pt.x()) || !std::isfinite(pt.y()))
                continue;
            const Clipper2Lib::Point64 p64 = toPoint64(pt);
            if (path.empty() || path.back() != p64)
                path.push_back(p64);
        }
        while (path.size() > 1 && path.back() == path.front())
            path.pop_back();
        if (path.size() >= 3)
            out.push_back(std::move(path));
    }
    return out;
}

Clipper2Lib::Paths64 toOpenPaths64(const Polygons &polys)
{
    Clipper2Lib::Paths64 out;
    out.reserve(static_cast<std::size_t>(polys.size()));
    for (const Polygon &poly : polys) {
        if (poly.size() < 2)
            continue;
        Clipper2Lib::Path64 path;
        path.reserve(static_cast<std::size_t>(poly.size()));
        for (const PointF &pt : poly) {
            if (!std::isfinite(pt.x()) || !std::isfinite(pt.y()))
                continue;
            const Clipper2Lib::Point64 p64 = toPoint64(pt);
            if (path.empty() || path.back() != p64)
                path.push_back(p64);
        }
        if (path.size() >= 2)
            out.push_back(std::move(path));
    }
    return out;
}

Polygons fromPaths64(const Clipper2Lib::Paths64 &paths)
{
    Polygons out;
    out.reserve(static_cast<std::ptrdiff_t>(paths.size()));
    for (const Clipper2Lib::Path64 &path : paths) {
        std::size_t n = path.size();
        while (n > 1 && path[n - 1] == path[0])
            --n;
        if (n < 3)
            continue;
        Polygon poly;
        poly.reserve(static_cast<std::ptrdiff_t>(n));
        for (std::size_t i = 0; i < n; ++i) {
            const PointF pt = toPointF(path[i]);
            if (poly.isEmpty() || poly.last() != pt)
                poly.append(pt);
        }
        while (poly.size() > 1 && poly.last() == poly.first())
            poly.removeLast();
        if (poly.size() >= 3)
            out.append(std::move(poly));
    }
    return out;
}

Clipper2Lib::Paths64 simplifyPaths64(const Clipper2Lib::Paths64 &paths, PolyFillRule rule)
{
    Clipper2Lib::Paths64 result;
    if (paths.empty())
        return result;
    Clipper2Lib::Clipper64 clipper;
    clipper.PreserveCollinear(false);
    clipper.AddSubject(paths);
    clipper.Execute(Clipper2Lib::ClipType::Union, toClipperFillRule(rule), result);
    return result;
}

Polygons executeBooleanOp(Clipper2Lib::ClipType clipType,
                          const Polygons &a, PolyFillRule ruleA,
                          const Polygons &b, PolyFillRule ruleB)
{
    Clipper2Lib::Paths64 sub = toClosedPaths64(a);
    Clipper2Lib::Paths64 clp = toClosedPaths64(b);

    if (clipType == Clipper2Lib::ClipType::Intersection) {
        if (sub.empty() || clp.empty())
            return Polygons();
    } else if (clipType == Clipper2Lib::ClipType::Difference) {
        if (sub.empty())
            return Polygons();
        if (clp.empty())
            return fromPaths64(simplifyPaths64(sub, ruleA));
    } else if (clipType == Clipper2Lib::ClipType::Union) {
        if (sub.empty() && clp.empty())
            return Polygons();
        if (sub.empty())
            return fromPaths64(simplifyPaths64(clp, ruleB));
        if (clp.empty())
            return fromPaths64(simplifyPaths64(sub, ruleA));
    }

    // Clipper executes a single fill rule across subject and clip; when the
    // two inputs carry different rules, normalize the OddEven side first so
    // its winding numbers match NonZero as well.
    Clipper2Lib::FillRule execRule = toClipperFillRule(ruleA);
    if (ruleA != ruleB) {
        if (ruleA == PolyFillRule::OddEven)
            sub = simplifyPaths64(sub, PolyFillRule::OddEven);
        if (ruleB == PolyFillRule::OddEven)
            clp = simplifyPaths64(clp, PolyFillRule::OddEven);
        execRule = Clipper2Lib::FillRule::NonZero;
    }

    Clipper2Lib::Paths64 result;
    Clipper2Lib::Clipper64 clipper;
    clipper.PreserveCollinear(false);
    clipper.AddSubject(sub);
    clipper.AddClip(clp);
    clipper.Execute(clipType, execRule, result);
    return fromPaths64(result);
}

struct BBox
{
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    bool isValid() const noexcept { return minX <= maxX && minY <= maxY; }
};

double signedPolygonArea(const Polygon &poly) noexcept
{
    const std::ptrdiff_t n = poly.size();
    if (n < 3)
        return 0.0;
    double area2 = 0.0;
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        const PointF &p0 = poly[i];
        const PointF &p1 = poly[(i + 1) % n];
        area2 += p0.x() * p1.y() - p1.x() * p0.y();
    }
    return 0.5 * area2;
}

Polygons cleanClosedPolygons(const Polygons &polys, BBox *outBox = nullptr)
{
    Polygons out;
    out.reserve(polys.size());
    BBox box;
    for (const Polygon &poly : polys) {
        if (poly.size() < 3)
            continue;
        Polygon cleaned;
        cleaned.reserve(poly.size());
        for (const PointF &pt : poly) {
            if (!std::isfinite(pt.x()) || !std::isfinite(pt.y()))
                continue;
            if (cleaned.isEmpty()
                || (std::abs(cleaned.last().x() - pt.x()) > 1e-12
                    || std::abs(cleaned.last().y() - pt.y()) > 1e-12)) {
                cleaned.append(pt);
            }
        }
        while (cleaned.size() > 1
               && std::abs(cleaned.last().x() - cleaned.first().x()) <= 1e-12
               && std::abs(cleaned.last().y() - cleaned.first().y()) <= 1e-12) {
            cleaned.removeLast();
        }
        if (cleaned.size() < 3 || std::abs(signedPolygonArea(cleaned)) <= 1e-10)
            continue;
        for (const PointF &pt : cleaned) {
            box.minX = std::min(box.minX, pt.x());
            box.minY = std::min(box.minY, pt.y());
            box.maxX = std::max(box.maxX, pt.x());
            box.maxY = std::max(box.maxY, pt.y());
        }
        out.append(std::move(cleaned));
    }
    if (outBox)
        *outBox = box;
    return out;
}

double pointToSegmentDistSq(const PointF &p, const PointF &a, const PointF &b) noexcept
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-24) {
        const double ex = p.x() - a.x();
        const double ey = p.y() - a.y();
        return ex * ex + ey * ey;
    }
    const double t = bound(0.0, ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / lenSq, 1.0);
    const double ex = p.x() - (a.x() + t * dx);
    const double ey = p.y() - (a.y() + t * dy);
    return ex * ex + ey * ey;
}

double crossProduct(const PointF &o, const PointF &a, const PointF &b) noexcept
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool segmentsTouchOrCross(const PointF &a, const PointF &b,
                          const PointF &c, const PointF &d) noexcept
{
    if (std::max(a.x(), b.x()) + kTouchEps < std::min(c.x(), d.x())
        || std::max(c.x(), d.x()) + kTouchEps < std::min(a.x(), b.x())
        || std::max(a.y(), b.y()) + kTouchEps < std::min(c.y(), d.y())
        || std::max(c.y(), d.y()) + kTouchEps < std::min(a.y(), b.y())) {
        return false;
    }

    const double d1 = crossProduct(a, b, c);
    const double d2 = crossProduct(a, b, d);
    const double d3 = crossProduct(c, d, a);
    const double d4 = crossProduct(c, d, b);
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0))
        && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
        return true;
    }

    return pointToSegmentDistSq(a, c, d) <= kTouchEpsSq
        || pointToSegmentDistSq(b, c, d) <= kTouchEpsSq
        || pointToSegmentDistSq(c, a, b) <= kTouchEpsSq
        || pointToSegmentDistSq(d, a, b) <= kTouchEpsSq;
}

bool anyEdgesTouchOrCross(const Polygons &polysA, const Polygons &polysB) noexcept
{
    for (const Polygon &pa : polysA) {
        const std::ptrdiff_t na = pa.size();
        for (std::ptrdiff_t i = 0; i < na; ++i) {
            const PointF &a0 = pa[i];
            const PointF &a1 = pa[(i + 1) % na];
            for (const Polygon &pb : polysB) {
                const std::ptrdiff_t nb = pb.size();
                for (std::ptrdiff_t j = 0; j < nb; ++j) {
                    const PointF &b0 = pb[j];
                    const PointF &b1 = pb[(j + 1) % nb];
                    if (segmentsTouchOrCross(a0, a1, b0, b1))
                        return true;
                }
            }
        }
    }
    return false;
}

bool pointInPolygons(const PointF &pt, const Polygons &polys, PolyFillRule rule) noexcept
{
    int totalWinding = 0;
    int oddEvenParity = 0;
    for (const Polygon &poly : polys) {
        const std::ptrdiff_t n = poly.size();
        if (n < 3)
            continue;
        int w = 0;
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            const PointF &a = poly[i];
            const PointF &b = poly[(i + 1) % n];
            if (a.y() <= pt.y()) {
                if (b.y() > pt.y() && crossProduct(a, b, pt) > 0.0)
                    ++w;
            } else {
                if (b.y() <= pt.y() && crossProduct(a, b, pt) < 0.0)
                    --w;
            }
        }
        totalWinding += w;
        if ((w & 1) != 0)
            oddEvenParity ^= 1;
    }
    return rule == PolyFillRule::OddEven ? (oddEvenParity != 0) : (totalWinding != 0);
}

} // namespace

Polygons polySimplified(const Polygons &polys, PolyFillRule rule)
{
    const Clipper2Lib::Paths64 paths = toClosedPaths64(polys);
    if (paths.empty())
        return Polygons();
    return fromPaths64(simplifyPaths64(paths, rule));
}

Polygons polyUnited(const Polygons &a, PolyFillRule ruleA, const Polygons &b, PolyFillRule ruleB)
{
    return executeBooleanOp(Clipper2Lib::ClipType::Union, a, ruleA, b, ruleB);
}

Polygons polySubtracted(const Polygons &a, PolyFillRule ruleA, const Polygons &b, PolyFillRule ruleB)
{
    return executeBooleanOp(Clipper2Lib::ClipType::Difference, a, ruleA, b, ruleB);
}

Polygons polyIntersected(const Polygons &a, PolyFillRule ruleA, const Polygons &b, PolyFillRule ruleB)
{
    return executeBooleanOp(Clipper2Lib::ClipType::Intersection, a, ruleA, b, ruleB);
}

Polygons polyStrokeOutline(const Polygons &open, const Polygons &closed,
                           double width, CapStyle cap, JoinStyle join)
{
    if (width <= 0.0 || !std::isfinite(width))
        return Polygons();

    const double delta = 0.5 * width * kScale;
    if (delta < 0.5)
        return Polygons();

    const Clipper2Lib::Paths64 openPaths = toOpenPaths64(open);
    const Clipper2Lib::Paths64 closedPaths = toOpenPaths64(closed);
    if (openPaths.empty() && closedPaths.empty())
        return Polygons();

    const Clipper2Lib::JoinType jt = toClipperJoinType(join);
    const Clipper2Lib::EndType openEt = toClipperEndType(cap);
    const double arcTol = kArcTolerancePx * kScale;

    Clipper2Lib::Paths64 openStroke;
    if (!openPaths.empty()) {
        openStroke = Clipper2Lib::InflatePaths(openPaths, delta, jt, openEt, 4.0, arcTol);
    }

    Clipper2Lib::Paths64 closedStroke;
    if (!closedPaths.empty()) {
        closedStroke = Clipper2Lib::InflatePaths(
            closedPaths, delta, jt, Clipper2Lib::EndType::Joined, 4.0, arcTol);
    }

    if (openStroke.empty())
        return fromPaths64(closedStroke);
    if (closedStroke.empty())
        return fromPaths64(openStroke);

    Clipper2Lib::Paths64 combined;
    Clipper2Lib::Clipper64 clipper;
    clipper.PreserveCollinear(false);
    clipper.AddSubject(openStroke);
    clipper.AddClip(closedStroke);
    clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, combined);
    return fromPaths64(combined);
}

bool polyContains(const Polygons &outer, PolyFillRule outerRule,
                  const Polygons &inner, PolyFillRule innerRule)
{
    BBox outerBox, innerBox;
    const Polygons cleanOuter = cleanClosedPolygons(outer, &outerBox);
    const Polygons cleanInner = cleanClosedPolygons(inner, &innerBox);
    if (cleanOuter.isEmpty() || cleanInner.isEmpty())
        return false;

    // Touching outlines return false (adjacent faces vs holes).
    if (innerBox.minX <= outerBox.minX + kTouchEps
        || innerBox.minY <= outerBox.minY + kTouchEps
        || innerBox.maxX >= outerBox.maxX - kTouchEps
        || innerBox.maxY >= outerBox.maxY - kTouchEps) {
        return false;
    }

    if (anyEdgesTouchOrCross(cleanOuter, cleanInner))
        return false;

    for (const Polygon &poly : cleanInner) {
        if (!pointInPolygons(poly[0], cleanOuter, outerRule))
            return false;
    }

    for (const Polygon &poly : cleanOuter) {
        if (pointInPolygons(poly[0], cleanInner, innerRule))
            return false;
    }

    return true;
}

bool polyIntersects(const Polygons &a, PolyFillRule ruleA,
                    const Polygons &b, PolyFillRule ruleB)
{
    BBox boxA, boxB;
    const Polygons cleanA = cleanClosedPolygons(a, &boxA);
    const Polygons cleanB = cleanClosedPolygons(b, &boxB);
    if (cleanA.isEmpty() || cleanB.isEmpty())
        return false;

    if (boxA.maxX + kTouchEps < boxB.minX
        || boxB.maxX + kTouchEps < boxA.minX
        || boxA.maxY + kTouchEps < boxB.minY
        || boxB.maxY + kTouchEps < boxA.minY) {
        return false;
    }

    if (anyEdgesTouchOrCross(cleanA, cleanB))
        return true;

    for (const Polygon &poly : cleanB) {
        if (pointInPolygons(poly[0], cleanA, ruleA))
            return true;
    }

    for (const Polygon &poly : cleanA) {
        if (pointInPolygons(poly[0], cleanB, ruleB))
            return true;
    }

    return false;
}

} // namespace AnimeCore
