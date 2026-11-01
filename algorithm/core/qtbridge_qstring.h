#ifndef ANIMECORE_QTBRIDGE_QSTRING_H
#define ANIMECORE_QTBRIDGE_QSTRING_H

#include "algorithm/core/bridge.h"
#include "algorithm/core/string.h"

#include <QString>

namespace AnimeCore {

// Comes before every other bridge: overload resolution on Qt constructors asks
// about QString almost at once, and the trait remembers the first answer.
template <>
struct Bridge<QString>
{
    using Core = String;

    static String toCore(const QString &q)
    {
        if (q.isNull())
            return String();
        return String(reinterpret_cast<const char16_t *>(q.utf16()), static_cast<int>(q.size()));
    }

    static QString fromCore(const String &c)
    {
        if (c.isNull())
            return QString();
        return QString::fromUtf16(c.utf16(), c.size());
    }
};

} // namespace AnimeCore

#endif // ANIMECORE_QTBRIDGE_QSTRING_H
