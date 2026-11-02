#pragma once

#include "algorithm/core/bridge.h"
#include "algorithm/core/json.h"
#include "algorithm/core/qtbridge_geometry.h"
#include "algorithm/core/qtbridge_qstring.h"
#include "algorithm/core/string.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace AnimeCore {

inline JsonValue Bridge<QJsonValue>::toCore(const QJsonValue &q)
{
    switch (q.type()) {
    case QJsonValue::Null:
        return JsonValue(JsonValue::Null);
    case QJsonValue::Bool:
        return JsonValue(q.toBool());
    case QJsonValue::Double: {
        qint64 i0 = q.toInteger(0);
        qint64 i1 = q.toInteger(1);
        if (i0 == i1
            && (static_cast<long double>(i0) != static_cast<long double>(q.toDouble())
                || i0 == std::numeric_limits<qint64>::min())) {
            return JsonValue(static_cast<long long>(i0));
        }
        return JsonValue(q.toDouble());
    }
    case QJsonValue::String:
        return JsonValue(Bridge<QString>::toCore(q.toString()));
    case QJsonValue::Array:
        return JsonValue(Bridge<QJsonArray>::toCore(q.toArray()));
    case QJsonValue::Object:
        return JsonValue(Bridge<QJsonObject>::toCore(q.toObject()));
    case QJsonValue::Undefined:
        return JsonValue(JsonValue::Undefined);
    }
    return JsonValue(JsonValue::Undefined);
}

inline QJsonValue Bridge<QJsonValue>::fromCore(const JsonValue &c)
{
    switch (c.type()) {
    case JsonValue::Null:
        return QJsonValue(QJsonValue::Null);
    case JsonValue::Bool:
        return QJsonValue(c.toBool());
    case JsonValue::Double:
        if (c.isInt64())
            return QJsonValue(static_cast<qint64>(c.int64Value()));
        return QJsonValue(c.toDouble());
    case JsonValue::String:
        return QJsonValue(Bridge<QString>::fromCore(c.toString()));
    case JsonValue::Array:
        return QJsonValue(Bridge<QJsonArray>::fromCore(c.toArray()));
    case JsonValue::Object:
        return QJsonValue(Bridge<QJsonObject>::fromCore(c.toObject()));
    case JsonValue::Undefined:
        return QJsonValue(QJsonValue::Undefined);
    }
    return QJsonValue(QJsonValue::Undefined);
}

inline JsonArray Bridge<QJsonArray>::toCore(const QJsonArray &q)
{
    JsonArray out;
    for (int i = 0; i < q.size(); ++i)
        out.append(Bridge<QJsonValue>::toCore(q.at(i)));
    return out;
}

inline QJsonArray Bridge<QJsonArray>::fromCore(const JsonArray &c)
{
    QJsonArray out;
    for (int i = 0; i < c.size(); ++i)
        out.append(Bridge<QJsonValue>::fromCore(c.at(i)));
    return out;
}

inline JsonObject Bridge<QJsonObject>::toCore(const QJsonObject &q)
{
    JsonObject out;
    for (auto it = q.constBegin(); it != q.constEnd(); ++it)
        out.insert(Bridge<QString>::toCore(it.key()), Bridge<QJsonValue>::toCore(it.value()));
    return out;
}

inline QJsonObject Bridge<QJsonObject>::fromCore(const JsonObject &c)
{
    QJsonObject out;
    for (auto it = c.constBegin(); it != c.constEnd(); ++it)
        out.insert(Bridge<QString>::fromCore(it.key()), Bridge<QJsonValue>::fromCore(it.value()));
    return out;
}

inline JsonVariant Bridge<QVariant>::toCore(const QVariant &q)
{
    return JsonVariant(Bridge<QJsonValue>::toCore(QJsonValue::fromVariant(q)));
}

inline QVariant Bridge<QVariant>::fromCore(const JsonVariant &c)
{
    return Bridge<QJsonValue>::fromCore(c.toJsonValue()).toVariant();
}

#ifdef ANIMECORE_HAS_COREOF
template <>
struct CoreOf<QString>
{
    using type = String;
};
#endif

} // namespace AnimeCore
