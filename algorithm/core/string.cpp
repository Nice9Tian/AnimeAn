#include "algorithm/core/string.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace AnimeCore {

namespace {

std::u16string decodeUtf8(const char *utf8, int size)
{
    std::u16string out;
    if (!utf8 || size == 0)
        return out;
    if (size < 0)
        size = static_cast<int>(std::strlen(utf8));
    out.reserve(static_cast<std::size_t>(size));

    const auto *bytes = reinterpret_cast<const unsigned char *>(utf8);
    int i = 0;
    while (i < size) {
        unsigned char b0 = bytes[i];
        if (b0 < 0x80) {
            out.push_back(static_cast<char16_t>(b0));
            ++i;
        } else if (b0 >= 0xC2 && b0 <= 0xDF) {
            if (i + 1 < size && (bytes[i + 1] & 0xC0) == 0x80) {
                uint32_t cp = ((uint32_t(b0 & 0x1F) << 6) | uint32_t(bytes[i + 1] & 0x3F));
                out.push_back(static_cast<char16_t>(cp));
                i += 2;
            } else {
                out.push_back(u'\uFFFD');
                ++i;
            }
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            if (i + 2 < size && (bytes[i + 1] & 0xC0) == 0x80 && (bytes[i + 2] & 0xC0) == 0x80) {
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                bool overlong = (b0 == 0xE0 && b1 < 0xA0);
                bool surrogate = (b0 == 0xED && b1 >= 0xA0);
                if (!overlong && !surrogate) {
                    uint32_t cp = (uint32_t(b0 & 0x0F) << 12)
                                | (uint32_t(b1 & 0x3F) << 6)
                                | uint32_t(b2 & 0x3F);
                    out.push_back(static_cast<char16_t>(cp));
                    i += 3;
                    continue;
                }
            }
            out.push_back(u'\uFFFD');
            ++i;
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            if (i + 3 < size
                && (bytes[i + 1] & 0xC0) == 0x80
                && (bytes[i + 2] & 0xC0) == 0x80
                && (bytes[i + 3] & 0xC0) == 0x80) {
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                unsigned char b3 = bytes[i + 3];
                bool overlong = (b0 == 0xF0 && b1 < 0x90);
                bool outOfRange = (b0 == 0xF4 && b1 >= 0x90);
                if (!overlong && !outOfRange) {
                    uint32_t cp = (uint32_t(b0 & 0x07) << 18)
                                | (uint32_t(b1 & 0x3F) << 12)
                                | (uint32_t(b2 & 0x3F) << 6)
                                | uint32_t(b3 & 0x3F);
                    cp -= 0x10000;
                    out.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
                    out.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
                    i += 4;
                    continue;
                }
            }
            out.push_back(u'\uFFFD');
            ++i;
        } else {
            out.push_back(u'\uFFFD');
            ++i;
        }
    }
    return out;
}

std::string encodeUtf8(std::u16string_view sv)
{
    std::string out;
    out.reserve(sv.size() * 3);
    for (std::size_t i = 0; i < sv.size(); ++i) {
        uint32_t u = sv[i];
        if (u < 0x80) {
            out.push_back(static_cast<char>(u));
        } else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < sv.size() && sv[i + 1] >= 0xDC00 && sv[i + 1] <= 0xDFFF) {
                uint32_t u2 = sv[++i];
                uint32_t cp = 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00));
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.append("\xEF\xBF\xBD", 3);
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            out.append("\xEF\xBF\xBD", 3);
        } else {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return out;
}

inline char16_t foldCase(char16_t c, CaseSensitivity cs) noexcept
{
    if (cs == CaseSensitive)
        return c;
    return Char(c).toLower().unicode();
}

bool rangeEquals(const char16_t *a, const char16_t *b, int len, CaseSensitivity cs) noexcept
{
    if (cs == CaseSensitive)
        return std::char_traits<char16_t>::compare(a, b, static_cast<std::size_t>(len)) == 0;
    for (int i = 0; i < len; ++i) {
        if (Char(a[i]).toLower() != Char(b[i]).toLower())
            return false;
    }
    return true;
}

String padField(const String &val, int fieldWidth, Char fillChar)
{
    if (fieldWidth == 0)
        return val;
    int absWidth = fieldWidth < 0 ? -fieldWidth : fieldWidth;
    if (val.size() >= absWidth)
        return val;
    int padCount = absWidth - val.size();
    std::u16string res;
    res.reserve(static_cast<std::size_t>(absWidth));
    if (fieldWidth > 0) {
        res.append(static_cast<std::size_t>(padCount), fillChar.unicode());
        res.append(val.view());
    } else {
        res.append(val.view());
        res.append(static_cast<std::size_t>(padCount), fillChar.unicode());
    }
    return String(std::move(res));
}

} // namespace

bool Char::isSpace() const noexcept
{
    if (m_ch >= 0x09 && m_ch <= 0x0D)
        return true;
    if (m_ch == 0x20 || m_ch == 0x85 || m_ch == 0xA0 || m_ch == 0x1680)
        return true;
    if (m_ch >= 0x2000 && m_ch <= 0x200A)
        return true;
    return m_ch == 0x2028 || m_ch == 0x2029 || m_ch == 0x202F || m_ch == 0x205F || m_ch == 0x3000;
}

bool Char::isDigit() const noexcept
{
    return m_ch >= u'0' && m_ch <= u'9';
}

bool Char::isLower() const noexcept
{
    return (m_ch >= u'a' && m_ch <= u'z')
        || (m_ch >= 0xDF && m_ch <= 0xF6)
        || (m_ch >= 0xF8 && m_ch <= 0xFF);
}

bool Char::isUpper() const noexcept
{
    return (m_ch >= u'A' && m_ch <= u'Z')
        || (m_ch >= 0xC0 && m_ch <= 0xD6)
        || (m_ch >= 0xD8 && m_ch <= 0xDE);
}

bool Char::isLetter() const noexcept
{
    if (isLower() || isUpper())
        return true;
    return m_ch == 0xAA || m_ch == 0xB5 || m_ch == 0xBA || (m_ch >= 0x0100 && !isSpace());
}

bool Char::isLetterOrNumber() const noexcept
{
    return isDigit() || isLetter();
}

Char Char::toLower() const noexcept
{
    if (m_ch >= u'A' && m_ch <= u'Z')
        return Char(static_cast<char16_t>(m_ch + 32));
    if ((m_ch >= 0xC0 && m_ch <= 0xD6) || (m_ch >= 0xD8 && m_ch <= 0xDE))
        return Char(static_cast<char16_t>(m_ch + 32));
    return *this;
}

Char Char::toUpper() const noexcept
{
    if (m_ch >= u'a' && m_ch <= u'z')
        return Char(static_cast<char16_t>(m_ch - 32));
    if ((m_ch >= 0xE0 && m_ch <= 0xF6) || (m_ch >= 0xF8 && m_ch <= 0xFE))
        return Char(static_cast<char16_t>(m_ch - 32));
    return *this;
}

const std::shared_ptr<std::u16string> &String::emptyShared()
{
    static const std::shared_ptr<std::u16string> s_empty = std::make_shared<std::u16string>();
    return s_empty;
}

void String::detach()
{
    if (!m_d) {
        m_d = std::make_shared<std::u16string>();
    } else if (m_d.use_count() > 1) {
        m_d = std::make_shared<std::u16string>(*m_d);
    }
}

String::String(const char *utf8)
{
    if (utf8) {
        std::u16string decoded = decodeUtf8(utf8, -1);
        m_d = decoded.empty() ? emptyShared() : std::make_shared<std::u16string>(std::move(decoded));
    }
}

String::String(const char *utf8, int size)
{
    if (!utf8 && size <= 0)
        return;
    std::u16string decoded = decodeUtf8(utf8, size);
    m_d = decoded.empty() ? emptyShared() : std::make_shared<std::u16string>(std::move(decoded));
}

String::String(const char16_t *utf16)
{
    if (utf16) {
        std::size_t len = std::char_traits<char16_t>::length(utf16);
        m_d = (len == 0) ? emptyShared() : std::make_shared<std::u16string>(utf16, len);
    }
}

String::String(const char16_t *utf16, int size)
{
    if (!utf16 && size <= 0)
        return;
    if (size < 0) {
        std::size_t len = utf16 ? std::char_traits<char16_t>::length(utf16) : 0;
        m_d = (len == 0) ? emptyShared() : std::make_shared<std::u16string>(utf16, len);
    } else if (size == 0) {
        m_d = emptyShared();
    } else {
        m_d = std::make_shared<std::u16string>(utf16, static_cast<std::size_t>(size));
    }
}

String::String(const std::u16string &s)
    : m_d(s.empty() ? emptyShared() : std::make_shared<std::u16string>(s))
{
}

String::String(std::u16string &&s)
    : m_d(s.empty() ? emptyShared() : std::make_shared<std::u16string>(std::move(s)))
{
}

String::String(Char c)
    : m_d(std::make_shared<std::u16string>(1, c.unicode()))
{
}

String::String(Latin1Char c)
    : m_d(std::make_shared<std::u16string>(1, c.unicode()))
{
}

String::String(Latin1String s)
{
    if (s.isNull())
        return;
    if (s.isEmpty()) {
        m_d = emptyShared();
        return;
    }
    auto d = std::make_shared<std::u16string>();
    d->resize(static_cast<std::size_t>(s.size()));
    const auto *src = reinterpret_cast<const unsigned char *>(s.data());
    for (int i = 0; i < s.size(); ++i)
        (*d)[static_cast<std::size_t>(i)] = static_cast<char16_t>(src[i]);
    m_d = std::move(d);
}

const char16_t *String::utf16() const noexcept
{
    static const char16_t s_nullTerm = 0;
    return m_d ? m_d->c_str() : &s_nullTerm;
}

const std::u16string &String::u16String() const noexcept
{
    return m_d ? *m_d : *emptyShared();
}

Char String::at(int i) const noexcept
{
    if (!m_d || i < 0 || i >= static_cast<int>(m_d->size()))
        return Char();
    return Char((*m_d)[static_cast<std::size_t>(i)]);
}

void String::reserve(int n)
{
    if (n > 0) {
        detach();
        m_d->reserve(static_cast<std::size_t>(n));
    }
}

void String::chop(int n)
{
    if (n <= 0 || isEmpty())
        return;
    if (n >= size()) {
        m_d = emptyShared();
    } else {
        detach();
        m_d->resize(static_cast<std::size_t>(size() - n));
    }
}

String &String::append(const String &other)
{
    if (other.isNull())
        return *this;
    if (isNull()) {
        *this = other;
        return *this;
    }
    if (other.isEmpty())
        return *this;
    if (isEmpty()) {
        *this = other;
        return *this;
    }
    detach();
    m_d->append(*other.m_d);
    return *this;
}

String &String::append(Char c)
{
    detach();
    m_d->push_back(c.unicode());
    return *this;
}

String &String::append(Latin1String s)
{
    return append(String(s));
}

String &String::append(const char *utf8)
{
    return append(String::fromUtf8(utf8));
}

String &String::prepend(const String &other)
{
    if (other.isNull())
        return *this;
    if (isNull() || isEmpty()) {
        if (!other.isEmpty() || isNull())
            *this = other;
        return *this;
    }
    if (other.isEmpty())
        return *this;
    detach();
    m_d->insert(0, *other.m_d);
    return *this;
}

String &String::prepend(Char c)
{
    detach();
    m_d->insert(m_d->begin(), c.unicode());
    return *this;
}

String &String::prepend(Latin1String s)
{
    return prepend(String(s));
}

String &String::prepend(const char *utf8)
{
    return prepend(String::fromUtf8(utf8));
}

String String::mid(int pos, int n) const
{
    if (isNull())
        return String();
    int len = size();
    if (pos < 0) {
        if (n < 0 || pos + n >= len) {
            pos = 0;
            n = len;
        } else if (pos + n <= 0) {
            return String();
        } else {
            n += pos;
            pos = 0;
        }
    } else if (pos > len) {
        return String();
    } else if (pos == len) {
        return String(u"", 0);
    }
    if (n < 0 || pos + n > len)
        n = len - pos;
    if (n == 0)
        return String(u"", 0);
    if (pos == 0 && n == len)
        return *this;
    return String(m_d->data() + pos, n);
}

String String::left(int n) const
{
    if (isNull())
        return String();
    if (n < 0 || n >= size())
        return *this;
    if (n == 0)
        return String(u"", 0);
    return String(m_d->data(), n);
}

String String::right(int n) const
{
    if (isNull())
        return String();
    int len = size();
    if (n < 0 || n >= len)
        return *this;
    if (n == 0)
        return String(u"", 0);
    return String(m_d->data() + (len - n), n);
}

String String::trimmed() const
{
    if (isEmpty())
        return *this;
    const char16_t *buf = m_d->data();
    int b = 0;
    int e = size();
    while (b < e && Char(buf[b]).isSpace())
        ++b;
    while (e > b && Char(buf[e - 1]).isSpace())
        --e;
    if (b == 0 && e == size())
        return *this;
    return String(buf + b, e - b);
}

String String::simplified() const
{
    if (isEmpty())
        return *this;
    const char16_t *buf = m_d->data();
    int len = size();
    std::u16string res;
    res.reserve(static_cast<std::size_t>(len));
    int i = 0;
    while (i < len && Char(buf[i]).isSpace())
        ++i;
    while (i < len) {
        if (Char(buf[i]).isSpace()) {
            while (i < len && Char(buf[i]).isSpace())
                ++i;
            if (i < len)
                res.push_back(u' ');
        } else {
            res.push_back(buf[i]);
            ++i;
        }
    }
    return String(std::move(res));
}

String String::toLower() const
{
    if (isEmpty())
        return *this;
    std::u16string res;
    res.resize(m_d->size());
    for (std::size_t i = 0; i < m_d->size(); ++i)
        res[i] = Char((*m_d)[i]).toLower().unicode();
    return String(std::move(res));
}

String String::toUpper() const
{
    if (isEmpty())
        return *this;
    std::u16string res;
    res.resize(m_d->size());
    for (std::size_t i = 0; i < m_d->size(); ++i)
        res[i] = Char((*m_d)[i]).toUpper().unicode();
    return String(std::move(res));
}

String String::leftJustified(int width, Char fill, bool truncate) const
{
    int padlen = width - size();
    if (padlen <= 0) {
        if (truncate && width >= 0)
            return left(width);
        return *this;
    }
    std::u16string res;
    res.reserve(static_cast<std::size_t>(width));
    res.append(view());
    res.append(static_cast<std::size_t>(padlen), fill.unicode());
    return String(std::move(res));
}

String String::rightJustified(int width, Char fill, bool truncate) const
{
    int padlen = width - size();
    if (padlen <= 0) {
        if (truncate && width >= 0)
            return left(width);
        return *this;
    }
    std::u16string res;
    res.reserve(static_cast<std::size_t>(width));
    res.append(static_cast<std::size_t>(padlen), fill.unicode());
    res.append(view());
    return String(std::move(res));
}

int String::indexOf(const String &str, int from, CaseSensitivity cs) const noexcept
{
    int len = size();
    int slen = str.size();
    if (from < 0)
        from += len;
    if (slen == 0)
        return (from >= 0 && from <= len) ? from : -1;
    if (from < 0) {
        if (slen == 1)
            return -1;
        from = 0;
    }
    if (from + slen > len)
        return -1;
    const char16_t *hay = utf16();
    const char16_t *needle = str.utf16();
    for (int i = from; i <= len - slen; ++i) {
        if (rangeEquals(hay + i, needle, slen, cs))
            return i;
    }
    return -1;
}

int String::indexOf(Char c, int from, CaseSensitivity cs) const noexcept
{
    int len = size();
    if (from < 0)
        from += len;
    if (from < 0 || from >= len)
        return -1;
    const char16_t *hay = utf16();
    char16_t target = foldCase(c.unicode(), cs);
    for (int i = from; i < len; ++i) {
        if (foldCase(hay[i], cs) == target)
            return i;
    }
    return -1;
}

int String::indexOf(Latin1String str, int from, CaseSensitivity cs) const noexcept
{
    return indexOf(String(str), from, cs);
}

int String::lastIndexOf(const String &str, CaseSensitivity cs) const noexcept
{
    return lastIndexOf(str, size(), cs);
}

int String::lastIndexOf(const String &str, int from, CaseSensitivity cs) const noexcept
{
    int slen = str.size();
    if (slen == 1)
        return lastIndexOf(Char(str.utf16()[0]), from, cs);
    int len = size();
    if (from < 0)
        from += len;
    if (from - len > 0)
        return -1;
    if (slen == 0)
        return from >= 0 ? from : -1;
    if (from + slen > len)
        from = len - slen;
    if (from < 0)
        return -1;
    const char16_t *hay = utf16();
    const char16_t *needle = str.utf16();
    for (int i = from; i >= 0; --i) {
        if (rangeEquals(hay + i, needle, slen, cs))
            return i;
    }
    return -1;
}

int String::lastIndexOf(Char c, CaseSensitivity cs) const noexcept
{
    return lastIndexOf(c, -1, cs);
}

int String::lastIndexOf(Char c, int from, CaseSensitivity cs) const noexcept
{
    int len = size();
    if (len == 0)
        return -1;
    if (from < 0)
        from += len;
    if (from >= len)
        from = len - 1;
    if (from < 0)
        return -1;
    const char16_t *hay = utf16();
    char16_t target = foldCase(c.unicode(), cs);
    for (int i = from; i >= 0; --i) {
        if (foldCase(hay[i], cs) == target)
            return i;
    }
    return -1;
}

bool String::startsWith(const String &s, CaseSensitivity cs) const noexcept
{
    if (isNull())
        return s.isNull();
    if (s.isEmpty())
        return true;
    if (s.size() > size())
        return false;
    return rangeEquals(utf16(), s.utf16(), s.size(), cs);
}

bool String::startsWith(Char c, CaseSensitivity cs) const noexcept
{
    if (isEmpty())
        return false;
    return foldCase(utf16()[0], cs) == foldCase(c.unicode(), cs);
}

bool String::startsWith(Latin1String s, CaseSensitivity cs) const noexcept
{
    return startsWith(String(s), cs);
}

bool String::endsWith(const String &s, CaseSensitivity cs) const noexcept
{
    if (isNull())
        return s.isNull();
    if (s.isEmpty())
        return true;
    if (s.size() > size())
        return false;
    return rangeEquals(utf16() + (size() - s.size()), s.utf16(), s.size(), cs);
}

bool String::endsWith(Char c, CaseSensitivity cs) const noexcept
{
    if (isEmpty())
        return false;
    return foldCase(utf16()[size() - 1], cs) == foldCase(c.unicode(), cs);
}

bool String::endsWith(Latin1String s, CaseSensitivity cs) const noexcept
{
    return endsWith(String(s), cs);
}

int String::compare(const String &other, CaseSensitivity cs) const noexcept
{
    const char16_t *a = utf16();
    const char16_t *b = other.utf16();
    int alen = size();
    int blen = other.size();
    int minLen = std::min(alen, blen);
    if (cs == CaseSensitive) {
        int cmp = std::char_traits<char16_t>::compare(a, b, static_cast<std::size_t>(minLen));
        if (cmp != 0)
            return cmp;
    } else {
        for (int i = 0; i < minLen; ++i) {
            char16_t ca = Char(a[i]).toLower().unicode();
            char16_t cb = Char(b[i]).toLower().unicode();
            if (ca != cb)
                return ca < cb ? -1 : 1;
        }
    }
    return alen < blen ? -1 : (alen > blen ? 1 : 0);
}

int String::compare(Latin1String other, CaseSensitivity cs) const noexcept
{
    const char16_t *a = utf16();
    const auto *b = reinterpret_cast<const unsigned char *>(other.data());
    int alen = size();
    int blen = other.size();
    int minLen = std::min(alen, blen);
    for (int i = 0; i < minLen; ++i) {
        char16_t ca = foldCase(a[i], cs);
        char16_t cb = foldCase(static_cast<char16_t>(b[i]), cs);
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return alen < blen ? -1 : (alen > blen ? 1 : 0);
}

StringList String::split(Char sep, SplitBehavior behavior, CaseSensitivity cs) const
{
    return split(String(sep), behavior, cs);
}

StringList String::split(const String &sep, SplitBehavior behavior, CaseSensitivity cs) const
{
    std::vector<String> out;
    if (sep.isEmpty()) {
        if (behavior == KeepEmptyParts)
            out.push_back(String(u"", 0));
        for (int i = 0; i < size(); ++i)
            out.push_back(mid(i, 1));
        if (behavior == KeepEmptyParts)
            out.push_back(String(u"", 0));
        return out;
    }

    int start = 0;
    int pos = 0;
    while ((pos = indexOf(sep, start, cs)) != -1) {
        if (pos > start || behavior == KeepEmptyParts)
            out.push_back(mid(start, pos - start));
        start = pos + sep.size();
    }
    if (start < size() || behavior == KeepEmptyParts) {
        if (start == 0 && isNull())
            out.push_back(String());
        else
            out.push_back(mid(start));
    }
    return out;
}

String &String::replace(const String &before, const String &after, CaseSensitivity cs)
{
    if (isNull() && before.isEmpty() && after.isEmpty())
        return *this;
    if (before.isEmpty()) {
        std::u16string res;
        res.reserve(static_cast<std::size_t>(size() + (size() + 1) * after.size()));
        res.append(after.view());
        for (int i = 0; i < size(); ++i) {
            res.push_back(utf16()[i]);
            res.append(after.view());
        }
        m_d = res.empty() ? emptyShared() : std::make_shared<std::u16string>(std::move(res));
        return *this;
    }

    int pos = indexOf(before, 0, cs);
    if (pos == -1)
        return *this;

    std::u16string res;
    res.reserve(static_cast<std::size_t>(size()));
    int last = 0;
    const char16_t *buf = utf16();
    while (pos != -1) {
        res.append(buf + last, static_cast<std::size_t>(pos - last));
        res.append(after.view());
        last = pos + before.size();
        pos = indexOf(before, last, cs);
    }
    res.append(buf + last, static_cast<std::size_t>(size() - last));
    m_d = res.empty() ? emptyShared() : std::make_shared<std::u16string>(std::move(res));
    return *this;
}

String &String::replace(Char before, Char after, CaseSensitivity cs)
{
    if (isEmpty())
        return *this;
    int first = indexOf(before, 0, cs);
    if (first == -1)
        return *this;
    detach();
    char16_t target = foldCase(before.unicode(), cs);
    for (std::size_t i = static_cast<std::size_t>(first); i < m_d->size(); ++i) {
        if (foldCase((*m_d)[i], cs) == target)
            (*m_d)[i] = after.unicode();
    }
    return *this;
}

String &String::replace(int pos, int len, const String &after)
{
    if (pos < 0) {
        len += pos;
        pos = 0;
    }
    if (len < 0)
        len = 0;
    if (pos > size())
        pos = size();
    if (pos + len > size())
        len = size() - pos;
    detach();
    m_d->replace(static_cast<std::size_t>(pos), static_cast<std::size_t>(len), after.view());
    return *this;
}

String &String::remove(int pos, int len)
{
    if (pos < 0) {
        len += pos;
        pos = 0;
    }
    if (len <= 0 || pos >= size())
        return *this;
    if (pos + len > size())
        len = size() - pos;
    detach();
    m_d->erase(static_cast<std::size_t>(pos), static_cast<std::size_t>(len));
    return *this;
}

String &String::remove(const String &str, CaseSensitivity cs)
{
    if (str.isEmpty())
        return *this;
    return replace(str, String(u"", 0), cs);
}

String &String::remove(Char c, CaseSensitivity cs)
{
    return remove(String(c), cs);
}

String String::arg(const String &a, int fieldWidth, Char fillChar) const
{
    if (isEmpty())
        return *this;

    const char16_t *buf = utf16();
    int len = size();
    int bestNum = 100;

    for (int i = 0; i < len - 1; ++i) {
        if (buf[i] == u'%') {
            char16_t c1 = buf[i + 1];
            if (c1 >= u'1' && c1 <= u'9') {
                int num = c1 - u'0';
                if (i + 2 < len && buf[i + 2] >= u'0' && buf[i + 2] <= u'9') {
                    num = num * 10 + (buf[i + 2] - u'0');
                    ++i;
                }
                if (num < bestNum)
                    bestNum = num;
                ++i;
            }
        }
    }

    if (bestNum == 100)
        return *this;

    String padded = padField(a, fieldWidth, fillChar);
    std::u16string res;
    res.reserve(static_cast<std::size_t>(len + padded.size()));

    for (int i = 0; i < len; ++i) {
        if (buf[i] == u'%' && i + 1 < len) {
            char16_t c1 = buf[i + 1];
            if (c1 >= u'1' && c1 <= u'9') {
                int num = c1 - u'0';
                int step = 1;
                if (i + 2 < len && buf[i + 2] >= u'0' && buf[i + 2] <= u'9') {
                    num = num * 10 + (buf[i + 2] - u'0');
                    step = 2;
                }
                if (num == bestNum) {
                    res.append(padded.view());
                    i += step;
                    continue;
                }
            }
        }
        res.push_back(buf[i]);
    }
    return String(std::move(res));
}

String String::arg(Char a, int fieldWidth, Char fillChar) const
{
    return arg(String(a), fieldWidth, fillChar);
}

String String::arg(Latin1String a, int fieldWidth, Char fillChar) const
{
    return arg(String(a), fieldWidth, fillChar);
}

String String::arg(const char *a, int fieldWidth, Char fillChar) const
{
    return arg(String::fromUtf8(a), fieldWidth, fillChar);
}

String String::arg(int a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(unsigned int a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(long a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(unsigned long a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(long long a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(unsigned long long a, int fieldWidth, int base, Char fillChar) const
{
    return arg(number(a, base), fieldWidth, fillChar);
}

String String::arg(double a, int fieldWidth, char format, int precision, Char fillChar) const
{
    return arg(number(a, format, precision < 0 ? 6 : precision), fieldWidth, fillChar);
}

String String::arg(const String &a1, const String &a2) const
{
    return arg(a1).arg(a2);
}

String String::arg(const String &a1, const String &a2, const String &a3) const
{
    return arg(a1).arg(a2).arg(a3);
}

String String::number(int n, int base)
{
    return number(static_cast<long long>(n), base);
}

String String::number(unsigned int n, int base)
{
    return number(static_cast<unsigned long long>(n), base);
}

String String::number(long n, int base)
{
    return number(static_cast<long long>(n), base);
}

String String::number(unsigned long n, int base)
{
    return number(static_cast<unsigned long long>(n), base);
}

String String::number(long long n, int base)
{
    if (base < 2 || base > 36)
        base = 10;
    char buf[72] = {};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n, base);
    if (ec != std::errc())
        return String();
    return String(Latin1String(buf, static_cast<int>(ptr - buf)));
}

String String::number(unsigned long long n, int base)
{
    if (base < 2 || base > 36)
        base = 10;
    char buf[72] = {};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n, base);
    if (ec != std::errc())
        return String();
    return String(Latin1String(buf, static_cast<int>(ptr - buf)));
}

String String::number(double n, char format, int precision)
{
    bool upper = (format == 'E' || format == 'F' || format == 'G');
    if (std::isnan(n))
        return String(Latin1String(upper ? "NAN" : "nan", 3));
    if (std::isinf(n)) {
        if (n < 0)
            return String(Latin1String(upper ? "-INF" : "-inf", 4));
        return String(Latin1String(upper ? "INF" : "inf", 3));
    }

    double norm = (n == 0.0) ? 0.0 : n;
    std::chars_format fmt = std::chars_format::general;
    char lowerFmt = static_cast<char>(format | 0x20);
    if (lowerFmt == 'f') {
        fmt = std::chars_format::fixed;
        if (precision < 0)
            precision = 6;
    } else if (lowerFmt == 'e') {
        fmt = std::chars_format::scientific;
        if (precision < 0)
            precision = 6;
    } else {
        fmt = std::chars_format::general;
        if (precision <= 0)
            precision = 1;
    }

    char buf[512] = {};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), norm, fmt, precision);
    if (ec != std::errc())
        return String();
    if (upper) {
        for (char *p = buf; p < ptr; ++p) {
            if (*p == 'e')
                *p = 'E';
        }
    }
    return String(Latin1String(buf, static_cast<int>(ptr - buf)));
}

long long String::toLongLong(bool *ok, int base) const noexcept
{
    if (ok)
        *ok = false;
    if (base != 0 && (base < 2 || base > 36))
        return 0;
    if (isEmpty())
        return 0;

    const char16_t *buf = utf16();
    int b = 0;
    int e = size();
    while (b < e && Char(buf[b]).isSpace())
        ++b;
    while (e > b && Char(buf[e - 1]).isSpace())
        --e;
    if (b >= e)
        return 0;

    std::string ascii;
    ascii.reserve(static_cast<std::size_t>(e - b));
    for (int i = b; i < e; ++i) {
        if (buf[i] >= 0x80)
            return 0;
        ascii.push_back(static_cast<char>(buf[i]));
    }

    std::size_t idx = 0;
    bool neg = false;
    if (ascii[idx] == '-') {
        neg = true;
        ++idx;
    } else if (ascii[idx] == '+') {
        ++idx;
    }
    if (idx >= ascii.size())
        return 0;

    int actualBase = base;
    if (actualBase == 0) {
        if (ascii[idx] == '0') {
            if (idx + 1 < ascii.size() && (ascii[idx + 1] == 'x' || ascii[idx + 1] == 'X')) {
                actualBase = 16;
                idx += 2;
            } else {
                actualBase = 8;
            }
        } else {
            actualBase = 10;
        }
    } else if (actualBase == 16) {
        if (ascii[idx] == '0' && idx + 1 < ascii.size() && (ascii[idx + 1] == 'x' || ascii[idx + 1] == 'X'))
            idx += 2;
    }

    if (idx >= ascii.size())
        return 0;
    if (ascii[idx] == '-' || ascii[idx] == '+')
        return 0;

    unsigned long long uval = 0;
    const char *first = ascii.data() + idx;
    const char *last = ascii.data() + ascii.size();
    auto [ptr, ec] = std::from_chars(first, last, uval, actualBase);
    if (ec != std::errc() || ptr != last)
        return 0;

    unsigned long long maxPos = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    if (neg) {
        unsigned long long maxNeg = maxPos + 1ULL;
        if (uval > maxNeg)
            return 0;
        if (ok)
            *ok = true;
        if (uval == maxNeg)
            return std::numeric_limits<long long>::min();
        return -static_cast<long long>(uval);
    } else {
        if (uval > maxPos)
            return 0;
        if (ok)
            *ok = true;
        return static_cast<long long>(uval);
    }
}

unsigned long long String::toULongLong(bool *ok, int base) const noexcept
{
    if (ok)
        *ok = false;
    if (base != 0 && (base < 2 || base > 36))
        return 0;
    if (isEmpty())
        return 0;

    const char16_t *buf = utf16();
    int b = 0;
    int e = size();
    while (b < e && Char(buf[b]).isSpace())
        ++b;
    while (e > b && Char(buf[e - 1]).isSpace())
        --e;
    if (b >= e)
        return 0;

    std::string ascii;
    ascii.reserve(static_cast<std::size_t>(e - b));
    for (int i = b; i < e; ++i) {
        if (buf[i] >= 0x80)
            return 0;
        ascii.push_back(static_cast<char>(buf[i]));
    }

    std::size_t idx = 0;
    if (ascii[idx] == '-')
        return 0;
    if (ascii[idx] == '+')
        ++idx;
    if (idx >= ascii.size())
        return 0;

    int actualBase = base;
    if (actualBase == 0) {
        if (ascii[idx] == '0') {
            if (idx + 1 < ascii.size() && (ascii[idx + 1] == 'x' || ascii[idx + 1] == 'X')) {
                actualBase = 16;
                idx += 2;
            } else {
                actualBase = 8;
            }
        } else {
            actualBase = 10;
        }
    } else if (actualBase == 16) {
        if (ascii[idx] == '0' && idx + 1 < ascii.size() && (ascii[idx + 1] == 'x' || ascii[idx + 1] == 'X'))
            idx += 2;
    }

    if (idx >= ascii.size())
        return 0;
    if (ascii[idx] == '-' || ascii[idx] == '+')
        return 0;

    unsigned long long uval = 0;
    const char *first = ascii.data() + idx;
    const char *last = ascii.data() + ascii.size();
    auto [ptr, ec] = std::from_chars(first, last, uval, actualBase);
    if (ec != std::errc() || ptr != last)
        return 0;

    if (ok)
        *ok = true;
    return uval;
}

unsigned int String::toUInt(bool *ok, int base) const noexcept
{
    bool ullOk = false;
    unsigned long long val = toULongLong(&ullOk, base);
    if (!ullOk || val > std::numeric_limits<unsigned int>::max()) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    return static_cast<unsigned int>(val);
}

int String::toInt(bool *ok, int base) const noexcept
{
    bool llOk = false;
    long long val = toLongLong(&llOk, base);
    if (!llOk || val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    return static_cast<int>(val);
}

double String::toDouble(bool *ok) const noexcept
{
    if (ok)
        *ok = false;
    if (isEmpty())
        return 0.0;

    const char16_t *buf = utf16();
    int b = 0;
    int e = size();
    while (b < e && Char(buf[b]).isSpace())
        ++b;
    while (e > b && Char(buf[e - 1]).isSpace())
        --e;
    if (b >= e)
        return 0.0;

    std::string ascii;
    ascii.reserve(static_cast<std::size_t>(e - b));
    for (int i = b; i < e; ++i) {
        if (buf[i] >= 0x80)
            return 0.0;
        ascii.push_back(static_cast<char>(buf[i]));
    }

    std::string lower = ascii;
    for (char &ch : lower) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + 32);
    }
    if (lower == "nan" || lower == "+nan" || lower == "-nan") {
        if (ok)
            *ok = true;
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (lower == "inf" || lower == "+inf") {
        if (ok)
            *ok = true;
        return std::numeric_limits<double>::infinity();
    }
    if (lower == "-inf") {
        if (ok)
            *ok = true;
        return -std::numeric_limits<double>::infinity();
    }
    // Only 3-letter "inf" is accepted; "infinity" and hex floats are rejected.
    if (lower.find("inf") != std::string::npos || lower.find('x') != std::string::npos)
        return 0.0;

    std::size_t idx = 0;
    if (ascii[idx] == '+') {
        ++idx;
        if (idx >= ascii.size() || ascii[idx] == '+' || ascii[idx] == '-')
            return 0.0;
    }

    double val = 0.0;
    const char *first = ascii.data() + idx;
    const char *last = ascii.data() + ascii.size();
    auto [ptr, ec] = std::from_chars(first, last, val, std::chars_format::general);
    if (ptr != last)
        return 0.0;
    if (ec == std::errc::result_out_of_range) {
        return ascii[0] == '-' ? -std::numeric_limits<double>::infinity()
                               : std::numeric_limits<double>::infinity();
    }
    if (ec != std::errc())
        return 0.0;
    if (ok)
        *ok = true;
    return val;
}

std::string String::toUtf8() const
{
    if (isEmpty())
        return std::string();
    return encodeUtf8(view());
}

std::string String::toLatin1() const
{
    if (isEmpty())
        return std::string();
    std::string out;
    out.resize(m_d->size());
    for (std::size_t i = 0; i < m_d->size(); ++i) {
        char16_t c = (*m_d)[i];
        out[i] = (c <= 0xFF) ? static_cast<char>(c) : '?';
    }
    return out;
}

String String::fromUtf8(const char *utf8, int size)
{
    return String(utf8, size);
}

String String::fromUtf8(const std::string &utf8)
{
    return String(utf8.data(), static_cast<int>(utf8.size()));
}

String String::fromLatin1(const char *latin1, int size)
{
    if (!latin1 && size <= 0)
        return String();
    int len = (size < 0) ? (latin1 ? static_cast<int>(std::strlen(latin1)) : 0) : size;
    return String(Latin1String(latin1 ? latin1 : "", len));
}

String String::fromLatin1(const std::string &latin1)
{
    return String(Latin1String(latin1.data(), static_cast<int>(latin1.size())));
}

String String::fromUtf16(const char16_t *utf16, int size)
{
    return String(utf16, size);
}

String operator+(const String &a, const String &b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(const String &a, Char b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(Char a, const String &b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(const String &a, Latin1String b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(Latin1String a, const String &b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(const String &a, const char *b)
{
    String res(a);
    res.append(b);
    return res;
}

String operator+(const char *a, const String &b)
{
    String res = String::fromUtf8(a);
    res.append(b);
    return res;
}

bool StringList::removeOne(const String &s)
{
    int idx = indexOf(s);
    if (idx < 0)
        return false;
    Base::erase(Base::begin() + idx);
    return true;
}

int StringList::removeAll(const String &s)
{
    int oldSize = size();
    Base::erase(std::remove(Base::begin(), Base::end(), s), Base::end());
    return oldSize - size();
}

int StringList::removeDuplicates()
{
    int n = size();
    int j = 0;
    for (int i = 0; i < n; ++i) {
        bool seen = false;
        for (int k = 0; k < j; ++k) {
            if ((*this)[static_cast<std::size_t>(k)] == (*this)[static_cast<std::size_t>(i)]) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            if (j != i)
                (*this)[static_cast<std::size_t>(j)] = std::move((*this)[static_cast<std::size_t>(i)]);
            ++j;
        }
    }
    if (j != n)
        Base::resize(static_cast<std::size_t>(j));
    return n - j;
}

int StringList::indexOf(const String &s, int from, CaseSensitivity cs) const noexcept
{
    int n = size();
    if (from < 0)
        from = std::max(from + n, 0);
    for (int i = from; i < n; ++i) {
        if ((*this)[static_cast<std::size_t>(i)].compare(s, cs) == 0)
            return i;
    }
    return -1;
}

int StringList::lastIndexOf(const String &s, int from, CaseSensitivity cs) const noexcept
{
    int n = size();
    if (from < 0)
        from += n;
    else if (from >= n)
        from = n - 1;
    for (int i = from; i >= 0; --i) {
        if ((*this)[static_cast<std::size_t>(i)].compare(s, cs) == 0)
            return i;
    }
    return -1;
}

String StringList::join(const String &sep) const
{
    if (Base::empty())
        return String();
    std::size_t total = 0;
    for (std::size_t i = 0; i < Base::size(); ++i) {
        if (i > 0)
            total += static_cast<std::size_t>(sep.size());
        total += static_cast<std::size_t>((*this)[i].size());
    }
    if (total == 0)
        return String(u"", 0);
    std::u16string res;
    res.reserve(total);
    for (std::size_t i = 0; i < Base::size(); ++i) {
        if (i > 0)
            res.append(sep.view());
        res.append((*this)[i].view());
    }
    return String(std::move(res));
}

StringList StringList::mid(int pos, int len) const
{
    int n = size();
    if (pos < 0) {
        if (len < 0 || pos + len >= n) {
            pos = 0;
            len = n;
        } else if (pos + len <= 0) {
            return StringList();
        } else {
            len += pos;
            pos = 0;
        }
    }
    if (pos >= n)
        return StringList();
    if (len < 0 || pos + len > n)
        len = n - pos;
    if (len <= 0)
        return StringList();
    return StringList(Base::begin() + pos, Base::begin() + pos + len);
}

void StringList::sort(CaseSensitivity cs)
{
    std::sort(Base::begin(), Base::end(), [cs](const String &a, const String &b) {
        return a.compare(b, cs) < 0;
    });
}

StringList StringList::filter(const String &str, CaseSensitivity cs) const
{
    StringList out;
    for (const String &item : *this) {
        if (item.contains(str, cs))
            out.append(item);
    }
    return out;
}

} // namespace AnimeCore
