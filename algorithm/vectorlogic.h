#ifndef VECTORLOGIC_H
#define VECTORLOGIC_H

#include "algorithm/animemodel.h"

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/path.h"

#include <cstdint>


struct AnimeVectorRange {
    double first = 0.0;
    double second = 1.0;
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

    double arc = 0.0;       // normalised position along the cut stroke
    int wallIndex = StrokeEnd;
    double wallArc = 0.0;   // normalised position along the line it crossed

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
    void configure(double strength);
    void reset();
    // timestampMs comes from the input event; a nonsense dt falls back to
    // assuming kFallbackHz.
    AnimeCore::PointF filter(const AnimeCore::PointF &raw, std::uint64_t timestampMs);

    bool active() const { return m_strength > 0.0; }

private:
    static AnimeCore::PointF lowpass(const AnimeCore::PointF &value, const AnimeCore::PointF &previous, double alpha);
    double alphaFor(double cutoff, double dt) const;

    double m_strength = 0.0;
    double m_minCutoff = 1.0;   // Hz; floor of the adaptive cutoff
    double m_beta = 0.0;        // cutoff gain per px/s of speed
    double m_derivativeCutoff = 1.0;
    double m_compensation = 0.0; // 0..1 share of the lag distance added back
    bool m_initialized = false;
    AnimeCore::PointF m_position;
    AnimeCore::PointF m_lastRaw;   // previous RAW sample: the velocity difference base
    AnimeCore::PointF m_velocity;
    std::uint64_t m_lastMs = 0;
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
    // Document px per SCREEN px at drawing time (1/zoom, clamped upstream).
    // The hand, the device's report rate and the eye all live in SCREEN
    // space - tremor amplitude, tangent windows and visible tolerances are
    // screen-sized quantities - so every px budget in the fitter is defined
    // in screen px and converted to document units with this factor. 1.0
    // (the default) reproduces the un-zoomed behavior exactly. Last member
    // on purpose: {simplify, corner} aggregate initializers stay valid.
    double pixelScale = 1.0;
};

struct AnimeVectorRegionFace {
    AnimeCore::Path path;
    double signedArea = 0.0;
};

// State of one live-drawn stroke's incremental fit (see liveFitStrokePath):
// the fitted-and-frozen prefix, and where its boundary sits. Value-reset on
// pen-down.
struct AnimeLiveFitState {
    // Everything baked EXCEPT the trailing open chord of straight mode.
    AnimeCore::Path frozenPath;
    int frozenSamples = 0;      // boundary sample index; also the tail's first
    // >= 0: straight mode - an open chord runs from chordAnchor to the
    // boundary seam, extended (never re-emitted) while the samples stay
    // within line tolerance of it. The straight test spans the WHOLE run,
    // so its scale is the drawing's, not the bake chunk's. -1: curved mode.
    int chordStartSample = -1;
    AnimeCore::PointF chordAnchor;        // where the open chord is pinned
    // Where the frozen prefix currently ends. In straight mode this is a
    // DENOISED endpoint (the offline fit chords the smoothed polyline too;
    // raw tremor at the endpoint made the pivot cap slice a ruler-straight
    // drag into many chords). The tail is pinned to it, so seams stay G0.
    AnimeCore::PointF seamPoint;
    bool hasSeam = false;
    AnimeCore::PointF boundaryPoint;      // raw boundary sample - retro-edit guard
    AnimeCore::PointF entryTangent;       // forward-travel unit tangent at the boundary
    bool hasEntryTangent = false;
    // The open CURVED RUN, for node-count consolidation: every bake refits
    // the whole run and REPLACES its elements when the part already on
    // screen would move imperceptibly (a per-chunk bake alone described one
    // doodle with 2.3x the nodes of the offline fit). -1: no open run.
    int runStartSample = -1;
    int runStartElement = 0;    // frozenPath element index where the run begins
    AnimeCore::PointF runStartPoint;
    AnimeCore::PointF runEntryTangent;
    bool hasRunEntryTangent = false;
};

namespace AnimeVectorLogic {

// How far a traced fill region is grown so it tucks UNDER the stroke that
// walls it instead of leaving a hairline of paper showing. Anything that
// clips a region has to leave this much room, which is why it is public.
inline constexpr double kVectorRegionOverpaintWidth = 2.0;
double epsilon();

AnimeCore::Vector<AnimeCore::PointF> filteredPoints(const AnimeCore::Vector<AnimeCore::PointF> &points);
AnimeCore::Path makeSmoothedPath(const AnimeCore::Vector<AnimeCore::PointF> &points, int smoothValue);
AnimeCore::Path makePolylinePath(const AnimeCore::Vector<AnimeCore::PointF> &points);
// Hybrid polyline + Bezier fit: Gaussian denoise, split at corners and
// inflections, classify each piece straight/curved, emit chords for the
// straight ones and least-squares cubic Beziers for the curved ones, G1 at
// every joint that is not a deliberate corner.
AnimeCore::Path fitStrokePath(const AnimeCore::Vector<AnimeCore::PointF> &points,
                           const AnimeStrokeFitSettings &settings = {});
// Incremental fitting for the LIVE stroke. Ink far enough behind the pen is
// fitted once and FROZEN (its curve never recomputed - recomputation is what
// made the already-drawn path tremble under the pen); only the tail refits
// each frame, entering the frozen boundary with the same tangent the baked
// side left with, so the seams stay G1. Committing the returned path on
// release makes "what you saw while drawing" and "what you get" identical
// by construction. The state belongs to one stroke gesture: value-reset it
// on pen-down, then pass it to every call for that stroke.
// `finalTail` false (while drawing): the tail is previewed as a denoised
// POLYLINE - append-only beyond the smoothing kernel's reach, so only the
// last few px at the pen tip move at all. Fitting the tail per frame made
// the whole 40-88 screen px behind the pen wobble within the fit tolerance,
// which slow zoomed-in detail work stared at for seconds at a time. true
// (on release): the tail is fitted like everything else, so the committed
// stroke is pure lines + curves; the settle is bounded by the fit tolerance
// and happens once, at pen-up.
AnimeCore::Path liveFitStrokePath(struct AnimeLiveFitState &state,
                               const AnimeCore::Vector<AnimeCore::PointF> &points,
                               const AnimeStrokeFitSettings &settings = {},
                               bool finalTail = false);
AnimeVectorStroke makeStroke(const AnimeCore::Vector<AnimeCore::PointF> &points,
                             const AnimeCore::Color &color,
                             double width,
                             int id = 0,
                             bool filterInput = true,
                             bool smoothPath = true,
                             const AnimeStrokeFitSettings &settings = {});
AnimeVectorStroke makeStrokeFromPath(const AnimeCore::Path &path,
                                     const AnimeCore::Vector<AnimeCore::PointF> &points,
                                     const AnimeCore::Color &color,
                                     double width,
                                     int id = 0);

bool strokeHitsCircle(const AnimeVectorStroke &stroke, const AnimeCore::PointF &center, double radius);
bool strokeHitsCapsule(const AnimeVectorStroke &stroke, const AnimeCore::PointF &from, const AnimeCore::PointF &to, double radius);

// Distance from the brush AXIS to the stroke's centreline - `from == to` for
// a click, the swept segment for a drag step. The stroke's own width is not
// subtracted: this ranks candidates against the brush centre, and a thick
// line should not win over a thin one it merely happens to be fatter than.
double strokeDistanceToBrush(const AnimeVectorStroke &stroke, const AnimeCore::PointF &from, const AnimeCore::PointF &to);
AnimeCore::Vector<AnimeVectorRange> keepRangesForCircle(const AnimeVectorStroke &stroke, const AnimeCore::PointF &center, double radius);
AnimeCore::Vector<AnimeVectorRange> keepRangesForCapsule(const AnimeVectorStroke &stroke, const AnimeCore::PointF &from, const AnimeCore::PointF &to, double radius);
AnimeCore::Vector<AnimeVectorRange> complementRanges(const AnimeCore::Vector<AnimeVectorRange> &eraseRanges);

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
AnimeCore::Vector<AnimeStrokeCrossing> strokeCrossings(const AnimeVectorStroke &stroke,
                                             const AnimeCore::Vector<AnimeVectorStroke> &walls,
                                             double mergeTolerance = 1e-4);

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
               const AnimeCore::Vector<AnimeVectorStroke> &walls,
               const AnimeCore::PointF &pos,
               AnimeCutPlan *plan);

// The span alone, for callers that do not care which junctions bounded it.
bool cutSpanAt(const AnimeVectorStroke &stroke,
               const AnimeCore::Vector<AnimeVectorStroke> &walls,
               const AnimeCore::PointF &pos,
               AnimeVectorRange *span);

// Splits `stroke` at each normalised arc in `arcs`, in order. Arcs within
// `endTolerance` of either end are ignored: there is nothing to divide at a
// point that already IS an endpoint, and splitting there would only shed a
// degenerate sliver. Returns the stroke unchanged (one piece) when no arc
// survives that test.
// `usedArcs`, when given, receives the arcs that actually divided the stroke
// (sorted, end-filtered, de-duplicated): pieces[k] and pieces[k+1] share the
// boundary at usedArcs[k], so a caller can name each junction without
// guessing by proximity.
AnimeCore::Vector<AnimeVectorStroke> splitStrokeAt(const AnimeVectorStroke &stroke,
                                         AnimeCore::Vector<double> arcs,
                                         const AnimeStrokeFitSettings &settings = {},
                                         double endTolerance = 1e-3,
                                         AnimeCore::Vector<double> *usedArcs = nullptr);
AnimeVectorStroke subStroke(const AnimeVectorStroke &stroke, double fromW, double toW,
                            const AnimeStrokeFitSettings &settings = {});
AnimeCore::PointF pointAtLength(const AnimeVectorStroke &stroke, double length);

// The stroke with one TERMINAL point moved to `target` (path element and
// polyline vertex together; lengths and bounds rebuilt). A junction belongs
// to every line that meets it: exact path splitting puts each piece's cut
// end on its OWN fitted path, and two fitted paths pass a fit tolerance
// apart near the crossing - sub-pixel gaps that the fill's planar graph
// (vertex snap 0.001 px) can never close. The nudge is at most that same
// fit tolerance, confined to the endpoint.
AnimeVectorStroke snapStrokeEndpoint(const AnimeVectorStroke &stroke, bool atEnd,
                                     const AnimeCore::PointF &target);

// The DOCUMENT-space width to hand the pen so a stroke never renders thinner
// than `minScreenPx` on screen. Display only - the stored width is untouched.
// A smooth maximum (6-norm) rather than a hard max: the width the user sees
// is a differentiable function of zoom, with no kink at the floor, and above
// the floor it converges to the true width almost immediately (0.07% at 3x).
double displayStrokeWidth(double documentWidth, double zoom, double minScreenPx = 1.0);

AnimeCore::Vector<AnimeCore::LineF> segmentsFromPath(const AnimeCore::Path &path);
// healRadius > 0 additionally attaches every DANGLING end (a point where
// exactly one segment terminates) to the nearest point of a segment it does
// not terminate, when one lies within that distance. Drawings whose
// junctions carry sub-pixel gaps - strokes cut before the endpoint snap
// existed, hand-drawn corners that almost meet, a line end floating a fit
// tolerance off the wall it was cut against - close into fillable faces.
// Attaching to existing ink can never invent a boundary between two separate
// lines, and interior vertices are untouched.
AnimeCore::Vector<AnimeVectorRegionFace> computeVectorRegionFaces(const AnimeCore::Vector<AnimeCore::LineF> &segments,
                                                        double healRadius = 0.0);
AnimeCore::Path vectorRegionPathAt(const AnimeCore::PointF &seed, const AnimeCore::Vector<AnimeCore::LineF> &segments, const AnimeCore::Rect &canvasRect);
AnimeCore::Path fillPathFromMask(const AnimeCore::Point &seed, const AnimeCore::Image &boundary);
}

#endif // VECTORLOGIC_H
