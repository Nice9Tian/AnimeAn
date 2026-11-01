#include "algorithm/core/painter.h"
#include "algorithm/core/qtbridge.h"
#include "algorithm/core/rasterdevice.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace AnimeCore;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct DiffStats {
    const char *name = "";
    bool requireExact = false;
    std::int64_t totalPixels = 0;
    std::int64_t exactPixels = 0;
    std::int64_t le16Pixels = 0;
    std::int64_t gt64Pixels = 0;
    int maxChannelDiff = 0;
    std::int64_t qtAlphaSum = 0;
    std::int64_t coreAlphaSum = 0;

    double pctLe16() const
    {
        return totalPixels > 0 ? (100.0 * static_cast<double>(le16Pixels) / static_cast<double>(totalPixels)) : 100.0;
    }

    double pctGt64() const
    {
        return totalPixels > 0 ? (100.0 * static_cast<double>(gt64Pixels) / static_cast<double>(totalPixels)) : 0.0;
    }

    double alphaRelDiffPct() const
    {
        if (qtAlphaSum == 0 && coreAlphaSum == 0) {
            return 0.0;
        }
        const double denom = static_cast<double>(std::max<std::int64_t>(1, qtAlphaSum));
        return 100.0 * std::abs(static_cast<double>(coreAlphaSum - qtAlphaSum)) / denom;
    }
};

DiffStats compareImages(const char *name, const QImage &qtImg, const Image &coreImg, bool requireExact = false)
{
    DiffStats s;
    s.name = name;
    s.requireExact = requireExact;

    if (qtImg.width() != coreImg.width() || qtImg.height() != coreImg.height()) {
        std::fprintf(stderr, "FAIL [%s]: size mismatch Qt(%dx%d) vs Core(%dx%d)\n",
                     name, qtImg.width(), qtImg.height(), coreImg.width(), coreImg.height());
        std::exit(1);
    }

    const int w = qtImg.width();
    const int h = qtImg.height();
    s.totalPixels = static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h);

    for (int y = 0; y < h; ++y) {
        const std::uint32_t *qRow = reinterpret_cast<const std::uint32_t *>(qtImg.constScanLine(y));
        const std::uint32_t *cRow = reinterpret_cast<const std::uint32_t *>(coreImg.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const std::uint32_t qp = qRow[x];
            const std::uint32_t cp = cRow[x];

            const int qa = static_cast<int>((qp >> 24) & 0xffU);
            const int qr = static_cast<int>((qp >> 16) & 0xffU);
            const int qg = static_cast<int>((qp >> 8) & 0xffU);
            const int qb = static_cast<int>(qp & 0xffU);

            const int ca = static_cast<int>((cp >> 24) & 0xffU);
            const int cr = static_cast<int>((cp >> 16) & 0xffU);
            const int cg = static_cast<int>((cp >> 8) & 0xffU);
            const int cb = static_cast<int>(cp & 0xffU);

            s.qtAlphaSum += qa;
            s.coreAlphaSum += ca;

            const int da = std::abs(qa - ca);
            const int dr = std::abs(qr - cr);
            const int dg = std::abs(qg - cg);
            const int db = std::abs(qb - cb);
            const int dMax = std::max({da, dr, dg, db});

            if (dMax == 0) {
                ++s.exactPixels;
            }
            if (dMax <= 16) {
                ++s.le16Pixels;
            }
            if (dMax > 64) {
                ++s.gt64Pixels;
            }
            if (dMax > s.maxChannelDiff) {
                s.maxChannelDiff = dMax;
            }
        }
    }

    std::printf("SCENE %-24s | px=%7lld | le16=%6.2f%% | gt64=%6.4f%% | alphaDiff=%6.3f%% | maxDiff=%3d%s\n",
                s.name,
                static_cast<long long>(s.totalPixels),
                s.pctLe16(),
                s.pctGt64(),
                s.alphaRelDiffPct(),
                s.maxChannelDiff,
                s.requireExact ? " [EXACT]" : "");
    std::fflush(stdout);

    if (s.requireExact && s.maxChannelDiff != 0) {
        for (int y = 0; y < h; ++y) {
            const std::uint32_t *qRow = reinterpret_cast<const std::uint32_t *>(qtImg.constScanLine(y));
            const std::uint32_t *cRow = reinterpret_cast<const std::uint32_t *>(coreImg.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                if (qRow[x] != cRow[x]) {
                    std::printf("  FIRST DIFF [%s] at (%d,%d): Qt=0x%08x Core=0x%08x\n",
                                s.name, x, y, qRow[x], cRow[x]);
                    y = h;
                    break;
                }
            }
        }
    }
    return s;
}

void accumulateStats(DiffStats &total, const DiffStats &scene)
{
    total.totalPixels += scene.totalPixels;
    total.exactPixels += scene.exactPixels;
    total.le16Pixels += scene.le16Pixels;
    total.gt64Pixels += scene.gt64Pixels;
    total.qtAlphaSum += scene.qtAlphaSum;
    total.coreAlphaSum += scene.coreAlphaSum;
    if (scene.maxChannelDiff > total.maxChannelDiff) {
        total.maxChannelDiff = scene.maxChannelDiff;
    }
}

// 1. Checkerboard + rectangle-only scene (exact match required)
DiffStats testCheckerboard()
{
    const int w = 320;
    const int h = 240;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(0);
    cImg.fill(0);

    const QColor qLight(66, 69, 74);
    const QColor qDark(54, 57, 62);
    const Color cLight(66, 69, 74);
    const Color cDark(54, 57, 62);
    const int tileSize = 16;

    {
        QPainter qp(&qImg);
        for (int y = 0; y < h; y += tileSize) {
            for (int x = 0; x < w; x += tileSize) {
                const bool isEven = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                qp.fillRect(x, y, tileSize, tileSize, isEven ? qLight : qDark);
            }
        }
        qp.fillRect(QRectF(40, 30, 200, 150), QColor(255, 255, 255, 255));
        qp.fillRect(QRectF(80, 60, 120, 90), QColor(30, 144, 255, 180));
        qp.setOpacity(0.5);
        qp.fillRect(QRectF(100, 50, 140, 110), QColor(220, 50, 80, 200));
    }
    {
        Painter cp(&cImg);
        for (int y = 0; y < h; y += tileSize) {
            for (int x = 0; x < w; x += tileSize) {
                const bool isEven = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                cp.fillRect(Rect(x, y, tileSize, tileSize), isEven ? cLight : cDark);
            }
        }
        cp.fillRect(RectF(40, 30, 200, 150), Color(255, 255, 255, 255));
        cp.fillRect(RectF(80, 60, 120, 90), Color(30, 144, 255, 180));
        cp.setOpacity(0.5);
        cp.fillRect(RectF(100, 50, 140, 110), Color(220, 50, 80, 200));
    }

    return compareImages("checkerboard_rects", qImg, cImg, true);
}

// 2. Non-AA fills (exact match required)
DiffStats testNonAAFills()
{
    const int w = 300;
    const int h = 220;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(Qt::transparent);
    cImg.fill(0);

    // Axis-aligned integer polygons and compound paths with OddEven and Winding fill rules
    QPainterPath qPathOdd;
    qPathOdd.setFillRule(Qt::OddEvenFill);
    qPathOdd.addRect(QRectF(20, 20, 120, 120));
    qPathOdd.addRect(QRectF(50, 50, 60, 60));
    qPathOdd.addRect(QRectF(65, 65, 30, 30));

    QPainterPath qPathWind;
    qPathWind.setFillRule(Qt::WindingFill);
    qPathWind.addRect(QRectF(160, 20, 120, 120));
    qPathWind.addRect(QRectF(190, 50, 60, 60));

    // L-shaped integer polygon
    QPolygonF qPoly;
    qPoly << QPointF(20, 155) << QPointF(140, 155) << QPointF(140, 180)
          << QPointF(60, 180) << QPointF(60, 205) << QPointF(20, 205);

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, false);
        qp.setPen(Qt::NoPen);
        qp.setBrush(QColor(240, 90, 70, 255));
        qp.drawPath(qPathOdd);
        qp.setBrush(QColor(60, 170, 110, 220));
        qp.drawPath(qPathWind);
        qp.setBrush(QColor(80, 120, 240, 255));
        qp.drawPolygon(qPoly, Qt::OddEvenFill);
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, false);
        cp.setPen(NoPen);
        cp.setBrush(Color(240, 90, 70, 255));
        cp.drawPath(Path(qPathOdd));
        cp.setBrush(Color(60, 170, 110, 220));
        cp.drawPath(Path(qPathWind));
        cp.setBrush(Color(80, 120, 240, 255));
        Vector<PointF> cPoly;
        for (const QPointF &pt : qPoly) {
            cPoly.append(PointF(pt));
        }
        cp.drawPolygon(cPoly, OddEvenFill);
    }

    return compareImages("non_aa_fills", qImg, cImg, true);
}

// 3. Brush ring cursor from openglwidget.cpp (updateBrushCursor)
DiffStats testBrushCursor()
{
    const int w = 160;
    const int h = 80;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(Qt::transparent);
    cImg.fill(0);

    const double diameters[2] = {8.0, 22.0};
    const double dprs[2] = {1.0, 1.25};

    for (int idx = 0; idx < 2; ++idx) {
        const double diameter = diameters[idx];
        const double dpr = dprs[idx];
        const double ringPen = 1.25;
        const int side = std::max(9, int(std::ceil(diameter + ringPen * 2.0 + 2.0)));
        const int pxSide = int(std::ceil(side * dpr));

        QImage qSub(pxSide, pxSide, QImage::Format_ARGB32_Premultiplied);
        qSub.setDevicePixelRatio(dpr);
        qSub.fill(Qt::transparent);
        {
            QPainter painter(&qSub);
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
        }

        Image cSub(pxSide, pxSide, Image::Format_ARGB32_Premultiplied);
        cSub.setDevicePixelRatio(dpr);
        cSub.fill(0);
        {
            Painter painter(&cSub);
            painter.setRenderHint(Painter::Antialiasing, true);
            const PointF centre(side * 0.5, side * 0.5);
            const double radius = diameter * 0.5;
            painter.setBrush(NoBrush);
            painter.setPen(Pen(Color(255, 255, 255, 200), ringPen * 2.0));
            painter.drawEllipse(centre, radius, radius);
            painter.setPen(Pen(Color(20, 20, 20, 230), ringPen));
            painter.drawEllipse(centre, radius, radius);
            if (diameter >= 12.0) {
                painter.setPen(Pen(Color(255, 255, 255, 200), 2.0));
                painter.drawPoint(centre);
                painter.setPen(Pen(Color(20, 20, 20, 230), 1.0));
                painter.drawPoint(centre);
            }
        }

        qSub.setDevicePixelRatio(1.0);
        cSub.setDevicePixelRatio(1.0);
        {
            QPainter qp(&qImg);
            qp.drawImage(QPointF(idx * 70 + 10, 10), qSub);
        }
        {
            Painter cp(&cImg);
            cp.drawImage(PointF(idx * 70 + 10, 10), cSub);
        }
    }

    return compareImages("brush_cursor", qImg, cImg);
}

// 4. Rotate cursor from openglwidget.cpp (buildRotateCursorPixmap)
DiffStats testRotateCursor()
{
    const int w = 140;
    const int h = 70;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(Qt::transparent);
    cImg.fill(0);

    const double dprs[2] = {1.0, 1.5};
    for (int idx = 0; idx < 2; ++idx) {
        const double dpr = dprs[idx];
        constexpr double kSide = 24.0;
        const int pxSide = int(std::ceil(kSide * dpr));

        const QPointF centre(kSide * 0.5, kSide * 0.5);
        const double radius = 6.5;
        const QRectF ring(centre.x() - radius, centre.y() - radius, radius * 2.0, radius * 2.0);
        QPainterPath arc;
        arc.arcMoveTo(ring, 55.0);
        arc.arcTo(ring, 55.0, 250.0);

        const double endAngle = (55.0 + 250.0) * (kPi / 180.0);
        const QPointF tip(centre.x() + radius * std::cos(endAngle),
                          centre.y() - radius * std::sin(endAngle));
        const QPointF tangent(-std::sin(endAngle), -std::cos(endAngle));
        const QPointF normal(-tangent.y(), tangent.x());
        QPolygonF head;
        head << tip + tangent * 4.2
             << tip + normal * 3.2 - tangent * 1.4
             << tip - normal * 3.2 - tangent * 1.4;

        QImage qSub(pxSide, pxSide, QImage::Format_ARGB32_Premultiplied);
        qSub.setDevicePixelRatio(dpr);
        qSub.fill(Qt::transparent);
        {
            QPainter painter(&qSub);
            painter.setRenderHint(QPainter::Antialiasing, true);
            for (int pass = 0; pass < 2; ++pass) {
                const QColor color = pass == 0 ? QColor(20, 20, 20, 235)
                                               : QColor(255, 255, 255, 245);
                const qreal width = pass == 0 ? 3.4 : 1.4;
                painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(arc);
                painter.setBrush(color);
                painter.drawPolygon(head);
            }
        }

        Image cSub(pxSide, pxSide, Image::Format_ARGB32_Premultiplied);
        cSub.setDevicePixelRatio(dpr);
        cSub.fill(0);
        {
            const Path coreArc(arc);
            Vector<PointF> coreHead;
            for (const QPointF &pt : head) {
                coreHead.append(PointF(pt));
            }
            Painter painter(&cSub);
            painter.setRenderHint(Painter::Antialiasing, true);
            for (int pass = 0; pass < 2; ++pass) {
                const Color color = pass == 0 ? Color(20, 20, 20, 235)
                                              : Color(255, 255, 255, 245);
                const double width = pass == 0 ? 3.4 : 1.4;
                painter.setPen(Pen(color, width, SolidLine, RoundCap, RoundJoin));
                painter.setBrush(NoBrush);
                painter.drawPath(coreArc);
                painter.setBrush(color);
                painter.drawPolygon(coreHead);
            }
        }

        qSub.setDevicePixelRatio(1.0);
        cSub.setDevicePixelRatio(1.0);
        {
            QPainter qp(&qImg);
            qp.drawImage(QPointF(idx * 60 + 10, 10), qSub);
        }
        {
            Painter cp(&cImg);
            cp.drawImage(PointF(idx * 60 + 10, 10), cSub);
        }
    }

    return compareImages("rotate_cursor", qImg, cImg);
}

// 5. Batch of cubic-curve strokes
DiffStats testCubicStrokes()
{
    const int w = 480;
    const int h = 320;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(250, 250, 248));
    cImg.fill(AnimeCore::qRgb(250, 250, 248));

    QPainter qp(&qImg);
    Painter cp(&cImg);
    qp.setRenderHint(QPainter::Antialiasing, true);
    cp.setRenderHint(Painter::Antialiasing, true);
    qp.setBrush(Qt::NoBrush);
    cp.setBrush(NoBrush);

    for (int i = 0; i < 35; ++i) {
        const double yBase = 20.0 + (i * 8.0);
        QPainterPath qPath(QPointF(20.0, yBase));
        for (int seg = 0; seg < 8; ++seg) {
            const double x0 = 20.0 + seg * 55.0;
            const double dy = (seg % 2 == 0 ? 1.0 : -1.0) * (12.0 + (i % 7) * 3.0);
            qPath.cubicTo(QPointF(x0 + 18.0, yBase + dy),
                          QPointF(x0 + 37.0, yBase - dy),
                          QPointF(x0 + 55.0, yBase + (seg % 3 - 1) * 4.0));
        }
        const QColor qCol((i * 47) % 220, (i * 83) % 200, (i * 131) % 230, 230);
        const double width = 1.0 + (i % 6) * 1.2;
        qp.setPen(QPen(qCol, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawPath(qPath);

        cp.setPen(Pen(Color(qCol), width, SolidLine, RoundCap, RoundJoin));
        cp.drawPath(Path(qPath));
    }

    return compareImages("cubic_strokes", qImg, cImg);
}

// 6. Dashed lines (all 5 styles)
DiffStats testDashedLines()
{
    const int w = 480;
    const int h = 320;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(255, 255, 255));
    cImg.fill(AnimeCore::qRgb(255, 255, 255));

    const Qt::PenStyle qStyles[5] = {
        Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine
    };
    const PenStyle cStyles[5] = {
        SolidLine, DashLine, DotLine, DashDotLine, DashDotDotLine
    };
    const Qt::PenCapStyle qCaps[3] = { Qt::FlatCap, Qt::SquareCap, Qt::RoundCap };
    const PenCapStyle cCaps[3] = { FlatCap, SquareCap, RoundCap };

    QPainter qp(&qImg);
    Painter cp(&cImg);
    qp.setRenderHint(QPainter::Antialiasing, true);
    cp.setRenderHint(Painter::Antialiasing, true);
    qp.setBrush(Qt::NoBrush);
    cp.setBrush(NoBrush);

    for (int s = 0; s < 5; ++s) {
        for (int c = 0; c < 3; ++c) {
            const int row = s * 3 + c;
            const double y = 16.0 + row * 19.0;
            const double width = 1.5 + c * 1.2;
            const QColor col(30 + s * 40, 40 + c * 70, 180 - s * 25, 240);

            qp.setPen(QPen(col, width, qStyles[s], qCaps[c], Qt::RoundJoin));
            cp.setPen(Pen(Color(col), width, cStyles[s], cCaps[c], RoundJoin));

            qp.drawLine(QPointF(20.0, y), QPointF(230.0, y + 4.0));
            cp.drawLine(PointF(20.0, y), PointF(230.0, y + 4.0));

            QPainterPath curve(QPointF(250.0, y));
            curve.cubicTo(QPointF(310.0, y - 12.0), QPointF(380.0, y + 12.0), QPointF(455.0, y));
            qp.drawPath(curve);
            cp.drawPath(Path(curve));
        }
    }

    return compareImages("dashed_lines", qImg, cImg);
}

// 7. Stroke widths 0.5 to 40 (plus cosmetic 0) and caps/joins
DiffStats testStrokeWidths()
{
    const int w = 560;
    const int h = 380;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(245, 245, 245));
    cImg.fill(AnimeCore::qRgb(245, 245, 245));

    const double widths[] = { 0.0, 0.5, 1.0, 2.5, 6.0, 14.0, 26.0, 40.0 };
    const Qt::PenCapStyle qCaps[] = { Qt::FlatCap, Qt::SquareCap, Qt::RoundCap };
    const PenCapStyle cCaps[] = { FlatCap, SquareCap, RoundCap };
    const Qt::PenJoinStyle qJoins[] = { Qt::MiterJoin, Qt::BevelJoin, Qt::RoundJoin };
    const PenJoinStyle cJoins[] = { MiterJoin, BevelJoin, RoundJoin };

    QPainter qp(&qImg);
    Painter cp(&cImg);
    qp.setRenderHint(QPainter::Antialiasing, true);
    cp.setRenderHint(Painter::Antialiasing, true);
    qp.setBrush(Qt::NoBrush);
    cp.setBrush(NoBrush);

    for (int i = 0; i < 8; ++i) {
        const double pw = widths[i];
        const int styleIdx = i % 3;
        const double x0 = 40.0 + (i % 4) * 130.0;
        const double y0 = (i < 4) ? 55.0 : 225.0;

        QPainterPath p(QPointF(x0, y0 + 80.0));
        p.lineTo(QPointF(x0 + 35.0, y0));
        p.cubicTo(QPointF(x0 + 55.0, y0 - 20.0), QPointF(x0 + 75.0, y0 + 100.0), QPointF(x0 + 95.0, y0 + 25.0));

        const QColor col(25 + i * 25, 160 - i * 15, 70 + i * 20, 235);
        qp.setPen(QPen(col, pw, Qt::SolidLine, qCaps[styleIdx], qJoins[styleIdx]));
        cp.setPen(Pen(Color(col), pw, SolidLine, cCaps[styleIdx], cJoins[styleIdx]));
        qp.drawPath(p);
        cp.drawPath(Path(p));
    }

    return compareImages("stroke_widths_0.5_to_40", qImg, cImg);
}

// 8. Scale 4x and 0.25x
DiffStats testScale4xAnd025x()
{
    const int w = 480;
    const int h = 320;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(255, 255, 255));
    cImg.fill(AnimeCore::qRgb(255, 255, 255));

    QPainterPath shape;
    shape.moveTo(10, 10);
    shape.cubicTo(30, 5, 45, 25, 35, 45);
    shape.lineTo(15, 40);
    shape.closeSubpath();

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);

        // Scale 4x
        qp.save();
        qp.translate(20.0, 20.0);
        qp.scale(4.0, 4.0);
        qp.setBrush(QColor(255, 190, 90, 210));
        qp.setPen(QPen(QColor(40, 40, 40, 240), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawPath(shape);
        qp.drawEllipse(QRectF(25.0, 10.0, 20.0, 15.0));
        // Cosmetic pen under 4x scale
        qp.setBrush(Qt::NoBrush);
        qp.setPen(QPen(QColor(200, 20, 60), 0.0));
        qp.drawRect(QRectF(8.0, 8.0, 40.0, 38.0));
        qp.restore();

        // Scale 0.25x
        qp.save();
        qp.translate(280.0, 30.0);
        qp.scale(0.25, 0.25);
        qp.setBrush(QColor(90, 180, 250, 230));
        qp.setPen(QPen(QColor(20, 60, 130, 255), 16.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        QPainterPath big;
        big.addRoundedRect(QRectF(20.0, 20.0, 560.0, 440.0), 80.0, 80.0);
        qp.drawPath(big);
        qp.setPen(QPen(QColor(220, 80, 30, 240), 24.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawLine(QPointF(40.0, 600.0), QPointF(620.0, 880.0));
        qp.restore();
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        const Path cShape(shape);

        // Scale 4x
        cp.save();
        cp.translate(20.0, 20.0);
        cp.scale(4.0, 4.0);
        cp.setBrush(Color(255, 190, 90, 210));
        cp.setPen(Pen(Color(40, 40, 40, 240), 1.5, SolidLine, RoundCap, RoundJoin));
        cp.drawPath(cShape);
        cp.drawEllipse(RectF(25.0, 10.0, 20.0, 15.0));
        cp.setBrush(NoBrush);
        cp.setPen(Pen(Color(200, 20, 60), 0.0));
        cp.drawRect(RectF(8.0, 8.0, 40.0, 38.0));
        cp.restore();

        // Scale 0.25x
        cp.save();
        cp.translate(280.0, 30.0);
        cp.scale(0.25, 0.25);
        cp.setBrush(Color(90, 180, 250, 230));
        cp.setPen(Pen(Color(20, 60, 130, 255), 16.0, SolidLine, SquareCap, MiterJoin));
        Path big;
        big.addRoundedRect(RectF(20.0, 20.0, 560.0, 440.0), 80.0, 80.0);
        cp.drawPath(big);
        cp.setPen(Pen(Color(220, 80, 30, 240), 24.0, DashLine, RoundCap, RoundJoin));
        cp.drawLine(PointF(40.0, 600.0), PointF(620.0, 880.0));
        cp.restore();
    }

    return compareImages("scale_4x_and_0.25x", qImg, cImg);
}

// 9. Rotate 30 degrees
DiffStats testRotate30Deg()
{
    const int w = 420;
    const int h = 320;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(250, 250, 250));
    cImg.fill(AnimeCore::qRgb(250, 250, 250));

    QTransform qxf;
    qxf.translate(160.0, 40.0);
    qxf.rotate(30.0);
    qxf.scale(1.2, 1.2);
    const Transform cxf(qxf);

    QPainterPath curve;
    curve.moveTo(10.0, 80.0);
    curve.cubicTo(60.0, 10.0, 140.0, 150.0, 190.0, 60.0);

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setWorldTransform(qxf);
        qp.setBrush(QColor(130, 210, 160, 220));
        qp.setPen(QPen(QColor(30, 80, 50, 250), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawRoundedRect(QRectF(10.0, 10.0, 140.0, 90.0), 16.0, 16.0);
        qp.setBrush(QColor(240, 130, 170, 200));
        qp.drawEllipse(QRectF(50.0, 40.0, 110.0, 70.0));
        qp.setBrush(Qt::NoBrush);
        qp.setPen(QPen(QColor(50, 40, 190, 240), 4.5, Qt::DashDotLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawPath(curve);
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.setWorldTransform(cxf);
        cp.setBrush(Color(130, 210, 160, 220));
        cp.setPen(Pen(Color(30, 80, 50, 250), 3.0, SolidLine, RoundCap, RoundJoin));
        cp.drawRoundedRect(RectF(10.0, 10.0, 140.0, 90.0), 16.0, 16.0);
        cp.setBrush(Color(240, 130, 170, 200));
        cp.drawEllipse(RectF(50.0, 40.0, 110.0, 70.0));
        cp.setBrush(NoBrush);
        cp.setPen(Pen(Color(50, 40, 190, 240), 4.5, DashDotLine, RoundCap, RoundJoin));
        cp.drawPath(Path(curve));
    }

    return compareImages("rotate_30deg", qImg, cImg);
}

// 10. OddEvenFill vs WindingFill, self-intersecting path, path with holes
DiffStats testFillRulesAndHoles()
{
    const int w = 540;
    const int h = 340;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(255, 255, 255));
    cImg.fill(AnimeCore::qRgb(255, 255, 255));

    // Self-intersecting 5-pointed star
    auto makeStar = [](double cx, double cy, double r, Qt::FillRule rule) {
        QPainterPath p;
        p.setFillRule(rule);
        for (int i = 0; i < 5; ++i) {
            const double ang = -0.5 * kPi + (i * 2 % 5) * (2.0 * kPi / 5.0);
            const QPointF pt(cx + std::cos(ang) * r, cy + std::sin(ang) * r);
            if (i == 0) {
                p.moveTo(pt);
            } else {
                p.lineTo(pt);
            }
        }
        p.closeSubpath();
        return p;
    };

    const QPainterPath starOdd = makeStar(95.0, 95.0, 75.0, Qt::OddEvenFill);
    const QPainterPath starWind = makeStar(265.0, 95.0, 75.0, Qt::WindingFill);

    // Self-intersecting figure-8 cubic curve
    QPainterPath fig8(QPointF(40.0, 250.0));
    fig8.setFillRule(Qt::OddEvenFill);
    fig8.cubicTo(QPointF(180.0, 150.0), QPointF(180.0, 350.0), QPointF(40.0, 250.0));
    fig8.cubicTo(QPointF(260.0, 170.0), QPointF(100.0, 340.0), QPointF(240.0, 240.0));
    fig8.closeSubpath();

    // Compound path with holes (outer rounded rect + inner ellipse hole + inner rect island)
    QPainterPath withHoles;
    withHoles.setFillRule(Qt::OddEvenFill);
    withHoles.addRoundedRect(QRectF(350.0, 30.0, 165.0, 280.0), 22.0, 22.0);
    withHoles.addEllipse(QRectF(375.0, 55.0, 115.0, 115.0));
    withHoles.addRect(QRectF(410.0, 90.0, 45.0, 45.0));
    withHoles.addEllipse(QRectF(380.0, 195.0, 105.0, 90.0));

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setPen(QPen(QColor(30, 30, 30), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        qp.setBrush(QColor(235, 85, 65, 230));
        qp.drawPath(starOdd);
        qp.setBrush(QColor(65, 155, 235, 230));
        qp.drawPath(starWind);
        qp.setBrush(QColor(150, 95, 220, 220));
        qp.drawPath(fig8);
        qp.setBrush(QColor(60, 185, 120, 235));
        qp.drawPath(withHoles);
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.setPen(Pen(Color(30, 30, 30), 2.0, SolidLine, RoundCap, RoundJoin));
        cp.setBrush(Color(235, 85, 65, 230));
        cp.drawPath(Path(starOdd));
        cp.setBrush(Color(65, 155, 235, 230));
        cp.drawPath(Path(starWind));
        cp.setBrush(Color(150, 95, 220, 220));
        cp.drawPath(Path(fig8));
        cp.setBrush(Color(60, 185, 120, 235));
        cp.drawPath(Path(withHoles));
    }

    return compareImages("oddeven_winding_holes", qImg, cImg);
}

// 11. Scaled & transformed images across all 5 source formats + nearest & bilinear
DiffStats testScaledImages()
{
    const int w = 520;
    const int h = 340;
    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(240, 242, 245));
    cImg.fill(AnimeCore::qRgb(240, 242, 245));

    // Build a procedural 48x48 base image in ARGB32
    QImage baseArgb(48, 48, QImage::Format_ARGB32);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            const int r = (x * 5) & 0xff;
            const int g = (y * 5) & 0xff;
            const int b = ((x + y) * 3) & 0xff;
            const int a = (std::hypot(x - 24.0, y - 24.0) <= 22.0) ? 230 : 60;
            baseArgb.setPixelColor(x, y, QColor(r, g, b, a));
        }
    }

    const QImage::Format qFmts[5] = {
        QImage::Format_ARGB32_Premultiplied,
        QImage::Format_ARGB32,
        QImage::Format_RGBA8888,
        QImage::Format_RGB32,
        QImage::Format_Grayscale8
    };
    const Image::Format cFmts[5] = {
        Image::Format_ARGB32_Premultiplied,
        Image::Format_ARGB32,
        Image::Format_RGBA8888,
        Image::Format_RGB32,
        Image::Format_Grayscale8
    };

    QPainter qp(&qImg);
    Painter cp(&cImg);
    RasterDevice cDev(&cImg);

    for (int f = 0; f < 5; ++f) {
        const QImage qSrc = baseArgb.convertToFormat(qFmts[f]);
        const Image cSrc(qSrc.constBits(), qSrc.width(), qSrc.height(), int(qSrc.bytesPerLine()), cFmts[f]);

        // Smooth bilinear upscale (x1.8)
        qp.save();
        qp.setRenderHint(QPainter::SmoothPixmapTransform, true);
        qp.translate(15.0 + f * 100.0, 15.0);
        qp.scale(1.8, 1.8);
        qp.drawImage(QPointF(0, 0), qSrc);
        qp.restore();

        cp.save();
        cp.setRenderHint(Painter::SmoothPixmapTransform, true);
        cp.translate(15.0 + f * 100.0, 15.0);
        cp.scale(1.8, 1.8);
        cp.drawImage(PointF(0, 0), cSrc);
        cp.restore();

        // Nearest-neighbor downscale & upscale + sub-rect via PaintDevice::drawImage
        qp.save();
        qp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        qp.drawImage(QRectF(15.0 + f * 100.0, 120.0, 72.0, 72.0), qSrc, QRectF(8.0, 8.0, 32.0, 32.0));
        qp.restore();

        {
            PaintState st;
            st.smoothImages = false;
            cDev.drawImage(st, RectF(15.0 + f * 100.0, 120.0, 72.0, 72.0), cSrc, RectF(8.0, 8.0, 32.0, 32.0));
        }

        // Rotated & scaled bilinear sub-rect
        QTransform rotXf;
        rotXf.translate(50.0 + f * 100.0, 265.0);
        rotXf.rotate(25.0);
        qp.save();
        qp.setRenderHint(QPainter::SmoothPixmapTransform, true);
        qp.setWorldTransform(rotXf);
        qp.drawImage(QRectF(-30.0, -30.0, 60.0, 60.0), qSrc, QRectF(4.0, 4.0, 40.0, 40.0));
        qp.restore();

        {
            PaintState st;
            st.smoothImages = true;
            st.transform = Transform(rotXf);
            cDev.drawImage(st, RectF(-30.0, -30.0, 60.0, 60.0), cSrc, RectF(4.0, 4.0, 40.0, 40.0));
        }
    }

    return compareImages("scaled_images_5fmts", qImg, cImg);
}

// 12. Translucent overlay + SourceIn onionGhost from openglwidget.cpp
DiffStats testOnionGhostAndTranslucent()
{
    const int w = 400;
    const int h = 280;

    auto renderGhostQ = [&](bool past) {
        QImage image(QSize(w, h), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(QPointF(30.0, 20.0));
        painter.scale(1.15, 1.15);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 120, 90, 200));
        painter.drawEllipse(QRectF(20.0, 20.0, 140.0, 110.0));

        QPainterPath stroke(QPointF(15.0, 40.0));
        stroke.cubicTo(QPointF(90.0, 10.0), QPointF(150.0, 160.0), QPointF(240.0, 70.0));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(10, 10, 10, 255), 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(stroke);

        painter.setWorldTransform(QTransform());
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        const QColor tint = past ? QColor(210, 60, 60, 128) : QColor(50, 170, 90, 128);
        painter.fillRect(QRect(0, 0, w, h), tint);
        painter.end();
        return image;
    };

    auto renderGhostC = [&](bool past) {
        Image image(w, h, Image::Format_ARGB32_Premultiplied);
        image.fill(0);
        Painter painter(&image);
        painter.setRenderHint(Painter::Antialiasing);
        painter.setRenderHint(Painter::SmoothPixmapTransform);
        painter.translate(PointF(30.0, 20.0));
        painter.scale(1.15, 1.15);

        painter.setPen(NoPen);
        painter.setBrush(Color(220, 120, 90, 200));
        painter.drawEllipse(RectF(20.0, 20.0, 140.0, 110.0));

        Path stroke(PointF(15.0, 40.0));
        stroke.cubicTo(PointF(90.0, 10.0), PointF(150.0, 160.0), PointF(240.0, 70.0));
        painter.setBrush(NoBrush);
        painter.setPen(Pen(Color(10, 10, 10, 255), 5.0, SolidLine, RoundCap, RoundJoin));
        painter.drawPath(stroke);

        painter.setWorldTransform(Transform());
        painter.setCompositionMode(CompositionMode_SourceIn);
        const Color tint = past ? Color(210, 60, 60, 128) : Color(50, 170, 90, 128);
        painter.fillRect(Rect(0, 0, w, h), tint);
        painter.end();
        return image;
    };

    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(255, 255, 255));
    cImg.fill(AnimeCore::qRgb(255, 255, 255));

    const QImage qPast = renderGhostQ(true);
    const QImage qAhead = renderGhostQ(false);
    const Image cPast = renderGhostC(true);
    const Image cAhead = renderGhostC(false);

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.drawImage(QPointF(0.0, 0.0), qPast);
        qp.setOpacity(0.75);
        qp.drawImage(QPointF(60.0, 40.0), qAhead);
        qp.setOpacity(0.45);
        qp.setBrush(QColor(80, 90, 230, 190));
        qp.setPen(QPen(QColor(20, 20, 80, 220), 4.0));
        qp.drawRoundedRect(QRectF(110.0, 80.0, 180.0, 120.0), 20.0, 20.0);
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.drawImage(PointF(0.0, 0.0), cPast);
        cp.setOpacity(0.75);
        cp.drawImage(PointF(60.0, 40.0), cAhead);
        cp.setOpacity(0.45);
        cp.setBrush(Color(80, 90, 230, 190));
        cp.setPen(Pen(Color(20, 20, 80, 220), 4.0));
        cp.drawRoundedRect(RectF(110.0, 80.0, 180.0, 120.0), 20.0, 20.0);
    }

    return compareImages("onion_ghost_translucent", qImg, cImg);
}

// 13. devicePixelRatio 1.25 image
DiffStats testDevicePixelRatio125()
{
    const double dpr = 1.25;
    const int logicalW = 320;
    const int logicalH = 240;
    const int pxW = int(std::lround(logicalW * dpr));
    const int pxH = int(std::lround(logicalH * dpr));

    QImage qImg(pxW, pxH, QImage::Format_ARGB32_Premultiplied);
    Image cImg(pxW, pxH, Image::Format_ARGB32_Premultiplied);
    qImg.setDevicePixelRatio(dpr);
    cImg.setDevicePixelRatio(dpr);
    qImg.fill(QColor(248, 248, 252));
    cImg.fill(AnimeCore::qRgb(248, 248, 252));

    // Also create a child stamp image with devicePixelRatio = 1.25
    QImage qStamp(100, 100, QImage::Format_ARGB32_Premultiplied);
    Image cStamp(100, 100, Image::Format_ARGB32_Premultiplied);
    qStamp.setDevicePixelRatio(dpr);
    cStamp.setDevicePixelRatio(dpr);
    qStamp.fill(Qt::transparent);
    cStamp.fill(0);
    {
        QPainter qp(&qStamp);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setBrush(QColor(250, 140, 60, 220));
        qp.setPen(QPen(QColor(80, 30, 10, 250), 2.5));
        qp.drawEllipse(QRectF(8.0, 8.0, 64.0, 64.0));
    }
    {
        Painter cp(&cStamp);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.setBrush(Color(250, 140, 60, 220));
        cp.setPen(Pen(Color(80, 30, 10, 250), 2.5));
        cp.drawEllipse(RectF(8.0, 8.0, 64.0, 64.0));
    }

    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setRenderHint(QPainter::SmoothPixmapTransform, true);
        qp.fillRect(QRectF(16.0, 16.0, 288.0, 208.0), QColor(230, 236, 245));
        qp.drawImage(QPointF(24.0, 24.0), qStamp);

        QPainterPath p(QPointF(120.0, 40.0));
        p.cubicTo(QPointF(180.0, 10.0), QPointF(220.0, 180.0), QPointF(290.0, 90.0));
        qp.setBrush(Qt::NoBrush);
        qp.setPen(QPen(QColor(30, 100, 210, 240), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        qp.drawPath(p);
    }
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.setRenderHint(Painter::SmoothPixmapTransform, true);
        cp.fillRect(RectF(16.0, 16.0, 288.0, 208.0), Color(230, 236, 245));
        cp.drawImage(PointF(24.0, 24.0), cStamp);

        Path p(PointF(120.0, 40.0));
        p.cubicTo(PointF(180.0, 10.0), PointF(220.0, 180.0), PointF(290.0, 90.0));
        cp.setBrush(NoBrush);
        cp.setPen(Pen(Color(30, 100, 210, 240), 4.0, SolidLine, RoundCap, RoundJoin));
        cp.drawPath(p);
    }

    return compareImages("dpr_1.25_image", qImg, cImg);
}

void runBenchmark()
{
    const int w = 1280;
    const int h = 720;
    const int strokeCount = 3000;
    const int segsPerStroke = 40;

    std::vector<QPainterPath> qPaths;
    std::vector<Path> cPaths;
    qPaths.reserve(strokeCount);
    cPaths.reserve(strokeCount);

    for (int i = 0; i < strokeCount; ++i) {
        double x = 40.0 + ((i * 97) % 1000);
        double y = 40.0 + ((i * 53) % 600);
        QPainterPath qp(QPointF(x, y));
        Path cp(PointF(x, y));

        for (int s = 0; s < segsPerStroke; ++s) {
            const double ang = 0.15 * s + 0.05 * (i % 13);
            const double dx = std::cos(ang) * 4.5;
            const double dy = std::sin(ang) * 4.5;
            const PointF c1(x + dx * 0.33 - dy * 0.2, y + dy * 0.33 + dx * 0.2);
            const PointF c2(x + dx * 0.66 + dy * 0.2, y + dy * 0.66 - dx * 0.2);
            const PointF endPt(x + dx, y + dy);
            qp.cubicTo(QPointF(c1.x(), c1.y()), QPointF(c2.x(), c2.y()), QPointF(endPt.x(), endPt.y()));
            cp.cubicTo(c1, c2, endPt);
            x = endPt.x();
            y = endPt.y();
        }
        qPaths.push_back(std::move(qp));
        cPaths.push_back(std::move(cp));
    }

    QImage qImg(w, h, QImage::Format_ARGB32_Premultiplied);
    qImg.fill(QColor(255, 255, 255));
    const QPen qPen(QColor(20, 20, 20, 255), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    QElapsedTimer timer;
    timer.start();
    {
        QPainter qp(&qImg);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setBrush(Qt::NoBrush);
        qp.setPen(qPen);
        for (int i = 0; i < strokeCount; ++i) {
            qp.drawPath(qPaths[static_cast<std::size_t>(i)]);
        }
    }
    const qint64 qtMs = timer.elapsed();

    Image cImg(w, h, Image::Format_ARGB32_Premultiplied);
    cImg.fill(AnimeCore::qRgb(255, 255, 255));
    const Pen cPen(Color(20, 20, 20, 255), 3.0, SolidLine, RoundCap, RoundJoin);

    timer.restart();
    {
        Painter cp(&cImg);
        cp.setRenderHint(Painter::Antialiasing, true);
        cp.setBrush(NoBrush);
        cp.setPen(cPen);
        for (int i = 0; i < strokeCount; ++i) {
            cp.drawPath(cPaths[static_cast<std::size_t>(i)]);
        }
    }
    const qint64 coreMs = timer.elapsed();

    const double ratio = (qtMs > 0) ? (static_cast<double>(coreMs) / static_cast<double>(qtMs)) : 0.0;
    std::printf("BENCHMARK 1280x720 3000x40-cubic strokes | QPainter=%lld ms | RasterDevice=%lld ms | ratio=%.2fx\n",
                static_cast<long long>(qtMs),
                static_cast<long long>(coreMs),
                ratio);
    std::fflush(stdout);
}

} // namespace

int main()
{
    DiffStats total;
    total.name = "TOTAL";

    const DiffStats s1 = testCheckerboard();
    const DiffStats s2 = testNonAAFills();
    accumulateStats(total, s1);
    accumulateStats(total, s2);
    accumulateStats(total, testBrushCursor());
    accumulateStats(total, testRotateCursor());
    accumulateStats(total, testCubicStrokes());
    accumulateStats(total, testDashedLines());
    accumulateStats(total, testStrokeWidths());
    accumulateStats(total, testScale4xAnd025x());
    accumulateStats(total, testRotate30Deg());
    accumulateStats(total, testFillRulesAndHoles());
    accumulateStats(total, testScaledImages());
    accumulateStats(total, testOnionGhostAndTranslucent());
    accumulateStats(total, testDevicePixelRatio125());

    std::printf("-----------------------------------------------------------------------------------------\n");
    std::printf("TOTAL %-19s | px=%7lld | le16=%6.2f%% | gt64=%6.4f%% | alphaDiff=%6.3f%% | maxDiff=%3d\n",
                total.name,
                static_cast<long long>(total.totalPixels),
                total.pctLe16(),
                total.pctGt64(),
                total.alphaRelDiffPct(),
                total.maxChannelDiff);
    std::fflush(stdout);

    if (s1.maxChannelDiff != 0 || s2.maxChannelDiff != 0) {
        std::fprintf(stderr, "FAIL: exact scene had non-zero diff (s1=%d, s2=%d)\n",
                     s1.maxChannelDiff, s2.maxChannelDiff);
        return 1;
    }
    if (total.pctLe16() < 99.0) {
        std::fprintf(stderr, "FAIL: total pixels with diff <= 16 is %.2f%% (< 99.0%%)\n", total.pctLe16());
        return 1;
    }
    if (total.pctGt64() > 0.2) {
        std::fprintf(stderr, "FAIL: total pixels with diff > 64 is %.4f%% (> 0.2%%)\n", total.pctGt64());
        return 1;
    }
    if (total.alphaRelDiffPct() > 1.0) {
        std::fprintf(stderr, "FAIL: total alpha sum diff is %.3f%% (> 1.0%%)\n", total.alphaRelDiffPct());
        return 1;
    }

    runBenchmark();
    return 0;
}
