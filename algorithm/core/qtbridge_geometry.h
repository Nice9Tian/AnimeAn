#ifndef ANIMECORE_QTBRIDGE_GEOMETRY_H
#define ANIMECORE_QTBRIDGE_GEOMETRY_H

#include "bridge.h"

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QPointF>
#include <QQueue>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QTransform>

namespace AnimeCore {

class PointF;
class Point;
class LineF;
class RectF;
class Rect;
class Size;
class Transform;
class Color;

template <>
struct Bridge<QPointF>
{
    using Core = PointF;

    static PointF toCore(const QPointF &p) noexcept;
    static QPointF fromCore(const PointF &p) noexcept;
};

template <>
struct Bridge<QPoint>
{
    using Core = Point;

    static Point toCore(const QPoint &p) noexcept;
    static QPoint fromCore(const Point &p) noexcept;
};

template <>
struct Bridge<QLineF>
{
    using Core = LineF;

    static LineF toCore(const QLineF &l) noexcept;
    static QLineF fromCore(const LineF &l) noexcept;
};

template <>
struct Bridge<QRectF>
{
    using Core = RectF;

    static RectF toCore(const QRectF &r) noexcept;
    static QRectF fromCore(const RectF &r) noexcept;
};

template <>
struct Bridge<QRect>
{
    using Core = Rect;

    static Rect toCore(const QRect &r) noexcept;
    static QRect fromCore(const Rect &r) noexcept;
};

template <>
struct Bridge<QSize>
{
    using Core = Size;

    static Size toCore(const QSize &s) noexcept;
    static QSize fromCore(const Size &s) noexcept;
};

template <>
struct Bridge<QTransform>
{
    using Core = Transform;

    static Transform toCore(const QTransform &t) noexcept;
    static QTransform fromCore(const Transform &t) noexcept;
};

template <>
struct Bridge<QColor>
{
    using Core = Color;

    static Color toCore(const QColor &c) noexcept;
    static QColor fromCore(const Color &c) noexcept;
};

} // namespace AnimeCore

#include "algorithm/core/qtbridge_qstring.h"
#include "color.h"
#include "containers.h"
#include "geometry.h"

namespace AnimeCore {

#ifndef ANIMECORE_HAS_COREOF
#define ANIMECORE_HAS_COREOF
template <class U>
struct CoreOf
{
    using type = U;
};
#endif

template <>
struct CoreOf<QPointF>
{
    using type = PointF;
};

template <>
struct CoreOf<QPoint>
{
    using type = Point;
};

template <>
struct CoreOf<QLineF>
{
    using type = LineF;
};

template <>
struct CoreOf<QRectF>
{
    using type = RectF;
};

template <>
struct CoreOf<QRect>
{
    using type = Rect;
};

template <>
struct CoreOf<QSize>
{
    using type = Size;
};

template <>
struct CoreOf<QTransform>
{
    using type = Transform;
};

template <>
struct CoreOf<QColor>
{
    using type = Color;
};

template <class U>
struct CoreOf<QList<U>>
{
    using type = Vector<typename CoreOf<U>::type>;
};

template <class K, class V>
struct CoreOf<QMap<K, V>>
{
    using type = Map<typename CoreOf<K>::type, typename CoreOf<V>::type>;
};

template <class K, class V>
struct CoreOf<QHash<K, V>>
{
    using type = Hash<typename CoreOf<K>::type, typename CoreOf<V>::type>;
};

template <class T>
struct CoreOf<QSet<T>>
{
    using type = Set<typename CoreOf<T>::type>;
};

template <class U>
struct CoreOf<QQueue<U>>
{
    using type = Queue<typename CoreOf<U>::type>;
};

namespace detail {

template <class U>
typename CoreOf<U>::type toCoreElement(const U &u)
{
    using CoreType = typename CoreOf<U>::type;
    if constexpr (std::is_same_v<U, CoreType>) {
        return u;
    } else {
        return Bridge<U>::toCore(u);
    }
}

template <class U>
U fromCoreElement(const typename CoreOf<U>::type &c)
{
    using CoreType = typename CoreOf<U>::type;
    if constexpr (std::is_same_v<U, CoreType>) {
        return c;
    } else {
        return Bridge<U>::fromCore(c);
    }
}

} // namespace detail

inline PointF Bridge<QPointF>::toCore(const QPointF &p) noexcept
{
    return PointF(p.x(), p.y());
}

inline QPointF Bridge<QPointF>::fromCore(const PointF &p) noexcept
{
    return QPointF(p.x(), p.y());
}

inline Point Bridge<QPoint>::toCore(const QPoint &p) noexcept
{
    return Point(p.x(), p.y());
}

inline QPoint Bridge<QPoint>::fromCore(const Point &p) noexcept
{
    return QPoint(p.x(), p.y());
}

inline LineF Bridge<QLineF>::toCore(const QLineF &l) noexcept
{
    return LineF(l.x1(), l.y1(), l.x2(), l.y2());
}

inline QLineF Bridge<QLineF>::fromCore(const LineF &l) noexcept
{
    return QLineF(l.x1(), l.y1(), l.x2(), l.y2());
}

inline RectF Bridge<QRectF>::toCore(const QRectF &r) noexcept
{
    return RectF(r.x(), r.y(), r.width(), r.height());
}

inline QRectF Bridge<QRectF>::fromCore(const RectF &r) noexcept
{
    return QRectF(r.x(), r.y(), r.width(), r.height());
}

inline Rect Bridge<QRect>::toCore(const QRect &r) noexcept
{
    return Rect(Point(r.topLeft().x(), r.topLeft().y()),
                Point(r.bottomRight().x(), r.bottomRight().y()));
}

inline QRect Bridge<QRect>::fromCore(const Rect &r) noexcept
{
    return QRect(QPoint(r.left(), r.top()), QPoint(r.right(), r.bottom()));
}

inline Size Bridge<QSize>::toCore(const QSize &s) noexcept
{
    return Size(s.width(), s.height());
}

inline QSize Bridge<QSize>::fromCore(const Size &s) noexcept
{
    return QSize(s.width(), s.height());
}

inline Transform Bridge<QTransform>::toCore(const QTransform &t) noexcept
{
    return Transform(t.m11(), t.m12(), t.m13(),
                     t.m21(), t.m22(), t.m23(),
                     t.m31(), t.m32(), t.m33());
}

inline QTransform Bridge<QTransform>::fromCore(const Transform &t) noexcept
{
    return QTransform(t.m11(), t.m12(), t.m13(),
                      t.m21(), t.m22(), t.m23(),
                      t.m31(), t.m32(), t.m33());
}

inline Color Bridge<QColor>::toCore(const QColor &c) noexcept
{
    if (!c.isValid())
        return Color();
    return Color(c.red(), c.green(), c.blue(), c.alpha());
}

inline QColor Bridge<QColor>::fromCore(const Color &c) noexcept
{
    if (!c.isValid())
        return QColor();
    return QColor(c.red(), c.green(), c.blue(), c.alpha());
}

template <class U>
struct Bridge<QList<U>>
{
    using CoreElem = typename CoreOf<U>::type;
    using Core = Vector<CoreElem>;

    static Core toCore(const QList<U> &list)
    {
        Core out;
        out.reserve(static_cast<std::ptrdiff_t>(list.size()));
        for (const U &item : list)
            out.append(detail::toCoreElement<U>(item));
        return out;
    }

    static QList<U> fromCore(const Core &vec)
    {
        QList<U> out;
        out.reserve(vec.size());
        for (const CoreElem &item : vec)
            out.append(detail::fromCoreElement<U>(item));
        return out;
    }
};

template <class K, class V>
struct Bridge<QMap<K, V>>
{
    using CoreKey = typename CoreOf<K>::type;
    using CoreVal = typename CoreOf<V>::type;
    using Core = Map<CoreKey, CoreVal>;

    static Core toCore(const QMap<K, V> &map)
    {
        Core out;
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            out.insert(detail::toCoreElement<K>(it.key()), detail::toCoreElement<V>(it.value()));
        return out;
    }

    static QMap<K, V> fromCore(const Core &map)
    {
        QMap<K, V> out;
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            out.insert(detail::fromCoreElement<K>(it.key()), detail::fromCoreElement<V>(it.value()));
        return out;
    }
};

template <class K, class V>
struct Bridge<QHash<K, V>>
{
    using CoreKey = typename CoreOf<K>::type;
    using CoreVal = typename CoreOf<V>::type;
    using Core = Hash<CoreKey, CoreVal>;

    static Core toCore(const QHash<K, V> &hash)
    {
        Core out;
        out.reserve(static_cast<std::ptrdiff_t>(hash.size()));
        for (auto it = hash.cbegin(); it != hash.cend(); ++it)
            out.insert(detail::toCoreElement<K>(it.key()), detail::toCoreElement<V>(it.value()));
        return out;
    }

    static QHash<K, V> fromCore(const Core &hash)
    {
        QHash<K, V> out;
        out.reserve(hash.size());
        for (auto it = hash.cbegin(); it != hash.cend(); ++it)
            out.insert(detail::fromCoreElement<K>(it.key()), detail::fromCoreElement<V>(it.value()));
        return out;
    }
};

template <class T>
struct Bridge<QSet<T>>
{
    using CoreElem = typename CoreOf<T>::type;
    using Core = Set<CoreElem>;

    static Core toCore(const QSet<T> &set)
    {
        Core out;
        out.reserve(static_cast<std::ptrdiff_t>(set.size()));
        for (const T &item : set)
            out.insert(detail::toCoreElement<T>(item));
        return out;
    }

    static QSet<T> fromCore(const Core &set)
    {
        QSet<T> out;
        out.reserve(set.size());
        for (const CoreElem &item : set)
            out.insert(detail::fromCoreElement<T>(item));
        return out;
    }
};

template <class U>
struct Bridge<QQueue<U>>
{
    using CoreElem = typename CoreOf<U>::type;
    using Core = Queue<CoreElem>;

    static Core toCore(const QQueue<U> &q)
    {
        Core out;
        out.reserve(static_cast<std::ptrdiff_t>(q.size()));
        for (const U &item : q)
            out.enqueue(detail::toCoreElement<U>(item));
        return out;
    }

    static QQueue<U> fromCore(const Core &q)
    {
        QQueue<U> out;
        out.reserve(q.size());
        for (const CoreElem &item : q)
            out.enqueue(detail::fromCoreElement<U>(item));
        return out;
    }
};

} // namespace AnimeCore

#endif // ANIMECORE_QTBRIDGE_GEOMETRY_H
