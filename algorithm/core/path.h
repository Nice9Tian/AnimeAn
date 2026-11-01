#ifndef ANIMECORE_PATH_H
#define ANIMECORE_PATH_H

#include <memory>
#include <type_traits>
#include <utility>

#include "bridge.h"
#include "containers.h"
#include "geometry.h"

namespace AnimeCore {

enum FillRule {
    OddEvenFill = 0,
    WindingFill = 1
};

class Path
{
public:
    using FillRule = AnimeCore::FillRule;
    static constexpr FillRule OddEvenFill = AnimeCore::OddEvenFill;
    static constexpr FillRule WindingFill = AnimeCore::WindingFill;

    enum ElementType {
        MoveToElement = 0,
        LineToElement = 1,
        CurveToElement = 2,
        CurveToDataElement = 3
    };

    class Element
    {
    public:
        double x = 0.0;
        double y = 0.0;
        ElementType type = MoveToElement;

        constexpr bool isMoveTo() const noexcept { return type == MoveToElement; }
        constexpr bool isLineTo() const noexcept { return type == LineToElement; }
        constexpr bool isCurveTo() const noexcept { return type == CurveToElement; }

        constexpr operator PointF() const noexcept { return PointF(x, y); }

        constexpr bool operator==(const Element &e) const noexcept
        {
            return fuzzyCompare(x, e.x) && fuzzyCompare(y, e.y) && type == e.type;
        }
        constexpr bool operator!=(const Element &e) const noexcept
        {
            return !operator==(e);
        }
    };

    Path() noexcept = default;
    explicit Path(const PointF &startPoint);
    Path(const Path &other) noexcept = default;
    Path(Path &&other) noexcept = default;
    Path &operator=(const Path &other) noexcept = default;
    Path &operator=(Path &&other) noexcept = default;
    ~Path() = default;

    template <class F, std::enable_if_t<BridgesToCore<F, Path>::value, int> = 0>
    Path(const F &f) : Path(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Path>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    void swap(Path &other) noexcept { m_d.swap(other.m_d); }

    void clear();
    void reserve(int size);
    int capacity() const;

    void closeSubpath();

    void moveTo(const PointF &p);
    void moveTo(double x, double y) { moveTo(PointF(x, y)); }

    void lineTo(const PointF &p);
    void lineTo(double x, double y) { lineTo(PointF(x, y)); }

    void cubicTo(const PointF &ctrlPt1, const PointF &ctrlPt2, const PointF &endPt);
    void cubicTo(double ctrlPt1x, double ctrlPt1y, double ctrlPt2x, double ctrlPt2y,
                 double endPtx, double endPty)
    {
        cubicTo(PointF(ctrlPt1x, ctrlPt1y), PointF(ctrlPt2x, ctrlPt2y), PointF(endPtx, endPty));
    }

    void quadTo(const PointF &ctrlPt, const PointF &endPt);
    void quadTo(double ctrlPtx, double ctrlPty, double endPtx, double endPty)
    {
        quadTo(PointF(ctrlPtx, ctrlPty), PointF(endPtx, endPty));
    }

    PointF currentPosition() const;

    void addRect(const RectF &rect);
    void addRect(double x, double y, double w, double h) { addRect(RectF(x, y, w, h)); }
    void addPolygon(const Vector<PointF> &polygon);
    void addPath(const Path &path);
    void connectPath(const Path &path);

    bool contains(const PointF &pt) const;

    void translate(double dx, double dy);
    void translate(const PointF &offset) { translate(offset.x(), offset.y()); }

    [[nodiscard]] Path translated(double dx, double dy) const;
    [[nodiscard]] Path translated(const PointF &offset) const
    {
        return translated(offset.x(), offset.y());
    }

    [[nodiscard]] Path transformed(const Transform &matrix) const;

    RectF boundingRect() const;
    RectF controlPointRect() const;

    FillRule fillRule() const;
    void setFillRule(FillRule fillRule);

    bool isEmpty() const;

    [[nodiscard]] Path toReversed() const;
    Vector<Vector<PointF>> toSubpathPolygons(const Transform &matrix = Transform()) const;

    int elementCount() const;
    Element elementAt(int i) const;
    void setElementPositionAt(int i, double x, double y);

    double length() const;
    double percentAtLength(double len) const;
    PointF pointAtPercent(double t) const;
    double angleAtPercent(double t) const;
    double slopeAtPercent(double t) const;

    bool operator==(const Path &other) const;
    bool operator!=(const Path &other) const { return !operator==(other); }

    bool isDetached() const noexcept { return !m_d || m_d.use_count() == 1; }
    bool isSharedWith(const Path &other) const noexcept { return m_d == other.m_d; }

private:
    struct Data;
    std::shared_ptr<Data> m_d;

    void ensureData();
    void detach();
    void maybeMoveTo();
};

inline Path operator*(const Path &p, const Transform &m)
{
    return p.transformed(m);
}

} // namespace AnimeCore

#endif // ANIMECORE_PATH_H
