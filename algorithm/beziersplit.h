#ifndef BEZIERSPLIT_H
#define BEZIERSPLIT_H

#include <QPainterPath>
#include <QPointF>
#include <QVector>

// ---------------------------------------------------------------------------
// THE shared home of exact bezier splitting. Anything in AnimeAn that divides
// a cubic, extracts the [t0, t1] stretch of one, or cuts a QPainterPath at an
// arc position belongs HERE - do not re-derive de Casteljau elsewhere. Every
// call site should carry a short comment pointing back to this header so the
// next reader knows where the wheel lives.
//
// The core guarantee: a sub-curve produced by these functions IS the original
// curve restricted to the requested range (de Casteljau is exact), so cutting
// never changes the drawn shape. Where a position must first be FOUND on the
// path (an arc fraction, an anchor point), precision is governed by
// AnimeBezierSplitOptions - the knobs trade accuracy against cost.
// ---------------------------------------------------------------------------

// One run element of a path: a straight edge or a cubic. Quads never appear -
// QPainterPath stores quadTo as an elevated cubic.
struct AnimeBezierCurve {
    bool isLine = false;
    QPointF p[4];   // cubics use all four; a line uses p[0] and p[3]
};

// One labelled arc sample: where (curve, t) sits and how far along the whole
// path it is. Joints carry a duplicate sample ((c, 1) then (c+1, 0), same
// point) so any non-degenerate interval between neighbours stays inside a
// single curve.
struct AnimeBezierPathSample {
    int curve = 0;
    qreal t = 0.0;
    QPointF point;
    qreal length = 0.0;   // cumulative along the path
};

// A resolved position on the path, ready to split at.
struct AnimeBezierPathPosition {
    int curve = 0;
    qreal t = 0.0;
    QPointF point;
};

// Precision / cost knobs. The defaults suit interactive editing: ~3 px arc
// sampling, and three refinement rounds that pin a located position to about
// sampleSpacing / refineSteps^rounds - far below a pixel even on the longest
// single curve the cap allows (512 * 3 px hull).
struct AnimeBezierSplitOptions {
    // Arc-table density: one sample every ~targetSpacing px of hull length.
    qreal targetSpacing = 3.0;
    // Ceiling per curve, so one enormous element cannot explode the table.
    // Spacing degrades to hull/maxSamplesPerCurve beyond the cap; the
    // refinement rounds keep located positions accurate regardless.
    int maxSamplesPerCurve = 512;
    // Each refinement round subdivides the winning interval this many times
    // and recurses into the slice around the winner.
    int refineSteps = 16;
    int refineRounds = 3;
    // How strongly locate() prefers the sample whose LENGTH fraction matches
    // the requested arc, per px of disagreement. Disambiguates the branches
    // of a stroke that passes close to itself; far too weak to move a point
    // off the locally nearest position.
    qreal branchPull = 0.05;
};

// A parsed-and-sampled path, reusable across many locate/slice calls on the
// same stroke.
struct AnimeBezierPathRun {
    QVector<AnimeBezierCurve> curves;
    QVector<AnimeBezierPathSample> samples;

    bool isValid() const { return !curves.isEmpty() && samples.size() >= 2; }
    qreal totalLength() const { return samples.isEmpty() ? 0.0 : samples.last().length; }
};

namespace AnimeBezierSplit {

// De Casteljau at t: returns the on-curve point and, when asked, the control
// points of the two halves. The halves ARE the original curve restricted to
// [0, t] and [t, 1].
QPointF splitCubic(const QPointF p[4], qreal t, QPointF *left = nullptr, QPointF *right = nullptr);

// The [t0, t1] stretch of one cubic, as a cubic (two de Casteljau splits).
void cubicSegment(const QPointF p[4], qreal t0, qreal t1, QPointF out[4]);

// Evaluate one run element at t.
QPointF curvePoint(const AnimeBezierCurve &curve, qreal t);

// A path as one uninterrupted run of curves. False for anything else (empty,
// several subpaths, malformed curve data).
bool parsePath(const QPainterPath &path, QVector<AnimeBezierCurve> *out);

// Parse + arc-sample in one step. Check isValid() on the result.
AnimeBezierPathRun buildRun(const QPainterPath &path,
                            const AnimeBezierSplitOptions &options = {});

// Where the arc fraction `w` (0..1 of the run's own length) sits on the path.
AnimeBezierPathPosition locate(const AnimeBezierPathRun &run, qreal w,
                               const AnimeBezierSplitOptions &options = {});

// Same, but anchored: `nearTo` is where the caller believes that arc is in
// space (e.g. a point measured on a different parameterization of the same
// stroke, or a crossing point). The answer is pulled to the locally nearest
// path position around that anchor, with `w` only breaking self-overlap ties.
AnimeBezierPathPosition locate(const AnimeBezierPathRun &run, qreal w, const QPointF &nearTo,
                               const AnimeBezierSplitOptions &options = {});

// The stretch of the path between two positions: an exact sub-path, plus (if
// wanted) a dense polyline of it built from the run's samples. False when the
// range is empty or inverted.
bool slice(const AnimeBezierPathRun &run,
           const AnimeBezierPathPosition &from,
           const AnimeBezierPathPosition &to,
           QPainterPath *path,
           QVector<QPointF> *polyline = nullptr);

} // namespace AnimeBezierSplit

#endif // BEZIERSPLIT_H
