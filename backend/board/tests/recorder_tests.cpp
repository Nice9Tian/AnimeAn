#include "recordingdevice.h"

#include "algorithm/core/qtbridge_geometry.h"
#include "algorithm/core/qtbridge_image.h"
#include "algorithm/core/qtbridge_path.h"

#define RecordedOp QtRecordedOp
#include "recordingengine.h"
#undef RecordedOp

#include <QGuiApplication>
#include <QPaintDevice>
#include <QPainter>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL [%s:%d]: ", __FILE__, __LINE__);        \
            std::fprintf(stderr, __VA_ARGS__);                                 \
            std::fprintf(stderr, "\n");                                        \
            ++g_failures;                                                      \
            if (g_failures >= 30) return;                                      \
        }                                                                      \
    } while (0)

class DummyQtPaintDevice : public QPaintDevice
{
public:
    explicit DummyQtPaintDevice(RecordingPaintEngine *engine, int w = 1280, int h = 720)
        : m_engine(engine), m_width(w), m_height(h)
    {
    }

    QPaintEngine *paintEngine() const override { return m_engine; }

protected:
    int metric(PaintDeviceMetric m) const override
    {
        switch (m) {
        case PdmWidth:
            return m_width;
        case PdmHeight:
            return m_height;
        case PdmWidthMM:
            return int(std::lround(m_width * 25.4 / 96.0));
        case PdmHeightMM:
            return int(std::lround(m_height * 25.4 / 96.0));
        case PdmNumColors:
            return INT_MAX;
        case PdmDepth:
            return 32;
        case PdmDpiX:
        case PdmPhysicalDpiX:
            return 96;
        case PdmDpiY:
        case PdmPhysicalDpiY:
            return 96;
        case PdmDevicePixelRatio:
            return 1;
        case PdmDevicePixelRatioScaled:
            return int(devicePixelRatioFScale());
        default:
            return QPaintDevice::metric(m);
        }
    }

private:
    RecordingPaintEngine *m_engine = nullptr;
    int m_width = 1280;
    int m_height = 720;
};

struct SentCoreImage {
    std::string key;
    AnimeCore::Image image;
};

struct SentQtImage {
    QString key;
    QImage image;
};

bool compareRecordings(const char *ctx, int caseIdx,
                       const RecordingDevice &coreDev,
                       const RecordingPaintEngine &qtEng,
                       const std::vector<SentCoreImage> &coreImgs,
                       const std::vector<SentQtImage> &qtImgs,
                       std::uint64_t &totalOpsOut)
{
    const auto &cOps = coreDev.ops();
    const auto &qOps = qtEng.ops();
    if (int(cOps.size()) != int(qOps.size())) {
        std::fprintf(stderr, "FAIL [%s case=%d]: op count mismatch core=%d qt=%d\n",
                     ctx, caseIdx, int(cOps.size()), int(qOps.size()));
        const int maxN = std::max(int(cOps.size()), int(qOps.size()));
        for (int i = 0; i < std::min(maxN, 40); ++i) {
            if (i < int(cOps.size())) {
                std::fprintf(stderr, "  core[%d] (0x%016llx): %s\n",
                             i, static_cast<unsigned long long>(cOps[i].hash), cOps[i].json.c_str());
            }
            if (i < int(qOps.size())) {
                std::fprintf(stderr, "  qt  [%d] (0x%016llx): %s\n",
                             i, static_cast<unsigned long long>(qOps[i].hash), qOps[i].json.constData());
            }
        }
        ++g_failures;
        return false;
    }

    for (int i = 0; i < int(cOps.size()); ++i) {
        const RecordedOp &co = cOps[i];
        const QtRecordedOp &qo = qOps[i];
        const bool sameJson = (int(co.json.size()) == int(qo.json.size()))
                              && (std::memcmp(co.json.data(), qo.json.constData(), co.json.size()) == 0);
        if (!sameJson || co.hash != qo.hash) {
            std::fprintf(stderr,
                         "FAIL [%s case=%d op=%d]:\n  core (0x%016llx): %s\n  qt   (0x%016llx): %s\n",
                         ctx, caseIdx, i,
                         static_cast<unsigned long long>(co.hash), co.json.c_str(),
                         static_cast<unsigned long long>(qo.hash), qo.json.constData());
            ++g_failures;
            return false;
        }
    }
    totalOpsOut += static_cast<std::uint64_t>(cOps.size());

    if (coreImgs.size() != qtImgs.size()) {
        std::fprintf(stderr, "FAIL [%s case=%d]: sent image count mismatch core=%zu qt=%zu\n",
                     ctx, caseIdx, coreImgs.size(), qtImgs.size());
        ++g_failures;
        return false;
    }

    for (std::size_t k = 0; k < coreImgs.size(); ++k) {
        const std::string qKey = qtImgs[k].key.toStdString();
        if (coreImgs[k].key != qKey) {
            std::fprintf(stderr, "FAIL [%s case=%d img=%zu]: key mismatch core=%s qt=%s\n",
                         ctx, caseIdx, k, coreImgs[k].key.c_str(), qKey.c_str());
            ++g_failures;
            return false;
        }
        const AnimeCore::Image &ci = coreImgs[k].image;
        const QImage &qi = qtImgs[k].image;
        if (ci.width() != qi.width() || ci.height() != qi.height()
            || ci.format() != AnimeCore::Image::Format_RGBA8888
            || qi.format() != QImage::Format_RGBA8888) {
            std::fprintf(stderr, "FAIL [%s case=%d img=%zu]: image meta mismatch\n", ctx, caseIdx, k);
            ++g_failures;
            return false;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(ci.width()) * 4;
        for (int y = 0; y < ci.height(); ++y) {
            if (std::memcmp(ci.constScanLine(y), qi.constScanLine(y), rowBytes) != 0) {
                std::fprintf(stderr, "FAIL [%s case=%d img=%zu row=%d]: pixel mismatch\n",
                             ctx, caseIdx, k, y);
                ++g_failures;
                return false;
            }
        }
    }
    return true;
}

void testFixedOpenGLWidgetSequences(std::uint64_t &totalOps, int &totalProgs)
{
    // 1. Checkerboard background (paintBackground in openglwidget.cpp)
    {
        const AnimeCore::RectF targets[] = {
            AnimeCore::RectF(0.0, 0.0, 64.0, 48.0),
            AnimeCore::RectF(-3.25, -5.5, 68.75, 52.25),
        };
        for (int t = 0; t < 2; ++t) {
            RecordingDevice coreDev;
            RecordingPaintEngine qtEng;
            DummyQtPaintDevice qtDev(&qtEng);
            std::vector<SentCoreImage> coreImgs;
            std::vector<SentQtImage> qtImgs;

            const AnimeCore::RectF target = targets[t];
            const QRectF qTarget(target.x(), target.y(), target.width(), target.height());

            {
                AnimeCore::Painter painter(&coreDev);
                constexpr int kSquare = 8;
                painter.save();
                painter.fillRect(target, AnimeCore::Color(255, 255, 255));
                painter.setPen(AnimeCore::NoPen);
                painter.setBrush(AnimeCore::Color(204, 204, 204));
                const int left = int(std::floor(target.left()));
                const int top = int(std::floor(target.top()));
                const int right = int(std::ceil(target.right()));
                const int bottom = int(std::ceil(target.bottom()));
                for (int y = top - (top % kSquare) - kSquare; y < bottom; y += kSquare) {
                    for (int x = left - (left % kSquare) - kSquare; x < right; x += kSquare) {
                        if (((x / kSquare) + (y / kSquare)) % 2 == 0) {
                            painter.drawRect(AnimeCore::Rect(x, y, kSquare, kSquare));
                        }
                    }
                }
                painter.restore();
            }
            {
                QPainter painter(&qtDev);
                constexpr int kSquare = 8;
                painter.save();
                painter.fillRect(qTarget, QColor(255, 255, 255));
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(204, 204, 204));
                const int left = int(std::floor(qTarget.left()));
                const int top = int(std::floor(qTarget.top()));
                const int right = int(std::ceil(qTarget.right()));
                const int bottom = int(std::ceil(qTarget.bottom()));
                for (int y = top - (top % kSquare) - kSquare; y < bottom; y += kSquare) {
                    for (int x = left - (left % kSquare) - kSquare; x < right; x += kSquare) {
                        if (((x / kSquare) + (y / kSquare)) % 2 == 0) {
                            painter.drawRect(QRect(x, y, kSquare, kSquare));
                        }
                    }
                }
                painter.restore();
            }
            ++totalProgs;
            compareRecordings("fixed_checkerboard", t, coreDev, qtEng, coreImgs, qtImgs, totalOps);
        }
    }

    // 2. Ring cursor (updateBrushCursor and canvas ring in openglwidget.cpp)
    {
        for (int c = 0; c < 2; ++c) {
            const double diameter = (c == 0) ? 8.0 : 18.5;
            const double ringPen = 1.25;
            const int side = std::max(9, int(std::ceil(diameter + ringPen * 2.0 + 2.0)));

            RecordingDevice coreDev;
            RecordingPaintEngine qtEng;
            DummyQtPaintDevice qtDev(&qtEng);
            std::vector<SentCoreImage> coreImgs;
            std::vector<SentQtImage> qtImgs;

            {
                AnimeCore::Painter painter(&coreDev);
                painter.setRenderHint(AnimeCore::Painter::Antialiasing, true);
                const AnimeCore::PointF centre(side * 0.5, side * 0.5);
                const double radius = diameter * 0.5;
                painter.setBrush(AnimeCore::NoBrush);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(255, 255, 255, 200), ringPen * 2.0));
                painter.drawEllipse(centre, radius, radius);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(20, 20, 20, 230), ringPen));
                painter.drawEllipse(centre, radius, radius);
                if (diameter >= 12.0) {
                    painter.setPen(AnimeCore::Pen(AnimeCore::Color(255, 255, 255, 200), 2.0));
                    painter.drawPoint(centre);
                    painter.setPen(AnimeCore::Pen(AnimeCore::Color(20, 20, 20, 230), 1.0));
                    painter.drawPoint(centre);
                }
                // Canvas ring (eraser dashed + pen fallback ring)
                const AnimeCore::PointF hoverPos(140.25, 210.75);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(220, 0, 180), 1.5, AnimeCore::DashLine));
                painter.setBrush(AnimeCore::NoBrush);
                painter.drawEllipse(hoverPos, 14.0, 14.0);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(40, 40, 40, 170), 0.75));
                painter.setBrush(AnimeCore::NoBrush);
                painter.drawEllipse(hoverPos, 24.0, 24.0);
            }
            {
                QPainter painter(&qtDev);
                painter.setRenderHint(QPainter::Antialiasing, true);
                const QPointF centre(side * 0.5, side * 0.5);
                const qreal radius = diameter * 0.5;
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 255, 255, 200), ringPen * 2.0));
                painter.drawEllipse(centre, radius, radius);
                painter.setPen(QPen(QColor(20, 20, 20, 230), ringPen));
                painter.drawEllipse(centre, radius, radius);
                if (diameter >= 12.0) {
                    painter.setPen(QPen(QColor(255, 255, 255, 200), 2.0));
                    painter.drawPoint(centre);
                    painter.setPen(QPen(QColor(20, 20, 20, 230), 1.0));
                    painter.drawPoint(centre);
                }
                const QPointF hoverPos(140.25, 210.75);
                painter.setPen(QPen(QColor(220, 0, 180), 1.5, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(hoverPos, 14.0, 14.0);
                painter.setPen(QPen(QColor(40, 40, 40, 170), 0.75));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(hoverPos, 24.0, 24.0);
            }
            ++totalProgs;
            compareRecordings("fixed_ring_cursor", c, coreDev, qtEng, coreImgs, qtImgs, totalOps);
        }
    }

    // 3. Edit handles (paintEditHandles in openglwidget.cpp: shapes 0, 1, 2, 3, 4)
    {
        RecordingDevice coreDev;
        RecordingPaintEngine qtEng;
        DummyQtPaintDevice qtDev(&qtEng);
        std::vector<SentCoreImage> coreImgs;
        std::vector<SentQtImage> qtImgs;

        constexpr double kEditHandleScreenPx = 9.0;
        constexpr double kEditHandleButtonScale = 1.65;
        const double half = kEditHandleScreenPx * 0.5;

        {
            AnimeCore::Painter painter(&coreDev);
            for (int shape = 0; shape <= 4; ++shape) {
                const AnimeCore::PointF screen(100.0 + shape * 35.5, 200.0 - shape * 12.25);
                const AnimeCore::Color handleColor(40 + shape * 40, 120, 220 - shape * 30, 240);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(30, 30, 30, 230), 1.2));
                painter.setBrush(handleColor);
                switch (shape) {
                case 1:
                    painter.drawEllipse(screen, half, half);
                    break;
                case 2: {
                    const AnimeCore::PointF diamond[4] = {
                        screen + AnimeCore::PointF(0.0, -half - 1.5),
                        screen + AnimeCore::PointF(half + 1.5, 0.0),
                        screen + AnimeCore::PointF(0.0, half + 1.5),
                        screen + AnimeCore::PointF(-half - 1.5, 0.0)};
                    painter.drawPolygon(diamond, 4);
                    break;
                }
                case 3: {
                    const double button = half * kEditHandleButtonScale;
                    painter.drawEllipse(screen, button, button);
                    painter.setPen(AnimeCore::Pen(AnimeCore::Color(250, 250, 250, 245), 2.0));
                    const AnimeCore::PointF check[3] = {
                        screen + AnimeCore::PointF(-button * 0.5, 0.05 * button),
                        screen + AnimeCore::PointF(-button * 0.12, button * 0.45),
                        screen + AnimeCore::PointF(button * 0.55, -button * 0.45)};
                    painter.drawPolyline(check, 3);
                    break;
                }
                case 4: {
                    const double button = half * kEditHandleButtonScale;
                    painter.drawEllipse(screen, button, button);
                    painter.setPen(AnimeCore::Pen(AnimeCore::Color(250, 250, 250, 245), 2.0));
                    const double arm = button * 0.5;
                    painter.drawLine(screen + AnimeCore::PointF(-arm, -arm),
                                     screen + AnimeCore::PointF(arm, arm));
                    painter.drawLine(screen + AnimeCore::PointF(-arm, arm),
                                     screen + AnimeCore::PointF(arm, -arm));
                    break;
                }
                default:
                    painter.drawRect(AnimeCore::RectF(screen.x() - half, screen.y() - half,
                                                      kEditHandleScreenPx, kEditHandleScreenPx));
                    break;
                }
            }
        }
        {
            QPainter painter(&qtDev);
            for (int shape = 0; shape <= 4; ++shape) {
                const QPointF screen(100.0 + shape * 35.5, 200.0 - shape * 12.25);
                const QColor handleColor(40 + shape * 40, 120, 220 - shape * 30, 240);
                painter.setPen(QPen(QColor(30, 30, 30, 230), 1.2));
                painter.setBrush(handleColor);
                switch (shape) {
                case 1:
                    painter.drawEllipse(screen, half, half);
                    break;
                case 2: {
                    const QPointF diamond[4] = {
                        screen + QPointF(0.0, -half - 1.5),
                        screen + QPointF(half + 1.5, 0.0),
                        screen + QPointF(0.0, half + 1.5),
                        screen + QPointF(-half - 1.5, 0.0)};
                    painter.drawPolygon(diamond, 4);
                    break;
                }
                case 3: {
                    const qreal button = half * kEditHandleButtonScale;
                    painter.drawEllipse(screen, button, button);
                    painter.setPen(QPen(QColor(250, 250, 250, 245), 2.0));
                    const QPointF check[3] = {
                        screen + QPointF(-button * 0.5, 0.05 * button),
                        screen + QPointF(-button * 0.12, button * 0.45),
                        screen + QPointF(button * 0.55, -button * 0.45)};
                    painter.drawPolyline(check, 3);
                    break;
                }
                case 4: {
                    const qreal button = half * kEditHandleButtonScale;
                    painter.drawEllipse(screen, button, button);
                    painter.setPen(QPen(QColor(250, 250, 250, 245), 2.0));
                    const qreal arm = button * 0.5;
                    painter.drawLine(screen + QPointF(-arm, -arm), screen + QPointF(arm, arm));
                    painter.drawLine(screen + QPointF(-arm, arm), screen + QPointF(arm, -arm));
                    break;
                }
                default:
                    painter.drawRect(QRectF(screen.x() - half, screen.y() - half,
                                            kEditHandleScreenPx, kEditHandleScreenPx));
                    break;
                }
            }
        }
        ++totalProgs;
        compareRecordings("fixed_edit_handles", 0, coreDev, qtEng, coreImgs, qtImgs, totalOps);
    }

    // 4. Overlay badges (paintOverlayItems in openglwidget.cpp: accept & remove badges)
    {
        RecordingDevice coreDev;
        RecordingPaintEngine qtEng;
        DummyQtPaintDevice qtDev(&qtEng);
        std::vector<SentCoreImage> coreImgs;
        std::vector<SentQtImage> qtImgs;

        const AnimeCore::RectF badgeRects[2] = {
            AnimeCore::RectF(310.0, 84.0, 16.0, 16.0),
            AnimeCore::RectF(330.0, 84.0, 16.0, 16.0),
        };
        {
            AnimeCore::Painter painter(&coreDev);
            for (int b = 0; b < 2; ++b) {
                const bool accept = (b == 1);
                AnimeCore::Color badge(230, 80, 60, 215);
                painter.setPen(AnimeCore::NoPen);
                painter.setBrush(badge);
                painter.drawRoundedRect(badgeRects[b], 3.0, 3.0);

                const AnimeCore::RectF inner = badgeRects[b].adjusted(4.0, 4.0, -4.0, -4.0);
                painter.setPen(AnimeCore::Pen(AnimeCore::Color(255, 255, 255), 1.8,
                                              AnimeCore::SolidLine, AnimeCore::RoundCap));
                if (accept) {
                    painter.drawLine(AnimeCore::PointF(inner.left(), inner.center().y()),
                                     AnimeCore::PointF(inner.center().x() - 1.0, inner.bottom()));
                    painter.drawLine(AnimeCore::PointF(inner.center().x() - 1.0, inner.bottom()),
                                     inner.topRight());
                } else {
                    painter.drawLine(inner.topLeft(), inner.bottomRight());
                    painter.drawLine(inner.topRight(), inner.bottomLeft());
                }
            }
        }
        {
            QPainter painter(&qtDev);
            for (int b = 0; b < 2; ++b) {
                const bool accept = (b == 1);
                const QRectF qRect(badgeRects[b].x(), badgeRects[b].y(),
                                   badgeRects[b].width(), badgeRects[b].height());
                QColor badge(230, 80, 60, 215);
                painter.setPen(Qt::NoPen);
                painter.setBrush(badge);
                painter.drawRoundedRect(qRect, 3.0, 3.0);

                const QRectF inner = qRect.adjusted(4.0, 4.0, -4.0, -4.0);
                painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
                if (accept) {
                    painter.drawLine(QPointF(inner.left(), inner.center().y()),
                                     QPointF(inner.center().x() - 1.0, inner.bottom()));
                    painter.drawLine(QPointF(inner.center().x() - 1.0, inner.bottom()),
                                     inner.topRight());
                } else {
                    painter.drawLine(inner.topLeft(), inner.bottomRight());
                    painter.drawLine(inner.topRight(), inner.bottomLeft());
                }
            }
        }
        ++totalProgs;
        compareRecordings("fixed_overlay_badges", 0, coreDev, qtEng, coreImgs, qtImgs, totalOps);
    }

    // 5. Active board border (paintActiveIndicator in openglwidget.cpp)
    {
        RecordingDevice coreDev;
        RecordingPaintEngine qtEng;
        DummyQtPaintDevice qtDev(&qtEng, 960, 540);
        std::vector<SentCoreImage> coreImgs;
        std::vector<SentQtImage> qtImgs;

        const AnimeCore::Rect vpRect(0, 0, 960, 540);
        const QRect qVpRect(0, 0, 960, 540);
        const AnimeCore::Color accent(76, 154, 255);
        const QColor qAccent(76, 154, 255);

        {
            AnimeCore::Painter painter(&coreDev);
            painter.setBrush(AnimeCore::NoBrush);
            painter.setPen(AnimeCore::Pen(accent, 3.0));
            painter.drawRect(AnimeCore::RectF(vpRect).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        {
            QPainter painter(&qtDev);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(qAccent, 3.0));
            painter.drawRect(QRectF(qVpRect).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        ++totalProgs;
        compareRecordings("fixed_active_border", 0, coreDev, qtEng, coreImgs, qtImgs, totalOps);
    }
}

AnimeCore::Image makeRandomImage(std::mt19937_64 &rng, int w, int h, AnimeCore::Image::Format fmt, double dpr)
{
    AnimeCore::Image img(w, h, fmt);
    for (int y = 0; y < h; ++y) {
        unsigned char *row = img.scanLine(y);
        const int bpp = (fmt == AnimeCore::Image::Format_Grayscale8) ? 1 : 4;
        for (int x = 0; x < w * bpp; ++x) {
            row[x] = static_cast<unsigned char>(rng() & 0xffu);
        }
        if (fmt == AnimeCore::Image::Format_RGB32) {
            for (int x = 0; x < w; ++x) {
                reinterpret_cast<std::uint32_t *>(row)[x] |= 0xff000000u;
            }
        } else if (fmt == AnimeCore::Image::Format_ARGB32_Premultiplied) {
            for (int x = 0; x < w; ++x) {
                std::uint32_t px = reinterpret_cast<std::uint32_t *>(row)[x];
                const unsigned int a = (px & 0x80000000u) ? 0xffu : 0x00u;
                const unsigned int r = (((px >> 16) & 0xffu) * a) / 255u;
                const unsigned int g = (((px >> 8) & 0xffu) * a) / 255u;
                const unsigned int b = ((px & 0xffu) * a) / 255u;
                reinterpret_cast<std::uint32_t *>(row)[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
    img.setDevicePixelRatio(dpr);
    return img;
}

void testRandomPrograms(std::uint64_t &totalOps, int &totalProgs)
{
    std::mt19937_64 rng(0x5EED20260418ULL);
    std::uniform_real_distribution<double> coordDist(-400.0, 800.0);
    std::uniform_real_distribution<double> sizeDist(-200.0, 300.0);
    std::uniform_real_distribution<double> posSizeDist(0.5, 250.0);
    std::uniform_real_distribution<double> radDist(-20.0, 120.0);
    std::uniform_real_distribution<double> widthDist(0.0, 12.0);
    std::uniform_real_distribution<double> scaleDist(0.25, 4.0);
    std::uniform_real_distribution<double> opacityDist(-0.2, 1.2);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> callCountDist(30, 42);
    std::uniform_int_distribution<int> opKindDist(0, 23);

    // Pre-build a pool of test images with different formats and DPRs
    const AnimeCore::Image::Format kFormats[] = {
        AnimeCore::Image::Format_RGBA8888,
        AnimeCore::Image::Format_ARGB32,
        AnimeCore::Image::Format_ARGB32_Premultiplied,
        AnimeCore::Image::Format_RGB32,
        AnimeCore::Image::Format_Grayscale8,
    };
    const double kDprs[] = {1.0, 1.0, 2.0, 1.5};
    std::vector<AnimeCore::Image> coreImgPool;
    std::vector<QImage> qtImgPool;
    for (int i = 0; i < 16; ++i) {
        const int w = 4 + (i % 7) * 3;
        const int h = 4 + ((i / 2) % 5) * 3;
        const auto fmt = kFormats[i % 5];
        const double dpr = kDprs[i % 4];
        AnimeCore::Image ci = makeRandomImage(rng, w, h, fmt, dpr);
        QImage qi = ci; qi.setDevicePixelRatio(dpr);
        coreImgPool.push_back(ci);
        qtImgPool.push_back(qi);
    }

    constexpr int kNumPrograms = 2200;

    for (int prog = 0; prog < kNumPrograms; ++prog) {
        if (g_failures >= 30) return;

        RecordingDevice coreDev;
        RecordingPaintEngine qtEng;
        DummyQtPaintDevice qtDev(&qtEng);

        std::vector<SentCoreImage> coreImgs;
        std::vector<SentQtImage> qtImgs;
        coreDev.setImageSink([&](const std::string &key, const AnimeCore::Image &img) {
            coreImgs.push_back({key, img});
        });
        qtEng.setImageSink([&](const QString &key, const QImage &img) {
            qtImgs.push_back({key, img});
        });

        AnimeCore::Painter cp(&coreDev);
        QPainter qp(&qtDev);

        int saveDepth = 0;
        double lastOpacity = 1.0;
        const int numCalls = callCountDist(rng);

        for (int step = 0; step < numCalls; ++step) {
            const int kind = opKindDist(rng);
            switch (kind) {
            case 0: { // save
                cp.save();
                qp.save();
                ++saveDepth;
                break;
            }
            case 1: { // restore
                if (saveDepth > 0) {
                    cp.restore();
                    qp.restore();
                    --saveDepth;
                }
                break;
            }
            case 2: { // setRenderHint
                const bool aa = (rng() & 1) != 0;
                const bool sm = (rng() & 2) != 0;
                cp.setRenderHint(AnimeCore::Painter::Antialiasing, aa);
                qp.setRenderHint(QPainter::Antialiasing, aa);
                cp.setRenderHint(AnimeCore::Painter::SmoothPixmapTransform, sm);
                qp.setRenderHint(QPainter::SmoothPixmapTransform, sm);
                break;
            }
            case 3: { // setPen(Pen)
                const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng);
                const int a = (rng() % 8 == 0) ? 0 : byteDist(rng);
                const double w = (rng() % 6 == 0) ? 0.0 : widthDist(rng);
                const AnimeCore::PenStyle styles[] = {
                    AnimeCore::NoPen, AnimeCore::SolidLine, AnimeCore::DashLine,
                    AnimeCore::DotLine, AnimeCore::DashDotLine, AnimeCore::DashDotDotLine};
                const Qt::PenStyle qStyles[] = {
                    Qt::NoPen, Qt::SolidLine, Qt::DashLine,
                    Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine};
                const AnimeCore::PenCapStyle caps[] = {
                    AnimeCore::FlatCap, AnimeCore::SquareCap, AnimeCore::RoundCap};
                const Qt::PenCapStyle qCaps[] = {
                    Qt::FlatCap, Qt::SquareCap, Qt::RoundCap};
                const AnimeCore::PenJoinStyle joins[] = {
                    AnimeCore::MiterJoin, AnimeCore::BevelJoin, AnimeCore::RoundJoin};
                const Qt::PenJoinStyle qJoins[] = {
                    Qt::MiterJoin, Qt::BevelJoin, Qt::RoundJoin};

                const int si = int(rng() % 6);
                const int ci = int(rng() % 3);
                const int ji = int(rng() % 3);
                AnimeCore::Pen cPen(AnimeCore::Color(r, g, b, a), w, styles[si], caps[ci], joins[ji]);
                QPen qPen(QColor(r, g, b, a), w, qStyles[si], qCaps[ci], qJoins[ji]);
                cp.setPen(cPen);
                qp.setPen(qPen);
                break;
            }
            case 4: { // setPen(Color) or setPen(PenStyle)
                if (rng() & 1) {
                    const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng), a = byteDist(rng);
                    cp.setPen(AnimeCore::Color(r, g, b, a));
                    qp.setPen(QColor(r, g, b, a));
                } else {
                    const bool noPen = (rng() & 1) != 0;
                    cp.setPen(noPen ? AnimeCore::NoPen : AnimeCore::SolidLine);
                    qp.setPen(noPen ? Qt::NoPen : Qt::SolidLine);
                }
                break;
            }
            case 5: { // setBrush
                const int sub = int(rng() % 3);
                if (sub == 0) {
                    cp.setBrush(AnimeCore::NoBrush);
                    qp.setBrush(Qt::NoBrush);
                } else if (sub == 1) {
                    const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng);
                    const int a = (rng() % 7 == 0) ? 0 : byteDist(rng);
                    cp.setBrush(AnimeCore::Color(r, g, b, a));
                    qp.setBrush(QColor(r, g, b, a));
                } else {
                    const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng), a = byteDist(rng);
                    cp.setBrush(AnimeCore::Brush(AnimeCore::Color(r, g, b, a)));
                    qp.setBrush(QBrush(QColor(r, g, b, a)));
                }
                break;
            }
            case 6: { // setOpacity
                double op = (rng() % 6 == 0) ? (lastOpacity + 1e-11) : opacityDist(rng);
                cp.setOpacity(op);
                qp.setOpacity(op);
                lastOpacity = std::min(1.0, std::max(0.0, op));
                break;
            }
            case 7: { // setCompositionMode
                const bool srcIn = (rng() % 3 == 0);
                cp.setCompositionMode(srcIn ? AnimeCore::CompositionMode_SourceIn
                                            : AnimeCore::CompositionMode_SourceOver);
                qp.setCompositionMode(srcIn ? QPainter::CompositionMode_SourceIn
                                            : QPainter::CompositionMode_SourceOver);
                break;
            }
            case 8: { // translate
                const double dx = coordDist(rng) * 0.25;
                const double dy = coordDist(rng) * 0.25;
                if (rng() & 1) {
                    cp.translate(dx, dy);
                    qp.translate(dx, dy);
                } else {
                    cp.translate(AnimeCore::PointF(dx, dy));
                    qp.translate(QPointF(dx, dy));
                }
                break;
            }
            case 9: { // scale
                const double sx = scaleDist(rng);
                const double sy = scaleDist(rng);
                cp.scale(sx, sy);
                qp.scale(sx, sy);
                break;
            }
            case 10: { // setWorldTransform
                const double m11 = scaleDist(rng), m12 = (rng() % 3 == 0) ? 0.15 : 0.0;
                const double m21 = (rng() % 3 == 0) ? -0.15 : 0.0, m22 = scaleDist(rng);
                const double dx = coordDist(rng) * 0.5, dy = coordDist(rng) * 0.5;
                const AnimeCore::Transform tr(m11, m12, m21, m22, dx, dy);
                const QTransform qtr(m11, m12, m21, m22, dx, dy);
                cp.setWorldTransform(tr);
                qp.setWorldTransform(qtr);
                break;
            }
            case 11: { // fillRect(RectF)
                const double x = coordDist(rng), y = coordDist(rng);
                const double w = posSizeDist(rng), h = posSizeDist(rng);
                const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng);
                const int a = 1 + (byteDist(rng) % 255);
                cp.fillRect(AnimeCore::RectF(x, y, w, h), AnimeCore::Color(r, g, b, a));
                qp.fillRect(QRectF(x, y, w, h), QColor(r, g, b, a));
                break;
            }
            case 12: { // fillRect(Rect)
                const int x = int(coordDist(rng)), y = int(coordDist(rng));
                const int w = 1 + int(posSizeDist(rng)), h = 1 + int(posSizeDist(rng));
                const int r = byteDist(rng), g = byteDist(rng), b = byteDist(rng);
                const int a = 1 + (byteDist(rng) % 255);
                cp.fillRect(AnimeCore::Rect(x, y, w, h), AnimeCore::Color(r, g, b, a));
                qp.fillRect(QRect(x, y, w, h), QColor(r, g, b, a));
                break;
            }
            case 13: { // drawRect(RectF)
                const double x = coordDist(rng), y = coordDist(rng);
                const double w = posSizeDist(rng), h = posSizeDist(rng);
                cp.drawRect(AnimeCore::RectF(x, y, w, h));
                qp.drawRect(QRectF(x, y, w, h));
                break;
            }
            case 14: { // drawRect(Rect)
                const int x = int(coordDist(rng)), y = int(coordDist(rng));
                const int w = 1 + int(posSizeDist(rng)), h = 1 + int(posSizeDist(rng));
                cp.drawRect(AnimeCore::Rect(x, y, w, h));
                qp.drawRect(QRect(x, y, w, h));
                break;
            }
            case 15: { // drawLine
                const double x1 = coordDist(rng), y1 = coordDist(rng);
                const double x2 = coordDist(rng), y2 = coordDist(rng);
                if (rng() & 1) {
                    cp.drawLine(AnimeCore::LineF(x1, y1, x2, y2));
                    qp.drawLine(QLineF(x1, y1, x2, y2));
                } else {
                    cp.drawLine(AnimeCore::PointF(x1, y1), AnimeCore::PointF(x2, y2));
                    qp.drawLine(QPointF(x1, y1), QPointF(x2, y2));
                }
                break;
            }
            case 16: { // drawEllipse(RectF)
                const double x = coordDist(rng), y = coordDist(rng);
                const double w = sizeDist(rng), h = sizeDist(rng);
                cp.drawEllipse(AnimeCore::RectF(x, y, w, h));
                qp.drawEllipse(QRectF(x, y, w, h));
                break;
            }
            case 17: { // drawEllipse(center, rx, ry)
                const double cx = coordDist(rng), cy = coordDist(rng);
                const double rx = posSizeDist(rng) * 0.5, ry = posSizeDist(rng) * 0.5;
                cp.drawEllipse(AnimeCore::PointF(cx, cy), rx, ry);
                qp.drawEllipse(QPointF(cx, cy), rx, ry);
                break;
            }
            case 18: { // drawPoint
                const double x = coordDist(rng), y = coordDist(rng);
                cp.drawPoint(AnimeCore::PointF(x, y));
                qp.drawPoint(QPointF(x, y));
                break;
            }
            case 19: { // drawPolygon
                const int n = 3 + int(rng() % 6);
                AnimeCore::Vector<AnimeCore::PointF> cPts;
                QVector<QPointF> qPts;
                for (int i = 0; i < n; ++i) {
                    const double x = coordDist(rng), y = coordDist(rng);
                    cPts.append(AnimeCore::PointF(x, y));
                    qPts.append(QPointF(x, y));
                }
                const bool winding = (rng() & 1) != 0;
                if (rng() & 1) {
                    cp.drawPolygon(cPts.constData(), n,
                                   winding ? AnimeCore::WindingFill : AnimeCore::OddEvenFill);
                } else {
                    cp.drawPolygon(cPts,
                                   winding ? AnimeCore::WindingFill : AnimeCore::OddEvenFill);
                }
                qp.drawPolygon(qPts.constData(), n,
                               winding ? Qt::WindingFill : Qt::OddEvenFill);
                break;
            }
            case 20: { // drawPolyline
                const int n = 2 + int(rng() % 6);
                std::vector<AnimeCore::PointF> cPts(static_cast<std::size_t>(n));
                std::vector<QPointF> qPts(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i) {
                    const double x = coordDist(rng), y = coordDist(rng);
                    cPts[static_cast<std::size_t>(i)] = AnimeCore::PointF(x, y);
                    qPts[static_cast<std::size_t>(i)] = QPointF(x, y);
                }
                cp.drawPolyline(cPts.data(), n);
                qp.drawPolyline(qPts.data(), n);
                break;
            }
            case 21: { // drawRoundedRect
                const double x = coordDist(rng), y = coordDist(rng);
                const double w = posSizeDist(rng), h = posSizeDist(rng);
                const double rx = radDist(rng), ry = radDist(rng);
                cp.drawRoundedRect(AnimeCore::RectF(x, y, w, h), rx, ry);
                qp.drawRoundedRect(QRectF(x, y, w, h), rx, ry);
                break;
            }
            case 22: { // drawPath (lines + cubics + subpaths)
                AnimeCore::Path cPath;
                QPainterPath qPath;
                if (rng() & 1) {
                    cPath.setFillRule(AnimeCore::WindingFill);
                    qPath.setFillRule(Qt::WindingFill);
                }
                const int segs = int(rng() % 7); // 0 tests empty path
                if (segs > 0) {
                    const double sx = coordDist(rng), sy = coordDist(rng);
                    cPath.moveTo(sx, sy);
                    qPath.moveTo(sx, sy);
                    for (int s = 0; s < segs; ++s) {
                        const int sk = int(rng() % 4);
                        if (sk == 0) {
                            const double lx = coordDist(rng), ly = coordDist(rng);
                            cPath.lineTo(lx, ly);
                            qPath.lineTo(lx, ly);
                        } else if (sk == 1) {
                            const double x1 = coordDist(rng), y1 = coordDist(rng);
                            const double x2 = coordDist(rng), y2 = coordDist(rng);
                            const double x3 = coordDist(rng), y3 = coordDist(rng);
                            cPath.cubicTo(x1, y1, x2, y2, x3, y3);
                            qPath.cubicTo(x1, y1, x2, y2, x3, y3);
                        } else if (sk == 2) {
                            cPath.closeSubpath();
                            qPath.closeSubpath();
                        } else {
                            const double mx = coordDist(rng), my = coordDist(rng);
                            cPath.moveTo(mx, my);
                            qPath.moveTo(mx, my);
                        }
                    }
                }
                cp.drawPath(cPath);
                qp.drawPath(qPath);
                break;
            }
            case 23: { // drawImage
                const std::size_t idx = static_cast<std::size_t>(rng() % coreImgPool.size());
                const double tx = coordDist(rng), ty = coordDist(rng);
                cp.drawImage(AnimeCore::PointF(tx, ty), coreImgPool[idx]);
                qp.drawImage(QPointF(tx, ty), qtImgPool[idx]);
                break;
            }
            }
        }

        cp.end();
        qp.end();

        ++totalProgs;
        if (!compareRecordings("random_program", prog, coreDev, qtEng, coreImgs, qtImgs, totalOps)) {
            return;
        }
    }
}

} // namespace

void testPainterEdgeCases(std::uint64_t &totalOps, int &totalProgs)
{
    auto runSingleEdgeCase = [&](const char *name, int subIdx,
                                 auto coreFn, auto qtFn) {
        RecordingDevice coreDev;
        RecordingPaintEngine qtEng;
        DummyQtPaintDevice qtDev(&qtEng);
        std::vector<SentCoreImage> coreImgs;
        std::vector<SentQtImage> qtImgs;
        {
            AnimeCore::Painter cp(&coreDev);
            coreFn(cp);
        }
        {
            QPainter qp(&qtDev);
            qtFn(qp);
        }
        ++totalProgs;
        compareRecordings(name, subIdx, coreDev, qtEng, coreImgs, qtImgs, totalOps);
    };

    // Edge case 1: default pen & brush without explicit setPen/setBrush
    runSingleEdgeCase("edge_default_pen_brush", 0,
        [](AnimeCore::Painter &p) {
            p.drawRect(AnimeCore::RectF(10.0, 20.0, 30.0, 40.0));
        },
        [](QPainter &p) {
            p.drawRect(QRectF(10.0, 20.0, 30.0, 40.0));
        });

    // Edge case 2: fillRect followed by drawRect (state restoration)
    runSingleEdgeCase("edge_fillRect_restore", 0,
        [](AnimeCore::Painter &p) {
            p.setPen(AnimeCore::Pen(AnimeCore::Color(10, 20, 30, 200), 2.5));
            p.setBrush(AnimeCore::Color(40, 50, 60, 180));
            p.fillRect(AnimeCore::RectF(0.0, 0.0, 50.0, 50.0), AnimeCore::Color(255, 0, 0, 255));
            p.drawRect(AnimeCore::RectF(5.0, 5.0, 20.0, 20.0));
        },
        [](QPainter &p) {
            p.setPen(QPen(QColor(10, 20, 30, 200), 2.5));
            p.setBrush(QColor(40, 50, 60, 180));
            p.fillRect(QRectF(0.0, 0.0, 50.0, 50.0), QColor(255, 0, 0, 255));
            p.drawRect(QRectF(5.0, 5.0, 20.0, 20.0));
        });

    // Edge case 3: fillRect with alpha=0, zero/negative size
    runSingleEdgeCase("edge_fillRect_special", 0,
        [](AnimeCore::Painter &p) {
            p.fillRect(AnimeCore::RectF(1.0, 2.0, 10.0, 20.0), AnimeCore::Color(100, 100, 100, 0));
            p.fillRect(AnimeCore::RectF(1.0, 2.0, 0.0, 20.0), AnimeCore::Color(100, 100, 100, 255));
            p.fillRect(AnimeCore::RectF(1.0, 2.0, -10.0, 20.0), AnimeCore::Color(100, 100, 100, 255));
            p.drawRect(AnimeCore::RectF(5.0, 5.0, 20.0, 20.0));
        },
        [](QPainter &p) {
            p.fillRect(QRectF(1.0, 2.0, 10.0, 20.0), QColor(100, 100, 100, 0));
            p.fillRect(QRectF(1.0, 2.0, 0.0, 20.0), QColor(100, 100, 100, 255));
            p.fillRect(QRectF(1.0, 2.0, -10.0, 20.0), QColor(100, 100, 100, 255));
            p.drawRect(QRectF(5.0, 5.0, 20.0, 20.0));
        });

    // Edge case 4: drawRect with negative / zero width & height
    runSingleEdgeCase("edge_drawRect_special", 0,
        [](AnimeCore::Painter &p) {
            p.drawRect(AnimeCore::RectF(10.0, 20.0, 0.0, 30.0));
            p.drawRect(AnimeCore::RectF(10.0, 20.0, -30.0, 40.0));
            p.drawRect(AnimeCore::Rect(10, 20, 0, 30));
            p.drawRect(AnimeCore::Rect(10, 20, -30, 40));
        },
        [](QPainter &p) {
            p.drawRect(QRectF(10.0, 20.0, 0.0, 30.0));
            p.drawRect(QRectF(10.0, 20.0, -30.0, 40.0));
            p.drawRect(QRect(10, 20, 0, 30));
            p.drawRect(QRect(10, 20, -30, 40));
        });

    // Edge case 5: drawRoundedRect with zero/negative radii & empty rect
    runSingleEdgeCase("edge_drawRoundedRect_special", 0,
        [](AnimeCore::Painter &p) {
            p.drawRoundedRect(AnimeCore::RectF(10.0, 20.0, 40.0, 30.0), 0.0, 5.0);
            p.drawRoundedRect(AnimeCore::RectF(10.0, 20.0, 40.0, 30.0), -2.0, 5.0);
            p.drawRoundedRect(AnimeCore::RectF(10.0, 20.0, 0.0, 30.0), 5.0, 5.0);
            p.drawRoundedRect(AnimeCore::RectF(10.0, 20.0, -40.0, 30.0), 5.0, 5.0);
        },
        [](QPainter &p) {
            p.drawRoundedRect(QRectF(10.0, 20.0, 40.0, 30.0), 0.0, 5.0);
            p.drawRoundedRect(QRectF(10.0, 20.0, 40.0, 30.0), -2.0, 5.0);
            p.drawRoundedRect(QRectF(10.0, 20.0, 0.0, 30.0), 5.0, 5.0);
            p.drawRoundedRect(QRectF(10.0, 20.0, -40.0, 30.0), 5.0, 5.0);
        });

    // Edge case 6: drawPolygon / drawPolyline with small counts (0, 1, 2)
    runSingleEdgeCase("edge_polygon_counts", 0,
        [](AnimeCore::Painter &p) {
            const AnimeCore::PointF pts[2] = {AnimeCore::PointF(1.0, 2.0), AnimeCore::PointF(3.0, 4.0)};
            p.drawPolygon(pts, 0);
            p.drawPolygon(pts, 1);
            p.drawPolygon(pts, 2);
            p.drawPolyline(pts, 0);
            p.drawPolyline(pts, 1);
            p.drawPolyline(pts, 2);
        },
        [](QPainter &p) {
            const QPointF pts[2] = {QPointF(1.0, 2.0), QPointF(3.0, 4.0)};
            p.drawPolygon(pts, 0);
            p.drawPolygon(pts, 1);
            p.drawPolygon(pts, 2);
            p.drawPolyline(pts, 0);
            p.drawPolyline(pts, 1);
            p.drawPolyline(pts, 2);
        });
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::uint64_t totalOps = 0;
    int totalProgs = 0;

    testFixedOpenGLWidgetSequences(totalOps, totalProgs);
    testPainterEdgeCases(totalOps, totalProgs);
    testRandomPrograms(totalOps, totalProgs);

    if (g_failures != 0) {
        std::fprintf(stderr, "RECORDER TESTS FAILED with %d failures.\n", g_failures);
        return 1;
    }

    std::printf("ALL RECORDER TESTS PASSED: %d programs, %llu ops verified byte-for-byte.\n",
                totalProgs, static_cast<unsigned long long>(totalOps));
    return 0;
}
