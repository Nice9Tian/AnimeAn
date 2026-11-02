#include "algorithm/core/rasterdevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AnimeCore {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline std::uint32_t div255(std::uint32_t x)
{
    return (x + (x >> 8) + 0x80U) >> 8;
}

inline std::uint32_t byteMul(std::uint32_t x, std::uint32_t a)
{
    std::uint32_t t = (x & 0xff00ffU) * a;
    t = (t + ((t >> 8) & 0xff00ffU) + 0x800080U) >> 8;
    t &= 0xff00ffU;
    x = ((x >> 8) & 0xff00ffU) * a;
    x = (x + ((x >> 8) & 0xff00ffU) + 0x800080U);
    x &= 0xff00ff00U;
    return x | t;
}

inline std::uint32_t colorToPremul(const Color &c, double opacity = 1.0)
{
    if (!c.isValid()) {
        return 0;
    }
    const double effA = std::clamp(static_cast<double>(c.alpha()) * std::clamp(opacity, 0.0, 1.0), 0.0, 255.0);
    const std::uint32_t a = static_cast<std::uint32_t>(effA + 0.5);
    if (a == 0) {
        return 0;
    }
    const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(c.red(), 0, 255));
    const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(c.green(), 0, 255));
    const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(c.blue(), 0, 255));
    if (a == 255) {
        return (255U << 24) | (r << 16) | (g << 8) | b;
    }
    return (a << 24) | (div255(r * a) << 16) | (div255(g * a) << 8) | div255(b * a);
}

inline std::uint32_t opacityToByte(double opacity)
{
    return static_cast<std::uint32_t>(std::clamp(static_cast<int>(opacity * 255.0 + 0.5), 0, 255));
}

inline void blendPixel(std::uint32_t &dst, std::uint32_t srcPremul, std::uint32_t opacity8,
                       std::uint32_t cov, CompositionMode mode)
{
    if (cov == 0) {
        return;
    }
    if (mode == CompositionMode_SourceOver) {
        const std::uint32_t ca = (cov == 255 && opacity8 == 255)
            ? 255U
            : ((cov == 255) ? opacity8 : ((opacity8 == 255) ? cov : div255(cov * opacity8)));
        if (ca == 0) {
            return;
        }
        const std::uint32_t s = (ca == 255) ? srcPremul : byteMul(srcPremul, ca);
        const std::uint32_t sa = s >> 24;
        if (sa == 255) {
            dst = s;
        } else if (sa > 0) {
            dst = s + byteMul(dst, 255U - sa);
        }
    } else if (mode == CompositionMode_SourceIn) {
        const std::uint32_t ca = (cov == 255 && opacity8 == 255)
            ? 255U
            : ((cov == 255) ? opacity8 : ((opacity8 == 255) ? cov : div255(cov * opacity8)));
        if (ca == 0) {
            return;
        }
        const std::uint32_t dstA = dst >> 24;
        const std::uint32_t res = (dstA == 0) ? 0U : ((dstA == 255) ? srcPremul : byteMul(srcPremul, dstA));
        if (ca == 255) {
            dst = res;
        } else {
            dst = byteMul(res, ca) + byteMul(dst, 255U - ca);
        }
    }
}

inline std::uint32_t fetchSourcePixelPremul(const unsigned char *srcBase, int srcBpl,
                                            Image::Format fmt, int x, int y)
{
    const unsigned char *row = srcBase + static_cast<std::size_t>(y) * static_cast<std::size_t>(srcBpl);
    switch (fmt) {
    case Image::Format_ARGB32_Premultiplied:
        return reinterpret_cast<const std::uint32_t *>(row)[x];
    case Image::Format_ARGB32: {
        const std::uint32_t px = reinterpret_cast<const std::uint32_t *>(row)[x];
        const std::uint32_t a = px >> 24;
        if (a == 255) {
            return px;
        }
        if (a == 0) {
            return 0;
        }
        const std::uint32_t r = div255(((px >> 16) & 0xffU) * a);
        const std::uint32_t g = div255(((px >> 8) & 0xffU) * a);
        const std::uint32_t b = div255((px & 0xffU) * a);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
    case Image::Format_RGB32:
        return reinterpret_cast<const std::uint32_t *>(row)[x] | 0xff000000U;
    case Image::Format_RGBA8888: {
        const unsigned char *p = row + x * 4;
        const std::uint32_t r = p[0];
        const std::uint32_t g = p[1];
        const std::uint32_t b = p[2];
        const std::uint32_t a = p[3];
        if (a == 255) {
            return (255U << 24) | (r << 16) | (g << 8) | b;
        }
        if (a == 0) {
            return 0;
        }
        return (a << 24) | (div255(r * a) << 16) | (div255(g * a) << 8) | div255(b * a);
    }
    case Image::Format_Grayscale8: {
        const std::uint32_t v = row[x];
        return 0xff000000U | (v << 16) | (v << 8) | v;
    }
    default:
        return 0;
    }
}

inline std::uint32_t bilerpPremul(std::uint32_t c00, std::uint32_t c10,
                                  std::uint32_t c01, std::uint32_t c11,
                                  std::uint32_t wx, std::uint32_t wy)
{
    const std::uint32_t invWx = 256U - wx;
    const std::uint32_t invWy = 256U - wy;
    const std::uint32_t w00 = invWx * invWy;
    const std::uint32_t w10 = wx * invWy;
    const std::uint32_t w01 = invWx * wy;
    const std::uint32_t w11 = wx * wy;

    const std::uint32_t b = ((c00 & 0xffU) * w00 + (c10 & 0xffU) * w10
                           + (c01 & 0xffU) * w01 + (c11 & 0xffU) * w11 + 0x8000U) >> 16;
    const std::uint32_t g = (((c00 >> 8) & 0xffU) * w00 + ((c10 >> 8) & 0xffU) * w10
                           + ((c01 >> 8) & 0xffU) * w01 + ((c11 >> 8) & 0xffU) * w11 + 0x8000U) >> 16;
    const std::uint32_t r = (((c00 >> 16) & 0xffU) * w00 + ((c10 >> 16) & 0xffU) * w10
                           + ((c01 >> 16) & 0xffU) * w01 + ((c11 >> 16) & 0xffU) * w11 + 0x8000U) >> 16;
    const std::uint32_t a = (((c00 >> 24) & 0xffU) * w00 + ((c10 >> 24) & 0xffU) * w10
                           + ((c01 >> 24) & 0xffU) * w01 + ((c11 >> 24) & 0xffU) * w11 + 0x8000U) >> 16;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

struct CubicSeg {
    double x0, y0, x1, y1, x2, y2, x3, y3;
};

void flattenCubic(double x0, double y0, double x1, double y1,
                  double x2, double y2, double x3, double y3,
                  double tolSq16, std::vector<PointF> &out)
{
    CubicSeg stack[32];
    stack[0] = {x0, y0, x1, y1, x2, y2, x3, y3};
    int top = 0;

    while (top >= 0) {
        const CubicSeg s = stack[top];
        const double ux = 3.0 * s.x1 - 2.0 * s.x0 - s.x3;
        const double uy = 3.0 * s.y1 - 2.0 * s.y0 - s.y3;
        const double vx = 3.0 * s.x2 - 2.0 * s.x3 - s.x0;
        const double vy = 3.0 * s.y2 - 2.0 * s.y3 - s.y0;
        const double err = std::max(ux * ux, vx * vx) + std::max(uy * uy, vy * vy);

        if (err <= tolSq16 || top >= 31) {
            out.emplace_back(s.x3, s.y3);
            --top;
        } else {
            const double x01 = 0.5 * (s.x0 + s.x1);
            const double y01 = 0.5 * (s.y0 + s.y1);
            const double x12 = 0.5 * (s.x1 + s.x2);
            const double y12 = 0.5 * (s.y1 + s.y2);
            const double x23 = 0.5 * (s.x2 + s.x3);
            const double y23 = 0.5 * (s.y2 + s.y3);
            const double x012 = 0.5 * (x01 + x12);
            const double y012 = 0.5 * (y01 + y12);
            const double x123 = 0.5 * (x12 + x23);
            const double y123 = 0.5 * (y12 + y23);
            const double x0123 = 0.5 * (x012 + x123);
            const double y0123 = 0.5 * (y012 + y123);

            stack[top] = {x0123, y0123, x123, y123, x23, y23, s.x3, s.y3};
            stack[++top] = {s.x0, s.y0, x01, y01, x012, y012, x0123, y0123};
        }
    }
}

inline bool pointsClose(const PointF &a, const PointF &b, double eps = 1e-6)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

inline double transformMaxScale(const Transform &t)
{
    const double s1 = std::hypot(t.m11(), t.m12());
    const double s2 = std::hypot(t.m21(), t.m22());
    return std::max(s1, s2);
}

inline bool isSimilarityTransform(const Transform &t, double *outScale)
{
    const double a = t.m11();
    const double b = t.m12();
    const double c = t.m21();
    const double d = t.m22();
    const double det = a * d - b * c;
    if (det <= 1e-12) {
        return false;
    }
    if (std::abs(a - d) <= 1e-6 && std::abs(b + c) <= 1e-6) {
        *outScale = std::hypot(a, b);
        return *outScale > 1e-9;
    }
    return false;
}

} // namespace

Painter::Painter(Image *image)
{
    if (image) {
        m_ownedDevice = std::make_unique<RasterDevice>(image);
        m_device = m_ownedDevice.get();
    }
    m_ratio = m_device ? m_device->paintDevicePixelRatio() : 1.0;
    updateTransform();
}

RasterDevice::RasterDevice(Image *image)
    : m_image(image)
{
}

RasterDevice::~RasterDevice() = default;

double RasterDevice::paintDevicePixelRatio() const
{
    return (m_image && !m_image->isNull()) ? m_image->devicePixelRatio() : 1.0;
}

bool RasterDevice::prepareTarget()
{
    if (!m_image || m_image->isNull()) {
        return false;
    }
    if (m_image->format() != Image::Format_ARGB32_Premultiplied) {
        return false;
    }
    m_width = m_image->width();
    m_height = m_image->height();
    if (m_width <= 0 || m_height <= 0) {
        return false;
    }
    m_baseBits = m_image->bits();
    m_bytesPerLine = m_image->bytesPerLine();
    if (!m_baseBits) {
        return false;
    }

    if (m_width != m_bufWidth || m_height != m_bufHeight) {
        m_bufWidth = m_width;
        m_bufHeight = m_height;
        m_deltaStride = m_width + 2;
        m_delta.assign(static_cast<std::size_t>(m_deltaStride) * static_cast<std::size_t>(m_height), 0.0f);
        m_rowMinX.assign(static_cast<std::size_t>(m_height), m_width);
        m_rowMaxX.assign(static_cast<std::size_t>(m_height), -1);
    }
    m_minY = m_height;
    m_maxY = -1;
    return true;
}

void RasterDevice::resetScanBounds(int y0, int y1)
{
    y0 = std::max(0, y0);
    y1 = std::min(m_height - 1, y1);
    for (int y = y0; y <= y1; ++y) {
        const int minX = m_rowMinX[static_cast<std::size_t>(y)];
        const int maxX = m_rowMaxX[static_cast<std::size_t>(y)];
        if (minX <= maxX) {
            float *row = &m_delta[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_deltaStride)];
            std::fill(row + minX, row + std::min(maxX + 2, m_deltaStride), 0.0f);
        }
        m_rowMinX[static_cast<std::size_t>(y)] = m_width;
        m_rowMaxX[static_cast<std::size_t>(y)] = -1;
    }
    m_minY = m_height;
    m_maxY = -1;
}

void RasterDevice::addEdgeAA(double x0, double y0, double x1, double y1)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
        return;
    }
    if (std::abs(y1 - y0) <= 1e-9) {
        return;
    }

    float dir = 1.0f;
    if (y0 > y1) {
        dir = -1.0f;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    const double hLimit = static_cast<double>(m_height);
    if (y1 <= 0.0 || y0 >= hLimit) {
        return;
    }

    const double dxdy = (x1 - x0) / (y1 - y0);
    if (y0 < 0.0) {
        x0 += (0.0 - y0) * dxdy;
        y0 = 0.0;
    }
    if (y1 > hLimit) {
        x1 += (hLimit - y1) * dxdy;
        y1 = hLimit;
    }
    if (y0 >= y1) {
        return;
    }

    const int iyStart = std::clamp(static_cast<int>(std::floor(y0)), 0, m_height - 1);
    const int iyEnd = std::clamp(static_cast<int>(std::ceil(y1)) - 1, 0, m_height - 1);
    if (iyStart > iyEnd) {
        return;
    }

    if (iyStart < m_minY) {
        m_minY = iyStart;
    }
    if (iyEnd > m_maxY) {
        m_maxY = iyEnd;
    }

    const double wLimit = static_cast<double>(m_width);

    for (int y = iyStart; y <= iyEnd; ++y) {
        const double sy0 = std::max(y0, static_cast<double>(y));
        const double sy1 = std::min(y1, static_cast<double>(y + 1));
        const double dy = sy1 - sy0;
        if (dy <= 1e-9) {
            continue;
        }

        const float signedDy = static_cast<float>(dy) * dir;
        const double sx0 = x0 + (sy0 - y0) * dxdy;
        const double sx1 = x0 + (sy1 - y0) * dxdy;
        const double xLeft = std::min(sx0, sx1);
        const double xRight = std::max(sx0, sx1);

        float *row = &m_delta[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_deltaStride)];

        if (xRight <= 0.0) {
            row[0] += signedDy;
            m_rowMinX[static_cast<std::size_t>(y)] = 0;
            if (m_rowMaxX[static_cast<std::size_t>(y)] < 0) {
                m_rowMaxX[static_cast<std::size_t>(y)] = 0;
            }
            continue;
        }
        if (xLeft >= wLimit) {
            m_rowMaxX[static_cast<std::size_t>(y)] = m_width - 1;
            continue;
        }

        if (xRight - xLeft <= 1e-7) {
            const double xm = 0.5 * (xLeft + xRight);
            if (xm <= 0.0) {
                row[0] += signedDy;
                m_rowMinX[static_cast<std::size_t>(y)] = 0;
                if (m_rowMaxX[static_cast<std::size_t>(y)] < 0) {
                    m_rowMaxX[static_cast<std::size_t>(y)] = 0;
                }
            } else if (xm < wLimit) {
                const int ix = static_cast<int>(xm);
                const float frac = static_cast<float>(xm - static_cast<double>(ix));
                row[ix] += signedDy * (1.0f - frac);
                row[ix + 1] += signedDy * frac;
                if (ix < m_rowMinX[static_cast<std::size_t>(y)]) {
                    m_rowMinX[static_cast<std::size_t>(y)] = ix;
                }
                const int maxCol = std::min(ix + 1, m_width - 1);
                if (maxCol > m_rowMaxX[static_cast<std::size_t>(y)]) {
                    m_rowMaxX[static_cast<std::size_t>(y)] = maxCol;
                }
            } else {
                m_rowMaxX[static_cast<std::size_t>(y)] = m_width - 1;
            }
        } else {
            const double invDx = 1.0 / (xRight - xLeft);
            if (xLeft < 0.0) {
                const double leftSpan = (0.0 - xLeft) * invDx;
                row[0] += static_cast<float>(leftSpan) * signedDy;
                m_rowMinX[static_cast<std::size_t>(y)] = 0;
                if (m_rowMaxX[static_cast<std::size_t>(y)] < 0) {
                    m_rowMaxX[static_cast<std::size_t>(y)] = 0;
                }
            }
            const int ixStart = std::max(0, static_cast<int>(std::floor(xLeft)));
            const int ixEnd = std::min(m_width - 1, static_cast<int>(std::floor(xRight)));
            if (ixStart <= ixEnd) {
                if (ixStart < m_rowMinX[static_cast<std::size_t>(y)]) {
                    m_rowMinX[static_cast<std::size_t>(y)] = ixStart;
                }
                const int maxCol = (xRight >= wLimit) ? (m_width - 1) : std::min(ixEnd + 1, m_width - 1);
                if (maxCol > m_rowMaxX[static_cast<std::size_t>(y)]) {
                    m_rowMaxX[static_cast<std::size_t>(y)] = maxCol;
                }
                for (int ix = ixStart; ix <= ixEnd; ++ix) {
                    const double u0 = std::max(xLeft, static_cast<double>(ix));
                    const double u1 = std::min(xRight, static_cast<double>(ix + 1));
                    if (u1 > u0) {
                        const float h = static_cast<float>((u1 - u0) * invDx) * signedDy;
                        const float xm = static_cast<float>(0.5 * (u0 + u1) - static_cast<double>(ix));
                        row[ix] += h * (1.0f - xm);
                        row[ix + 1] += h * xm;
                    }
                }
            } else if (xRight >= wLimit) {
                m_rowMaxX[static_cast<std::size_t>(y)] = m_width - 1;
            }
        }
    }
}

void RasterDevice::addEdgeNonAA(double x0, double y0, double x1, double y1)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
        return;
    }

    int iy0 = static_cast<int>(std::floor(y0 + 0.5));
    int iy1 = static_cast<int>(std::floor(y1 + 0.5));
    if (iy0 == iy1) {
        return;
    }

    float dir = 1.0f;
    if (y0 > y1) {
        dir = -1.0f;
        std::swap(x0, x1);
        std::swap(y0, y1);
        std::swap(iy0, iy1);
    }

    const int iyStart = std::max(0, iy0);
    const int iyEnd = std::min(m_height, iy1) - 1;
    if (iyStart > iyEnd) {
        return;
    }

    if (iyStart < m_minY) {
        m_minY = iyStart;
    }
    if (iyEnd > m_maxY) {
        m_maxY = iyEnd;
    }

    const double dy = y1 - y0;
    const double dxdy = (std::abs(dy) > 1e-9) ? ((x1 - x0) / dy) : 0.0;

    for (int y = iyStart; y <= iyEnd; ++y) {
        const double yc = std::clamp(static_cast<double>(y) + 0.5, y0, y1);
        const double x = x0 + (yc - y0) * dxdy;
        const int ix = static_cast<int>(std::floor(x + 0.5));
        float *row = &m_delta[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_deltaStride)];
        if (ix <= 0) {
            row[0] += dir;
            m_rowMinX[static_cast<std::size_t>(y)] = 0;
            if (m_rowMaxX[static_cast<std::size_t>(y)] < 0) {
                m_rowMaxX[static_cast<std::size_t>(y)] = 0;
            }
        } else if (ix < m_width) {
            row[ix] += dir;
            if (ix < m_rowMinX[static_cast<std::size_t>(y)]) {
                m_rowMinX[static_cast<std::size_t>(y)] = ix;
            }
            if (ix > m_rowMaxX[static_cast<std::size_t>(y)]) {
                m_rowMaxX[static_cast<std::size_t>(y)] = ix;
            }
        } else {
            m_rowMaxX[static_cast<std::size_t>(y)] = m_width - 1;
        }
    }
}

void RasterDevice::flushScanlines(std::uint32_t srcPremul, std::uint32_t opacity8,
                                  CompositionMode mode, FillRule rule, bool antialiasing)
{
    if (m_minY > m_maxY) {
        return;
    }

    const std::uint32_t effSrc = (opacity8 == 255) ? srcPremul : byteMul(srcPremul, opacity8);
    const std::uint32_t effSrcA = effSrc >> 24;

    if (mode == CompositionMode_SourceOver && effSrcA == 0) {
        resetScanBounds(m_minY, m_maxY);
        return;
    }

    for (int y = m_minY; y <= m_maxY; ++y) {
        const int minX = m_rowMinX[static_cast<std::size_t>(y)];
        const int maxX = m_rowMaxX[static_cast<std::size_t>(y)];
        m_rowMinX[static_cast<std::size_t>(y)] = m_width;
        m_rowMaxX[static_cast<std::size_t>(y)] = -1;
        if (minX > maxX) {
            continue;
        }

        float *row = &m_delta[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_deltaStride)];
        std::uint32_t *dstRow = reinterpret_cast<std::uint32_t *>(
            m_baseBits + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_bytesPerLine));

        float acc = 0.0f;
        if (!antialiasing) {
            for (int x = minX; x <= maxX; ++x) {
                acc += row[x];
                row[x] = 0.0f;
                const int w = static_cast<int>(std::round(acc));
                const bool inside = (rule == WindingFill) ? (w != 0) : ((w & 1) != 0);
                if (inside) {
                    if (mode == CompositionMode_SourceOver) {
                        if (effSrcA == 255) {
                            dstRow[x] = effSrc;
                        } else {
                            dstRow[x] = effSrc + byteMul(dstRow[x], 255U - effSrcA);
                        }
                    } else {
                        blendPixel(dstRow[x], srcPremul, opacity8, 255U, mode);
                    }
                }
            }
        } else {
            for (int x = minX; x <= maxX; ++x) {
                acc += row[x];
                row[x] = 0.0f;
                const float absAcc = std::abs(acc);
                if (absAcc <= 1e-4f) {
                    continue;
                }
                float covF;
                if (rule == WindingFill) {
                    covF = std::min(absAcc, 1.0f);
                } else {
                    const float mod2 = std::fmod(absAcc, 2.0f);
                    covF = (mod2 > 1.0f) ? (2.0f - mod2) : mod2;
                }
                const std::uint32_t cov = static_cast<std::uint32_t>(covF * 255.0f + 0.5f);
                if (cov == 0) {
                    continue;
                }
                if (cov >= 255 && mode == CompositionMode_SourceOver) {
                    if (effSrcA == 255) {
                        dstRow[x] = effSrc;
                    } else {
                        dstRow[x] = effSrc + byteMul(dstRow[x], 255U - effSrcA);
                    }
                } else {
                    blendPixel(dstRow[x], srcPremul, opacity8, std::min(cov, 255U), mode);
                }
            }
        }
        row[maxX + 1] = 0.0f;
    }

    m_minY = m_height;
    m_maxY = -1;
}

void RasterDevice::extractPathSubpaths(const Path &path, const Transform &transform,
                                       double flattenThreshold, bool closeAll)
{
    m_points.clear();
    m_subpaths.clear();

    const int elemCount = path.elementCount();
    if (elemCount <= 0) {
        return;
    }

    const bool isIdentity = transform.isIdentity();
    const auto mapPt = [&](double x, double y) -> PointF {
        if (isIdentity) {
            return PointF(x, y);
        }
        double mx, my;
        transform.map(x, y, &mx, &my);
        return PointF(mx, my);
    };

    const double tolSq16 = 16.0 * flattenThreshold * flattenThreshold;
    int subStart = 0;
    bool haveSub = false;

    auto finishSubpath = [&]() {
        if (!haveSub) {
            return;
        }
        int count = static_cast<int>(m_points.size()) - subStart;
        if (count <= 0) {
            haveSub = false;
            return;
        }
        bool closed = closeAll;
        if (count >= 2 && pointsClose(m_points[static_cast<std::size_t>(subStart)], m_points.back())) {
            if (count > 2) {
                closed = true;
            }
            if (count > 1) {
                m_points.pop_back();
                --count;
            }
        }
        if (count > 0) {
            m_subpaths.push_back(SubpathRange{subStart, count, closed});
        }
        haveSub = false;
    };

    int i = 0;
    while (i < elemCount) {
        const Path::Element e = path.elementAt(i);
        if (e.type == Path::MoveToElement) {
            finishSubpath();
            subStart = static_cast<int>(m_points.size());
            m_points.push_back(mapPt(e.x, e.y));
            haveSub = true;
            ++i;
        } else if (e.type == Path::LineToElement) {
            if (!haveSub) {
                subStart = static_cast<int>(m_points.size());
                m_points.push_back(mapPt(0.0, 0.0));
                haveSub = true;
            }
            const PointF pt = mapPt(e.x, e.y);
            if (!pointsClose(m_points.back(), pt)) {
                m_points.push_back(pt);
            }
            ++i;
        } else if (e.type == Path::CurveToElement && i + 2 < elemCount) {
            if (!haveSub) {
                subStart = static_cast<int>(m_points.size());
                m_points.push_back(mapPt(0.0, 0.0));
                haveSub = true;
            }
            const Path::Element e1 = path.elementAt(i + 1);
            const Path::Element e2 = path.elementAt(i + 2);
            const PointF p0 = m_points.back();
            const PointF p1 = mapPt(e.x, e.y);
            const PointF p2 = mapPt(e1.x, e1.y);
            const PointF p3 = mapPt(e2.x, e2.y);
            const std::size_t before = m_points.size();
            flattenCubic(p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y(), p3.x(), p3.y(), tolSq16, m_points);
            std::size_t writeIdx = before;
            PointF prev = p0;
            for (std::size_t k = before; k < m_points.size(); ++k) {
                if (!pointsClose(prev, m_points[k])) {
                    m_points[writeIdx++] = m_points[k];
                    prev = m_points[k];
                }
            }
            m_points.resize(writeIdx);
            i += 3;
        } else {
            ++i;
        }
    }
    finishSubpath();
}

void RasterDevice::fillSubpaths(const PaintState &state, FillRule rule)
{
    if (state.brush.style() == NoBrush) {
        return;
    }
    const bool isSrcOver = (state.compositionMode == CompositionMode_SourceOver);
    const std::uint32_t opacity8 = isSrcOver ? 255U : opacityToByte(state.opacity);
    if (!isSrcOver && opacity8 == 0) {
        return;
    }
    const std::uint32_t srcPremul = colorToPremul(state.brush.color(), isSrcOver ? state.opacity : 1.0);
    if (isSrcOver && (srcPremul >> 24) == 0) {
        return;
    }

    const bool aa = state.antialiasing;
    for (const SubpathRange &sub : m_subpaths) {
        if (sub.count < 2) {
            continue;
        }
        const PointF *pts = &m_points[static_cast<std::size_t>(sub.start)];
        for (int i = 0; i < sub.count; ++i) {
            const PointF &a = pts[i];
            const PointF &b = pts[(i + 1 == sub.count) ? 0 : (i + 1)];
            if (aa) {
                addEdgeAA(a.x(), a.y(), b.x(), b.y());
            } else {
                addEdgeNonAA(a.x(), a.y(), b.x(), b.y());
            }
        }
    }
    flushScanlines(srcPremul, opacity8, state.compositionMode, rule, aa);
}

void RasterDevice::strokeSingleSubpath(const PointF *pts, int count, bool closed,
                                       double halfWidth, PenCapStyle cap, PenJoinStyle join,
                                       double miterLimit, const Transform *postTransform,
                                       bool antialiasing, double deviceScale)
{
    if (count <= 0 || halfWidth <= 0.0) {
        return;
    }

    auto emitEdge = [&](PointF a, PointF b) {
        if (postTransform) {
            a = postTransform->map(a);
            b = postTransform->map(b);
        }
        if (antialiasing) {
            addEdgeAA(a.x(), a.y(), b.x(), b.y());
        } else {
            addEdgeNonAA(a.x(), a.y(), b.x(), b.y());
        }
    };

    if (count == 1) {
        if (cap == FlatCap) {
            return;
        }
        const PointF c = pts[0];
        if (cap == SquareCap) {
            const PointF p0(c.x() - halfWidth, c.y() - halfWidth);
            const PointF p1(c.x() + halfWidth, c.y() - halfWidth);
            const PointF p2(c.x() + halfWidth, c.y() + halfWidth);
            const PointF p3(c.x() - halfWidth, c.y() + halfWidth);
            emitEdge(p0, p1);
            emitEdge(p1, p2);
            emitEdge(p2, p3);
            emitEdge(p3, p0);
        } else if (cap == RoundCap) {
            const int steps = std::clamp(static_cast<int>(halfWidth * deviceScale * 2.0) + 8, 12, 64);
            PointF prev(c.x() + halfWidth, c.y());
            for (int i = 1; i <= steps; ++i) {
                const double ang = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(steps);
                const PointF cur(c.x() + std::cos(ang) * halfWidth, c.y() + std::sin(ang) * halfWidth);
                emitEdge(prev, cur);
                prev = cur;
            }
        }
        return;
    }

    const int segCount = closed ? count : (count - 1);
    if (segCount <= 0) {
        return;
    }

    m_segDirs.resize(static_cast<std::size_t>(segCount));
    m_segNormals.resize(static_cast<std::size_t>(segCount));
    m_capTable.resize(static_cast<std::size_t>(segCount));

    for (int i = 0; i < segCount; ++i) {
        const PointF &a = pts[i];
        const PointF &b = pts[(i + 1) % count];
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        m_capTable[static_cast<std::size_t>(i)] = PointF(len, 0.0);
        if (len > 1e-9) {
            const double ux = dx / len;
            const double uy = dy / len;
            m_segDirs[static_cast<std::size_t>(i)] = PointF(ux, uy);
            m_segNormals[static_cast<std::size_t>(i)] = PointF(-uy * halfWidth, ux * halfWidth);
        } else {
            m_segDirs[static_cast<std::size_t>(i)] = PointF(1.0, 0.0);
            m_segNormals[static_cast<std::size_t>(i)] = PointF(0.0, halfWidth);
        }
    }

    const double effectiveMiterLimit = std::max(1.0, miterLimit * 2.0);
    const double minCosPlusOne = 2.0 / (effectiveMiterLimit * effectiveMiterLimit);

    auto emitArc = [&](const PointF &center, const PointF &nStart, const PointF &nEnd) {
        const double a0 = std::atan2(nStart.y(), nStart.x());
        const double a1 = std::atan2(nEnd.y(), nEnd.x());
        double da = a1 - a0;
        while (da > 0.0) {
            da -= 2.0 * kPi;
        }
        if (da < -kPi - 1e-3) {
            da = -kPi;
        }
        const int steps = std::clamp(static_cast<int>(std::abs(da) * halfWidth * deviceScale * 0.75) + 2, 2, 48);
        PointF prev(center.x() + nStart.x(), center.y() + nStart.y());
        for (int s = 1; s < steps; ++s) {
            const double ang = a0 + da * (static_cast<double>(s) / static_cast<double>(steps));
            const PointF cur(center.x() + std::cos(ang) * halfWidth,
                             center.y() + std::sin(ang) * halfWidth);
            emitEdge(prev, cur);
            prev = cur;
        }
        const PointF endPt(center.x() + nEnd.x(), center.y() + nEnd.y());
        emitEdge(prev, endPt);
    };

    // Emit join connecting from (v + nIn) to (v + nOut) around vertex v.
    // On the outer side of the turn, applies Miter/Bevel/RoundJoin.
    // On the inner side of the turn, retraces to the inner intersection to avoid self-intersection holes.
    auto emitJoin = [&](const PointF &v, const PointF &uIn, const PointF &uOut,
                        const PointF &nIn, const PointF &nOut, double maxD, bool isPlusN) {
        const PointF pIn(v.x() + nIn.x(), v.y() + nIn.y());
        const PointF pOut(v.x() + nOut.x(), v.y() + nOut.y());
        const double cross = uIn.x() * uOut.y() - uIn.y() * uOut.x();
        const double dot = uIn.x() * uOut.x() + uIn.y() * uOut.y();

        if (std::abs(cross) <= 1e-5 && dot > 0.0) {
            if (!pointsClose(pIn, pOut)) {
                emitEdge(pIn, pOut);
            }
            return;
        }

        const bool isOuter = isPlusN ? (cross < 0.0) : (cross > 0.0);
        if (!isOuter) {
            const double denom = 1.0 + dot;
            if (denom > 1e-4 && maxD > 1e-6) {
                const PointF isect(v.x() + (nIn.x() + nOut.x()) / denom,
                                   v.y() + (nIn.y() + nOut.y()) / denom);
                const double d = halfWidth * std::abs(cross) / denom;
                if (d <= maxD) {
                    emitEdge(pIn, isect);
                    emitEdge(isect, pOut);
                } else {
                    const double s = maxD / d;
                    const PointF qIn(pIn.x() + (isect.x() - pIn.x()) * s,
                                     pIn.y() + (isect.y() - pIn.y()) * s);
                    const PointF qOut(pOut.x() + (isect.x() - pOut.x()) * s,
                                      pOut.y() + (isect.y() - pOut.y()) * s);
                    emitEdge(pIn, qIn);
                    emitEdge(qIn, qOut);
                    emitEdge(qOut, pOut);
                }
            } else {
                emitEdge(pIn, pOut);
            }
            return;
        }

        if (join == RoundJoin) {
            emitArc(v, nIn, nOut);
        } else if (join == MiterJoin) {
            const double denom = 1.0 + dot;
            if (denom >= minCosPlusOne && denom > 1e-6) {
                const PointF midN((nIn.x() + nOut.x()) / denom, (nIn.y() + nOut.y()) / denom);
                const PointF miterPt(v.x() + midN.x(), v.y() + midN.y());
                emitEdge(pIn, miterPt);
                emitEdge(miterPt, pOut);
            } else {
                emitEdge(pIn, pOut);
            }
        } else {
            emitEdge(pIn, pOut);
        }
    };

    auto emitCap = [&](const PointF &v, const PointF &uDir, const PointF &nFrom, const PointF &nTo) {
        const PointF pFrom(v.x() + nFrom.x(), v.y() + nFrom.y());
        const PointF pTo(v.x() + nTo.x(), v.y() + nTo.y());
        if (cap == FlatCap) {
            emitEdge(pFrom, pTo);
        } else if (cap == SquareCap) {
            const PointF ext(uDir.x() * halfWidth, uDir.y() * halfWidth);
            const PointF c0(pFrom.x() + ext.x(), pFrom.y() + ext.y());
            const PointF c1(pTo.x() + ext.x(), pTo.y() + ext.y());
            emitEdge(pFrom, c0);
            emitEdge(c0, c1);
            emitEdge(c1, pTo);
        } else {
            const double a0 = std::atan2(nFrom.y(), nFrom.x());
            const int steps = std::clamp(static_cast<int>(kPi * halfWidth * deviceScale * 0.75) + 4, 6, 36);
            PointF prev = pFrom;
            for (int s = 1; s < steps; ++s) {
                const double ang = a0 - kPi * (static_cast<double>(s) / static_cast<double>(steps));
                const PointF cur(v.x() + std::cos(ang) * halfWidth,
                                 v.y() + std::sin(ang) * halfWidth);
                emitEdge(prev, cur);
                prev = cur;
            }
            emitEdge(prev, pTo);
        }
    };

    if (closed) {
        // Left contour (forward)
        for (int i = 0; i < count; ++i) {
            const int next = (i + 1) % count;
            const PointF &n = m_segNormals[static_cast<std::size_t>(i)];
            const PointF a(pts[i].x() + n.x(), pts[i].y() + n.y());
            const PointF b(pts[next].x() + n.x(), pts[next].y() + n.y());
            emitEdge(a, b);
            const double maxD = std::min(m_capTable[static_cast<std::size_t>(i)].x(),
                                         m_capTable[static_cast<std::size_t>(next)].x());
            emitJoin(pts[next],
                     m_segDirs[static_cast<std::size_t>(i)],
                     m_segDirs[static_cast<std::size_t>(next)],
                     n,
                     m_segNormals[static_cast<std::size_t>(next)],
                     maxD,
                     true);
        }
        // Right contour (backward, so its interior stays to its left / positive winding)
        for (int i = count - 1; i >= 0; --i) {
            const int next = (i + 1) % count;
            const int prevSeg = (i - 1 + count) % count;
            const PointF nCur(-m_segNormals[static_cast<std::size_t>(i)].x(),
                              -m_segNormals[static_cast<std::size_t>(i)].y());
            const PointF nPrev(-m_segNormals[static_cast<std::size_t>(prevSeg)].x(),
                               -m_segNormals[static_cast<std::size_t>(prevSeg)].y());
            const PointF a(pts[next].x() + nCur.x(), pts[next].y() + nCur.y());
            const PointF b(pts[i].x() + nCur.x(), pts[i].y() + nCur.y());
            emitEdge(a, b);
            const double maxD = std::min(m_capTable[static_cast<std::size_t>(i)].x(),
                                         m_capTable[static_cast<std::size_t>(prevSeg)].x());
            emitJoin(pts[i],
                     m_segDirs[static_cast<std::size_t>(prevSeg)],
                     m_segDirs[static_cast<std::size_t>(i)],
                     nCur,
                     nPrev,
                     maxD,
                     false);
        }
    } else {
        // Forward along left side
        for (int i = 0; i < segCount; ++i) {
            const PointF &n = m_segNormals[static_cast<std::size_t>(i)];
            const PointF a(pts[i].x() + n.x(), pts[i].y() + n.y());
            const PointF b(pts[i + 1].x() + n.x(), pts[i + 1].y() + n.y());
            emitEdge(a, b);
            if (i + 1 < segCount) {
                const double maxD = std::min(m_capTable[static_cast<std::size_t>(i)].x(),
                                             m_capTable[static_cast<std::size_t>(i + 1)].x());
                emitJoin(pts[i + 1],
                         m_segDirs[static_cast<std::size_t>(i)],
                         m_segDirs[static_cast<std::size_t>(i + 1)],
                         n,
                         m_segNormals[static_cast<std::size_t>(i + 1)],
                         maxD,
                         true);
            }
        }
        // End cap at pts[count - 1]
        const PointF &nLast = m_segNormals[static_cast<std::size_t>(segCount - 1)];
        const PointF &uLast = m_segDirs[static_cast<std::size_t>(segCount - 1)];
        emitCap(pts[count - 1], uLast, nLast, PointF(-nLast.x(), -nLast.y()));

        // Backward along right side
        for (int i = segCount - 1; i >= 0; --i) {
            const PointF nNeg(-m_segNormals[static_cast<std::size_t>(i)].x(),
                              -m_segNormals[static_cast<std::size_t>(i)].y());
            const PointF a(pts[i + 1].x() + nNeg.x(), pts[i + 1].y() + nNeg.y());
            const PointF b(pts[i].x() + nNeg.x(), pts[i].y() + nNeg.y());
            emitEdge(a, b);
            if (i > 0) {
                const PointF nPrevNeg(-m_segNormals[static_cast<std::size_t>(i - 1)].x(),
                                      -m_segNormals[static_cast<std::size_t>(i - 1)].y());
                const double maxD = std::min(m_capTable[static_cast<std::size_t>(i - 1)].x(),
                                             m_capTable[static_cast<std::size_t>(i)].x());
                emitJoin(pts[i],
                         m_segDirs[static_cast<std::size_t>(i - 1)],
                         m_segDirs[static_cast<std::size_t>(i)],
                         nNeg,
                         nPrevNeg,
                         maxD,
                         false);
            }
        }
        // Start cap at pts[0]
        const PointF &nFirst = m_segNormals[0];
        const PointF &uFirst = m_segDirs[0];
        emitCap(pts[0], PointF(-uFirst.x(), -uFirst.y()), PointF(-nFirst.x(), -nFirst.y()), nFirst);
    }
}

void RasterDevice::strokeSubpaths(const PaintState &state)
{
    if (state.pen.style() == NoPen) {
        return;
    }
    const bool isSrcOver = (state.compositionMode == CompositionMode_SourceOver);
    const std::uint32_t opacity8 = isSrcOver ? 255U : opacityToByte(state.opacity);
    if (!isSrcOver && opacity8 == 0) {
        return;
    }
    const std::uint32_t srcPremul = colorToPremul(state.pen.color(), isSrcOver ? state.opacity : 1.0);
    if (isSrcOver && (srcPremul >> 24) == 0) {
        return;
    }

    const bool cosmetic = state.pen.isCosmetic();
    double simScale = 1.0;
    const bool similarity = isSimilarityTransform(state.transform, &simScale);
    const bool preTransformed = cosmetic || similarity;
    const Transform *postTransform = preTransformed ? nullptr : &state.transform;

    double penWidth = cosmetic ? 1.0 : state.pen.widthF();
    if (penWidth <= 0.0) {
        return;
    }
    if (!cosmetic && similarity) {
        penWidth *= simScale;
    }
    const double deviceScale = preTransformed ? 1.0 : transformMaxScale(state.transform);
    const double halfWidth = 0.5 * penWidth;
    const PenCapStyle cap = state.pen.capStyle();
    const PenJoinStyle join = state.pen.joinStyle();
    const double miterLimit = state.pen.miterLimit();
    const bool aa = state.antialiasing;

    const Vector<double> rawDashes = state.pen.dashPattern();
    if (rawDashes.isEmpty()) {
        for (const SubpathRange &sub : m_subpaths) {
            if (sub.count <= 0) {
                continue;
            }
            strokeSingleSubpath(&m_points[static_cast<std::size_t>(sub.start)],
                                sub.count, sub.closed, halfWidth, cap, join,
                                miterLimit, postTransform, aa, deviceScale);
        }
    } else {
        std::vector<double> dashes(static_cast<std::size_t>(rawDashes.size()));
        for (std::ptrdiff_t i = 0; i < rawDashes.size(); ++i) {
            dashes[static_cast<std::size_t>(i)] = std::max(1e-4, rawDashes[i] * penWidth);
        }
        const int dashCount = static_cast<int>(dashes.size());

        for (const SubpathRange &sub : m_subpaths) {
            if (sub.count <= 0) {
                continue;
            }
            const PointF *pts = &m_points[static_cast<std::size_t>(sub.start)];
            if (sub.count == 1) {
                strokeSingleSubpath(pts, 1, false, halfWidth, cap, join,
                                    miterLimit, postTransform, aa, deviceScale);
                continue;
            }

            const int segCount = sub.closed ? sub.count : (sub.count - 1);
            int dashIdx = 0;
            double dashRem = dashes[0];
            bool dashOn = true;

            m_dashPoints.clear();
            m_dashPoints.push_back(pts[0]);

            for (int s = 0; s < segCount; ++s) {
                PointF cur = pts[s];
                const PointF target = pts[(s + 1) % sub.count];
                double dx = target.x() - cur.x();
                double dy = target.y() - cur.y();
                double segRem = std::hypot(dx, dy);
                if (segRem <= 1e-9) {
                    continue;
                }
                const double ux = dx / segRem;
                const double uy = dy / segRem;

                while (segRem > 1e-7) {
                    if (dashRem <= segRem + 1e-7) {
                        cur = PointF(cur.x() + ux * dashRem, cur.y() + uy * dashRem);
                        segRem -= dashRem;
                        if (dashOn) {
                            if (m_dashPoints.empty() || !pointsClose(m_dashPoints.back(), cur)) {
                                m_dashPoints.push_back(cur);
                            }
                            if (m_dashPoints.size() >= 2) {
                                strokeSingleSubpath(m_dashPoints.data(),
                                                    static_cast<int>(m_dashPoints.size()),
                                                    false, halfWidth, cap, join,
                                                    miterLimit, postTransform, aa, deviceScale);
                            }
                            m_dashPoints.clear();
                        }
                        dashIdx = (dashIdx + 1) % dashCount;
                        dashRem = dashes[static_cast<std::size_t>(dashIdx)];
                        dashOn = ((dashIdx & 1) == 0);
                        if (dashOn) {
                            m_dashPoints.clear();
                            m_dashPoints.push_back(cur);
                        }
                    } else {
                        dashRem -= segRem;
                        segRem = 0.0;
                        if (dashOn) {
                            if (m_dashPoints.empty() || !pointsClose(m_dashPoints.back(), target)) {
                                m_dashPoints.push_back(target);
                            }
                        }
                    }
                }
            }
            if (dashOn && m_dashPoints.size() >= 2) {
                strokeSingleSubpath(m_dashPoints.data(),
                                    static_cast<int>(m_dashPoints.size()),
                                    false, halfWidth, cap, join,
                                    miterLimit, postTransform, aa, deviceScale);
            }
        }
    }

    flushScanlines(srcPremul, opacity8, state.compositionMode, WindingFill, aa);
}

void RasterDevice::drawRects(const PaintState &state, const RectF *rects, int count)
{
    if (!rects || count <= 0 || !prepareTarget()) {
        return;
    }

    const Transform &t = state.transform;
    const bool axisAligned = (std::abs(t.m12()) <= 1e-9 && std::abs(t.m21()) <= 1e-9);

    // Fast path for non-AA axis-aligned filled rectangles without pen (e.g. fillRect)
    if (axisAligned && !state.antialiasing && state.pen.style() == NoPen && state.brush.style() != NoBrush) {
        const bool isSrcOver = (state.compositionMode == CompositionMode_SourceOver);
        const std::uint32_t opacity8 = isSrcOver ? 255U : opacityToByte(state.opacity);
        if (!isSrcOver && opacity8 == 0) {
            return;
        }
        const std::uint32_t srcPremul = colorToPremul(state.brush.color(), isSrcOver ? state.opacity : 1.0);
        const std::uint32_t effSrc = srcPremul;
        const std::uint32_t effSrcA = effSrc >> 24;
        if (isSrcOver && effSrcA == 0) {
            return;
        }

        for (int i = 0; i < count; ++i) {
            const RectF r = rects[i].normalized();
            if (r.isEmpty()) {
                continue;
            }
            double fx0 = r.left() * t.m11() + t.dx();
            double fy0 = r.top() * t.m22() + t.dy();
            double fx1 = r.right() * t.m11() + t.dx();
            double fy1 = r.bottom() * t.m22() + t.dy();
            if (fx0 > fx1) {
                std::swap(fx0, fx1);
            }
            if (fy0 > fy1) {
                std::swap(fy0, fy1);
            }
            const int x0 = std::max(0, static_cast<int>(std::floor(fx0 + 0.5)));
            const int y0 = std::max(0, static_cast<int>(std::floor(fy0 + 0.5)));
            const int x1 = std::min(m_width, static_cast<int>(std::floor(fx1 + 0.5)));
            const int y1 = std::min(m_height, static_cast<int>(std::floor(fy1 + 0.5)));
            if (x0 >= x1 || y0 >= y1) {
                continue;
            }
            const int span = x1 - x0;
            for (int y = y0; y < y1; ++y) {
                std::uint32_t *dstRow = reinterpret_cast<std::uint32_t *>(
                    m_baseBits + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_bytesPerLine)) + x0;
                if (state.compositionMode == CompositionMode_SourceOver) {
                    if (effSrcA == 255) {
                        std::fill_n(dstRow, span, effSrc);
                    } else {
                        for (int x = 0; x < span; ++x) {
                            dstRow[x] = effSrc + byteMul(dstRow[x], 255U - effSrcA);
                        }
                    }
                } else {
                    for (int x = 0; x < span; ++x) {
                        blendPixel(dstRow[x], srcPremul, opacity8, 255U, state.compositionMode);
                    }
                }
            }
        }
        return;
    }

    Path path;
    for (int i = 0; i < count; ++i) {
        const RectF r = rects[i].normalized();
        if (!r.isEmpty()) {
            path.addRect(r);
        }
    }
    drawPath(state, path);
}

void RasterDevice::drawLines(const PaintState &state, const LineF *lines, int count)
{
    if (!lines || count <= 0 || state.pen.style() == NoPen || !prepareTarget()) {
        return;
    }

    double simScale = 1.0;
    const bool preTransform = state.pen.isCosmetic() || isSimilarityTransform(state.transform, &simScale);
    const Transform &t = state.transform;

    m_points.clear();
    m_subpaths.clear();
    m_points.reserve(static_cast<std::size_t>(count) * 2);
    m_subpaths.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        PointF p1 = lines[i].p1();
        PointF p2 = lines[i].p2();
        if (preTransform && !t.isIdentity()) {
            p1 = t.map(p1);
            p2 = t.map(p2);
        }
        const int start = static_cast<int>(m_points.size());
        m_points.push_back(p1);
        if (!pointsClose(p1, p2)) {
            m_points.push_back(p2);
            m_subpaths.push_back(SubpathRange{start, 2, false});
        } else {
            m_subpaths.push_back(SubpathRange{start, 1, false});
        }
    }

    strokeSubpaths(state);
}

void RasterDevice::drawEllipse(const PaintState &state, const RectF &rect)
{
    if (rect.isNull() || !prepareTarget()) {
        return;
    }
    Path path;
    path.addEllipse(rect.normalized());
    drawPath(state, path);
}

void RasterDevice::drawPath(const PaintState &state, const Path &path)
{
    if (path.isEmpty() || !prepareTarget()) {
        return;
    }

    const bool hasFill = (state.brush.style() != NoBrush);
    const bool hasStroke = (state.pen.style() != NoPen);
    if (!hasFill && !hasStroke) {
        return;
    }

    double simScale = 1.0;
    const bool strokePreTransform = !hasStroke || state.pen.isCosmetic() || isSimilarityTransform(state.transform, &simScale);

    if (hasFill && (!hasStroke || strokePreTransform)) {
        extractPathSubpaths(path, state.transform, 0.0625, false);
        fillSubpaths(state, path.fillRule());
        if (hasStroke) {
            strokeSubpaths(state);
        }
        return;
    }

    if (hasFill) {
        extractPathSubpaths(path, state.transform, 0.0625, false);
        fillSubpaths(state, path.fillRule());
    }
    if (hasStroke) {
        if (strokePreTransform) {
            extractPathSubpaths(path, state.transform, 0.0625, false);
        } else {
            const double maxScale = std::max(1e-4, transformMaxScale(state.transform));
            extractPathSubpaths(path, Transform(), 0.0625 / maxScale, false);
        }
        strokeSubpaths(state);
    }
}

void RasterDevice::drawPoints(const PaintState &state, const PointF *points, int count)
{
    if (!points || count <= 0 || state.pen.style() == NoPen || !prepareTarget()) {
        return;
    }

    PaintState ptState = state;
    if (ptState.pen.capStyle() == FlatCap) {
        ptState.pen.setCapStyle(SquareCap);
    }

    double simScale = 1.0;
    const bool preTransform = ptState.pen.isCosmetic() || isSimilarityTransform(ptState.transform, &simScale);
    const Transform &t = ptState.transform;

    m_points.clear();
    m_subpaths.clear();
    m_points.reserve(static_cast<std::size_t>(count));
    m_subpaths.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        PointF p = points[i];
        if (preTransform && !t.isIdentity()) {
            p = t.map(p);
        }
        const int start = static_cast<int>(m_points.size());
        m_points.push_back(p);
        m_subpaths.push_back(SubpathRange{start, 1, false});
    }

    strokeSubpaths(ptState);
}

void RasterDevice::drawPolygon(const PaintState &state, const PointF *points, int count,
                               PolygonMode mode)
{
    if (!points || count <= 0 || !prepareTarget()) {
        return;
    }

    const bool closed = (mode != PolygonMode::Polyline);
    const bool hasFill = closed && (state.brush.style() != NoBrush);
    const bool hasStroke = (state.pen.style() != NoPen);
    if (!hasFill && !hasStroke) {
        return;
    }

    auto buildPolygonSubpath = [&](const Transform &t) {
        m_points.clear();
        m_subpaths.clear();
        const bool isId = t.isIdentity();
        for (int i = 0; i < count; ++i) {
            const PointF pt = isId ? points[i] : t.map(points[i]);
            if (m_points.empty() || !pointsClose(m_points.back(), pt)) {
                m_points.push_back(pt);
            }
        }
        if (closed && m_points.size() >= 2 && pointsClose(m_points.front(), m_points.back())) {
            m_points.pop_back();
        }
        if (!m_points.empty()) {
            m_subpaths.push_back(SubpathRange{0, static_cast<int>(m_points.size()), closed && m_points.size() >= 2});
        }
    };

    double simScale = 1.0;
    const bool strokePreTransform = !hasStroke || state.pen.isCosmetic() || isSimilarityTransform(state.transform, &simScale);

    if (hasFill) {
        buildPolygonSubpath(state.transform);
        const FillRule rule = (mode == PolygonMode::Winding) ? WindingFill : OddEvenFill;
        fillSubpaths(state, rule);
        if (hasStroke) {
            if (!strokePreTransform) {
                buildPolygonSubpath(Transform());
            }
            strokeSubpaths(state);
        }
    } else if (hasStroke) {
        buildPolygonSubpath(strokePreTransform ? state.transform : Transform());
        strokeSubpaths(state);
    }
}

void RasterDevice::drawImage(const PaintState &state, const RectF &target, const Image &image,
                             const RectF &source)
{
    if (image.isNull() || !prepareTarget()) {
        return;
    }

    const std::uint32_t opacity8 = opacityToByte(state.opacity);
    if (opacity8 == 0) {
        return;
    }

    const RectF tgt = target.normalized();
    const RectF src = source.normalized();
    if (tgt.isEmpty() || src.isEmpty()) {
        return;
    }

    const int srcW = image.width();
    const int srcH = image.height();
    const unsigned char *srcBase = image.constBits();
    const int srcBpl = image.bytesPerLine();
    const Image::Format fmt = image.format();
    if (!srcBase || srcW <= 0 || srcH <= 0) {
        return;
    }

    const Transform &t = state.transform;
    const bool axisAligned = (std::abs(t.m12()) <= 1e-9 && std::abs(t.m21()) <= 1e-9 && t.m11() > 0.0 && t.m22() > 0.0);

    // Fast path for unscaled 1:1 integer-aligned blits
    if (axisAligned && std::abs(t.m11() - 1.0) <= 1e-6 && std::abs(t.m22() - 1.0) <= 1e-6
        && std::abs(tgt.width() - src.width()) <= 1e-5
        && std::abs(tgt.height() - src.height()) <= 1e-5) {
        const double dx0 = tgt.left() + t.dx();
        const double dy0 = tgt.top() + t.dy();
        const int ix0 = static_cast<int>(std::round(dx0));
        const int iy0 = static_cast<int>(std::round(dy0));
        const int sx0 = static_cast<int>(std::round(src.left()));
        const int sy0 = static_cast<int>(std::round(src.top()));
        if (std::abs(dx0 - ix0) <= 1e-4 && std::abs(dy0 - iy0) <= 1e-4
            && std::abs(src.left() - sx0) <= 1e-4 && std::abs(src.top() - sy0) <= 1e-4) {
            const int w = static_cast<int>(std::round(tgt.width()));
            const int h = static_cast<int>(std::round(tgt.height()));
            int clipX0 = std::max(0, ix0);
            int clipY0 = std::max(0, iy0);
            int clipX1 = std::min(m_width, ix0 + w);
            int clipY1 = std::min(m_height, iy0 + h);
            if (sx0 + (clipX0 - ix0) < 0) {
                clipX0 = ix0 - sx0;
            }
            if (sy0 + (clipY0 - iy0) < 0) {
                clipY0 = iy0 - sy0;
            }
            if (sx0 + (clipX1 - ix0) > srcW) {
                clipX1 = ix0 + (srcW - sx0);
            }
            if (sy0 + (clipY1 - iy0) > srcH) {
                clipY1 = iy0 + (srcH - sy0);
            }
            if (clipX0 >= clipX1 || clipY0 >= clipY1) {
                return;
            }

            for (int y = clipY0; y < clipY1; ++y) {
                const int sy = sy0 + (y - iy0);
                std::uint32_t *dstRow = reinterpret_cast<std::uint32_t *>(
                    m_baseBits + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_bytesPerLine));
                for (int x = clipX0; x < clipX1; ++x) {
                    const int sx = sx0 + (x - ix0);
                    const std::uint32_t s = fetchSourcePixelPremul(srcBase, srcBpl, fmt, sx, sy);
                    blendPixel(dstRow[x], s, opacity8, 255U, state.compositionMode);
                }
            }
            return;
        }
    }

    const Transform srcToLocal = Transform::fromTranslate(-src.left(), -src.top())
        * Transform::fromScale(tgt.width() / src.width(), tgt.height() / src.height())
        * Transform::fromTranslate(tgt.left(), tgt.top());
    const Transform srcToDev = srcToLocal * t;
    bool invertible = false;
    const Transform devToSrc = srcToDev.inverted(&invertible);
    if (!invertible) {
        return;
    }

    const PointF c0 = t.map(tgt.topLeft());
    const PointF c1 = t.map(tgt.topRight());
    const PointF c2 = t.map(tgt.bottomRight());
    const PointF c3 = t.map(tgt.bottomLeft());

    const double minX = std::min({c0.x(), c1.x(), c2.x(), c3.x()});
    const double maxX = std::max({c0.x(), c1.x(), c2.x(), c3.x()});
    const double minY = std::min({c0.y(), c1.y(), c2.y(), c3.y()});
    const double maxY = std::max({c0.y(), c1.y(), c2.y(), c3.y()});

    const int ixStart = std::max(0, static_cast<int>(std::floor(minX)));
    const int iyStart = std::max(0, static_cast<int>(std::floor(minY)));
    const int ixEnd = std::min(m_width - 1, static_cast<int>(std::ceil(maxX)) - 1);
    const int iyEnd = std::min(m_height - 1, static_cast<int>(std::ceil(maxY)) - 1);
    if (ixStart > ixEnd || iyStart > iyEnd) {
        return;
    }

    const double sMinX = std::max(0.0, src.left());
    const double sMinY = std::max(0.0, src.top());
    const double sMaxX = std::min(static_cast<double>(srcW), src.right());
    const double sMaxY = std::min(static_cast<double>(srcH), src.bottom());
    if (sMinX >= sMaxX || sMinY >= sMaxY) {
        return;
    }

    const int clampIxMin = std::clamp(static_cast<int>(std::floor(sMinX)), 0, srcW - 1);
    const int clampIyMin = std::clamp(static_cast<int>(std::floor(sMinY)), 0, srcH - 1);
    const int clampIxMax = std::clamp(static_cast<int>(std::ceil(sMaxX)) - 1, 0, srcW - 1);
    const int clampIyMax = std::clamp(static_cast<int>(std::ceil(sMaxY)) - 1, 0, srcH - 1);

    const double bilerpMinX = std::max(0.0, sMinX);
    const double bilerpMinY = std::max(0.0, sMinY);
    const double bilerpMaxX = std::max(bilerpMinX, sMaxX - 1.0);
    const double bilerpMaxY = std::max(bilerpMinY, sMaxY - 1.0);

    for (int y = iyStart; y <= iyEnd; ++y) {
        std::uint32_t *dstRow = reinterpret_cast<std::uint32_t *>(
            m_baseBits + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_bytesPerLine));
        const double py = static_cast<double>(y) + 0.5;
        for (int x = ixStart; x <= ixEnd; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            double xs, ys;
            devToSrc.map(px, py, &xs, &ys);
            if (xs < sMinX || xs >= sMaxX || ys < sMinY || ys >= sMaxY) {
                continue;
            }

            std::uint32_t sample;
            if (!state.smoothImages) {
                const int sx = std::clamp(static_cast<int>(std::floor(xs)), clampIxMin, clampIxMax);
                const int sy = std::clamp(static_cast<int>(std::floor(ys)), clampIyMin, clampIyMax);
                sample = fetchSourcePixelPremul(srcBase, srcBpl, fmt, sx, sy);
            } else {
                const double fx = std::clamp(xs - 0.5, bilerpMinX, bilerpMaxX);
                const double fy = std::clamp(ys - 0.5, bilerpMinY, bilerpMaxY);
                const int x0 = std::clamp(static_cast<int>(std::floor(fx)), clampIxMin, clampIxMax);
                const int y0 = std::clamp(static_cast<int>(std::floor(fy)), clampIyMin, clampIyMax);
                const int x1 = std::clamp(x0 + 1, clampIxMin, clampIxMax);
                const int y1 = std::clamp(y0 + 1, clampIyMin, clampIyMax);
                const std::uint32_t wx = static_cast<std::uint32_t>((fx - static_cast<double>(x0)) * 256.0 + 0.5);
                const std::uint32_t wy = static_cast<std::uint32_t>((fy - static_cast<double>(y0)) * 256.0 + 0.5);

                const std::uint32_t c00 = fetchSourcePixelPremul(srcBase, srcBpl, fmt, x0, y0);
                const std::uint32_t c10 = fetchSourcePixelPremul(srcBase, srcBpl, fmt, x1, y0);
                const std::uint32_t c01 = fetchSourcePixelPremul(srcBase, srcBpl, fmt, x0, y1);
                const std::uint32_t c11 = fetchSourcePixelPremul(srcBase, srcBpl, fmt, x1, y1);
                sample = bilerpPremul(c00, c10, c01, c11, std::min(wx, 256U), std::min(wy, 256U));
            }

            blendPixel(dstRow[x], sample, opacity8, 255U, state.compositionMode);
        }
    }
}

} // namespace AnimeCore
