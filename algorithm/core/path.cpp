#include "path.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace AnimeCore {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Cubic
{
    double x1, y1, x2, y2, x3, y3, x4, y4;

    static Cubic fromPoints(const PointF &p0, const PointF &p1, const PointF &p2, const PointF &p3)
    {
        return {p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y(), p3.x(), p3.y()};
    }

    PointF pointAt(double t) const
    {
        double m_t = 1.0 - t;
        double x12 = x1 * m_t + x2 * t;
        double y12 = y1 * m_t + y2 * t;
        double x23 = x2 * m_t + x3 * t;
        double y23 = y2 * m_t + y3 * t;
        double x34 = x3 * m_t + x4 * t;
        double y34 = y3 * m_t + y4 * t;

        double x123 = x12 * m_t + x23 * t;
        double y123 = y12 * m_t + y23 * t;
        double x234 = x23 * m_t + x34 * t;
        double y234 = y23 * m_t + y34 * t;

        return PointF(x123 * m_t + x234 * t, y123 * m_t + y234 * t);
    }

    PointF derivedAt(double t) const
    {
        double m_t = 1.0 - t;
        double d = t * t;
        double a = -m_t * m_t;
        double b = 1.0 - 4.0 * t + 3.0 * d;
        double c = 2.0 * t - 3.0 * d;
        return PointF(3.0 * (a * x1 + b * x2 + c * x3 + d * x4),
                      3.0 * (a * y1 + b * y2 + c * y3 + d * y4));
    }

    LineF tangentAt(double t) const
    {
        PointF p = pointAt(t);
        PointF dir = derivedAt(t);
        if (dir.isNull()) {
            if (t == 0.0) {
                if (x1 != x2 || y1 != y2)
                    dir = PointF(x2 - x1, y2 - y1);
                else if (x1 != x3 || y1 != y3)
                    dir = PointF(x3 - x1, y3 - y1);
                else
                    dir = PointF(x4 - x1, y4 - y1);
            } else if (t == 1.0) {
                if (x3 != x4 || y3 != y4)
                    dir = PointF(x4 - x3, y4 - y3);
                else if (x2 != x4 || y2 != y4)
                    dir = PointF(x4 - x2, y4 - y2);
                else
                    dir = PointF(x4 - x1, y4 - y1);
            }
        }
        return LineF(p, p + dir);
    }

    void split(Cubic *firstHalf, Cubic *secondHalf) const
    {
        double c = (x2 + x3) * 0.5;
        firstHalf->x2 = (x1 + x2) * 0.5;
        secondHalf->x3 = (x3 + x4) * 0.5;
        firstHalf->x1 = x1;
        secondHalf->x4 = x4;
        firstHalf->x3 = (firstHalf->x2 + c) * 0.5;
        secondHalf->x2 = (secondHalf->x3 + c) * 0.5;
        firstHalf->x4 = secondHalf->x1 = (firstHalf->x3 + secondHalf->x2) * 0.5;

        c = (y2 + y3) * 0.5;
        firstHalf->y2 = (y1 + y2) * 0.5;
        secondHalf->y3 = (y3 + y4) * 0.5;
        firstHalf->y1 = y1;
        secondHalf->y4 = y4;
        firstHalf->y3 = (firstHalf->y2 + c) * 0.5;
        secondHalf->y2 = (secondHalf->y3 + c) * 0.5;
        firstHalf->y4 = secondHalf->y1 = (firstHalf->y3 + secondHalf->y2) * 0.5;
    }

    void addIfClose(double *length, double error) const
    {
        const double dx12 = x2 - x1, dy12 = y2 - y1;
        const double dx23 = x3 - x2, dy23 = y3 - y2;
        const double dx34 = x4 - x3, dy34 = y4 - y3;
        const double dx14 = x4 - x1, dy14 = y4 - y1;
        const double len = std::hypot(dx12, dy12) + std::hypot(dx23, dy23) + std::hypot(dx34, dy34);
        const double chord = std::hypot(dx14, dy14);
        if ((len - chord) > error) {
            Cubic left, right;
            split(&left, &right);
            left.addIfClose(length, error);
            right.addIfClose(length, error);
            return;
        }
        *length += len;
    }

    double length(double error = 0.01) const
    {
        double l = 0.0;
        addIfClose(&l, error);
        return l;
    }

    Cubic prefixTo(double t) const
    {
        double x12 = x1 + t * (x2 - x1);
        double y12 = y1 + t * (y2 - y1);
        double x23 = x2 + t * (x3 - x2);
        double y23 = y2 + t * (y3 - y2);
        double x34 = x3 + t * (x4 - x3);
        double y34 = y3 + t * (y4 - y3);

        double x123 = x12 + t * (x23 - x12);
        double y123 = y12 + t * (y23 - y12);
        double x234 = x23 + t * (x34 - x23);
        double y234 = y23 + t * (y34 - y23);

        double x1234 = x123 + t * (x234 - x123);
        double y1234 = y123 + t * (y234 - y123);

        return {x1, y1, x12, y12, x123, y123, x1234, y1234};
    }

    void parameterSplitLeft(double t, Cubic *left)
    {
        left->x1 = x1;
        left->y1 = y1;

        left->x2 = x1 + t * (x2 - x1);
        left->y2 = y1 + t * (y2 - y1);

        left->x3 = x2 + t * (x3 - x2);
        left->y3 = y2 + t * (y3 - y2);

        x3 = x3 + t * (x4 - x3);
        y3 = y3 + t * (y4 - y3);

        x2 = left->x3 + t * (x3 - left->x3);
        y2 = left->y3 + t * (y3 - left->y3);

        left->x3 = left->x2 + t * (left->x3 - left->x2);
        left->y3 = left->y2 + t * (left->y3 - left->y2);

        left->x4 = x1 = left->x3 + t * (x2 - left->x3);
        left->y4 = y1 = left->y3 + t * (y2 - left->y3);
    }

    Cubic bezierOnInterval(double t0, double t1) const
    {
        if (t0 == 0.0 && t1 == 1.0)
            return *this;

        Cubic bezier = *this;
        Cubic result;
        bezier.parameterSplitLeft(t0, &result);
        double trueT = (t1 - t0) / (1.0 - t0);
        bezier.parameterSplitLeft(trueT, &result);
        return result;
    }

    // Bisects until the prefix is within 0.01 of the wanted length. The result
    // depends on where that stops, so the steps follow Qt's exactly: a different
    // bisection lands on another t, about 1e-5 away.
    double tAtLength(double l) const
    {
        const double error = 0.01;
        const double len = length();
        if (l > len || fuzzyCompare(l, len))
            return 1.0;

        double t = 0.5;
        double lastBigger = 1.0;
        while (true) {
            const double prefixLength = prefixTo(t).length();
            if (std::abs(prefixLength - l) < error)
                return t;
            if (prefixLength < l) {
                t += (lastBigger - t) * 0.5;
            } else {
                lastBigger = t;
                t -= t * 0.5;
            }
        }
    }
};

// One curve's extent, widened into the path's by the caller. Bounds end up in
// project files, so the last bit has to agree with Qt, which shapes this code:
// each curve starts from its own end points, every stationary point updates
// BOTH axes, the point is found by repeated interpolation rather than the
// expanded polynomial, and the roots go through one reciprocal.
struct CubicExtent {
    double minX, minY, maxX, maxY;
};

CubicExtent cubicExtent(const PointF &p0, const PointF &p1, const PointF &p2, const PointF &p3)
{
    CubicExtent e;
    e.minX = std::min(p0.x(), p3.x());
    e.maxX = std::max(p0.x(), p3.x());
    e.minY = std::min(p0.y(), p3.y());
    e.maxY = std::max(p0.y(), p3.y());

    const auto interpolate = [](double v0, double v1, double v2, double v3, double t) {
        const double mt = 1.0 - t;
        double q0 = v0 * mt + v1 * t;
        double q1 = v1 * mt + v2 * t;
        const double q2 = v2 * mt + v3 * t;
        q0 = q0 * mt + q1 * t;
        q1 = q1 * mt + q2 * t;
        return q0 * mt + q1 * t;
    };
    const auto widenAt = [&](double t) {
        if (t < 0.0 || t > 1.0)
            return;
        const double x = interpolate(p0.x(), p1.x(), p2.x(), p3.x(), t);
        const double y = interpolate(p0.y(), p1.y(), p2.y(), p3.y(), t);
        if (x < e.minX) e.minX = x;
        if (x > e.maxX) e.maxX = x;
        if (y < e.minY) e.minY = y;
        if (y > e.maxY) e.maxY = y;
    };
    const auto widenAtStationary = [&](double v0, double v1, double v2, double v3) {
        const double a = 3.0 * (-v0 + 3.0 * v1 - 3.0 * v2 + v3);
        const double b = 6.0 * (v0 - 2.0 * v1 + v2);
        const double c = 3.0 * (v1 - v0);
        if (fuzzyIsNull(a)) {
            if (!fuzzyIsNull(b)) {
                widenAt(-c / b);
            }
            return;
        }
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double s = std::sqrt(disc);
            const double rcp = 1.0 / (2.0 * a);
            widenAt((-b + s) * rcp);
            widenAt((-b - s) * rcp);
        }
    };
    widenAtStationary(p0.x(), p1.x(), p2.x(), p3.x());
    widenAtStationary(p0.y(), p1.y(), p2.y(), p3.y());
    return e;
}

void flattenCubicToPolygon(const Cubic &b, Vector<PointF> &poly, double threshold = 0.5)
{
    Cubic beziers[32];
    beziers[0] = b;
    Cubic *bz = beziers;

    while (bz >= beziers) {
        double y4y1 = bz->y4 - bz->y1;
        double x4x1 = bz->x4 - bz->x1;
        double l = std::abs(x4x1) + std::abs(y4y1);
        double d;
        if (l > 1.0) {
            d = std::abs(x4x1 * (bz->y1 - bz->y2) - y4y1 * (bz->x1 - bz->x2))
              + std::abs(x4x1 * (bz->y1 - bz->y3) - y4y1 * (bz->x1 - bz->x3));
        } else {
            d = std::abs(bz->x1 - bz->x2) + std::abs(bz->y1 - bz->y2)
              + std::abs(bz->x1 - bz->x3) + std::abs(bz->y1 - bz->y3);
            l = 1.0;
        }
        if (d < threshold * l || bz == beziers + 31) {
            poly.append(PointF(bz->x4, bz->y4));
            --bz;
        } else {
            Cubic split;
            bz->split(&split, bz);
            *(++bz) = split;
        }
    }
}

void windingForLine(const PointF &p1, const PointF &p2, const PointF &pt, int &winding, bool &onEdge)
{
    double x1 = p1.x();
    double y1 = p1.y();
    double x2 = p2.x();
    double y2 = p2.y();
    const double x = pt.x();
    const double y = pt.y();

    int dir = 1;
    if (fuzzyCompare(y1, y2)) {
        if (fuzzyCompare(y, y1) && x >= std::min(x1, x2) && x <= std::max(x1, x2)) {
            onEdge = true;
        }
        return;
    } else if (y2 < y1) {
        std::swap(x2, x1);
        std::swap(y2, y1);
        dir = -1;
    }

    if (y < y1 || y >= y2)
        return;

    const double t = (y - y1) / (y2 - y1);
    const double xCross = x1 + t * (x2 - x1);
    if (fuzzyCompare(xCross, x)) {
        onEdge = true;
        return;
    }
    if (xCross <= x) {
        winding += dir;
    }
}

void windingForCubic(const Cubic &b, const PointF &pt, int &winding, bool &onEdge, int depth = 0)
{
    const double minY = std::min(std::min(b.y1, b.y2), std::min(b.y3, b.y4));
    const double maxY = std::max(std::max(b.y1, b.y2), std::max(b.y3, b.y4));
    const double minX = std::min(std::min(b.x1, b.x2), std::min(b.x3, b.x4));
    const double maxX = std::max(std::max(b.x1, b.x2), std::max(b.x3, b.x4));

    if (pt.y() < minY || pt.y() > maxY || pt.x() < minX) {
        return;
    }

    const double dx = maxX - minX;
    const double dy = maxY - minY;
    if (depth >= 24 || (dx < 1e-5 && dy < 1e-5)) {
        windingForLine(PointF(b.x1, b.y1), PointF(b.x4, b.y4), pt, winding, onEdge);
        return;
    }

    // If completely to the left of pt.x() and monotonic in y, we can treat it as a line!
    if (maxX < pt.x()) {
        const bool monoUp = (b.y1 <= b.y2 && b.y2 <= b.y3 && b.y3 <= b.y4);
        const bool monoDown = (b.y1 >= b.y2 && b.y2 >= b.y3 && b.y3 >= b.y4);
        if (monoUp || monoDown) {
            windingForLine(PointF(b.x1, b.y1), PointF(b.x4, b.y4), pt, winding, onEdge);
            return;
        }
    }

    Cubic left, right;
    b.split(&left, &right);
    windingForCubic(left, pt, winding, onEdge, depth + 1);
    if (!onEdge) {
        windingForCubic(right, pt, winding, onEdge, depth + 1);
    }
}

} // namespace

struct Path::Data
{
    std::vector<Element> elements;
    int cStart = 0;
    FillRule fillRule = OddEvenFill;
    bool require_moveTo = false;
    std::shared_ptr<const void> bridged;
};

Path::Path(const PointF &startPoint)
    : m_d(std::make_shared<Data>())
{
    m_d->elements.push_back(Element{startPoint.x(), startPoint.y(), MoveToElement});
}

void Path::ensureData()
{
    if (!m_d) {
        m_d = std::make_shared<Data>();
        m_d->elements.push_back(Element{0.0, 0.0, MoveToElement});
        m_d->cStart = 0;
        m_d->require_moveTo = false;
    }
}

void Path::detach()
{
    if (!m_d) {
        ensureData();
    } else if (m_d.use_count() > 1) {
        m_d = std::make_shared<Data>(*m_d);
    }
    m_d->bridged.reset();
}

std::shared_ptr<const void> Path::bridgedCopy() const
{
    return m_d ? m_d->bridged : nullptr;
}

void Path::setBridgedCopy(std::shared_ptr<const void> copy) const
{
    if (m_d) {
        m_d->bridged = std::move(copy);
    }
}

void Path::maybeMoveTo()
{
    if (m_d && m_d->require_moveTo) {
        Element e = m_d->elements.back();
        e.type = MoveToElement;
        m_d->elements.push_back(e);
        m_d->require_moveTo = false;
    }
}

void Path::clear()
{
    if (!m_d)
        return;
    if (m_d.use_count() == 1) {
        m_d->elements.clear();
        m_d->cStart = 0;
        m_d->require_moveTo = false;
        m_d->bridged.reset();
        if (m_d->fillRule == OddEvenFill && m_d->elements.capacity() == 0) {
            m_d.reset();
        }
    } else {
        const FillRule rule = m_d->fillRule;
        if (rule == OddEvenFill) {
            m_d.reset();
        } else {
            m_d = std::make_shared<Data>();
            m_d->fillRule = rule;
        }
    }
}

void Path::reserve(int size)
{
    if (size <= 0)
        return;
    if (!m_d) {
        ensureData();
    } else {
        detach();
    }
    m_d->elements.reserve(static_cast<std::size_t>(size));
}

int Path::capacity() const
{
    return (!m_d || m_d->elements.empty()) ? 0 : static_cast<int>(m_d->elements.capacity());
}

void Path::closeSubpath()
{
    if (isEmpty())
        return;
    detach();
    m_d->require_moveTo = true;
    const Element &first = m_d->elements[static_cast<std::size_t>(m_d->cStart)];
    Element &last = m_d->elements.back();
    if (first.x != last.x || first.y != last.y) {
        if (fuzzyCompare(PointF(first.x, first.y), PointF(last.x, last.y))) {
            last.x = first.x;
            last.y = first.y;
        } else {
            m_d->elements.push_back(Element{first.x, first.y, LineToElement});
        }
    }
}

void Path::moveTo(const PointF &p)
{
    if (!m_d) {
        m_d = std::make_shared<Data>();
        m_d->elements.push_back(Element{p.x(), p.y(), MoveToElement});
        m_d->cStart = 0;
        m_d->require_moveTo = false;
        return;
    }
    detach();
    if (m_d->elements.empty()) {
        m_d->elements.push_back(Element{p.x(), p.y(), MoveToElement});
        m_d->cStart = 0;
    } else if (m_d->elements.back().type == MoveToElement) {
        m_d->elements.back().x = p.x();
        m_d->elements.back().y = p.y();
        m_d->cStart = static_cast<int>(m_d->elements.size()) - 1;
    } else {
        m_d->cStart = static_cast<int>(m_d->elements.size());
        m_d->elements.push_back(Element{p.x(), p.y(), MoveToElement});
    }
    m_d->require_moveTo = false;
}

void Path::lineTo(const PointF &p)
{
    detach();
    if (m_d->elements.empty()) {
        m_d->elements.push_back(Element{0.0, 0.0, MoveToElement});
        m_d->cStart = 0;
    }
    maybeMoveTo();
    if (p == PointF(m_d->elements.back()))
        return;
    m_d->elements.push_back(Element{p.x(), p.y(), LineToElement});
}

void Path::cubicTo(const PointF &ctrlPt1, const PointF &ctrlPt2, const PointF &endPt)
{
    detach();
    if (m_d->elements.empty()) {
        m_d->elements.push_back(Element{0.0, 0.0, MoveToElement});
        m_d->cStart = 0;
    }
    const PointF prev(m_d->elements.back());
    if (prev == ctrlPt1 && ctrlPt1 == ctrlPt2 && ctrlPt2 == endPt)
        return;
    maybeMoveTo();
    m_d->elements.push_back(Element{ctrlPt1.x(), ctrlPt1.y(), CurveToElement});
    m_d->elements.push_back(Element{ctrlPt2.x(), ctrlPt2.y(), CurveToDataElement});
    m_d->elements.push_back(Element{endPt.x(), endPt.y(), CurveToDataElement});
}

void Path::quadTo(const PointF &ctrlPt, const PointF &endPt)
{
    detach();
    if (m_d->elements.empty()) {
        m_d->elements.push_back(Element{0.0, 0.0, MoveToElement});
        m_d->cStart = 0;
    }
    const PointF prev(m_d->elements.back());
    if (prev == ctrlPt && ctrlPt == endPt)
        return;
    maybeMoveTo();
    const PointF c1((prev.x() + 2.0 * ctrlPt.x()) / 3.0, (prev.y() + 2.0 * ctrlPt.y()) / 3.0);
    const PointF c2((endPt.x() + 2.0 * ctrlPt.x()) / 3.0, (endPt.y() + 2.0 * ctrlPt.y()) / 3.0);
    m_d->elements.push_back(Element{c1.x(), c1.y(), CurveToElement});
    m_d->elements.push_back(Element{c2.x(), c2.y(), CurveToDataElement});
    m_d->elements.push_back(Element{endPt.x(), endPt.y(), CurveToDataElement});
}

PointF Path::currentPosition() const
{
    return (!m_d || m_d->elements.empty())
               ? PointF()
               : PointF(m_d->elements.back().x, m_d->elements.back().y);
}

namespace {

constexpr double kArcKappa = 0.5522847498;

double tForArcAngle(double angle)
{
    if (fuzzyIsNull(angle))
        return 0.0;
    if (fuzzyCompare(angle, 90.0))
        return 1.0;

    double radians = degreesToRadians(angle);
    double cosAngle = std::cos(radians);
    double sinAngle = std::sin(radians);

    double tc = angle / 90.0;
    double ts = tc * tc;

    double b = 0.5;
    double a = 0.0;

    if (angle < 45.0) {
        double x = 0.5 - kArcKappa;
        double A = 3.0 * kArcKappa - 2.0;
        double B = 6.0 * x;
        double C = 3.0 * kArcKappa;
        while (b > 1e-12) {
            a = A * ts * tc + B * ts + C * tc - sinAngle;
            if (std::abs(a) < 1e-12)
                break;
            b *= 0.5;
            tc += (a < 0.0 ? b : -b);
            ts = tc * tc;
        }
    } else {
        double x = kArcKappa - 0.5;
        double A = 2.0 - 3.0 * kArcKappa;
        double B = 3.0 * (kArcKappa - 1.0);
        while (b > 1e-12) {
            a = (A * tc + B) * ts + 1.0 - cosAngle;
            if (std::abs(a) < 1e-12)
                break;
            b *= 0.5;
            tc += (a < 0.0 ? -b : b);
            ts = tc * tc;
        }
    }
    return tc;
}

void findEllipseCoords(const RectF &r, double angle, double length,
                       PointF *startPoint, PointF *endPoint)
{
    if (r.isNull()) {
        if (startPoint)
            *startPoint = PointF();
        if (endPoint)
            *endPoint = PointF();
        return;
    }

    double w2 = r.width() / 2.0;
    double h2 = r.height() / 2.0;

    double angles[2] = { angle, angle + length };
    PointF *points[2] = { startPoint, endPoint };

    for (int i = 0; i < 2; ++i) {
        if (!points[i])
            continue;

        double theta = angles[i] - 360.0 * std::floor(angles[i] / 360.0);
        double t = theta / 90.0;
        int quadrant = int(t);
        t -= quadrant;

        t = tForArcAngle(90.0 * t);

        if (quadrant & 1)
            t = 1.0 - t;

        Cubic unitQuad{1.0, 0.0, 1.0, kArcKappa, kArcKappa, 1.0, 0.0, 1.0};
        PointF pt = unitQuad.pointAt(t);
        double x = pt.x();
        double y = pt.y();

        switch (quadrant) {
        case 0:
            y = -y;
            break;
        case 1:
            x = -x;
            y = -y;
            break;
        case 2:
            x = -x;
            break;
        case 3:
        default:
            break;
        }

        *points[i] = PointF(r.x() + w2 + w2 * x, r.y() + h2 + h2 * y);
    }
}

Cubic arcQuadrantBezier(const RectF &rect, int quadrant, bool counterClockwise)
{
    const double x = rect.x();
    const double y = rect.y();
    const double w2 = rect.width() / 2.0;
    const double h2 = rect.height() / 2.0;
    const double w2k = w2 * kArcKappa;
    const double h2k = h2 * kArcKappa;
    const double cx = x + w2;
    const double cy = y + h2;
    const double right = x + rect.width();
    const double bottom = y + rect.height();

    Cubic c{};
    switch (quadrant & 3) {
    case 0:
        c = {right, cy, right, cy - h2k, cx + w2k, y, cx, y};
        break;
    case 1:
        c = {cx, y, cx - w2k, y, x, cy - h2k, x, cy};
        break;
    case 2:
        c = {x, cy, x, cy + h2k, cx - w2k, bottom, cx, bottom};
        break;
    case 3:
    default:
        c = {cx, bottom, cx + w2k, bottom, right, cy + h2k, right, cy};
        break;
    }
    if (!counterClockwise) {
        return {c.x4, c.y4, c.x3, c.y3, c.x2, c.y2, c.x1, c.y1};
    }
    return c;
}

PointF curvesForArc(const RectF &rect, double startAngle, double sweepLength,
                    PointF *curves, int *pointCount)
{
    *pointCount = 0;
    if (rect.isNull())
        return PointF();

    if (sweepLength > 360.0)
        sweepLength = 360.0;
    else if (sweepLength < -360.0)
        sweepLength = -360.0;

    int startSegment = int(std::floor(startAngle / 90.0));
    int endSegment = int(std::floor((startAngle + sweepLength) / 90.0));

    double startT = (startAngle - startSegment * 90) / 90.0;
    double endT = (startAngle + sweepLength - endSegment * 90) / 90.0;

    int delta = sweepLength > 0 ? 1 : -1;
    if (delta < 0) {
        startT = 1.0 - startT;
        endT = 1.0 - endT;
    }

    if (fuzzyIsNull(startT - 1.0)) {
        startT = 0.0;
        startSegment += delta;
    }
    if (fuzzyIsNull(endT)) {
        endT = 1.0;
        endSegment -= delta;
    }

    startT = tForArcAngle(startT * 90.0);
    endT = tForArcAngle(endT * 90.0);

    const bool splitAtStart = !fuzzyIsNull(startT);
    const bool splitAtEnd = !fuzzyIsNull(endT - 1.0);

    const int end = endSegment + delta;

    if (startSegment == end) {
        PointF startPoint;
        findEllipseCoords(rect, startAngle, sweepLength, &startPoint, nullptr);
        return startPoint;
    }

    PointF startPoint, endPoint;
    findEllipseCoords(rect, startAngle, sweepLength, &startPoint, &endPoint);

    for (int i = startSegment; i != end; i += delta) {
        const int quadrant = ((i % 4) + 4) % 4;
        Cubic c = arcQuadrantBezier(rect, quadrant, delta > 0);
        double t0 = 0.0;
        double t1 = 1.0;
        if (i == startSegment && splitAtStart)
            t0 = startT;
        if (i == endSegment && splitAtEnd)
            t1 = endT;
        c = c.bezierOnInterval(t0, t1);
        curves[(*pointCount)++] = PointF(c.x2, c.y2);
        curves[(*pointCount)++] = PointF(c.x3, c.y3);
        curves[(*pointCount)++] = PointF(c.x4, c.y4);
    }

    if (*pointCount > 0) {
        curves[*pointCount - 1] = endPoint;
    }
    return startPoint;
}

} // namespace

void Path::arcMoveTo(const RectF &rect, double angle)
{
    if (rect.isNull())
        return;
    PointF pt;
    findEllipseCoords(rect, angle, 0.0, &pt, nullptr);
    moveTo(pt);
}

void Path::arcTo(const RectF &rect, double startAngle, double sweepLength)
{
    if (rect.isNull())
        return;
    int pointCount = 0;
    PointF pts[15];
    PointF curveStart = curvesForArc(rect, startAngle, sweepLength, pts, &pointCount);
    lineTo(curveStart);
    // No sweep, no curve: a zero-length arc stops at its start point.
    if (std::abs(sweepLength) <= 1e-12) {
        return;
    }
    for (int i = 0; i < pointCount; i += 3) {
        cubicTo(pts[i].x(), pts[i].y(),
                pts[i + 1].x(), pts[i + 1].y(),
                pts[i + 2].x(), pts[i + 2].y());
    }
}

void Path::addRect(const RectF &r)
{
    if (r.isNull())
        return;
    detach();
    if (m_d->elements.empty()) {
        m_d->elements.push_back(Element{0.0, 0.0, MoveToElement});
    }
    const double x1 = r.x();
    const double y1 = r.y();
    const double x2 = r.x() + r.width();
    const double y2 = r.y() + r.height();
    moveTo(x1, y1);
    m_d->elements.push_back(Element{x2, y1, LineToElement});
    m_d->elements.push_back(Element{x2, y2, LineToElement});
    m_d->elements.push_back(Element{x1, y2, LineToElement});
    m_d->elements.push_back(Element{x1, y1, LineToElement});
    m_d->require_moveTo = true;
}

void Path::addEllipse(const RectF &rect)
{
    if (rect.isNull())
        return;
    const double kappa = 0.5522847498;
    const double x = rect.x();
    const double y = rect.y();
    const double w2 = rect.width() / 2.0;
    const double h2 = rect.height() / 2.0;
    const double w2k = w2 * kappa;
    const double h2k = h2 * kappa;
    const double cx = x + w2;
    const double cy = y + h2;
    const double right = x + rect.width();
    const double bottom = y + rect.height();
    moveTo(right, cy);
    cubicTo(right, cy + h2k, cx + w2k, bottom, cx, bottom);
    cubicTo(cx - w2k, bottom, x, cy + h2k, x, cy);
    cubicTo(x, cy - h2k, cx - w2k, y, cx, y);
    cubicTo(cx + w2k, y, right, cy - h2k, right, cy);
    m_d->require_moveTo = true;
}

void Path::addRoundedRect(const RectF &rect, double xRadius, double yRadius)
{
    const RectF r = rect.normalized();
    if (r.isNull())
        return;
    // Qt turns an absolute radius into a percentage and back, and the round trip
    // moves the corners by an ULP, so the same steps are taken here.
    const double halfW = r.width() / 2.0;
    const double halfH = r.height() / 2.0;
    xRadius = (halfW == 0.0) ? 0.0 : (100.0 * std::min(xRadius, halfW) / halfW);
    yRadius = (halfH == 0.0) ? 0.0 : (100.0 * std::min(yRadius, halfH) / halfH);
    if (xRadius <= 0.0 || yRadius <= 0.0) {
        addRect(r);
        return;
    }
    const double kappa = 0.5522847498;
    const double rxx2 = r.width() * xRadius / 100.0;
    const double ryy2 = r.height() * yRadius / 100.0;
    const double rx = rxx2 / 2.0;
    const double ry = ryy2 / 2.0;
    const double kx = rx * kappa;
    const double ky = ry * kappa;
    const double left = r.x();
    const double top = r.y();
    const double ex = left + r.width() - rxx2;
    const double ey = top + r.height() - ryy2;
    const double cxL = left + rx;
    const double cyT = top + ry;
    const double cxR = ex + rx;
    const double cyB = ey + ry;
    const double right = rxx2 + ex;
    const double bottom = ryy2 + ey;
    moveTo(-rx + cxL, cyT);
    cubicTo(left, cyT - ky, cxL - kx, top, cxL, -ry + cyT);
    lineTo(cxR, -ry + cyT);
    cubicTo(cxR + kx, top, right, cyT - ky, rx + cxR, cyT);
    lineTo(rx + cxR, cyB);
    cubicTo(right, cyB + ky, cxR + kx, bottom, cxR, ry + cyB);
    lineTo(cxL, ry + cyB);
    cubicTo(cxL - kx, bottom, left, cyB + ky, -rx + cxL, cyB);
    closeSubpath();
}

void Path::addPolygon(const Vector<PointF> &polygon){
    if (polygon.isEmpty())
        return;
    detach();
    moveTo(polygon.first());
    for (std::ptrdiff_t i = 1; i < polygon.size(); ++i) {
        lineTo(polygon[i]);
    }
}

void Path::addPath(const Path &source)
{
    // A copy keeps the elements alive and apart when a path is added to itself.
    const Path other = source;
    if (other.isEmpty())
        return;
    detach();
    if (!m_d->elements.empty() && m_d->elements.back().type == MoveToElement) {
        m_d->elements.pop_back();
    }
    const int offset = static_cast<int>(m_d->elements.size());
    m_d->elements.insert(m_d->elements.end(),
                         other.m_d->elements.begin(),
                         other.m_d->elements.end());
    m_d->cStart = offset + other.m_d->cStart;
    m_d->require_moveTo = other.m_d->require_moveTo;
}

void Path::connectPath(const Path &source)
{
    const Path other = source;
    if (other.isEmpty())
        return;
    detach();
    if (isEmpty()) {
        if (!m_d->elements.empty() && m_d->elements.back().type == MoveToElement) {
            m_d->elements.pop_back();
        }
        const int offset = static_cast<int>(m_d->elements.size());
        m_d->elements.insert(m_d->elements.end(),
                             other.m_d->elements.begin(),
                             other.m_d->elements.end());
        m_d->cStart = offset + other.m_d->cStart;
        m_d->require_moveTo = other.m_d->require_moveTo;
        return;
    }

    if (!m_d->elements.empty() && m_d->elements.back().type == MoveToElement) {
        m_d->elements.pop_back();
    }
    int offset = static_cast<int>(m_d->elements.size());
    const Element &firstOther = other.m_d->elements[0];
    if (PointF(firstOther) != PointF(m_d->elements.back())) {
        m_d->elements.push_back(Element{firstOther.x, firstOther.y, LineToElement});
    } else {
        --offset;
    }

    m_d->elements.insert(m_d->elements.end(),
                         other.m_d->elements.begin() + 1,
                         other.m_d->elements.end());
    if (other.m_d->cStart != 0) {
        m_d->cStart = offset + other.m_d->cStart;
    }
}

bool Path::contains(const PointF &pt) const
{
    if (isEmpty())
        return false;

    int winding = 0;
    bool onEdge = false;

    PointF startPt;
    PointF curPt;
    bool haveSubpath = false;

    const std::size_t count = m_d->elements.size();
    std::size_t i = 0;
    while (i < count) {
        const Element &e = m_d->elements[i];
        switch (e.type) {
        case MoveToElement:
            if (haveSubpath && curPt != startPt) {
                windingForLine(curPt, startPt, pt, winding, onEdge);
                if (onEdge)
                    return false;
            }
            startPt = PointF(e.x, e.y);
            curPt = startPt;
            haveSubpath = true;
            ++i;
            break;
        case LineToElement: {
            const PointF nextPt(e.x, e.y);
            windingForLine(curPt, nextPt, pt, winding, onEdge);
            if (onEdge)
                return false;
            curPt = nextPt;
            ++i;
            break;
        }
        case CurveToElement: {
            if (i + 2 < count) {
                const PointF c1(e.x, e.y);
                const PointF c2(m_d->elements[i + 1].x, m_d->elements[i + 1].y);
                const PointF endPt(m_d->elements[i + 2].x, m_d->elements[i + 2].y);
                const Cubic b = Cubic::fromPoints(curPt, c1, c2, endPt);
                windingForCubic(b, pt, winding, onEdge);
                if (onEdge)
                    return false;
                curPt = endPt;
                i += 3;
            } else {
                ++i;
            }
            break;
        }
        case CurveToDataElement:
            ++i;
            break;
        }
    }

    if (haveSubpath && curPt != startPt) {
        windingForLine(curPt, startPt, pt, winding, onEdge);
        if (onEdge)
            return false;
    }

    if (fillRule() == WindingFill) {
        return winding != 0;
    }
    return (winding & 1) != 0;
}

void Path::translate(double dx, double dy)
{
    if (!m_d || m_d->elements.empty() || (dx == 0.0 && dy == 0.0))
        return;
    detach();
    for (Element &e : m_d->elements) {
        e.x += dx;
        e.y += dy;
    }
}

Path Path::translated(double dx, double dy) const
{
    Path copy(*this);
    copy.translate(dx, dy);
    return copy;
}

Path Path::transformed(const Transform &matrix) const
{
    if (matrix.isIdentity() || !m_d || m_d->elements.empty())
        return *this;
    if (matrix.type() == Transform::TxTranslate)
        return translated(matrix.dx(), matrix.dy());

    Path out = *this;
    out.detach();
    for (Element &e : out.m_d->elements) {
        matrix.map(e.x, e.y, &e.x, &e.y);
    }
    return out;
}

RectF Path::boundingRect() const
{
    if (!m_d || m_d->elements.empty())
        return RectF();

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();

    const std::size_t count = m_d->elements.size();
    PointF curPt(m_d->elements[0].x, m_d->elements[0].y);
    minX = maxX = curPt.x();
    minY = maxY = curPt.y();

    std::size_t i = 1;
    while (i < count) {
        const Element &e = m_d->elements[i];
        switch (e.type) {
        case MoveToElement:
        case LineToElement:
            if (e.x < minX) minX = e.x;
            if (e.x > maxX) maxX = e.x;
            if (e.y < minY) minY = e.y;
            if (e.y > maxY) maxY = e.y;
            curPt = PointF(e.x, e.y);
            ++i;
            break;
        case CurveToElement:
            if (i + 2 < count) {
                const Element &e1 = m_d->elements[i + 1];
                const Element &e2 = m_d->elements[i + 2];
                const CubicExtent extent = cubicExtent(curPt, PointF(e.x, e.y), PointF(e1.x, e1.y), PointF(e2.x, e2.y));
                const double right = extent.minX + (extent.maxX - extent.minX);
                const double bottom = extent.minY + (extent.maxY - extent.minY);
                if (extent.minX < minX) minX = extent.minX;
                if (right > maxX) maxX = right;
                if (extent.minY < minY) minY = extent.minY;
                if (bottom > maxY) maxY = bottom;
                curPt = PointF(e2.x, e2.y);
                i += 3;
            } else {
                ++i;
            }
            break;
        case CurveToDataElement:
            ++i;
            break;
        }
    }

    return RectF(minX, minY, maxX - minX, maxY - minY);
}

RectF Path::controlPointRect() const
{
    if (!m_d || m_d->elements.empty())
        return RectF();

    double minX = m_d->elements[0].x;
    double maxX = minX;
    double minY = m_d->elements[0].y;
    double maxY = minY;

    for (std::size_t i = 1; i < m_d->elements.size(); ++i) {
        const Element &e = m_d->elements[i];
        if (e.x < minX) minX = e.x;
        if (e.x > maxX) maxX = e.x;
        if (e.y < minY) minY = e.y;
        if (e.y > maxY) maxY = e.y;
    }

    return RectF(minX, minY, maxX - minX, maxY - minY);
}

Path::FillRule Path::fillRule() const
{
    return m_d ? m_d->fillRule : OddEvenFill;
}

void Path::setFillRule(FillRule rule)
{
    if (fillRule() == rule)
        return;
    detach();
    m_d->fillRule = rule;
}

bool Path::isEmpty() const
{
    return !m_d || m_d->elements.empty()
        || (m_d->elements.size() == 1 && m_d->elements[0].type == MoveToElement);
}

Path Path::toReversed() const
{
    Path rev;
    rev.setFillRule(fillRule());
    if (isEmpty()) {
        if (m_d && m_d->elements.size() == 1 && m_d->elements[0].type == MoveToElement) {
            rev.moveTo(m_d->elements[0].x, m_d->elements[0].y);
        }
        return rev;
    }

    const int count = static_cast<int>(m_d->elements.size());
    rev.moveTo(m_d->elements[static_cast<std::size_t>(count - 1)].x,
               m_d->elements[static_cast<std::size_t>(count - 1)].y);

    for (int i = count - 1; i >= 1; --i) {
        const Element &e = m_d->elements[static_cast<std::size_t>(i)];
        const Element &prev = m_d->elements[static_cast<std::size_t>(i - 1)];
        if (e.type == LineToElement) {
            rev.lineTo(prev.x, prev.y);
        } else if (e.type == MoveToElement) {
            rev.moveTo(prev.x, prev.y);
        } else if (e.type == CurveToDataElement && i >= 3) {
            const Element &c1 = m_d->elements[static_cast<std::size_t>(i - 2)];
            const Element &sp = m_d->elements[static_cast<std::size_t>(i - 3)];
            rev.cubicTo(prev.x, prev.y, c1.x, c1.y, sp.x, sp.y);
            i -= 2;
        }
    }
    return rev;
}

Vector<Vector<PointF>> Path::toSubpathPolygons(const Transform &matrix) const
{
    Vector<Vector<PointF>> flatCurves;
    if (isEmpty())
        return flatCurves;

    Vector<PointF> current;
    const std::size_t count = m_d->elements.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Element &e = m_d->elements[i];
        switch (e.type) {
        case MoveToElement:
            if (current.size() > 1)
                flatCurves.append(current);
            current.clear();
            current.append(matrix.map(PointF(e.x, e.y)));
            break;
        case LineToElement:
            current.append(matrix.map(PointF(e.x, e.y)));
            break;
        case CurveToElement: {
            if (i + 2 < count) {
                const PointF p0 = matrix.map(PointF(m_d->elements[i - 1].x, m_d->elements[i - 1].y));
                const PointF p1 = matrix.map(PointF(e.x, e.y));
                const PointF p2 = matrix.map(PointF(m_d->elements[i + 1].x, m_d->elements[i + 1].y));
                const PointF p3 = matrix.map(PointF(m_d->elements[i + 2].x, m_d->elements[i + 2].y));
                Cubic b = Cubic::fromPoints(p0, p1, p2, p3);
                flattenCubicToPolygon(b, current, 0.5);
                i += 2;
            }
            break;
        }
        case CurveToDataElement:
            break;
        }
    }

    if (current.size() > 1)
        flatCurves.append(current);

    return flatCurves;
}

int Path::elementCount() const
{
    return m_d ? static_cast<int>(m_d->elements.size()) : 0;
}

Path::Element Path::elementAt(int i) const
{
    return m_d->elements[static_cast<std::size_t>(i)];
}

void Path::setElementPositionAt(int i, double x, double y)
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->elements.size()))
        return;
    detach();
    Element &e = m_d->elements[static_cast<std::size_t>(i)];
    e.x = x;
    e.y = y;
}

double Path::length() const
{
    if (isEmpty())
        return 0.0;

    double len = 0.0;
    const std::size_t count = m_d->elements.size();
    for (std::size_t i = 1; i < count; ++i) {
        const Element &e = m_d->elements[i];
        switch (e.type) {
        case MoveToElement:
            break;
        case LineToElement:
            len += LineF(PointF(m_d->elements[i - 1]), PointF(e)).length();
            break;
        case CurveToElement:
            if (i + 2 < count) {
                Cubic b = Cubic::fromPoints(PointF(m_d->elements[i - 1]),
                                            PointF(e),
                                            PointF(m_d->elements[i + 1]),
                                            PointF(m_d->elements[i + 2]));
                len += b.length();
                i += 2;
            }
            break;
        case CurveToDataElement:
            break;
        }
    }
    return len;
}

double Path::percentAtLength(double len) const
{
    if (isEmpty() || len <= 0.0)
        return 0.0;

    const double totalLength = length();
    if (len > totalLength)
        return 1.0;
    if (totalLength == 0.0)
        return 0.0;

    double totLen = 0.0;
    const std::size_t count = m_d->elements.size();
    for (std::size_t i = 1; i < count; ++i) {
        const Element &e = m_d->elements[i];
        switch (e.type) {
        case MoveToElement:
            break;
        case LineToElement: {
            const LineF line(PointF(m_d->elements[i - 1]), PointF(e));
            const double slen = line.length();
            totLen += slen;
            if (totLen >= len) {
                return len / totalLength;
            }
            break;
        }
        case CurveToElement: {
            if (i + 2 < count) {
                const Cubic b = Cubic::fromPoints(PointF(m_d->elements[i - 1]),
                                                  PointF(e),
                                                  PointF(m_d->elements[i + 1]),
                                                  PointF(m_d->elements[i + 2]));
                const double slen = b.length();
                const double prevLen = totLen;
                totLen += slen;
                if (totLen >= len) {
                    const double localT = b.tAtLength(len - prevLen);
                    return (slen * localT + prevLen) / totalLength;
                }
                i += 2;
            }
            break;
        }
        case CurveToDataElement:
            break;
        }
    }

    return 1.0;
}

namespace {

Cubic bezierAtT(const Path &path, double t, double *startingLength, double *bezierLength)
{
    *startingLength = 0.0;
    *bezierLength = 0.0;
    if (path.isEmpty())
        return Cubic{0, 0, 0, 0, 0, 0, 0, 0};

    const double totalLength = path.length();
    const double len = t * totalLength;

    double curLen = 0.0;
    Cubic lastBezier{0, 0, 0, 0, 0, 0, 0, 0};
    const int count = path.elementCount();
    for (int i = 1; i < count; ++i) {
        const Path::Element e = path.elementAt(i);
        switch (e.type) {
        case Path::MoveToElement:
            break;
        case Path::LineToElement: {
            const PointF p1(path.elementAt(i - 1));
            const PointF p2(e);
            const LineF line(p1, p2);
            const double slen = line.length();
            const PointF delta = p2 - p1;
            lastBezier = Cubic::fromPoints(p1, p1 + delta / 3.0, p1 + 2.0 * delta / 3.0, p2);
            *startingLength = curLen;
            *bezierLength = slen;
            curLen += slen;
            if (i == count - 1 || curLen >= len) {
                return lastBezier;
            }
            break;
        }
        case Path::CurveToElement: {
            if (i + 2 < count) {
                lastBezier = Cubic::fromPoints(PointF(path.elementAt(i - 1)),
                                               PointF(e),
                                               PointF(path.elementAt(i + 1)),
                                               PointF(path.elementAt(i + 2)));
                const double slen = lastBezier.length();
                *startingLength = curLen;
                *bezierLength = slen;
                curLen += slen;
                if (i + 2 == count - 1 || curLen >= len) {
                    return lastBezier;
                }
                i += 2;
            }
            break;
        }
        case Path::CurveToDataElement:
            break;
        }
    }
    return lastBezier;
}

} // namespace

PointF Path::pointAtPercent(double t) const
{
    if (t < 0.0 || t > 1.0) {
        t = std::clamp(t, 0.0, 1.0);
    }
    if (!m_d || m_d->elements.empty())
        return PointF();
    if (isEmpty())
        return PointF(m_d->elements[0]);

    const double totalLength = length();
    const double len = totalLength * t;

    double startingLength = 0.0;
    double bezierLen = 0.0;
    const Cubic b = bezierAtT(*this, t, &startingLength, &bezierLen);
    const double localT = bezierLen > 0.0 ? (len - startingLength) / bezierLen : 0.0;
    return b.pointAt(std::clamp(localT, 0.0, 1.0));
}

double Path::angleAtPercent(double t) const
{
    if (t < 0.0 || t > 1.0) {
        t = std::clamp(t, 0.0, 1.0);
    }
    if (isEmpty())
        return 0.0;

    const double totalLength = length();
    const double len = totalLength * t;

    double startingLength = 0.0;
    double bezierLen = 0.0;
    const Cubic b = bezierAtT(*this, t, &startingLength, &bezierLen);
    const double localT = bezierLen > 0.0 ? (len - startingLength) / bezierLen : 0.0;
    return b.tangentAt(std::clamp(localT, 0.0, 1.0)).angle();
}

double Path::slopeAtPercent(double t) const
{
    if (t < 0.0 || t > 1.0) {
        t = std::clamp(t, 0.0, 1.0);
    }
    if (isEmpty())
        return 0.0;

    const double totalLength = length();
    const double len = totalLength * t;

    double startingLength = 0.0;
    double bezierLen = 0.0;
    const Cubic b = bezierAtT(*this, t, &startingLength, &bezierLen);
    const double localT = bezierLen > 0.0 ? (len - startingLength) / bezierLen : 0.0;
    const LineF tang = b.tangentAt(std::clamp(localT, 0.0, 1.0));
    return tang.dy() / tang.dx();
}

bool Path::operator==(const Path &other) const
{
    if (m_d == other.m_d)
        return true;
    if (fillRule() != other.fillRule())
        return false;
    if (isEmpty() && other.isEmpty())
        return true;
    if (!m_d || !other.m_d)
        return false;
    if (m_d->elements.size() != other.m_d->elements.size())
        return false;
    for (std::size_t i = 0; i < m_d->elements.size(); ++i) {
        if (m_d->elements[i] != other.m_d->elements[i])
            return false;
    }
    return true;
}

} // namespace AnimeCore
