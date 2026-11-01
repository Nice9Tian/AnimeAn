#ifndef ANIMECORE_QTBRIDGE_IMAGE_H
#define ANIMECORE_QTBRIDGE_IMAGE_H

#include "algorithm/core/image.h"

#include <QImage>

#include <cstring>
#include <memory>

namespace AnimeCore {

template <>
struct Bridge<QImage> {
    static Image toCore(const QImage &qimg)
    {
        if (qimg.isNull() || qimg.width() <= 0 || qimg.height() <= 0) {
            return Image();
        }
        if (qimg.format() == QImage::Format_Grayscale8) {
            return Image(qimg.constBits(),
                         qimg.width(),
                         qimg.height(),
                         int(qimg.bytesPerLine()),
                         Image::Format_Grayscale8);
        }

        const QImage rgba = (qimg.format() == QImage::Format_RGBA8888)
                                ? qimg
                                : qimg.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) {
            return Image();
        }
        return Image(rgba.constBits(),
                     rgba.width(),
                     rgba.height(),
                     int(rgba.bytesPerLine()),
                     Image::Format_RGBA8888);
    }

    static QImage fromCore(const Image &image)
    {
        if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
            return QImage();
        }

        QImage::Format qfmt = QImage::Format_Invalid;
        switch (image.format()) {
        case Image::Format_RGBA8888:
            qfmt = QImage::Format_RGBA8888;
            break;
        case Image::Format_Grayscale8:
            qfmt = QImage::Format_Grayscale8;
            break;
        case Image::Format_ARGB32:
            qfmt = QImage::Format_ARGB32;
            break;
        case Image::Format_ARGB32_Premultiplied:
            qfmt = QImage::Format_ARGB32_Premultiplied;
            break;
        case Image::Format_RGB32:
            qfmt = QImage::Format_RGB32;
            break;
        case Image::Format_Invalid:
        default:
            return QImage();
        }

        auto *owner = new std::shared_ptr<const void>(image.pixelOwner());
        return QImage(image.constBits(),
                      image.width(),
                      image.height(),
                      image.bytesPerLine(),
                      qfmt,
                      [](void *info) {
                          delete static_cast<std::shared_ptr<const void> *>(info);
                      },
                      owner);
    }
};

} // namespace AnimeCore

#endif // ANIMECORE_QTBRIDGE_IMAGE_H
