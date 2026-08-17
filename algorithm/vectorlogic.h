#ifndef VECTORLOGIC_H
#define VECTORLOGIC_H

#include "algorithm/animemodel.h"

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

struct AnimeVectorRange {
    qreal first = 0.0;
    qreal second = 1.0;
};

// One place where a stroke crosses another - or ITSELF. Carries both sides of
// the junction: where it sits on the stroke being cut, and where it sits on
// the line it crossed. A junction belongs to two lines, and a cut that only
// ever knows about one of them leaves the other running whole through a point
// that has just become its neighbour's endpoint.
struct AnimeStrokeCrossing {
    // What the crossing was with. Anything >= 0 indexes the walls vector.
    enum Kind {
        StrokeEnd = -1,   // not a junction at all: an end of the cut stroke
        SelfCrossing = -2 // the stroke through itself; wallArc is its OTHER arc
    };

    qreal arc = 0.0;       // normalised position along the cut stroke
    int wallIndex = StrokeEnd;
    qreal wallArc = 0.0;   // normalised position along the line it crossed

    bool isJunction() const { return wallIndex != StrokeEnd; }
};

// What a CutMode click removes, and which junctions bounded it.
struct AnimeCutPlan {
    AnimeVectorRange span;      // the arc range to remove from the target
    AnimeStrokeCrossing low;    // wallIndex < 0 when this bound is the start
    AnimeStrokeCrossing high;   // wallIndex < 0 when this bound is the end
};

// 1 Euro filter over a 2D input stream, with phase-lag compensation.
//
// The classic adaptive lowpass: the cutoff frequency follows the FILTERED
// speed, so slow, precise movement gets heavy smoothing (jitter dies) while a
// fast sweep opens the filter up (no perceptible lag). On top of that, the
// output is pushed toward the raw sample by the estimated lag distance
// |v| * tau - a first-order lowpass trails a constant-velocity input by
// exactly its time constant, so adding that distance back cancels the trail.
// The push is clamped to the raw-filtered gap, so it can never overshoot the
// physical pen tip, and at rest the gap is noise-sized so the push is too.
//
// Pure math, no Qt widgets: lives here so it can be unit-tested standalone.
class AnimeOneEuroFilter {
public:
    // strength 0..1: 0 disables (passthrough), 1 is the heaviest smoothing.
    void configure(qreal strength);
    void reset();
    // timestampMs comes from the input event; a nonsense dt falls back to
    // assuming kFallbackHz.
    QPointF filter(const QPointF &raw, qulonglong timestampMs);

    bool active() const { return m_strength > 0.0; }

private:
    static QPointF lowpass(const QPointF &value, const QPointF &previous, qreal alpha);
    qreal alphaFor(qreal cutoff, qreal dt) const;

    qreal m_strength = 0.0;
    qreal m_minCutoff = 1.0;   // Hz; floor of the adaptive cutoff
    qreal m_beta = 0.0;        // cutoff gain per px/s of speed
    qreal m_derivativeCutoff = 1.0;
    qreal m_compensation = 0.0; // 0..1 share of the lag distance added back
    bool m_initialized = false;
    QPointF m_position;
    QPointF m_lastRaw;   // previous RAW sample: the velocity difference base
    QPointF m_velocity;
    qulonglong m_lastMs = 0;
};

// How a committed stroke is turned into lines + curves. SEPARATE from the
// realtime stabilizer above: measured over the full range, the two are
// near-orthogonal - the stabilizer decides how faithful the result is to what
// the hand meant, the settings here decide how economically it is described -
// and one knob driving both could only ever walk the diagonal of that square.
struct AnimeStrokeFitSettings {
    // 0..100: how aggressively straight runs are merged and curves are
    // allowed to depart from the samples. Low = follow the input closely with
    // many nodes; high = few nodes, looser.
    int simplify = 50;
    // 0..100: how eagerly a turn is called a CORNER rather than a curve.
    // Low = only very sharp turns break the curve (rounder, fewer breaks);
    // high = gentle turns already count (crisper angles, more pieces).
    int corner = 50;
};

struct AnimeVectorRegionFace {
    QPainterPath path;
    qreal signedArea = 0.0;
};

namespace AnimeVectorLogic {
qreal epsilon();

QVector<QPointF> filteredPoints(const QVector<QPointF> &points);
QPainterPath makeSmoothedPath(const QVector<QPointF> &points, int smoothValue);
QPainterPath makePolylinePath(const QVector<QPointF> &points);
// Hybrid polyline + Bezier fit: Gaussian denoise, split at corners and
// inflections, classify each piece straight/curved, emit chords for the
// straight ones and least-squares cubic Beziers for the curved ones, G1 at
// every joint that is not a deliberate corner.
QPainterPath fitStrokePath(const QVector<QPointF> &points,
                           const AnimeStrokeFitSettings &settings = {});
AnimeVectorStroke makeStroke(const QVector<QPointF> &points,
                             const QColor &color,
                             qreal width,
                             int id = 0,
                             bool filterInput = true,
                             bool smoothPath = true,
                             const AnimeStrokeFitSettings &settings = {});
AnimeVectorStroke makeStrokeFromPath(const QPainterPath &path,
                                     const QVector<QPointF> &points,
                                     const QColor &color,
                                     qreal width,
                                     int id = 0);

bool strokeHitsCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius);
bool strokeHitsCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius);

// Distance from the brush AXIS to the stroke's centreline - `from == to` for
// a click, the swept segment for a drag step. The stroke's own width is not
// subtracted: this ranks candidates against the brush centre, and a thick
// line should not win over a thin one it merely happens to be fatter than.
qreal strokeDistanceToBrush(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to);
QVector<AnimeVectorRange> keepRangesForCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius);
QVector<AnimeVectorRange> keepRangesForCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius);
QVector<AnimeVectorRange> complementRanges(const QVector<AnimeVectorRange> &eraseRanges);

// Every crossing of `stroke` with `walls` AND with itself, sorted by arc
// along `stroke` and de-duplicated within `mergeTolerance` (a crossing that
// lands on a shared vertex is found by both adjoining segments and is still
// one crossing).
//
// A self-crossing - the stroke loops back through its own path - is a
// junction like any other, and appears TWICE: once at each of the two arcs
// the stroke passes through that point, each naming the other in wallArc.
// Adjoining segments and the closing vertex of a closed stroke are not
// crossings; they are how a line is joined to itself, not where it cuts
// across itself.
QVector<AnimeStrokeCrossing> strokeCrossings(const AnimeVectorStroke &stroke,
                                             const QVector<AnimeVectorStroke> &walls,
                                             qreal mergeTolerance = 1e-4);

// The span to cut out of `stroke` for a CutMode click at `pos`: the piece
// between the crossing before the click and the crossing after it.
//
// "Before" and "after" are along the stroke's own arc, so the two bounds are
// always on opposite sides of the click - that is what makes the removed
// piece the one the user is pointing at rather than an arbitrary neighbour.
// With a crossing on only one side the cut runs from it to that END of the
// stroke, and with no crossing at all the whole stroke goes: an uncrossed
// stroke is a single span bounded by its two ends, so the rule is the same
// one, not a special case.
//
// Returns false when the stroke has no length (nothing to cut).
bool planCutAt(const AnimeVectorStroke &stroke,
               const QVector<AnimeVectorStroke> &walls,
               const QPointF &pos,
               AnimeCutPlan *plan);

// The span alone, for callers that do not care which junctions bounded it.
bool cutSpanAt(const AnimeVectorStroke &stroke,
               const QVector<AnimeVectorStroke> &walls,
               const QPointF &pos,
               AnimeVectorRange *span);

// Splits `stroke` at each normalised arc in `arcs`, in order. Arcs within
// `endTolerance` of either end are ignored: there is nothing to divide at a
// point that already IS an endpoint, and splitting there would only shed a
// degenerate sliver. Returns the stroke unchanged (one piece) when no arc
// survives that test.
QVector<AnimeVectorStroke> splitStrokeAt(const AnimeVectorStroke &stroke,
                                         QVector<qreal> arcs,
                                         const AnimeStrokeFitSettings &settings = {},
                                         qreal endTolerance = 1e-3);
AnimeVectorStroke subStroke(const AnimeVectorStroke &stroke, qreal fromW, qreal toW,
                            const AnimeStrokeFitSettings &settings = {});
QPointF pointAtLength(const AnimeVectorStroke &stroke, qreal length);

// The DOCUMENT-space width to hand the pen so a stroke never renders thinner
// than `minScreenPx` on screen. Display only - the stored width is untouched.
// A smooth maximum (6-norm) rather than a hard max: the width the user sees
// is a differentiable function of zoom, with no kink at the floor, and above
// the floor it converges to the true width almost immediately (0.07% at 3x).
qreal displayStrokeWidth(qreal documentWidth, qreal zoom, qreal minScreenPx = 1.0);

QVector<QLineF> segmentsFromPath(const QPainterPath &path);
QVector<AnimeVectorRegionFace> computeVectorRegionFaces(const QVector<QLineF> &segments);
QPainterPath vectorRegionPathAt(const QPointF &seed, const QVector<QLineF> &segments, const QRect &canvasRect);
QPainterPath fillPathFromMask(const QPoint &seed, const QImage &boundary);
}

#endif // VECTORLOGIC_H
