#ifndef ANIMECORE_CURSOR_H
#define ANIMECORE_CURSOR_H

#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"

namespace AnimeCore {

enum CursorShape {
    ArrowCursor = 0,
    UpArrowCursor = 1,
    CrossCursor = 2,
    WaitCursor = 3,
    IBeamCursor = 4,
    SizeVerCursor = 5,
    SizeHorCursor = 6,
    SizeBDiagCursor = 7,
    SizeFDiagCursor = 8,
    SizeAllCursor = 9,
    BlankCursor = 10,
    SplitVCursor = 11,
    SplitHCursor = 12,
    PointingHandCursor = 13,
    ForbiddenCursor = 14,
    WhatsThisCursor = 15,
    BusyCursor = 16,
    OpenHandCursor = 17,
    ClosedHandCursor = 18,
    DragCopyCursor = 19,
    DragMoveCursor = 20,
    DragLinkCursor = 21,
    LastCursor = DragLinkCursor,
    BitmapCursor = 24,
    CustomCursor = 25
};

class Cursor
{
public:
    Cursor() noexcept = default;
    Cursor(CursorShape shape) noexcept : m_shape(shape) {}
    Cursor(const Image &pixmap, int hotX = -1, int hotY = -1)
        : m_shape(BitmapCursor)
        , m_pixmap(pixmap)
        , m_hotSpot(hotX < 0 ? pixmap.width() / 2 : hotX,
                    hotY < 0 ? pixmap.height() / 2 : hotY)
    {
    }

    CursorShape shape() const noexcept { return m_shape; }
    Image pixmap() const { return m_pixmap; }
    Point hotSpot() const noexcept { return m_hotSpot; }

private:
    CursorShape m_shape = ArrowCursor;
    Image m_pixmap;
    Point m_hotSpot;
};

} // namespace AnimeCore

#endif // ANIMECORE_CURSOR_H
