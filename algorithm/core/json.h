#pragma once

#include "algorithm/core/bridge.h"
#include "algorithm/core/string.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace AnimeCore {

class JsonArray;
class JsonObject;
class JsonValueRef;

class JsonValue
{
public:
    enum Type {
        Null = 0x0,
        Bool = 0x1,
        Double = 0x2,
        String = 0x3,
        Array = 0x4,
        Object = 0x5,
        Undefined = 0x80
    };

    JsonValue(Type type = Null) noexcept;
    JsonValue(bool b) noexcept;
    JsonValue(double n) noexcept;
    JsonValue(int n) noexcept;
    JsonValue(long long n) noexcept;
    JsonValue(const AnimeCore::String &s);
    JsonValue(AnimeCore::Latin1String s);
    JsonValue(const char *s);
    JsonValue(const JsonArray &a);
    JsonValue(JsonArray &&a) noexcept;
    JsonValue(const JsonObject &o);
    JsonValue(JsonObject &&o) noexcept;

    JsonValue(const JsonValue &other) = default;
    JsonValue(JsonValue &&other) noexcept = default;
    JsonValue &operator=(const JsonValue &other) = default;
    JsonValue &operator=(JsonValue &&other) noexcept = default;
    ~JsonValue() = default;

    template <typename T, typename = std::enable_if_t<BridgesToCore<T, JsonValue>::value>>
    JsonValue(const T &foreign)
        : JsonValue(Bridge<T>::toCore(foreign))
    {
    }

    template <typename T, typename = std::enable_if_t<BridgesFromCore<T, JsonValue>::value>>
    operator T() const
    {
        return Bridge<T>::fromCore(*this);
    }

    Type type() const noexcept { return m_type; }
    bool isNull() const noexcept { return m_type == Null; }
    bool isBool() const noexcept { return m_type == Bool; }
    bool isDouble() const noexcept { return m_type == Double; }
    bool isString() const noexcept { return m_type == Type::String; }
    bool isArray() const noexcept { return m_type == Array; }
    bool isObject() const noexcept { return m_type == Object; }
    bool isUndefined() const noexcept { return m_type == Undefined; }

    bool isInt64() const noexcept { return m_type == Double && m_isInt64; }
    long long int64Value() const noexcept { return m_int; }

    bool toBool(bool defaultValue = false) const noexcept;
    int toInt(int defaultValue = 0) const noexcept;
    long long toInteger(long long defaultValue = 0) const noexcept;
    double toDouble(double defaultValue = 0.0) const noexcept;
    AnimeCore::String toString() const;
    AnimeCore::String toString(const AnimeCore::String &defaultValue) const;
    JsonArray toArray() const;
    JsonArray toArray(const JsonArray &defaultValue) const;
    JsonObject toObject() const;
    JsonObject toObject(const JsonObject &defaultValue) const;

    JsonValue operator[](const AnimeCore::String &key) const;
    JsonValue operator[](AnimeCore::Latin1String key) const;
    JsonValue operator[](const char *key) const;
    JsonValue operator[](int i) const;

    bool operator==(const JsonValue &other) const noexcept;
    bool operator!=(const JsonValue &other) const noexcept { return !(*this == other); }

private:
    Type m_type = Null;
    bool m_bool = false;
    bool m_isInt64 = false;
    long long m_int = 0;
    double m_dbl = 0.0;
    AnimeCore::String m_str;
    std::shared_ptr<JsonArray> m_arr;
    std::shared_ptr<JsonObject> m_obj;
};

class JsonValueRef
{
public:
    JsonValueRef(JsonArray *array, int index) noexcept
        : m_isObject(false), m_array(array), m_index(index)
    {
    }
    JsonValueRef(JsonObject *object, const String &key)
        : m_isObject(true), m_object(object), m_key(key)
    {
    }

    operator JsonValue() const { return toValue(); }
    JsonValueRef &operator=(const JsonValue &val);
    JsonValueRef &operator=(const JsonValueRef &ref) { return *this = ref.toValue(); }

    JsonValue::Type type() const { return toValue().type(); }
    bool isNull() const { return toValue().isNull(); }
    bool isBool() const { return toValue().isBool(); }
    bool isDouble() const { return toValue().isDouble(); }
    bool isString() const { return toValue().isString(); }
    bool isArray() const { return toValue().isArray(); }
    bool isObject() const { return toValue().isObject(); }
    bool isUndefined() const { return toValue().isUndefined(); }

    bool toBool(bool defaultValue = false) const { return toValue().toBool(defaultValue); }
    int toInt(int defaultValue = 0) const { return toValue().toInt(defaultValue); }
    long long toInteger(long long defaultValue = 0) const { return toValue().toInteger(defaultValue); }
    double toDouble(double defaultValue = 0.0) const { return toValue().toDouble(defaultValue); }
    String toString() const { return toValue().toString(); }
    String toString(const String &defaultValue) const { return toValue().toString(defaultValue); }
    JsonArray toArray() const;
    JsonObject toObject() const;

    bool operator==(const JsonValue &other) const { return toValue() == other; }
    bool operator!=(const JsonValue &other) const { return toValue() != other; }

private:
    JsonValue toValue() const;

    bool m_isObject = false;
    JsonArray *m_array = nullptr;
    JsonObject *m_object = nullptr;
    int m_index = 0;
    String m_key;
};

// Implicitly shared JSON array.
class JsonArray
{
public:
    using MapType = std::vector<JsonValue>;
    using const_iterator = MapType::const_iterator;
    using iterator = MapType::const_iterator;
    using value_type = JsonValue;
    using size_type = int;

    JsonArray() noexcept = default;
    JsonArray(std::initializer_list<JsonValue> args);
    JsonArray(const JsonArray &) noexcept = default;
    JsonArray(JsonArray &&) noexcept = default;
    JsonArray &operator=(const JsonArray &) noexcept = default;
    JsonArray &operator=(JsonArray &&) noexcept = default;
    ~JsonArray() = default;

    template <typename T, typename = std::enable_if_t<BridgesToCore<T, JsonArray>::value>>
    JsonArray(const T &foreign)
        : JsonArray(Bridge<T>::toCore(foreign))
    {
    }

    template <typename T, typename = std::enable_if_t<BridgesFromCore<T, JsonArray>::value>>
    operator T() const
    {
        return Bridge<T>::fromCore(*this);
    }

    int size() const noexcept { return m_d ? static_cast<int>(m_d->size()) : 0; }
    int count() const noexcept { return size(); }
    bool isEmpty() const noexcept { return !m_d || m_d->empty(); }

    JsonValue at(int i) const noexcept;
    JsonValue first() const noexcept { return at(0); }
    JsonValue last() const noexcept { return at(size() - 1); }
    JsonValue operator[](int i) const noexcept { return at(i); }
    JsonValueRef operator[](int i) { return JsonValueRef(this, i); }

    void append(const JsonValue &val);
    void push_back(const JsonValue &val) { append(val); }
    void prepend(const JsonValue &val);
    void insert(int i, const JsonValue &val);
    void replace(int i, const JsonValue &val);
    void removeAt(int i);
    JsonValue takeAt(int i);
    void removeFirst() { removeAt(0); }
    void removeLast() { removeAt(size() - 1); }
    bool contains(const JsonValue &val) const noexcept;

    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    const_iterator constBegin() const noexcept { return begin(); }
    const_iterator constEnd() const noexcept { return end(); }

    bool operator==(const JsonArray &other) const noexcept;
    bool operator!=(const JsonArray &other) const noexcept { return !(*this == other); }

private:
    void detach();
    static const std::shared_ptr<MapType> &emptyShared();

    std::shared_ptr<MapType> m_d;
};

// Implicitly shared JSON object sorted by key in UTF-16 code-unit order.
class JsonObject
{
public:
    using MapType = std::map<String, JsonValue>;

    class const_iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = JsonValue;
        using difference_type = std::ptrdiff_t;
        using pointer = const JsonValue *;
        using reference = const JsonValue &;

        const_iterator() = default;
        explicit const_iterator(MapType::const_iterator it) : m_it(it) {}

        const String &key() const { return m_it->first; }
        const JsonValue &value() const { return m_it->second; }
        const JsonValue &operator*() const { return m_it->second; }
        const JsonValue *operator->() const { return &m_it->second; }

        const_iterator &operator++()
        {
            ++m_it;
            return *this;
        }
        const_iterator operator++(int)
        {
            const_iterator tmp = *this;
            ++m_it;
            return tmp;
        }
        const_iterator &operator--()
        {
            --m_it;
            return *this;
        }
        const_iterator operator--(int)
        {
            const_iterator tmp = *this;
            --m_it;
            return tmp;
        }

        bool operator==(const const_iterator &other) const { return m_it == other.m_it; }
        bool operator!=(const const_iterator &other) const { return m_it != other.m_it; }

    private:
        MapType::const_iterator m_it;
    };

    using iterator = const_iterator;

    JsonObject() noexcept = default;
    JsonObject(std::initializer_list<std::pair<String, JsonValue>> args);
    JsonObject(const JsonObject &) noexcept = default;
    JsonObject(JsonObject &&) noexcept = default;
    JsonObject &operator=(const JsonObject &) noexcept = default;
    JsonObject &operator=(JsonObject &&) noexcept = default;
    ~JsonObject() = default;

    template <typename T, typename = std::enable_if_t<BridgesToCore<T, JsonObject>::value>>
    JsonObject(const T &foreign)
        : JsonObject(Bridge<T>::toCore(foreign))
    {
    }

    template <typename T, typename = std::enable_if_t<BridgesFromCore<T, JsonObject>::value>>
    operator T() const
    {
        return Bridge<T>::fromCore(*this);
    }

    int size() const noexcept { return m_d ? static_cast<int>(m_d->size()) : 0; }
    int count() const noexcept { return size(); }
    int length() const noexcept { return size(); }
    bool isEmpty() const noexcept { return !m_d || m_d->empty(); }

    std::vector<String> keys() const;

    bool contains(const String &key) const noexcept;
    bool contains(Latin1String key) const noexcept { return contains(String(key)); }
    bool contains(const char *key) const noexcept { return contains(String::fromUtf8(key)); }

    JsonValue value(const String &key) const noexcept;
    JsonValue value(Latin1String key) const noexcept { return value(String(key)); }
    JsonValue value(const char *key) const noexcept { return value(String::fromUtf8(key)); }

    JsonValue operator[](const String &key) const noexcept { return value(key); }
    JsonValue operator[](Latin1String key) const noexcept { return value(String(key)); }
    JsonValue operator[](const char *key) const noexcept { return value(String::fromUtf8(key)); }

    JsonValueRef operator[](const String &key) { return JsonValueRef(this, key); }
    JsonValueRef operator[](Latin1String key) { return JsonValueRef(this, String(key)); }
    JsonValueRef operator[](const char *key) { return JsonValueRef(this, String::fromUtf8(key)); }

    const_iterator find(const String &key) const noexcept;
    const_iterator constFind(const String &key) const noexcept { return find(key); }

    const_iterator insert(const String &key, const JsonValue &val);
    void remove(const String &key);
    JsonValue take(const String &key);

    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    const_iterator constBegin() const noexcept { return begin(); }
    const_iterator constEnd() const noexcept { return end(); }

    bool operator==(const JsonObject &other) const noexcept;
    bool operator!=(const JsonObject &other) const noexcept { return !(*this == other); }

private:
    void detach();
    static const std::shared_ptr<MapType> &emptyShared();

    std::shared_ptr<MapType> m_d;
};

struct JsonParseError
{
    enum ParseError {
        NoError = 0,
        UnterminatedObject = 1,
        MissingNameSeparator = 2,
        UnterminatedArray = 3,
        MissingValueSeparator = 4,
        IllegalValue = 5,
        TerminationByNumber = 6,
        IllegalNumber = 7,
        IllegalEscapeSequence = 8,
        IllegalUTF8String = 9,
        UnterminatedString = 10,
        MissingObject = 11,
        DeepNesting = 12,
        DocumentTooLarge = 13,
        GarbageAtEnd = 14
    };

    int offset = -1;
    ParseError error = NoError;

    String errorString() const;
};

class JsonDocument
{
public:
    enum JsonFormat {
        Indented = 0,
        Compact = 1
    };

    JsonDocument() noexcept = default;
    explicit JsonDocument(const JsonObject &object);
    explicit JsonDocument(const JsonArray &array);
    JsonDocument(const JsonDocument &) = default;
    JsonDocument(JsonDocument &&) noexcept = default;
    JsonDocument &operator=(const JsonDocument &) = default;
    JsonDocument &operator=(JsonDocument &&) noexcept = default;
    ~JsonDocument() = default;

    static JsonDocument parse(const std::string &utf8, bool *ok = nullptr);
    static JsonDocument parse(const char *utf8, int size = -1, bool *ok = nullptr);
    static bool parse(const std::string &utf8, JsonDocument *out);
    static bool parse(const char *utf8, int size, JsonDocument *out);

    static JsonDocument fromJson(const std::string &utf8, JsonParseError *error = nullptr);
    static JsonDocument fromJson(const char *utf8, int size = -1, JsonParseError *error = nullptr);

    std::string toJson(JsonFormat format = Indented) const;

    bool isNull() const noexcept { return m_kind == Kind::Null; }
    bool isEmpty() const noexcept;
    bool isObject() const noexcept { return m_kind == Kind::Object; }
    bool isArray() const noexcept { return m_kind == Kind::Array; }

    JsonObject object() const { return m_kind == Kind::Object ? m_object : JsonObject(); }
    JsonArray array() const { return m_kind == Kind::Array ? m_array : JsonArray(); }

    void setObject(const JsonObject &object);
    void setArray(const JsonArray &array);

    bool operator==(const JsonDocument &other) const noexcept;
    bool operator!=(const JsonDocument &other) const noexcept { return !(*this == other); }

private:
    enum class Kind {
        Null,
        Object,
        Array
    };

    Kind m_kind = Kind::Null;
    JsonObject m_object;
    JsonArray m_array;
};

} // namespace AnimeCore
