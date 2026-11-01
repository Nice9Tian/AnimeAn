#ifndef ANIMECORE_COLOR_H
#define ANIMECORE_COLOR_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "bridge.h"
#include "geometry.h"

namespace AnimeCore {

namespace detail {

constexpr int parseHexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

constexpr bool parseHexByte(char hi, char lo, int *out) noexcept
{
    const int h = parseHexDigit(hi);
    const int l = parseHexDigit(lo);
    if (h < 0 || l < 0)
        return false;
    *out = (h << 4) | l;
    return true;
}

inline char toHexLower(int nibble) noexcept
{
    constexpr char kDigits[] = "0123456789abcdef";
    return kDigits[nibble & 0x0f];
}

} // namespace detail

class Color
{
public:
    enum NameFormat {
        HexRgb,
        HexArgb
    };

    class NameString : public std::string
    {
    public:
        using std::string::string;
        NameString() = default;
        NameString(std::string &&s) noexcept : std::string(std::move(s)) {}
        NameString(const std::string &s) : std::string(s) {}

        template <class S,
                  std::enable_if_t<!std::is_same_v<std::decay_t<S>, NameString> &&
                                   !std::is_same_v<std::decay_t<S>, std::string> &&
                                   !std::is_same_v<std::decay_t<S>, std::string_view>, int> = 0,
                  class = decltype(S::fromLatin1(std::declval<const char *>(), std::declval<int>()))>
        operator S() const
        {
            return S::fromLatin1(this->data(), static_cast<int>(this->size()));
        }

        template <class S,
                  class = decltype(std::declval<const S &>().toStdString())>
        friend bool operator==(const NameString &lhs, const S &rhs)
        {
            return static_cast<const std::string &>(lhs) == rhs.toStdString();
        }

        template <class S,
                  class = decltype(std::declval<const S &>().toStdString())>
        friend bool operator==(const S &lhs, const NameString &rhs)
        {
            return lhs.toStdString() == static_cast<const std::string &>(rhs);
        }

        template <class S,
                  class = decltype(std::declval<const S &>().toStdString())>
        friend bool operator!=(const NameString &lhs, const S &rhs)
        {
            return !(lhs == rhs);
        }

        template <class S,
                  class = decltype(std::declval<const S &>().toStdString())>
        friend bool operator!=(const S &lhs, const NameString &rhs)
        {
            return !(lhs == rhs);
        }
    };

    constexpr Color() noexcept
        : m_a(0xffff), m_r(0), m_g(0), m_b(0), m_valid(false)
    {}

    constexpr Color(int r, int g, int b, int a = 255) noexcept
        : m_a(0xffff), m_r(0), m_g(0), m_b(0), m_valid(false)
    {
        setRgb(r, g, b, a);
    }

    explicit constexpr Color(std::uint32_t rgb) noexcept
        : m_a(0xffff),
          m_r(static_cast<std::uint16_t>(((rgb >> 16) & 0xff) * 0x101)),
          m_g(static_cast<std::uint16_t>(((rgb >> 8) & 0xff) * 0x101)),
          m_b(static_cast<std::uint16_t>((rgb & 0xff) * 0x101)),
          m_valid(true)
    {}

    Color(const char *name) noexcept
        : m_a(0xffff), m_r(0), m_g(0), m_b(0), m_valid(false)
    {
        if (name)
            setFromString(std::string_view(name));
    }

    Color(std::string_view name) noexcept
        : m_a(0xffff), m_r(0), m_g(0), m_b(0), m_valid(false)
    {
        setFromString(name);
    }

    Color(const std::string &name) noexcept
        : Color(std::string_view(name))
    {}

    template <class S,
              std::enable_if_t<!BridgesToCore<S, Color>::value &&
                               !std::is_convertible_v<S, std::string_view>, int> = 0,
              class = decltype(std::declval<const S &>().toStdString())>
    Color(const S &name)
        : Color(std::string_view(name.toStdString()))
    {}

    template <class F, std::enable_if_t<BridgesToCore<F, Color>::value, int> = 0>
    constexpr Color(const F &f) : Color(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Color>::value, int> = 0>
    constexpr operator F() const { return Bridge<F>::fromCore(*this); }

    constexpr bool isValid() const noexcept { return m_valid; }

    constexpr int red() const noexcept { return m_r >> 8; }
    constexpr int green() const noexcept { return m_g >> 8; }
    constexpr int blue() const noexcept { return m_b >> 8; }
    constexpr int alpha() const noexcept { return m_a >> 8; }

    constexpr void setRed(int red) noexcept
    {
        if (red < 0 || red > 255) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_g = 0;
            m_b = 0;
        }
        m_r = static_cast<std::uint16_t>(red * 0x101);
    }

    constexpr void setGreen(int green) noexcept
    {
        if (green < 0 || green > 255) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_r = 0;
            m_b = 0;
        }
        m_g = static_cast<std::uint16_t>(green * 0x101);
    }

    constexpr void setBlue(int blue) noexcept
    {
        if (blue < 0 || blue > 255) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_r = 0;
            m_g = 0;
        }
        m_b = static_cast<std::uint16_t>(blue * 0x101);
    }

    constexpr void setAlpha(int alpha) noexcept
    {
        if (alpha < 0 || alpha > 255)
            return;
        m_a = static_cast<std::uint16_t>(alpha * 0x101);
    }

    constexpr float redF() const noexcept
    {
        return static_cast<float>(m_r) / 65535.0f;
    }

    constexpr float greenF() const noexcept
    {
        return static_cast<float>(m_g) / 65535.0f;
    }

    constexpr float blueF() const noexcept
    {
        return static_cast<float>(m_b) / 65535.0f;
    }

    constexpr float alphaF() const noexcept
    {
        return static_cast<float>(m_a) / 65535.0f;
    }

    void setRedF(float red) noexcept
    {
        if (red < 0.0f || red > 1.0f) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_g = 0;
            m_b = 0;
        }
        m_r = static_cast<std::uint16_t>(round(red * 65535.0));
    }

    void setGreenF(float green) noexcept
    {
        if (green < 0.0f || green > 1.0f) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_r = 0;
            m_b = 0;
        }
        m_g = static_cast<std::uint16_t>(round(green * 65535.0));
    }

    void setBlueF(float blue) noexcept
    {
        if (blue < 0.0f || blue > 1.0f) {
            invalidate();
            return;
        }
        if (!m_valid) {
            m_valid = true;
            m_a = 0xffff;
            m_r = 0;
            m_g = 0;
        }
        m_b = static_cast<std::uint16_t>(round(blue * 65535.0));
    }

    void setAlphaF(float alpha) noexcept
    {
        if (alpha < 0.0f || alpha > 1.0f)
            return;
        m_a = static_cast<std::uint16_t>(round(alpha * 65535.0));
    }

    constexpr void getRgb(int *r, int *g, int *b, int *a = nullptr) const noexcept
    {
        if (!r || !g || !b)
            return;
        if (!m_valid) {
            *r = -1;
            *g = -1;
            *b = -1;
            if (a)
                *a = -1;
            return;
        }
        *r = m_r >> 8;
        *g = m_g >> 8;
        *b = m_b >> 8;
        if (a)
            *a = m_a >> 8;
    }

    constexpr void setRgb(int r, int g, int b, int a = 255) noexcept
    {
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || a < 0 || a > 255) {
            invalidate();
            return;
        }
        m_r = static_cast<std::uint16_t>(r * 0x101);
        m_g = static_cast<std::uint16_t>(g * 0x101);
        m_b = static_cast<std::uint16_t>(b * 0x101);
        m_a = static_cast<std::uint16_t>(a * 0x101);
        m_valid = true;
    }

    constexpr void setRgb(std::uint32_t rgb) noexcept
    {
        m_r = static_cast<std::uint16_t>(((rgb >> 16) & 0xff) * 0x101);
        m_g = static_cast<std::uint16_t>(((rgb >> 8) & 0xff) * 0x101);
        m_b = static_cast<std::uint16_t>((rgb & 0xff) * 0x101);
        m_a = 0xffff;
        m_valid = true;
    }

    constexpr void setRgba(std::uint32_t rgba) noexcept
    {
        m_a = static_cast<std::uint16_t>(((rgba >> 24) & 0xff) * 0x101);
        m_r = static_cast<std::uint16_t>(((rgba >> 16) & 0xff) * 0x101);
        m_g = static_cast<std::uint16_t>(((rgba >> 8) & 0xff) * 0x101);
        m_b = static_cast<std::uint16_t>((rgba & 0xff) * 0x101);
        m_valid = true;
    }

    constexpr std::uint32_t rgba() const noexcept
    {
        return (static_cast<std::uint32_t>(m_a >> 8) << 24)
             | (static_cast<std::uint32_t>(m_r >> 8) << 16)
             | (static_cast<std::uint32_t>(m_g >> 8) << 8)
             | static_cast<std::uint32_t>(m_b >> 8);
    }

    constexpr std::uint32_t rgb() const noexcept
    {
        return 0xff000000u
             | (static_cast<std::uint32_t>(m_r >> 8) << 16)
             | (static_cast<std::uint32_t>(m_g >> 8) << 8)
             | static_cast<std::uint32_t>(m_b >> 8);
    }

    static constexpr Color fromRgb(std::uint32_t rgb) noexcept
    {
        return Color(static_cast<int>((rgb >> 16) & 0xff),
                     static_cast<int>((rgb >> 8) & 0xff),
                     static_cast<int>(rgb & 0xff),
                     255);
    }

    static constexpr Color fromRgb(int r, int g, int b, int a = 255) noexcept
    {
        return Color(r, g, b, a);
    }

    static constexpr Color fromRgba(std::uint32_t rgba) noexcept
    {
        return Color(static_cast<int>((rgba >> 16) & 0xff),
                     static_cast<int>((rgba >> 8) & 0xff),
                     static_cast<int>(rgba & 0xff),
                     static_cast<int>((rgba >> 24) & 0xff));
    }

    static Color fromRgbF(float r, float g, float b, float a = 1.0f) noexcept
    {
        if (r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f ||
            b < 0.0f || b > 1.0f || a < 0.0f || a > 1.0f) {
            return Color();
        }
        Color c;
        c.m_r = static_cast<std::uint16_t>(round(r * 65535.0));
        c.m_g = static_cast<std::uint16_t>(round(g * 65535.0));
        c.m_b = static_cast<std::uint16_t>(round(b * 65535.0));
        c.m_a = static_cast<std::uint16_t>(round(a * 65535.0));
        c.m_valid = true;
        return c;
    }

    NameString name(NameFormat format = HexRgb) const
    {
        const int r = m_r >> 8;
        const int g = m_g >> 8;
        const int b = m_b >> 8;
        if (format == HexRgb) {
            std::string out(7, '#');
            out[1] = detail::toHexLower(r >> 4);
            out[2] = detail::toHexLower(r & 0x0f);
            out[3] = detail::toHexLower(g >> 4);
            out[4] = detail::toHexLower(g & 0x0f);
            out[5] = detail::toHexLower(b >> 4);
            out[6] = detail::toHexLower(b & 0x0f);
            return NameString(std::move(out));
        }
        if (format == HexArgb) {
            const int a = m_a >> 8;
            std::string out(9, '#');
            out[1] = detail::toHexLower(a >> 4);
            out[2] = detail::toHexLower(a & 0x0f);
            out[3] = detail::toHexLower(r >> 4);
            out[4] = detail::toHexLower(r & 0x0f);
            out[5] = detail::toHexLower(g >> 4);
            out[6] = detail::toHexLower(g & 0x0f);
            out[7] = detail::toHexLower(b >> 4);
            out[8] = detail::toHexLower(b & 0x0f);
            return NameString(std::move(out));
        }
        return NameString();
    }

    constexpr std::uint16_t rawAlpha() const noexcept { return m_a; }
    constexpr std::uint16_t rawRed() const noexcept { return m_r; }
    constexpr std::uint16_t rawGreen() const noexcept { return m_g; }
    constexpr std::uint16_t rawBlue() const noexcept { return m_b; }

    static constexpr Color fromRawRgba16(std::uint16_t r, std::uint16_t g,
                                         std::uint16_t b, std::uint16_t a,
                                         bool valid = true) noexcept
    {
        Color c;
        if (valid) {
            c.m_r = r;
            c.m_g = g;
            c.m_b = b;
            c.m_a = a;
            c.m_valid = true;
        }
        return c;
    }

    friend constexpr bool operator==(const Color &c1, const Color &c2) noexcept
    {
        if (!c1.m_valid && !c2.m_valid)
            return true;
        return c1.m_valid == c2.m_valid
            && c1.m_a == c2.m_a
            && c1.m_r == c2.m_r
            && c1.m_g == c2.m_g
            && c1.m_b == c2.m_b;
    }

    friend constexpr bool operator!=(const Color &c1, const Color &c2) noexcept
    {
        return !(c1 == c2);
    }

private:
    constexpr void invalidate() noexcept
    {
        m_valid = false;
        m_a = 0;
        m_r = 0;
        m_g = 0;
        m_b = 0;
    }

    constexpr void setFromString(std::string_view str) noexcept
    {
        m_valid = false;
        m_a = 0xffff;
        m_r = 0;
        m_g = 0;
        m_b = 0;
        if (str.empty())
            return;

        if (str[0] == '#') {
            const std::string_view hex = str.substr(1);
            if (hex.size() == 6) {
                int r = 0, g = 0, b = 0;
                if (detail::parseHexByte(hex[0], hex[1], &r) &&
                    detail::parseHexByte(hex[2], hex[3], &g) &&
                    detail::parseHexByte(hex[4], hex[5], &b)) {
                    setRgb(r, g, b, 255);
                }
            } else if (hex.size() == 8) {
                int a = 0, r = 0, g = 0, b = 0;
                if (detail::parseHexByte(hex[0], hex[1], &a) &&
                    detail::parseHexByte(hex[2], hex[3], &r) &&
                    detail::parseHexByte(hex[4], hex[5], &g) &&
                    detail::parseHexByte(hex[6], hex[7], &b)) {
                    setRgb(r, g, b, a);
                }
            } else if (hex.size() == 3) {
                const int r = detail::parseHexDigit(hex[0]);
                const int g = detail::parseHexDigit(hex[1]);
                const int b = detail::parseHexDigit(hex[2]);
                if (r >= 0 && g >= 0 && b >= 0)
                    setRgb((r << 4) | r, (g << 4) | g, (b << 4) | b, 255);
            }
            return;
        }
    }

    std::uint16_t m_a;
    std::uint16_t m_r;
    std::uint16_t m_g;
    std::uint16_t m_b;
    bool m_valid;
};

} // namespace AnimeCore

#endif // ANIMECORE_COLOR_H
