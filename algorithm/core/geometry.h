#ifndef ANIMECORE_GEOMETRY_H
#define ANIMECORE_GEOMETRY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "bridge.h"

namespace AnimeCore {

constexpr inline bool fuzzyCompare(double p1, double p2) noexcept
{
    return (std::abs(p1 - p2) * 1000000000000. <= std::min(std::abs(p1), std::abs(p2)));
}

constexpr inline bool fuzzyCompare(float p1, float p2) noexcept
{
    return (std::abs(p1 - p2) * 100000.f <= std::min(std::abs(p1), std::abs(p2)));
}

constexpr inline bool fuzzyIsNull(double d) noexcept
{
    return std::abs(d) <= 0.000000000001;
}

constexpr inline bool fuzzyIsNull(float f) noexcept
{
    return std::abs(f) <= 0.00001f;
}

namespace detail {

constexpr inline bool fuzzyCompareElement(double p1, double p2) noexcept
{
    return (p1 == 0.0 || p2 == 0.0) ? fuzzyIsNull(p1 - p2) : fuzzyCompare(p1, p2);
}

} // namespace detail

inline int round(double d) noexcept
{
    return static_cast<int>(d + std::copysign(decltype(d)(0.5), d));
}

inline int round(float d) noexcept
{
    return static_cast<int>(d + std::copysign(decltype(d)(0.5), d));
}

inline std::int64_t round64(double d) noexcept
{
    return static_cast<std::int64_t>(d + std::copysign(decltype(d)(0.5), d));
}

inline std::int64_t round64(float d) noexcept
{
    return static_cast<std::int64_t>(d + std::copysign(decltype(d)(0.5), d));
}

template <typename T>
constexpr inline const T &bound(const T &minVal, const T &val, const T &maxVal) noexcept
{
    const T &clampedMax = (maxVal < val) ? maxVal : val;
    return (minVal < clampedMax) ? clampedMax : minVal;
}

class Point
{
public:
    constexpr Point() noexcept : m_x(0), m_y(0) {}
    constexpr Point(int xpos, int ypos) noexcept : m_x(xpos), m_y(ypos) {}

    template <class F, std::enable_if_t<BridgesToCore<F, Point>::value, int> = 0>
    constexpr Point(const F &f) : Point(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Point>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr bool isNull() const noexcept { return m_x == 0 && m_y == 0; }

    constexpr int x() const noexcept { return m_x; }
    constexpr int y() const noexcept { return m_y; }
    constexpr void setX(int x) noexcept { m_x = x; }
    constexpr void setY(int y) noexcept { m_y = y; }

    constexpr int &rx() noexcept { return m_x; }
    constexpr int &ry() noexcept { return m_y; }

    constexpr int manhattanLength() const noexcept
    {
        return (m_x < 0 ? -m_x : m_x) + (m_y < 0 ? -m_y : m_y);
    }

    constexpr Point transposed() const noexcept { return Point(m_y, m_x); }

    static constexpr int dotProduct(const Point &p1, const Point &p2) noexcept
    {
        return p1.m_x * p2.m_x + p1.m_y * p2.m_y;
    }

    constexpr Point &operator+=(const Point &p) noexcept
    {
        m_x += p.m_x;
        m_y += p.m_y;
        return *this;
    }

    constexpr Point &operator-=(const Point &p) noexcept
    {
        m_x -= p.m_x;
        m_y -= p.m_y;
        return *this;
    }

    Point &operator*=(float factor) noexcept
    {
        m_x = round(m_x * factor);
        m_y = round(m_y * factor);
        return *this;
    }

    Point &operator*=(double factor) noexcept
    {
        m_x = round(m_x * factor);
        m_y = round(m_y * factor);
        return *this;
    }

    constexpr Point &operator*=(int factor) noexcept
    {
        m_x = m_x * factor;
        m_y = m_y * factor;
        return *this;
    }

    Point &operator/=(double c)
    {
        m_x = round(m_x / c);
        m_y = round(m_y / c);
        return *this;
    }

    friend constexpr bool operator==(const Point &p1, const Point &p2) noexcept
    {
        return p1.m_x == p2.m_x && p1.m_y == p2.m_y;
    }

    friend constexpr bool operator!=(const Point &p1, const Point &p2) noexcept
    {
        return p1.m_x != p2.m_x || p1.m_y != p2.m_y;
    }

    friend constexpr Point operator+(const Point &p1, const Point &p2) noexcept
    {
        return Point(p1.m_x + p2.m_x, p1.m_y + p2.m_y);
    }

    friend constexpr Point operator-(const Point &p1, const Point &p2) noexcept
    {
        return Point(p1.m_x - p2.m_x, p1.m_y - p2.m_y);
    }

    friend Point operator*(const Point &p, float factor) noexcept
    {
        return Point(round(p.m_x * factor), round(p.m_y * factor));
    }

    friend Point operator*(const Point &p, double factor) noexcept
    {
        return Point(round(p.m_x * factor), round(p.m_y * factor));
    }

    friend constexpr Point operator*(const Point &p, int factor) noexcept
    {
        return Point(p.m_x * factor, p.m_y * factor);
    }

    friend Point operator*(float factor, const Point &p) noexcept
    {
        return Point(round(p.m_x * factor), round(p.m_y * factor));
    }

    friend Point operator*(double factor, const Point &p) noexcept
    {
        return Point(round(p.m_x * factor), round(p.m_y * factor));
    }

    friend constexpr Point operator*(int factor, const Point &p) noexcept
    {
        return Point(p.m_x * factor, p.m_y * factor);
    }

    friend constexpr Point operator+(const Point &p) noexcept { return p; }

    friend constexpr Point operator-(const Point &p) noexcept
    {
        return Point(-p.m_x, -p.m_y);
    }

    friend Point operator/(const Point &p, double c)
    {
        return Point(round(p.m_x / c), round(p.m_y / c));
    }

private:
    int m_x;
    int m_y;
};

class PointF
{
public:
    constexpr PointF() noexcept : m_x(0.0), m_y(0.0) {}
    constexpr PointF(const Point &p) noexcept : m_x(p.x()), m_y(p.y()) {}
    constexpr PointF(double xpos, double ypos) noexcept : m_x(xpos), m_y(ypos) {}

    template <class F, std::enable_if_t<BridgesToCore<F, PointF>::value, int> = 0>
    constexpr PointF(const F &f) : PointF(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, PointF>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr double manhattanLength() const noexcept
    {
        return std::abs(m_x) + std::abs(m_y);
    }

    constexpr bool isNull() const noexcept
    {
        return m_x == 0.0 && m_y == 0.0;
    }

    constexpr double x() const noexcept { return m_x; }
    constexpr double y() const noexcept { return m_y; }
    constexpr void setX(double x) noexcept { m_x = x; }
    constexpr void setY(double y) noexcept { m_y = y; }

    constexpr double &rx() noexcept { return m_x; }
    constexpr double &ry() noexcept { return m_y; }

    constexpr PointF transposed() const noexcept { return PointF(m_y, m_x); }

    constexpr PointF &operator+=(const PointF &p) noexcept
    {
        m_x += p.m_x;
        m_y += p.m_y;
        return *this;
    }

    constexpr PointF &operator-=(const PointF &p) noexcept
    {
        m_x -= p.m_x;
        m_y -= p.m_y;
        return *this;
    }

    constexpr PointF &operator*=(double c) noexcept
    {
        m_x *= c;
        m_y *= c;
        return *this;
    }

    constexpr PointF &operator/=(double divisor)
    {
        m_x /= divisor;
        m_y /= divisor;
        return *this;
    }

    static constexpr double dotProduct(const PointF &p1, const PointF &p2) noexcept
    {
        return p1.m_x * p2.m_x + p1.m_y * p2.m_y;
    }

    friend constexpr bool fuzzyCompare(const PointF &p1, const PointF &p2) noexcept
    {
        return detail::fuzzyCompareElement(p1.m_x, p2.m_x)
            && detail::fuzzyCompareElement(p1.m_y, p2.m_y);
    }

    friend constexpr bool operator==(const PointF &p1, const PointF &p2) noexcept
    {
        return fuzzyCompare(p1, p2);
    }

    friend constexpr bool operator!=(const PointF &p1, const PointF &p2) noexcept
    {
        return !fuzzyCompare(p1, p2);
    }

    friend constexpr PointF operator+(const PointF &p1, const PointF &p2) noexcept
    {
        return PointF(p1.m_x + p2.m_x, p1.m_y + p2.m_y);
    }

    friend constexpr PointF operator-(const PointF &p1, const PointF &p2) noexcept
    {
        return PointF(p1.m_x - p2.m_x, p1.m_y - p2.m_y);
    }

    friend constexpr PointF operator*(const PointF &p, double c) noexcept
    {
        return PointF(p.m_x * c, p.m_y * c);
    }

    friend constexpr PointF operator*(double c, const PointF &p) noexcept
    {
        return PointF(p.m_x * c, p.m_y * c);
    }

    friend constexpr PointF operator+(const PointF &p) noexcept { return p; }

    friend constexpr PointF operator-(const PointF &p) noexcept
    {
        return PointF(-p.m_x, -p.m_y);
    }

    friend constexpr PointF operator/(const PointF &p, double divisor)
    {
        return PointF(p.m_x / divisor, p.m_y / divisor);
    }

    Point toPoint() const noexcept
    {
        return Point(round(m_x), round(m_y));
    }

private:
    double m_x;
    double m_y;
};

class Size
{
public:
    constexpr Size() noexcept : m_wd(-1), m_ht(-1) {}
    constexpr Size(int w, int h) noexcept : m_wd(w), m_ht(h) {}

    template <class F, std::enable_if_t<BridgesToCore<F, Size>::value, int> = 0>
    constexpr Size(const F &f) : Size(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Size>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr bool isNull() const noexcept { return m_wd == 0 && m_ht == 0; }
    constexpr bool isEmpty() const noexcept { return m_wd < 1 || m_ht < 1; }
    constexpr bool isValid() const noexcept { return m_wd >= 0 && m_ht >= 0; }

    constexpr int width() const noexcept { return m_wd; }
    constexpr int height() const noexcept { return m_ht; }
    constexpr void setWidth(int w) noexcept { m_wd = w; }
    constexpr void setHeight(int h) noexcept { m_ht = h; }

    constexpr void transpose() noexcept
    {
        int tmp = m_wd;
        m_wd = m_ht;
        m_ht = tmp;
    }

    constexpr Size transposed() const noexcept { return Size(m_ht, m_wd); }

    constexpr Size expandedTo(const Size &otherSize) const noexcept
    {
        return Size(std::max(m_wd, otherSize.m_wd), std::max(m_ht, otherSize.m_ht));
    }

    constexpr Size boundedTo(const Size &otherSize) const noexcept
    {
        return Size(std::min(m_wd, otherSize.m_wd), std::min(m_ht, otherSize.m_ht));
    }

    constexpr int &rwidth() noexcept { return m_wd; }
    constexpr int &rheight() noexcept { return m_ht; }

    constexpr Size &operator+=(const Size &s) noexcept
    {
        m_wd += s.m_wd;
        m_ht += s.m_ht;
        return *this;
    }

    constexpr Size &operator-=(const Size &s) noexcept
    {
        m_wd -= s.m_wd;
        m_ht -= s.m_ht;
        return *this;
    }

    constexpr Size &operator*=(int c) noexcept
    {
        m_wd *= c;
        m_ht *= c;
        return *this;
    }

    Size &operator*=(double c) noexcept
    {
        m_wd = round(m_wd * c);
        m_ht = round(m_ht * c);
        return *this;
    }

    Size &operator/=(double c)
    {
        m_wd = round(m_wd / c);
        m_ht = round(m_ht / c);
        return *this;
    }

    friend constexpr bool operator==(const Size &s1, const Size &s2) noexcept
    {
        return s1.m_wd == s2.m_wd && s1.m_ht == s2.m_ht;
    }

    friend constexpr bool operator!=(const Size &s1, const Size &s2) noexcept
    {
        return s1.m_wd != s2.m_wd || s1.m_ht != s2.m_ht;
    }

    friend constexpr Size operator+(const Size &s1, const Size &s2) noexcept
    {
        return Size(s1.m_wd + s2.m_wd, s1.m_ht + s2.m_ht);
    }

    friend constexpr Size operator-(const Size &s1, const Size &s2) noexcept
    {
        return Size(s1.m_wd - s2.m_wd, s1.m_ht - s2.m_ht);
    }

    friend constexpr Size operator*(const Size &s, int c) noexcept
    {
        return Size(s.m_wd * c, s.m_ht * c);
    }

    friend constexpr Size operator*(int c, const Size &s) noexcept
    {
        return Size(s.m_wd * c, s.m_ht * c);
    }

    friend Size operator*(const Size &s, double c) noexcept
    {
        return Size(round(s.m_wd * c), round(s.m_ht * c));
    }

    friend Size operator*(double c, const Size &s) noexcept
    {
        return Size(round(s.m_wd * c), round(s.m_ht * c));
    }

    friend Size operator/(const Size &s, double c)
    {
        return Size(round(s.m_wd / c), round(s.m_ht / c));
    }

private:
    int m_wd;
    int m_ht;
};

class LineF
{
public:
    enum IntersectionType {
        NoIntersection,
        BoundedIntersection,
        UnboundedIntersection
    };
    using IntersectType = IntersectionType;

    constexpr LineF() noexcept = default;
    constexpr LineF(const PointF &pt1, const PointF &pt2) noexcept : m_pt1(pt1), m_pt2(pt2) {}
    constexpr LineF(double x1pos, double y1pos, double x2pos, double y2pos) noexcept
        : m_pt1(x1pos, y1pos), m_pt2(x2pos, y2pos) {}
    constexpr LineF(const Point &pt1, const Point &pt2) noexcept
        : m_pt1(pt1), m_pt2(pt2) {}

    template <class F, std::enable_if_t<BridgesToCore<F, LineF>::value, int> = 0>
    constexpr LineF(const F &f) : LineF(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, LineF>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    static LineF fromPolar(double length, double angle) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        const double angleR = angle * (2.0 * kPi / 360.0);
        return LineF(0.0, 0.0, std::cos(angleR) * length, -std::sin(angleR) * length);
    }

    constexpr bool isNull() const noexcept
    {
        return m_pt1 == m_pt2;
    }

    constexpr PointF p1() const noexcept { return m_pt1; }
    constexpr PointF p2() const noexcept { return m_pt2; }

    constexpr double x1() const noexcept { return m_pt1.x(); }
    constexpr double y1() const noexcept { return m_pt1.y(); }
    constexpr double x2() const noexcept { return m_pt2.x(); }
    constexpr double y2() const noexcept { return m_pt2.y(); }

    constexpr double dx() const noexcept { return m_pt2.x() - m_pt1.x(); }
    constexpr double dy() const noexcept { return m_pt2.y() - m_pt1.y(); }

    double length() const noexcept
    {
        return std::hypot(dx(), dy());
    }

    void setLength(double len) noexcept
    {
        const double l = length();
        if (fuzzyIsNull(l))
            return;
        m_pt2 = PointF(m_pt1.x() + dx() / l * len, m_pt1.y() + dy() / l * len);
    }

    double angle() const noexcept
    {
        const double d_x = m_pt2.x() - m_pt1.x();
        const double d_y = m_pt2.y() - m_pt1.y();
        constexpr double kPi = 3.14159265358979323846;
        const double theta = std::atan2(-d_y, d_x) * (360.0 / (2.0 * kPi));
        const double theta_normalized = theta < 0.0 ? theta + 360.0 : theta;
        if (fuzzyCompare(theta_normalized, 360.0))
            return 0.0;
        return theta_normalized;
    }

    void setAngle(double a) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        const double angleR = a * (2.0 * kPi / 360.0);
        const double l = length();
        const double d_x = std::cos(angleR) * l;
        const double d_y = -std::sin(angleR) * l;
        m_pt2.setX(m_pt1.x() + d_x);
        m_pt2.setY(m_pt1.y() + d_y);
    }

    double angleTo(const LineF &l) const noexcept
    {
        if (isNull() || l.isNull())
            return 0.0;
        const double a1 = angle();
        const double a2 = l.angle();
        const double delta = a2 - a1;
        const double delta_normalized = delta < 0.0 ? delta + 360.0 : delta;
        if (fuzzyCompare(delta, 360.0))
            return 0.0;
        return delta_normalized;
    }

    LineF unitVector() const noexcept
    {
        const double x = dx();
        const double y = dy();
        const double f = std::hypot(x, y);
        return LineF(m_pt1, PointF(m_pt1.x() + x / f, m_pt1.y() + y / f));
    }

    constexpr LineF normalVector() const noexcept
    {
        return LineF(p1(), p1() + PointF(dy(), -dx()));
    }

    IntersectionType intersects(const LineF &l, PointF *intersectionPoint = nullptr) const noexcept
    {
        const PointF a = m_pt2 - m_pt1;
        const PointF b = l.m_pt1 - l.m_pt2;
        const PointF c = m_pt1 - l.m_pt1;

        const double denominator = a.y() * b.x() - a.x() * b.y();
        if (denominator == 0.0 || !std::isfinite(denominator))
            return NoIntersection;

        const double reciprocal = 1.0 / denominator;
        const double na = (b.y() * c.x() - b.x() * c.y()) * reciprocal;
        if (intersectionPoint)
            *intersectionPoint = m_pt1 + a * na;

        if (na < 0.0 || na > 1.0)
            return UnboundedIntersection;

        const double nb = (a.x() * c.y() - a.y() * c.x()) * reciprocal;
        if (nb < 0.0 || nb > 1.0)
            return UnboundedIntersection;

        return BoundedIntersection;
    }

    constexpr void translate(const PointF &p) noexcept
    {
        m_pt1 += p;
        m_pt2 += p;
    }

    constexpr void translate(double adx, double ady) noexcept
    {
        translate(PointF(adx, ady));
    }

    constexpr LineF translated(const PointF &p) const noexcept
    {
        return LineF(m_pt1 + p, m_pt2 + p);
    }

    constexpr LineF translated(double adx, double ady) const noexcept
    {
        return translated(PointF(adx, ady));
    }

    constexpr PointF center() const noexcept
    {
        return PointF(0.5 * m_pt1.x() + 0.5 * m_pt2.x(), 0.5 * m_pt1.y() + 0.5 * m_pt2.y());
    }

    constexpr void setP1(const PointF &aP1) noexcept { m_pt1 = aP1; }
    constexpr void setP2(const PointF &aP2) noexcept { m_pt2 = aP2; }
    constexpr void setPoints(const PointF &aP1, const PointF &aP2) noexcept
    {
        m_pt1 = aP1;
        m_pt2 = aP2;
    }
    constexpr void setLine(double aX1, double aY1, double aX2, double aY2) noexcept
    {
        m_pt1 = PointF(aX1, aY1);
        m_pt2 = PointF(aX2, aY2);
    }

    constexpr PointF pointAt(double t) const noexcept
    {
        return PointF(m_pt1.x() + (m_pt2.x() - m_pt1.x()) * t,
                      m_pt1.y() + (m_pt2.y() - m_pt1.y()) * t);
    }

    constexpr bool operator==(const LineF &d) const noexcept
    {
        return m_pt1 == d.m_pt1 && m_pt2 == d.m_pt2;
    }

    constexpr bool operator!=(const LineF &d) const noexcept
    {
        return !(*this == d);
    }

private:
    PointF m_pt1;
    PointF m_pt2;
};

class Rect
{
public:
    constexpr Rect() noexcept : m_x1(0), m_y1(0), m_x2(-1), m_y2(-1) {}
    constexpr Rect(const Point &topleft, const Point &bottomright) noexcept
        : m_x1(topleft.x()), m_y1(topleft.y()), m_x2(bottomright.x()), m_y2(bottomright.y()) {}
    constexpr Rect(const Point &topleft, const Size &size) noexcept
        : m_x1(topleft.x()), m_y1(topleft.y()),
          m_x2(topleft.x() + size.width() - 1), m_y2(topleft.y() + size.height() - 1) {}
    constexpr Rect(int aleft, int atop, int awidth, int aheight) noexcept
        : m_x1(aleft), m_y1(atop), m_x2(aleft + awidth - 1), m_y2(atop + aheight - 1) {}

    template <class F, std::enable_if_t<BridgesToCore<F, Rect>::value, int> = 0>
    constexpr Rect(const F &f) : Rect(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Rect>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr bool isNull() const noexcept
    {
        return m_x2 == m_x1 - 1 && m_y2 == m_y1 - 1;
    }

    constexpr bool isEmpty() const noexcept
    {
        return m_x1 > m_x2 || m_y1 > m_y2;
    }

    constexpr bool isValid() const noexcept
    {
        return m_x1 <= m_x2 && m_y1 <= m_y2;
    }

    constexpr int left() const noexcept { return m_x1; }
    constexpr int top() const noexcept { return m_y1; }
    constexpr int right() const noexcept { return m_x2; }
    constexpr int bottom() const noexcept { return m_y2; }

    constexpr int x() const noexcept { return m_x1; }
    constexpr int y() const noexcept { return m_y1; }
    constexpr void setLeft(int pos) noexcept { m_x1 = pos; }
    constexpr void setTop(int pos) noexcept { m_y1 = pos; }
    constexpr void setRight(int pos) noexcept { m_x2 = pos; }
    constexpr void setBottom(int pos) noexcept { m_y2 = pos; }
    constexpr void setX(int ax) noexcept { m_x1 = ax; }
    constexpr void setY(int ay) noexcept { m_y1 = ay; }

    constexpr void setTopLeft(const Point &p) noexcept { m_x1 = p.x(); m_y1 = p.y(); }
    constexpr void setBottomRight(const Point &p) noexcept { m_x2 = p.x(); m_y2 = p.y(); }
    constexpr void setTopRight(const Point &p) noexcept { m_x2 = p.x(); m_y1 = p.y(); }
    constexpr void setBottomLeft(const Point &p) noexcept { m_x1 = p.x(); m_y2 = p.y(); }

    constexpr Point topLeft() const noexcept { return Point(m_x1, m_y1); }
    constexpr Point bottomRight() const noexcept { return Point(m_x2, m_y2); }
    constexpr Point topRight() const noexcept { return Point(m_x2, m_y1); }
    constexpr Point bottomLeft() const noexcept { return Point(m_x1, m_y2); }
    constexpr Point center() const noexcept
    {
        return Point(static_cast<int>((static_cast<std::int64_t>(m_x1) + m_x2) / 2),
                     static_cast<int>((static_cast<std::int64_t>(m_y1) + m_y2) / 2));
    }

    constexpr int width() const noexcept { return m_x2 - m_x1 + 1; }
    constexpr int height() const noexcept { return m_y2 - m_y1 + 1; }
    constexpr Size size() const noexcept { return Size(width(), height()); }

    constexpr void translate(int dx, int dy) noexcept
    {
        m_x1 += dx;
        m_y1 += dy;
        m_x2 += dx;
        m_y2 += dy;
    }

    constexpr void translate(const Point &p) noexcept
    {
        m_x1 += p.x();
        m_y1 += p.y();
        m_x2 += p.x();
        m_y2 += p.y();
    }

    constexpr Rect translated(int dx, int dy) const noexcept
    {
        return Rect(Point(m_x1 + dx, m_y1 + dy), Point(m_x2 + dx, m_y2 + dy));
    }

    constexpr Rect translated(const Point &p) const noexcept
    {
        return Rect(Point(m_x1 + p.x(), m_y1 + p.y()), Point(m_x2 + p.x(), m_y2 + p.y()));
    }

    constexpr Rect transposed() const noexcept
    {
        return Rect(topLeft(), size().transposed());
    }

    constexpr void moveTo(int ax, int ay) noexcept
    {
        m_x2 += ax - m_x1;
        m_y2 += ay - m_y1;
        m_x1 = ax;
        m_y1 = ay;
    }

    constexpr void moveTo(const Point &p) noexcept
    {
        moveTo(p.x(), p.y());
    }

    constexpr void moveLeft(int pos) noexcept { m_x2 += (pos - m_x1); m_x1 = pos; }
    constexpr void moveTop(int pos) noexcept { m_y2 += (pos - m_y1); m_y1 = pos; }
    constexpr void moveRight(int pos) noexcept { m_x1 += (pos - m_x2); m_x2 = pos; }
    constexpr void moveBottom(int pos) noexcept { m_y1 += (pos - m_y2); m_y2 = pos; }
    constexpr void moveTopLeft(const Point &p) noexcept { moveLeft(p.x()); moveTop(p.y()); }
    constexpr void moveBottomRight(const Point &p) noexcept { moveRight(p.x()); moveBottom(p.y()); }
    constexpr void moveTopRight(const Point &p) noexcept { moveRight(p.x()); moveTop(p.y()); }
    constexpr void moveBottomLeft(const Point &p) noexcept { moveLeft(p.x()); moveBottom(p.y()); }

    constexpr void moveCenter(const Point &p) noexcept
    {
        int w = m_x2 - m_x1;
        int h = m_y2 - m_y1;
        m_x1 = p.x() - w / 2;
        m_y1 = p.y() - h / 2;
        m_x2 = m_x1 + w;
        m_y2 = m_y1 + h;
    }

    constexpr void getRect(int *ax, int *ay, int *aw, int *ah) const noexcept
    {
        *ax = m_x1;
        *ay = m_y1;
        *aw = m_x2 - m_x1 + 1;
        *ah = m_y2 - m_y1 + 1;
    }

    constexpr void setRect(int ax, int ay, int aw, int ah) noexcept
    {
        m_x1 = ax;
        m_y1 = ay;
        m_x2 = (ax + aw - 1);
        m_y2 = (ay + ah - 1);
    }

    constexpr void getCoords(int *xp1, int *yp1, int *xp2, int *yp2) const noexcept
    {
        *xp1 = m_x1;
        *yp1 = m_y1;
        *xp2 = m_x2;
        *yp2 = m_y2;
    }

    constexpr void setCoords(int xp1, int yp1, int xp2, int yp2) noexcept
    {
        m_x1 = xp1;
        m_y1 = yp1;
        m_x2 = xp2;
        m_y2 = yp2;
    }

    constexpr void adjust(int dx1, int dy1, int dx2, int dy2) noexcept
    {
        m_x1 += dx1;
        m_y1 += dy1;
        m_x2 += dx2;
        m_y2 += dy2;
    }

    constexpr Rect adjusted(int xp1, int yp1, int xp2, int yp2) const noexcept
    {
        return Rect(Point(m_x1 + xp1, m_y1 + yp1), Point(m_x2 + xp2, m_y2 + yp2));
    }

    constexpr void setWidth(int w) noexcept { m_x2 = (m_x1 + w - 1); }
    constexpr void setHeight(int h) noexcept { m_y2 = (m_y1 + h - 1); }
    constexpr void setSize(const Size &s) noexcept
    {
        m_x2 = (s.width() + m_x1 - 1);
        m_y2 = (s.height() + m_y1 - 1);
    }

    constexpr Rect normalized() const noexcept
    {
        Rect r;
        if (m_x2 < m_x1 - 1) {
            r.m_x1 = m_x2 + 1;
            r.m_x2 = m_x1 - 1;
        } else {
            r.m_x1 = m_x1;
            r.m_x2 = m_x2;
        }
        if (m_y2 < m_y1 - 1) {
            r.m_y1 = m_y2 + 1;
            r.m_y2 = m_y1 - 1;
        } else {
            r.m_y1 = m_y1;
            r.m_y2 = m_y2;
        }
        return r;
    }

    constexpr bool contains(int ax, int ay, bool aproper) const noexcept
    {
        int l = m_x1;
        int r = m_x1 - 1;
        if (m_x2 - m_x1 + 1 < 0)
            l = m_x2 + 1;
        else
            r = m_x2;
        if (aproper) {
            if (ax <= l || ax >= r)
                return false;
        } else {
            if (ax < l || ax > r)
                return false;
        }
        int t = m_y1;
        int b = m_y1 - 1;
        if (m_y2 - m_y1 + 1 < 0)
            t = m_y2 + 1;
        else
            b = m_y2;
        if (aproper) {
            if (ay <= t || ay >= b)
                return false;
        } else {
            if (ay < t || ay > b)
                return false;
        }
        return true;
    }

    constexpr bool contains(int ax, int ay) const noexcept
    {
        return contains(ax, ay, false);
    }

    constexpr bool contains(const Point &p, bool aproper = false) const noexcept
    {
        return contains(p.x(), p.y(), aproper);
    }

    constexpr bool contains(const Rect &r, bool aproper = false) const noexcept
    {
        if (isNull() || r.isNull())
            return false;

        int l1 = m_x1;
        int r1 = m_x1 - 1;
        if (m_x2 - m_x1 + 1 < 0)
            l1 = m_x2 + 1;
        else
            r1 = m_x2;

        int l2 = r.m_x1;
        int r2 = r.m_x1 - 1;
        if (r.m_x2 - r.m_x1 + 1 < 0)
            l2 = r.m_x2 + 1;
        else
            r2 = r.m_x2;

        if (aproper) {
            if (l2 <= l1 || r2 >= r1)
                return false;
        } else {
            if (l2 < l1 || r2 > r1)
                return false;
        }

        int t1 = m_y1;
        int b1 = m_y1 - 1;
        if (m_y2 - m_y1 + 1 < 0)
            t1 = m_y2 + 1;
        else
            b1 = m_y2;

        int t2 = r.m_y1;
        int b2 = r.m_y1 - 1;
        if (r.m_y2 - r.m_y1 + 1 < 0)
            t2 = r.m_y2 + 1;
        else
            b2 = r.m_y2;

        if (aproper) {
            if (t2 <= t1 || b2 >= b1)
                return false;
        } else {
            if (t2 < t1 || b2 > b1)
                return false;
        }

        return true;
    }

    constexpr Rect operator|(const Rect &r) const noexcept
    {
        if (isNull())
            return r;
        if (r.isNull())
            return *this;

        int l1 = m_x1;
        int r1 = m_x1 - 1;
        if (m_x2 - m_x1 + 1 < 0)
            l1 = m_x2 + 1;
        else
            r1 = m_x2;

        int l2 = r.m_x1;
        int r2 = r.m_x1 - 1;
        if (r.m_x2 - r.m_x1 + 1 < 0)
            l2 = r.m_x2 + 1;
        else
            r2 = r.m_x2;

        int t1 = m_y1;
        int b1 = m_y1 - 1;
        if (m_y2 - m_y1 + 1 < 0)
            t1 = m_y2 + 1;
        else
            b1 = m_y2;

        int t2 = r.m_y1;
        int b2 = r.m_y1 - 1;
        if (r.m_y2 - r.m_y1 + 1 < 0)
            t2 = r.m_y2 + 1;
        else
            b2 = r.m_y2;

        Rect tmp;
        tmp.m_x1 = std::min(l1, l2);
        tmp.m_x2 = std::max(r1, r2);
        tmp.m_y1 = std::min(t1, t2);
        tmp.m_y2 = std::max(b1, b2);
        return tmp;
    }

    constexpr Rect operator&(const Rect &r) const noexcept
    {
        if (isNull() || r.isNull())
            return Rect();

        int l1 = m_x1;
        int r1 = m_x1 - 1;
        if (m_x2 - m_x1 + 1 < 0)
            l1 = m_x2 + 1;
        else
            r1 = m_x2;

        int l2 = r.m_x1;
        int r2 = r.m_x1 - 1;
        if (r.m_x2 - r.m_x1 + 1 < 0)
            l2 = r.m_x2 + 1;
        else
            r2 = r.m_x2;

        if (l1 > r2 || l2 > r1)
            return Rect();

        int t1 = m_y1;
        int b1 = m_y1 - 1;
        if (m_y2 - m_y1 + 1 < 0)
            t1 = m_y2 + 1;
        else
            b1 = m_y2;

        int t2 = r.m_y1;
        int b2 = r.m_y1 - 1;
        if (r.m_y2 - r.m_y1 + 1 < 0)
            t2 = r.m_y2 + 1;
        else
            b2 = r.m_y2;

        if (t1 > b2 || t2 > b1)
            return Rect();

        Rect tmp;
        tmp.m_x1 = std::max(l1, l2);
        tmp.m_x2 = std::min(r1, r2);
        tmp.m_y1 = std::max(t1, t2);
        tmp.m_y2 = std::min(b1, b2);
        return tmp;
    }

    constexpr Rect &operator|=(const Rect &r) noexcept
    {
        *this = *this | r;
        return *this;
    }

    constexpr Rect &operator&=(const Rect &r) noexcept
    {
        *this = *this & r;
        return *this;
    }

    constexpr Rect united(const Rect &other) const noexcept
    {
        return *this | other;
    }

    constexpr Rect intersected(const Rect &other) const noexcept
    {
        return *this & other;
    }

    constexpr bool intersects(const Rect &r) const noexcept
    {
        if (isNull() || r.isNull())
            return false;

        int l1 = m_x1;
        int r1 = m_x1 - 1;
        if (m_x2 - m_x1 + 1 < 0)
            l1 = m_x2 + 1;
        else
            r1 = m_x2;

        int l2 = r.m_x1;
        int r2 = r.m_x1 - 1;
        if (r.m_x2 - r.m_x1 + 1 < 0)
            l2 = r.m_x2 + 1;
        else
            r2 = r.m_x2;

        if (l1 > r2 || l2 > r1)
            return false;

        int t1 = m_y1;
        int b1 = m_y1 - 1;
        if (m_y2 - m_y1 + 1 < 0)
            t1 = m_y2 + 1;
        else
            b1 = m_y2;

        int t2 = r.m_y1;
        int b2 = r.m_y1 - 1;
        if (r.m_y2 - r.m_y1 + 1 < 0)
            t2 = r.m_y2 + 1;
        else
            b2 = r.m_y2;

        if (t1 > b2 || t2 > b1)
            return false;

        return true;
    }

    friend constexpr bool operator==(const Rect &r1, const Rect &r2) noexcept
    {
        return r1.m_x1 == r2.m_x1 && r1.m_x2 == r2.m_x2
            && r1.m_y1 == r2.m_y1 && r1.m_y2 == r2.m_y2;
    }

    friend constexpr bool operator!=(const Rect &r1, const Rect &r2) noexcept
    {
        return r1.m_x1 != r2.m_x1 || r1.m_x2 != r2.m_x2
            || r1.m_y1 != r2.m_y1 || r1.m_y2 != r2.m_y2;
    }

private:
    int m_x1;
    int m_y1;
    int m_x2;
    int m_y2;
};

class RectF
{
public:
    constexpr RectF() noexcept : m_xp(0.0), m_yp(0.0), m_w(0.0), m_h(0.0) {}
    constexpr RectF(const PointF &topleft, const Size &size) noexcept
        : m_xp(topleft.x()), m_yp(topleft.y()), m_w(size.width()), m_h(size.height()) {}
    constexpr RectF(const PointF &topleft, const PointF &bottomRight) noexcept
        : m_xp(topleft.x()), m_yp(topleft.y()),
          m_w(bottomRight.x() - topleft.x()), m_h(bottomRight.y() - topleft.y()) {}
    constexpr RectF(double aleft, double atop, double awidth, double aheight) noexcept
        : m_xp(aleft), m_yp(atop), m_w(awidth), m_h(aheight) {}
    constexpr RectF(const Rect &r) noexcept
        : m_xp(r.x()), m_yp(r.y()), m_w(r.width()), m_h(r.height()) {}

    template <class F, std::enable_if_t<BridgesToCore<F, RectF>::value, int> = 0>
    constexpr RectF(const F &f) : RectF(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, RectF>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr bool isNull() const noexcept { return m_w == 0.0 && m_h == 0.0; }
    constexpr bool isEmpty() const noexcept { return m_w <= 0.0 || m_h <= 0.0; }
    constexpr bool isValid() const noexcept { return m_w > 0.0 && m_h > 0.0; }

    constexpr RectF normalized() const noexcept
    {
        RectF r = *this;
        if (r.m_w < 0.0) {
            r.m_xp += r.m_w;
            r.m_w = -r.m_w;
        }
        if (r.m_h < 0.0) {
            r.m_yp += r.m_h;
            r.m_h = -r.m_h;
        }
        return r;
    }

    constexpr double left() const noexcept { return m_xp; }
    constexpr double top() const noexcept { return m_yp; }
    constexpr double right() const noexcept { return m_xp + m_w; }
    constexpr double bottom() const noexcept { return m_yp + m_h; }

    constexpr double x() const noexcept { return m_xp; }
    constexpr double y() const noexcept { return m_yp; }
    constexpr void setLeft(double pos) noexcept { double diff = pos - m_xp; m_xp += diff; m_w -= diff; }
    constexpr void setRight(double pos) noexcept { m_w = pos - m_xp; }
    constexpr void setTop(double pos) noexcept { double diff = pos - m_yp; m_yp += diff; m_h -= diff; }
    constexpr void setBottom(double pos) noexcept { m_h = pos - m_yp; }
    constexpr void setX(double pos) noexcept { setLeft(pos); }
    constexpr void setY(double pos) noexcept { setTop(pos); }

    constexpr void setTopLeft(const PointF &p) noexcept { setLeft(p.x()); setTop(p.y()); }
    constexpr void setBottomRight(const PointF &p) noexcept { setRight(p.x()); setBottom(p.y()); }
    constexpr void setTopRight(const PointF &p) noexcept { setRight(p.x()); setTop(p.y()); }
    constexpr void setBottomLeft(const PointF &p) noexcept { setLeft(p.x()); setBottom(p.y()); }

    constexpr PointF topLeft() const noexcept { return PointF(m_xp, m_yp); }
    constexpr PointF bottomRight() const noexcept { return PointF(m_xp + m_w, m_yp + m_h); }
    constexpr PointF topRight() const noexcept { return PointF(m_xp + m_w, m_yp); }
    constexpr PointF bottomLeft() const noexcept { return PointF(m_xp, m_yp + m_h); }
    constexpr PointF center() const noexcept { return PointF(m_xp + m_w / 2.0, m_yp + m_h / 2.0); }

    constexpr void moveLeft(double pos) noexcept { m_xp = pos; }
    constexpr void moveTop(double pos) noexcept { m_yp = pos; }
    constexpr void moveRight(double pos) noexcept { m_xp = pos - m_w; }
    constexpr void moveBottom(double pos) noexcept { m_yp = pos - m_h; }
    constexpr void moveTopLeft(const PointF &p) noexcept { moveLeft(p.x()); moveTop(p.y()); }
    constexpr void moveBottomRight(const PointF &p) noexcept { moveRight(p.x()); moveBottom(p.y()); }
    constexpr void moveTopRight(const PointF &p) noexcept { moveRight(p.x()); moveTop(p.y()); }
    constexpr void moveBottomLeft(const PointF &p) noexcept { moveLeft(p.x()); moveBottom(p.y()); }
    constexpr void moveCenter(const PointF &p) noexcept { m_xp = p.x() - m_w / 2.0; m_yp = p.y() - m_h / 2.0; }

    constexpr void translate(double dx, double dy) noexcept
    {
        m_xp += dx;
        m_yp += dy;
    }

    constexpr void translate(const PointF &p) noexcept
    {
        m_xp += p.x();
        m_yp += p.y();
    }

    constexpr RectF translated(double dx, double dy) const noexcept
    {
        return RectF(m_xp + dx, m_yp + dy, m_w, m_h);
    }

    constexpr RectF translated(const PointF &p) const noexcept
    {
        return RectF(m_xp + p.x(), m_yp + p.y(), m_w, m_h);
    }

    constexpr RectF transposed() const noexcept
    {
        return RectF(m_xp, m_yp, m_h, m_w);
    }

    constexpr void moveTo(double ax, double ay) noexcept
    {
        m_xp = ax;
        m_yp = ay;
    }

    constexpr void moveTo(const PointF &p) noexcept
    {
        m_xp = p.x();
        m_yp = p.y();
    }

    constexpr void getRect(double *ax, double *ay, double *aaw, double *aah) const noexcept
    {
        *ax = m_xp;
        *ay = m_yp;
        *aaw = m_w;
        *aah = m_h;
    }

    constexpr void setRect(double ax, double ay, double aaw, double aah) noexcept
    {
        m_xp = ax;
        m_yp = ay;
        m_w = aaw;
        m_h = aah;
    }

    constexpr void getCoords(double *xp1, double *yp1, double *xp2, double *yp2) const noexcept
    {
        *xp1 = m_xp;
        *yp1 = m_yp;
        *xp2 = m_xp + m_w;
        *yp2 = m_yp + m_h;
    }

    constexpr void setCoords(double xp1, double yp1, double xp2, double yp2) noexcept
    {
        m_xp = xp1;
        m_yp = yp1;
        m_w = xp2 - xp1;
        m_h = yp2 - yp1;
    }

    constexpr void adjust(double xp1, double yp1, double xp2, double yp2) noexcept
    {
        m_xp += xp1;
        m_yp += yp1;
        m_w += xp2 - xp1;
        m_h += yp2 - yp1;
    }

    constexpr RectF adjusted(double xp1, double yp1, double xp2, double yp2) const noexcept
    {
        return RectF(m_xp + xp1, m_yp + yp1, m_w + xp2 - xp1, m_h + yp2 - yp1);
    }

    constexpr double width() const noexcept { return m_w; }
    constexpr double height() const noexcept { return m_h; }
    constexpr void setWidth(double aaw) noexcept { m_w = aaw; }
    constexpr void setHeight(double aah) noexcept { m_h = aah; }

    constexpr bool contains(const PointF &p) const noexcept
    {
        return contains(p.x(), p.y());
    }

    constexpr bool contains(double ax, double ay) const noexcept
    {
        double l = m_xp;
        double r = m_xp;
        if (m_w < 0.0)
            l += m_w;
        else
            r += m_w;
        if (l == r)
            return false;

        if (ax < l || ax > r)
            return false;

        double t = m_yp;
        double b = m_yp;
        if (m_h < 0.0)
            t += m_h;
        else
            b += m_h;
        if (t == b)
            return false;

        if (ay < t || ay > b)
            return false;

        return true;
    }

    constexpr bool contains(const RectF &r) const noexcept
    {
        double l1 = m_xp;
        double r1 = m_xp;
        if (m_w < 0.0)
            l1 += m_w;
        else
            r1 += m_w;
        if (l1 == r1)
            return false;

        double l2 = r.m_xp;
        double r2 = r.m_xp;
        if (r.m_w < 0.0)
            l2 += r.m_w;
        else
            r2 += r.m_w;
        if (l2 == r2)
            return false;

        if (l2 < l1 || r2 > r1)
            return false;

        double t1 = m_yp;
        double b1 = m_yp;
        if (m_h < 0.0)
            t1 += m_h;
        else
            b1 += m_h;
        if (t1 == b1)
            return false;

        double t2 = r.m_yp;
        double b2 = r.m_yp;
        if (r.m_h < 0.0)
            t2 += r.m_h;
        else
            b2 += r.m_h;
        if (t2 == b2)
            return false;

        if (t2 < t1 || b2 > b1)
            return false;

        return true;
    }

    constexpr RectF operator|(const RectF &r) const noexcept
    {
        if (isNull())
            return r;
        if (r.isNull())
            return *this;

        double left = m_xp;
        double right = m_xp;
        if (m_w < 0.0)
            left += m_w;
        else
            right += m_w;

        if (r.m_w < 0.0) {
            left = std::min(left, r.m_xp + r.m_w);
            right = std::max(right, r.m_xp);
        } else {
            left = std::min(left, r.m_xp);
            right = std::max(right, r.m_xp + r.m_w);
        }

        double top = m_yp;
        double bottom = m_yp;
        if (m_h < 0.0)
            top += m_h;
        else
            bottom += m_h;

        if (r.m_h < 0.0) {
            top = std::min(top, r.m_yp + r.m_h);
            bottom = std::max(bottom, r.m_yp);
        } else {
            top = std::min(top, r.m_yp);
            bottom = std::max(bottom, r.m_yp + r.m_h);
        }

        return RectF(left, top, right - left, bottom - top);
    }

    constexpr RectF operator&(const RectF &r) const noexcept
    {
        double l1 = m_xp;
        double r1 = m_xp;
        if (m_w < 0.0)
            l1 += m_w;
        else
            r1 += m_w;
        if (l1 == r1)
            return RectF();

        double l2 = r.m_xp;
        double r2 = r.m_xp;
        if (r.m_w < 0.0)
            l2 += r.m_w;
        else
            r2 += r.m_w;
        if (l2 == r2)
            return RectF();

        if (l1 >= r2 || l2 >= r1)
            return RectF();

        double t1 = m_yp;
        double b1 = m_yp;
        if (m_h < 0.0)
            t1 += m_h;
        else
            b1 += m_h;
        if (t1 == b1)
            return RectF();

        double t2 = r.m_yp;
        double b2 = r.m_yp;
        if (r.m_h < 0.0)
            t2 += r.m_h;
        else
            b2 += r.m_h;
        if (t2 == b2)
            return RectF();

        if (t1 >= b2 || t2 >= b1)
            return RectF();

        RectF tmp;
        tmp.m_xp = std::max(l1, l2);
        tmp.m_yp = std::max(t1, t2);
        tmp.m_w = std::min(r1, r2) - tmp.m_xp;
        tmp.m_h = std::min(b1, b2) - tmp.m_yp;
        return tmp;
    }

    constexpr RectF &operator|=(const RectF &r) noexcept
    {
        *this = *this | r;
        return *this;
    }

    constexpr RectF &operator&=(const RectF &r) noexcept
    {
        *this = *this & r;
        return *this;
    }

    constexpr RectF united(const RectF &r) const noexcept
    {
        return *this | r;
    }

    constexpr RectF intersected(const RectF &r) const noexcept
    {
        return *this & r;
    }

    constexpr bool intersects(const RectF &r) const noexcept
    {
        double l1 = m_xp;
        double r1 = m_xp;
        if (m_w < 0.0)
            l1 += m_w;
        else
            r1 += m_w;
        if (l1 == r1)
            return false;

        double l2 = r.m_xp;
        double r2 = r.m_xp;
        if (r.m_w < 0.0)
            l2 += r.m_w;
        else
            r2 += r.m_w;
        if (l2 == r2)
            return false;

        if (l1 >= r2 || l2 >= r1)
            return false;

        double t1 = m_yp;
        double b1 = m_yp;
        if (m_h < 0.0)
            t1 += m_h;
        else
            b1 += m_h;
        if (t1 == b1)
            return false;

        double t2 = r.m_yp;
        double b2 = r.m_yp;
        if (r.m_h < 0.0)
            t2 += r.m_h;
        else
            b2 += r.m_h;
        if (t2 == b2)
            return false;

        if (t1 >= b2 || t2 >= b1)
            return false;

        return true;
    }

    Rect toRect() const noexcept
    {
        const int x = round(m_xp);
        const int y = round(m_yp);
        const int w = round(m_w + (m_xp - x) / 2);
        const int h = round(m_h + (m_yp - y) / 2);
        return Rect(x, y, w, h);
    }

    Rect toAlignedRect() const noexcept
    {
        const int xmin = static_cast<int>(std::floor(m_xp));
        const int xmax = static_cast<int>(std::ceil(m_xp + m_w));
        const int ymin = static_cast<int>(std::floor(m_yp));
        const int ymax = static_cast<int>(std::ceil(m_yp + m_h));
        return Rect(xmin, ymin, xmax - xmin, ymax - ymin);
    }

    friend constexpr bool fuzzyCompare(const RectF &r1, const RectF &r2) noexcept
    {
        return detail::fuzzyCompareElement(r1.m_xp, r2.m_xp)
            && detail::fuzzyCompareElement(r1.m_yp, r2.m_yp)
            && detail::fuzzyCompareElement(r1.m_w, r2.m_w)
            && detail::fuzzyCompareElement(r1.m_h, r2.m_h);
    }

    friend constexpr bool operator==(const RectF &r1, const RectF &r2) noexcept
    {
        return fuzzyCompare(r1, r2);
    }

    friend constexpr bool operator!=(const RectF &r1, const RectF &r2) noexcept
    {
        return !fuzzyCompare(r1, r2);
    }

private:
    double m_xp;
    double m_yp;
    double m_w;
    double m_h;
};

class Transform
{
public:
    enum TransformationType {
        TxNone      = 0x00,
        TxTranslate = 0x01,
        TxScale     = 0x02,
        TxRotate    = 0x04,
        TxShear     = 0x08,
        TxProject   = 0x10
    };

    constexpr Transform() noexcept
        : m_matrix{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
          m_type(TxNone),
          m_dirty(TxNone)
    {}

    constexpr Transform(double h11, double h12, double h13,
                        double h21, double h22, double h23,
                        double h31, double h32, double h33 = 1.0) noexcept
        : m_matrix{{h11, h12, h13}, {h21, h22, h23}, {h31, h32, h33}},
          m_type(TxNone),
          m_dirty(TxProject)
    {}

    constexpr Transform(double h11, double h12, double h21,
                        double h22, double dx, double dy) noexcept
        : m_matrix{{h11, h12, 0.0}, {h21, h22, 0.0}, {dx, dy, 1.0}},
          m_type(TxNone),
          m_dirty(TxShear)
    {}

    template <class F, std::enable_if_t<BridgesToCore<F, Transform>::value, int> = 0>
    Transform(const F &f) : Transform(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Transform>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    bool isAffine() const noexcept { return inline_type() < TxProject; }
    bool isIdentity() const noexcept { return inline_type() == TxNone; }
    bool isInvertible() const noexcept { return !fuzzyIsNull(determinant()); }
    bool isScaling() const noexcept { return type() >= TxScale; }
    bool isRotating() const noexcept
    {
        TransformationType t = type();
        return t == TxRotate || t >= TxShear;
    }
    bool isTranslating() const noexcept { return inline_type() >= TxTranslate; }

    TransformationType type() const noexcept
    {
        if (m_dirty == TxNone)
            return static_cast<TransformationType>(m_type);

        switch (static_cast<TransformationType>(m_dirty)) {
        case TxProject:
            if (!fuzzyIsNull(m_matrix[0][2]) || !fuzzyIsNull(m_matrix[1][2]) || !fuzzyIsNull(m_matrix[2][2] - 1.0)) {
                m_type = TxProject;
                break;
            }
            [[fallthrough]];
        case TxShear:
        case TxRotate:
            if (!fuzzyIsNull(m_matrix[0][1]) || !fuzzyIsNull(m_matrix[1][0])) {
                const double dot = m_matrix[0][0] * m_matrix[1][0] + m_matrix[0][1] * m_matrix[1][1];
                if (fuzzyIsNull(dot))
                    m_type = TxRotate;
                else
                    m_type = TxShear;
                break;
            }
            [[fallthrough]];
        case TxScale:
            if (!fuzzyIsNull(m_matrix[0][0] - 1.0) || !fuzzyIsNull(m_matrix[1][1] - 1.0)) {
                m_type = TxScale;
                break;
            }
            [[fallthrough]];
        case TxTranslate:
            if (!fuzzyIsNull(m_matrix[2][0]) || !fuzzyIsNull(m_matrix[2][1])) {
                m_type = TxTranslate;
                break;
            }
            [[fallthrough]];
        case TxNone:
            m_type = TxNone;
            break;
        }

        m_dirty = TxNone;
        return static_cast<TransformationType>(m_type);
    }

    constexpr double determinant() const noexcept
    {
        return m_matrix[0][0] * (m_matrix[2][2] * m_matrix[1][1] - m_matrix[2][1] * m_matrix[1][2]) -
               m_matrix[1][0] * (m_matrix[2][2] * m_matrix[0][1] - m_matrix[2][1] * m_matrix[0][2]) +
               m_matrix[2][0] * (m_matrix[1][2] * m_matrix[0][1] - m_matrix[1][1] * m_matrix[0][2]);
    }

    constexpr double det() const noexcept { return determinant(); }

    constexpr double m11() const noexcept { return m_matrix[0][0]; }
    constexpr double m12() const noexcept { return m_matrix[0][1]; }
    constexpr double m13() const noexcept { return m_matrix[0][2]; }
    constexpr double m21() const noexcept { return m_matrix[1][0]; }
    constexpr double m22() const noexcept { return m_matrix[1][1]; }
    constexpr double m23() const noexcept { return m_matrix[1][2]; }
    constexpr double m31() const noexcept { return m_matrix[2][0]; }
    constexpr double m32() const noexcept { return m_matrix[2][1]; }
    constexpr double m33() const noexcept { return m_matrix[2][2]; }
    constexpr double dx() const noexcept { return m_matrix[2][0]; }
    constexpr double dy() const noexcept { return m_matrix[2][1]; }

    void setMatrix(double m11, double m12, double m13,
                   double m21, double m22, double m23,
                   double m31, double m32, double m33) noexcept
    {
        m_matrix[0][0] = m11; m_matrix[0][1] = m12; m_matrix[0][2] = m13;
        m_matrix[1][0] = m21; m_matrix[1][1] = m22; m_matrix[1][2] = m23;
        m_matrix[2][0] = m31; m_matrix[2][1] = m32; m_matrix[2][2] = m33;
        m_type = TxNone;
        m_dirty = TxProject;
    }

    void reset() noexcept
    {
        m_matrix[0][0] = 1.0; m_matrix[0][1] = 0.0; m_matrix[0][2] = 0.0;
        m_matrix[1][0] = 0.0; m_matrix[1][1] = 1.0; m_matrix[1][2] = 0.0;
        m_matrix[2][0] = 0.0; m_matrix[2][1] = 0.0; m_matrix[2][2] = 1.0;
        m_type = TxNone;
        m_dirty = TxNone;
    }

    Transform &translate(double dx, double dy) noexcept
    {
        if (dx == 0.0 && dy == 0.0)
            return *this;

        switch (inline_type()) {
        case TxNone:
            m_matrix[2][0] = dx;
            m_matrix[2][1] = dy;
            break;
        case TxTranslate:
            m_matrix[2][0] += dx;
            m_matrix[2][1] += dy;
            break;
        case TxScale:
            m_matrix[2][0] += dx * m_matrix[0][0];
            m_matrix[2][1] += dy * m_matrix[1][1];
            break;
        case TxProject:
            m_matrix[2][2] += dx * m_matrix[0][2] + dy * m_matrix[1][2];
            [[fallthrough]];
        case TxShear:
        case TxRotate:
            m_matrix[2][0] += dx * m_matrix[0][0] + dy * m_matrix[1][0];
            m_matrix[2][1] += dy * m_matrix[1][1] + dx * m_matrix[0][1];
            break;
        }
        markDirty(TxTranslate);
        return *this;
    }

    Transform &scale(double sx, double sy) noexcept
    {
        if (sx == 1.0 && sy == 1.0)
            return *this;

        switch (inline_type()) {
        case TxNone:
        case TxTranslate:
            m_matrix[0][0] = sx;
            m_matrix[1][1] = sy;
            break;
        case TxProject:
            m_matrix[0][2] *= sx;
            m_matrix[1][2] *= sy;
            [[fallthrough]];
        case TxRotate:
        case TxShear:
            m_matrix[0][1] *= sx;
            m_matrix[1][0] *= sy;
            [[fallthrough]];
        case TxScale:
            m_matrix[0][0] *= sx;
            m_matrix[1][1] *= sy;
            break;
        }
        markDirty(TxScale);
        return *this;
    }

    Transform &shear(double sh, double sv) noexcept
    {
        if (sh == 0.0 && sv == 0.0)
            return *this;

        switch (inline_type()) {
        case TxNone:
        case TxTranslate:
            m_matrix[0][1] = sv;
            m_matrix[1][0] = sh;
            break;
        case TxScale:
            m_matrix[0][1] = sv * m_matrix[1][1];
            m_matrix[1][0] = sh * m_matrix[0][0];
            break;
        case TxProject: {
            double tm13 = sv * m_matrix[1][2];
            double tm23 = sh * m_matrix[0][2];
            m_matrix[0][2] += tm13;
            m_matrix[1][2] += tm23;
            [[fallthrough]];
        }
        case TxRotate:
        case TxShear: {
            double tm11 = sv * m_matrix[1][0];
            double tm22 = sh * m_matrix[0][1];
            double tm12 = sv * m_matrix[1][1];
            double tm21 = sh * m_matrix[0][0];
            m_matrix[0][0] += tm11;
            m_matrix[0][1] += tm12;
            m_matrix[1][0] += tm21;
            m_matrix[1][1] += tm22;
            break;
        }
        }
        markDirty(TxShear);
        return *this;
    }

    Transform &rotate(double a) noexcept
    {
        if (a == 0.0)
            return *this;

        double sina = 0.0;
        double cosa = 0.0;
        if (a == 90.0 || a == -270.0) {
            sina = 1.0;
        } else if (a == 270.0 || a == -90.0) {
            sina = -1.0;
        } else if (a == 180.0) {
            cosa = -1.0;
        } else {
            constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
            const double b = kDeg2Rad * a;
            sina = std::sin(b);
            cosa = std::cos(b);
        }

        switch (inline_type()) {
        case TxNone:
        case TxTranslate:
            m_matrix[0][0] = cosa;
            m_matrix[0][1] = sina;
            m_matrix[1][0] = -sina;
            m_matrix[1][1] = cosa;
            break;
        case TxScale: {
            double tm11 = cosa * m_matrix[0][0];
            double tm12 = sina * m_matrix[1][1];
            double tm21 = -sina * m_matrix[0][0];
            double tm22 = cosa * m_matrix[1][1];
            m_matrix[0][0] = tm11;
            m_matrix[0][1] = tm12;
            m_matrix[1][0] = tm21;
            m_matrix[1][1] = tm22;
            break;
        }
        case TxProject: {
            double tm13 = cosa * m_matrix[0][2] + sina * m_matrix[1][2];
            double tm23 = -sina * m_matrix[0][2] + cosa * m_matrix[1][2];
            m_matrix[0][2] = tm13;
            m_matrix[1][2] = tm23;
            [[fallthrough]];
        }
        case TxRotate:
        case TxShear: {
            double tm11 = cosa * m_matrix[0][0] + sina * m_matrix[1][0];
            double tm12 = cosa * m_matrix[0][1] + sina * m_matrix[1][1];
            double tm21 = -sina * m_matrix[0][0] + cosa * m_matrix[1][0];
            double tm22 = -sina * m_matrix[0][1] + cosa * m_matrix[1][1];
            m_matrix[0][0] = tm11;
            m_matrix[0][1] = tm12;
            m_matrix[1][0] = tm21;
            m_matrix[1][1] = tm22;
            break;
        }
        }
        markDirty(TxShear);
        return *this;
    }

    Transform &rotateRadians(double a) noexcept
    {
        if (a == 0.0)
            return *this;
        const double sina = std::sin(a);
        const double cosa = std::cos(a);
        switch (inline_type()) {
        case TxNone:
        case TxTranslate:
            m_matrix[0][0] = cosa;
            m_matrix[0][1] = sina;
            m_matrix[1][0] = -sina;
            m_matrix[1][1] = cosa;
            break;
        case TxScale: {
            double tm11 = cosa * m_matrix[0][0];
            double tm12 = sina * m_matrix[1][1];
            double tm21 = -sina * m_matrix[0][0];
            double tm22 = cosa * m_matrix[1][1];
            m_matrix[0][0] = tm11;
            m_matrix[0][1] = tm12;
            m_matrix[1][0] = tm21;
            m_matrix[1][1] = tm22;
            break;
        }
        case TxProject: {
            double tm13 = cosa * m_matrix[0][2] + sina * m_matrix[1][2];
            double tm23 = -sina * m_matrix[0][2] + cosa * m_matrix[1][2];
            m_matrix[0][2] = tm13;
            m_matrix[1][2] = tm23;
            [[fallthrough]];
        }
        case TxRotate:
        case TxShear: {
            double tm11 = cosa * m_matrix[0][0] + sina * m_matrix[1][0];
            double tm12 = cosa * m_matrix[0][1] + sina * m_matrix[1][1];
            double tm21 = -sina * m_matrix[0][0] + cosa * m_matrix[1][0];
            double tm22 = -sina * m_matrix[0][1] + cosa * m_matrix[1][1];
            m_matrix[0][0] = tm11;
            m_matrix[0][1] = tm12;
            m_matrix[1][0] = tm21;
            m_matrix[1][1] = tm22;
            break;
        }
        }
        markDirty(TxShear);
        return *this;
    }

    static Transform fromTranslate(double dx, double dy) noexcept
    {
        Transform transform(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, dx, dy, 1.0);
        if (dx == 0.0 && dy == 0.0)
            transform.m_type = TxNone;
        else
            transform.m_type = TxTranslate;
        transform.m_dirty = TxNone;
        return transform;
    }

    static Transform fromScale(double sx, double sy) noexcept
    {
        Transform transform(sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 1.0);
        if (sx == 1.0 && sy == 1.0)
            transform.m_type = TxNone;
        else
            transform.m_type = TxScale;
        transform.m_dirty = TxNone;
        return transform;
    }

    Transform adjoint() const noexcept
    {
        double h11 = m_matrix[1][1] * m_matrix[2][2] - m_matrix[1][2] * m_matrix[2][1];
        double h21 = m_matrix[1][2] * m_matrix[2][0] - m_matrix[1][0] * m_matrix[2][2];
        double h31 = m_matrix[1][0] * m_matrix[2][1] - m_matrix[1][1] * m_matrix[2][0];
        double h12 = m_matrix[0][2] * m_matrix[2][1] - m_matrix[0][1] * m_matrix[2][2];
        double h22 = m_matrix[0][0] * m_matrix[2][2] - m_matrix[0][2] * m_matrix[2][0];
        double h32 = m_matrix[0][1] * m_matrix[2][0] - m_matrix[0][0] * m_matrix[2][1];
        double h13 = m_matrix[0][1] * m_matrix[1][2] - m_matrix[0][2] * m_matrix[1][1];
        double h23 = m_matrix[0][2] * m_matrix[1][0] - m_matrix[0][0] * m_matrix[1][2];
        double h33 = m_matrix[0][0] * m_matrix[1][1] - m_matrix[0][1] * m_matrix[1][0];

        return Transform(h11, h12, h13,
                         h21, h22, h23,
                         h31, h32, h33);
    }

    Transform transposed() const noexcept
    {
        Transform t(m_matrix[0][0], m_matrix[1][0], m_matrix[2][0],
                    m_matrix[0][1], m_matrix[1][1], m_matrix[2][1],
                    m_matrix[0][2], m_matrix[1][2], m_matrix[2][2]);
        t.m_type = m_type;
        t.m_dirty = m_dirty;
        return t;
    }

    Transform inverted(bool *invertible = nullptr) const noexcept
    {
        Transform invert;
        bool inv = true;

        switch (inline_type()) {
        case TxNone:
            break;
        case TxTranslate:
            invert.m_matrix[2][0] = -m_matrix[2][0];
            invert.m_matrix[2][1] = -m_matrix[2][1];
            break;
        case TxScale:
            inv = !fuzzyIsNull(m_matrix[0][0]);
            inv &= !fuzzyIsNull(m_matrix[1][1]);
            if (inv) {
                invert.m_matrix[0][0] = 1.0 / m_matrix[0][0];
                invert.m_matrix[1][1] = 1.0 / m_matrix[1][1];
                invert.m_matrix[2][0] = -m_matrix[2][0] * invert.m_matrix[0][0];
                invert.m_matrix[2][1] = -m_matrix[2][1] * invert.m_matrix[1][1];
            }
            break;
        default: {
            double d = determinant();
            inv = !fuzzyIsNull(d);
            if (inv)
                invert = (adjoint() /= d);
            break;
        }
        }

        if (invertible)
            *invertible = inv;

        if (inv) {
            invert.m_type = m_type;
            invert.m_dirty = m_dirty;
        }
        return invert;
    }

    Transform &operator*=(const Transform &o) noexcept
    {
        const TransformationType otherType = o.inline_type();
        if (otherType == TxNone)
            return *this;

        const TransformationType thisType = inline_type();
        if (thisType == TxNone) {
            *this = o;
            return *this;
        }

        TransformationType t = std::max(thisType, otherType);
        switch (t) {
        case TxNone:
            break;
        case TxTranslate:
            m_matrix[2][0] += o.m_matrix[2][0];
            m_matrix[2][1] += o.m_matrix[2][1];
            break;
        case TxScale: {
            double m11 = m_matrix[0][0] * o.m_matrix[0][0];
            double m22 = m_matrix[1][1] * o.m_matrix[1][1];
            double m31 = m_matrix[2][0] * o.m_matrix[0][0] + o.m_matrix[2][0];
            double m32 = m_matrix[2][1] * o.m_matrix[1][1] + o.m_matrix[2][1];
            m_matrix[0][0] = m11;
            m_matrix[1][1] = m22;
            m_matrix[2][0] = m31;
            m_matrix[2][1] = m32;
            break;
        }
        case TxRotate:
        case TxShear: {
            double m11 = m_matrix[0][0] * o.m_matrix[0][0] + m_matrix[0][1] * o.m_matrix[1][0];
            double m12 = m_matrix[0][0] * o.m_matrix[0][1] + m_matrix[0][1] * o.m_matrix[1][1];
            double m21 = m_matrix[1][0] * o.m_matrix[0][0] + m_matrix[1][1] * o.m_matrix[1][0];
            double m22 = m_matrix[1][0] * o.m_matrix[0][1] + m_matrix[1][1] * o.m_matrix[1][1];
            double m31 = m_matrix[2][0] * o.m_matrix[0][0] + m_matrix[2][1] * o.m_matrix[1][0] + o.m_matrix[2][0];
            double m32 = m_matrix[2][0] * o.m_matrix[0][1] + m_matrix[2][1] * o.m_matrix[1][1] + o.m_matrix[2][1];
            m_matrix[0][0] = m11; m_matrix[0][1] = m12;
            m_matrix[1][0] = m21; m_matrix[1][1] = m22;
            m_matrix[2][0] = m31; m_matrix[2][1] = m32;
            break;
        }
        case TxProject: {
            double m11 = m_matrix[0][0] * o.m_matrix[0][0] + m_matrix[0][1] * o.m_matrix[1][0] + m_matrix[0][2] * o.m_matrix[2][0];
            double m12 = m_matrix[0][0] * o.m_matrix[0][1] + m_matrix[0][1] * o.m_matrix[1][1] + m_matrix[0][2] * o.m_matrix[2][1];
            double m13 = m_matrix[0][0] * o.m_matrix[0][2] + m_matrix[0][1] * o.m_matrix[1][2] + m_matrix[0][2] * o.m_matrix[2][2];

            double m21 = m_matrix[1][0] * o.m_matrix[0][0] + m_matrix[1][1] * o.m_matrix[1][0] + m_matrix[1][2] * o.m_matrix[2][0];
            double m22 = m_matrix[1][0] * o.m_matrix[0][1] + m_matrix[1][1] * o.m_matrix[1][1] + m_matrix[1][2] * o.m_matrix[2][1];
            double m23 = m_matrix[1][0] * o.m_matrix[0][2] + m_matrix[1][1] * o.m_matrix[1][2] + m_matrix[1][2] * o.m_matrix[2][2];

            double m31 = m_matrix[2][0] * o.m_matrix[0][0] + m_matrix[2][1] * o.m_matrix[1][0] + m_matrix[2][2] * o.m_matrix[2][0];
            double m32 = m_matrix[2][0] * o.m_matrix[0][1] + m_matrix[2][1] * o.m_matrix[1][1] + m_matrix[2][2] * o.m_matrix[2][1];
            double m33 = m_matrix[2][0] * o.m_matrix[0][2] + m_matrix[2][1] * o.m_matrix[1][2] + m_matrix[2][2] * o.m_matrix[2][2];

            m_matrix[0][0] = m11; m_matrix[0][1] = m12; m_matrix[0][2] = m13;
            m_matrix[1][0] = m21; m_matrix[1][1] = m22; m_matrix[1][2] = m23;
            m_matrix[2][0] = m31; m_matrix[2][1] = m32; m_matrix[2][2] = m33;
            break;
        }
        }

        m_dirty = t;
        return *this;
    }

    Transform operator*(const Transform &m) const noexcept
    {
        const TransformationType otherType = m.inline_type();
        if (otherType == TxNone)
            return *this;

        const TransformationType thisType = inline_type();
        if (thisType == TxNone)
            return m;

        Transform t;
        TransformationType type = std::max(thisType, otherType);
        switch (type) {
        case TxNone:
            break;
        case TxTranslate:
            t.m_matrix[2][0] = m_matrix[2][0] + m.m_matrix[2][0];
            t.m_matrix[2][1] = m_matrix[2][1] + m.m_matrix[2][1];
            break;
        case TxScale: {
            double m11 = m_matrix[0][0] * m.m_matrix[0][0];
            double m22 = m_matrix[1][1] * m.m_matrix[1][1];
            double m31 = m_matrix[2][0] * m.m_matrix[0][0] + m.m_matrix[2][0];
            double m32 = m_matrix[2][1] * m.m_matrix[1][1] + m.m_matrix[2][1];
            t.m_matrix[0][0] = m11;
            t.m_matrix[1][1] = m22;
            t.m_matrix[2][0] = m31;
            t.m_matrix[2][1] = m32;
            break;
        }
        case TxRotate:
        case TxShear: {
            double m11 = m_matrix[0][0] * m.m_matrix[0][0] + m_matrix[0][1] * m.m_matrix[1][0];
            double m12 = m_matrix[0][0] * m.m_matrix[0][1] + m_matrix[0][1] * m.m_matrix[1][1];
            double m21 = m_matrix[1][0] * m.m_matrix[0][0] + m_matrix[1][1] * m.m_matrix[1][0];
            double m22 = m_matrix[1][0] * m.m_matrix[0][1] + m_matrix[1][1] * m.m_matrix[1][1];
            double m31 = m_matrix[2][0] * m.m_matrix[0][0] + m_matrix[2][1] * m.m_matrix[1][0] + m.m_matrix[2][0];
            double m32 = m_matrix[2][0] * m.m_matrix[0][1] + m_matrix[2][1] * m.m_matrix[1][1] + m.m_matrix[2][1];
            t.m_matrix[0][0] = m11; t.m_matrix[0][1] = m12;
            t.m_matrix[1][0] = m21; t.m_matrix[1][1] = m22;
            t.m_matrix[2][0] = m31; t.m_matrix[2][1] = m32;
            break;
        }
        case TxProject: {
            double m11 = m_matrix[0][0] * m.m_matrix[0][0] + m_matrix[0][1] * m.m_matrix[1][0] + m_matrix[0][2] * m.m_matrix[2][0];
            double m12 = m_matrix[0][0] * m.m_matrix[0][1] + m_matrix[0][1] * m.m_matrix[1][1] + m_matrix[0][2] * m.m_matrix[2][1];
            double m13 = m_matrix[0][0] * m.m_matrix[0][2] + m_matrix[0][1] * m.m_matrix[1][2] + m_matrix[0][2] * m.m_matrix[2][2];

            double m21 = m_matrix[1][0] * m.m_matrix[0][0] + m_matrix[1][1] * m.m_matrix[1][0] + m_matrix[1][2] * m.m_matrix[2][0];
            double m22 = m_matrix[1][0] * m.m_matrix[0][1] + m_matrix[1][1] * m.m_matrix[1][1] + m_matrix[1][2] * m.m_matrix[2][1];
            double m23 = m_matrix[1][0] * m.m_matrix[0][2] + m_matrix[1][1] * m.m_matrix[1][2] + m_matrix[1][2] * m.m_matrix[2][2];

            double m31 = m_matrix[2][0] * m.m_matrix[0][0] + m_matrix[2][1] * m.m_matrix[1][0] + m_matrix[2][2] * m.m_matrix[2][0];
            double m32 = m_matrix[2][0] * m.m_matrix[0][1] + m_matrix[2][1] * m.m_matrix[1][1] + m_matrix[2][2] * m.m_matrix[2][1];
            double m33 = m_matrix[2][0] * m.m_matrix[0][2] + m_matrix[2][1] * m.m_matrix[1][2] + m_matrix[2][2] * m.m_matrix[2][2];

            t.m_matrix[0][0] = m11; t.m_matrix[0][1] = m12; t.m_matrix[0][2] = m13;
            t.m_matrix[1][0] = m21; t.m_matrix[1][1] = m22; t.m_matrix[1][2] = m23;
            t.m_matrix[2][0] = m31; t.m_matrix[2][1] = m32; t.m_matrix[2][2] = m33;
            break;
        }
        }

        t.m_dirty = type;
        return t;
    }

    Transform &operator*=(double num) noexcept
    {
        if (num == 1.0)
            return *this;
        m_matrix[0][0] *= num; m_matrix[0][1] *= num; m_matrix[0][2] *= num;
        m_matrix[1][0] *= num; m_matrix[1][1] *= num; m_matrix[1][2] *= num;
        m_matrix[2][0] *= num; m_matrix[2][1] *= num; m_matrix[2][2] *= num;
        markDirty(TxScale);
        return *this;
    }

    Transform &operator/=(double div) noexcept
    {
        if (div == 0.0)
            return *this;
        return operator*=(1.0 / div);
    }

    Transform &operator+=(double num) noexcept
    {
        if (num == 0.0)
            return *this;
        m_matrix[0][0] += num; m_matrix[0][1] += num; m_matrix[0][2] += num;
        m_matrix[1][0] += num; m_matrix[1][1] += num; m_matrix[1][2] += num;
        m_matrix[2][0] += num; m_matrix[2][1] += num; m_matrix[2][2] += num;
        m_dirty = TxProject;
        return *this;
    }

    Transform &operator-=(double num) noexcept
    {
        if (num == 0.0)
            return *this;
        m_matrix[0][0] -= num; m_matrix[0][1] -= num; m_matrix[0][2] -= num;
        m_matrix[1][0] -= num; m_matrix[1][1] -= num; m_matrix[1][2] -= num;
        m_matrix[2][0] -= num; m_matrix[2][1] -= num; m_matrix[2][2] -= num;
        m_dirty = TxProject;
        return *this;
    }

    void map(double x, double y, double *tx, double *ty) const noexcept
    {
        TransformationType t = inline_type();
        double fx = x;
        double fy = y;
        switch (t) {
        case TxNone:
            *tx = fx;
            *ty = fy;
            break;
        case TxTranslate:
            *tx = fx + m_matrix[2][0];
            *ty = fy + m_matrix[2][1];
            break;
        case TxScale:
            *tx = m_matrix[0][0] * fx + m_matrix[2][0];
            *ty = m_matrix[1][1] * fy + m_matrix[2][1];
            break;
        case TxRotate:
        case TxShear:
        case TxProject: {
            double nx = m_matrix[0][0] * fx + m_matrix[1][0] * fy + m_matrix[2][0];
            double ny = m_matrix[0][1] * fx + m_matrix[1][1] * fy + m_matrix[2][1];
            if (t == TxProject) {
                double w = (m_matrix[0][2] * fx + m_matrix[1][2] * fy + m_matrix[2][2]);
                w = 1.0 / std::max(w, 0.000001);
                nx *= w;
                ny *= w;
            }
            *tx = nx;
            *ty = ny;
            break;
        }
        }
    }

    void map(int x, int y, int *tx, int *ty) const noexcept
    {
        double fx = 0.0;
        double fy = 0.0;
        map(double(x), double(y), &fx, &fy);
        *tx = round(fx);
        *ty = round(fy);
    }

    PointF map(const PointF &p) const noexcept
    {
        double fx = p.x();
        double fy = p.y();
        double x = 0.0;
        double y = 0.0;
        map(fx, fy, &x, &y);
        return PointF(x, y);
    }

    Point map(const Point &p) const noexcept
    {
        double fx = p.x();
        double fy = p.y();
        double x = 0.0;
        double y = 0.0;
        map(fx, fy, &x, &y);
        return Point(round(x), round(y));
    }

    LineF map(const LineF &l) const noexcept
    {
        return LineF(map(l.p1()), map(l.p2()));
    }

    RectF mapRect(const RectF &rect) const noexcept
    {
        TransformationType t = inline_type();
        if (t <= TxTranslate)
            return rect.translated(m_matrix[2][0], m_matrix[2][1]);

        if (t <= TxScale) {
            double x = m_matrix[0][0] * rect.x() + m_matrix[2][0];
            double y = m_matrix[1][1] * rect.y() + m_matrix[2][1];
            double w = m_matrix[0][0] * rect.width();
            double h = m_matrix[1][1] * rect.height();
            if (w < 0.0) {
                w = -w;
                x -= w;
            }
            if (h < 0.0) {
                h = -h;
                y -= h;
            }
            return RectF(x, y, w, h);
        }

        double x = 0.0, y = 0.0;
        map(rect.left(), rect.top(), &x, &y);
        double xmin = x;
        double ymin = y;
        double xmax = x;
        double ymax = y;
        map(rect.right(), rect.top(), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        map(rect.right(), rect.bottom(), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        map(rect.left(), rect.bottom(), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        return RectF(xmin, ymin, xmax - xmin, ymax - ymin);
    }

    Rect mapRect(const Rect &rect) const noexcept
    {
        TransformationType t = inline_type();
        if (t <= TxTranslate)
            return rect.translated(round(m_matrix[2][0]), round(m_matrix[2][1]));

        if (t <= TxScale) {
            int x = round(m_matrix[0][0] * rect.x() + m_matrix[2][0]);
            int y = round(m_matrix[1][1] * rect.y() + m_matrix[2][1]);
            int w = round(m_matrix[0][0] * rect.width());
            int h = round(m_matrix[1][1] * rect.height());
            if (w < 0) {
                w = -w;
                x -= w;
            }
            if (h < 0) {
                h = -h;
                y -= h;
            }
            return Rect(x, y, w, h);
        }

        double x = 0.0, y = 0.0;
        map(double(rect.left()), double(rect.top()), &x, &y);
        double xmin = x;
        double ymin = y;
        double xmax = x;
        double ymax = y;
        map(double(rect.left() + rect.width()), double(rect.top()), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        map(double(rect.left() + rect.width()), double(rect.top() + rect.height()), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        map(double(rect.left()), double(rect.top() + rect.height()), &x, &y);
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
        return RectF(xmin, ymin, xmax - xmin, ymax - ymin).toRect();
    }

    bool operator==(const Transform &o) const noexcept
    {
        return m_matrix[0][0] == o.m_matrix[0][0] &&
               m_matrix[0][1] == o.m_matrix[0][1] &&
               m_matrix[1][0] == o.m_matrix[1][0] &&
               m_matrix[1][1] == o.m_matrix[1][1] &&
               m_matrix[2][0] == o.m_matrix[2][0] &&
               m_matrix[2][1] == o.m_matrix[2][1] &&
               m_matrix[0][2] == o.m_matrix[0][2] &&
               m_matrix[1][2] == o.m_matrix[1][2] &&
               m_matrix[2][2] == o.m_matrix[2][2];
    }

    bool operator!=(const Transform &o) const noexcept
    {
        return !operator==(o);
    }

    friend bool fuzzyCompare(const Transform &t1, const Transform &t2) noexcept
    {
        return fuzzyCompare(t1.m11(), t2.m11()) &&
               fuzzyCompare(t1.m12(), t2.m12()) &&
               fuzzyCompare(t1.m13(), t2.m13()) &&
               fuzzyCompare(t1.m21(), t2.m21()) &&
               fuzzyCompare(t1.m22(), t2.m22()) &&
               fuzzyCompare(t1.m23(), t2.m23()) &&
               fuzzyCompare(t1.m31(), t2.m31()) &&
               fuzzyCompare(t1.m32(), t2.m32()) &&
               fuzzyCompare(t1.m33(), t2.m33());
    }

private:
    void markDirty(TransformationType opType) noexcept
    {
        if (m_dirty < m_type)
            m_dirty = m_type;
        if (m_dirty < static_cast<unsigned int>(opType))
            m_dirty = opType;
    }

    TransformationType inline_type() const noexcept
    {
        if (m_dirty == TxNone)
            return static_cast<TransformationType>(m_type);
        return type();
    }

    double m_matrix[3][3];
    mutable unsigned int m_type : 5;
    mutable unsigned int m_dirty : 5;
};

} // namespace AnimeCore

#endif // ANIMECORE_GEOMETRY_H
