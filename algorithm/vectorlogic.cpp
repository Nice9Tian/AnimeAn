#include "vectorlogic.h"

#include <QMap>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QQueue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal kEpsilon = 0.0001;
constexpr qreal kVectorRegionOverpaintWidth = 2.0;
constexpr qreal kGraphEpsilon = 0.001;

struct GraphVertex {
    QPointF point;
    QVector<int> neighbors;
};

qreal distanceSquared(const QPointF &a, const QPointF &b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

qreal clamp01(qreal value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }
    return value;
}

qreal distancePointToSegmentSquared(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon) {
        return distanceSquared(point, a);
    }

    const QPointF ap = point - a;
    const qreal t = clamp01((ap.x() * ab.x() + ap.y() * ab.y()) / len2);
    const QPointF closest = a + ab * t;
    return distanceSquared(point, closest);
}

qreal crossProduct(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF ab = b - a;
    const QPointF ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

bool segmentsIntersect(const QPointF &a0, const QPointF &a1, const QPointF &b0, const QPointF &b1)
{
    const qreal aLeft = std::min(a0.x(), a1.x());
    const qreal aRight = std::max(a0.x(), a1.x());
    const qreal aTop = std::min(a0.y(), a1.y());
    const qreal aBottom = std::max(a0.y(), a1.y());
    const qreal bLeft = std::min(b0.x(), b1.x());
    const qreal bRight = std::max(b0.x(), b1.x());
    const qreal bTop = std::min(b0.y(), b1.y());
    const qreal bBottom = std::max(b0.y(), b1.y());
    if (aRight < bLeft || bRight < aLeft || aBottom < bTop || bBottom < aTop) {
        return false;
    }

    const qreal c1 = crossProduct(a0, a1, b0);
    const qreal c2 = crossProduct(a0, a1, b1);
    const qreal c3 = crossProduct(b0, b1, a0);
    const qreal c4 = crossProduct(b0, b1, a1);
    return ((c1 <= 0 && c2 >= 0) || (c1 >= 0 && c2 <= 0)) &&
           ((c3 <= 0 && c4 >= 0) || (c3 >= 0 && c4 <= 0));
}

qreal segmentSegmentDistanceSquared(const QPointF &a0, const QPointF &a1, const QPointF &b0, const QPointF &b1)
{
    if (segmentsIntersect(a0, a1, b0, b1)) {
        return 0;
    }

    const qreal d0 = distancePointToSegmentSquared(a0, b0, b1);
    const qreal d1 = distancePointToSegmentSquared(a1, b0, b1);
    const qreal d2 = distancePointToSegmentSquared(b0, a0, a1);
    const qreal d3 = distancePointToSegmentSquared(b1, a0, a1);
    return std::min(std::min(d0, d1), std::min(d2, d3));
}

QVector<QPointF> densifySegment(const QPointF &from, const QPointF &to, qreal step)
{
    QVector<QPointF> points;
    const qreal length = QLineF(from, to).length();
    const int rawCount = std::max(1, static_cast<int>(std::ceil(length / std::max(step, qreal(1)))));
    const int count = std::min(rawCount, 64);
    points.reserve(count);
    for (int i = 1; i <= count; ++i) {
        const qreal t = static_cast<qreal>(i) / count;
        points.append(from + (to - from) * t);
    }
    return points;
}

qreal polygonSignedArea(const QVector<QPointF> &points)
{
    if (points.size() < 3) {
        return 0.0;
    }

    qreal area = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

QString vertexKey(const QPointF &point)
{
    const qint64 x = qRound64(point.x() / kGraphEpsilon);
    const qint64 y = qRound64(point.y() / kGraphEpsilon);
    return QString::number(x) + QLatin1Char(':') + QString::number(y);
}

bool segmentIntersectionParameters(const QLineF &a, const QLineF &b, qreal &ta, qreal &tb)
{
    const QPointF r = a.p2() - a.p1();
    const QPointF s = b.p2() - b.p1();
    const qreal denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) <= kEpsilon) {
        return false;
    }

    const QPointF delta = b.p1() - a.p1();
    ta = (delta.x() * s.y() - delta.y() * s.x()) / denom;
    tb = (delta.x() * r.y() - delta.y() * r.x()) / denom;
    return ta >= -kEpsilon && ta <= 1.0 + kEpsilon &&
           tb >= -kEpsilon && tb <= 1.0 + kEpsilon;
}

void appendUniqueParameter(QVector<qreal> &values, qreal value)
{
    value = clamp01(value);
    for (qreal existing : values) {
        if (std::abs(existing - value) <= kGraphEpsilon) {
            return;
        }
    }
    values.append(value);
}

void appendUniqueNeighbor(QVector<int> &neighbors, int neighbor)
{
    if (!neighbors.contains(neighbor)) {
        neighbors.append(neighbor);
    }
}
}

qreal AnimeVectorLogic::epsilon()
{
    return kEpsilon;
}

// ---------------------------------------------------------------------------
// AnimeOneEuroFilter
// ---------------------------------------------------------------------------

namespace {
constexpr qreal kEuroFallbackDt = 1.0 / 120.0; // assumed rate for broken timestamps
constexpr qreal kTwoPi = 6.28318530717958647692;

qreal dotProduct(const QPointF &a, const QPointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}
}

void AnimeOneEuroFilter::configure(qreal strength)
{
    m_strength = std::max<qreal>(0.0, std::min<qreal>(1.0, strength));
    // Tuning, all as functions of one knob:
    //  - minCutoff: 5 Hz at the lightest setting down to 0.9 Hz at the
    //    heaviest. Below ~0.7 Hz the line starts feeling like a rope even
    //    with compensation; above ~6 Hz the filter stops doing anything.
    //  - beta: how fast speed opens the filter. Speeds are px/s in document
    //    space, so a deliberate 600 px/s sweep adds ~9 Hz to the cutoff and
    //    effectively disables smoothing, which is the "fast = raw" rule.
    //  - compensation: the share of the theoretical lag distance added back.
    //    Full strength at the heavy end, none at the light end where the lag
    //    is imperceptible anyway.
    m_minCutoff = 5.0 - 4.1 * m_strength;
    m_beta = 0.015;
    // 0.6 Hz rather than the paper's 1 Hz: the velocity is differenced from
    // raw samples (rate-independent, see filter()), which doubles its noise
    // variance, and a noisy speed chatters the adaptive cutoff - measured as
    // jitter leaking back through at low speed. The slower estimate costs
    // ~0.1 s of cutoff adaptation, imperceptible next to the smoothing it buys.
    m_derivativeCutoff = 0.6;
    m_compensation = m_strength;
    reset();
}

void AnimeOneEuroFilter::reset()
{
    m_initialized = false;
    m_position = QPointF();
    m_lastRaw = QPointF();
    m_velocity = QPointF();
    m_lastMs = 0;
}

QPointF AnimeOneEuroFilter::lowpass(const QPointF &value, const QPointF &previous, qreal alpha)
{
    return previous + alpha * (value - previous);
}

qreal AnimeOneEuroFilter::alphaFor(qreal cutoff, qreal dt) const
{
    // alpha = 1 / (1 + tau/dt) with tau = 1/(2*pi*cutoff): the exact
    // discretization of a first-order lowpass.
    const qreal tau = 1.0 / (kTwoPi * cutoff);
    return 1.0 / (1.0 + tau / dt);
}

QPointF AnimeOneEuroFilter::filter(const QPointF &raw, qulonglong timestampMs)
{
    if (m_strength <= 0.0) {
        return raw;
    }
    if (!m_initialized) {
        m_initialized = true;
        m_position = raw;
        m_lastRaw = raw;
        m_velocity = QPointF();
        m_lastMs = timestampMs;
        return raw;
    }

    qreal dt = (timestampMs > m_lastMs)
                   ? qreal(timestampMs - m_lastMs) / 1000.0
                   : kEuroFallbackDt;
    // A stall (palm lift, hitch) must not turn into one giant step that
    // teleports the filter; treat it as a fresh-ish sample instead.
    dt = std::min<qreal>(dt, 0.1);
    m_lastMs = timestampMs;

    // Velocity estimate from consecutive RAW samples (the classic filter's
    // definition), itself lowpassed so the cutoff cannot chatter. Differencing
    // against the FILTERED position instead made the estimate depend on the
    // sample rate - the same 240 px/s hand on a 240 Hz tablet read 66% faster
    // than on a 60 Hz mouse, so the two got visibly different smoothing.
    const QPointF rawVelocity = (raw - m_lastRaw) / dt;
    m_lastRaw = raw;
    m_velocity = lowpass(rawVelocity, m_velocity, alphaFor(m_derivativeCutoff, dt));
    const qreal speed = std::hypot(m_velocity.x(), m_velocity.y());

    const qreal cutoff = m_minCutoff + m_beta * speed;
    m_position = lowpass(raw, m_position, alphaFor(cutoff, dt));

    // Phase-lag compensation. A first-order lowpass tracking a constant-
    // velocity input settles exactly tau behind it, so |v| * tau is the lag
    // distance. The push is applied ALONG THE FILTERED VELOCITY - that
    // direction is the smoothed travel direction, so cross-track jitter is
    // NOT reinstated (pushing toward the raw sample instead was measured to
    // hand back most of the noise the filter had just removed). The
    // magnitude is clamped to the gap's projection onto that direction: the
    // output can never pass the pen tip, and on a hard reversal the
    // projection goes negative and the push simply vanishes.
    if (m_compensation > 0.0 && speed > 1e-9) {
        const QPointF direction = m_velocity / speed;
        const QPointF gap = raw - m_position;
        const qreal ahead = dotProduct(gap, direction);
        if (ahead > 1e-9) {
            const qreal tau = 1.0 / (kTwoPi * cutoff);
            const qreal push = std::min(ahead, m_compensation * speed * tau);
            return m_position + direction * push;
        }
    }
    return m_position;
}

QVector<QPointF> AnimeVectorLogic::filteredPoints(const QVector<QPointF> &points)
{
    return points;
}

QPainterPath AnimeVectorLogic::makeSmoothedPath(const QVector<QPointF> &points, int smoothValue)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    QVector<QPointF> smoothed = points;
    path.moveTo(smoothed.first());
    if (smoothed.size() == 1) {
        path.lineTo(smoothed.first() + QPointF(0.01, 0.01));
        return path;
    }

    const int smoothPasses = smoothValue / 25;
    for (int pass = 0; pass < smoothPasses && smoothed.size() > 2; ++pass) {
        QVector<QPointF> next;
        next.reserve(smoothed.size());
        next.append(smoothed.first());
        for (int i = 1; i + 1 < smoothed.size(); ++i) {
            next.append((smoothed[i - 1] + smoothed[i] * 2.0 + smoothed[i + 1]) / 4.0);
        }
        next.append(smoothed.last());
        smoothed = next;
    }

    if (smoothed.size() == 2) {
        path.lineTo(smoothed.last());
        return path;
    }

    for (int i = 1; i < smoothed.size(); ++i) {
        const QPointF control = smoothed[i - 1];
        const QPointF end = (smoothed[i - 1] + smoothed[i]) / 2.0;
        path.quadTo(control, end);
    }

    path.lineTo(smoothed.last());
    return path;
}

QPainterPath AnimeVectorLogic::makePolylinePath(const QVector<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    path.moveTo(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first() + QPointF(0.01, 0.01));
        return path;
    }

    for (int i = 1; i < points.size(); ++i) {
        path.lineTo(points[i]);
    }
    return path;
}

// ---------------------------------------------------------------------------
// Hybrid polyline + Bezier stroke fitting
//
// Pure Bezier fitting bends hand-drawn straight runs into faint noodles and
// spends dozens of control points doing it; pure polylines turn arcs into
// facets. The pipeline here splits the trace at its FEATURES and gives each
// piece the primitive it is actually shaped like:
//
//   1. Gaussian denoise of x(t), y(t) (endpoints pinned - they are anchors).
//   2. Stride-window tangents: direction over a fixed arc window, so the
//      derivative reads the macro shape instead of pixel jitter.
//   3. Boundaries at curvature PEAKS (corners: the turn angle spikes) and at
//      curvature SIGN CHANGES (inflections: left bend becomes right bend).
//   4. Each piece is classified straight/curved by its max deviation from its
//      own chord.
//   5. Straight pieces emit one chord (every interior sample is within the
//      tolerance by definition, so RDP would keep nothing anyway). Curved
//      pieces get a least-squares cubic Bezier fit (Schneider), splitting at
//      the worst sample until the error fits.
//
// Joints: a corner is a DELIBERATE break - tangents on either side are free,
// so squares keep their corners. Every other joint (inflections, error
// splits, straight-to-curve transitions) shares one tangent, giving G1.
// ---------------------------------------------------------------------------

namespace {

struct FitParams {
    qreal gaussianSigma = 1.2;   // px; input denoise
    qreal strideWindow = 6.0;    // px; arc half-window for tangents
    qreal cornerAngleDeg = 35.0; // turn sharper than this is a corner
    qreal lineTolerance = 1.2;   // px; max chord deviation of a "straight" run
    qreal fitTolerance = 1.2;    // px; max Bezier fit error before splitting
    qreal inflectionNoise = 0.08; // min |normalized turn| to trust a sign
};

FitParams fitParamsFor(int smoothValue)
{
    const qreal s = std::max(0, std::min(100, smoothValue)) / 100.0;
    FitParams params;
    // In PIXELS of arc (fitPiece divides by the piece's sample spacing).
    params.gaussianSigma = 1.2 + 3.6 * s;
    params.strideWindow = 4.0 + 4.0 * s;
    params.lineTolerance = 0.8 + 1.0 * s;
    params.fitTolerance = 0.8 + 1.2 * s;
    return params;
}

QVector<QPointF> dedupePoints(const QVector<QPointF> &points)
{
    QVector<QPointF> out;
    out.reserve(points.size());
    for (const QPointF &p : points) {
        if (out.isEmpty() || QLineF(out.last(), p).length() > 0.01) {
            out.append(p);
        }
    }
    return out;
}

// 1D Gaussian over the point list, endpoints pinned: the first and last
// sample are where the user put the pen down and lifted it, and the fit must
// pass through them exactly (the same anchor rule the mapper follows).
QVector<QPointF> gaussianSmooth(const QVector<QPointF> &points, qreal sigma)
{
    if (points.size() < 3 || sigma <= 0.05) {
        return points;
    }
    const int radius = std::max(1, int(std::ceil(2.5 * sigma)));
    QVector<qreal> kernel(2 * radius + 1);
    qreal sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const qreal w = std::exp(-0.5 * (i * i) / (sigma * sigma));
        kernel[i + radius] = w;
        sum += w;
    }
    for (qreal &w : kernel) {
        w /= sum;
    }

    QVector<QPointF> out(points.size());
    out.first() = points.first();
    out.last() = points.last();
    for (int i = 1; i + 1 < points.size(); ++i) {
        QPointF acc(0.0, 0.0);
        for (int k = -radius; k <= radius; ++k) {
            // Reflect at the ends so the border samples keep full weight.
            // LOOPED, not two sequential ifs: when the kernel radius exceeds
            // the piece length one reflection can land past the OTHER end
            // (j = 2n-2-j goes negative for j > 2n-2), and the single-pass
            // version then indexed points[-1..-3] - an out-of-bounds read
            // that aborted Debug builds on a 3-point tick and folded heap
            // bytes into the curve in Release (measured: a 6 px straight
            // tick came back as an S swinging 7 px off the stroke).
            int j = i + k;
            while (j < 0 || j >= points.size()) {
                if (j < 0) {
                    j = -j;
                }
                if (j >= points.size()) {
                    j = 2 * points.size() - 2 - j;
                }
            }
            acc += kernel[k + radius] * points[j];
        }
        out[i] = acc;
    }
    return out;
}

// Direction of travel around index i, measured over an ARC window rather
// than adjacent samples: adjacent-sample tangents on a hand-drawn trace are
// dominated by sensor jitter.
QPointF strideTangent(const QVector<QPointF> &pts, const QVector<qreal> &arc, int i, qreal window)
{
    int back = i;
    while (back > 0 && arc[i] - arc[back] < window) {
        --back;
    }
    int fwd = i;
    while (fwd + 1 < pts.size() && arc[fwd] - arc[i] < window) {
        ++fwd;
    }
    const QPointF d = pts[fwd] - pts[back];
    const qreal len = std::hypot(d.x(), d.y());
    return len > 1e-9 ? d / len : QPointF(1.0, 0.0);
}

qreal crossZ(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

qreal dotP(const QPointF &a, const QPointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}

// Max perpendicular distance of the samples in [first, last] from the chord.
qreal chordDeviation(const QVector<QPointF> &pts, int first, int last)
{
    const QPointF a = pts[first];
    const QPointF b = pts[last];
    const QPointF ab = b - a;
    const qreal len = std::hypot(ab.x(), ab.y());
    qreal worst = 0.0;
    if (len < 1e-9) {
        for (int i = first + 1; i < last; ++i) {
            worst = std::max(worst, QLineF(a, pts[i]).length());
        }
        return worst;
    }
    for (int i = first + 1; i < last; ++i) {
        worst = std::max(worst, std::abs(crossZ(ab, pts[i] - a)) / len);
    }
    return worst;
}

QPointF bezierPoint(const QPointF *c, qreal t)
{
    const qreal u = 1.0 - t;
    return u * u * u * c[0] + 3.0 * u * u * t * c[1] + 3.0 * u * t * t * c[2] + t * t * t * c[3];
}

// --- Schneider least-squares cubic fit (Graphics Gems "FitCurve") ---------

void chordLengthParameterize(const QVector<QPointF> &pts, int first, int last, QVector<qreal> &u)
{
    u.resize(last - first + 1);
    u[0] = 0.0;
    for (int i = first + 1; i <= last; ++i) {
        u[i - first] = u[i - first - 1] + QLineF(pts[i - 1], pts[i]).length();
    }
    const qreal total = u.last();
    if (total > 1e-12) {
        for (qreal &value : u) {
            value /= total;
        }
    }
}

// One Newton-Raphson step moving u_i toward the parameter whose curve point
// is closest to the sample.
qreal refineParameter(const QPointF *bez, const QPointF &point, qreal u)
{
    const QPointF d1[3] = {3.0 * (bez[1] - bez[0]), 3.0 * (bez[2] - bez[1]), 3.0 * (bez[3] - bez[2])};
    const QPointF d2[2] = {2.0 * (d1[1] - d1[0]), 2.0 * (d1[2] - d1[1])};

    const QPointF q = bezierPoint(bez, u);
    const qreal v = 1.0 - u;
    const QPointF q1 = v * v * d1[0] + 2.0 * v * u * d1[1] + u * u * d1[2];
    const QPointF q2 = v * d2[0] + u * d2[1];

    const qreal numerator = dotP(q - point, q1);
    const qreal denominator = dotP(q1, q1) + dotP(q - point, q2);
    if (std::abs(denominator) < 1e-12) {
        return u;
    }
    return std::max<qreal>(0.0, std::min<qreal>(1.0, u - numerator / denominator));
}

// Least-squares placement of the two inner control points for FIXED end
// tangents tHat1/tHat2 (unit vectors, pointing INTO the segment).
void generateBezier(const QVector<QPointF> &pts, int first, int last,
                    const QVector<qreal> &u,
                    const QPointF &tHat1, const QPointF &tHat2,
                    QPointF *bez)
{
    const int n = last - first + 1;
    bez[0] = pts[first];
    bez[3] = pts[last];

    qreal c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
    for (int i = 0; i < n; ++i) {
        const qreal t = u[i];
        const qreal v = 1.0 - t;
        const QPointF a0 = tHat1 * (3.0 * v * v * t);
        const QPointF a1 = tHat2 * (3.0 * v * t * t);
        c00 += dotP(a0, a0);
        c01 += dotP(a0, a1);
        c11 += dotP(a1, a1);
        const QPointF tmp = pts[first + i]
                            - (pts[first] * (v * v * v + 3.0 * v * v * t)
                               + pts[last] * (t * t * t + 3.0 * v * t * t));
        x0 += dotP(a0, tmp);
        x1 += dotP(a1, tmp);
    }

    const qreal det = c00 * c11 - c01 * c01;
    qreal alpha1 = 0.0;
    qreal alpha2 = 0.0;
    if (std::abs(det) > 1e-12) {
        alpha1 = (c11 * x0 - c01 * x1) / det;
        alpha2 = (c00 * x1 - c01 * x0) / det;
    }

    // Degenerate, inverted OR RUNAWAY alphas fall back to the Wu/Barsky
    // heuristic (handles at a third of the chord). The upper bound matters as
    // much as the lower one: a nearly-collinear span leaves the normal matrix
    // near-singular but still past the 1e-12 det test, and the solve then
    // returns handles hundreds of chord lengths out. computeMaxError samples
    // the curve only at the u_i, so such a balloon can pass within tolerance
    // of every sample while swinging thousands of px between them (measured:
    // control points at (28774,-10127) on a 450x235 px stroke). Three chord
    // lengths comfortably covers every legitimate cubic - a handle longer
    // than that means the solve went singular, not that the curve needs it.
    const qreal segLength = QLineF(pts[first], pts[last]).length();
    const qreal epsilonAlpha = 1e-6 * segLength;
    const qreal maxAlpha = 3.0 * segLength;
    if (alpha1 < epsilonAlpha || alpha2 < epsilonAlpha
        || alpha1 > maxAlpha || alpha2 > maxAlpha) {
        alpha1 = alpha2 = segLength / 3.0;
    }
    bez[1] = bez[0] + tHat1 * alpha1;
    bez[2] = bez[3] + tHat2 * alpha2;
}

qreal computeMaxError(const QVector<QPointF> &pts, int first, int last,
                      const QPointF *bez, const QVector<qreal> &u, int *splitPoint)
{
    qreal maxDistance = 0.0;
    *splitPoint = (first + last) / 2;
    for (int i = first + 1; i < last; ++i) {
        const QPointF p = bezierPoint(bez, u[i - first]);
        const qreal distance = dotP(p - pts[i], p - pts[i]);
        if (distance > maxDistance) {
            maxDistance = distance;
            *splitPoint = i;
        }
    }
    return std::sqrt(maxDistance);
}

QPointF centerTangent(const QVector<QPointF> &pts, int center)
{
    const QPointF d = pts[std::min<int>(center + 1, pts.size() - 1)] - pts[std::max(center - 1, 0)];
    const qreal len = std::hypot(d.x(), d.y());
    return len > 1e-9 ? d / len : QPointF(1.0, 0.0);
}

void fitCubicRecursive(const QVector<QPointF> &pts, int first, int last,
                       QPointF tHat1, QPointF tHat2,
                       qreal tolerance, int depth, QPainterPath &path)
{
    // Two samples: nothing to fit.
    if (last - first < 2 || depth > 24) {
        const QPointF a = pts[first];
        const QPointF b = pts[last];
        path.cubicTo(a + (b - a) / 3.0, a + 2.0 * (b - a) / 3.0, b);
        return;
    }

    QVector<qreal> u;
    chordLengthParameterize(pts, first, last, u);
    QPointF bez[4];
    generateBezier(pts, first, last, u, tHat1, tHat2, bez);

    int splitPoint = 0;
    qreal error = computeMaxError(pts, first, last, bez, u, &splitPoint);

    // Try to rescue a near-miss by re-parameterizing before splitting: the
    // chord-length guess is often what is wrong, not the curve.
    if (error > tolerance && error < tolerance * 4.0) {
        for (int iteration = 0; iteration < 4 && error > tolerance; ++iteration) {
            for (int i = 1; i < last - first; ++i) {
                u[i] = refineParameter(bez, pts[first + i], u[i]);
            }
            generateBezier(pts, first, last, u, tHat1, tHat2, bez);
            error = computeMaxError(pts, first, last, bez, u, &splitPoint);
        }
    }

    if (error <= tolerance) {
        path.cubicTo(bez[1], bez[2], bez[3]);
        return;
    }

    // Split at the worst sample with a SHARED center tangent: the two halves
    // leave the split point in exactly opposite directions, so the joint is
    // G1 by construction.
    splitPoint = std::max(first + 1, std::min(last - 1, splitPoint));
    const QPointF tCenter = centerTangent(pts, splitPoint);
    // Schneider convention: an end tangent points INTO its segment, so the
    // left half ends with -tCenter and the right half starts with +tCenter.
    fitCubicRecursive(pts, first, splitPoint, tHat1, -tCenter, tolerance, depth + 1, path);
    fitCubicRecursive(pts, splitPoint, last, tCenter, tHat2, tolerance, depth + 1, path);
}

} // namespace

namespace {

QVector<qreal> arcLengths(const QVector<QPointF> &pts)
{
    QVector<qreal> arc(pts.size());
    if (pts.isEmpty()) {
        return arc;
    }
    arc[0] = 0.0;
    for (int i = 1; i < pts.size(); ++i) {
        arc[i] = arc[i - 1] + QLineF(pts[i - 1], pts[i]).length();
    }
    return arc;
}

// Signed stride-window turn angle per sample (radians).
QVector<qreal> turnAngles(const QVector<QPointF> &pts, const QVector<qreal> &arc, qreal window)
{
    const int n = pts.size();
    QVector<qreal> turn(n, 0.0);
    for (int i = 1; i + 1 < n; ++i) {
        int back = i;
        while (back > 0 && arc[i] - arc[back] < window) {
            --back;
        }
        int fwd = i;
        while (fwd + 1 < n && arc[fwd] - arc[i] < window) {
            ++fwd;
        }
        QPointF vIn = pts[i] - pts[back];
        QPointF vOut = pts[fwd] - pts[i];
        const qreal lenIn = std::hypot(vIn.x(), vIn.y());
        const qreal lenOut = std::hypot(vOut.x(), vOut.y());
        if (lenIn < 1e-9 || lenOut < 1e-9) {
            continue;
        }
        vIn /= lenIn;
        vOut /= lenOut;
        turn[i] = std::atan2(crossZ(vIn, vOut), dotP(vIn, vOut));
    }
    return turn;
}

// One corner-free piece: smooth it (endpoints pinned, so a corner endpoint
// stays exactly where it was detected), split at inflections, classify each
// span straight/curved, emit chords and Beziers with G1 handoff inside.
void fitPiece(const QVector<QPointF> &piece, const FitParams &params, QPainterPath &path)
{
    if (piece.size() < 2) {
        return;
    }
    if (piece.size() == 2) {
        path.lineTo(piece.last());
        return;
    }
    if (QLineF(piece.first(), piece.last()).length() < 1e-6) {
        // Coincident endpoints with real samples between them is a CLOSED
        // LOOP, not a degenerate segment - collapsing it to a zero-length
        // lineTo deleted the whole circle (a drawn O rendered as nothing
        // while stroke.points still claimed its full length). Split at the
        // sample farthest from the shared endpoint: both halves then have
        // distinct endpoints and fit normally, and the two joints inherit
        // G1 through the usual exit-tangent handoff inside each half.
        int farthest = piece.size() / 2;
        qreal best = -1.0;
        for (int i = 1; i + 1 < piece.size(); ++i) {
            const qreal distance = QLineF(piece.first(), piece[i]).length();
            if (distance > best) {
                best = distance;
                farthest = i;
            }
        }
        if (best < 1e-6) {
            return; // every sample coincides: nothing drawable
        }
        fitPiece(piece.mid(0, farthest + 1), params, path);
        fitPiece(piece.mid(farthest), params, path);
        return;
    }

    // The sigma budget is in PIXELS; the kernel walks SAMPLES. Convert with
    // the piece's mean sample spacing, otherwise the denoise strength rides
    // the pen speed: a fast pass at 8 px spacing was smoothed over 4x the arc
    // of a slow pass at 2 px spacing and visibly shrank tight arcs.
    qreal meanSpacing = 1.0;
    {
        qreal total = 0.0;
        for (int i = 1; i < piece.size(); ++i) {
            total += QLineF(piece[i - 1], piece[i]).length();
        }
        meanSpacing = std::max<qreal>(0.25, total / (piece.size() - 1));
    }
    const qreal sigmaSamples =
        std::min<qreal>(4.0, params.gaussianSigma / meanSpacing);
    const QVector<QPointF> pts = gaussianSmooth(piece, sigmaSamples);
    const QVector<qreal> arc = arcLengths(pts);
    const QVector<qreal> turn = turnAngles(pts, arc, params.strideWindow);
    const int n = pts.size();

    // Inflections: the signed turn crosses zero with real turning on both
    // sides (the hysteresis keeps straight runs, whose sign is pure noise,
    // from spraying phantom boundaries).
    QVector<int> boundaries;
    boundaries.append(0);
    {
        int lastSignedIndex = -1;
        qreal lastSign = 0.0;
        for (int i = 1; i + 1 < n; ++i) {
            if (std::abs(turn[i]) < params.inflectionNoise) {
                continue;
            }
            const qreal sign = turn[i] > 0.0 ? 1.0 : -1.0;
            if (lastSign != 0.0 && sign != lastSign && lastSignedIndex >= 0) {
                const int mid = (lastSignedIndex + i) / 2;
                if (arc[mid] - arc[boundaries.last()] > params.strideWindow
                    && arc[n - 1] - arc[mid] > params.strideWindow) {
                    boundaries.append(mid);
                }
            }
            lastSign = sign;
            lastSignedIndex = i;
        }
    }
    boundaries.append(n - 1);

    // Emit. Consecutive STRAIGHT spans are merged first: the inflection
    // detector reads noise signs on a straight run and can sprinkle phantom
    // boundaries there (measured: a straight stroke came back as 12 collinear
    // chords), and two adjacent chords whose UNION still passes the chord
    // test are by definition one line. This is RDP's criterion applied at
    // the span level. `exitTangent` carries the leave direction of the
    // previous emission so every joint inside a piece stays G1.
    QPointF exitTangent;
    bool haveExitTangent = false;
    int pendingLineStart = -1;
    auto flushLine = [&](int upTo) {
        if (pendingLineStart < 0) {
            return;
        }
        path.lineTo(pts[upTo]);
        const QPointF d = pts[upTo] - pts[pendingLineStart];
        const qreal len = std::hypot(d.x(), d.y());
        exitTangent = len > 1e-9 ? d / len : QPointF(1.0, 0.0);
        haveExitTangent = true;
        pendingLineStart = -1;
    };
    for (int span = 0; span + 1 < boundaries.size(); ++span) {
        const int first = boundaries[span];
        const int last = boundaries[span + 1];
        if (last <= first) {
            continue;
        }
        if (chordDeviation(pts, first, last) <= params.lineTolerance) {
            if (pendingLineStart >= 0
                && chordDeviation(pts, pendingLineStart, last) <= params.lineTolerance) {
                continue; // extends the pending chord; keep absorbing
            }
            flushLine(first);
            pendingLineStart = first;
            continue;
        }

        flushLine(first);
        QPointF tHat1 = haveExitTangent
                            ? exitTangent
                            : strideTangent(pts, arc, std::min(first + 1, last), params.strideWindow);
        const QPointF tHat2 = -strideTangent(pts, arc, std::max(last - 1, first), params.strideWindow);
        fitCubicRecursive(pts, first, last, tHat1, tHat2, params.fitTolerance, 0, path);
        exitTangent = -tHat2;
        haveExitTangent = true;
    }
    flushLine(boundaries.last());
}

} // namespace

QPainterPath AnimeVectorLogic::fitStrokePath(const QVector<QPointF> &points, int smoothValue)
{
    QPainterPath path;
    const QVector<QPointF> deduped = dedupePoints(points);
    if (deduped.isEmpty()) {
        return path;
    }
    path.moveTo(deduped.first());
    if (deduped.size() == 1) {
        path.lineTo(deduped.first() + QPointF(0.01, 0.01));
        return path;
    }
    if (deduped.size() == 2) {
        path.lineTo(deduped.last());
        return path;
    }

    const FitParams params = fitParamsFor(smoothValue);

    // CORNERS FIRST, on a lightly-smoothed copy. Running the full Gaussian
    // before corner detection was measured to round a 90-degree corner over
    // ~2.5 sigma of samples: the shoulder then failed the straight test on
    // both sides and the square came back as Beziers with 3-5 px corner
    // error. Detecting on light smoothing keeps the corner localized, and
    // smoothing PER PIECE afterwards pins the corner as a shared endpoint,
    // so it cannot move at all.
    const QVector<QPointF> light = gaussianSmooth(deduped, 0.8);
    const QVector<qreal> arc = arcLengths(light);
    const QVector<qreal> turn = turnAngles(light, arc, params.strideWindow);
    const int n = light.size();
    const qreal cornerThreshold = params.cornerAngleDeg * (kTwoPi * 0.5) / 180.0;

    QVector<int> corners;
    corners.append(0);
    for (int i = 1; i + 1 < n; ++i) {
        if (std::abs(turn[i]) < cornerThreshold) {
            continue;
        }
        bool localMax = true;
        for (int j = i - 1; j > 0 && arc[i] - arc[j] < params.strideWindow; --j) {
            if (std::abs(turn[j]) > std::abs(turn[i])) {
                localMax = false;
                break;
            }
        }
        for (int j = i + 1; j + 1 < n && arc[j] - arc[i] < params.strideWindow; ++j) {
            if (std::abs(turn[j]) >= std::abs(turn[i])) {
                localMax = false;
                break;
            }
        }
        if (localMax && arc[i] - arc[corners.last()] > params.strideWindow * 0.5) {
            corners.append(i);
        }
    }
    corners.append(n - 1);

    for (int piece = 0; piece + 1 < corners.size(); ++piece) {
        const int first = corners[piece];
        const int last = corners[piece + 1];
        if (last <= first) {
            continue;
        }
        fitPiece(deduped.mid(first, last - first + 1), params, path);
    }
    return path;
}

AnimeVectorStroke AnimeVectorLogic::makeStroke(const QVector<QPointF> &points,
                                               const QColor &color,
                                               qreal width,
                                               int id,
                                               bool filterInput,
                                               bool smoothPath,
                                               int smoothValue)
{
    AnimeVectorStroke stroke;
    stroke.id = id;
    stroke.points = filterInput ? filteredPoints(points) : points;
    stroke.lengths.clear();
    stroke.lengths.reserve(stroke.points.size());
    stroke.totalLength = 0.0;
    for (int i = 0; i < stroke.points.size(); ++i) {
        if (i > 0) {
            stroke.totalLength += QLineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }
    // smoothPath now means the HYBRID FIT: straight runs become chords,
    // curved runs become least-squares cubics, corners stay corners. The old
    // midpoint-quad smoother (makeSmoothedPath) remains only for callers that
    // ask for it explicitly.
    stroke.path = smoothPath ? fitStrokePath(stroke.points, smoothValue) : makePolylinePath(stroke.points);
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

AnimeVectorStroke AnimeVectorLogic::makeStrokeFromPath(const QPainterPath &path,
                                                       const QVector<QPointF> &points,
                                                       const QColor &color,
                                                       qreal width,
                                                       int id)
{
    AnimeVectorStroke stroke;
    stroke.id = id;
    stroke.points = points;
    stroke.lengths.clear();
    stroke.lengths.reserve(stroke.points.size());
    stroke.totalLength = 0.0;
    for (int i = 0; i < stroke.points.size(); ++i) {
        if (i > 0) {
            stroke.totalLength += QLineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }
    stroke.path = path;
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

bool AnimeVectorLogic::strokeHitsCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius)
{
    if (stroke.points.isEmpty()) {
        return false;
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    if (distanceSquared(stroke.points.first(), center) <= effectiveRadius2) {
        return true;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        if (distancePointToSegmentSquared(center, stroke.points[i - 1], stroke.points[i]) <= effectiveRadius2) {
            return true;
        }
    }
    return false;
}

bool AnimeVectorLogic::strokeHitsCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius)
{
    if (QLineF(from, to).length() <= kEpsilon) {
        return strokeHitsCircle(stroke, from, radius);
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    for (int i = 1; i < stroke.points.size(); ++i) {
        if (segmentSegmentDistanceSquared(stroke.points[i - 1], stroke.points[i], from, to) <= effectiveRadius2) {
            return true;
        }
    }
    return false;
}

QVector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius)
{
    QVector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    qreal eraseStart = 0.0;
    auto inside = [&](const QPointF &point) {
        return distanceSquared(point, center) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i - 1];
        const QPointF b = stroke.points[i];
        if (distancePointToSegmentSquared(center, a, b) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const QVector<QPointF> samples = densifySegment(a, b, std::max(qreal(1), effectiveRadius * 0.25));
        const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const qreal t = static_cast<qreal>(sampleIndex + 1) / samples.size();
            const qreal w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
            const bool sampleInside = inside(samples[sampleIndex]);
            if (sampleInside && !inErase) {
                inErase = true;
                eraseStart = w;
            } else if (!sampleInside && inErase) {
                inErase = false;
                eraseRanges.append(AnimeVectorRange{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(AnimeVectorRange{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius)
{
    if (QLineF(from, to).length() <= kEpsilon) {
        return keepRangesForCircle(stroke, from, radius);
    }

    QVector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return QVector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
    }

    const qreal effectiveRadius = radius + stroke.width * 0.5;
    const qreal effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    qreal eraseStart = 0.0;
    auto inside = [&](const QPointF &point) {
        return distancePointToSegmentSquared(point, from, to) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i - 1];
        const QPointF b = stroke.points[i];
        if (segmentSegmentDistanceSquared(a, b, from, to) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const QVector<QPointF> samples = densifySegment(a, b, std::max(qreal(1), effectiveRadius * 0.25));
        const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const qreal t = static_cast<qreal>(sampleIndex + 1) / samples.size();
            const qreal w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
            const bool sampleInside = inside(samples[sampleIndex]);
            if (sampleInside && !inErase) {
                inErase = true;
                eraseStart = w;
            } else if (!sampleInside && inErase) {
                inErase = false;
                eraseRanges.append(AnimeVectorRange{eraseStart, w});
            }
        }
    }

    if (inErase) {
        eraseRanges.append(AnimeVectorRange{eraseStart, 1.0});
    }

    return complementRanges(eraseRanges);
}

QVector<AnimeVectorRange> AnimeVectorLogic::complementRanges(const QVector<AnimeVectorRange> &eraseRanges)
{
    QVector<AnimeVectorRange> keepRanges;
    qreal last = 0.0;

    for (const AnimeVectorRange &range : eraseRanges) {
        const qreal first = clamp01(range.first);
        const qreal second = clamp01(range.second);
        if (second <= first) {
            continue;
        }
        if (first > last + kEpsilon) {
            keepRanges.append(AnimeVectorRange{last, first});
        }
        if (second > last) {
            last = second;
        }
    }

    if (last < 1.0 - kEpsilon) {
        keepRanges.append(AnimeVectorRange{last, 1.0});
    }

    return keepRanges;
}

QVector<AnimeStrokeCrossing> AnimeVectorLogic::strokeCrossings(const AnimeVectorStroke &stroke,
                                                               const QVector<AnimeVectorStroke> &walls,
                                                               qreal mergeTolerance)
{
    QVector<AnimeStrokeCrossing> crossings;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return crossings;
    }

    for (int i = 0; i + 1 < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i];
        const QPointF b = stroke.points[i + 1];
        const QLineF segment(a, b);
        const qreal segmentLength = segment.length();
        if (segmentLength <= kEpsilon) {
            continue;
        }
        // Arc at the start of this segment, from the prefix table the stroke
        // already carries, so a crossing's position is exact rather than
        // re-integrated.
        const qreal arcStart = (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;

        for (int w = 0; w < walls.size(); ++w) {
            const AnimeVectorStroke &wall = walls[w];
            if (wall.points.size() < 2 || wall.totalLength <= kEpsilon) {
                continue;
            }
            for (int k = 0; k + 1 < wall.points.size(); ++k) {
                const QLineF wallSegment(wall.points[k], wall.points[k + 1]);
                const qreal wallSegmentLength = wallSegment.length();
                if (wallSegmentLength <= kEpsilon) {
                    continue;
                }
                QPointF hit;
                if (segment.intersects(wallSegment, &hit) != QLineF::BoundedIntersection) {
                    continue;
                }
                AnimeStrokeCrossing crossing;
                const qreal along = std::min(QLineF(a, hit).length(), segmentLength);
                crossing.arc = clamp01((arcStart + along) / stroke.totalLength);
                crossing.wallIndex = w;
                const qreal wallArcStart = (k < wall.lengths.size()) ? wall.lengths[k] : 0.0;
                const qreal wallAlong =
                    std::min(QLineF(wall.points[k], hit).length(), wallSegmentLength);
                crossing.wallArc = clamp01((wallArcStart + wallAlong) / wall.totalLength);
                crossings.append(crossing);
            }
        }
    }

    std::sort(crossings.begin(), crossings.end(),
              [](const AnimeStrokeCrossing &lhs, const AnimeStrokeCrossing &rhs) {
                  return lhs.arc < rhs.arc;
              });
    // One crossing found on two adjoining segments (it sits on their shared
    // vertex) must not count twice, or a cut would collapse to nothing.
    QVector<AnimeStrokeCrossing> merged;
    for (const AnimeStrokeCrossing &crossing : crossings) {
        if (merged.isEmpty() || crossing.arc - merged.last().arc > mergeTolerance) {
            merged.append(crossing);
        }
    }
    return merged;
}

QVector<AnimeVectorStroke> AnimeVectorLogic::splitStrokeAt(const AnimeVectorStroke &stroke,
                                                           QVector<qreal> arcs,
                                                           int smoothValue,
                                                           qreal endTolerance)
{
    QVector<AnimeVectorStroke> pieces;
    std::sort(arcs.begin(), arcs.end());

    QVector<qreal> cuts;
    for (qreal arc : arcs) {
        const qreal value = clamp01(arc);
        if (value <= endTolerance || value >= 1.0 - endTolerance) {
            continue;   // already an endpoint: nothing to divide there
        }
        if (cuts.isEmpty() || value - cuts.last() > endTolerance) {
            cuts.append(value);
        }
    }
    if (cuts.isEmpty()) {
        pieces.append(stroke);
        return pieces;
    }

    qreal previous = 0.0;
    for (qreal cut : cuts) {
        pieces.append(subStroke(stroke, previous, cut, smoothValue));
        previous = cut;
    }
    pieces.append(subStroke(stroke, previous, 1.0, smoothValue));
    return pieces;
}

bool AnimeVectorLogic::cutSpanAt(const AnimeVectorStroke &stroke,
                                 const QVector<AnimeVectorStroke> &walls,
                                 const QPointF &pos,
                                 AnimeVectorRange *span)
{
    AnimeCutPlan plan;
    if (!span || !planCutAt(stroke, walls, pos, &plan)) {
        return false;
    }
    *span = plan.span;
    return true;
}

bool AnimeVectorLogic::planCutAt(const AnimeVectorStroke &stroke,
                                 const QVector<AnimeVectorStroke> &walls,
                                 const QPointF &pos,
                                 AnimeCutPlan *plan)
{
    if (!plan || stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return false;
    }

    // Where the click sits along the stroke, in the same normalised arc the
    // crossings use.
    qreal bestDistanceSq = std::numeric_limits<qreal>::max();
    qreal clickArc = 0.0;
    for (int i = 0; i + 1 < stroke.points.size(); ++i) {
        const QPointF a = stroke.points[i];
        const QPointF b = stroke.points[i + 1];
        const qreal dx = b.x() - a.x();
        const qreal dy = b.y() - a.y();
        const qreal lengthSq = dx * dx + dy * dy;
        qreal t = 0.0;
        if (lengthSq > kEpsilon) {
            t = ((pos.x() - a.x()) * dx + (pos.y() - a.y()) * dy) / lengthSq;
            t = std::min<qreal>(1.0, std::max<qreal>(0.0, t));
        }
        const qreal projX = a.x() + dx * t;
        const qreal projY = a.y() + dy * t;
        const qreal distanceSq = (pos.x() - projX) * (pos.x() - projX)
                                 + (pos.y() - projY) * (pos.y() - projY);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            const qreal arcStart = (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;
            clickArc = clamp01((arcStart + std::sqrt(lengthSq) * t) / stroke.totalLength);
        }
    }

    const QVector<AnimeStrokeCrossing> crossings = strokeCrossings(stroke, walls);

    // The nearest crossing on each SIDE of the click. Strictly opposite by
    // construction: one is below the click's arc, the other above it. A side
    // with no crossing keeps its default, which is that END of the stroke
    // (wallIndex -1 marks it as an end rather than a junction).
    AnimeStrokeCrossing low;
    low.arc = 0.0;
    AnimeStrokeCrossing high;
    high.arc = 1.0;
    for (const AnimeStrokeCrossing &crossing : crossings) {
        if (crossing.arc <= clickArc) {
            low = crossing;      // sorted, so the last one under the click wins
        } else {
            high = crossing;     // and the first one over it
            break;
        }
    }

    if (high.arc - low.arc <= kEpsilon) {
        return false;
    }
    plan->span.first = low.arc;
    plan->span.second = high.arc;
    plan->low = low;
    plan->high = high;
    return true;
}

AnimeVectorStroke AnimeVectorLogic::subStroke(const AnimeVectorStroke &stroke, qreal fromW, qreal toW, int smoothValue)
{
    // Pieces keep the source stroke's identity attributes. Dropping them
    // here silently untagged partially-erased strokes: a half-erased
    // "auto_mapped" stroke lost its property and was re-collected as pattern
    // by the next mapping run.
    const auto inherit = [&stroke](AnimeVectorStroke piece) {
        piece.property = stroke.property;
        piece.penStyle = stroke.penStyle;
        return piece;
    };

    const qreal fromLength = clamp01(fromW) * stroke.totalLength;
    const qreal toLength = clamp01(toW) * stroke.totalLength;
    QVector<QPointF> points;
    if (toLength <= fromLength + kEpsilon) {
        return inherit(makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, smoothValue));
    }

    points.append(pointAtLength(stroke, fromLength));
    for (int i = 1; i + 1 < stroke.points.size(); ++i) {
        if (stroke.lengths[i] > fromLength + kEpsilon &&
            stroke.lengths[i] < toLength - kEpsilon) {
            points.append(stroke.points[i]);
        }
    }
    points.append(pointAtLength(stroke, toLength));

    return inherit(makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, smoothValue));
}

qreal AnimeVectorLogic::displayStrokeWidth(qreal documentWidth, qreal zoom, qreal minScreenPx)
{
    // Zoomed out, width * zoom drops below one screen pixel and antialiasing
    // renders the line as a faint alpha smear - at 5% zoom a 3px stroke is a
    // 0.15px ghost the user simply cannot find. Flooring the ON-SCREEN width
    // keeps every stroke visible at any zoom.
    //
    // The floor is a SMOOTH maximum, max(s, t) ~ (s^6 + t^6)^(1/6) in screen
    // space: the rendered width is a differentiable function of zoom instead
    // of kinking at the threshold, and the price is nothing - at the floor
    // itself the width is 2^(1/6) = 1.12x the hard max, and by 3x the floor
    // the difference is 0.07%, so the true width at working zoom is intact.
    if (zoom <= 0.0 || minScreenPx <= 0.0 || documentWidth <= 0.0) {
        return documentWidth;
    }
    const qreal s = (documentWidth * zoom) / minScreenPx;   // screen widths, in floors
    const qreal smooth = std::pow(1.0 + std::pow(s, 6.0), 1.0 / 6.0);
    return smooth * minScreenPx / zoom;
}

QPointF AnimeVectorLogic::pointAtLength(const AnimeVectorStroke &stroke, qreal length)
{
    if (stroke.points.isEmpty()) {
        return QPointF();
    }
    if (length <= 0 || stroke.points.size() == 1) {
        return stroke.points.first();
    }
    if (length >= stroke.totalLength) {
        return stroke.points.last();
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        if (stroke.lengths[i] >= length) {
            const qreal segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
            if (segmentLength <= kEpsilon) {
                return stroke.points[i];
            }
            const qreal t = (length - stroke.lengths[i - 1]) / segmentLength;
            return stroke.points[i - 1] + (stroke.points[i] - stroke.points[i - 1]) * t;
        }
    }

    return stroke.points.last();
}

QVector<QLineF> AnimeVectorLogic::segmentsFromPath(const QPainterPath &path)
{
    QVector<QLineF> segments;
    const QList<QPolygonF> subpaths = path.toSubpathPolygons();
    for (const QPolygonF &polyline : subpaths) {
        for (int i = 1; i < polyline.size(); ++i) {
            const QPointF a = polyline[i - 1];
            const QPointF b = polyline[i];
            if (QLineF(a, b).length() > kGraphEpsilon) {
                segments.append(QLineF(a, b));
            }
        }
    }
    return segments;
}

QVector<AnimeVectorRegionFace> AnimeVectorLogic::computeVectorRegionFaces(const QVector<QLineF> &segments)
{
    QVector<AnimeVectorRegionFace> faces;
    if (segments.isEmpty()) {
        return faces;
    }

    QVector<QVector<qreal>> splitParameters;
    splitParameters.resize(segments.size());
    for (int i = 0; i < segments.size(); ++i) {
        splitParameters[i].append(0.0);
        splitParameters[i].append(1.0);
    }

    for (int i = 0; i < segments.size(); ++i) {
        for (int j = i + 1; j < segments.size(); ++j) {
            qreal ti = 0.0;
            qreal tj = 0.0;
            if (!segmentIntersectionParameters(segments[i], segments[j], ti, tj)) {
                continue;
            }
            appendUniqueParameter(splitParameters[i], ti);
            appendUniqueParameter(splitParameters[j], tj);
        }
    }

    QVector<GraphVertex> vertices;
    QMap<QString, int> vertexByKey;
    auto vertexIndex = [&](const QPointF &point) {
        const QString key = vertexKey(point);
        const auto it = vertexByKey.constFind(key);
        if (it != vertexByKey.constEnd()) {
            const int index = it.value();
            vertices[index].point = (vertices[index].point + point) * 0.5;
            return index;
        }

        GraphVertex vertex;
        vertex.point = point;
        const int index = vertices.size();
        vertices.append(vertex);
        vertexByKey.insert(key, index);
        return index;
    };

    for (int i = 0; i < segments.size(); ++i) {
        QVector<qreal> params = splitParameters[i];
        std::sort(params.begin(), params.end());
        for (int j = 1; j < params.size(); ++j) {
            if (params[j] - params[j - 1] <= kGraphEpsilon) {
                continue;
            }
            const QPointF delta = segments[i].p2() - segments[i].p1();
            const QPointF p0 = segments[i].p1() + delta * params[j - 1];
            const QPointF p1 = segments[i].p1() + delta * params[j];
            const int v0 = vertexIndex(p0);
            const int v1 = vertexIndex(p1);
            if (v0 == v1) {
                continue;
            }
            appendUniqueNeighbor(vertices[v0].neighbors, v1);
            appendUniqueNeighbor(vertices[v1].neighbors, v0);
        }
    }

    for (GraphVertex &vertex : vertices) {
        std::sort(vertex.neighbors.begin(), vertex.neighbors.end(), [&](int lhs, int rhs) {
            const QPointF dl = vertices[lhs].point - vertex.point;
            const QPointF dr = vertices[rhs].point - vertex.point;
            return std::atan2(dl.y(), dl.x()) < std::atan2(dr.y(), dr.x());
        });
    }

    QMap<QString, bool> visited;
    auto edgeKey = [](int from, int to) {
        return QString::number(from) + QLatin1Char('>') + QString::number(to);
    };

    for (int from = 0; from < vertices.size(); ++from) {
        for (int to : vertices[from].neighbors) {
            const QString firstKey = edgeKey(from, to);
            if (visited.value(firstKey, false)) {
                continue;
            }

            QVector<QPointF> facePoints;
            int currentFrom = from;
            int currentTo = to;
            bool closed = false;
            for (int guard = 0; guard < 10000; ++guard) {
                visited.insert(edgeKey(currentFrom, currentTo), true);
                facePoints.append(vertices[currentFrom].point);

                const QVector<int> &nextNeighbors = vertices[currentTo].neighbors;
                const int reverseIndex = nextNeighbors.indexOf(currentFrom);
                if (reverseIndex < 0 || nextNeighbors.isEmpty()) {
                    break;
                }

                const int nextIndex = (reverseIndex - 1 + nextNeighbors.size()) % nextNeighbors.size();
                const int nextTo = nextNeighbors[nextIndex];
                currentFrom = currentTo;
                currentTo = nextTo;
                if (currentFrom == from && currentTo == to) {
                    closed = true;
                    break;
                }
            }

            if (!closed || facePoints.size() < 3) {
                continue;
            }

            const qreal area = polygonSignedArea(facePoints);
            if (area <= kGraphEpsilon) {
                continue;
            }

            QPainterPath facePath;
            facePath.moveTo(facePoints.first());
            for (int i = 1; i < facePoints.size(); ++i) {
                facePath.lineTo(facePoints[i]);
            }
            facePath.closeSubpath();
            faces.append(AnimeVectorRegionFace{facePath.simplified(), area});
        }
    }

    return faces;
}

QPainterPath AnimeVectorLogic::vectorRegionPathAt(const QPointF &seed, const QVector<QLineF> &segments, const QRect &canvasRect)
{
    if (!canvasRect.contains(seed.toPoint())) {
        return QPainterPath();
    }

    const QVector<AnimeVectorRegionFace> faces = computeVectorRegionFaces(segments);
    const AnimeVectorRegionFace *bestFace = nullptr;
    qreal bestArea = std::numeric_limits<qreal>::max();
    for (const AnimeVectorRegionFace &face : faces) {
        const qreal area = std::abs(face.signedArea);
        if (area <= kGraphEpsilon ||
            area >= bestArea ||
            !face.path.contains(seed)) {
            continue;
        }
        bestFace = &face;
        bestArea = area;
    }

    if (!bestFace) {
        return QPainterPath();
    }

    QPainterPath canvas;
    canvas.addRect(canvasRect);

    QPainterPathStroker overpaintStroker;
    overpaintStroker.setWidth(kVectorRegionOverpaintWidth);
    overpaintStroker.setCapStyle(Qt::RoundCap);
    overpaintStroker.setJoinStyle(Qt::RoundJoin);
    return bestFace->path.united(overpaintStroker.createStroke(bestFace->path)).intersected(canvas).simplified();
}

QPainterPath AnimeVectorLogic::fillPathFromMask(const QPoint &seed, const QImage &boundary)
{
    QPainterPath path;
    if (boundary.isNull() || !boundary.rect().contains(seed)) {
        return path;
    }
    if (qGray(boundary.pixel(seed.x(), seed.y())) > 0) {
        return path;
    }

    QImage visited(boundary.size(), QImage::Format_Grayscale8);
    visited.fill(0);

    QQueue<QPoint> queue;
    queue.enqueue(seed);
    visited.setPixel(seed.x(), seed.y(), 255);

    bool touchesCanvasEdge = false;
    while (!queue.isEmpty()) {
        const QPoint p = queue.dequeue();
        if (p.x() == 0 || p.y() == 0 || p.x() == boundary.width() - 1 || p.y() == boundary.height() - 1) {
            touchesCanvasEdge = true;
        }

        const QPoint neighbors[4] = {
            QPoint(p.x() + 1, p.y()),
            QPoint(p.x() - 1, p.y()),
            QPoint(p.x(), p.y() + 1),
            QPoint(p.x(), p.y() - 1)
        };

        for (const QPoint &next : neighbors) {
            if (!boundary.rect().contains(next)) {
                continue;
            }
            if (qGray(visited.pixel(next.x(), next.y())) > 0 ||
                qGray(boundary.pixel(next.x(), next.y())) > 0) {
                continue;
            }
            visited.setPixel(next.x(), next.y(), 255);
            queue.enqueue(next);
        }
    }

    if (touchesCanvasEdge) {
        return QPainterPath();
    }

    for (int y = 0; y < visited.height(); ++y) {
        int runStart = -1;
        for (int x = 0; x < visited.width(); ++x) {
            const bool filled = qGray(visited.pixel(x, y)) > 0;
            if (filled && runStart < 0) {
                runStart = x;
            } else if (!filled && runStart >= 0) {
                path.addRect(QRectF(runStart, y, x - runStart, 1));
                runStart = -1;
            }
        }
        if (runStart >= 0) {
            path.addRect(QRectF(runStart, y, visited.width() - runStart, 1));
        }
    }

    return path.simplified();
}
