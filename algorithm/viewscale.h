#ifndef VIEWSCALE_H
#define VIEWSCALE_H

#include <QPointF>
#include <algorithm>

// ---------------------------------------------------------------------------
// THE shared home of screen <-> document (canvas) conversion. Anything in
// AnimeAn that turns a viewport position into a canvas position, or sizes a
// threshold that belongs to the USER's eye and hand rather than to the
// drawing, belongs HERE - do not re-derive `/ zoom` elsewhere. Every call site
// should carry a short comment pointing back to this header so the next reader
// knows where the wheel lives. The Python mirror is pyfile/viewscale.py.
//
// The view transform is: screen = document * zoom + pan. Everything below is
// that one equation, named.
//
// WHICH SPACE IS A NUMBER IN?
//   Screen px  - anything the hand or the eye owns: tremor, tablet report
//                distance, hit-test slop, hold-still radius, minimum visible
//                line width. These do NOT change when the canvas is zoomed;
//                the user's hand and screen did not change.
//   Document px - anything the artwork owns: stroke geometry, arc lengths,
//                canvas bounds. These do not change when the view is zoomed.
// A constant is a bug the moment it is used in the space it was not written
// for, which is exactly the mistake this header exists to prevent.
// ---------------------------------------------------------------------------

namespace AnimeViewScale {

// Zoom is a divisor throughout, so a non-finite or non-positive value would
// poison every conversion. Callers pass raw view state; this is the one guard.
inline qreal safeZoom(qreal zoom)
{
    return (zoom > 0.0 && zoom < 1e9) ? zoom : 1.0;
}

// --- points -----------------------------------------------------------------

inline QPointF toDocument(const QPointF &screen, qreal zoom, const QPointF &pan)
{
    return (screen - pan) / safeZoom(zoom);
}

inline QPointF toScreen(const QPointF &document, qreal zoom, const QPointF &pan)
{
    return document * safeZoom(zoom) + pan;
}

// --- lengths ----------------------------------------------------------------
// A length has no pan term: only the scale survives.

// How much artwork one screen-sized measurement covers. Use this to spend a
// screen-px budget (tremor, slop, a hold radius) on document geometry.
inline qreal toDocumentLength(qreal screenPx, qreal zoom)
{
    return screenPx / safeZoom(zoom);
}

// How big a piece of artwork looks right now.
inline qreal toScreenLength(qreal documentPx, qreal zoom)
{
    return documentPx * safeZoom(zoom);
}

// --- clamped budgets --------------------------------------------------------

// The working range of a gesture budget, named ONCE so the view side and the
// fitter cannot drift apart (they were two independently written copies of
// these numbers).
//
// The zoom UI spans 0.1x .. 8x, so only the upper bound is slack. The LOWER
// bound binds, on purpose: from 0.1x to 0.25x the budgets stop widening, and
// they should - a capture floor that kept growing with the zoom-out would
// visibly facet long sweeping strokes, and no budget buys anything once the
// artwork is smaller than the tremor it is meant to reject.
inline constexpr qreal kBudgetMinZoom = 0.25;
inline constexpr qreal kBudgetMaxZoom = 16.0;

// Document px per screen px, with the zoom clamped first.
//
// Budgets that are converted ONCE and then govern a whole gesture (the stroke
// fitter's tremor budgets, the capture floor) clamp the zoom before dividing:
// past the clamp the conversion stops buying anything real. Zoomed far in, a
// finer capture is bounded by the tablet report rate, not by the budget;
// zoomed far out, a coarser one would visibly facet long sweeping strokes.
// Pure geometry (a cursor position, a hit test, a hold radius) must NOT use
// this - it needs the true zoom, so it stays exact at every magnification.
inline qreal pixelScale(qreal zoom, qreal minZoom = kBudgetMinZoom,
                        qreal maxZoom = kBudgetMaxZoom)
{
    const qreal z = std::max(minZoom, std::min(maxZoom, safeZoom(zoom)));
    return 1.0 / z;
}

// The same clamp for a value that is ALREADY document px per screen px, for
// code that receives the converted scale rather than the zoom. Kept as its
// own function rather than inverting twice: round-tripping through 1/x moves
// degenerate inputs (a zero or negative scale) to a different answer.
inline qreal clampPixelScale(qreal scale, qreal minZoom = kBudgetMinZoom,
                             qreal maxZoom = kBudgetMaxZoom)
{
    return std::max(1.0 / maxZoom, std::min(1.0 / minZoom, scale));
}

}   // namespace AnimeViewScale

#endif // VIEWSCALE_H
