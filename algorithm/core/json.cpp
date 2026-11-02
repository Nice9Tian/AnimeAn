#include "algorithm/core/json.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace AnimeCore {

JsonValue::JsonValue(Type type) noexcept
    : m_type(type)
{
    if (m_type == Array)
        m_arr = std::make_shared<JsonArray>();
    else if (m_type == Object)
        m_obj = std::make_shared<JsonObject>();
}

JsonValue::JsonValue(bool b) noexcept
    : m_type(Bool), m_bool(b)
{
}

JsonValue::JsonValue(double n) noexcept
    : m_type(Double), m_isInt64(false), m_dbl(n)
{
}

JsonValue::JsonValue(int n) noexcept
    : m_type(Double), m_isInt64(true), m_int(n), m_dbl(static_cast<double>(n))
{
}

JsonValue::JsonValue(long long n) noexcept
    : m_type(Double), m_isInt64(true), m_int(n), m_dbl(static_cast<double>(n))
{
}

JsonValue::JsonValue(const AnimeCore::String &s)
    : m_type(Type::String), m_str(s.isNull() ? AnimeCore::String(u"", 0) : s)
{
}

JsonValue::JsonValue(AnimeCore::Latin1String s)
    : m_type(Type::String), m_str(s.isNull() ? AnimeCore::String(u"", 0) : AnimeCore::String(s))
{
}

JsonValue::JsonValue(const char *s)
    : m_type(Type::String), m_str(s ? AnimeCore::String::fromUtf8(s) : AnimeCore::String(u"", 0))
{
}

JsonValue::JsonValue(const JsonArray &a)
    : m_type(Array), m_arr(std::make_shared<JsonArray>(a))
{
}

JsonValue::JsonValue(JsonArray &&a) noexcept
    : m_type(Array), m_arr(std::make_shared<JsonArray>(std::move(a)))
{
}

JsonValue::JsonValue(const JsonObject &o)
    : m_type(Object), m_obj(std::make_shared<JsonObject>(o))
{
}

JsonValue::JsonValue(JsonObject &&o) noexcept
    : m_type(Object), m_obj(std::make_shared<JsonObject>(std::move(o)))
{
}

bool JsonValue::toBool(bool defaultValue) const noexcept
{
    return m_type == Bool ? m_bool : defaultValue;
}

int JsonValue::toInt(int defaultValue) const noexcept
{
    if (m_type != Double)
        return defaultValue;
    if (m_isInt64) {
        if (m_int >= std::numeric_limits<int>::min() && m_int <= std::numeric_limits<int>::max())
            return static_cast<int>(m_int);
        return defaultValue;
    }
    if (std::isfinite(m_dbl)
        && m_dbl >= static_cast<double>(std::numeric_limits<int>::min())
        && m_dbl <= static_cast<double>(std::numeric_limits<int>::max())) {
        int iv = static_cast<int>(m_dbl);
        if (static_cast<double>(iv) == m_dbl)
            return iv;
    }
    return defaultValue;
}

long long JsonValue::toInteger(long long defaultValue) const noexcept
{
    if (m_type != Double)
        return defaultValue;
    if (m_isInt64)
        return m_int;
    if (std::isfinite(m_dbl)
        && m_dbl >= static_cast<double>(std::numeric_limits<long long>::min())
        && m_dbl < 9223372036854775808.0) {
        long long iv = static_cast<long long>(m_dbl);
        if (static_cast<double>(iv) == m_dbl)
            return iv;
    }
    return defaultValue;
}

double JsonValue::toDouble(double defaultValue) const noexcept
{
    if (m_type != Double)
        return defaultValue;
    return m_isInt64 ? static_cast<double>(m_int) : m_dbl;
}

AnimeCore::String JsonValue::toString() const
{
    return m_type == Type::String ? m_str : AnimeCore::String();
}

AnimeCore::String JsonValue::toString(const AnimeCore::String &defaultValue) const
{
    return m_type == Type::String ? m_str : defaultValue;
}

JsonArray JsonValue::toArray() const
{
    return (m_type == Array && m_arr) ? *m_arr : JsonArray();
}

JsonArray JsonValue::toArray(const JsonArray &defaultValue) const
{
    return (m_type == Array && m_arr) ? *m_arr : defaultValue;
}

JsonObject JsonValue::toObject() const
{
    return (m_type == Object && m_obj) ? *m_obj : JsonObject();
}

JsonObject JsonValue::toObject(const JsonObject &defaultValue) const
{
    return (m_type == Object && m_obj) ? *m_obj : defaultValue;
}

JsonValue JsonValue::operator[](const AnimeCore::String &key) const
{
    if (m_type == Object && m_obj)
        return m_obj->value(key);
    return JsonValue(Undefined);
}

JsonValue JsonValue::operator[](AnimeCore::Latin1String key) const
{
    return (*this)[AnimeCore::String(key)];
}

JsonValue JsonValue::operator[](const char *key) const
{
    return (*this)[AnimeCore::String::fromUtf8(key)];
}

JsonValue JsonValue::operator[](int i) const
{
    if (m_type == Array && m_arr)
        return m_arr->at(i);
    return JsonValue(Undefined);
}

bool JsonValue::operator==(const JsonValue &other) const noexcept
{
    if (m_type != other.m_type)
        return false;
    switch (m_type) {
    case Null:
    case Undefined:
        return true;
    case Bool:
        return m_bool == other.m_bool;
    case Double:
        if (m_isInt64 && other.m_isInt64)
            return m_int == other.m_int;
        return toDouble() == other.toDouble();
    case Type::String:
        return m_str == other.m_str;
    case Array:
        return toArray() == other.toArray();
    case Object:
        return toObject() == other.toObject();
    }
    return false;
}

JsonValue JsonValueRef::toValue() const
{
    if (m_isObject)
        return m_object ? m_object->value(m_key) : JsonValue(JsonValue::Undefined);
    return m_array ? m_array->at(m_index) : JsonValue(JsonValue::Undefined);
}

JsonValueRef &JsonValueRef::operator=(const JsonValue &val)
{
    if (m_isObject) {
        if (m_object) {
            if (val.isUndefined())
                m_object->remove(m_key);
            else
                m_object->insert(m_key, val);
        }
    } else if (m_array) {
        m_array->replace(m_index, val);
    }
    return *this;
}

JsonArray JsonValueRef::toArray() const
{
    return toValue().toArray();
}

JsonObject JsonValueRef::toObject() const
{
    return toValue().toObject();
}

const std::shared_ptr<JsonArray::MapType> &JsonArray::emptyShared()
{
    static const std::shared_ptr<MapType> s_empty = std::make_shared<MapType>();
    return s_empty;
}

void JsonArray::detach()
{
    if (!m_d)
        m_d = std::make_shared<MapType>();
    else if (m_d.use_count() > 1)
        m_d = std::make_shared<MapType>(*m_d);
}

JsonArray::JsonArray(std::initializer_list<JsonValue> args)
{
    if (args.size() > 0)
        m_d = std::make_shared<MapType>(args);
}

JsonValue JsonArray::at(int i) const noexcept
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->size()))
        return JsonValue(JsonValue::Undefined);
    return (*m_d)[static_cast<std::size_t>(i)];
}

void JsonArray::append(const JsonValue &val)
{
    detach();
    m_d->push_back(val.isUndefined() ? JsonValue(JsonValue::Null) : val);
}

void JsonArray::prepend(const JsonValue &val)
{
    insert(0, val);
}

void JsonArray::insert(int i, const JsonValue &val)
{
    detach();
    if (i < 0)
        i = 0;
    if (i > static_cast<int>(m_d->size()))
        i = static_cast<int>(m_d->size());
    m_d->insert(m_d->begin() + i, val.isUndefined() ? JsonValue(JsonValue::Null) : val);
}

void JsonArray::replace(int i, const JsonValue &val)
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->size()))
        return;
    detach();
    (*m_d)[static_cast<std::size_t>(i)] = val.isUndefined() ? JsonValue(JsonValue::Null) : val;
}

void JsonArray::removeAt(int i)
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->size()))
        return;
    detach();
    m_d->erase(m_d->begin() + i);
}

JsonValue JsonArray::takeAt(int i)
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->size()))
        return JsonValue(JsonValue::Undefined);
    detach();
    JsonValue v = std::move((*m_d)[static_cast<std::size_t>(i)]);
    m_d->erase(m_d->begin() + i);
    return v;
}

bool JsonArray::contains(const JsonValue &val) const noexcept
{
    if (!m_d)
        return false;
    for (const auto &item : *m_d) {
        if (item == val)
            return true;
    }
    return false;
}

JsonArray::const_iterator JsonArray::begin() const noexcept
{
    return m_d ? m_d->cbegin() : emptyShared()->cbegin();
}

JsonArray::const_iterator JsonArray::end() const noexcept
{
    return m_d ? m_d->cend() : emptyShared()->cend();
}

bool JsonArray::operator==(const JsonArray &other) const noexcept
{
    if (m_d == other.m_d)
        return true;
    if (size() != other.size())
        return false;
    for (int i = 0; i < size(); ++i) {
        if (at(i) != other.at(i))
            return false;
    }
    return true;
}

const std::shared_ptr<JsonObject::MapType> &JsonObject::emptyShared()
{
    static const std::shared_ptr<MapType> s_empty = std::make_shared<MapType>();
    return s_empty;
}

void JsonObject::detach()
{
    if (!m_d)
        m_d = std::make_shared<MapType>();
    else if (m_d.use_count() > 1)
        m_d = std::make_shared<MapType>(*m_d);
}

JsonObject::JsonObject(std::initializer_list<std::pair<String, JsonValue>> args)
{
    for (const auto &kv : args)
        insert(kv.first, kv.second);
}

StringList JsonObject::keys() const
{
    std::vector<String> out;
    if (!m_d)
        return out;
    out.reserve(m_d->size());
    for (const auto &kv : *m_d)
        out.push_back(kv.first);
    return out;
}

bool JsonObject::contains(const String &key) const noexcept
{
    return m_d && m_d->find(key) != m_d->end();
}

JsonValue JsonObject::value(const String &key) const noexcept
{
    if (!m_d)
        return JsonValue(JsonValue::Undefined);
    auto it = m_d->find(key);
    if (it == m_d->end())
        return JsonValue(JsonValue::Undefined);
    return it->second;
}

JsonObject::const_iterator JsonObject::find(const String &key) const noexcept
{
    if (!m_d)
        return end();
    return const_iterator(m_d->find(key));
}

JsonObject::const_iterator JsonObject::insert(const String &key, const JsonValue &val)
{
    if (val.isUndefined()) {
        remove(key);
        return end();
    }
    detach();
    String nonNullKey = key.isNull() ? String(u"", 0) : key;
    auto [it, inserted] = m_d->insert_or_assign(nonNullKey, val);
    (void)inserted;
    return const_iterator(it);
}

void JsonObject::remove(const String &key)
{
    if (!m_d)
        return;
    auto it = m_d->find(key);
    if (it == m_d->end())
        return;
    detach();
    m_d->erase(key);
}

JsonValue JsonObject::take(const String &key)
{
    if (!m_d)
        return JsonValue(JsonValue::Undefined);
    auto it = m_d->find(key);
    if (it == m_d->end())
        return JsonValue(JsonValue::Undefined);
    detach();
    auto it2 = m_d->find(key);
    JsonValue v = std::move(it2->second);
    m_d->erase(it2);
    return v;
}

JsonObject::const_iterator JsonObject::begin() const noexcept
{
    return const_iterator(m_d ? m_d->cbegin() : emptyShared()->cbegin());
}

JsonObject::const_iterator JsonObject::end() const noexcept
{
    return const_iterator(m_d ? m_d->cend() : emptyShared()->cend());
}

bool JsonObject::operator==(const JsonObject &other) const noexcept
{
    if (m_d == other.m_d)
        return true;
    if (size() != other.size())
        return false;
    if (!m_d || m_d->empty())
        return true;
    return *m_d == *other.m_d;
}

String JsonParseError::errorString() const
{
    switch (error) {
    case NoError:
        return ANIME_STR("no error occurred");
    case UnterminatedObject:
        return ANIME_STR("unterminated object");
    case MissingNameSeparator:
        return ANIME_STR("missing name separator");
    case UnterminatedArray:
        return ANIME_STR("unterminated array");
    case MissingValueSeparator:
        return ANIME_STR("missing value separator");
    case IllegalValue:
        return ANIME_STR("illegal value");
    case TerminationByNumber:
        return ANIME_STR("invalid termination by number");
    case IllegalNumber:
        return ANIME_STR("illegal number");
    case IllegalEscapeSequence:
        return ANIME_STR("illegal escape sequence");
    case IllegalUTF8String:
        return ANIME_STR("invalid UTF8 string");
    case UnterminatedString:
        return ANIME_STR("unterminated string");
    case MissingObject:
        return ANIME_STR("object is missing after a comma");
    case DeepNesting:
        return ANIME_STR("too deeply nested document");
    case DocumentTooLarge:
        return ANIME_STR("too large document");
    case GarbageAtEnd:
        return ANIME_STR("garbage at the end of the document");
    }
    return ANIME_STR("unknown error");
}

namespace {

void appendHexEscape(std::string &out, uint16_t u)
{
    static const char hex[] = "0123456789abcdef";
    char buf[6] = {
        '\\', 'u',
        hex[(u >> 12) & 0xF],
        hex[(u >> 8) & 0xF],
        hex[(u >> 4) & 0xF],
        hex[u & 0xF]
    };
    out.append(buf, 6);
}

void writeEscapedString(std::string &out, std::u16string_view sv)
{
    out.push_back('"');
    for (std::size_t i = 0; i < sv.size(); ++i) {
        uint16_t u = sv[i];
        if (u == u'"') {
            out.append("\\\"", 2);
        } else if (u == u'\\') {
            out.append("\\\\", 2);
        } else if (u == 0x08) {
            out.append("\\b", 2);
        } else if (u == 0x0C) {
            out.append("\\f", 2);
        } else if (u == 0x0A) {
            out.append("\\n", 2);
        } else if (u == 0x0D) {
            out.append("\\r", 2);
        } else if (u == 0x09) {
            out.append("\\t", 2);
        } else if (u < 0x20) {
            appendHexEscape(out, u);
        } else if (u < 0x80) {
            out.push_back(static_cast<char>(u));
        } else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < sv.size() && sv[i + 1] >= 0xDC00 && sv[i + 1] <= 0xDFFF) {
                uint32_t u2 = sv[++i];
                uint32_t cp = 0x10000 + (((uint32_t(u) - 0xD800) << 10) | (u2 - 0xDC00));
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                appendHexEscape(out, u);
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            appendHexEscape(out, u);
        } else {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    out.push_back('"');
}

void writeNumber(std::string &out, const JsonValue &val)
{
    if (val.isInt64()) {
        char buf[32] = {};
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val.int64Value());
        if (ec == std::errc()) {
            out.append(buf, static_cast<std::size_t>(ptr - buf));
            return;
        }
    }

    double d = val.toDouble();
    if (std::isnan(d) || std::isinf(d)) {
        out.append("null", 4);
        return;
    }
    if (d == 0.0) {
        out.push_back('0');
        return;
    }

    // Exact integers within 2^53 are emitted without decimal point or exponent.
    if (std::abs(d) <= static_cast<double>(1LL << 53)) {
        long long iv = static_cast<long long>(d);
        if (static_cast<double>(iv) == d) {
            char buf[32] = {};
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), iv);
            if (ec == std::errc()) {
                out.append(buf, static_cast<std::size_t>(ptr - buf));
                return;
            }
        }
    }

    bool neg = std::signbit(d);
    double absd = std::abs(d);
    char buf[64] = {};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), absd, std::chars_format::scientific);
    if (ec != std::errc()) {
        out.append("null", 4);
        return;
    }

    std::string s(buf, static_cast<std::size_t>(ptr - buf));
    std::size_t epos = s.find('e');
    int exp = std::stoi(s.substr(epos + 1));
    std::string sig;
    sig.reserve(epos);
    for (std::size_t i = 0; i < epos; ++i) {
        if (s[i] != '.')
            sig.push_back(s[i]);
    }
    int k = static_cast<int>(sig.size());
    int decpt = exp + 1;

    std::string sci = (neg ? "-" : "") + s;
    std::string fix;
    if (decpt <= 0 && decpt > -6) {
        fix = (neg ? "-0." : "0.") + std::string(static_cast<std::size_t>(-decpt), '0') + sig;
    } else if (decpt > 0 && decpt <= k) {
        fix = (neg ? "-" : "") + sig.substr(0, static_cast<std::size_t>(decpt));
        if (decpt < k)
            fix += "." + sig.substr(static_cast<std::size_t>(decpt));
    } else if (decpt > k && decpt - k <= 10) {
        fix = (neg ? "-" : "") + sig + std::string(static_cast<std::size_t>(decpt - k), '0');
    }

    if (!fix.empty() && fix.size() <= sci.size())
        out.append(fix);
    else
        out.append(sci);
}

void writeValue(std::string &out, const JsonValue &val, bool indented, int depth);

void writeObject(std::string &out, const JsonObject &obj, bool indented, int depth)
{
    if (obj.isEmpty()) {
        if (indented) {
            out.append("{\n", 2);
            out.append(static_cast<std::size_t>(depth * 4), ' ');
            out.push_back('}');
        } else {
            out.append("{}", 2);
        }
        return;
    }

    out.push_back('{');
    if (indented)
        out.push_back('\n');

    int idx = 0;
    int total = obj.size();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it, ++idx) {
        if (indented)
            out.append(static_cast<std::size_t>((depth + 1) * 4), ' ');
        writeEscapedString(out, it.key().view());
        if (indented)
            out.append(": ", 2);
        else
            out.push_back(':');
        writeValue(out, it.value(), indented, depth + 1);
        if (idx + 1 < total)
            out.push_back(',');
        if (indented)
            out.push_back('\n');
    }

    if (indented)
        out.append(static_cast<std::size_t>(depth * 4), ' ');
    out.push_back('}');
}

void writeArray(std::string &out, const JsonArray &arr, bool indented, int depth)
{
    if (arr.isEmpty()) {
        if (indented) {
            out.append("[\n", 2);
            out.append(static_cast<std::size_t>(depth * 4), ' ');
            out.push_back(']');
        } else {
            out.append("[]", 2);
        }
        return;
    }

    out.push_back('[');
    if (indented)
        out.push_back('\n');

    int total = arr.size();
    for (int i = 0; i < total; ++i) {
        if (indented)
            out.append(static_cast<std::size_t>((depth + 1) * 4), ' ');
        writeValue(out, arr.at(i), indented, depth + 1);
        if (i + 1 < total)
            out.push_back(',');
        if (indented)
            out.push_back('\n');
    }

    if (indented)
        out.append(static_cast<std::size_t>(depth * 4), ' ');
    out.push_back(']');
}

void writeValue(std::string &out, const JsonValue &val, bool indented, int depth)
{
    switch (val.type()) {
    case JsonValue::Null:
    case JsonValue::Undefined:
        out.append("null", 4);
        break;
    case JsonValue::Bool:
        if (val.toBool())
            out.append("true", 4);
        else
            out.append("false", 5);
        break;
    case JsonValue::Double:
        writeNumber(out, val);
        break;
    case JsonValue::String:
        writeEscapedString(out, val.toString().view());
        break;
    case JsonValue::Array:
        writeArray(out, val.toArray(), indented, depth);
        break;
    case JsonValue::Object:
        writeObject(out, val.toObject(), indented, depth);
        break;
    }
}

class Parser
{
public:
    Parser(const char *data, int size)
        : m_data(reinterpret_cast<const unsigned char *>(data)), m_size(size)
    {
    }

    JsonDocument parse(JsonParseError *err)
    {
        if (m_size >= 3 && m_data[0] == 0xEF && m_data[1] == 0xBB && m_data[2] == 0xBF)
            m_pos = 3;

        skipSpace();
        if (m_pos >= m_size) {
            setError(JsonParseError::IllegalValue);
            finishError(err);
            return JsonDocument();
        }

        JsonDocument doc;
        unsigned char c = m_data[m_pos];
        if (c == '{') {
            JsonObject obj;
            if (!parseObject(obj, 1)) {
                finishError(err);
                return JsonDocument();
            }
            doc.setObject(obj);
        } else if (c == '[') {
            JsonArray arr;
            if (!parseArray(arr, 1)) {
                finishError(err);
                return JsonDocument();
            }
            doc.setArray(arr);
        } else {
            setError(JsonParseError::IllegalValue);
            finishError(err);
            return JsonDocument();
        }

        skipSpace();
        if (m_pos < m_size) {
            setError(JsonParseError::GarbageAtEnd);
            finishError(err);
            return JsonDocument();
        }

        if (err) {
            err->error = JsonParseError::NoError;
            err->offset = 0;
        }
        return doc;
    }

private:
    static constexpr int kMaxDepth = 1024;

    void setError(JsonParseError::ParseError e)
    {
        if (m_error == JsonParseError::NoError) {
            m_error = e;
            m_errorOffset = m_pos;
        }
    }

    void finishError(JsonParseError *err) const
    {
        if (err) {
            err->error = m_error;
            err->offset = m_errorOffset;
        }
    }

    void skipSpace()
    {
        while (m_pos < m_size) {
            unsigned char c = m_data[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++m_pos;
            else
                break;
        }
    }

    static int hexDigit(unsigned char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    bool parseString(String &out)
    {
        if (m_pos >= m_size || m_data[m_pos] != '"') {
            setError(JsonParseError::IllegalValue);
            return false;
        }
        ++m_pos;
        std::u16string buf;
        while (m_pos < m_size) {
            unsigned char b0 = m_data[m_pos++];
            if (b0 == '"') {
                out = String(std::move(buf));
                return true;
            }
            if (b0 == '\\') {
                if (m_pos >= m_size) {
                    setError(JsonParseError::UnterminatedString);
                    return false;
                }
                unsigned char esc = m_data[m_pos++];
                switch (esc) {
                case '"':
                    buf.push_back(u'"');
                    break;
                case '\\':
                    buf.push_back(u'\\');
                    break;
                case '/':
                    buf.push_back(u'/');
                    break;
                case 'b':
                    buf.push_back(0x08);
                    break;
                case 'f':
                    buf.push_back(0x0C);
                    break;
                case 'n':
                    buf.push_back(0x0A);
                    break;
                case 'r':
                    buf.push_back(0x0D);
                    break;
                case 't':
                    buf.push_back(0x09);
                    break;
                case 'u': {
                    if (m_pos + 4 > m_size) {
                        setError(JsonParseError::IllegalEscapeSequence);
                        return false;
                    }
                    int d0 = hexDigit(m_data[m_pos]);
                    int d1 = hexDigit(m_data[m_pos + 1]);
                    int d2 = hexDigit(m_data[m_pos + 2]);
                    int d3 = hexDigit(m_data[m_pos + 3]);
                    if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
                        setError(JsonParseError::IllegalEscapeSequence);
                        return false;
                    }
                    uint16_t u = static_cast<uint16_t>((d0 << 12) | (d1 << 8) | (d2 << 4) | d3);
                    buf.push_back(static_cast<char16_t>(u));
                    m_pos += 4;
                    break;
                }
                default:
                    if (esc >= 0x80) {
                        setError(JsonParseError::IllegalEscapeSequence);
                        return false;
                    }
                    buf.push_back(static_cast<char16_t>(esc));
                    break;
                }
            } else if (b0 < 0x80) {
                buf.push_back(static_cast<char16_t>(b0));
            } else if (b0 >= 0xC2 && b0 <= 0xDF) {
                if (m_pos >= m_size || (m_data[m_pos] & 0xC0) != 0x80) {
                    setError(JsonParseError::IllegalUTF8String);
                    return false;
                }
                unsigned char b1 = m_data[m_pos++];
                uint32_t cp = ((uint32_t(b0 & 0x1F) << 6) | uint32_t(b1 & 0x3F));
                buf.push_back(static_cast<char16_t>(cp));
            } else if (b0 >= 0xE0 && b0 <= 0xEF) {
                if (m_pos + 1 >= m_size
                    || (m_data[m_pos] & 0xC0) != 0x80
                    || (m_data[m_pos + 1] & 0xC0) != 0x80) {
                    setError(JsonParseError::IllegalUTF8String);
                    return false;
                }
                unsigned char b1 = m_data[m_pos++];
                unsigned char b2 = m_data[m_pos++];
                if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 >= 0xA0)) {
                    setError(JsonParseError::IllegalUTF8String);
                    return false;
                }
                uint32_t cp = (uint32_t(b0 & 0x0F) << 12)
                            | (uint32_t(b1 & 0x3F) << 6)
                            | uint32_t(b2 & 0x3F);
                buf.push_back(static_cast<char16_t>(cp));
            } else if (b0 >= 0xF0 && b0 <= 0xF4) {
                if (m_pos + 2 >= m_size
                    || (m_data[m_pos] & 0xC0) != 0x80
                    || (m_data[m_pos + 1] & 0xC0) != 0x80
                    || (m_data[m_pos + 2] & 0xC0) != 0x80) {
                    setError(JsonParseError::IllegalUTF8String);
                    return false;
                }
                unsigned char b1 = m_data[m_pos++];
                unsigned char b2 = m_data[m_pos++];
                unsigned char b3 = m_data[m_pos++];
                if ((b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 >= 0x90)) {
                    setError(JsonParseError::IllegalUTF8String);
                    return false;
                }
                uint32_t cp = (uint32_t(b0 & 0x07) << 18)
                            | (uint32_t(b1 & 0x3F) << 12)
                            | (uint32_t(b2 & 0x3F) << 6)
                            | uint32_t(b3 & 0x3F);
                cp -= 0x10000;
                buf.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
                buf.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
            } else {
                setError(JsonParseError::IllegalUTF8String);
                return false;
            }
        }
        setError(JsonParseError::UnterminatedString);
        return false;
    }

    bool parseObject(JsonObject &out, int depth)
    {
        if (depth > kMaxDepth) {
            setError(JsonParseError::DeepNesting);
            return false;
        }
        ++m_pos; // skip '{'
        skipSpace();
        if (m_pos < m_size && m_data[m_pos] == '}') {
            ++m_pos;
            return true;
        }

        while (true) {
            skipSpace();
            if (m_pos >= m_size) {
                setError(JsonParseError::UnterminatedObject);
                return false;
            }
            if (m_data[m_pos] != '"') {
                setError(JsonParseError::MissingObject);
                return false;
            }
            String key;
            if (!parseString(key))
                return false;

            skipSpace();
            if (m_pos >= m_size || m_data[m_pos] != ':') {
                setError(JsonParseError::MissingNameSeparator);
                return false;
            }
            ++m_pos;

            skipSpace();
            JsonValue val;
            if (!parseValue(val, depth))
                return false;
            out.insert(key, val);

            skipSpace();
            if (m_pos >= m_size) {
                setError(JsonParseError::UnterminatedObject);
                return false;
            }
            if (m_data[m_pos] == ',') {
                ++m_pos;
                skipSpace();
                if (m_pos >= m_size || m_data[m_pos] == '}') {
                    setError(JsonParseError::MissingObject);
                    return false;
                }
            } else if (m_data[m_pos] == '}') {
                ++m_pos;
                return true;
            } else {
                setError(JsonParseError::UnterminatedObject);
                return false;
            }
        }
    }

    bool parseArray(JsonArray &out, int depth)
    {
        if (depth > kMaxDepth) {
            setError(JsonParseError::DeepNesting);
            return false;
        }
        ++m_pos; // skip '['
        skipSpace();
        if (m_pos < m_size && m_data[m_pos] == ']') {
            ++m_pos;
            return true;
        }

        while (true) {
            skipSpace();
            if (m_pos >= m_size) {
                setError(JsonParseError::UnterminatedArray);
                return false;
            }
            JsonValue val;
            if (!parseValue(val, depth))
                return false;
            out.append(val);

            skipSpace();
            if (m_pos >= m_size) {
                setError(JsonParseError::UnterminatedArray);
                return false;
            }
            if (m_data[m_pos] == ',') {
                ++m_pos;
                skipSpace();
                if (m_pos >= m_size || m_data[m_pos] == ']') {
                    setError(JsonParseError::MissingObject);
                    return false;
                }
            } else if (m_data[m_pos] == ']') {
                ++m_pos;
                return true;
            } else {
                setError(JsonParseError::MissingValueSeparator);
                return false;
            }
        }
    }

    bool parseNumber(JsonValue &out)
    {
        int start = m_pos;
        bool neg = false;
        if (m_pos < m_size && m_data[m_pos] == '-') {
            neg = true;
            ++m_pos;
        }

        int intDigits = 0;
        if (m_pos < m_size && m_data[m_pos] == '0') {
            ++m_pos;
            ++intDigits;
            if (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                setError(JsonParseError::IllegalNumber);
                return false;
            }
        } else {
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
                ++intDigits;
            }
        }

        bool isFloat = false;
        int fracDigits = 0;
        if (m_pos < m_size && m_data[m_pos] == '.') {
            isFloat = true;
            ++m_pos;
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
                ++fracDigits;
            }
        }

        if (intDigits == 0 && fracDigits == 0) {
            setError(JsonParseError::IllegalNumber);
            return false;
        }

        if (m_pos < m_size && (m_data[m_pos] == 'e' || m_data[m_pos] == 'E')) {
            isFloat = true;
            ++m_pos;
            if (m_pos < m_size && (m_data[m_pos] == '+' || m_data[m_pos] == '-'))
                ++m_pos;
            int expDigits = 0;
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
                ++expDigits;
            }
            if (expDigits == 0) {
                setError(JsonParseError::IllegalNumber);
                return false;
            }
        }

        const char *first = reinterpret_cast<const char *>(m_data + start);
        const char *last = reinterpret_cast<const char *>(m_data + m_pos);

        if (!isFloat) {
            const char *ufirst = neg ? (first + 1) : first;
            unsigned long long uval = 0;
            auto [ptr, ec] = std::from_chars(ufirst, last, uval, 10);
            if (ec == std::errc() && ptr == last) {
                unsigned long long maxPos = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
                if (!neg && uval <= maxPos) {
                    out = JsonValue(static_cast<long long>(uval));
                    return true;
                }
                if (neg && uval <= maxPos + 1ULL) {
                    if (uval == 0)
                        out = JsonValue(0LL);
                    else if (uval == maxPos + 1ULL)
                        out = JsonValue(std::numeric_limits<long long>::min());
                    else
                        out = JsonValue(-static_cast<long long>(uval));
                    return true;
                }
            }
        }

        double d = 0.0;
        auto [ptr, ec] = std::from_chars(first, last, d, std::chars_format::general);
        if (ec != std::errc() || ptr != last || !std::isfinite(d)) {
            setError(JsonParseError::IllegalNumber);
            return false;
        }
        out = JsonValue(d);
        return true;
    }

    bool parseValue(JsonValue &out, int depth)
    {
        if (m_pos >= m_size) {
            setError(JsonParseError::IllegalValue);
            return false;
        }
        unsigned char c = m_data[m_pos];
        if (c == '"') {
            String s;
            if (!parseString(s))
                return false;
            out = JsonValue(s);
            return true;
        }
        if (c == '{') {
            JsonObject obj;
            if (!parseObject(obj, depth + 1))
                return false;
            out = JsonValue(std::move(obj));
            return true;
        }
        if (c == '[') {
            JsonArray arr;
            if (!parseArray(arr, depth + 1))
                return false;
            out = JsonValue(std::move(arr));
            return true;
        }
        if (c == 't') {
            if (m_pos + 4 <= m_size && std::memcmp(m_data + m_pos, "true", 4) == 0) {
                m_pos += 4;
                out = JsonValue(true);
                return true;
            }
            setError(JsonParseError::IllegalValue);
            return false;
        }
        if (c == 'f') {
            if (m_pos + 5 <= m_size && std::memcmp(m_data + m_pos, "false", 5) == 0) {
                m_pos += 5;
                out = JsonValue(false);
                return true;
            }
            setError(JsonParseError::IllegalValue);
            return false;
        }
        if (c == 'n') {
            if (m_pos + 4 <= m_size && std::memcmp(m_data + m_pos, "null", 4) == 0) {
                m_pos += 4;
                out = JsonValue(JsonValue::Null);
                return true;
            }
            setError(JsonParseError::IllegalValue);
            return false;
        }
        if (c == '-' || c == '.' || (c >= '0' && c <= '9'))
            return parseNumber(out);

        setError(JsonParseError::IllegalValue);
        return false;
    }

    const unsigned char *m_data;
    int m_size;
    int m_pos = 0;
    JsonParseError::ParseError m_error = JsonParseError::NoError;
    int m_errorOffset = 0;
};

} // namespace

JsonDocument::JsonDocument(const JsonObject &object)
    : m_kind(Kind::Object), m_object(object)
{
}

JsonDocument::JsonDocument(const JsonArray &array)
    : m_kind(Kind::Array), m_array(array)
{
}

JsonDocument JsonDocument::fromJson(const char *utf8, int size, JsonParseError *error)
{
    if (!utf8) {
        if (error) {
            error->error = JsonParseError::IllegalValue;
            error->offset = 0;
        }
        return JsonDocument();
    }
    if (size < 0)
        size = static_cast<int>(std::strlen(utf8));
    Parser parser(utf8, size);
    return parser.parse(error);
}

JsonDocument JsonDocument::fromJson(const std::string &utf8, JsonParseError *error)
{
    return fromJson(utf8.data(), static_cast<int>(utf8.size()), error);
}

JsonDocument JsonDocument::parse(const char *utf8, int size, bool *ok)
{
    JsonParseError err;
    JsonDocument doc = fromJson(utf8, size, &err);
    if (ok)
        *ok = (err.error == JsonParseError::NoError && !doc.isNull());
    return doc;
}

JsonDocument JsonDocument::parse(const std::string &utf8, bool *ok)
{
    return parse(utf8.data(), static_cast<int>(utf8.size()), ok);
}

bool JsonDocument::parse(const std::string &utf8, JsonDocument *out)
{
    bool ok = false;
    JsonDocument doc = parse(utf8, &ok);
    if (out)
        *out = std::move(doc);
    return ok;
}

bool JsonDocument::parse(const char *utf8, int size, JsonDocument *out)
{
    bool ok = false;
    JsonDocument doc = parse(utf8, size, &ok);
    if (out)
        *out = std::move(doc);
    return ok;
}

std::string JsonDocument::toJson(JsonFormat format) const
{
    if (m_kind == Kind::Null)
        return std::string();
    std::string out;
    bool indented = (format == Indented);
    if (m_kind == Kind::Object)
        writeObject(out, m_object, indented, 0);
    else if (m_kind == Kind::Array)
        writeArray(out, m_array, indented, 0);
    if (indented)
        out.push_back('\n');
    return out;
}

bool JsonDocument::isEmpty() const noexcept
{
    if (m_kind == Kind::Object)
        return m_object.isEmpty();
    if (m_kind == Kind::Array)
        return m_array.isEmpty();
    return true;
}

void JsonDocument::setObject(const JsonObject &object)
{
    m_kind = Kind::Object;
    m_object = object;
    m_array = JsonArray();
}

void JsonDocument::setArray(const JsonArray &array)
{
    m_kind = Kind::Array;
    m_array = array;
    m_object = JsonObject();
}

bool JsonDocument::operator==(const JsonDocument &other) const noexcept
{
    if (m_kind != other.m_kind)
        return false;
    if (m_kind == Kind::Object)
        return m_object == other.m_object;
    if (m_kind == Kind::Array)
        return m_array == other.m_array;
    return true;
}

bool JsonVariant::toBool() const noexcept
{
    if (m_val.isBool())
        return m_val.toBool();
    if (m_val.isDouble())
        return m_val.toDouble() != 0.0;
    if (m_val.isString()) {
        const String s = m_val.toString().toLower();
        return !s.isEmpty() && s != "0" && s != "false";
    }
    return false;
}

int JsonVariant::toInt(bool *ok) const noexcept
{
    if (m_val.isDouble()) {
        if (m_val.isInt64()) {
            // QVariant::toInt wraps a 64-bit value into int and reports success;
            // here a value outside int fails, so it never turns into a small one.
            const long long v = m_val.int64Value();
            const bool inRange = v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max();
            if (ok)
                *ok = inRange;
            return inRange ? static_cast<int>(v) : 0;
        }
        const double d = m_val.toDouble();
        if (std::isfinite(d)
            && d >= static_cast<double>(std::numeric_limits<int>::min())
            && d <= static_cast<double>(std::numeric_limits<int>::max())) {
            if (ok)
                *ok = true;
            return static_cast<int>(std::lround(d));
        }
        if (ok)
            *ok = false;
        return 0;
    }
    if (m_val.isBool()) {
        if (ok)
            *ok = true;
        return m_val.toBool() ? 1 : 0;
    }
    if (m_val.isString())
        return m_val.toString().toInt(ok);
    if (ok)
        *ok = false;
    return 0;
}

long long JsonVariant::toLongLong(bool *ok) const noexcept
{
    if (m_val.isDouble()) {
        if (m_val.isInt64()) {
            if (ok)
                *ok = true;
            return m_val.int64Value();
        }
        const double d = m_val.toDouble();
        if (std::isfinite(d)
            && d >= static_cast<double>(std::numeric_limits<long long>::min())
            && d < 9223372036854775808.0) {
            if (ok)
                *ok = true;
            return static_cast<long long>(std::llround(d));
        }
        if (ok)
            *ok = false;
        return 0;
    }
    if (m_val.isBool()) {
        if (ok)
            *ok = true;
        return m_val.toBool() ? 1LL : 0LL;
    }
    if (m_val.isString())
        return m_val.toString().toLongLong(ok);
    if (ok)
        *ok = false;
    return 0;
}

double JsonVariant::toDouble(bool *ok) const noexcept
{
    if (m_val.isDouble()) {
        if (ok)
            *ok = true;
        return m_val.toDouble();
    }
    if (m_val.isBool()) {
        if (ok)
            *ok = true;
        return m_val.toBool() ? 1.0 : 0.0;
    }
    if (m_val.isString())
        return m_val.toString().toDouble(ok);
    if (ok)
        *ok = false;
    return 0.0;
}

String JsonVariant::toString() const
{
    if (m_val.isString())
        return m_val.toString();
    if (m_val.isBool())
        return m_val.toBool() ? String("true") : String("false");
    if (m_val.isDouble()) {
        if (m_val.isInt64()) {
            const long long v = m_val.int64Value();
            if (v <= -(1LL << 53) || v >= (1LL << 53))
                return String::number(v);
        }
        const double d = m_val.toDouble();
        if (std::isnan(d) || std::isinf(d))
            return String();
        double norm = (d == 0.0) ? 0.0 : d;
        char buf[128] = {};
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), norm);
        if (ec != std::errc())
            return String();
        return String(Latin1String(buf, static_cast<int>(ptr - buf)));
    }
    return String();
}

} // namespace AnimeCore
