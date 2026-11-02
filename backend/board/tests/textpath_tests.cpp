#include "backend/board/textpath.h"

#include <cmath>
#include <cstdio>

using namespace AnimeCore;

namespace {

bool checkCloseToPixelSize(const char *label, double actualHeight, double pixelSize)
{
    if (!(actualHeight >= pixelSize * 0.45 && actualHeight <= pixelSize * 1.6)) {
        std::fprintf(stderr,
                     "%s: expected bounding height close to pixelSize=%g, got %g\n",
                     label,
                     pixelSize,
                     actualHeight);
        return false;
    }
    return true;
}

bool testMetrics()
{
    const TextMetrics m12 = textMetrics(12.0);
    const TextMetrics m24 = textMetrics(24.0);
    if (m12.ascent <= 0.0 || m12.descent <= 0.0 || m12.height <= 0.0) {
        std::fprintf(stderr,
                     "Invalid metrics at 12px: ascent=%g descent=%g height=%g\n",
                     m12.ascent,
                     m12.descent,
                     m12.height);
        return false;
    }
    if (!checkCloseToPixelSize("metrics(12)", m12.height, 12.0)
        || !checkCloseToPixelSize("metrics(24)", m24.height, 24.0)) {
        return false;
    }
    if (!(m24.height > m12.height && m24.ascent > m12.ascent)) {
        std::fprintf(stderr, "Expected 24px metrics > 12px metrics\n");
        return false;
    }
    return true;
}

bool testEnglishLabels()
{
    constexpr double kPx = 24.0;
    const String s1("A");
    const String s2("Hello");
    const String s3("Hello World");

    const Path p1 = textPath(s1, kPx, PointF(0.0, kPx));
    const Path p2 = textPath(s2, kPx, PointF(0.0, kPx));
    const Path p3 = textPath(s3, kPx, PointF(0.0, kPx));

    if (p1.isEmpty() || p2.isEmpty() || p3.isEmpty()) {
        std::fprintf(stderr, "Expected non-empty English text paths\n");
        return false;
    }

    const double w1 = textWidth(s1, kPx);
    const double w2 = textWidth(s2, kPx);
    const double w3 = textWidth(s3, kPx);
    if (!(w1 > 0.0 && w2 > w1 && w3 > w2)) {
        std::fprintf(stderr,
                     "Expected English textWidth to grow with char count: %g, %g, %g\n",
                     w1,
                     w2,
                     w3);
        return false;
    }

    const RectF b1 = p1.boundingRect();
    const RectF b2 = p2.boundingRect();
    const RectF b3 = p3.boundingRect();
    if (!(b1.width() > 0.0 && b2.width() > b1.width() && b3.width() > b2.width())) {
        std::fprintf(stderr,
                     "Expected English path bounding width to grow: %g, %g, %g\n",
                     b1.width(),
                     b2.width(),
                     b3.width());
        return false;
    }

    if (!checkCloseToPixelSize("English(A)", b1.height(), kPx)
        || !checkCloseToPixelSize("English(Hello)", b2.height(), kPx)
        || !checkCloseToPixelSize("English(Hello World)", b3.height(), kPx)) {
        return false;
    }

    return true;
}

bool testChineseAndJapaneseLabels()
{
    constexpr double kPx = 24.0;
    const String zh1 = String::fromUtf8("\xE4\xB8\xAD");                         // 中
    const String zh2 = String::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87");             // 中文
    const String zh3 = String::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE7\xAD\xBE"); // 中文标签

    const Path p1 = textPath(zh1, kPx, PointF(0.0, kPx));
    const Path p2 = textPath(zh2, kPx, PointF(0.0, kPx));
    const Path p3 = textPath(zh3, kPx, PointF(0.0, kPx));

    if (p1.isEmpty() || p2.isEmpty() || p3.isEmpty()) {
        std::fprintf(stderr, "Expected non-empty Chinese text paths\n");
        return false;
    }

    const double w1 = textWidth(zh1, kPx);
    const double w2 = textWidth(zh2, kPx);
    const double w3 = textWidth(zh3, kPx);
    if (!(w1 > 0.0 && w2 > w1 && w3 > w2)) {
        std::fprintf(stderr,
                     "Expected Chinese textWidth to grow with char count: %g, %g, %g\n",
                     w1,
                     w2,
                     w3);
        return false;
    }

    const RectF b1 = p1.boundingRect();
    const RectF b2 = p2.boundingRect();
    const RectF b3 = p3.boundingRect();
    if (!(b1.width() > 0.0 && b2.width() > b1.width() && b3.width() > b2.width())) {
        std::fprintf(stderr,
                     "Expected Chinese path bounding width to grow: %g, %g, %g\n",
                     b1.width(),
                     b2.width(),
                     b3.width());
        return false;
    }

    if (!checkCloseToPixelSize("Chinese(1)", b1.height(), kPx)
        || !checkCloseToPixelSize("Chinese(2)", b2.height(), kPx)
        || !checkCloseToPixelSize("Chinese(4)", b3.height(), kPx)) {
        return false;
    }

    const String ja1 = String::fromUtf8("\xE3\x81\x82");                         // あ
    const String ja2 = String::fromUtf8("\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86"); // あいう
    const Path jp1 = textPath(ja1, kPx, PointF(0.0, kPx));
    const Path jp2 = textPath(ja2, kPx, PointF(0.0, kPx));
    if (jp1.isEmpty() || jp2.isEmpty() || !(textWidth(ja2, kPx) > textWidth(ja1, kPx))) {
        std::fprintf(stderr, "Expected non-empty growing Japanese text paths\n");
        return false;
    }
    if (!checkCloseToPixelSize("Japanese(3)", jp2.boundingRect().height(), kPx)) {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!testMetrics()) {
        return 1;
    }
    if (!testEnglishLabels()) {
        return 1;
    }
    if (!testChineseAndJapaneseLabels()) {
        return 1;
    }
    std::printf("TEXTPATH_TESTS_OK\n");
    return 0;
}
