#include "algorithm/core/path.h"
#include "algorithm/core/qtbridge_path.h"

#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

using namespace AnimeCore;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL [%s:%d]: ", __FILE__, __LINE__);        \
            std::fprintf(stderr, __VA_ARGS__);                                 \
            std::fprintf(stderr, "\n");                                        \
            ++g_failures;                                                      \
            if (g_failures >= 50) return;                                      \
        }                                                                      \
    } while (0)

std::uint64_t toBits(double d)
{
    if (d == 0.0) d = 0.0; // normalize -0.0 to +0.0
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof(d));
    return u;
}

bool sameBits(double a, double b)
{
    return toBits(a) == toBits(b);
}

bool closeRel(double a, double b, double relTol = 1e-9, double absTol = 1e-12)
{
    if (sameBits(a, b)) return true;
    const double diff = std::abs(a - b);
    if (diff <= absTol) return true;
    const double scale = std::max(std::abs(a), std::abs(b));
    return diff <= relTol * scale;
}

bool compareElementsBitwise(const Path &cp, const QPainterPath &qp, const char *ctx, int caseIdx = -1)
{
    if (cp.isEmpty() != qp.isEmpty()) {
        std::fprintf(stderr, "FAIL [%s case=%d]: isEmpty mismatch core=%d qt=%d (count core=%d qt=%d)\n",
                     ctx, caseIdx, int(cp.isEmpty()), int(qp.isEmpty()),
                     cp.elementCount(), qp.elementCount());
        ++g_failures;
        return false;
    }
    if (int(cp.fillRule()) != int(qp.fillRule())) {
        std::fprintf(stderr, "FAIL [%s case=%d]: fillRule mismatch core=%d qt=%d\n",
                     ctx, caseIdx, int(cp.fillRule()), int(qp.fillRule()));
        ++g_failures;
        return false;
    }
    if (cp.elementCount() != qp.elementCount()) {
        std::fprintf(stderr, "FAIL [%s case=%d]: elementCount mismatch core=%d qt=%d\n",
                     ctx, caseIdx, cp.elementCount(), qp.elementCount());
        for (int i = 0; i < std::max(cp.elementCount(), qp.elementCount()); ++i) {
            if (i < cp.elementCount()) {
                auto ce = cp.elementAt(i);
                std::fprintf(stderr, "  core[%d]: type=%d (%.17g, %.17g)\n", i, int(ce.type), ce.x, ce.y);
            }
            if (i < qp.elementCount()) {
                auto qe = qp.elementAt(i);
                std::fprintf(stderr, "  qt  [%d]: type=%d (%.17g, %.17g)\n", i, int(qe.type), qe.x, qe.y);
            }
        }
        ++g_failures;
        return false;
    }
    const PointF cCur = cp.currentPosition();
    const QPointF qCur = qp.currentPosition();
    if (!sameBits(cCur.x(), qCur.x()) || !sameBits(cCur.y(), qCur.y())) {
        std::fprintf(stderr, "FAIL [%s case=%d]: currentPosition mismatch core=(%.17g, %.17g) qt=(%.17g, %.17g)\n",
                     ctx, caseIdx, cCur.x(), cCur.y(), qCur.x(), qCur.y());
        ++g_failures;
        return false;
    }
    for (int i = 0; i < cp.elementCount(); ++i) {
        const Path::Element ce = cp.elementAt(i);
        const QPainterPath::Element qe = qp.elementAt(i);
        if (int(ce.type) != int(qe.type) || !sameBits(ce.x, qe.x) || !sameBits(ce.y, qe.y)) {
            std::fprintf(stderr,
                         "FAIL [%s case=%d elem=%d]: core=(type=%d, %.17g, %.17g) qt=(type=%d, %.17g, %.17g)\n",
                         ctx, caseIdx, i, int(ce.type), ce.x, ce.y, int(qe.type), qe.x, qe.y);
            ++g_failures;
            return false;
        }
        if (ce.isMoveTo() != qe.isMoveTo() || ce.isLineTo() != qe.isLineTo() || ce.isCurveTo() != qe.isCurveTo()) {
            std::fprintf(stderr, "FAIL [%s case=%d elem=%d]: element predicate mismatch\n", ctx, caseIdx, i);
            ++g_failures;
            return false;
        }
        const PointF pt = ce;
        const QPointF qpt = qe;
        if (!sameBits(pt.x(), qpt.x()) || !sameBits(pt.y(), qpt.y())) {
            std::fprintf(stderr, "FAIL [%s case=%d elem=%d]: PointF conversion mismatch\n", ctx, caseIdx, i);
            ++g_failures;
            return false;
        }
    }
    return true;
}

void testEdgeCases()
{
    // 1. Default empty path
    {
        Path cp;
        QPainterPath qp;
        compareElementsBitwise(cp, qp, "default_empty");
        CHECK(sameBits(cp.boundingRect().x(), qp.boundingRect().x()) &&
              sameBits(cp.boundingRect().y(), qp.boundingRect().y()) &&
              sameBits(cp.boundingRect().width(), qp.boundingRect().width()) &&
              sameBits(cp.boundingRect().height(), qp.boundingRect().height()),
              "empty boundingRect mismatch");
        CHECK(sameBits(cp.controlPointRect().x(), qp.controlPointRect().x()) &&
              sameBits(cp.controlPointRect().width(), qp.controlPointRect().width()),
              "empty controlPointRect mismatch");
        CHECK(sameBits(cp.length(), qp.length()), "empty length mismatch");
        CHECK(sameBits(cp.percentAtLength(0.0), qp.percentAtLength(0.0)), "empty percentAtLength(0)");
        CHECK(sameBits(cp.percentAtLength(5.0), qp.percentAtLength(5.0)), "empty percentAtLength(5)");
        CHECK(sameBits(cp.pointAtPercent(0.5).x(), qp.pointAtPercent(0.5).x()) &&
              sameBits(cp.pointAtPercent(0.5).y(), qp.pointAtPercent(0.5).y()),
              "empty pointAtPercent");
        CHECK(cp.contains(PointF(0, 0)) == qp.contains(QPointF(0, 0)), "empty contains");
    }

    // 2. Single MoveTo path (isEmpty() == true)
    {
        Path cp(PointF(12.5, -7.25));
        QPainterPath qp(QPointF(12.5, -7.25));
        compareElementsBitwise(cp, qp, "single_moveto");
        RectF cb = cp.boundingRect();
        QRectF qb = qp.boundingRect();
        CHECK(sameBits(cb.x(), qb.x()) && sameBits(cb.y(), qb.y()) &&
              sameBits(cb.width(), qb.width()) && sameBits(cb.height(), qb.height()),
              "single_moveto boundingRect core=(%g,%g,%g,%g) qt=(%g,%g,%g,%g)",
              cb.x(), cb.y(), cb.width(), cb.height(), qb.x(), qb.y(), qb.width(), qb.height());
        RectF cc = cp.controlPointRect();
        QRectF qc = qp.controlPointRect();
        CHECK(sameBits(cc.x(), qc.x()) && sameBits(cc.y(), qc.y()) &&
              sameBits(cc.width(), qc.width()) && sameBits(cc.height(), qc.height()),
              "single_moveto controlPointRect core=(%g,%g,%g,%g) qt=(%g,%g,%g,%g)",
              cc.x(), cc.y(), cc.width(), cc.height(), qc.x(), qc.y(), qc.width(), qc.height());
        PointF cpp = cp.pointAtPercent(0.5);
        QPointF qpp = qp.pointAtPercent(0.5);
        CHECK(sameBits(cpp.x(), qpp.x()) && sameBits(cpp.y(), qpp.y()),
              "single_moveto pointAtPercent core=(%g,%g) qt=(%g,%g)",
              cpp.x(), cpp.y(), qpp.x(), qpp.y());
    }

    // 3. Consecutive moveTo replaces previous moveTo
    {
        Path cp;
        QPainterPath qp;
        cp.moveTo(1, 2);
        qp.moveTo(1, 2);
        cp.moveTo(3, 4);
        qp.moveTo(3, 4);
        compareElementsBitwise(cp, qp, "consecutive_moveto_initial");
        cp.lineTo(10, 20);
        qp.lineTo(10, 20);
        cp.moveTo(5, 6);
        qp.moveTo(5, 6);
        cp.moveTo(7, 8);
        qp.moveTo(7, 8);
        compareElementsBitwise(cp, qp, "consecutive_moveto_after_line");
    }

    // 4. lineTo / cubicTo / quadTo on empty path (implicit moveTo(0, 0))
    {
        Path cp1, cp2, cp3, cp4;
        QPainterPath qp1, qp2, qp3, qp4;
        cp1.lineTo(5, 6);
        qp1.lineTo(5, 6);
        compareElementsBitwise(cp1, qp1, "lineTo_on_empty");

        cp2.lineTo(0, 0);
        qp2.lineTo(0, 0);
        compareElementsBitwise(cp2, qp2, "lineTo_zero_on_empty");

        cp3.cubicTo(1, 2, 3, 4, 5, 6);
        qp3.cubicTo(1, 2, 3, 4, 5, 6);
        compareElementsBitwise(cp3, qp3, "cubicTo_on_empty");

        cp4.quadTo(2, 4, 6, 8);
        qp4.quadTo(2, 4, 6, 8);
        compareElementsBitwise(cp4, qp4, "quadTo_on_empty");
    }

    // 5. closeSubpath followed by lineTo / cubicTo / moveTo
    {
        Path cp;
        QPainterPath qp;
        cp.moveTo(10, 10);
        qp.moveTo(10, 10);
        cp.lineTo(20, 10);
        qp.lineTo(20, 10);
        cp.lineTo(20, 20);
        qp.lineTo(20, 20);
        cp.closeSubpath();
        qp.closeSubpath();
        compareElementsBitwise(cp, qp, "closeSubpath_basic");

        cp.lineTo(30, 30);
        qp.lineTo(30, 30);
        compareElementsBitwise(cp, qp, "lineTo_after_closeSubpath");
    }

    // 6. addRect, addPath, connectPath
    {
        Path cp;
        QPainterPath qp;
        cp.addRect(10, 20, 30, 40);
        qp.addRect(10, 20, 30, 40);
        compareElementsBitwise(cp, qp, "addRect_basic");
        cp.lineTo(100, 200);
        qp.lineTo(100, 200);
        compareElementsBitwise(cp, qp, "lineTo_after_addRect");

        Path cpSub;
        QPainterPath qpSub;
        cpSub.moveTo(50, 50);
        qpSub.moveTo(50, 50);
        cpSub.cubicTo(60, 40, 70, 80, 90, 50);
        qpSub.cubicTo(60, 40, 70, 80, 90, 50);

        cp.addPath(cpSub);
        qp.addPath(qpSub);
        compareElementsBitwise(cp, qp, "addPath");

        cp.connectPath(cpSub);
        qp.connectPath(qpSub);
        compareElementsBitwise(cp, qp, "connectPath");
    }
}

void testImplicitSharing()
{
    Path p1;
    p1.moveTo(0, 0);
    p1.lineTo(10, 20);
    p1.cubicTo(15, 25, 30, 35, 40, 50);

    Path p2 = p1;
    CHECK(p1.isSharedWith(p2), "p1 and p2 should share data after copy");
    CHECK(!p1.isDetached(), "p1 should not be detached when shared");

    p2.lineTo(60, 70);
    CHECK(!p1.isSharedWith(p2), "p1 and p2 should detach on write");
    CHECK(p1.elementCount() == 5, "p1 elementCount unchanged (got %d)", p1.elementCount());
    CHECK(p2.elementCount() == 6, "p2 elementCount incremented (got %d)", p2.elementCount());

    Path p3 = p1;
    p3.setElementPositionAt(1, 99.0, 88.0);
    CHECK(p1.elementAt(1).x == 10.0 && p1.elementAt(1).y == 20.0, "p1 untouched by setElementPositionAt");
    CHECK(p3.elementAt(1).x == 99.0 && p3.elementAt(1).y == 88.0, "p3 updated by setElementPositionAt");

    Path p4 = p1;
    p4.translate(5.0, -5.0);
    CHECK(p1.elementAt(0).x == 0.0, "p1 untouched by translate");
    CHECK(p4.elementAt(0).x == 5.0, "p4 updated by translate");

    Path p5 = p1;
    p5.setFillRule(WindingFill);
    CHECK(p1.fillRule() == OddEvenFill, "p1 fillRule untouched");
    CHECK(p5.fillRule() == WindingFill, "p5 fillRule updated");
}

void testRandomDifferential()
{
    std::mt19937_64 rng(0xA11E4FC05EULL);
    std::uniform_real_distribution<double> coordDist(-500.0, 500.0);
    std::uniform_real_distribution<double> smallDist(-1e-6, 1e-6);
    std::uniform_real_distribution<double> unitDist(0.0, 1.0);
    std::uniform_int_distribution<int> opCountDist(1, 25);
    std::uniform_int_distribution<int> opDist(0, 11);

    constexpr int kNumPaths = 5200;
    int lengthExactCount = 0;
    int pctExactCount = 0;
    int ptExactCount = 0;
    int lengthTotalCount = 0;

    for (int idx = 0; idx < kNumPaths; ++idx) {
        if (g_failures >= 50) return;

        Path cp;
        QPainterPath qp;

        if (idx % 3 == 1) {
            const double sx = coordDist(rng);
            const double sy = coordDist(rng);
            cp = Path(PointF(sx, sy));
            qp = QPainterPath(QPointF(sx, sy));
        }

        if (idx % 2 == 1) {
            cp.setFillRule(WindingFill);
            qp.setFillRule(Qt::WindingFill);
        }

        const int numOps = opCountDist(rng);
        for (int op = 0; op < numOps; ++op) {
            const int kind = opDist(rng);
            switch (kind) {
            case 0: { // moveTo
                const double x = coordDist(rng);
                const double y = coordDist(rng);
                cp.moveTo(x, y);
                qp.moveTo(x, y);
                break;
            }
            case 1: { // lineTo
                const double x = coordDist(rng);
                const double y = coordDist(rng);
                cp.lineTo(x, y);
                qp.lineTo(x, y);
                break;
            }
            case 2: { // degenerate lineTo (currentPosition or near-currentPosition)
                const PointF cur = cp.currentPosition();
                const double dx = (op % 2 == 0) ? 0.0 : smallDist(rng);
                const double dy = (op % 2 == 0) ? 0.0 : smallDist(rng);
                cp.lineTo(cur.x() + dx, cur.y() + dy);
                qp.lineTo(cur.x() + dx, cur.y() + dy);
                break;
            }
            case 3: { // cubicTo
                const double x1 = coordDist(rng), y1 = coordDist(rng);
                const double x2 = coordDist(rng), y2 = coordDist(rng);
                const double x3 = coordDist(rng), y3 = coordDist(rng);
                cp.cubicTo(x1, y1, x2, y2, x3, y3);
                qp.cubicTo(x1, y1, x2, y2, x3, y3);
                break;
            }
            case 4: { // cubicTo with coincident control points
                const PointF cur = cp.currentPosition();
                const double x3 = coordDist(rng), y3 = coordDist(rng);
                if (op % 3 == 0) {
                    // all points equal to cur
                    cp.cubicTo(cur, cur, cur);
                    qp.cubicTo(QPointF(cur.x(), cur.y()), QPointF(cur.x(), cur.y()), QPointF(cur.x(), cur.y()));
                } else if (op % 3 == 1) {
                    // c1 == cur
                    cp.cubicTo(cur.x(), cur.y(), x3, y3, x3, y3);
                    qp.cubicTo(cur.x(), cur.y(), x3, y3, x3, y3);
                } else {
                    // c1 == c2
                    cp.cubicTo(x3, y3, x3, y3, cur.x(), cur.y());
                    qp.cubicTo(x3, y3, x3, y3, cur.x(), cur.y());
                }
                break;
            }
            case 5: { // quadTo
                const double cx = coordDist(rng), cy = coordDist(rng);
                const double ex = coordDist(rng), ey = coordDist(rng);
                cp.quadTo(cx, cy, ex, ey);
                qp.quadTo(cx, cy, ex, ey);
                break;
            }
            case 6: { // closeSubpath
                cp.closeSubpath();
                qp.closeSubpath();
                break;
            }
            case 7: { // addRect
                const double x = coordDist(rng), y = coordDist(rng);
                const double w = (op % 4 == 0) ? 0.0 : std::abs(coordDist(rng));
                const double h = (op % 4 == 0) ? 0.0 : std::abs(coordDist(rng));
                cp.addRect(x, y, w, h);
                qp.addRect(x, y, w, h);
                break;
            }
            case 8: { // translate
                const double dx = coordDist(rng) * 0.1;
                const double dy = coordDist(rng) * 0.1;
                cp.translate(dx, dy);
                qp.translate(dx, dy);
                break;
            }
            case 9: { // setElementPositionAt
                if (cp.elementCount() > 0) {
                    std::uniform_int_distribution<int> elemDist(0, cp.elementCount() - 1);
                    const int ei = elemDist(rng);
                    const double nx = coordDist(rng);
                    const double ny = coordDist(rng);
                    cp.setElementPositionAt(ei, nx, ny);
                    qp.setElementPositionAt(ei, nx, ny);
                }
                break;
            }
            case 10: { // addPath
                Path sub;
                QPainterPath qsub;
                const double sx = coordDist(rng), sy = coordDist(rng);
                sub.moveTo(sx, sy);
                qsub.moveTo(sx, sy);
                const double lx = coordDist(rng), ly = coordDist(rng);
                sub.lineTo(lx, ly);
                qsub.lineTo(lx, ly);
                if (op % 2 == 0) {
                    sub.closeSubpath();
                    qsub.closeSubpath();
                }
                cp.addPath(sub);
                qp.addPath(qsub);
                break;
            }
            case 11: { // connectPath
                Path sub;
                QPainterPath qsub;
                const double sx = coordDist(rng), sy = coordDist(rng);
                sub.moveTo(sx, sy);
                qsub.moveTo(sx, sy);
                const double cx = coordDist(rng), cy = coordDist(rng);
                const double ex = coordDist(rng), ey = coordDist(rng);
                sub.quadTo(cx, cy, ex, ey);
                qsub.quadTo(cx, cy, ex, ey);
                cp.connectPath(sub);
                qp.connectPath(qsub);
                break;
            }
            }
        }

        if (!compareElementsBitwise(cp, qp, "random_ops", idx)) {
            return;
        }

        // Round-trip Bridge conversion test
        {
            const QPainterPath toQt = cp;
            const Path backToCore = toQt;
            if (!compareElementsBitwise(backToCore, qp, "bridge_roundtrip", idx)) {
                return;
            }
        }

        // boundingRect & controlPointRect
        {
            const RectF cb = cp.boundingRect();
            const QRectF qb = qp.boundingRect();
            CHECK(sameBits(cb.x(), qb.x()) && sameBits(cb.y(), qb.y()) &&
                  sameBits(cb.width(), qb.width()) && sameBits(cb.height(), qb.height()),
                  "case %d boundingRect mismatch core=(%.17g, %.17g, %.17g, %.17g) qt=(%.17g, %.17g, %.17g, %.17g)",
                  idx, cb.x(), cb.y(), cb.width(), cb.height(), qb.x(), qb.y(), qb.width(), qb.height());

            const RectF cc = cp.controlPointRect();
            const QRectF qc = qp.controlPointRect();
            CHECK(sameBits(cc.x(), qc.x()) && sameBits(cc.y(), qc.y()) &&
                  sameBits(cc.width(), qc.width()) && sameBits(cc.height(), qc.height()),
                  "case %d controlPointRect mismatch core=(%.17g, %.17g, %.17g, %.17g) qt=(%.17g, %.17g, %.17g, %.17g)",
                  idx, cc.x(), cc.y(), cc.width(), cc.height(), qc.x(), qc.y(), qc.width(), qc.height());
        }

        // length, percentAtLength, pointAtPercent, angleAtPercent
        {
            const double clen = cp.length();
            const double qlen = qp.length();
            ++lengthTotalCount;
            if (sameBits(clen, qlen)) {
                ++lengthExactCount;
            }
            CHECK(closeRel(clen, qlen, 1e-9, 1e-9),
                  "case %d length mismatch core=%.17g qt=%.17g", idx, clen, qlen);

            const double tSample = unitDist(rng);
            const double targetLen = clen * tSample;
            const double cPct = cp.percentAtLength(targetLen);
            const double qPct = qp.percentAtLength(targetLen);
            if (sameBits(cPct, qPct)) {
                ++pctExactCount;
            }
            CHECK(closeRel(cPct, qPct, 1e-9, 1e-9),
                  "case %d percentAtLength(%g) mismatch core=%.17g qt=%.17g", idx, targetLen, cPct, qPct);

            const PointF cPt = cp.pointAtPercent(tSample);
            const QPointF qPt = qp.pointAtPercent(tSample);
            if (sameBits(cPt.x(), qPt.x()) && sameBits(cPt.y(), qPt.y())) {
                ++ptExactCount;
            }
            CHECK(closeRel(cPt.x(), qPt.x(), 1e-9, 1e-9) && closeRel(cPt.y(), qPt.y(), 1e-9, 1e-9),
                  "case %d pointAtPercent(%g) mismatch core=(%.17g, %.17g) qt=(%.17g, %.17g)",
                  idx, tSample, cPt.x(), cPt.y(), qPt.x(), qPt.y());

            for (double endT : {0.0, 1.0}) {
                const PointF cePt = cp.pointAtPercent(endT);
                const QPointF qePt = qp.pointAtPercent(endT);
                CHECK(closeRel(cePt.x(), qePt.x(), 1e-9, 1e-9) && closeRel(cePt.y(), qePt.y(), 1e-9, 1e-9),
                      "case %d pointAtPercent(%g) mismatch core=(%.17g, %.17g) qt=(%.17g, %.17g)",
                      idx, endT, cePt.x(), cePt.y(), qePt.x(), qePt.y());
            }

            if (clen > 1e-6) {
                const double cAng = cp.angleAtPercent(tSample);
                const double qAng = qp.angleAtPercent(tSample);
                CHECK(closeRel(cAng, qAng, 1e-7, 1e-7),
                      "case %d angleAtPercent(%g) mismatch core=%.17g qt=%.17g", idx, tSample, cAng, qAng);
            }
        }

        // contains(PointF)
        {
            const RectF br = cp.boundingRect();
            const PointF probe(br.x() + unitDist(rng) * (br.width() + 10.0) - 5.0,
                               br.y() + unitDist(rng) * (br.height() + 10.0) - 5.0);
            const bool cIn = cp.contains(probe);
            const bool qIn = qp.contains(QPointF(probe.x(), probe.y()));
            CHECK(cIn == qIn,
                  "case %d contains(%.17g, %.17g) mismatch core=%d qt=%d",
                  idx, probe.x(), probe.y(), int(cIn), int(qIn));
        }

        // translated & transformed
        {
            const Transform tr(1.25, -0.5, 0.0, 0.75, 1.5, 0.0, 10.0, -20.0, 1.0);
            const QTransform qtr = Bridge<QTransform>::fromCore(tr);
            const Path cMapped = cp.transformed(tr);
            const QPainterPath qMapped = qtr.map(qp);
            if (!compareElementsBitwise(cMapped, qMapped, "transformed", idx)) {
                return;
            }
        }

        // toSubpathPolygons
        {
            const Vector<Vector<PointF>> cPolys = cp.toSubpathPolygons();
            const QList<QPolygonF> qPolys = qp.toSubpathPolygons();
            CHECK(int(cPolys.size()) == int(qPolys.size()),
                  "case %d toSubpathPolygons count core=%d qt=%d",
                  idx, int(cPolys.size()), int(qPolys.size()));
            if (int(cPolys.size()) == int(qPolys.size())) {
                for (int pi = 0; pi < int(qPolys.size()); ++pi) {
                    CHECK(int(cPolys[pi].size()) == int(qPolys[pi].size()),
                          "case %d toSubpathPolygons[%d] size core=%d qt=%d",
                          idx, pi, int(cPolys[pi].size()), int(qPolys[pi].size()));
                    const int n = std::min(int(cPolys[pi].size()), int(qPolys[pi].size()));
                    for (int vi = 0; vi < n; ++vi) {
                        CHECK(closeRel(cPolys[pi][vi].x(), qPolys[pi][vi].x(), 1e-12, 1e-12) &&
                              closeRel(cPolys[pi][vi].y(), qPolys[pi][vi].y(), 1e-12, 1e-12),
                              "case %d toSubpathPolygons[%d][%d] mismatch", idx, pi, vi);
                    }
                }
            }
        }
    }

    std::printf("Random differential: %d paths tested, bitwise identical: length %d/%d, percentAtLength %d/%d, pointAtPercent %d/%d\n",
                kNumPaths, lengthExactCount, lengthTotalCount, pctExactCount, lengthTotalCount, ptExactCount, lengthTotalCount);
}

void testPerformance()
{
    std::mt19937_64 rng(987654321ULL);
    std::uniform_real_distribution<double> dist(-200.0, 200.0);

    Path cp;
    QPainterPath qp;
    cp.moveTo(0, 0);
    qp.moveTo(0, 0);
    while (cp.elementCount() < 2000) {
        const double x1 = dist(rng), y1 = dist(rng);
        const double x2 = dist(rng), y2 = dist(rng);
        const double x3 = dist(rng), y3 = dist(rng);
        cp.cubicTo(x1, y1, x2, y2, x3, y3);
        qp.cubicTo(x1, y1, x2, y2, x3, y3);
    }

    constexpr int kIters = 100;
    double cSum = 0.0;
    const auto cStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        cSum += cp.length();
    }
    const auto cEnd = std::chrono::steady_clock::now();

    double qSum = 0.0;
    const auto qStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        qSum += qp.length();
    }
    const auto qEnd = std::chrono::steady_clock::now();

    const double cMs = std::chrono::duration<double, std::milli>(cEnd - cStart).count();
    const double qMs = std::chrono::duration<double, std::milli>(qEnd - qStart).count();
    const double ratio = (qMs > 0.0) ? (cMs / qMs) : 1.0;

    std::printf("Performance (2000+ elements x %d): Core=%.2f ms, Qt=%.2f ms, ratio=%.2fx (sum diff=%g)\n",
                kIters, cMs, qMs, ratio, std::abs(cSum - qSum));
    CHECK(ratio < 3.0, "Path::length() is too slow compared to QPainterPath::length(): %.2fx", ratio);
}

void testBridgedCopy()
{
    Path path;
    path.moveTo(1.0, 2.0);
    path.cubicTo(3.0, 4.0, 5.0, 6.0, 7.0, 8.0);

    const QPainterPath first = path;
    CHECK(path.bridgedCopy() != nullptr, "conversion should leave a bridged copy");
    const std::shared_ptr<const void> kept = path.bridgedCopy();
    const QPainterPath second = path;
    CHECK(path.bridgedCopy() == kept, "an unchanged path should not be converted again");
    CHECK(first == second, "the bridged copy should equal the first conversion");

    Path shared = path;
    CHECK(shared.bridgedCopy() == kept, "copies should share the bridged copy");
    shared.lineTo(9.0, 10.0);
    CHECK(shared.bridgedCopy() == nullptr, "a write should drop the bridged copy");
    CHECK(path.bridgedCopy() == kept, "writing to a copy should not touch the original");
    CHECK(QPainterPath(shared).elementCount() == first.elementCount() + 1,
          "conversion after a write should see the write");

    path.setElementPositionAt(0, 11.0, 12.0);
    CHECK(QPainterPath(path).elementAt(0).x == 11.0, "moving an element should invalidate the copy");
    path.setFillRule(WindingFill);
    CHECK(QPainterPath(path).fillRule() == Qt::WindingFill, "fill rule should invalidate the copy");
    path.translate(1.0, 0.0);
    CHECK(QPainterPath(path).elementAt(0).x == 12.0, "translate should invalidate the copy");
    path.clear();
    CHECK(QPainterPath(path).isEmpty(), "clear should invalidate the copy");
void testAddSelf()
{
    Path core;
    QPainterPath qt;
    core.moveTo(1.0, 2.0);
    core.lineTo(3.0, 4.0);
    qt.moveTo(1.0, 2.0);
    qt.lineTo(3.0, 4.0);
    core.reserve(2);
    core.addPath(core);
    qt.addPath(qt);
    CHECK(core.elementCount() == qt.elementCount(), "adding a path to itself should double it");
    for (int i = 0; i < qt.elementCount(); ++i) {
        CHECK(core.elementAt(i).x == qt.elementAt(i).x && core.elementAt(i).y == qt.elementAt(i).y,
              "adding a path to itself should keep its elements");
    }
}

} // namespace

int main()
{
    testEdgeCases();
    testBridgedCopy();
    testAddSelf();
    testImplicitSharing();
    testRandomDifferential();
    testPerformance();

    if (g_failures != 0) {
        std::fprintf(stderr, "FAILED with %d failures.\n", g_failures);
        return 1;
    }
    std::printf("ALL PATH TESTS PASSED.\n");
    return 0;
}
