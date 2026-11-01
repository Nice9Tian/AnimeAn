#include "algorithm/core/geometry.h"
#include "algorithm/core/color.h"
#include "algorithm/core/qtbridge_geometry.h"

#include <QColor>
#include <QLineF>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QtGlobal>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

using namespace AnimeCore;

static int g_failed = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL [line %d]: ", __LINE__); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n"); \
        g_failed = 1; \
        return; \
    } \
} while (0)

static bool sameBits(double a, double b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    std::uint64_t ua = 0, ub = 0;
    std::memcpy(&ua, &a, sizeof(double));
    std::memcpy(&ub, &b, sizeof(double));
    return ua == ub;
}

static bool sameBitsF(float a, float b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    std::uint32_t ua = 0, ub = 0;
    std::memcpy(&ua, &a, sizeof(float));
    std::memcpy(&ub, &b, sizeof(float));
    return ua == ub;
}

static bool samePointF(const PointF &a, const QPointF &b)
{
    return sameBits(a.x(), b.x()) && sameBits(a.y(), b.y());
}

static bool samePoint(const Point &a, const QPoint &b)
{
    return a.x() == b.x() && a.y() == b.y();
}

static bool sameSize(const Size &a, const QSize &b)
{
    return a.width() == b.width() && a.height() == b.height();
}

static bool sameLineF(const LineF &a, const QLineF &b)
{
    return sameBits(a.x1(), b.x1()) && sameBits(a.y1(), b.y1())
        && sameBits(a.x2(), b.x2()) && sameBits(a.y2(), b.y2());
}

static bool sameRect(const Rect &a, const QRect &b)
{
    return a.left() == b.left() && a.top() == b.top()
        && a.right() == b.right() && a.bottom() == b.bottom();
}

static bool sameRectF(const RectF &a, const QRectF &b)
{
    return sameBits(a.x(), b.x()) && sameBits(a.y(), b.y())
        && sameBits(a.width(), b.width()) && sameBits(a.height(), b.height());
}

static bool sameTransform(const Transform &a, const QTransform &b)
{
    return sameBits(a.m11(), b.m11()) && sameBits(a.m12(), b.m12()) && sameBits(a.m13(), b.m13())
        && sameBits(a.m21(), b.m21()) && sameBits(a.m22(), b.m22()) && sameBits(a.m23(), b.m23())
        && sameBits(a.m31(), b.m31()) && sameBits(a.m32(), b.m32()) && sameBits(a.m33(), b.m33());
}

static constexpr int kNumSamples = 25000;

static double randomDouble(std::mt19937_64 &rng)
{
    static const double kSpecials[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5,
        1e-13, -1e-13, 1e-12, -1e-12, 1e-9, -1e-9, 1e-6, -1e-6,
        1e-5, -1e-5, 1e-3, -1e-3, 10.0, -10.0, 45.0, -45.0,
        90.0, -90.0, 180.0, -180.0, 270.0, -270.0, 360.0, -360.0,
        1000.0, -1000.0, 1e6, -1e6, 1e8, -1e8
    };
    std::uniform_int_distribution<int> modeDist(0, 9);
    const int mode = modeDist(rng);
    if (mode < 2) {
        std::uniform_int_distribution<std::size_t> idxDist(
            0, sizeof(kSpecials) / sizeof(kSpecials[0]) - 1);
        return kSpecials[idxDist(rng)];
    }
    if (mode < 6) {
        std::uniform_real_distribution<double> dist(-500.0, 500.0);
        return dist(rng);
    }
    if (mode < 8) {
        std::uniform_int_distribution<int> halfDist(-2000, 2000);
        return halfDist(rng) * 0.5;
    }
    std::uniform_real_distribution<double> wideDist(-1e6, 1e6);
    return wideDist(rng);
}

static int randomInt(std::mt19937_64 &rng)
{
    static const int kSpecials[] = {
        0, 1, -1, 2, -2, 3, -3, 10, -10, 100, -100, 10000, -10000, 1000000, -1000000
    };
    std::uniform_int_distribution<int> modeDist(0, 9);
    const int mode = modeDist(rng);
    if (mode < 3) {
        std::uniform_int_distribution<std::size_t> idxDist(
            0, sizeof(kSpecials) / sizeof(kSpecials[0]) - 1);
        return kSpecials[idxDist(rng)];
    }
    if (mode < 8) {
        std::uniform_int_distribution<int> dist(-500, 500);
        return dist(rng);
    }
    std::uniform_int_distribution<int> wideDist(-1000000, 1000000);
    return wideDist(rng);
}

static void testMathFunctions()
{
    std::mt19937_64 rng(0x1001ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        const double a = randomDouble(rng);
        double b = randomDouble(rng);
        if (i % 5 == 0) {
            std::uniform_int_distribution<int> epsSteps(-20, 20);
            b = a + epsSteps(rng) * 1e-13 * (1.0 + std::abs(a));
        }

        CHECK(fuzzyCompare(a, b) == qFuzzyCompare(a, b),
              "fuzzyCompare(double) mismatch at i=%d: a=%.17g b=%.17g", i, a, b);
        CHECK(fuzzyIsNull(a) == qFuzzyIsNull(a),
              "fuzzyIsNull(double) mismatch at i=%d: a=%.17g", i, a);

        const float fa = static_cast<float>(a);
        const float fb = (i % 5 == 0)
            ? static_cast<float>(fa + (i % 7 - 3) * 1e-6f * (1.0f + std::abs(fa)))
            : static_cast<float>(b);
        CHECK(fuzzyCompare(fa, fb) == qFuzzyCompare(fa, fb),
              "fuzzyCompare(float) mismatch at i=%d: fa=%.9g fb=%.9g", i, fa, fb);
        CHECK(fuzzyIsNull(fa) == qFuzzyIsNull(fa),
              "fuzzyIsNull(float) mismatch at i=%d: fa=%.9g", i, fa);

        const double rInput = std::clamp(a, -1e8, 1e8);
        CHECK(AnimeCore::round(rInput) == qRound(rInput),
              "round(double) mismatch at i=%d: v=%.17g got=%d expected=%d",
              i, rInput, AnimeCore::round(rInput), qRound(rInput));
        CHECK(AnimeCore::round64(rInput) == qRound64(rInput),
              "round64(double) mismatch at i=%d: v=%.17g", i, rInput);

        const float rfInput = static_cast<float>(std::clamp(a, -1e6, 1e6));
        CHECK(AnimeCore::round(rfInput) == qRound(rfInput),
              "round(float) mismatch at i=%d: v=%.9g got=%d expected=%d",
              i, rfInput, AnimeCore::round(rfInput), qRound(rfInput));
        CHECK(AnimeCore::round64(rfInput) == qRound64(rfInput),
              "round64(float) mismatch at i=%d: v=%.9g", i, rfInput);

        const double lo = std::min(a, b);
        const double hi = std::max(a, b);
        const double val = randomDouble(rng);
        CHECK(sameBits(bound(lo, val, hi), qBound(lo, val, hi)),
              "bound(double) mismatch at i=%d", i);

        const int ilo = std::min(randomInt(rng), randomInt(rng));
        const int ihi = ilo + std::abs(randomInt(rng) % 10000);
        const int ival = randomInt(rng);
        CHECK(bound(ilo, ival, ihi) == qBound(ilo, ival, ihi),
              "bound(int) mismatch at i=%d", i);
    }
}

static void testPointAndSize()
{
    std::mt19937_64 rng(0x2002ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        const int x1 = randomInt(rng);
        const int y1 = randomInt(rng);
        const int x2 = randomInt(rng);
        const int y2 = randomInt(rng);

        Point cp1(x1, y1);
        QPoint qp1(x1, y1);
        Point cp2(x2, y2);
        QPoint qp2(x2, y2);

        CHECK(samePoint(cp1, qp1), "Point ctor mismatch at i=%d", i);
        CHECK(cp1.isNull() == qp1.isNull(), "Point::isNull mismatch at i=%d", i);
        CHECK(cp1.manhattanLength() == qp1.manhattanLength(), "Point::manhattanLength mismatch at i=%d", i);
        CHECK(samePoint(cp1.transposed(), qp1.transposed()), "Point::transposed mismatch at i=%d", i);
        CHECK(Point::dotProduct(cp1, cp2) == QPoint::dotProduct(qp1, qp2), "Point::dotProduct mismatch at i=%d", i);
        CHECK(samePoint(cp1 + cp2, qp1 + qp2), "Point + mismatch at i=%d", i);
        CHECK(samePoint(cp1 - cp2, qp1 - qp2), "Point - mismatch at i=%d", i);
        CHECK(samePoint(-cp1, -qp1), "Point unary - mismatch at i=%d", i);
        CHECK(samePoint(+cp1, +qp1), "Point unary + mismatch at i=%d", i);
        CHECK((cp1 == cp2) == (qp1 == qp2), "Point == mismatch at i=%d", i);
        CHECK((cp1 != cp2) == (qp1 != qp2), "Point != mismatch at i=%d", i);

        const int factorI = (x2 % 31);
        CHECK(samePoint(cp1 * factorI, qp1 * factorI), "Point * int mismatch at i=%d", i);
        CHECK(samePoint(factorI * cp1, factorI * qp1), "int * Point mismatch at i=%d", i);

        const double factorD = std::clamp(randomDouble(rng), -100.0, 100.0);
        CHECK(samePoint(cp1 * factorD, qp1 * factorD), "Point * double mismatch at i=%d", i);
        CHECK(samePoint(factorD * cp1, factorD * qp1), "double * Point mismatch at i=%d", i);

        if ( std::abs(factorD) > 0.01) {
            Point cpDiv = cp1;
            QPoint qpDiv = qp1;
            cpDiv /= factorD;
            qpDiv /= factorD;
            CHECK(samePoint(cpDiv, qpDiv), "Point /= mismatch at i=%d", i);
            CHECK(samePoint(cp1 / factorD, qp1 / factorD), "Point / mismatch at i=%d", i);
        }

        Size cs1(x1, y1);
        QSize qs1(x1, y1);
        Size cs2(x2, y2);
        QSize qs2(x2, y2);
        CHECK(sameSize(cs1, qs1), "Size ctor mismatch at i=%d", i);
        CHECK(cs1.isNull() == qs1.isNull(), "Size::isNull mismatch at i=%d", i);
        CHECK(cs1.isEmpty() == qs1.isEmpty(), "Size::isEmpty mismatch at i=%d", i);
        CHECK(cs1.isValid() == qs1.isValid(), "Size::isValid mismatch at i=%d", i);
        CHECK(sameSize(cs1.transposed(), qs1.transposed()), "Size::transposed mismatch at i=%d", i);
        CHECK(sameSize(cs1.expandedTo(cs2), qs1.expandedTo(qs2)), "Size::expandedTo mismatch at i=%d", i);
        CHECK(sameSize(cs1.boundedTo(cs2), qs1.boundedTo(qs2)), "Size::boundedTo mismatch at i=%d", i);
        CHECK(sameSize(cs1 + cs2, qs1 + qs2), "Size + mismatch at i=%d", i);
        CHECK(sameSize(cs1 - cs2, qs1 - qs2), "Size - mismatch at i=%d", i);
        CHECK((cs1 == cs2) == (qs1 == qs2), "Size == mismatch at i=%d", i);
    }

    CHECK(Size().isNull() == QSize().isNull(), "Default Size::isNull mismatch");
    CHECK(Size().isEmpty() == QSize().isEmpty(), "Default Size::isEmpty mismatch");
    CHECK(Size().isValid() == QSize().isValid(), "Default Size::isValid mismatch");
    CHECK(sameSize(Size(), QSize()), "Default Size mismatch");
}

static void testPointF()
{
    std::mt19937_64 rng(0x3003ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        const double x1 = randomDouble(rng);
        const double y1 = randomDouble(rng);
        double x2 = randomDouble(rng);
        double y2 = randomDouble(rng);
        if (i % 5 == 0) {
            std::uniform_int_distribution<int> step(-15, 15);
            x2 = x1 + step(rng) * 1e-13 * (1.0 + std::abs(x1));
            y2 = y1 + step(rng) * 1e-13 * (1.0 + std::abs(y1));
        }

        PointF cp1(x1, y1);
        QPointF qp1(x1, y1);
        PointF cp2(x2, y2);
        QPointF qp2(x2, y2);

        CHECK(samePointF(cp1, qp1), "PointF ctor mismatch at i=%d", i);
        CHECK(cp1.isNull() == qp1.isNull(), "PointF::isNull mismatch at i=%d", i);
        CHECK(sameBits(cp1.manhattanLength(), qp1.manhattanLength()),
              "PointF::manhattanLength mismatch at i=%d", i);
        CHECK(samePointF(cp1.transposed(), qp1.transposed()),
              "PointF::transposed mismatch at i=%d", i);
        CHECK(sameBits(PointF::dotProduct(cp1, cp2), QPointF::dotProduct(qp1, qp2)),
              "PointF::dotProduct mismatch at i=%d", i);
        CHECK(samePointF(cp1 + cp2, qp1 + qp2), "PointF + mismatch at i=%d", i);
        CHECK(samePointF(cp1 - cp2, qp1 - qp2), "PointF - mismatch at i=%d", i);
        CHECK(samePointF(-cp1, -qp1), "PointF unary - mismatch at i=%d", i);
        CHECK(samePointF(+cp1, +qp1), "PointF unary + mismatch at i=%d", i);
        CHECK((cp1 == cp2) == (qp1 == qp2),
              "PointF == mismatch at i=%d: (%.17g, %.17g) vs (%.17g, %.17g)",
              i, x1, y1, x2, y2);
        CHECK((cp1 != cp2) == (qp1 != qp2), "PointF != mismatch at i=%d", i);

        const double s = randomDouble(rng);
        CHECK(samePointF(cp1 * s, qp1 * s), "PointF * s mismatch at i=%d", i);
        CHECK(samePointF(s * cp1, s * qp1), "s * PointF mismatch at i=%d", i);
        if (std::abs(s) > 1e-6) {
            CHECK(samePointF(cp1 / s, qp1 / s), "PointF / s mismatch at i=%d", i);
        }

        const PointF clampedP(std::clamp(x1, -1e8, 1e8), std::clamp(y1, -1e8, 1e8));
        const QPointF clampedQP(clampedP.x(), clampedP.y());
        CHECK(samePoint(clampedP.toPoint(), clampedQP.toPoint()),
              "PointF::toPoint mismatch at i=%d", i);
    }
}

static void testLineF()
{
    std::mt19937_64 rng(0x4004ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        const double x1 = std::clamp(randomDouble(rng), -1e5, 1e5);
        const double y1 = std::clamp(randomDouble(rng), -1e5, 1e5);
        double x2 = std::clamp(randomDouble(rng), -1e5, 1e5);
        double y2 = std::clamp(randomDouble(rng), -1e5, 1e5);
        if (i % 15 == 0) {
            x2 = x1;
            y2 = y1;
        }

        LineF cl1(x1, y1, x2, y2);
        QLineF ql1(x1, y1, x2, y2);

        CHECK(sameLineF(cl1, ql1), "LineF ctor mismatch at i=%d", i);
        CHECK(cl1.isNull() == ql1.isNull(), "LineF::isNull mismatch at i=%d", i);
        CHECK(sameBits(cl1.dx(), ql1.dx()), "LineF::dx mismatch at i=%d", i);
        CHECK(sameBits(cl1.dy(), ql1.dy()), "LineF::dy mismatch at i=%d", i);
        CHECK(sameBits(cl1.length(), ql1.length()),
              "LineF::length mismatch at i=%d: got=%.17g expected=%.17g",
              i, cl1.length(), ql1.length());
        CHECK(sameBits(cl1.angle(), ql1.angle()),
              "LineF::angle mismatch at i=%d: got=%.17g expected=%.17g",
              i, cl1.angle(), ql1.angle());
        CHECK(sameLineF(cl1.unitVector(), ql1.unitVector()),
              "LineF::unitVector mismatch at i=%d", i);
        CHECK(sameLineF(cl1.normalVector(), ql1.normalVector()),
              "LineF::normalVector mismatch at i=%d", i);
        CHECK(samePointF(cl1.center(), ql1.center()),
              "LineF::center mismatch at i=%d", i);

        const double t = (i % 11 - 5) * 0.25;
        CHECK(samePointF(cl1.pointAt(t), ql1.pointAt(t)),
              "LineF::pointAt mismatch at i=%d", i);

        const double polarLen = std::abs(std::clamp(randomDouble(rng), -1e4, 1e4));
        const double polarAng = std::clamp(randomDouble(rng), -720.0, 720.0);
        CHECK(sameLineF(LineF::fromPolar(polarLen, polarAng), QLineF::fromPolar(polarLen, polarAng)),
              "LineF::fromPolar mismatch at i=%d (len=%.17g ang=%.17g)", i, polarLen, polarAng);

        double x3 = std::clamp(randomDouble(rng), -1e5, 1e5);
        double y3 = std::clamp(randomDouble(rng), -1e5, 1e5);
        double x4 = std::clamp(randomDouble(rng), -1e5, 1e5);
        double y4 = std::clamp(randomDouble(rng), -1e5, 1e5);
        if (i % 20 == 0) {
            x4 = x3 + (x2 - x1) * 2.0;
            y4 = y3 + (y2 - y1) * 2.0;
        }
        LineF cl2(x3, y3, x4, y4);
        QLineF ql2(x3, y3, x4, y4);

        CHECK(sameBits(cl1.angleTo(cl2), ql1.angleTo(ql2)),
              "LineF::angleTo mismatch at i=%d: got=%.17g expected=%.17g",
              i, cl1.angleTo(cl2), ql1.angleTo(ql2));

        PointF cHit(-999.0, -888.0);
        QPointF qHit(-999.0, -888.0);
        const auto cType = cl1.intersects(cl2, &cHit);
        const auto qType = ql1.intersects(ql2, &qHit);
        CHECK(static_cast<int>(cType) == static_cast<int>(qType),
              "LineF::intersects type mismatch at i=%d: got=%d expected=%d",
              i, static_cast<int>(cType), static_cast<int>(qType));
        if (cType != LineF::NoIntersection) {
            CHECK(samePointF(cHit, qHit),
                  "LineF::intersects point mismatch at i=%d: got=(%.17g, %.17g) expected=(%.17g, %.17g)",
                  i, cHit.x(), cHit.y(), qHit.x(), qHit.y());
        }
    }
}

static void testRect()
{
    std::mt19937_64 rng(0x5005ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        int x1 = std::clamp(randomInt(rng), -50000, 50000);
        int y1 = std::clamp(randomInt(rng), -50000, 50000);
        int w1 = std::clamp(randomInt(rng), -50000, 50000);
        int h1 = std::clamp(randomInt(rng), -50000, 50000);
        if (i % 10 == 0) { w1 = 0; h1 = 0; }
        else if (i % 10 == 1) { w1 = 0; }
        else if (i % 10 == 2) { h1 = 0; }
        else if (i % 10 == 3) { w1 = -1; h1 = -1; }

        int x2 = std::clamp(randomInt(rng), -50000, 50000);
        int y2 = std::clamp(randomInt(rng), -50000, 50000);
        int w2 = std::clamp(randomInt(rng), -50000, 50000);
        int h2 = std::clamp(randomInt(rng), -50000, 50000);
        if (i % 13 == 0) { w2 = 0; h2 = 0; }

        Rect cr1 = (i % 17 == 0) ? Rect() : Rect(x1, y1, w1, h1);
        QRect qr1 = (i % 17 == 0) ? QRect() : QRect(x1, y1, w1, h1);
        Rect cr2 = (i % 19 == 0) ? Rect() : Rect(x2, y2, w2, h2);
        QRect qr2 = (i % 19 == 0) ? QRect() : QRect(x2, y2, w2, h2);

        CHECK(sameRect(cr1, qr1), "Rect ctor mismatch at i=%d", i);
        CHECK(cr1.isNull() == qr1.isNull(), "Rect::isNull mismatch at i=%d", i);
        CHECK(cr1.isEmpty() == qr1.isEmpty(), "Rect::isEmpty mismatch at i=%d", i);
        CHECK(cr1.isValid() == qr1.isValid(), "Rect::isValid mismatch at i=%d", i);
        CHECK(cr1.width() == qr1.width(), "Rect::width mismatch at i=%d", i);
        CHECK(cr1.height() == qr1.height(), "Rect::height mismatch at i=%d", i);
        CHECK(cr1.right() == qr1.right(), "Rect::right mismatch at i=%d", i);
        CHECK(cr1.bottom() == qr1.bottom(), "Rect::bottom mismatch at i=%d", i);
        CHECK(samePoint(cr1.topLeft(), qr1.topLeft()), "Rect::topLeft mismatch at i=%d", i);
        CHECK(samePoint(cr1.bottomRight(), qr1.bottomRight()), "Rect::bottomRight mismatch at i=%d", i);
        CHECK(samePoint(cr1.topRight(), qr1.topRight()), "Rect::topRight mismatch at i=%d", i);
        CHECK(samePoint(cr1.bottomLeft(), qr1.bottomLeft()), "Rect::bottomLeft mismatch at i=%d", i);
        CHECK(samePoint(cr1.center(), qr1.center()), "Rect::center mismatch at i=%d", i);
        CHECK(sameRect(cr1.normalized(), qr1.normalized()), "Rect::normalized mismatch at i=%d", i);
        CHECK(sameRect(cr1.transposed(), qr1.transposed()), "Rect::transposed mismatch at i=%d", i);
        CHECK(sameRect(cr1.adjusted(1, -2, 3, -4), qr1.adjusted(1, -2, 3, -4)), "Rect::adjusted mismatch at i=%d", i);
        CHECK(sameRect(cr1.united(cr2), qr1.united(qr2)), "Rect::united mismatch at i=%d", i);
        CHECK(sameRect(cr1.intersected(cr2), qr1.intersected(qr2)), "Rect::intersected mismatch at i=%d", i);
        CHECK(cr1.intersects(cr2) == qr1.intersects(qr2), "Rect::intersects mismatch at i=%d", i);

        Point pt(x2, y2);
        QPoint qpt(x2, y2);
        CHECK(cr1.contains(pt, false) == qr1.contains(qpt, false), "Rect::contains(Point, false) mismatch at i=%d", i);
        CHECK(cr1.contains(pt, true) == qr1.contains(qpt, true), "Rect::contains(Point, true) mismatch at i=%d", i);
        CHECK(cr1.contains(cr2, false) == qr1.contains(qr2, false), "Rect::contains(Rect, false) mismatch at i=%d", i);
        CHECK(cr1.contains(cr2, true) == qr1.contains(qr2, true), "Rect::contains(Rect, true) mismatch at i=%d", i);

        Rect crMove = cr1;
        QRect qrMove = qr1;
        crMove.moveCenter(pt);
        qrMove.moveCenter(qpt);
        CHECK(sameRect(crMove, qrMove), "Rect::moveCenter mismatch at i=%d", i);
    }
}

static void testRectF()
{
    std::mt19937_64 rng(0x6006ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        double x1 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double y1 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double w1 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double h1 = std::clamp(randomDouble(rng), -1e6, 1e6);
        if (i % 10 == 0) { w1 = 0.0; h1 = 0.0; }
        else if (i % 10 == 1) { w1 = 0.0; }
        else if (i % 10 == 2) { h1 = 0.0; }
        else if (i % 10 == 3) { w1 = -std::abs(w1) - 1.0; h1 = -std::abs(h1) - 1.0; }

        double x2 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double y2 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double w2 = std::clamp(randomDouble(rng), -1e6, 1e6);
        double h2 = std::clamp(randomDouble(rng), -1e6, 1e6);
        if (i % 13 == 0) { w2 = 0.0; h2 = 0.0; }
        else if (i % 13 == 1) { w2 = 0.0; }

        RectF cr1(x1, y1, w1, h1);
        QRectF qr1(x1, y1, w1, h1);
        RectF cr2(x2, y2, w2, h2);
        QRectF qr2(x2, y2, w2, h2);

        CHECK(sameRectF(cr1, qr1), "RectF ctor mismatch at i=%d", i);
        CHECK(cr1.isNull() == qr1.isNull(), "RectF::isNull mismatch at i=%d", i);
        CHECK(cr1.isEmpty() == qr1.isEmpty(), "RectF::isEmpty mismatch at i=%d", i);
        CHECK(cr1.isValid() == qr1.isValid(), "RectF::isValid mismatch at i=%d", i);
        CHECK(sameBits(cr1.left(), qr1.left()), "RectF::left mismatch at i=%d", i);
        CHECK(sameBits(cr1.top(), qr1.top()), "RectF::top mismatch at i=%d", i);
        CHECK(sameBits(cr1.right(), qr1.right()), "RectF::right mismatch at i=%d", i);
        CHECK(sameBits(cr1.bottom(), qr1.bottom()), "RectF::bottom mismatch at i=%d", i);
        CHECK(samePointF(cr1.topLeft(), qr1.topLeft()), "RectF::topLeft mismatch at i=%d", i);
        CHECK(samePointF(cr1.bottomRight(), qr1.bottomRight()), "RectF::bottomRight mismatch at i=%d", i);
        CHECK(samePointF(cr1.topRight(), qr1.topRight()), "RectF::topRight mismatch at i=%d", i);
        CHECK(samePointF(cr1.bottomLeft(), qr1.bottomLeft()), "RectF::bottomLeft mismatch at i=%d", i);
        CHECK(samePointF(cr1.center(), qr1.center()), "RectF::center mismatch at i=%d", i);
        CHECK(sameRectF(cr1.normalized(), qr1.normalized()), "RectF::normalized mismatch at i=%d", i);
        CHECK(sameRectF(cr1.transposed(), qr1.transposed()), "RectF::transposed mismatch at i=%d", i);
        CHECK(sameRectF(cr1.adjusted(1.5, -2.5, 3.25, -4.75), qr1.adjusted(1.5, -2.5, 3.25, -4.75)),
              "RectF::adjusted mismatch at i=%d", i);
        CHECK(sameRectF(cr1.united(cr2), qr1.united(qr2)), "RectF::united mismatch at i=%d", i);
        CHECK(sameRectF(cr1.intersected(cr2), qr1.intersected(qr2)), "RectF::intersected mismatch at i=%d", i);
        CHECK(cr1.intersects(cr2) == qr1.intersects(qr2), "RectF::intersects mismatch at i=%d", i);

        PointF pt(x2, y2);
        QPointF qpt(x2, y2);
        CHECK(cr1.contains(pt) == qr1.contains(qpt), "RectF::contains(PointF) mismatch at i=%d", i);
        CHECK(cr1.contains(cr1.topLeft()) == qr1.contains(qr1.topLeft()),
              "RectF::contains(topLeft) mismatch at i=%d", i);
        CHECK(cr1.contains(cr1.bottomRight()) == qr1.contains(qr1.bottomRight()),
              "RectF::contains(bottomRight) mismatch at i=%d", i);
        CHECK(cr1.contains(cr2) == qr1.contains(qr2), "RectF::contains(RectF) mismatch at i=%d", i);

        if (!sameRect(cr1.toRect(), qr1.toRect())) {
            std::fprintf(stderr,
                "RectF::toRect mismatch at i=%d: in=(%.17g, %.17g, %.17g, %.17g) got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                i, x1, y1, w1, h1,
                cr1.toRect().x(), cr1.toRect().y(), cr1.toRect().width(), cr1.toRect().height(),
                qr1.toRect().x(), qr1.toRect().y(), qr1.toRect().width(), qr1.toRect().height());
            g_failed = 1;
            if (i > 30)
                return;
        }
        CHECK(sameRect(cr1.toAlignedRect(), qr1.toAlignedRect()),
              "RectF::toAlignedRect mismatch at i=%d: rect=(%.17g, %.17g, %.17g, %.17g)",
              i, x1, y1, w1, h1);
        CHECK((cr1 == cr2) == (qr1 == qr2), "RectF == mismatch at i=%d", i);
    }
}

static void buildRandomTransformPair(std::mt19937_64 &rng, int i, Transform &ct, QTransform &qt)
{
    ct.reset();
    qt.reset();
    const int kind = i % 8;
    if (kind == 0) {
        return;
    }
    if (kind == 1) {
        const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
        ct.translate(dx, dy);
        qt.translate(dx, dy);
        return;
    }
    if (kind == 2) {
        const double sx = std::clamp(randomDouble(rng), -100.0, 100.0);
        const double sy = std::clamp(randomDouble(rng), -100.0, 100.0);
        const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
        ct.translate(dx, dy);
        qt.translate(dx, dy);
        ct.scale(sx, sy);
        qt.scale(sx, sy);
        return;
    }
    if (kind == 3) {
        const double ang = std::clamp(randomDouble(rng), -720.0, 720.0);
        const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
        ct.translate(dx, dy);
        qt.translate(dx, dy);
        ct.rotate(ang);
        qt.rotate(ang);
        return;
    }
    if (kind == 4) {
        const double sx = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double sy = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double sh = std::clamp(randomDouble(rng), -10.0, 10.0);
        const double sv = std::clamp(randomDouble(rng), -10.0, 10.0);
        const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
        ct.scale(sx, sy);
        qt.scale(sx, sy);
        ct.shear(sh, sv);
        qt.shear(sh, sv);
        ct.translate(dx, dy);
        qt.translate(dx, dy);
        return;
    }
    if (kind == 5) {
        const double m11 = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double m12 = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double m21 = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double m22 = std::clamp(randomDouble(rng), -50.0, 50.0);
        const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
        ct = Transform(m11, m12, m21, m22, dx, dy);
        qt = QTransform(m11, m12, m21, m22, dx, dy);
        return;
    }
    if (kind == 6) {
        const double m11 = std::clamp(randomDouble(rng), -20.0, 20.0);
        const double m12 = std::clamp(randomDouble(rng), -20.0, 20.0);
        const double m13 = std::clamp(randomDouble(rng), -0.05, 0.05);
        const double m21 = std::clamp(randomDouble(rng), -20.0, 20.0);
        const double m22 = std::clamp(randomDouble(rng), -20.0, 20.0);
        const double m23 = std::clamp(randomDouble(rng), -0.05, 0.05);
        const double m31 = std::clamp(randomDouble(rng), -1000.0, 1000.0);
        const double m32 = std::clamp(randomDouble(rng), -1000.0, 1000.0);
        double m33 = std::clamp(randomDouble(rng), 0.5, 10.0);
        ct = Transform(m11, m12, m13, m21, m22, m23, m31, m32, m33);
        qt = QTransform(m11, m12, m13, m21, m22, m23, m31, m32, m33);
        return;
    }
    const double dx = std::clamp(randomDouble(rng), -1e4, 1e4);
    const double dy = std::clamp(randomDouble(rng), -1e4, 1e4);
    const double sx = std::clamp(randomDouble(rng), -20.0, 20.0);
    const double sy = std::clamp(randomDouble(rng), -20.0, 20.0);
    ct = Transform::fromTranslate(dx, dy) * Transform::fromScale(sx, sy);
    qt = QTransform::fromTranslate(dx, dy) * QTransform::fromScale(sx, sy);
}

static void testTransform()
{
    std::mt19937_64 rng(0x7007ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        Transform ct1, ct2;
        QTransform qt1, qt2;
        buildRandomTransformPair(rng, i, ct1, qt1);
        buildRandomTransformPair(rng, i + 3, ct2, qt2);

        CHECK(sameTransform(ct1, qt1), "Transform build mismatch at i=%d", i);
        CHECK(static_cast<int>(ct1.type()) == static_cast<int>(qt1.type()),
              "Transform::type mismatch at i=%d: got=%d expected=%d",
              i, static_cast<int>(ct1.type()), static_cast<int>(qt1.type()));
        CHECK(ct1.isIdentity() == qt1.isIdentity(), "Transform::isIdentity mismatch at i=%d", i);
        CHECK(ct1.isAffine() == qt1.isAffine(), "Transform::isAffine mismatch at i=%d", i);
        CHECK(ct1.isInvertible() == qt1.isInvertible(), "Transform::isInvertible mismatch at i=%d", i);
        CHECK(ct1.isScaling() == qt1.isScaling(), "Transform::isScaling mismatch at i=%d", i);
        CHECK(ct1.isRotating() == qt1.isRotating(), "Transform::isRotating mismatch at i=%d", i);
        CHECK(ct1.isTranslating() == qt1.isTranslating(), "Transform::isTranslating mismatch at i=%d", i);
        CHECK(sameBits(ct1.determinant(), qt1.determinant()), "Transform::determinant mismatch at i=%d", i);
        CHECK(sameTransform(ct1.adjoint(), qt1.adjoint()), "Transform::adjoint mismatch at i=%d", i);
        CHECK(sameTransform(ct1.transposed(), qt1.transposed()), "Transform::transposed mismatch at i=%d", i);

        bool cInvOk = false;
        bool qInvOk = false;
        Transform cInv = ct1.inverted(&cInvOk);
        QTransform qInv = qt1.inverted(&qInvOk);
        CHECK(cInvOk == qInvOk, "Transform::inverted ok flag mismatch at i=%d", i);
        CHECK(sameTransform(cInv, qInv), "Transform::inverted matrix mismatch at i=%d", i);
        CHECK(static_cast<int>(cInv.type()) == static_cast<int>(qInv.type()),
              "Transform::inverted type mismatch at i=%d", i);

        Transform cMul = ct1 * ct2;
        QTransform qMul = qt1 * qt2;
        CHECK(sameTransform(cMul, qMul), "Transform::operator* mismatch at i=%d", i);
        CHECK(static_cast<int>(cMul.type()) == static_cast<int>(qMul.type()),
              "Transform::operator* type mismatch at i=%d", i);

        const double px = std::clamp(randomDouble(rng), -1e4, 1e4);
        const double py = std::clamp(randomDouble(rng), -1e4, 1e4);
        PointF cMapP = ct1.map(PointF(px, py));
        QPointF qMapP = qt1.map(QPointF(px, py));
        CHECK(samePointF(cMapP, qMapP),
              "Transform::map(PointF) mismatch at i=%d (type=%d, px=%.17g, py=%.17g, m13=%.17g, m23=%.17g, m33=%.17g): got=(%.17g, %.17g) expected=(%.17g, %.17g)",
              i, static_cast<int>(ct1.type()), px, py, ct1.m13(), ct1.m23(), ct1.m33(),
              cMapP.x(), cMapP.y(), qMapP.x(), qMapP.y());

        const int ipx = std::clamp(randomInt(rng), -10000, 10000);
        const int ipy = std::clamp(randomInt(rng), -10000, 10000);
        CHECK(samePoint(ct1.map(Point(ipx, ipy)), qt1.map(QPoint(ipx, ipy))),
              "Transform::map(Point) mismatch at i=%d", i);

        const double rw = std::clamp(randomDouble(rng), -5000.0, 5000.0);
        const double rh = std::clamp(randomDouble(rng), -5000.0, 5000.0);
        CHECK(sameRectF(ct1.mapRect(RectF(px, py, rw, rh)), qt1.mapRect(QRectF(px, py, rw, rh))),
              "Transform::mapRect(RectF) mismatch at i=%d", i);

        const int irw = std::clamp(randomInt(rng), -5000, 5000);
        const int irh = std::clamp(randomInt(rng), -5000, 5000);
        Rect cMappedR = ct1.mapRect(Rect(ipx, ipy, irw, irh));
        QRect qMappedR = qt1.mapRect(QRect(ipx, ipy, irw, irh));
        if (!sameRect(cMappedR, qMappedR)) {
            RectF rf = ct1.mapRect(RectF(ipx, ipy, irw, irh));
            std::fprintf(stderr,
                "Transform::mapRect(Rect) mismatch at i=%d (type=%d, in=(%d,%d,%d,%d)): got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d) rf=(%.17g, %.17g, %.17g, %.17g)\n",
                i, static_cast<int>(ct1.type()), ipx, ipy, irw, irh,
                cMappedR.x(), cMappedR.y(), cMappedR.width(), cMappedR.height(),
                qMappedR.x(), qMappedR.y(), qMappedR.width(), qMappedR.height(),
                rf.x(), rf.y(), rf.width(), rf.height());
            g_failed = 1;
            if (i > 100)
                return;
        }
    }
}

static void testColor()
{
    CHECK(!Color().isValid() && !QColor().isValid(), "Default Color validity mismatch");
    CHECK(Color().red() == QColor().red(), "Invalid Color::red mismatch: %d vs %d", Color().red(), QColor().red());
    CHECK(Color().green() == QColor().green(), "Invalid Color::green mismatch");
    CHECK(Color().blue() == QColor().blue(), "Invalid Color::blue mismatch");
    CHECK(Color().alpha() == QColor().alpha(), "Invalid Color::alpha mismatch: %d vs %d", Color().alpha(), QColor().alpha());
    CHECK(Color().rgba() == QColor().rgba(), "Invalid Color::rgba mismatch");
    CHECK(Color().rgb() == QColor().rgb(), "Invalid Color::rgb mismatch");
    CHECK(Color().name() == QColor().name().toStdString(), "Invalid Color::name mismatch");

    std::mt19937_64 rng(0x8008ULL);
    std::uniform_int_distribution<int> chanDist(-20, 280);
    for (int i = 0; i < kNumSamples; ++i) {
        const int r = chanDist(rng);
        const int g = chanDist(rng);
        const int b = chanDist(rng);
        const int a = chanDist(rng);

        Color cc(r, g, b, a);
        QColor qc(r, g, b, a);

        CHECK(cc.isValid() == qc.isValid(), "Color(r,g,b,a) isValid mismatch at i=%d (%d,%d,%d,%d)", i, r, g, b, a);
        CHECK(cc.red() == qc.red(), "Color::red mismatch at i=%d", i);
        CHECK(cc.green() == qc.green(), "Color::green mismatch at i=%d", i);
        CHECK(cc.blue() == qc.blue(), "Color::blue mismatch at i=%d", i);
        CHECK(cc.alpha() == qc.alpha(),
              "Color::alpha mismatch at i=%d (%d,%d,%d,%d valid=%d): got=%d expected=%d",
              i, r, g, b, a, int(cc.isValid()), cc.alpha(), qc.alpha());
        CHECK(sameBitsF(cc.redF(), qc.redF()), "Color::redF mismatch at i=%d", i);
        CHECK(sameBitsF(cc.greenF(), qc.greenF()), "Color::greenF mismatch at i=%d", i);
        CHECK(sameBitsF(cc.blueF(), qc.blueF()), "Color::blueF mismatch at i=%d", i);
        CHECK(sameBitsF(cc.alphaF(), qc.alphaF()), "Color::alphaF mismatch at i=%d", i);
        CHECK(cc.rgba() == qc.rgba(), "Color::rgba mismatch at i=%d", i);
        CHECK(cc.rgb() == qc.rgb(), "Color::rgb mismatch at i=%d", i);
        CHECK(cc.name(Color::HexRgb) == qc.name(QColor::HexRgb).toStdString(),
              "Color::name(HexRgb) mismatch at i=%d", i);
        CHECK(cc.name(Color::HexArgb) == qc.name(QColor::HexArgb).toStdString(),
              "Color::name(HexArgb) mismatch at i=%d", i);

        if (cc.isValid()) {
            const std::string hex6 = cc.name(Color::HexRgb);
            const std::string hex8 = cc.name(Color::HexArgb);
            Color fromHex6(hex6);
            QColor qFromHex6(QString::fromStdString(hex6));
            CHECK(fromHex6.isValid() == qFromHex6.isValid() && fromHex6.rgba() == qFromHex6.rgba(),
                  "Color from #rrggbb mismatch at i=%d (%s)", i, hex6.c_str());

            Color fromHex8(hex8);
            QColor qFromHex8(QString::fromStdString(hex8));
            CHECK(fromHex8.isValid() == qFromHex8.isValid() && fromHex8.rgba() == qFromHex8.rgba(),
                  "Color from #aarrggbb mismatch at i=%d (%s)", i, hex8.c_str());
        }

        const int newAlpha = i % 256;
        Color ccAlpha = cc;
        QColor qcAlpha = qc;
        ccAlpha.setAlpha(newAlpha);
        qcAlpha.setAlpha(newAlpha);
        CHECK(ccAlpha.isValid() == qcAlpha.isValid() && ccAlpha.rgba() == qcAlpha.rgba(),
              "Color::setAlpha mismatch at i=%d: valid=(%d vs %d) rgba=(%x vs %x)",
              i, int(ccAlpha.isValid()), int(qcAlpha.isValid()), ccAlpha.rgba(), qcAlpha.rgba());
    }

    const char *kHexCases[] = {
        "#000000", "#ffffff", "#FF00aa", "#12345678", "#80ff8040",
        "#abc", "#ABCD", "", "#", "#12", "#12345", "#1234567", "#1234567890", "#zzzzzz"
    };
    for (const char *s : kHexCases) {
        Color cc(s);
        QColor qc(QString::fromLatin1(s));
        CHECK(cc.isValid() == qc.isValid(), "Hex case '%s' isValid mismatch", s);
        CHECK(cc.rgba() == qc.rgba(), "Hex case '%s' rgba mismatch: %u vs %u", s, cc.rgba(), qc.rgba());
    }
}

static void testQtBridgeGeometry()
{
    std::mt19937_64 rng(0x9009ULL);
    for (int i = 0; i < kNumSamples; ++i) {
        const double x = std::clamp(randomDouble(rng), -1e5, 1e5);
        const double y = std::clamp(randomDouble(rng), -1e5, 1e5);
        const double w = std::clamp(randomDouble(rng), -1e5, 1e5);
        const double h = std::clamp(randomDouble(rng), -1e5, 1e5);
        const int ix = std::clamp(randomInt(rng), -50000, 50000);
        const int iy = std::clamp(randomInt(rng), -50000, 50000);
        const int iw = std::clamp(randomInt(rng), -50000, 50000);
        const int ih = std::clamp(randomInt(rng), -50000, 50000);

        QPointF qpf(x, y);
        PointF cpf = qpf;
        QPointF qpfBack = cpf;
        CHECK(samePointF(cpf, qpf) && samePointF(cpf, qpfBack), "Bridge<QPointF> mismatch at i=%d", i);

        QPoint qp(ix, iy);
        Point cp = qp;
        QPoint qpBack = cp;
        CHECK(samePoint(cp, qp) && qp == qpBack, "Bridge<QPoint> mismatch at i=%d", i);

        QSize qs(iw, ih);
        Size cs = qs;
        QSize qsBack = cs;
        CHECK(sameSize(cs, qs) && qs == qsBack, "Bridge<QSize> mismatch at i=%d", i);

        QLineF ql(x, y, w, h);
        LineF cl = ql;
        QLineF qlBack = cl;
        CHECK(sameLineF(cl, ql) && sameLineF(cl, qlBack), "Bridge<QLineF> mismatch at i=%d", i);

        QRectF qrf(x, y, w, h);
        RectF crf = qrf;
        QRectF qrfBack = crf;
        CHECK(sameRectF(crf, qrf) && sameRectF(crf, qrfBack), "Bridge<QRectF> mismatch at i=%d", i);

        QRect qr(ix, iy, iw, ih);
        Rect cr = qr;
        QRect qrBack = cr;
        CHECK(sameRect(cr, qr) && qr == qrBack, "Bridge<QRect> mismatch at i=%d", i);

        Transform ct;
        QTransform qt;
        buildRandomTransformPair(rng, i, ct, qt);
        Transform ctFromQt = qt;
        QTransform qtFromCore = ctFromQt;
        CHECK(sameTransform(ctFromQt, qt) && sameTransform(ctFromQt, qtFromCore),
              "Bridge<QTransform> mismatch at i=%d", i);

        QColor qc((i * 3) % 256, (i * 7) % 256, (i * 11) % 256, (i * 13) % 256);
        Color cc = qc;
        QColor qcBack = cc;
        CHECK(cc.rgba() == qc.rgba() && qc == qcBack, "Bridge<QColor> mismatch at i=%d", i);
    }
}

int main()
{
    testMathFunctions();
    testPointAndSize();
    testPointF();
    testLineF();
    testRect();
    testRectF();
    testTransform();
    testColor();
    testQtBridgeGeometry();

    if (g_failed == 0) {
        std::printf("ALL geometry_tests PASSED (%d random samples per suite)\n", kNumSamples);
    }
    return g_failed;
}
