#include "algorithm/core/pathops.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace AnimeCore;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRasterScale = 8;
constexpr int kSamplesPerCategory = 500;

int g_failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

PolyFillRule toCoreFillRule(Qt::FillRule rule)
{
    return rule == Qt::OddEvenFill ? PolyFillRule::OddEven : PolyFillRule::Winding;
}

Polygons toCoreClosedPolygons(const QPainterPath &path)
{
    const QList<QPolygonF> subpaths = path.toSubpathPolygons();
    Polygons out;
    out.reserve(subpaths.size());
    for (const QPolygonF &sp : subpaths) {
        Polygon poly;
        poly.reserve(sp.size());
        for (const QPointF &pt : sp) {
            const PointF p(pt.x(), pt.y());
            if (poly.isEmpty() || poly.last() != p)
                poly.append(p);
        }
        if (poly.size() > 1 && poly.last() == poly.first())
            poly.removeLast();
        if (poly.size() >= 3)
            out.append(poly);
    }
    return out;
}

QPainterPath toQtClosedPath(const Polygons &polys, Qt::FillRule rule = Qt::OddEvenFill)
{
    QPainterPath path;
    path.setFillRule(rule);
    for (const Polygon &poly : polys) {
        if (poly.size() < 3)
            continue;
        path.moveTo(poly[0].x(), poly[0].y());
        for (std::ptrdiff_t i = 1; i < poly.size(); ++i)
            path.lineTo(poly[i].x(), poly[i].y());
        path.closeSubpath();
    }
    return path;
}

double polygonPerimeter(const Polygons &polys)
{
    double total = 0.0;
    for (const Polygon &poly : polys) {
        const std::ptrdiff_t n = poly.size();
        if (n < 2)
            continue;
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            const PointF &a = poly[i];
            const PointF &b = poly[(i + 1) % n];
            total += std::hypot(b.x() - a.x(), b.y() - a.y());
        }
    }
    return total;
}

struct AreaDiffStats
{
    double maxDiffArea = 0.0;
    double maxDiffPerimeter = 0.0;
    double maxRatioToTol = 0.0;
    int count = 0;
    int overTolCount = 0;

    void record(double diffArea, double perimeter, const char *categoryName, int caseIdx)
    {
        ++count;
        const double tol = 0.1 * perimeter;
        const double ratio = tol > 0.0 ? (diffArea / tol) : (diffArea > 0.0 ? 1e9 : 0.0);
        if (diffArea > maxDiffArea) {
            maxDiffArea = diffArea;
            maxDiffPerimeter = perimeter;
        }
        if (ratio > maxRatioToTol)
            maxRatioToTol = ratio;
        if (diffArea > tol) {
            ++overTolCount;
            std::fprintf(stderr,
                         "FAIL [%s #%d]: diffArea=%.4f px^2 > tol=%.4f px^2 (perim=%.2f)\n",
                         categoryName, caseIdx, diffArea, tol, perimeter);
            ++g_failures;
        }
    }
};

// Rasterizes both paths at 8x magnification with antialiasing off and returns
// the symmetric difference area in document square pixels. Also verifies that
// core output fills identically under OddEvenFill and WindingFill.
double compareFilledRegions(const QPainterPath &qtPath, const Polygons &corePolys,
                            double *outPerimeter = nullptr)
{
    const QPainterPath coreOddEven = toQtClosedPath(corePolys, Qt::OddEvenFill);
    const QPainterPath coreWinding = toQtClosedPath(corePolys, Qt::WindingFill);

    const double perim = std::max(polygonPerimeter(corePolys), qtPath.length());
    if (outPerimeter)
        *outPerimeter = perim;

    if (qtPath.isEmpty() && corePolys.isEmpty())
        return 0.0;

    QRectF bounds = qtPath.boundingRect().united(coreOddEven.boundingRect());
    if (bounds.isEmpty() && bounds.width() <= 0.0 && bounds.height() <= 0.0)
        return 0.0;
    bounds.adjust(-2.0, -2.0, 2.0, 2.0);

    const int w = std::max(4, static_cast<int>(std::ceil(bounds.width() * kRasterScale)) + 2);
    const int h = std::max(4, static_cast<int>(std::ceil(bounds.height() * kRasterScale)) + 2);

    auto renderMask = [&](const QPainterPath &p) {
        QImage img(w, h, QImage::Format_Grayscale8);
        img.fill(0);
        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.scale(kRasterScale, kRasterScale);
        painter.translate(-bounds.left(), -bounds.top());
        painter.fillPath(p, Qt::white);
        painter.end();
        return img;
    };

    const QImage imgQt = renderMask(qtPath);
    const QImage imgCoreOE = renderMask(coreOddEven);
    const QImage imgCoreW = renderMask(coreWinding);

    std::int64_t diffPixels = 0;
    std::int64_t ruleDiffPixels = 0;
    for (int y = 0; y < h; ++y) {
        const uchar *rowQt = imgQt.constScanLine(y);
        const uchar *rowOE = imgCoreOE.constScanLine(y);
        const uchar *rowW = imgCoreW.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if ((rowQt[x] != 0) != (rowOE[x] != 0))
                ++diffPixels;
            if ((rowOE[x] != 0) != (rowW[x] != 0))
                ++ruleDiffPixels;
        }
    }

    if (ruleDiffPixels > 0) {
        std::fprintf(stderr, "FAIL: core output differs between OddEven and Winding (%lld px)\n",
                     static_cast<long long>(ruleDiffPixels));
        ++g_failures;
    }

    return static_cast<double>(diffPixels) / static_cast<double>(kRasterScale * kRasterScale);
}

void compareAndRecord(AreaDiffStats &stats, const QPainterPath &qtPath,
                      const Polygons &corePolys, const char *name, int caseIdx)
{
    double perim = 0.0;
    const double diff = compareFilledRegions(qtPath, corePolys, &perim);
    stats.record(diff, perim, name, caseIdx);
}

// Generates a random star-shaped polygon (convex or concave) without self-intersections.
QPainterPath randomSimplePolygon(std::mt19937_64 &rng, double cx, double cy,
                                 double avgRadius, bool concave)
{
    std::uniform_int_distribution<int> vertDist(concave ? 6 : 4, concave ? 16 : 10);
    std::uniform_real_distribution<double> jitter(0.0, 1.0);
    const int n = vertDist(rng);

    std::vector<double> angles(n);
    for (int i = 0; i < n; ++i) {
        angles[i] = (2.0 * kPi * i / n) + (jitter(rng) - 0.5) * (kPi / n);
    }
    std::sort(angles.begin(), angles.end());

    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        double r = avgRadius;
        if (concave) {
            r *= (i % 2 == 0) ? (0.85 + 0.35 * jitter(rng)) : (0.35 + 0.30 * jitter(rng));
        } else {
            r *= 0.90 + 0.15 * jitter(rng);
        }
        const QPointF pt(cx + r * std::cos(angles[i]), cy + r * std::sin(angles[i]));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    if (!concave) {
        // Ensure strict convexity when requested by taking a regular-ish polygon.
        QPainterPath convexPath;
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * kPi * i / n;
            const QPointF pt(cx + avgRadius * std::cos(a), cy + avgRadius * std::sin(a));
            if (i == 0)
                convexPath.moveTo(pt);
            else
                convexPath.lineTo(pt);
        }
        convexPath.closeSubpath();
        return convexPath;
    }
    return path;
}

QPainterPath randomPolygonWithHole(std::mt19937_64 &rng, double cx, double cy, double outerRadius)
{
    QPainterPath outer = randomSimplePolygon(rng, cx, cy, outerRadius, true);
    QPainterPath inner = randomSimplePolygon(rng, cx, cy, outerRadius * 0.35, false);
    return outer.subtracted(inner);
}

QPainterPath randomBezierClosedPath(std::mt19937_64 &rng, double cx, double cy, double avgRadius)
{
    std::uniform_int_distribution<int> segDist(4, 9);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    const int n = segDist(rng);

    std::vector<QPointF> pts(n);
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * kPi * i / n;
        const double r = avgRadius * (0.7 + 0.4 * (0.5 * unit(rng) + 0.5));
        pts[i] = QPointF(cx + r * std::cos(a), cy + r * std::sin(a));
    }

    QPainterPath path;
    path.moveTo(pts[0]);
    for (int i = 0; i < n; ++i) {
        const QPointF p0 = pts[(i - 1 + n) % n];
        const QPointF p1 = pts[i];
        const QPointF p2 = pts[(i + 1) % n];
        const QPointF p3 = pts[(i + 2) % n];
        const QPointF c1 = p1 + (p2 - p0) * 0.2;
        const QPointF c2 = p2 - (p3 - p1) * 0.2;
        path.cubicTo(c1, c2, p2);
    }
    path.closeSubpath();
    return path;
}

} // namespace

int main()
{
    std::mt19937_64 rng(0xA11E40C0FFEEULL);
    std::uniform_real_distribution<double> posDist(20.0, 80.0);
    std::uniform_real_distribution<double> offsetDist(-25.0, 25.0);
    std::uniform_real_distribution<double> radiusDist(12.0, 28.0);

    // 0. Empty input checks
    {
        const Polygons empty;
        check(polySimplified(empty, PolyFillRule::OddEven).isEmpty(), "empty simplified");
        check(polyUnited(empty, PolyFillRule::OddEven, empty, PolyFillRule::OddEven).isEmpty(), "empty united");
        check(polySubtracted(empty, PolyFillRule::OddEven, empty, PolyFillRule::OddEven).isEmpty(), "empty subtracted");
        check(polyIntersected(empty, PolyFillRule::OddEven, empty, PolyFillRule::OddEven).isEmpty(), "empty intersected");
        check(polyStrokeOutline(empty, empty, 2.0, CapStyle::Round, JoinStyle::Round).isEmpty(), "empty stroke");
        check(!polyContains(empty, PolyFillRule::OddEven, empty, PolyFillRule::OddEven), "empty contains");
        check(!polyIntersects(empty, PolyFillRule::OddEven, empty, PolyFillRule::OddEven), "empty intersects");
    }

    // 1a. Random convex polygons - 500 groups
    AreaDiffStats convexPolyStats;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double cx = posDist(rng);
        const double cy = posDist(rng);
        const QPainterPath a = randomSimplePolygon(rng, cx, cy, radiusDist(rng), false);
        const QPainterPath b = randomSimplePolygon(rng, cx + offsetDist(rng), cy + offsetDist(rng),
                                                   radiusDist(rng), false);
        const Polygons pa = toCoreClosedPolygons(a);
        const Polygons pb = toCoreClosedPolygons(b);
        const PolyFillRule ra = toCoreFillRule(a.fillRule());
        const PolyFillRule rb = toCoreFillRule(b.fillRule());

        compareAndRecord(convexPolyStats, a.united(b), polyUnited(pa, ra, pb, rb), "convex_united", i);
        compareAndRecord(convexPolyStats, a.subtracted(b), polySubtracted(pa, ra, pb, rb), "convex_subtracted", i);
        compareAndRecord(convexPolyStats, a.intersected(b), polyIntersected(pa, ra, pb, rb), "convex_intersected", i);

        QPainterPath combined = a;
        combined.addPath(b);
        compareAndRecord(convexPolyStats, combined.simplified(),
                         polySimplified(toCoreClosedPolygons(combined), PolyFillRule::OddEven),
                         "convex_simplified", i);
    }

    // 1b. Random concave polygons - 500 groups
    AreaDiffStats concavePolyStats;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double cx = posDist(rng);
        const double cy = posDist(rng);
        const QPainterPath a = randomSimplePolygon(rng, cx, cy, radiusDist(rng), true);
        const QPainterPath b = randomSimplePolygon(rng, cx + offsetDist(rng), cy + offsetDist(rng),
                                                   radiusDist(rng), true);
        const Polygons pa = toCoreClosedPolygons(a);
        const Polygons pb = toCoreClosedPolygons(b);
        const PolyFillRule ra = toCoreFillRule(a.fillRule());
        const PolyFillRule rb = toCoreFillRule(b.fillRule());

        compareAndRecord(concavePolyStats, a.united(b), polyUnited(pa, ra, pb, rb), "concave_united", i);
        compareAndRecord(concavePolyStats, a.subtracted(b), polySubtracted(pa, ra, pb, rb), "concave_subtracted", i);
        compareAndRecord(concavePolyStats, a.intersected(b), polyIntersected(pa, ra, pb, rb), "concave_intersected", i);

        QPainterPath combined = a;
        combined.addPath(b);
        compareAndRecord(concavePolyStats, combined.simplified(),
                         polySimplified(toCoreClosedPolygons(combined), PolyFillRule::OddEven),
                         "concave_simplified", i);
    }

    // 2. Paths with holes - 500 groups
    AreaDiffStats holeStats;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double cx = posDist(rng);
        const double cy = posDist(rng);
        const QPainterPath a = randomPolygonWithHole(rng, cx, cy, radiusDist(rng) + 8.0);
        const QPainterPath b = randomPolygonWithHole(rng, cx + offsetDist(rng) * 0.6,
                                                     cy + offsetDist(rng) * 0.6, radiusDist(rng) + 6.0);
        const Polygons pa = toCoreClosedPolygons(a);
        const Polygons pb = toCoreClosedPolygons(b);
        const PolyFillRule ra = toCoreFillRule(a.fillRule());
        const PolyFillRule rb = toCoreFillRule(b.fillRule());

        compareAndRecord(holeStats, a.united(b), polyUnited(pa, ra, pb, rb), "hole_united", i);
        compareAndRecord(holeStats, a.subtracted(b), polySubtracted(pa, ra, pb, rb), "hole_subtracted", i);
        compareAndRecord(holeStats, a.intersected(b), polyIntersected(pa, ra, pb, rb), "hole_intersected", i);
    }

    // 3. Adjacent shared-edge faces, 4. Fully contained, 5. Fully separated, 6. Single-point touch - 500 groups each
    AreaDiffStats adjacentStats, containedStats, separatedStats, pointTouchStats;
    int containsIntersectsChecks = 0;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double x0 = 10.0 + (i % 20) * 1.5;
        const double y0 = 10.0 + ((i / 20) % 25) * 1.5;
        const double w1 = 12.0 + (i % 11) * 1.25;
        const double h1 = 10.0 + (i % 9) * 1.5;
        const double w2 = 8.0 + (i % 7) * 1.25;
        const double h2 = 9.0 + (i % 13) * 1.25;
        const double skew = (i % 5 - 2) * 1.5;

        // 3a. Adjacent sharing an edge (possibly sloped via skew)
        {
            const QPointF p0(x0, y0);
            const QPointF p1(x0 + w1, y0);
            const QPointF p2(x0 + w1 + skew, y0 + h1);
            const QPointF p3(x0, y0 + h1);
            const QPointF p4(x0 + w1 + w2, y0);
            const QPointF p5(x0 + w1 + w2 + skew, y0 + h1);

            QPainterPath a, b;
            a.moveTo(p0); a.lineTo(p1); a.lineTo(p2); a.lineTo(p3); a.closeSubpath();
            b.moveTo(p1); b.lineTo(p4); b.lineTo(p5); b.lineTo(p2); b.closeSubpath();

            const Polygons pa = toCoreClosedPolygons(a);
            const Polygons pb = toCoreClosedPolygons(b);

            compareAndRecord(adjacentStats, a.united(b), polyUnited(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "adj_united", i);
            compareAndRecord(adjacentStats, a.subtracted(b), polySubtracted(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "adj_sub", i);
            compareAndRecord(adjacentStats, a.intersected(b), polyIntersected(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "adj_int", i);

            check(polyContains(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.contains(b), "adjacent contains A->B");
            check(polyContains(pb, PolyFillRule::OddEven, pa, PolyFillRule::OddEven) == b.contains(a), "adjacent contains B->A");
            check(polyIntersects(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.intersects(b), "adjacent intersects");
            ++containsIntersectsChecks;
        }

        // 3b. Internally tangent (inner polygon inside outer polygon but sharing an edge or vertex)
        {
            QPainterPath outer, inner;
            outer.addRect(QRectF(x0, y0, w1 + 10.0, h1 + 10.0));
            if (i % 2 == 0) {
                // Shares top edge segment
                inner.addRect(QRectF(x0 + 2.0, y0, w1 * 0.5, h1 * 0.5));
            } else {
                // Diamond inscribed touching top edge at one point
                const double mx = x0 + (w1 + 10.0) * 0.5;
                const double my = y0 + (h1 + 10.0) * 0.5;
                inner.moveTo(mx, y0);
                inner.lineTo(mx + 4.0, my);
                inner.lineTo(mx, my + 4.0);
                inner.lineTo(mx - 4.0, my);
                inner.closeSubpath();
            }
            const Polygons pOuter = toCoreClosedPolygons(outer);
            const Polygons pInner = toCoreClosedPolygons(inner);
            check(polyContains(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven) == outer.contains(inner), "tangent contains outer->inner");
            check(!polyContains(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven), "tangent contains must be false");
            check(polyIntersects(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven) == outer.intersects(inner), "tangent intersects");
            ++containsIntersectsChecks;
        }

        // 4. Strictly contained
        {
            const QPainterPath outer = randomSimplePolygon(rng, 50.0, 50.0, 28.0, false);
            const QPainterPath inner = randomSimplePolygon(rng, 50.0, 50.0, 10.0, (i % 2 == 1));
            const Polygons pOuter = toCoreClosedPolygons(outer);
            const Polygons pInner = toCoreClosedPolygons(inner);

            compareAndRecord(containedStats, outer.united(inner), polyUnited(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven), "cont_united", i);
            compareAndRecord(containedStats, outer.subtracted(inner), polySubtracted(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven), "cont_sub", i);
            compareAndRecord(containedStats, outer.intersected(inner), polyIntersected(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven), "cont_int", i);

            check(polyContains(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven) == outer.contains(inner), "contained outer->inner");
            check(polyContains(pInner, PolyFillRule::OddEven, pOuter, PolyFillRule::OddEven) == inner.contains(outer), "contained inner->outer");
            check(polyIntersects(pOuter, PolyFillRule::OddEven, pInner, PolyFillRule::OddEven) == outer.intersects(inner), "contained intersects");
            ++containsIntersectsChecks;
        }

        // 5. Completely separated
        {
            const QPainterPath a = randomSimplePolygon(rng, 25.0, 25.0, 12.0, (i % 2 == 0));
            const QPainterPath b = randomSimplePolygon(rng, 75.0, 75.0, 12.0, (i % 3 == 0));
            const Polygons pa = toCoreClosedPolygons(a);
            const Polygons pb = toCoreClosedPolygons(b);

            compareAndRecord(separatedStats, a.united(b), polyUnited(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "sep_united", i);
            compareAndRecord(separatedStats, a.subtracted(b), polySubtracted(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "sep_sub", i);
            compareAndRecord(separatedStats, a.intersected(b), polyIntersected(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "sep_int", i);

            check(polyContains(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.contains(b), "separated contains");
            check(polyIntersects(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.intersects(b), "separated intersects");
            ++containsIntersectsChecks;
        }

        // 6. Single-point contact (external vertex-to-vertex or vertex-to-edge)
        {
            const QPointF touchPt(x0 + w1, y0 + h1);
            QPainterPath a, b;
            a.addRect(QRectF(x0, y0, w1, h1));
            if (i % 2 == 0) {
                b.addRect(QRectF(touchPt.x(), touchPt.y(), w2, h2));
            } else {
                b.moveTo(touchPt);
                b.lineTo(touchPt.x() + w2, touchPt.y() - 2.0);
                b.lineTo(touchPt.x() + w2, touchPt.y() + h2);
                b.closeSubpath();
            }
            const Polygons pa = toCoreClosedPolygons(a);
            const Polygons pb = toCoreClosedPolygons(b);

            compareAndRecord(pointTouchStats, a.united(b), polyUnited(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "pt_united", i);
            compareAndRecord(pointTouchStats, a.subtracted(b), polySubtracted(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "pt_sub", i);
            compareAndRecord(pointTouchStats, a.intersected(b), polyIntersected(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven), "pt_int", i);

            check(polyContains(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.contains(b), "point-touch contains");
            check(polyIntersects(pa, PolyFillRule::OddEven, pb, PolyFillRule::OddEven) == a.intersects(b), "point-touch intersects");
            ++containsIntersectsChecks;
        }
    }

    // 7. Flattened Bezier closed paths - 500 groups
    AreaDiffStats bezierStats;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double cx = posDist(rng);
        const double cy = posDist(rng);
        const QPainterPath rawA = randomBezierClosedPath(rng, cx, cy, radiusDist(rng));
        const QPainterPath rawB = randomBezierClosedPath(rng, cx + offsetDist(rng), cy + offsetDist(rng), radiusDist(rng));

        const Polygons pa = toCoreClosedPolygons(rawA);
        const Polygons pb = toCoreClosedPolygons(rawB);
        const QPainterPath flatA = toQtClosedPath(pa, rawA.fillRule());
        const QPainterPath flatB = toQtClosedPath(pb, rawB.fillRule());
        const PolyFillRule ra = toCoreFillRule(flatA.fillRule());
        const PolyFillRule rb = toCoreFillRule(flatB.fillRule());

        compareAndRecord(bezierStats, flatA.simplified(), polySimplified(pa, ra), "bezier_simp", i);
        compareAndRecord(bezierStats, flatA.united(flatB), polyUnited(pa, ra, pb, rb), "bezier_united", i);
        compareAndRecord(bezierStats, flatA.subtracted(flatB), polySubtracted(pa, ra, pb, rb), "bezier_sub", i);
        compareAndRecord(bezierStats, flatA.intersected(flatB), polyIntersected(pa, ra, pb, rb), "bezier_int", i);
    }

    // 8. Key pipeline from vectorlogic.cpp lines 2653-2679 + stroke tests - 500 groups
    AreaDiffStats vectorRegionStats, strokeOpenClosedStats;
    for (int i = 0; i < kSamplesPerCategory; ++i) {
        const double cx = 50.0 + (i % 11 - 5) * 2.0;
        const double cy = 50.0 + ((i / 11) % 11 - 5) * 2.0;
        const QPainterPath outer = randomSimplePolygon(rng, cx, cy, 26.0, true);
        const QPainterPath hole = randomSimplePolygon(rng, cx, cy, 8.5, false);

        QPainterPath canvas;
        canvas.addRect(QRectF(15.0, 15.0, 70.0, 70.0));

        // Qt version (vectorlogic.cpp 2653-2679)
        QPainterPath qtRegion = outer.subtracted(hole);
        QPainterPathStroker overpaintStroker;
        overpaintStroker.setWidth(2.0);
        overpaintStroker.setCapStyle(Qt::RoundCap);
        overpaintStroker.setJoinStyle(Qt::RoundJoin);
        const QPainterPath qtResult =
            qtRegion.united(overpaintStroker.createStroke(qtRegion)).intersected(canvas).simplified();

        // Core version of the exact same pipeline
        const Polygons coreOuter = toCoreClosedPolygons(outer);
        const Polygons coreHole = toCoreClosedPolygons(hole);
        const Polygons coreCanvas = toCoreClosedPolygons(canvas);
        const Polygons coreRegion = polySubtracted(coreOuter, PolyFillRule::OddEven, coreHole, PolyFillRule::OddEven);
        const Polygons coreStroke = polyStrokeOutline(Polygons(), coreRegion, 2.0, CapStyle::Round, JoinStyle::Round);
        const Polygons coreUnited = polyUnited(coreRegion, PolyFillRule::OddEven, coreStroke, PolyFillRule::Winding);
        const Polygons coreClipped = polyIntersected(coreUnited, PolyFillRule::OddEven, coreCanvas, PolyFillRule::OddEven);
        const Polygons coreResult = polySimplified(coreClipped, PolyFillRule::OddEven);

        compareAndRecord(vectorRegionStats, qtResult, coreResult, "vectorlogic_pipeline", i);

        // Also test standalone open and closed polyline strokes with Round, Flat, Square caps & Bevel, Round joins
        {
            QPainterPath openQt;
            Polygon openCore;
            const int pts = 4 + (i % 5);
            for (int k = 0; k < pts; ++k) {
                const QPointF p(20.0 + k * 12.0, 30.0 + ((k % 2 == 0) ? -10.0 : 10.0) + (i % 7));
                if (k == 0)
                    openQt.moveTo(p);
                else
                    openQt.lineTo(p);
                openCore.append(PointF(p.x(), p.y()));
            }
            const double w = 2.0 + (i % 4) * 1.5;
            QPainterPathStroker stroker;
            stroker.setWidth(w);
            const CapStyle cap = (i % 3 == 0) ? CapStyle::Round : ((i % 3 == 1) ? CapStyle::Flat : CapStyle::Square);
            const JoinStyle join = (i % 3 == 0) ? JoinStyle::Round : ((i % 3 == 1) ? JoinStyle::Bevel : JoinStyle::Miter);
            stroker.setCapStyle(cap == CapStyle::Round ? Qt::RoundCap : (cap == CapStyle::Flat ? Qt::FlatCap : Qt::SquareCap));
            stroker.setJoinStyle(join == JoinStyle::Round ? Qt::RoundJoin : (join == JoinStyle::Bevel ? Qt::BevelJoin : Qt::MiterJoin));

            Polygons openPolys;
            openPolys.append(openCore);
            const Polygons coreOpenStroke = polyStrokeOutline(openPolys, Polygons(), w, cap, join);
            compareAndRecord(strokeOpenClosedStats, stroker.createStroke(openQt), coreOpenStroke, "open_stroke", i);

            const QPainterPath &closedTargetQt = (join == JoinStyle::Miter) ? hole : qtRegion;
            const Polygons &closedTargetCore = (join == JoinStyle::Miter) ? coreHole : coreRegion;
            const Polygons coreClosedStroke = polyStrokeOutline(Polygons(), closedTargetCore, w, cap, join);
            compareAndRecord(strokeOpenClosedStats, stroker.createStroke(closedTargetQt), coreClosedStroke, "closed_stroke", i);
        }
    }

    // 9. fillPathFromMask benchmark & correctness: 1000x1000 region, tens of thousands of 1px-high rects
    double maskDiffArea = 0.0;
    double maskPerim = 0.0;
    qint64 qtMaskMs = 0;
    qint64 coreMaskMs = 0;
    int numMaskRects = 0;
    {
        QPainterPath qtRectsPath;
        Polygons coreRectsPolys;
        // Generate ~30,000 1-pixel-high horizontal run rectangles across a 1000x1000 region
        // (simulating a complex flood-fill mask with many internal holes/islands).
        for (int y = 0; y < 1000; ++y) {
            const int shift = (y * 7) % 13;
            for (int band = 0; band < 30; ++band) {
                const int xStart = band * 33 + (shift % 5);
                const int width = 20 + ((y + band) % 8);
                if (xStart + width >= 1000)
                    continue;
                qtRectsPath.addRect(QRectF(xStart, y, width, 1));
                Polygon rectPoly;
                rectPoly.reserve(4);
                rectPoly.append(PointF(xStart, y));
                rectPoly.append(PointF(xStart + width, y));
                rectPoly.append(PointF(xStart + width, y + 1));
                rectPoly.append(PointF(xStart, y + 1));
                coreRectsPolys.append(std::move(rectPoly));
                ++numMaskRects;
            }
        }

        QElapsedTimer timer;
        timer.start();
        const QPainterPath qtSimplified = qtRectsPath.simplified();
        qtMaskMs = timer.elapsed();

        timer.restart();
        const Polygons coreSimplified = polySimplified(coreRectsPolys, PolyFillRule::OddEven);
        coreMaskMs = timer.elapsed();

        // Compare on a sub-window of the 1000x1000 mask so 8x rasterization stays fast
        QPainterPath clipWindow;
        clipWindow.addRect(QRectF(0, 0, 200, 200));
        const Polygons coreWindow = toCoreClosedPolygons(clipWindow);
        maskDiffArea = compareFilledRegions(
            qtSimplified.intersected(clipWindow),
            polyIntersected(coreSimplified, PolyFillRule::OddEven, coreWindow, PolyFillRule::OddEven),
            &maskPerim);
        check(maskDiffArea <= 0.1 * maskPerim, "fillPathFromMask simplified area within tolerance");
        check(coreMaskMs <= qtMaskMs, "fillPathFromMask polySimplified must not be slower than Qt");
    }

    std::printf("convex_polygons: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                convexPolyStats.count, convexPolyStats.maxDiffArea,
                0.1 * convexPolyStats.maxDiffPerimeter, convexPolyStats.maxRatioToTol);
    std::printf("concave_polygons: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                concavePolyStats.count, concavePolyStats.maxDiffArea,
                0.1 * concavePolyStats.maxDiffPerimeter, concavePolyStats.maxRatioToTol);
    std::printf("holes: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                holeStats.count, holeStats.maxDiffArea,
                0.1 * holeStats.maxDiffPerimeter, holeStats.maxRatioToTol);
    std::printf("adjacent: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                adjacentStats.count, adjacentStats.maxDiffArea,
                0.1 * adjacentStats.maxDiffPerimeter, adjacentStats.maxRatioToTol);
    std::printf("contained: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                containedStats.count, containedStats.maxDiffArea,
                0.1 * containedStats.maxDiffPerimeter, containedStats.maxRatioToTol);
    std::printf("separated: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                separatedStats.count, separatedStats.maxDiffArea,
                0.1 * separatedStats.maxDiffPerimeter, separatedStats.maxRatioToTol);
    std::printf("point_touch: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                pointTouchStats.count, pointTouchStats.maxDiffArea,
                0.1 * pointTouchStats.maxDiffPerimeter, pointTouchStats.maxRatioToTol);
    std::printf("bezier_flattened: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                bezierStats.count, bezierStats.maxDiffArea,
                0.1 * bezierStats.maxDiffPerimeter, bezierStats.maxRatioToTol);
    std::printf("vectorlogic_pipeline: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                vectorRegionStats.count, vectorRegionStats.maxDiffArea,
                0.1 * vectorRegionStats.maxDiffPerimeter, vectorRegionStats.maxRatioToTol);
    std::printf("stroke_outline: cases=%d maxDiffArea=%.4f tol=%.4f (ratio=%.4f)\n",
                strokeOpenClosedStats.count, strokeOpenClosedStats.maxDiffArea,
                0.1 * strokeOpenClosedStats.maxDiffPerimeter, strokeOpenClosedStats.maxRatioToTol);
    std::printf("fillPathFromMask: rects=%d qt_ms=%lld core_ms=%lld diffArea=%.4f tol=%.4f\n",
                numMaskRects, static_cast<long long>(qtMaskMs), static_cast<long long>(coreMaskMs),
                maskDiffArea, 0.1 * maskPerim);
    std::printf("contains_intersects: pairs_checked=%d\n", containsIntersectsChecks);

    if (g_failures == 0) {
        std::printf("pathops tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d pathops check(s) failed\n", g_failures);
    return 1;
}
