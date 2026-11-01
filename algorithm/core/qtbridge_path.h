#ifndef ANIMECORE_QTBRIDGE_PATH_H
#define ANIMECORE_QTBRIDGE_PATH_H

#include "algorithm/core/qtbridge_geometry.h"
#include "path.h"

#include <QPainterPath>

namespace AnimeCore {

template <>
struct CoreOf<QPainterPath>
{
    using type = Path;
};

inline Path Bridge<QPainterPath>::toCore(const QPainterPath &qp)
{
    const int count = qp.elementCount();
    Path out;
    if (qp.fillRule() != Qt::OddEvenFill) {
        out.setFillRule(qp.fillRule() == Qt::WindingFill ? WindingFill : OddEvenFill);
    }
    if (count <= 0) {
        return out;
    }

    out.reserve(count);
    int i = 0;
    if (qp.elementAt(0).type == QPainterPath::MoveToElement) {
        const QPainterPath::Element e0 = qp.elementAt(0);
        if (out.elementCount() == 0) {
            Path started(PointF(e0.x, e0.y));
            started.setFillRule(qp.fillRule() == Qt::WindingFill ? WindingFill : OddEvenFill);
            started.reserve(count);
            out = std::move(started);
        } else {
            out.moveTo(e0.x, e0.y);
        }
        i = 1;
    }

    int lastMoveStart = 0;
    while (i < count) {
        const QPainterPath::Element e = qp.elementAt(i);
        if (e.type == QPainterPath::MoveToElement) {
            const int before = out.elementCount();
            out.moveTo(e.x, e.y);
            if (out.elementCount() == before && i > 0) {
                const Path::Element m0 = out.elementAt(lastMoveStart);
                const QPainterPath::Element prev = qp.elementAt(i - 1);
                out.setElementPositionAt(i - 1, m0.x, m0.y);
                out.closeSubpath();
                out.lineTo(m0.x, m0.y);
                out.setElementPositionAt(i - 1, prev.x, prev.y);
                out.setElementPositionAt(i, e.x, e.y);
            } else {
                lastMoveStart = i;
            }
            ++i;
        } else if (e.type == QPainterPath::LineToElement) {
            const int before = out.elementCount();
            out.lineTo(e.x, e.y);
            if (out.elementCount() == before) {
                out.lineTo(e.x + 1.0, e.y);
                out.setElementPositionAt(out.elementCount() - 1, e.x, e.y);
            }
            ++i;
        } else if (e.type == QPainterPath::CurveToElement && i + 2 < count) {
            const QPainterPath::Element e1 = qp.elementAt(i + 1);
            const QPainterPath::Element e2 = qp.elementAt(i + 2);
            const int before = out.elementCount();
            out.cubicTo(e.x, e.y, e1.x, e1.y, e2.x, e2.y);
            if (out.elementCount() == before) {
                out.cubicTo(e.x + 1.0, e.y, e1.x, e1.y, e2.x, e2.y);
                out.setElementPositionAt(out.elementCount() - 3, e.x, e.y);
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return out;
}

inline QPainterPath Bridge<QPainterPath>::fromCore(const Path &p)
{
    const int count = p.elementCount();
    const Qt::FillRule rule = p.fillRule() == WindingFill ? Qt::WindingFill : Qt::OddEvenFill;
    if (count <= 0) {
        QPainterPath out;
        if (rule != Qt::OddEvenFill) {
            out.setFillRule(rule);
        }
        return out;
    }

    int i = 0;
    QPainterPath out;
    if (p.elementAt(0).type == Path::MoveToElement) {
        const Path::Element e0 = p.elementAt(0);
        out = QPainterPath(QPointF(e0.x, e0.y));
        i = 1;
    }
    if (rule != Qt::OddEvenFill) {
        out.setFillRule(rule);
    }
    out.reserve(count);

    int lastMoveStart = 0;
    while (i < count) {
        const Path::Element e = p.elementAt(i);
        if (e.type == Path::MoveToElement) {
            const int before = out.elementCount();
            out.moveTo(e.x, e.y);
            if (out.elementCount() == before && i > 0) {
                const QPainterPath::Element m0 = out.elementAt(lastMoveStart);
                const Path::Element prev = p.elementAt(i - 1);
                out.setElementPositionAt(i - 1, m0.x, m0.y);
                out.closeSubpath();
                out.lineTo(m0.x, m0.y);
                out.setElementPositionAt(i - 1, prev.x, prev.y);
                out.setElementPositionAt(i, e.x, e.y);
            } else {
                lastMoveStart = i;
            }
            ++i;
        } else if (e.type == Path::LineToElement) {
            const int before = out.elementCount();
            out.lineTo(e.x, e.y);
            if (out.elementCount() == before) {
                out.lineTo(e.x + 1.0, e.y);
                out.setElementPositionAt(out.elementCount() - 1, e.x, e.y);
            }
            ++i;
        } else if (e.type == Path::CurveToElement && i + 2 < count) {
            const Path::Element e1 = p.elementAt(i + 1);
            const Path::Element e2 = p.elementAt(i + 2);
            const int before = out.elementCount();
            out.cubicTo(e.x, e.y, e1.x, e1.y, e2.x, e2.y);
            if (out.elementCount() == before) {
                out.cubicTo(e.x + 1.0, e.y, e1.x, e1.y, e2.x, e2.y);
                out.setElementPositionAt(out.elementCount() - 3, e.x, e.y);
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return out;
}

} // namespace AnimeCore

#endif // ANIMECORE_QTBRIDGE_PATH_H
