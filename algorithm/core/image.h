#ifndef ANIMECORE_IMAGE_H
#define ANIMECORE_IMAGE_H

#include "algorithm/core/bridge.h"
#include "algorithm/core/geometry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace AnimeCore {

class Color;
class String;

template <class T = unsigned int, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr int qRed(T rgb)
{
    return int((static_cast<unsigned int>(rgb) >> 16) & 0xffu);
}

template <class T = unsigned int, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr int qGreen(T rgb)
{
    return int((static_cast<unsigned int>(rgb) >> 8) & 0xffu);
}

template <class T = unsigned int, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr int qBlue(T rgb)
{
    return int(static_cast<unsigned int>(rgb) & 0xffu);
}

template <class T = unsigned int, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr int qAlpha(T rgb)
{
    return int((static_cast<unsigned int>(rgb) >> 24) & 0xffu);
}

template <class I = int, std::enable_if_t<std::is_integral_v<I>, int> = 0>
constexpr unsigned int qRgb(I r, I g, I b)
{
    return (0xffu << 24) | ((static_cast<unsigned int>(r) & 0xffu) << 16)
           | ((static_cast<unsigned int>(g) & 0xffu) << 8)
           | (static_cast<unsigned int>(b) & 0xffu);
}

template <class I = int, std::enable_if_t<std::is_integral_v<I>, int> = 0>
constexpr unsigned int qRgba(I r, I g, I b, I a)
{
    return ((static_cast<unsigned int>(a) & 0xffu) << 24)
           | ((static_cast<unsigned int>(r) & 0xffu) << 16)
           | ((static_cast<unsigned int>(g) & 0xffu) << 8)
           | (static_cast<unsigned int>(b) & 0xffu);
}

template <class I = int, std::enable_if_t<std::is_integral_v<I>, int> = 0>
constexpr int qGray(I r, I g, I b)
{
    return (int(r) * 11 + int(g) * 16 + int(b) * 5) / 32;
}

template <class T = unsigned int, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr int qGray(T rgb)
{
    return qGray(qRed(rgb), qGreen(rgb), qBlue(rgb));
}

class Image {
public:
    enum Format {
        Format_Invalid = 0,
        Format_RGBA8888,
        Format_Grayscale8,
        Format_ARGB32,
        Format_ARGB32_Premultiplied,
        Format_RGB32,

        Invalid = Format_Invalid,
        RGBA8888 = Format_RGBA8888,
        Grayscale8 = Format_Grayscale8,
        ARGB32 = Format_ARGB32,
        ARGB32_Premultiplied = Format_ARGB32_Premultiplied,
        RGB32 = Format_RGB32
    };

    Image() = default;
    Image(int width, int height, Format format);
    Image(const Size &size, Format format) : Image(size.width(), size.height(), format) {}
    Image(const unsigned char *data, int width, int height, Format format);
    Image(const unsigned char *data, int width, int height, int bytesPerLine, Format format);

    template <class Foreign, std::enable_if_t<BridgesToCore<Foreign, Image>::value, int> = 0>
    Image(const Foreign &foreign) : Image(Bridge<Foreign>::toCore(foreign))
    {
    }

    template <class Foreign, std::enable_if_t<BridgesFromCore<Foreign, Image>::value, int> = 0>
    operator Foreign() const
    {
        return Bridge<Foreign>::fromCore(*this);
    }

    bool isNull() const;
    int width() const;
    Size size() const { return Size(width(), height()); }
    int height() const;
    Format format() const;
    int bytesPerLine() const;
    std::size_t sizeInBytes() const;
    std::int64_t cacheKey() const;
    double devicePixelRatio() const;
    void setDevicePixelRatio(double ratio);

    const unsigned char *constBits() const;
    unsigned char *bits();
    const unsigned char *bits() const;

    const unsigned char *constScanLine(int y) const;
    unsigned char *scanLine(int y);
    const unsigned char *scanLine(int y) const;

    void fill(unsigned int pixelValue);
    void fill(const Color &color);
    unsigned int pixel(int x, int y) const;
    void setPixel(int x, int y, unsigned int indexOrRgb);
    int gray(int x, int y) const;
    int pixelGray(int x, int y) const;

    Image convertToFormat(Format targetFormat) const;
    Image mirrored(bool horizontally = false, bool vertically = true) const;
    Image scaled(int width, int height) const;

    // Exposes the shared ownership token so external views can pin the pixel
    // buffer without copying it, while still triggering copy-on-write if this
    // Image is later mutated.
    std::shared_ptr<const void> pixelOwner() const;

    bool operator==(const Image &other) const;
    bool operator!=(const Image &other) const;

    static Image decodePng(const unsigned char *data, std::size_t size);
    static Image decodePng(const std::vector<unsigned char> &bytes);
    static std::vector<unsigned char> encodePng(const Image &image);
    std::vector<unsigned char> encodePng() const;

    bool save(const String &fileName, const char *format = nullptr, int quality = -1) const;
    bool load(const String &fileName, const char *format = nullptr);

private:
    struct Data;
    void detach();
    std::shared_ptr<Data> m_data;
};

Image decodePng(const unsigned char *data, std::size_t size);
Image decodePng(const std::vector<unsigned char> &bytes);
std::vector<unsigned char> encodePng(const Image &image);

std::string toBase64(const unsigned char *data, std::size_t size);
std::string toBase64(const std::vector<unsigned char> &data);
std::string encodeBase64(const unsigned char *data, std::size_t size);
std::string encodeBase64(const std::vector<unsigned char> &data);

std::vector<unsigned char> fromBase64(const char *data, std::size_t size);
std::vector<unsigned char> fromBase64(const unsigned char *data, std::size_t size);
std::vector<unsigned char> fromBase64(std::string_view encoded);
std::vector<unsigned char> decodeBase64(const char *data, std::size_t size);
std::vector<unsigned char> decodeBase64(const unsigned char *data, std::size_t size);
std::vector<unsigned char> decodeBase64(std::string_view encoded);

} // namespace AnimeCore

#endif // ANIMECORE_IMAGE_H
