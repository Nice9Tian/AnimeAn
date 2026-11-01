#include "algorithm/core/image.h"
#include "algorithm/core/qtbridge_image.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QRgb>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

QByteArray saveQImageToPng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

bool compareRgbaImages(const AnimeCore::Image &coreImg,
                       const QImage &qtImg,
                       int maxChannelDiff = 0)
{
    const QImage expected = (qtImg.format() == QImage::Format_RGBA8888)
                                ? qtImg
                                : qtImg.convertToFormat(QImage::Format_RGBA8888);
    if (coreImg.isNull() != expected.isNull()) {
        return false;
    }
    if (coreImg.isNull()) {
        return true;
    }
    if (coreImg.width() != expected.width()
        || coreImg.height() != expected.height()
        || coreImg.format() != AnimeCore::Image::Format_RGBA8888) {
        return false;
    }

    for (int y = 0; y < expected.height(); ++y) {
        const unsigned char *coreRow = coreImg.constScanLine(y);
        const uchar *qtRow = expected.constScanLine(y);
        if (!coreRow || !qtRow) {
            return false;
        }
        for (int x = 0; x < expected.width() * 4; ++x) {
            const int diff = std::abs(int(coreRow[x]) - int(qtRow[x]));
            if (diff > maxChannelDiff) {
                return false;
            }
        }
    }
    return true;
}

void testNullAndZeroSizedImages()
{
    AnimeCore::Image def;
    check(def.isNull(), "default constructed Image should be null");
    check(def.width() == 0 && def.height() == 0, "default Image dimensions should be 0");
    check(def.bytesPerLine() == 0, "default Image bytesPerLine should be 0");
    check(def.constBits() == nullptr, "default Image constBits should be nullptr");
    check(def.bits() == nullptr, "default Image bits should be nullptr");
    check(def.cacheKey() == 0, "default Image cacheKey should be 0");

    AnimeCore::Image zeroBoth(0, 0, AnimeCore::Image::Format_RGBA8888);
    AnimeCore::Image zeroW(0, 8, AnimeCore::Image::Format_RGBA8888);
    AnimeCore::Image zeroH(8, 0, AnimeCore::Image::Format_Grayscale8);
    AnimeCore::Image neg(-3, 4, AnimeCore::Image::Format_RGBA8888);
    check(zeroBoth.isNull(), "0x0 Image should be null");
    check(zeroW.isNull(), "0x8 Image should be null");
    check(zeroH.isNull(), "8x0 Image should be null");
    check(neg.isNull(), "negative-sized Image should be null");

    QImage qNull(0, 0, QImage::Format_RGBA8888);
    QByteArray emptyPng = saveQImageToPng(qNull);
    AnimeCore::Image decodedEmpty = AnimeCore::Image::decodePng(
        reinterpret_cast<const unsigned char *>(emptyPng.constData()),
        static_cast<std::size_t>(emptyPng.size()));
    check(decodedEmpty.isNull(), "decoding PNG from 0x0 QImage should return null Image");

    AnimeCore::Image decodedNull = AnimeCore::Image::decodePng(nullptr, 0);
    check(decodedNull.isNull(), "decodePng(nullptr, 0) should return null Image");

    const unsigned char badBytes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    AnimeCore::Image decodedBad = AnimeCore::Image::decodePng(badBytes, sizeof(badBytes));
    check(decodedBad.isNull(), "decodePng on invalid bytes should return null Image");

    std::vector<unsigned char> encodedNull = AnimeCore::encodePng(def);
    check(encodedNull.empty(), "encodePng on null Image should return empty vector");
}

void testBasicOperationsAndGrayscale()
{
    AnimeCore::Image oneByOne(1, 1, AnimeCore::Image::Format_RGBA8888);
    check(!oneByOne.isNull(), "1x1 RGBA8888 Image should not be null");
    check(oneByOne.width() == 1 && oneByOne.height() == 1, "1x1 dimensions");
    check(oneByOne.bytesPerLine() == 4, "1x1 RGBA8888 bytesPerLine should be 4");
    oneByOne.setPixel(0, 0, AnimeCore::qRgba(12, 34, 56, 78));
    check(oneByOne.pixel(0, 0) == qRgba(12, 34, 56, 78), "1x1 RGBA8888 pixel round-trip");
    check(oneByOne.gray(0, 0) == qGray(qRgba(12, 34, 56, 78)), "1x1 RGBA8888 gray matches qGray");

    // Verify grayscale mask operations matching vectorlogic.cpp (fillPathFromMask).
    const int w = 9;
    const int h = 7;
    AnimeCore::Image mask(w, h, AnimeCore::Image::Format_Grayscale8);
    QImage qmask(w, h, QImage::Format_Grayscale8);
    check(!mask.isNull(), "Grayscale8 Image should not be null");
    check(mask.bytesPerLine() == w, "Grayscale8 bytesPerLine should equal width");

    mask.fill(0);
    qmask.fill(0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned int v = static_cast<unsigned int>((x * 37 + y * 53) & 0xff);
            mask.setPixel(x, y, v);
            qmask.setPixel(x, y, qRgb(int(v), int(v), int(v)));
        }
    }

    bool grayMatches = true;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (mask.pixel(x, y) != qmask.pixel(x, y)
                || AnimeCore::qGray(mask.pixel(x, y)) != ::qGray(qmask.pixel(x, y))
                || mask.gray(x, y) != ::qGray(qmask.pixel(x, y))) {
                grayMatches = false;
            }
        }
    }
    check(grayMatches, "Grayscale8 fill/setPixel/pixel/qGray should match QImage");
}

void testImplicitSharingAndCacheKey()
{
    AnimeCore::Image orig(6, 5, AnimeCore::Image::Format_RGBA8888);
    orig.fill(AnimeCore::qRgba(10, 20, 30, 255));
    orig.setPixel(1, 2, AnimeCore::qRgba(100, 110, 120, 130));

    const unsigned char *origBits = orig.constBits();
    const std::int64_t origKey = orig.cacheKey();
    check(origBits != nullptr && origKey > 0, "orig should have valid constBits and cacheKey");

    AnimeCore::Image copy = orig;
    check(copy.constBits() == orig.constBits(),
          "copied Image should share constBits before mutation");
    check(copy.cacheKey() == orig.cacheKey(),
          "copied Image should share cacheKey before mutation");

    copy.setPixel(1, 2, AnimeCore::qRgba(200, 210, 220, 230));
    check(copy.constBits() != orig.constBits(),
          "setPixel on copy should detach pixel buffer");
    check(orig.constBits() == origBits,
          "setPixel on copy should not change original constBits pointer");
    check(orig.cacheKey() == origKey,
          "setPixel on copy should not change original cacheKey");
    check(copy.cacheKey() > origKey,
          "setPixel on copy should assign a new incremented cacheKey");
    check(orig.pixel(1, 2) == AnimeCore::qRgba(100, 110, 120, 130),
          "setPixel on copy should not affect original pixel value");
    check(copy.pixel(1, 2) == AnimeCore::qRgba(200, 210, 220, 230),
          "setPixel on copy should update copy pixel value");

    // Grayscale8 implicit sharing.
    AnimeCore::Image gOrig(4, 4, AnimeCore::Image::Format_Grayscale8);
    gOrig.fill(42);
    AnimeCore::Image gCopy = gOrig;
    check(gCopy.constBits() == gOrig.constBits(),
          "Grayscale8 copy should share constBits before mutation");
    gCopy.setPixel(2, 1, 255);
    check(gCopy.constBits() != gOrig.constBits(),
          "Grayscale8 setPixel on copy should detach");
    check(gOrig.gray(2, 1) == 42 && gCopy.gray(2, 1) == 255,
          "Grayscale8 setPixel on copy should preserve original and update copy");

    // Unshared mutation also advances cacheKey.
    const std::int64_t keyBeforeFill = gCopy.cacheKey();
    gCopy.fill(128);
    check(gCopy.cacheKey() > keyBeforeFill,
          "fill on unshared Image should increment cacheKey");
}

void testPngDecodeDifferential()
{
    const struct SizeCase {
        int w;
        int h;
    } sizes[] = {
        {1, 1},
        {1, 5},
        {7, 1},
        {3, 5},
        {16, 16},
        {29, 17},
    };

    // 1. 8-bit RGBA / ARGB32 / RGB32 / RGB888
    const QImage::Format rgbFormats[] = {
        QImage::Format_RGBA8888,
        QImage::Format_ARGB32,
        QImage::Format_RGB32,
        QImage::Format_RGB888,
    };

    for (QImage::Format fmt : rgbFormats) {
        for (const auto &sz : sizes) {
            QImage qimg(sz.w, sz.h, fmt);
            for (int y = 0; y < sz.h; ++y) {
                for (int x = 0; x < sz.w; ++x) {
                    const int r = (x * 41 + y * 19 + 7) & 0xff;
                    const int g = (x * 73 + y * 31 + 23) & 0xff;
                    const int b = (x * 13 + y * 97 + 101) & 0xff;
                    const int a = (fmt == QImage::Format_RGB32 || fmt == QImage::Format_RGB888)
                                      ? 255
                                      : ((x * 59 + y * 83) & 0xff);
                    qimg.setPixel(x, y, qRgba(r, g, b, a));
                }
            }
            const QByteArray pngBytes = saveQImageToPng(qimg);
            const QImage qtDecoded = QImage::fromData(pngBytes, "PNG")
                                         .convertToFormat(QImage::Format_RGBA8888);
            const AnimeCore::Image coreDecoded = AnimeCore::Image::decodePng(
                reinterpret_cast<const unsigned char *>(pngBytes.constData()),
                static_cast<std::size_t>(pngBytes.size()));

            check(compareRgbaImages(coreDecoded, qtDecoded, 0),
                  "decodePng 8-bit RGB/RGBA should match QImage::loadFromData pixel-for-pixel");
        }
    }

    // 2. 8-bit Grayscale
    for (const auto &sz : sizes) {
        QImage qimg(sz.w, sz.h, QImage::Format_Grayscale8);
        for (int y = 0; y < sz.h; ++y) {
            uchar *row = qimg.scanLine(y);
            for (int x = 0; x < sz.w; ++x) {
                row[x] = static_cast<uchar>((x * 29 + y * 67 + 15) & 0xff);
            }
        }
        const QByteArray pngBytes = saveQImageToPng(qimg);
        const QImage qtDecoded = QImage::fromData(pngBytes, "PNG")
                                     .convertToFormat(QImage::Format_RGBA8888);
        const AnimeCore::Image coreDecoded = AnimeCore::Image::decodePng(
            reinterpret_cast<const unsigned char *>(pngBytes.constData()),
            static_cast<std::size_t>(pngBytes.size()));

        check(compareRgbaImages(coreDecoded, qtDecoded, 0),
              "decodePng 8-bit Grayscale8 should match QImage::loadFromData pixel-for-pixel");
    }

    // 3. 8-bit Indexed / Palette (both opaque and with alpha / tRNS)
    for (bool withAlpha : {false, true}) {
        for (const auto &sz : sizes) {
            QImage qimg(sz.w, sz.h, QImage::Format_Indexed8);
            QList<QRgb> table;
            table.reserve(32);
            for (int i = 0; i < 32; ++i) {
                const int r = (i * 53 + 11) & 0xff;
                const int g = (i * 97 + 47) & 0xff;
                const int b = (i * 149 + 83) & 0xff;
                const int a = withAlpha ? ((i * 37) & 0xff) : 255;
                table.push_back(qRgba(r, g, b, a));
            }
            qimg.setColorTable(table);
            for (int y = 0; y < sz.h; ++y) {
                for (int x = 0; x < sz.w; ++x) {
                    qimg.setPixel(x, y, static_cast<uint>((x * 7 + y * 13) % table.size()));
                }
            }
            const QByteArray pngBytes = saveQImageToPng(qimg);
            const QImage qtDecoded = QImage::fromData(pngBytes, "PNG")
                                         .convertToFormat(QImage::Format_RGBA8888);
            const AnimeCore::Image coreDecoded = AnimeCore::Image::decodePng(
                reinterpret_cast<const unsigned char *>(pngBytes.constData()),
                static_cast<std::size_t>(pngBytes.size()));

            check(compareRgbaImages(coreDecoded, qtDecoded, 0),
                  "decodePng Indexed8 palette PNG should match QImage::loadFromData pixel-for-pixel");
        }
    }

    // 4. 16-bit RGBA64, RGBX64, and Grayscale16 (tolerance <= 1)
    for (const auto &sz : sizes) {
        QImage qRgba64(sz.w, sz.h, QImage::Format_RGBA64);
        for (int y = 0; y < sz.h; ++y) {
            auto *row = reinterpret_cast<std::uint16_t *>(qRgba64.scanLine(y));
            for (int x = 0; x < sz.w; ++x) {
                row[x * 4 + 0] = static_cast<std::uint16_t>((x * 6553 + y * 12345 + 100) & 0xffff);
                row[x * 4 + 1] = static_cast<std::uint16_t>((x * 17321 + y * 4321 + 900) & 0xffff);
                row[x * 4 + 2] = static_cast<std::uint16_t>((x * 9999 + y * 22222 + 333) & 0xffff);
                row[x * 4 + 3] = static_cast<std::uint16_t>((x * 31337 + y * 7777 + 65535) & 0xffff);
            }
        }
        const QByteArray pngRgba64 = saveQImageToPng(qRgba64);
        const QImage qtRgba64 = QImage::fromData(pngRgba64, "PNG")
                                    .convertToFormat(QImage::Format_RGBA8888);
        const AnimeCore::Image coreRgba64 = AnimeCore::Image::decodePng(
            reinterpret_cast<const unsigned char *>(pngRgba64.constData()),
            static_cast<std::size_t>(pngRgba64.size()));
        check(compareRgbaImages(coreRgba64, qtRgba64, 1),
              "decodePng 16-bit RGBA64 should match QImage within tolerance 1");

        QImage qGray16(sz.w, sz.h, QImage::Format_Grayscale16);
        for (int y = 0; y < sz.h; ++y) {
            auto *row = reinterpret_cast<std::uint16_t *>(qGray16.scanLine(y));
            for (int x = 0; x < sz.w; ++x) {
                row[x] = static_cast<std::uint16_t>((x * 13579 + y * 24680 + 513) & 0xffff);
            }
        }
        const QByteArray pngGray16 = saveQImageToPng(qGray16);
        const QImage qtGray16 = QImage::fromData(pngGray16, "PNG")
                                    .convertToFormat(QImage::Format_RGBA8888);
        const AnimeCore::Image coreGray16 = AnimeCore::Image::decodePng(
            reinterpret_cast<const unsigned char *>(pngGray16.constData()),
            static_cast<std::size_t>(pngGray16.size()));
        check(compareRgbaImages(coreGray16, qtGray16, 1),
              "decodePng 16-bit Grayscale16 should match QImage within tolerance 1");
    }
}

void testPngEncodeDifferential()
{
    const struct SizeCase {
        int w;
        int h;
    } sizes[] = {
        {1, 1},
        {2, 3},
        {11, 7},
        {24, 20},
    };

    for (const auto &sz : sizes) {
        AnimeCore::Image rgbaImg(sz.w, sz.h, AnimeCore::Image::Format_RGBA8888);
        QImage expectedRgba(sz.w, sz.h, QImage::Format_RGBA8888);
        for (int y = 0; y < sz.h; ++y) {
            for (int x = 0; x < sz.w; ++x) {
                const int r = (x * 61 + y * 17 + 5) & 0xff;
                const int g = (x * 23 + y * 89 + 40) & 0xff;
                const int b = (x * 107 + y * 3 + 200) & 0xff;
                const int a = (x * 47 + y * 71 + 128) & 0xff;
                rgbaImg.setPixel(x, y, AnimeCore::qRgba(r, g, b, a));
                expectedRgba.setPixel(x, y, qRgba(r, g, b, a));
            }
        }

        const std::vector<unsigned char> pngBytes = AnimeCore::encodePng(rgbaImg);
        check(!pngBytes.empty(), "encodePng on valid RGBA8888 Image should not be empty");

        QImage qtReadBack = QImage::fromData(
            reinterpret_cast<const uchar *>(pngBytes.data()),
            static_cast<int>(pngBytes.size()),
            "PNG");
        check(!qtReadBack.isNull(), "QImage should load PNG bytes produced by encodePng(RGBA8888)");
        check(compareRgbaImages(rgbaImg, qtReadBack, 0),
              "encodePng(RGBA8888) read back by QImage should match pixel-for-pixel");

        AnimeCore::Image coreReadBack = AnimeCore::Image::decodePng(pngBytes);
        check(compareRgbaImages(coreReadBack, expectedRgba, 0),
              "encodePng(RGBA8888) read back by decodePng should match pixel-for-pixel");

        // Grayscale8 encodePng test.
        AnimeCore::Image grayImg(sz.w, sz.h, AnimeCore::Image::Format_Grayscale8);
        QImage expectedGray(sz.w, sz.h, QImage::Format_Grayscale8);
        for (int y = 0; y < sz.h; ++y) {
            for (int x = 0; x < sz.w; ++x) {
                const unsigned int g = static_cast<unsigned int>((x * 37 + y * 91 + 12) & 0xff);
                grayImg.setPixel(x, y, g);
                expectedGray.setPixel(x, y, qRgb(int(g), int(g), int(g)));
            }
        }

        const std::vector<unsigned char> grayPngBytes = AnimeCore::encodePng(grayImg);
        check(!grayPngBytes.empty(), "encodePng on valid Grayscale8 Image should not be empty");

        QImage qtGrayReadBack = QImage::fromData(
            reinterpret_cast<const uchar *>(grayPngBytes.data()),
            static_cast<int>(grayPngBytes.size()),
            "PNG");
        check(!qtGrayReadBack.isNull(), "QImage should load PNG bytes produced by encodePng(Grayscale8)");
        AnimeCore::Image coreGrayReadBack = AnimeCore::Image::decodePng(grayPngBytes);
        check(compareRgbaImages(coreGrayReadBack, qtGrayReadBack, 0)
                  && compareRgbaImages(coreGrayReadBack, expectedGray, 0),
              "encodePng(Grayscale8) read back by QImage and decodePng should match pixel-for-pixel");
    }
}

void testBase64Differential()
{
    check(AnimeCore::toBase64(nullptr, 0).empty(), "toBase64(nullptr, 0) should be empty");
    check(AnimeCore::fromBase64(std::string_view{}).empty(), "fromBase64(\"\") should be empty");

    std::mt19937 rng(0xC0DEC0DEu);
    const std::size_t lengths[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 511, 1024, 2053
    };

    for (std::size_t len : lengths) {
        std::vector<unsigned char> raw(len);
        for (std::size_t i = 0; i < len; ++i) {
            raw[i] = static_cast<unsigned char>(rng() & 0xffu);
        }

        const QByteArray qRaw(reinterpret_cast<const char *>(raw.data()),
                              static_cast<qsizetype>(raw.size()));
        const QByteArray qB64 = qRaw.toBase64();
        const std::string coreB64 = AnimeCore::toBase64(raw);

        check(coreB64.size() == static_cast<std::size_t>(qB64.size())
                  && std::memcmp(coreB64.data(), qB64.constData(), coreB64.size()) == 0,
              "toBase64 should match QByteArray::toBase64");

        const std::vector<unsigned char> coreDecoded = AnimeCore::fromBase64(coreB64);
        const QByteArray qDecoded = QByteArray::fromBase64(qB64);
        check(coreDecoded == raw, "fromBase64 should round-trip original bytes");
        check(coreDecoded.size() == static_cast<std::size_t>(qDecoded.size())
                  && (coreDecoded.empty()
                      || std::memcmp(coreDecoded.data(), qDecoded.constData(), coreDecoded.size()) == 0),
              "fromBase64 should match QByteArray::fromBase64");

        // Also verify decoding with embedded whitespace/newlines like QByteArray::fromBase64.
        std::string wrappedB64;
        for (std::size_t i = 0; i < coreB64.size(); ++i) {
            if (i > 0 && (i % 16) == 0) {
                wrappedB64 += "\r\n ";
            }
            wrappedB64.push_back(coreB64[i]);
        }
        const std::vector<unsigned char> coreWrappedDecoded = AnimeCore::fromBase64(wrappedB64);
        const QByteArray qWrappedDecoded = QByteArray::fromBase64(
            QByteArray(wrappedB64.data(), static_cast<qsizetype>(wrappedB64.size())));
        check(coreWrappedDecoded.size() == static_cast<std::size_t>(qWrappedDecoded.size())
                  && (coreWrappedDecoded.empty()
                      || std::memcmp(coreWrappedDecoded.data(),
                                     qWrappedDecoded.constData(),
                                     coreWrappedDecoded.size()) == 0),
              "fromBase64 with whitespace should match QByteArray::fromBase64");
    }
}

void testQtBridge()
{
    // Null conversions
    QImage nullQ;
    AnimeCore::Image coreFromNull = nullQ;
    check(coreFromNull.isNull(), "QImage() -> AnimeCore::Image should be null");
    QImage qFromNull = coreFromNull;
    check(qFromNull.isNull(), "AnimeCore::Image() -> QImage should be null");

    // Non-Grayscale8 QImage -> AnimeCore::Image becomes Format_RGBA8888
    QImage qArgb(9, 6, QImage::Format_ARGB32);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 9; ++x) {
            qArgb.setPixel(x, y, qRgba((x * 25) & 0xff, (y * 40) & 0xff, ((x + y) * 19) & 0xff, 200));
        }
    }
    AnimeCore::Image coreFromArgb = qArgb;
    check(coreFromArgb.format() == AnimeCore::Image::Format_RGBA8888,
          "QImage(Format_ARGB32) converted to AnimeCore::Image should have Format_RGBA8888");
    check(compareRgbaImages(coreFromArgb, qArgb, 0),
          "QImage(Format_ARGB32) -> AnimeCore::Image pixels should match");

    // Grayscale8 QImage -> AnimeCore::Image stays Format_Grayscale8
    QImage qGrayImg(7, 5, QImage::Format_Grayscale8);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 7; ++x) {
            const int g = (x * 31 + y * 17) & 0xff;
            qGrayImg.setPixel(x, y, qRgb(g, g, g));
        }
    }
    AnimeCore::Image coreFromGray = qGrayImg;
    check(coreFromGray.format() == AnimeCore::Image::Format_Grayscale8,
          "QImage(Format_Grayscale8) converted to AnimeCore::Image should keep Format_Grayscale8");
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 7; ++x) {
            check(coreFromGray.gray(x, y) == ::qGray(qGrayImg.pixel(x, y)),
                  "QImage(Format_Grayscale8) -> AnimeCore::Image gray values should match");
        }
    }

    // AnimeCore::Image -> QImage zero-copy and shared lifetime
    QImage survivingQImage;
    const unsigned char *sharedPtrAddress = nullptr;
    {
        AnimeCore::Image coreImg(8, 6, AnimeCore::Image::Format_RGBA8888);
        coreImg.fill(AnimeCore::qRgba(11, 22, 33, 44));
        coreImg.setPixel(3, 2, AnimeCore::qRgba(99, 88, 77, 66));
        sharedPtrAddress = coreImg.constBits();

        QImage borrowedQ = coreImg;
        check(borrowedQ.constBits() == sharedPtrAddress,
              "AnimeCore::Image -> QImage should share pixel buffer without copying");
        check(borrowedQ.pixel(3, 2) == qRgba(99, 88, 77, 66),
              "AnimeCore::Image -> QImage should preserve pixels");

        // Mutating coreImg while borrowedQ holds a shared reference must detach coreImg.
        coreImg.setPixel(3, 2, AnimeCore::qRgba(1, 2, 3, 4));
        check(coreImg.constBits() != sharedPtrAddress,
              "mutating AnimeCore::Image while QImage holds buffer must detach");
        check(borrowedQ.constBits() == sharedPtrAddress,
              "borrowed QImage constBits must remain unchanged after coreImg detaches");
        check(borrowedQ.pixel(3, 2) == qRgba(99, 88, 77, 66),
              "borrowed QImage pixel must remain unchanged after coreImg detaches");

        survivingQImage = borrowedQ;
    }
    check(!survivingQImage.isNull()
              && survivingQImage.constBits() == sharedPtrAddress
              && survivingQImage.pixel(3, 2) == qRgba(99, 88, 77, 66),
          "QImage cleanup handler must keep shared pixel buffer alive after AnimeCore::Image is destroyed");
}

void testScaledDifferential()
{
    AnimeCore::Image nullImg;
    check(nullImg.scaled(10, 10).isNull(), "scaled on null Image should return null");

    AnimeCore::Image validImg(4, 4, AnimeCore::Image::Format_RGBA8888);
    check(validImg.scaled(0, 4).isNull() && validImg.scaled(4, -1).isNull(),
          "scaled with non-positive target dimensions should return null");

    enum Pattern {
        Pattern_Noise = 0,
        Pattern_Gradient,
        Pattern_TransparentChecker,
        Pattern_ThinLines,
    };

    const struct ScaleCase {
        const char *name;
        int srcW;
        int srcH;
        int dstW;
        int dstH;
    } cases[] = {
        {"upscale_1", 11, 9, 29, 25},
        {"upscale_2", 8, 12, 33, 41},
        {"downscale_1", 37, 29, 13, 10},
        {"downscale_2", 48, 36, 15, 11},
        {"mixed_x_up_y_down", 13, 42, 38, 11},
        {"mixed_x_down_y_up", 45, 11, 12, 35},
        {"to_1x1", 31, 23, 1, 1},
        {"to_1xN", 27, 19, 1, 9},
        {"to_Nx1", 27, 19, 9, 1},
        {"shrink_gt_8x_1", 96, 80, 9, 7},
        {"shrink_gt_8x_2", 108, 90, 11, 8},
    };

    std::mt19937 rng(0x5CA1E123u);
    int overallMaxDiff = 0;
    std::uint64_t overallDiffSum = 0;
    std::uint64_t overallChannelCount = 0;

    for (int pat = Pattern_Noise; pat <= Pattern_ThinLines; ++pat) {
        for (const auto &sc : cases) {
            AnimeCore::Image coreSrc(sc.srcW, sc.srcH, AnimeCore::Image::Format_RGBA8888);
            QImage qtSrc(sc.srcW, sc.srcH, QImage::Format_RGBA8888);

            const int r0 = int(rng() & 0xffu);
            const int g0 = int(rng() & 0xffu);
            const int b0 = int(rng() & 0xffu);
            const int r1 = int(rng() & 0xffu);
            const int g1 = int(rng() & 0xffu);
            const int b1 = int(rng() & 0xffu);
            const int cellW = std::max(1, int(rng() % 5) + 2);
            const int cellH = std::max(1, int(rng() % 5) + 2);
            const int lineX = int(rng() % static_cast<unsigned int>(sc.srcW));
            const int lineY = int(rng() % static_cast<unsigned int>(sc.srcH));

            for (int y = 0; y < sc.srcH; ++y) {
                for (int x = 0; x < sc.srcW; ++x) {
                    int r = 0;
                    int g = 0;
                    int b = 0;
                    int a = 255;

                    switch (static_cast<Pattern>(pat)) {
                    case Pattern_Noise: {
                        r = int(rng() & 0xffu);
                        g = int(rng() & 0xffu);
                        b = int(rng() & 0xffu);
                        const unsigned int mode = rng() % 6u;
                        if (mode == 0u) {
                            a = 0;
                        } else if (mode == 1u) {
                            a = 255;
                        } else {
                            a = int(rng() & 0xffu);
                        }
                        break;
                    }
                    case Pattern_Gradient: {
                        const double tx = (sc.srcW > 1) ? double(x) / double(sc.srcW - 1) : 0.0;
                        const double ty = (sc.srcH > 1) ? double(y) / double(sc.srcH - 1) : 0.0;
                        const double t = 0.5 * (tx + ty);
                        r = std::clamp(int(std::lround(r0 + (r1 - r0) * tx)), 0, 255);
                        g = std::clamp(int(std::lround(g0 + (g1 - g0) * ty)), 0, 255);
                        b = std::clamp(int(std::lround(b0 + (b1 - b0) * t)), 0, 255);
                        a = std::clamp(int(std::lround(255.0 * tx)), 0, 255);
                        break;
                    }
                    case Pattern_TransparentChecker: {
                        const int cx = x / cellW;
                        const int cy = y / cellH;
                        if (((cx + cy) & 1) == 0) {
                            // Deliberately store bright non-zero RGB under alpha 0 to verify
                            // transparent pixels do not bleed into neighbors.
                            r = 250;
                            g = 10;
                            b = 240;
                            a = 0;
                        } else if (((cx * 3 + cy) & 3) == 1) {
                            r = r0;
                            g = g0;
                            b = b0;
                            a = 140;
                        } else {
                            r = r1;
                            g = g1;
                            b = b1;
                            a = 255;
                        }
                        break;
                    }
                    case Pattern_ThinLines: {
                        const bool onLine = (x == lineX)
                                            || (y == lineY)
                                            || ((x * sc.srcH) / std::max(1, sc.srcW) == y);
                        if (onLine) {
                            r = r1;
                            g = g1;
                            b = b1;
                            a = 255;
                        } else {
                            r = r0;
                            g = g0;
                            b = b0;
                            a = 0;
                        }
                        break;
                    }
                    }

                    coreSrc.setPixel(x, y, AnimeCore::qRgba(r, g, b, a));
                    qtSrc.setPixel(x, y, qRgba(r, g, b, a));
                }
            }

            const AnimeCore::Image coreScaled = coreSrc.scaled(sc.dstW, sc.dstH);
            const QImage qtScaled = qtSrc.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                                        .scaled(sc.dstW,
                                                sc.dstH,
                                                Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);

            check(coreScaled.width() == sc.dstW && coreScaled.height() == sc.dstH,
                  "scaled dimensions should match target");

            int caseMaxDiff = 0;
            std::uint64_t caseDiffSum = 0;
            std::uint64_t caseChannels = 0;

            for (int y = 0; y < sc.dstH; ++y) {
                const unsigned char *cRow = coreScaled.constScanLine(y);
                const uchar *qRow = qtScaled.constScanLine(y);
                for (int x = 0; x < sc.dstW; ++x) {
                    const unsigned char *cp = cRow + x * 4;
                    const uchar *qp = qRow + x * 4;

                    const int da = std::abs(int(cp[3]) - int(qp[3]));
                    caseMaxDiff = std::max(caseMaxDiff, da);
                    caseDiffSum += static_cast<std::uint64_t>(da);
                    ++caseChannels;

                    if (cp[3] > 0 && qp[3] > 0) {
                        for (int c = 0; c < 3; ++c) {
                            const int dc = std::abs(int(cp[c]) - int(qp[c]));
                            caseMaxDiff = std::max(caseMaxDiff, dc);
                            caseDiffSum += static_cast<std::uint64_t>(dc);
                            ++caseChannels;
                        }
                    }
                }
            }

            overallMaxDiff = std::max(overallMaxDiff, caseMaxDiff);
            overallDiffSum += caseDiffSum;
            overallChannelCount += caseChannels;

            const double caseMeanDiff =
                (caseChannels > 0) ? (double(caseDiffSum) / double(caseChannels)) : 0.0;
            if (caseMaxDiff > 3 || caseMeanDiff > 1.0) {
                std::fprintf(stderr,
                             "RGBA scaled mismatch pat=%d case=%s (%dx%d->%dx%d): maxDiff=%d meanDiff=%.4f\n",
                             pat,
                             sc.name,
                             sc.srcW,
                             sc.srcH,
                             sc.dstW,
                             sc.dstH,
                             caseMaxDiff,
                             caseMeanDiff);
            }
            check(caseMaxDiff <= 3, "RGBA8888 scaled max channel diff should be <= 3");
            check(caseMeanDiff <= 1.0, "RGBA8888 scaled mean channel diff should be <= 1");

            // Grayscale8 single-channel differential check.
            AnimeCore::Image grayCoreSrc(sc.srcW, sc.srcH, AnimeCore::Image::Format_Grayscale8);
            QImage grayQtSrc(sc.srcW, sc.srcH, QImage::Format_Grayscale8);
            for (int y = 0; y < sc.srcH; ++y) {
                uchar *qRow = grayQtSrc.scanLine(y);
                for (int x = 0; x < sc.srcW; ++x) {
                    const unsigned char v = static_cast<unsigned char>(
                        AnimeCore::qGray(coreSrc.pixel(x, y)));
                    grayCoreSrc.setPixel(x, y, v);
                    qRow[x] = v;
                }
            }

            const AnimeCore::Image grayCoreScaled = grayCoreSrc.scaled(sc.dstW, sc.dstH);
            const QImage grayQtScaled = grayQtSrc
                                            .scaled(sc.dstW,
                                                    sc.dstH,
                                                    Qt::IgnoreAspectRatio,
                                                    Qt::SmoothTransformation)
                                            .convertToFormat(QImage::Format_Grayscale8);

            int grayMaxDiff = 0;
            std::uint64_t grayDiffSum = 0;
            for (int y = 0; y < sc.dstH; ++y) {
                const unsigned char *cRow = grayCoreScaled.constScanLine(y);
                const uchar *qRow = grayQtScaled.constScanLine(y);
                for (int x = 0; x < sc.dstW; ++x) {
                    const int d = std::abs(int(cRow[x]) - int(qRow[x]));
                    grayMaxDiff = std::max(grayMaxDiff, d);
                    grayDiffSum += static_cast<std::uint64_t>(d);
                }
            }
            const std::uint64_t grayPixels =
                static_cast<std::uint64_t>(sc.dstW) * static_cast<std::uint64_t>(sc.dstH);
            const double grayMeanDiff = double(grayDiffSum) / double(grayPixels);
            overallMaxDiff = std::max(overallMaxDiff, grayMaxDiff);
            overallDiffSum += grayDiffSum;
            overallChannelCount += grayPixels;

            if (grayMaxDiff > 3 || grayMeanDiff > 1.0) {
                std::fprintf(stderr,
                             "Grayscale8 scaled mismatch pat=%d case=%s (%dx%d->%dx%d): maxDiff=%d meanDiff=%.4f\n",
                             pat,
                             sc.name,
                             sc.srcW,
                             sc.srcH,
                             sc.dstW,
                             sc.dstH,
                             grayMaxDiff,
                             grayMeanDiff);
            }
            check(grayMaxDiff <= 3, "Grayscale8 scaled max diff should be <= 3");
            check(grayMeanDiff <= 1.0, "Grayscale8 scaled mean diff should be <= 1");
        }
    }

    const double overallMeanDiff =
        (overallChannelCount > 0) ? (double(overallDiffSum) / double(overallChannelCount)) : 0.0;
    std::printf("scaled_diff_stats: maxDiff=%d meanDiff=%.6f\n", overallMaxDiff, overallMeanDiff);
}

} // namespace

int main()
{
    testNullAndZeroSizedImages();
    testBasicOperationsAndGrayscale();
    testImplicitSharingAndCacheKey();
    testPngDecodeDifferential();
    testPngEncodeDifferential();
    testBase64Differential();
    testQtBridge();
    testScaledDifferential();
    return failures == 0 ? 0 : 1;
}
