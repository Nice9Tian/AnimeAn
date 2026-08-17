#include "beziersplit.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QPointF lerpPoint(const QPointF &a, const QPointF &b, qreal t)
{
    return a + (b - a) * t;
}

// An interval between two consecutive samples is refinable when it spans a
// real stretch of one curve; the duplicated joint samples produce degenerate
// (cross-curve, zero-length) intervals that must be stepped over.
bool refinableInterval(const QVector<AnimeBezierPathSample> &samples, int i0, int i1)
{
    return i0 >= 0 && i1 < samples.size()
           && samples[i0].curve == samples[i1].curve
           && samples[i1].t > samples[i0].t + 1e-12;
}

struct LocateCost {
    // Anchored mode: pull toward `nearTo`, with the arc only breaking ties
    // between branches of a self-overlapping stroke. Pure-arc mode (no
    // anchor): the arc IS the target.
    bool anchored = false;
    QPointF nearTo;
    qreal w = 0.0;
    qreal total = 0.0;
    qreal branchPull = 0.05;

    qreal operator()(const QPointF &point, qreal length) const
    {
        const qreal arcMiss = std::abs(length - w * total);
        if (!anchored) {
            return arcMiss;
        }
        return QLineF(point, nearTo).length() + branchPull * arcMiss;
    }
};

AnimeBezierPathPosition locateWithCost(const AnimeBezierPathRun &run,
                                       qreal w,
                                       const LocateCost &cost,
                                       const AnimeBezierSplitOptions &options)
{
    const QVector<AnimeBezierPathSample> &samples = run.samples;
    AnimeBezierPathPosition position;
    if (w <= 1e-9) {
        position.point = samples.first().point;
        return position;
    }
    if (w >= 1.0 - 1e-9) {
        position.curve = run.curves.size() - 1;
        position.t = 1.0;
        position.point = samples.last().point;
        return position;
    }

    int best = 0;
    qreal bestCost = std::numeric_limits<qreal>::max();
    for (int j = 0; j < samples.size(); ++j) {
        const qreal candidate = cost(samples[j].point, samples[j].length);
        if (candidate < bestCost) {
            bestCost = candidate;
            best = j;
        }
    }
    position.curve = samples[best].curve;
    position.t = samples[best].t;
    position.point = samples[best].point;

    // Refine every real interval around the winner. The window is [-2, +1]
    // because a joint occupies TWO sample indices (its duplicate pair): when
    // the winner is a joint copy, the nearest real intervals sit one step
    // further out on each side, and a narrower window silently skipped the
    // stretch just after a joint - cuts there snapped back onto the joint
    // (measured up to half a sample step, 15 px on a long straight run).
    for (int offset = -2; offset <= 1; ++offset) {
        int i0 = best + offset;
        int i1 = i0 + 1;
        if (!refinableInterval(samples, i0, i1)) {
            continue;
        }
        const AnimeBezierCurve &curve = run.curves[samples[i0].curve];
        qreal tA = samples[i0].t;
        qreal tB = samples[i1].t;
        qreal lengthA = samples[i0].length;
        qreal lengthB = samples[i1].length;

        // Iterative subdivision: each round narrows to the slice around the
        // round's winner, so precision reaches roughly
        // sampleSpacing / refineSteps^rounds at a fixed, small cost.
        const int steps = std::max(2, options.refineSteps);
        for (int round = 0; round < std::max(1, options.refineRounds); ++round) {
            int bestStep = 0;
            qreal bestStepCost = std::numeric_limits<qreal>::max();
            for (int k = 0; k <= steps; ++k) {
                const qreal blend = qreal(k) / steps;
                const qreal t = tA + (tB - tA) * blend;
                const QPointF point = AnimeBezierSplit::curvePoint(curve, t);
                const qreal length = lengthA + (lengthB - lengthA) * blend;
                const qreal candidate = cost(point, length);
                if (candidate < bestStepCost) {
                    bestStepCost = candidate;
                    bestStep = k;
                }
                if (candidate < bestCost) {
                    bestCost = candidate;
                    position.curve = samples[i0].curve;
                    position.t = t;
                    position.point = point;
                }
            }
            const qreal loBlend = qreal(std::max(0, bestStep - 1)) / steps;
            const qreal hiBlend = qreal(std::min(steps, bestStep + 1)) / steps;
            const qreal nextTA = tA + (tB - tA) * loBlend;
            const qreal nextTB = tA + (tB - tA) * hiBlend;
            const qreal nextLengthA = lengthA + (lengthB - lengthA) * loBlend;
            const qreal nextLengthB = lengthA + (lengthB - lengthA) * hiBlend;
            tA = nextTA;
            tB = nextTB;
            lengthA = nextLengthA;
            lengthB = nextLengthB;
        }
    }
    return position;
}

bool positionBefore(const AnimeBezierPathPosition &a, const AnimeBezierPathPosition &b)
{
    if (a.curve != b.curve) {
        return a.curve < b.curve;
    }
    return a.t + 1e-9 < b.t;
}

// Appends the [t0, t1] part of one curve to a path whose current point is
// already the part's start.
void appendSegment(QPainterPath &path, const AnimeBezierCurve &curve, qreal t0, qreal t1)
{
    if (t1 - t0 <= 1e-9) {
        return;
    }
    if (curve.isLine) {
        path.lineTo(lerpPoint(curve.p[0], curve.p[3], t1));
        return;
    }
    QPointF part[4];
    AnimeBezierSplit::cubicSegment(curve.p, t0, t1, part);
    path.cubicTo(part[1], part[2], part[3]);
}

} // namespace

QPointF AnimeBezierSplit::splitCubic(const QPointF p[4], qreal t, QPointF *left, QPointF *right)
{
    const QPointF p01 = lerpPoint(p[0], p[1], t);
    const QPointF p12 = lerpPoint(p[1], p[2], t);
    const QPointF p23 = lerpPoint(p[2], p[3], t);
    const QPointF p012 = lerpPoint(p01, p12, t);
    const QPointF p123 = lerpPoint(p12, p23, t);
    const QPointF mid = lerpPoint(p012, p123, t);
    if (left) {
        left[0] = p[0];
        left[1] = p01;
        left[2] = p012;
        left[3] = mid;
    }
    if (right) {
        right[0] = mid;
        right[1] = p123;
        right[2] = p23;
        right[3] = p[3];
    }
    return mid;
}

void AnimeBezierSplit::cubicSegment(const QPointF p[4], qreal t0, qreal t1, QPointF out[4])
{
    if (t1 <= t0) {
        out[0] = out[1] = out[2] = out[3] = splitCubic(p, t0, nullptr, nullptr);
        return;
    }
    QPointF right[4];
    if (t0 > 0.0) {
        splitCubic(p, t0, nullptr, right);
    } else {
        right[0] = p[0];
        right[1] = p[1];
        right[2] = p[2];
        right[3] = p[3];
    }
    if (t1 < 1.0) {
        splitCubic(right, (t1 - t0) / (1.0 - t0), out, nullptr);
    } else {
        out[0] = right[0];
        out[1] = right[1];
        out[2] = right[2];
        out[3] = right[3];
    }
}

QPointF AnimeBezierSplit::curvePoint(const AnimeBezierCurve &curve, qreal t)
{
    if (curve.isLine) {
        return lerpPoint(curve.p[0], curve.p[3], t);
    }
    return splitCubic(curve.p, t, nullptr, nullptr);
}

bool AnimeBezierSplit::parsePath(const QPainterPath &path, QVector<AnimeBezierCurve> *out)
{
    out->clear();
    const int count = path.elementCount();
    if (count < 2 || path.elementAt(0).type != QPainterPath::MoveToElement) {
        return false;
    }
    QPointF current(path.elementAt(0).x, path.elementAt(0).y);
    int i = 1;
    while (i < count) {
        const QPainterPath::Element element = path.elementAt(i);
        if (element.type == QPainterPath::LineToElement) {
            AnimeBezierCurve curve;
            curve.isLine = true;
            curve.p[0] = current;
            curve.p[3] = QPointF(element.x, element.y);
            current = curve.p[3];
            out->append(curve);
            ++i;
            continue;
        }
        if (element.type != QPainterPath::CurveToElement || i + 2 >= count) {
            return false;   // a second MoveTo, or a truncated cubic
        }
        const QPainterPath::Element control2 = path.elementAt(i + 1);
        const QPainterPath::Element end = path.elementAt(i + 2);
        if (control2.type != QPainterPath::CurveToDataElement
            || end.type != QPainterPath::CurveToDataElement) {
            return false;
        }
        AnimeBezierCurve curve;
        curve.p[0] = current;
        curve.p[1] = QPointF(element.x, element.y);
        curve.p[2] = QPointF(control2.x, control2.y);
        curve.p[3] = QPointF(end.x, end.y);
        current = curve.p[3];
        out->append(curve);
        i += 3;
    }
    return !out->isEmpty();
}

AnimeBezierPathRun AnimeBezierSplit::buildRun(const QPainterPath &path,
                                              const AnimeBezierSplitOptions &options)
{
    AnimeBezierPathRun run;
    if (!parsePath(path, &run.curves)) {
        return run;
    }
    const qreal spacing = std::max<qreal>(0.25, options.targetSpacing);
    const int cap = std::max(2, options.maxSamplesPerCurve);
    for (int c = 0; c < run.curves.size(); ++c) {
        const AnimeBezierCurve &curve = run.curves[c];
        const qreal hullLength =
            curve.isLine
                ? QLineF(curve.p[0], curve.p[3]).length()
                : QLineF(curve.p[0], curve.p[1]).length()
                      + QLineF(curve.p[1], curve.p[2]).length()
                      + QLineF(curve.p[2], curve.p[3]).length();
        const int steps = std::max(2, std::min(cap, int(hullLength / spacing) + 2));
        for (int k = 0; k <= steps; ++k) {
            AnimeBezierPathSample sample;
            sample.curve = c;
            sample.t = qreal(k) / steps;
            sample.point = curvePoint(curve, sample.t);
            sample.length = run.samples.isEmpty()
                                ? 0.0
                                : run.samples.last().length
                                      + QLineF(run.samples.last().point, sample.point).length();
            run.samples.append(sample);
        }
    }
    return run;
}

AnimeBezierPathPosition AnimeBezierSplit::locate(const AnimeBezierPathRun &run, qreal w,
                                                 const AnimeBezierSplitOptions &options)
{
    LocateCost cost;
    cost.anchored = false;
    cost.w = w;
    cost.total = run.totalLength();
    return locateWithCost(run, w, cost, options);
}

AnimeBezierPathPosition AnimeBezierSplit::locate(const AnimeBezierPathRun &run, qreal w,
                                                 const QPointF &nearTo,
                                                 const AnimeBezierSplitOptions &options)
{
    LocateCost cost;
    cost.anchored = true;
    cost.nearTo = nearTo;
    cost.w = w;
    cost.total = run.totalLength();
    cost.branchPull = options.branchPull;
    return locateWithCost(run, w, cost, options);
}

bool AnimeBezierSplit::slice(const AnimeBezierPathRun &run,
                            const AnimeBezierPathPosition &from,
                            const AnimeBezierPathPosition &to,
                            QPainterPath *path,
                            QVector<QPointF> *polyline)
{
    if (!positionBefore(from, to) || !path) {
        return false;
    }
    path->moveTo(from.point);
    if (from.curve == to.curve) {
        appendSegment(*path, run.curves[from.curve], from.t, to.t);
    } else {
        appendSegment(*path, run.curves[from.curve], from.t, 1.0);
        for (int c = from.curve + 1; c < to.curve; ++c) {
            appendSegment(*path, run.curves[c], 0.0, 1.0);
        }
        appendSegment(*path, run.curves[to.curve], 0.0, to.t);
    }
    if (path->elementCount() < 2) {
        return false;
    }

    if (!polyline) {
        return true;
    }
    polyline->clear();
    polyline->append(from.point);
    for (const AnimeBezierPathSample &sample : run.samples) {
        AnimeBezierPathPosition at;
        at.curve = sample.curve;
        at.t = sample.t;
        if (!positionBefore(from, at) || !positionBefore(at, to)) {
            continue;
        }
        if (QLineF(polyline->last(), sample.point).length() > 1e-9) {
            polyline->append(sample.point);
        }
    }
    if (QLineF(polyline->last(), to.point).length() > 1e-9) {
        polyline->append(to.point);
    }
    return polyline->size() >= 2;
}
