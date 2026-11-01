#include "algorithm/core/image.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace AnimeCore {

namespace {

std::int64_t nextCacheKey()
{
    static std::atomic<std::int64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

int bytesPerPixelForFormat(Image::Format format)
{
    switch (format) {
    case Image::Format_Grayscale8:
        return 1;
    case Image::Format_RGBA8888:
    case Image::Format_ARGB32:
    case Image::Format_ARGB32_Premultiplied:
    case Image::Format_RGB32:
        return 4;
    case Image::Format_Invalid:
    default:
        return 0;
    }
}

void pngWriteCallback(void *context, void *data, int size)
{
    if (!context || !data || size <= 0) {
        return;
    }
    auto *out = static_cast<std::vector<unsigned char> *>(context);
    const auto *bytes = static_cast<const unsigned char *>(data);
    out->insert(out->end(), bytes, bytes + size);
}

constexpr unsigned char kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

bool hasPngSignature(const unsigned char *data, std::size_t size)
{
    return data && size >= sizeof(kPngSignature)
           && std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0;
}

} // namespace

struct Image::Data {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int bytesPerLine = 0;
    Format format = Format_Invalid;
    std::int64_t cacheKey = 0;
};

Image::Image(int width, int height, Format format)
{
    const int bpp = bytesPerPixelForFormat(format);
    if (width <= 0 || height <= 0 || bpp <= 0) {
        return;
    }
    if (width > std::numeric_limits<int>::max() / bpp) {
        return;
    }
    const int stride = width * bpp;
    const std::size_t rowBytes = static_cast<std::size_t>(stride);
    const std::size_t rows = static_cast<std::size_t>(height);
    if (rowBytes > std::numeric_limits<std::size_t>::max() / rows) {
        return;
    }

    auto data = std::make_shared<Data>();
    data->width = width;
    data->height = height;
    data->bytesPerLine = stride;
    data->format = format;
    data->cacheKey = nextCacheKey();
    data->pixels.assign(rowBytes * rows, 0);
    m_data = std::move(data);
}

Image::Image(const unsigned char *data, int width, int height, Format format)
    : Image(data, width, height, width * bytesPerPixelForFormat(format), format)
{
}

Image::Image(const unsigned char *data, int width, int height, int bytesPerLine, Format format)
{
    const int bpp = bytesPerPixelForFormat(format);
    if (!data || width <= 0 || height <= 0 || bpp <= 0) {
        return;
    }
    if (width > std::numeric_limits<int>::max() / bpp) {
        return;
    }
    const int tightStride = width * bpp;
    if (bytesPerLine < tightStride) {
        return;
    }

    Image img(width, height, format);
    if (img.isNull()) {
        return;
    }
    unsigned char *dst = img.m_data->pixels.data();
    for (int y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(tightStride),
                    data + static_cast<std::size_t>(y) * static_cast<std::size_t>(bytesPerLine),
                    static_cast<std::size_t>(tightStride));
    }
    m_data = std::move(img.m_data);
}

void Image::detach()
{
    if (!m_data) {
        return;
    }
    if (m_data.use_count() > 1) {
        m_data = std::make_shared<Data>(*m_data);
    }
    // Every pixel mutation stamps a fresh key so downstream caches can tell
    // in-place edits apart from untouched snapshots even when unshared.
    m_data->cacheKey = nextCacheKey();
}

bool Image::isNull() const
{
    return !m_data;
}

int Image::width() const
{
    return m_data ? m_data->width : 0;
}

int Image::height() const
{
    return m_data ? m_data->height : 0;
}

Image::Format Image::format() const
{
    return m_data ? m_data->format : Format_Invalid;
}

int Image::bytesPerLine() const
{
    return m_data ? m_data->bytesPerLine : 0;
}

std::size_t Image::sizeInBytes() const
{
    return m_data ? m_data->pixels.size() : 0;
}

std::int64_t Image::cacheKey() const
{
    return m_data ? m_data->cacheKey : 0;
}

const unsigned char *Image::constBits() const
{
    return m_data ? m_data->pixels.data() : nullptr;
}

unsigned char *Image::bits()
{
    if (!m_data) {
        return nullptr;
    }
    detach();
    return m_data->pixels.data();
}

const unsigned char *Image::bits() const
{
    return constBits();
}

const unsigned char *Image::constScanLine(int y) const
{
    if (!m_data || y < 0 || y >= m_data->height) {
        return nullptr;
    }
    return m_data->pixels.data()
           + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_data->bytesPerLine);
}

unsigned char *Image::scanLine(int y)
{
    if (!m_data || y < 0 || y >= m_data->height) {
        return nullptr;
    }
    detach();
    return m_data->pixels.data()
           + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_data->bytesPerLine);
}

const unsigned char *Image::scanLine(int y) const
{
    return constScanLine(y);
}

void Image::fill(unsigned int pixelValue)
{
    if (!m_data) {
        return;
    }
    detach();
    if (m_data->format == Format_Grayscale8) {
        std::memset(m_data->pixels.data(),
                    static_cast<unsigned char>(pixelValue & 0xffu),
                    m_data->pixels.size());
        return;
    }

    const std::uint32_t word = static_cast<std::uint32_t>(pixelValue);
    unsigned char *dst = m_data->pixels.data();
    const std::size_t count = m_data->pixels.size() / 4;
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(dst + i * 4, &word, 4);
    }
}

unsigned int Image::pixel(int x, int y) const
{
    if (!m_data || x < 0 || y < 0 || x >= m_data->width || y >= m_data->height) {
        return 0;
    }
    const unsigned char *row =
        m_data->pixels.data()
        + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_data->bytesPerLine);
    switch (m_data->format) {
    case Format_Grayscale8: {
        const unsigned char g = row[x];
        return qRgb(g, g, g);
    }
    case Format_RGBA8888: {
        const unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        return qRgba(p[0], p[1], p[2], p[3]);
    }
    case Format_ARGB32:
    case Format_ARGB32_Premultiplied: {
        const unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        return qRgba(p[2], p[1], p[0], p[3]);
    }
    case Format_RGB32: {
        const unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        return qRgb(p[2], p[1], p[0]);
    }
    case Format_Invalid:
    default:
        return 0;
    }
}

void Image::setPixel(int x, int y, unsigned int indexOrRgb)
{
    if (!m_data || x < 0 || y < 0 || x >= m_data->width || y >= m_data->height) {
        return;
    }
    detach();
    unsigned char *row =
        m_data->pixels.data()
        + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_data->bytesPerLine);
    switch (m_data->format) {
    case Format_Grayscale8:
        row[x] = static_cast<unsigned char>(
            (indexOrRgb <= 0xffu) ? indexOrRgb : static_cast<unsigned int>(qGray(indexOrRgb)));
        break;
    case Format_RGBA8888: {
        unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        p[0] = static_cast<unsigned char>((indexOrRgb >> 16) & 0xffu);
        p[1] = static_cast<unsigned char>((indexOrRgb >> 8) & 0xffu);
        p[2] = static_cast<unsigned char>(indexOrRgb & 0xffu);
        p[3] = static_cast<unsigned char>((indexOrRgb >> 24) & 0xffu);
        break;
    }
    case Format_ARGB32:
    case Format_ARGB32_Premultiplied: {
        unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        p[0] = static_cast<unsigned char>(indexOrRgb & 0xffu);
        p[1] = static_cast<unsigned char>((indexOrRgb >> 8) & 0xffu);
        p[2] = static_cast<unsigned char>((indexOrRgb >> 16) & 0xffu);
        p[3] = static_cast<unsigned char>((indexOrRgb >> 24) & 0xffu);
        break;
    }
    case Format_RGB32: {
        unsigned char *p = row + static_cast<std::size_t>(x) * 4;
        p[0] = static_cast<unsigned char>(indexOrRgb & 0xffu);
        p[1] = static_cast<unsigned char>((indexOrRgb >> 8) & 0xffu);
        p[2] = static_cast<unsigned char>((indexOrRgb >> 16) & 0xffu);
        p[3] = 0xffu;
        break;
    }
    case Format_Invalid:
    default:
        break;
    }
}

int Image::gray(int x, int y) const
{
    if (!m_data || x < 0 || y < 0 || x >= m_data->width || y >= m_data->height) {
        return 0;
    }
    if (m_data->format == Format_Grayscale8) {
        return m_data->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_data->bytesPerLine)
                              + static_cast<std::size_t>(x)];
    }
    return qGray(pixel(x, y));
}

int Image::pixelGray(int x, int y) const
{
    return gray(x, y);
}

Image Image::convertToFormat(Format targetFormat) const
{
    if (!m_data || targetFormat == Format_Invalid) {
        return Image();
    }
    if (m_data->format == targetFormat) {
        return *this;
    }

    Image out(m_data->width, m_data->height, targetFormat);
    if (out.isNull()) {
        return out;
    }
    for (int y = 0; y < m_data->height; ++y) {
        for (int x = 0; x < m_data->width; ++x) {
            const unsigned int c = pixel(x, y);
            if (targetFormat == Format_Grayscale8) {
                out.m_data->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.m_data->bytesPerLine)
                                   + static_cast<std::size_t>(x)] = static_cast<unsigned char>(qGray(c));
            } else {
                out.setPixel(x, y, c);
            }
        }
    }
    out.m_data->cacheKey = nextCacheKey();
    return out;
}

Image Image::mirrored(bool horizontally, bool vertically) const
{
    if (!m_data) {
        return Image();
    }
    if (!horizontally && !vertically) {
        return *this;
    }

    Image out(m_data->width, m_data->height, m_data->format);
    if (out.isNull()) {
        return out;
    }
    const int w = m_data->width;
    const int h = m_data->height;
    const int bpp = bytesPerPixelForFormat(m_data->format);
    for (int y = 0; y < h; ++y) {
        const int srcY = vertically ? (h - 1 - y) : y;
        const unsigned char *srcRow = constScanLine(srcY);
        unsigned char *dstRow =
            out.m_data->pixels.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.m_data->bytesPerLine);
        if (!horizontally) {
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(out.m_data->bytesPerLine));
        } else if (bpp == 1) {
            for (int x = 0; x < w; ++x) {
                dstRow[x] = srcRow[w - 1 - x];
            }
        } else {
            for (int x = 0; x < w; ++x) {
                std::memcpy(dstRow + static_cast<std::size_t>(x) * 4,
                            srcRow + static_cast<std::size_t>(w - 1 - x) * 4,
                            4);
            }
        }
    }
    return out;
}

Image Image::scaled(int targetWidth, int targetHeight) const
{
    if (!m_data || targetWidth <= 0 || targetHeight <= 0) {
        return Image();
    }
    if (targetWidth == m_data->width && targetHeight == m_data->height) {
        return *this;
    }

    Image out(targetWidth, targetHeight, m_data->format);
    if (out.isNull()) {
        return out;
    }

    const int srcW = m_data->width;
    const int srcH = m_data->height;
    const int bpp = bytesPerPixelForFormat(m_data->format);
    if (bpp <= 0) {
        return Image();
    }

    struct ScaleTap {
        int index = 0;
        std::int64_t weight = 0;
    };
    struct ScaleSpan {
        int offset = 0;
        int count = 0;
    };

    constexpr std::int64_t kWeightUnit = 1 << 14;

    auto buildAxisKernel = [](int srcLen,
                              int dstLen,
                              std::vector<ScaleSpan> &spans,
                              std::vector<ScaleTap> &taps) {
        spans.resize(static_cast<std::size_t>(dstLen));
        taps.clear();

        // Each axis independently uses linear interpolation when enlarging and
        // box area averaging when shrinking so downscaling does not alias.
        if (dstLen >= srcLen) {
            taps.reserve(static_cast<std::size_t>(dstLen) * 2);
            const std::int64_t step = (static_cast<std::int64_t>(srcLen) << 16)
                                      / static_cast<std::int64_t>(dstLen);
            std::int64_t pos = step / 2 - 32768;
            const std::int64_t maxPos = static_cast<std::int64_t>(srcLen - 1) << 16;
            for (int d = 0; d < dstLen; ++d) {
                const std::int64_t clamped = std::clamp<std::int64_t>(pos, 0, maxPos);
                const int i0 = static_cast<int>(clamped >> 16);
                const int i1 = std::min(i0 + 1, srcLen - 1);
                const std::int64_t f8 = (clamped & 0xffff) >> 8;
                const std::int64_t w1 = f8 << 6;
                const std::int64_t w0 = kWeightUnit - w1;

                ScaleSpan span;
                span.offset = static_cast<int>(taps.size());
                if (i0 == i1 || w1 == 0) {
                    taps.push_back({i0, kWeightUnit});
                    span.count = 1;
                } else {
                    taps.push_back({i0, w0});
                    taps.push_back({i1, w1});
                    span.count = 2;
                }
                spans[static_cast<std::size_t>(d)] = span;
                pos += step;
            }
            return;
        }

        const std::int64_t Cp = ((static_cast<std::int64_t>(dstLen) << 14) + srcLen - 1)
                                / static_cast<std::int64_t>(srcLen);
        taps.reserve(static_cast<std::size_t>(srcLen + dstLen * 2));
        for (int d = 0; d < dstLen; ++d) {
            const std::int64_t pos =
                (static_cast<std::int64_t>(d) * (static_cast<std::int64_t>(srcLen) << 14))
                / static_cast<std::int64_t>(dstLen);
            ScaleSpan span;
            span.offset = static_cast<int>(taps.size());
            int s = std::clamp(static_cast<int>(pos >> 14), 0, srcLen - 1);
            const std::int64_t frac = pos & 0x3fff;
            std::int64_t rem = kWeightUnit;

            if (frac != 0) {
                const std::int64_t w0 = std::min(rem, ((0x4000 - frac) * Cp) >> 14);
                if (w0 > 0) {
                    taps.push_back({s, w0});
                    ++span.count;
                    rem -= w0;
                }
                ++s;
            }

            while (rem > 0 && s < srcLen) {
                if (rem <= Cp || s == srcLen - 1) {
                    taps.push_back({s, rem});
                    ++span.count;
                    rem = 0;
                    break;
                }
                taps.push_back({s, Cp});
                ++span.count;
                rem -= Cp;
                ++s;
            }

            if (span.count == 0) {
                taps.push_back({srcLen - 1, kWeightUnit});
                span.count = 1;
            }
            spans[static_cast<std::size_t>(d)] = span;
        }
    };

    std::vector<ScaleSpan> xSpans;
    std::vector<ScaleTap> xTaps;
    std::vector<ScaleSpan> ySpans;
    std::vector<ScaleTap> yTaps;
    buildAxisKernel(srcW, targetWidth, xSpans, xTaps);
    buildAxisKernel(srcH, targetHeight, ySpans, yTaps);

    const bool bothEnlarged = (targetWidth >= srcW && targetHeight >= srcH);
    const int vertShift = bothEnlarged ? 14 : 0;
    const int horizShift = bothEnlarged ? 14 : 28;

    if (m_data->format == Format_Grayscale8) {
        std::vector<std::int64_t> vertRow(static_cast<std::size_t>(srcW));
        for (int dy = 0; dy < targetHeight; ++dy) {
            const ScaleSpan ySpan = ySpans[static_cast<std::size_t>(dy)];
            for (int sx = 0; sx < srcW; ++sx) {
                std::int64_t sumY = 0;
                for (int k = 0; k < ySpan.count; ++k) {
                    const ScaleTap &tap = yTaps[static_cast<std::size_t>(ySpan.offset + k)];
                    sumY += static_cast<std::int64_t>(constScanLine(tap.index)[sx]) * tap.weight;
                }
                vertRow[static_cast<std::size_t>(sx)] = sumY >> vertShift;
            }

            unsigned char *dstRow =
                out.m_data->pixels.data()
                + static_cast<std::size_t>(dy) * static_cast<std::size_t>(out.m_data->bytesPerLine);
            for (int dx = 0; dx < targetWidth; ++dx) {
                const ScaleSpan xSpan = xSpans[static_cast<std::size_t>(dx)];
                std::int64_t sumX = 0;
                for (int k = 0; k < xSpan.count; ++k) {
                    const ScaleTap &tap = xTaps[static_cast<std::size_t>(xSpan.offset + k)];
                    sumX += vertRow[static_cast<std::size_t>(tap.index)] * tap.weight;
                }
                dstRow[dx] = static_cast<unsigned char>(
                    std::clamp<std::int64_t>(sumX >> horizShift, 0, 255));
            }
        }
        return out;
    }

    // Straight-alpha pixels must be converted to premultiplied form before
    // filtering so transparent neighbors do not bleed their RGB values into
    // semi-transparent edges.
    const bool needsPremul = (m_data->format == Format_RGBA8888
                              || m_data->format == Format_ARGB32);
    const std::size_t srcRowStride = static_cast<std::size_t>(srcW) * 4;
    std::vector<unsigned char> srcPremul(static_cast<std::size_t>(srcH) * srcRowStride);

    if (needsPremul) {
        for (int sy = 0; sy < srcH; ++sy) {
            const unsigned char *srcRow = constScanLine(sy);
            unsigned char *dstPremul = srcPremul.data() + static_cast<std::size_t>(sy) * srcRowStride;
            for (int sx = 0; sx < srcW; ++sx) {
                const unsigned char *p = srcRow + static_cast<std::size_t>(sx) * 4;
                unsigned char *q = dstPremul + static_cast<std::size_t>(sx) * 4;
                const unsigned int a = p[3];
                const unsigned int t0 = static_cast<unsigned int>(p[0]) * a + 0x7fu;
                const unsigned int t1 = static_cast<unsigned int>(p[1]) * a + 0x7fu;
                const unsigned int t2 = static_cast<unsigned int>(p[2]) * a + 0x7fu;
                q[0] = static_cast<unsigned char>((t0 + (t0 >> 8)) >> 8);
                q[1] = static_cast<unsigned char>((t1 + (t1 >> 8)) >> 8);
                q[2] = static_cast<unsigned char>((t2 + (t2 >> 8)) >> 8);
                q[3] = static_cast<unsigned char>(a);
            }
        }
    }

    std::vector<std::int64_t> vertRow(srcRowStride);
    for (int dy = 0; dy < targetHeight; ++dy) {
        const ScaleSpan ySpan = ySpans[static_cast<std::size_t>(dy)];
        for (int sx = 0; sx < srcW; ++sx) {
            std::int64_t c0 = 0;
            std::int64_t c1 = 0;
            std::int64_t c2 = 0;
            std::int64_t c3 = 0;
            for (int k = 0; k < ySpan.count; ++k) {
                const ScaleTap &tap = yTaps[static_cast<std::size_t>(ySpan.offset + k)];
                const unsigned char *rowPtr =
                    needsPremul
                        ? (srcPremul.data() + static_cast<std::size_t>(tap.index) * srcRowStride)
                        : constScanLine(tap.index);
                const unsigned char *p = rowPtr + static_cast<std::size_t>(sx) * 4;
                c0 += static_cast<std::int64_t>(p[0]) * tap.weight;
                c1 += static_cast<std::int64_t>(p[1]) * tap.weight;
                c2 += static_cast<std::int64_t>(p[2]) * tap.weight;
                c3 += static_cast<std::int64_t>(p[3]) * tap.weight;
            }
            std::int64_t *v = vertRow.data() + static_cast<std::size_t>(sx) * 4;
            v[0] = c0 >> vertShift;
            v[1] = c1 >> vertShift;
            v[2] = c2 >> vertShift;
            v[3] = c3 >> vertShift;
        }

        unsigned char *dstRow =
            out.m_data->pixels.data()
            + static_cast<std::size_t>(dy) * static_cast<std::size_t>(out.m_data->bytesPerLine);
        for (int dx = 0; dx < targetWidth; ++dx) {
            const ScaleSpan xSpan = xSpans[static_cast<std::size_t>(dx)];
            std::int64_t c0 = 0;
            std::int64_t c1 = 0;
            std::int64_t c2 = 0;
            std::int64_t c3 = 0;
            for (int k = 0; k < xSpan.count; ++k) {
                const ScaleTap &tap = xTaps[static_cast<std::size_t>(xSpan.offset + k)];
                const std::int64_t *p = vertRow.data() + static_cast<std::size_t>(tap.index) * 4;
                c0 += p[0] * tap.weight;
                c1 += p[1] * tap.weight;
                c2 += p[2] * tap.weight;
                c3 += p[3] * tap.weight;
            }

            unsigned char *dst = dstRow + static_cast<std::size_t>(dx) * 4;
            if (m_data->format == Format_RGB32) {
                dst[0] = static_cast<unsigned char>(std::clamp<std::int64_t>(c0 >> horizShift, 0, 255));
                dst[1] = static_cast<unsigned char>(std::clamp<std::int64_t>(c1 >> horizShift, 0, 255));
                dst[2] = static_cast<unsigned char>(std::clamp<std::int64_t>(c2 >> horizShift, 0, 255));
                dst[3] = 0xffu;
                continue;
            }

            const int a = static_cast<int>(std::clamp<std::int64_t>(c3 >> horizShift, 0, 255));
            const int p0 = static_cast<int>(std::clamp<std::int64_t>(c0 >> horizShift, 0, a));
            const int p1 = static_cast<int>(std::clamp<std::int64_t>(c1 >> horizShift, 0, a));
            const int p2 = static_cast<int>(std::clamp<std::int64_t>(c2 >> horizShift, 0, a));

            if (!needsPremul) {
                dst[0] = static_cast<unsigned char>(p0);
                dst[1] = static_cast<unsigned char>(p1);
                dst[2] = static_cast<unsigned char>(p2);
                dst[3] = static_cast<unsigned char>(a);
            } else if (a == 0) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
            } else if (a == 255) {
                dst[0] = static_cast<unsigned char>(p0);
                dst[1] = static_cast<unsigned char>(p1);
                dst[2] = static_cast<unsigned char>(p2);
                dst[3] = 255u;
            } else {
                const unsigned int ua = static_cast<unsigned int>(a);
                const unsigned int bias = (ua - 1u) >> 1;
                dst[0] = static_cast<unsigned char>(
                    std::min(255u, (static_cast<unsigned int>(p0) * 255u + bias) / ua));
                dst[1] = static_cast<unsigned char>(
                    std::min(255u, (static_cast<unsigned int>(p1) * 255u + bias) / ua));
                dst[2] = static_cast<unsigned char>(
                    std::min(255u, (static_cast<unsigned int>(p2) * 255u + bias) / ua));
                dst[3] = static_cast<unsigned char>(a);
            }
        }
    }
    return out;
}

std::shared_ptr<const void> Image::pixelOwner() const
{
    return m_data;
}

bool Image::operator==(const Image &other) const
{
    if (m_data == other.m_data) {
        return true;
    }
    if (!m_data || !other.m_data) {
        return false;
    }
    return m_data->width == other.m_data->width
           && m_data->height == other.m_data->height
           && m_data->format == other.m_data->format
           && m_data->pixels == other.m_data->pixels;
}

bool Image::operator!=(const Image &other) const
{
    return !(*this == other);
}

Image Image::decodePng(const unsigned char *data, std::size_t size)
{
    if (!hasPngSignature(data, size)
        || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Image();
    }

    const int len = static_cast<int>(size);
    int w = 0;
    int h = 0;
    int channels = 0;

    if (stbi_is_16_bit_from_memory(data, len)) {
        stbi_us *decoded16 = stbi_load_16_from_memory(data, len, &w, &h, &channels, 4);
        if (!decoded16) {
            return Image();
        }
        Image out(w, h, Format_RGBA8888);
        if (!out.isNull()) {
            unsigned char *dst = out.m_data->pixels.data();
            const std::size_t total = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
            for (std::size_t i = 0; i < total; ++i) {
                dst[i] = static_cast<unsigned char>((static_cast<unsigned int>(decoded16[i]) + 128u) / 257u);
            }
        }
        stbi_image_free(decoded16);
        return out;
    }

    stbi_uc *decoded8 = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!decoded8) {
        return Image();
    }
    Image out(w, h, Format_RGBA8888);
    if (!out.isNull()) {
        std::memcpy(out.m_data->pixels.data(),
                    decoded8,
                    static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    }
    stbi_image_free(decoded8);
    return out;
}

Image Image::decodePng(const std::vector<unsigned char> &bytes)
{
    return decodePng(bytes.data(), bytes.size());
}

std::vector<unsigned char> Image::encodePng(const Image &image)
{
    std::vector<unsigned char> out;
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return out;
    }

    if (image.format() == Format_Grayscale8) {
        if (!stbi_write_png_to_func(&pngWriteCallback,
                                    &out,
                                    image.width(),
                                    image.height(),
                                    1,
                                    image.constBits(),
                                    image.bytesPerLine())) {
            out.clear();
        }
        return out;
    }

    const Image rgba = (image.format() == Format_RGBA8888)
                           ? image
                           : image.convertToFormat(Format_RGBA8888);
    if (rgba.isNull()) {
        return out;
    }

    if (!stbi_write_png_to_func(&pngWriteCallback,
                                &out,
                                rgba.width(),
                                rgba.height(),
                                4,
                                rgba.constBits(),
                                rgba.bytesPerLine())) {
        out.clear();
    }
    return out;
}

std::vector<unsigned char> Image::encodePng() const
{
    return encodePng(*this);
}

Image decodePng(const unsigned char *data, std::size_t size)
{
    return Image::decodePng(data, size);
}

Image decodePng(const std::vector<unsigned char> &bytes)
{
    return Image::decodePng(bytes);
}

std::vector<unsigned char> encodePng(const Image &image)
{
    return Image::encodePng(image);
}

std::string toBase64(const unsigned char *data, std::size_t size)
{
    if (!data || size == 0) {
        return std::string();
    }

    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.resize(((size + 2) / 3) * 4);

    std::size_t i = 0;
    std::size_t o = 0;
    while (i + 3 <= size) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = data[i + 1];
        const unsigned int b2 = data[i + 2];
        out[o++] = kAlphabet[(b0 >> 2) & 0x3fu];
        out[o++] = kAlphabet[((b0 & 0x03u) << 4) | ((b1 >> 4) & 0x0fu)];
        out[o++] = kAlphabet[((b1 & 0x0fu) << 2) | ((b2 >> 6) & 0x03u)];
        out[o++] = kAlphabet[b2 & 0x3fu];
        i += 3;
    }

    const std::size_t rem = size - i;
    if (rem == 1) {
        const unsigned int b0 = data[i];
        out[o++] = kAlphabet[(b0 >> 2) & 0x3fu];
        out[o++] = kAlphabet[(b0 & 0x03u) << 4];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = data[i + 1];
        out[o++] = kAlphabet[(b0 >> 2) & 0x3fu];
        out[o++] = kAlphabet[((b0 & 0x03u) << 4) | ((b1 >> 4) & 0x0fu)];
        out[o++] = kAlphabet[(b1 & 0x0fu) << 2];
        out[o++] = '=';
    }

    return out;
}

std::string toBase64(const std::vector<unsigned char> &data)
{
    return toBase64(data.data(), data.size());
}

std::string encodeBase64(const unsigned char *data, std::size_t size)
{
    return toBase64(data, size);
}

std::string encodeBase64(const std::vector<unsigned char> &data)
{
    return toBase64(data.data(), data.size());
}

std::vector<unsigned char> fromBase64(const char *data, std::size_t size)
{
    std::vector<unsigned char> out;
    if (!data || size == 0) {
        return out;
    }

    out.reserve((size / 4) * 3);
    unsigned int buf = 0;
    int bitsCollected = 0;
    bool padSeen = false;

    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        int sextet = -1;
        if (ch >= 'A' && ch <= 'Z') {
            sextet = int(ch - 'A');
        } else if (ch >= 'a' && ch <= 'z') {
            sextet = int(ch - 'a') + 26;
        } else if (ch >= '0' && ch <= '9') {
            sextet = int(ch - '0') + 52;
        } else if (ch == '+') {
            sextet = 62;
        } else if (ch == '/') {
            sextet = 63;
        } else if (ch == '=') {
            padSeen = true;
            continue;
        } else {
            continue;
        }

        if (padSeen) {
            continue;
        }

        buf = (buf << 6) | static_cast<unsigned int>(sextet);
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            out.push_back(static_cast<unsigned char>((buf >> bitsCollected) & 0xffu));
        }
    }

    return out;
}

std::vector<unsigned char> fromBase64(const unsigned char *data, std::size_t size)
{
    return fromBase64(reinterpret_cast<const char *>(data), size);
}

std::vector<unsigned char> fromBase64(std::string_view encoded)
{
    return fromBase64(encoded.data(), encoded.size());
}

std::vector<unsigned char> decodeBase64(const char *data, std::size_t size)
{
    return fromBase64(data, size);
}

std::vector<unsigned char> decodeBase64(const unsigned char *data, std::size_t size)
{
    return fromBase64(reinterpret_cast<const char *>(data), size);
}

std::vector<unsigned char> decodeBase64(std::string_view encoded)
{
    return fromBase64(encoded.data(), encoded.size());
}

} // namespace AnimeCore
