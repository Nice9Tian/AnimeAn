#pragma once

#include "algorithm/core/bridge.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace AnimeCore {

enum CaseSensitivity {
    CaseInsensitive = 0,
    CaseSensitive = 1
};

enum SplitBehavior {
    KeepEmptyParts = 0,
    SkipEmptyParts = 1
};

class Latin1Char
{
public:
    constexpr explicit Latin1Char(char c) noexcept
        : m_ch(static_cast<unsigned char>(c))
    {
    }

    constexpr char toLatin1() const noexcept { return static_cast<char>(m_ch); }
    constexpr char16_t unicode() const noexcept { return static_cast<char16_t>(m_ch); }

private:
    unsigned char m_ch;
};

class Char
{
public:
    constexpr Char() noexcept : m_ch(0) {}
    constexpr Char(char16_t c) noexcept : m_ch(c) {}
    constexpr Char(Latin1Char c) noexcept : m_ch(c.unicode()) {}
    constexpr Char(char c) noexcept : m_ch(static_cast<unsigned char>(c)) {}
    constexpr Char(int c) noexcept : m_ch(static_cast<char16_t>(c)) {}

    constexpr char16_t unicode() const noexcept { return m_ch; }
    constexpr char toLatin1() const noexcept { return m_ch <= 0xFF ? static_cast<char>(m_ch) : '\0'; }
    constexpr bool isNull() const noexcept { return m_ch == 0; }

    bool isSpace() const noexcept;
    bool isDigit() const noexcept;
    bool isLetter() const noexcept;
    bool isLetterOrNumber() const noexcept;
    bool isLower() const noexcept;
    bool isUpper() const noexcept;
    bool isHighSurrogate() const noexcept { return m_ch >= 0xD800 && m_ch <= 0xDBFF; }
    bool isLowSurrogate() const noexcept { return m_ch >= 0xDC00 && m_ch <= 0xDFFF; }
    bool isSurrogate() const noexcept { return m_ch >= 0xD800 && m_ch <= 0xDFFF; }

    Char toLower() const noexcept;
    Char toUpper() const noexcept;

    friend constexpr bool operator==(Char a, Char b) noexcept { return a.m_ch == b.m_ch; }
    friend constexpr bool operator!=(Char a, Char b) noexcept { return a.m_ch != b.m_ch; }
    friend constexpr bool operator<(Char a, Char b) noexcept { return a.m_ch < b.m_ch; }
    friend constexpr bool operator<=(Char a, Char b) noexcept { return a.m_ch <= b.m_ch; }
    friend constexpr bool operator>(Char a, Char b) noexcept { return a.m_ch > b.m_ch; }
    friend constexpr bool operator>=(Char a, Char b) noexcept { return a.m_ch >= b.m_ch; }

private:
    char16_t m_ch;
};

class Latin1String
{
public:
    constexpr Latin1String() noexcept : m_data(nullptr), m_size(0) {}
    explicit Latin1String(const char *s) noexcept
        : m_data(s), m_size(s ? static_cast<int>(std::strlen(s)) : 0)
    {
    }
    constexpr Latin1String(const char *s, int n) noexcept
        : m_data(s), m_size(n >= 0 ? n : 0)
    {
    }

    constexpr const char *data() const noexcept { return m_data; }
    constexpr const char *latin1() const noexcept { return m_data; }
    constexpr int size() const noexcept { return m_size; }
    constexpr int length() const noexcept { return m_size; }
    constexpr bool isNull() const noexcept { return m_data == nullptr; }
    constexpr bool isEmpty() const noexcept { return m_size == 0; }

private:
    const char *m_data;
    int m_size;
};

// Implicitly shared UTF-16 string. A null m_d represents the null state;
// a non-null m_d pointing to an empty u16string represents an empty but non-null string.
// This keeps copying O(1) for multi-megabyte scriptData buffers while preserving
// UTF-16 code-unit indexing in size/mid/left/right.
class String
{
public:
    using value_type = char16_t;
    using size_type = int;

    String() noexcept = default;
    String(const String &) noexcept = default;
    String(String &&) noexcept = default;
    String &operator=(const String &) noexcept = default;
    String &operator=(String &&) noexcept = default;
    ~String() = default;

    String(const char *utf8);
    String(const char *utf8, int size);
    String(const char16_t *utf16);
    String(const char16_t *utf16, int size);
    String(const std::u16string &s);
    String(std::u16string &&s);
    String(Char c);
    String(Latin1Char c);
    String(Latin1String s);

    template <typename T, typename = std::enable_if_t<BridgesToCore<T, String>::value>>
    String(const T &foreign)
        : String(Bridge<T>::toCore(foreign))
    {
    }

    template <typename T, typename = std::enable_if_t<BridgesFromCore<T, String>::value>>
    operator T() const
    {
        return Bridge<T>::fromCore(*this);
    }

    bool isNull() const noexcept { return !m_d; }
    bool isEmpty() const noexcept { return !m_d || m_d->empty(); }
    int size() const noexcept { return m_d ? static_cast<int>(m_d->size()) : 0; }
    int length() const noexcept { return size(); }
    void clear() noexcept { m_d.reset(); }

    const char16_t *utf16() const noexcept;
    const char16_t *constData() const noexcept { return utf16(); }
    const char16_t *data() const noexcept { return utf16(); }
    const std::u16string &u16String() const noexcept;
    std::u16string_view view() const noexcept
    {
        return m_d ? std::u16string_view(*m_d) : std::u16string_view();
    }

    Char at(int i) const noexcept;
    Char operator[](int i) const noexcept { return at(i); }
    Char front() const noexcept { return at(0); }
    Char back() const noexcept { return at(size() - 1); }

    const char16_t *begin() const noexcept { return utf16(); }
    const char16_t *end() const noexcept { return utf16() + size(); }
    const char16_t *cbegin() const noexcept { return begin(); }
    const char16_t *cend() const noexcept { return end(); }

    void reserve(int n);
    void chop(int n);

    String &append(const String &other);
    String &append(Char c);
    String &append(Latin1Char c) { return append(Char(c)); }
    String &append(Latin1String s);
    String &append(const char *utf8);

    String &prepend(const String &other);
    String &prepend(Char c);
    String &prepend(Latin1Char c) { return prepend(Char(c)); }
    String &prepend(Latin1String s);
    String &prepend(const char *utf8);

    String &operator+=(const String &other) { return append(other); }
    String &operator+=(Char c) { return append(c); }
    String &operator+=(Latin1Char c) { return append(c); }
    String &operator+=(Latin1String s) { return append(s); }
    String &operator+=(const char *utf8) { return append(utf8); }

    String mid(int pos, int n = -1) const;
    String left(int n) const;
    String right(int n) const;
    String trimmed() const;
    String simplified() const;
    String toLower() const;
    String toUpper() const;

    int indexOf(const String &str, int from = 0, CaseSensitivity cs = CaseSensitive) const noexcept;
    int indexOf(Char c, int from = 0, CaseSensitivity cs = CaseSensitive) const noexcept;
    int indexOf(Latin1Char c, int from = 0, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return indexOf(Char(c), from, cs);
    }
    int indexOf(Latin1String str, int from = 0, CaseSensitivity cs = CaseSensitive) const noexcept;

    int lastIndexOf(const String &str, CaseSensitivity cs = CaseSensitive) const noexcept;
    int lastIndexOf(const String &str, int from, CaseSensitivity cs = CaseSensitive) const noexcept;
    int lastIndexOf(Char c, CaseSensitivity cs = CaseSensitive) const noexcept;
    int lastIndexOf(Char c, int from, CaseSensitivity cs = CaseSensitive) const noexcept;
    int lastIndexOf(Latin1Char c, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return lastIndexOf(Char(c), cs);
    }
    int lastIndexOf(Latin1Char c, int from, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return lastIndexOf(Char(c), from, cs);
    }

    bool startsWith(const String &s, CaseSensitivity cs = CaseSensitive) const noexcept;
    bool startsWith(Char c, CaseSensitivity cs = CaseSensitive) const noexcept;
    bool startsWith(Latin1Char c, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return startsWith(Char(c), cs);
    }
    bool startsWith(Latin1String s, CaseSensitivity cs = CaseSensitive) const noexcept;

    bool endsWith(const String &s, CaseSensitivity cs = CaseSensitive) const noexcept;
    bool endsWith(Char c, CaseSensitivity cs = CaseSensitive) const noexcept;
    bool endsWith(Latin1Char c, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return endsWith(Char(c), cs);
    }
    bool endsWith(Latin1String s, CaseSensitivity cs = CaseSensitive) const noexcept;

    bool contains(const String &s, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return indexOf(s, 0, cs) != -1;
    }
    bool contains(Char c, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return indexOf(c, 0, cs) != -1;
    }
    bool contains(Latin1Char c, CaseSensitivity cs = CaseSensitive) const noexcept
    {
        return indexOf(Char(c), 0, cs) != -1;
    }

    int compare(const String &other, CaseSensitivity cs = CaseSensitive) const noexcept;
    int compare(Latin1String other, CaseSensitivity cs = CaseSensitive) const noexcept;
    static int compare(const String &a, const String &b, CaseSensitivity cs = CaseSensitive) noexcept
    {
        return a.compare(b, cs);
    }

    std::vector<String> split(Char sep, SplitBehavior behavior = KeepEmptyParts,
                              CaseSensitivity cs = CaseSensitive) const;
    std::vector<String> split(Latin1Char sep, SplitBehavior behavior = KeepEmptyParts,
                              CaseSensitivity cs = CaseSensitive) const
    {
        return split(Char(sep), behavior, cs);
    }
    std::vector<String> split(const String &sep, SplitBehavior behavior = KeepEmptyParts,
                              CaseSensitivity cs = CaseSensitive) const;

    String &replace(const String &before, const String &after, CaseSensitivity cs = CaseSensitive);
    String &replace(Char before, Char after, CaseSensitivity cs = CaseSensitive);
    String &replace(Latin1Char before, Latin1Char after, CaseSensitivity cs = CaseSensitive)
    {
        return replace(Char(before), Char(after), cs);
    }
    String &replace(int pos, int len, const String &after);

    String &remove(int pos, int len);
    String &remove(const String &str, CaseSensitivity cs = CaseSensitive);
    String &remove(Char c, CaseSensitivity cs = CaseSensitive);
    String &remove(Latin1Char c, CaseSensitivity cs = CaseSensitive)
    {
        return remove(Char(c), cs);
    }

    String arg(const String &a, int fieldWidth = 0, Char fillChar = Char(u' ')) const;
    String arg(Char a, int fieldWidth = 0, Char fillChar = Char(u' ')) const;
    String arg(Latin1Char a, int fieldWidth = 0, Char fillChar = Char(u' ')) const
    {
        return arg(Char(a), fieldWidth, fillChar);
    }
    String arg(Latin1String a, int fieldWidth = 0, Char fillChar = Char(u' ')) const;
    String arg(const char *a, int fieldWidth = 0, Char fillChar = Char(u' ')) const;
    String arg(int a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(unsigned int a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(long a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(unsigned long a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(long long a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(unsigned long long a, int fieldWidth = 0, int base = 10, Char fillChar = Char(u' ')) const;
    String arg(double a, int fieldWidth = 0, char format = 'g', int precision = -1,
               Char fillChar = Char(u' ')) const;
    String arg(const String &a1, const String &a2) const;
    String arg(const String &a1, const String &a2, const String &a3) const;

    static String number(int n, int base = 10);
    static String number(unsigned int n, int base = 10);
    static String number(long n, int base = 10);
    static String number(unsigned long n, int base = 10);
    static String number(long long n, int base = 10);
    static String number(unsigned long long n, int base = 10);
    static String number(double n, char format = 'g', int precision = 6);

    int toInt(bool *ok = nullptr, int base = 10) const noexcept;
    long long toLongLong(bool *ok = nullptr, int base = 10) const noexcept;
    double toDouble(bool *ok = nullptr) const noexcept;

    std::string toUtf8() const;
    std::string toStdString() const { return toUtf8(); }
    std::string toLatin1() const;

    static String fromUtf8(const char *utf8, int size = -1);
    static String fromUtf8(const std::string &utf8);
    static String fromStdString(const std::string &s) { return fromUtf8(s); }
    static String fromLatin1(const char *latin1, int size = -1);
    static String fromLatin1(const std::string &latin1);
    static String fromUtf16(const char16_t *utf16, int size = -1);

    friend bool operator==(const String &a, const String &b) noexcept { return a.view() == b.view(); }
    friend bool operator!=(const String &a, const String &b) noexcept { return a.view() != b.view(); }
    friend bool operator<(const String &a, const String &b) noexcept { return a.view() < b.view(); }
    friend bool operator<=(const String &a, const String &b) noexcept { return a.view() <= b.view(); }
    friend bool operator>(const String &a, const String &b) noexcept { return a.view() > b.view(); }
    friend bool operator>=(const String &a, const String &b) noexcept { return a.view() >= b.view(); }

    friend bool operator==(const String &a, const char *b) noexcept { return a == String::fromUtf8(b); }
    friend bool operator!=(const String &a, const char *b) noexcept { return a != String::fromUtf8(b); }
    friend bool operator==(const char *a, const String &b) noexcept { return String::fromUtf8(a) == b; }
    friend bool operator!=(const char *a, const String &b) noexcept { return String::fromUtf8(a) != b; }

    friend bool operator==(const String &a, Latin1String b) noexcept { return a.compare(b) == 0; }
    friend bool operator!=(const String &a, Latin1String b) noexcept { return a.compare(b) != 0; }
    friend bool operator==(Latin1String a, const String &b) noexcept { return b.compare(a) == 0; }
    friend bool operator!=(Latin1String a, const String &b) noexcept { return b.compare(a) != 0; }

    friend String operator+(const String &a, const String &b);
    friend String operator+(const String &a, Char b);
    friend String operator+(Char a, const String &b);
    friend String operator+(const String &a, Latin1Char b) { return a + Char(b); }
    friend String operator+(Latin1Char a, const String &b) { return Char(a) + b; }
    friend String operator+(const String &a, Latin1String b);
    friend String operator+(Latin1String a, const String &b);
    friend String operator+(const String &a, const char *b);
    friend String operator+(const char *a, const String &b);

private:
    void detach();
    static const std::shared_ptr<std::u16string> &emptyShared();

    std::shared_ptr<std::u16string> m_d;
};

using StringList = std::vector<String>;

} // namespace AnimeCore

#define ANIME_STR(lit) ::AnimeCore::String::fromUtf8((lit), static_cast<int>(sizeof(lit) - 1))

namespace std {
template <>
struct hash<AnimeCore::String>
{
    std::size_t operator()(const AnimeCore::String &s) const noexcept
    {
        return std::hash<std::u16string_view>{}(s.view());
    }
};
} // namespace std
