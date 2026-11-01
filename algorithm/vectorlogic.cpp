#include "vectorlogic.h"

#include "beziersplit.h"
#include "viewscale.h"


#include <algorithm>
#include <cmath>
#include <limits>

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/path.h"
#include "algorithm/core/pathbool.h"
#include "algorithm/core/string.h"

#include <cstdint>

using namespace AnimeCore;

namespace {
constexpr double kEpsilon = 0.0001;
// Lives in the header now: the fill clip has to leave room for this halo.
using AnimeVectorLogic::kVectorRegionOverpaintWidth;
constexpr double kGraphEpsilon = 0.001;
// Fill-side dangling-tip heal radius: well above any junction residual a cut
// can leave (fit tolerance), well below the distance at which two REAL line
// ends read as separate marks.
constexpr double kFillGapHealPx = 1.0;

struct GraphVertex {
    PointF point;
    Vector<int> neighbors;
};

double distanceSquared(const PointF &a, const PointF &b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

double clamp01(double value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }
    return value;
}

double distancePointToSegmentSquared(const PointF &point, const PointF &a, const PointF &b)
{
    const PointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon) {
        return distanceSquared(point, a);
    }

    const PointF ap = point - a;
    const double t = clamp01((ap.x() * ab.x() + ap.y() * ab.y()) / len2);
    const PointF closest = a + ab * t;
    return distanceSquared(point, closest);
}

double crossProduct(const PointF &a, const PointF &b, const PointF &c)
{
    const PointF ab = b - a;
    const PointF ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

bool segmentsIntersect(const PointF &a0, const PointF &a1, const PointF &b0, const PointF &b1)
{
    const double aLeft = std::min(a0.x(), a1.x());
    const double aRight = std::max(a0.x(), a1.x());
    const double aTop = std::min(a0.y(), a1.y());
    const double aBottom = std::max(a0.y(), a1.y());
    const double bLeft = std::min(b0.x(), b1.x());
    const double bRight = std::max(b0.x(), b1.x());
    const double bTop = std::min(b0.y(), b1.y());
    const double bBottom = std::max(b0.y(), b1.y());
    if (aRight < bLeft || bRight < aLeft || aBottom < bTop || bBottom < aTop) {
        return false;
    }

    const double c1 = crossProduct(a0, a1, b0);
    const double c2 = crossProduct(a0, a1, b1);
    const double c3 = crossProduct(b0, b1, a0);
    const double c4 = crossProduct(b0, b1, a1);
    return ((c1 <= 0 && c2 >= 0) || (c1 >= 0 && c2 <= 0)) &&
           ((c3 <= 0 && c4 >= 0) || (c3 >= 0 && c4 <= 0));
}

double segmentSegmentDistanceSquared(const PointF &a0, const PointF &a1, const PointF &b0, const PointF &b1)
{
    if (segmentsIntersect(a0, a1, b0, b1)) {
        return 0;
    }

    const double d0 = distancePointToSegmentSquared(a0, b0, b1);
    const double d1 = distancePointToSegmentSquared(a1, b0, b1);
    const double d2 = distancePointToSegmentSquared(b0, a0, a1);
    const double d3 = distancePointToSegmentSquared(b1, a0, a1);
    return std::min(std::min(d0, d1), std::min(d2, d3));
}

Vector<PointF> densifySegment(const PointF &from, const PointF &to, double step)
{
    Vector<PointF> points;
    const double length = LineF(from, to).length();
    const int rawCount = std::max(1, static_cast<int>(std::ceil(length / std::max(step, double(1)))));
    const int count = std::min(rawCount, 64);
    points.reserve(count);
    for (int i = 1; i <= count; ++i) {
        const double t = static_cast<double>(i) / count;
        points.append(from + (to - from) * t);
    }
    return points;
}

double polygonSignedArea(const Vector<PointF> &points)
{
    if (points.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const PointF &a = points[i];
        const PointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

String vertexKey(const PointF &point)
{
    const std::int64_t x = AnimeCore::round64(point.x() / kGraphEpsilon);
    const std::int64_t y = AnimeCore::round64(point.y() / kGraphEpsilon);
    return String::number(x) + Latin1Char(':') + String::number(y);
}

bool segmentIntersectionParameters(const LineF &a, const LineF &b, double &ta, double &tb)
{
    const PointF r = a.p2() - a.p1();
    const PointF s = b.p2() - b.p1();
    const double denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) <= kEpsilon) {
        return false;
    }

    const PointF delta = b.p1() - a.p1();
    ta = (delta.x() * s.y() - delta.y() * s.x()) / denom;
    tb = (delta.x() * r.y() - delta.y() * r.x()) / denom;
    return ta >= -kEpsilon && ta <= 1.0 + kEpsilon &&
           tb >= -kEpsilon && tb <= 1.0 + kEpsilon;
}

void appendUniqueParameter(Vector<double> &values, double value)
{
    value = clamp01(value);
    for (double existing : values) {
        if (std::abs(existing - value) <= kGraphEpsilon) {
            return;
        }
    }
    values.append(value);
}

void appendUniqueNeighbor(Vector<int> &neighbors, int neighbor)
{
    if (!neighbors.contains(neighbor)) {
        neighbors.append(neighbor);
    }
}
}

double AnimeVectorLogic::epsilon()
{
    return kEpsilon;
}

// ---------------------------------------------------------------------------
// AnimeOneEuroFilter
// ---------------------------------------------------------------------------

namespace {
constexpr double kEuroFallbackDt = 1.0 / 120.0; // assumed rate for broken timestamps
constexpr double kTwoPi = 6.28318530717958647692;

double dotProduct(const PointF &a, const PointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}
}

void AnimeOneEuroFilter::configure(double strength)
{
    m_strength = std::max<double>(0.0, std::min<double>(1.0, strength));
    // Tuning, all as functions of one knob:
    //  - minCutoff: 5 Hz at the lightest setting down to 0.9 Hz at the
    //    heaviest. Below ~0.7 Hz the line starts feeling like a rope even
    //    with compensation; above ~6 Hz the filter stops doing anything.
    //  - beta: how fast speed opens the filter. Speeds are px/s in SCREEN
    //    space (the caller feeds screen coordinates: hand speed is screen
    //    speed at any zoom), so a deliberate 600 px/s sweep adds ~9 Hz to
    //    the cutoff and effectively disables smoothing - "fast = raw".
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
    m_position = PointF();
    m_lastRaw = PointF();
    m_velocity = PointF();
    m_lastMs = 0;
}

PointF AnimeOneEuroFilter::lowpass(const PointF &value, const PointF &previous, double alpha)
{
    return previous + alpha * (value - previous);
}

double AnimeOneEuroFilter::alphaFor(double cutoff, double dt) const
{
    // alpha = 1 / (1 + tau/dt) with tau = 1/(2*pi*cutoff): the exact
    // discretization of a first-order lowpass.
    const double tau = 1.0 / (kTwoPi * cutoff);
    return 1.0 / (1.0 + tau / dt);
}

PointF AnimeOneEuroFilter::filter(const PointF &raw, std::uint64_t timestampMs)
{
    if (m_strength <= 0.0) {
        return raw;
    }
    if (!m_initialized) {
        m_initialized = true;
        m_position = raw;
        m_lastRaw = raw;
        m_velocity = PointF();
        m_lastMs = timestampMs;
        return raw;
    }

    double dt = (timestampMs > m_lastMs)
                   ? double(timestampMs - m_lastMs) / 1000.0
                   : kEuroFallbackDt;
    // A stall (palm lift, hitch) must not turn into one giant step that
    // teleports the filter; treat it as a fresh-ish sample instead.
    dt = std::min<double>(dt, 0.1);
    m_lastMs = timestampMs;

    // Velocity estimate from consecutive RAW samples (the classic filter's
    // definition), itself lowpassed so the cutoff cannot chatter. Differencing
    // against the FILTERED position instead made the estimate depend on the
    // sample rate - the same 240 px/s hand on a 240 Hz tablet read 66% faster
    // than on a 60 Hz mouse, so the two got visibly different smoothing.
    const PointF rawVelocity = (raw - m_lastRaw) / dt;
    m_lastRaw = raw;
    m_velocity = lowpass(rawVelocity, m_velocity, alphaFor(m_derivativeCutoff, dt));
    const double speed = std::hypot(m_velocity.x(), m_velocity.y());

    const double cutoff = m_minCutoff + m_beta * speed;
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
        const PointF direction = m_velocity / speed;
        const PointF gap = raw - m_position;
        const double ahead = dotProduct(gap, direction);
        if (ahead > 1e-9) {
            const double tau = 1.0 / (kTwoPi * cutoff);
            const double push = std::min(ahead, m_compensation * speed * tau);
            return m_position + direction * push;
        }
    }
    return m_position;
}

Vector<PointF> AnimeVectorLogic::filteredPoints(const Vector<PointF> &points)
{
    return points;
}

Path AnimeVectorLogic::makeSmoothedPath(const Vector<PointF> &points, int smoothValue)
{
    Path path;
    if (points.isEmpty()) {
        return path;
    }

    Vector<PointF> smoothed = points;
    path.moveTo(smoothed.first());
    if (smoothed.size() == 1) {
        path.lineTo(smoothed.first() + PointF(0.01, 0.01));
        return path;
    }

    const int smoothPasses = smoothValue / 25;
    for (int pass = 0; pass < smoothPasses && smoothed.size() > 2; ++pass) {
        Vector<PointF> next;
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
        const PointF control = smoothed[i - 1];
        const PointF end = (smoothed[i - 1] + smoothed[i]) / 2.0;
        path.quadTo(control, end);
    }

    path.lineTo(smoothed.last());
    return path;
}

Path AnimeVectorLogic::makePolylinePath(const Vector<PointF> &points)
{
    Path path;
    if (points.isEmpty()) {
        return path;
    }

    path.moveTo(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first() + PointF(0.01, 0.01));
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
    double gaussianSigma = 1.2;   // px; input denoise
    double strideWindow = 6.0;    // px; arc half-window for tangents
    double cornerAngleDeg = 35.0; // turn sharper than this is a corner
    double lineTolerance = 1.2;   // px; max chord deviation of a "straight" run
    double fitTolerance = 1.2;    // px; max Bezier fit error before splitting
    double inflectionNoise = 0.08; // min |normalized turn| to trust a sign
    // Live baking only: the straight-vs-curved decision is NOT valid on a
    // short slice (over a 24 px chunk the sagitta of any gentle curve is
    // under lineTolerance, and whole curves froze as chord chains). The
    // live fitter makes that call itself at drawing scale and sets this to
    // suppress the slice-local chord test.
    bool forceCurved = false;
    // Document px per screen px (see AnimeStrokeFitSettings::pixelScale);
    // carried here so inner px constants convert consistently.
    double pixelScale = 1.0;
};

// Document px per screen px, defensively clamped: zoom outside 1/4x..16x
// gains nothing for the budgets (matches appendPoint's capture clamp).
// The clamp range lives in the shared wheel, algorithm/viewscale.h, so this
// and the two view-side conversions cannot drift apart.
double fitPixelScale(const AnimeStrokeFitSettings &settings)
{
    return AnimeViewScale::clampPixelScale(settings.pixelScale);
}

FitParams fitParamsFor(const AnimeStrokeFitSettings &settings)
{
    const double s = std::max(0, std::min(100, settings.simplify)) / 100.0;
    const double c = std::max(0, std::min(100, settings.corner)) / 100.0;
    FitParams params;
    // SIMPLIFY drives denoise strength and the two tolerances. The floors are
    // 1.2 px rather than 0.8: below roughly a pixel the fit starts chasing
    // sensor noise instead of the drawing, and a straight line came back as
    // 61 collinear chords - worse on BOTH axes than a mid setting (16 nodes
    // at better fidelity), i.e. the old bottom end was strictly dominated.
    // In PIXELS of arc (fitPiece divides by the piece's sample spacing).
    params.gaussianSigma = 1.2 + 3.6 * s;
    params.lineTolerance = 1.2 + 0.8 * s;
    params.fitTolerance = 1.2 + 1.0 * s;
    // CORNER drives the angle threshold and the window the turn is measured
    // over. A wider window averages a turn out, so a corner-shy setting gets
    // both a steeper threshold and a longer window; they pull the same way.
    // The window here is the LONG-stroke budget: the fit entry points
    // additionally cap it by the stroke's own arc (shortStrokeStrideWindow
    // below), so a stroke drawn small on screen is classified by its shape
    // rather than sliced up by a window that dwarfs it.
    params.cornerAngleDeg = 55.0 - 35.0 * c;
    params.strideWindow = 8.0 - 4.0 * c;
    // All px budgets above are SCREEN px: tremor, the device's report rate
    // and what the eye can resolve do not change when the canvas is zoomed,
    // so the document-space budgets must. At 8x zoom a document-px sigma
    // over-smoothed 8x the visible detail; zoomed out it stopped denoising.
    // Angle thresholds are scale-free and stay put.
    const double scale = fitPixelScale(settings);
    params.gaussianSigma *= scale;
    params.lineTolerance *= scale;
    params.fitTolerance *= scale;
    params.strideWindow *= scale;
    params.pixelScale = scale;
    return params;
}

// NOTE on scale: an earlier draft rescaled EVERY px budget above by the
// stroke's arc length to help short strokes. Adversarial review measured it
// strictly worse: hand tremor does not shrink with stroke length, so a
// noise budget scaled below the tremor floor chases jitter (3-6x the nodes
// AND further from the intended line), sub-sample windows invent corners,
// and 3-sample spans slip through the Schneider solve as runaway cubics.
// What helps short strokes is narrower: gaussianSigma and lineTolerance are
// NOISE budgets and stay fixed. Two per-stroke budgets do adapt, each with
// a floor near the tremor scale: fitTolerance - the curve-follow budget,
// which also caps the smoothing displacement in fitPiece - scales down with
// the WHOLE stroke's arc, so a small curved drawing cannot be deformed by a
// long-stroke-sized allowance; and strideWindow, capped by the stroke's arc
// in shortStrokeStrideWindow below, so the corner question stays about the
// stroke's SHAPE rather than its on-screen size.
double shortStrokeFitTolerance(double fitTolerance, double arc, double pixelScale)
{
    // The reference length and the tremor floor are screen-sized quantities
    // too (a "short stroke" is short ON SCREEN).
    return std::max<double>(0.8 * pixelScale,
                           fitTolerance * std::min<double>(1.0, arc / (60.0 * pixelScale)));
}

// The corner/tangent window is a screen-px budget like the others, but the
// corner decision it feeds is a SHAPE question, and a fixed screen window
// stops asking it once the drawing is small on screen: the turn measured
// over an arc window w on a curve of radius r is ~w/r, so on a stroke drawn
// zoomed out (small screen radius, window converted by pixelScale into a
// large share of the whole stroke) smooth curvature itself crosses the
// corner threshold and the fit slices the stroke into chords (measured: a
// 30 doc-px-radius circle drawn at 1/4 zoom - a 7.5 px gesture on screen -
// came back as 3 chords with 84-degree joints; the same circle drawn at 1x
// fits as smooth cubics). Capping the window by the stroke's own arc makes
// the classification a SHAPE question again - "does the turn CONCENTRATE
// within this share of the stroke?" - which is invariant to where the zoom
// slider sat while drawing.
//
// The cap is the corner threshold's own share of a full turn, at 60%: over
// a window of arc * (threshold / 2*pi) a closed convex curve accumulates
// exactly the threshold, so 60% of that guarantees smooth closed geometry
// stays under it at EVERY corner setting, with the remaining 40% absorbing
// the sampling wiggle of a sparse zoomed-out capture (measured on a jittery
// 20-sample circle: geometric turn 22.5 degrees, readings up to ~30, i.e.
// noise eats most of the margin - a flat arc/12 cap left 7.5 degrees and
// the circle still polygonised). At the default 37.5-degree threshold this
// works out to arc/16, which keeps every deliberate polygon corner sharp: a
// square's side is a quarter of the arc, so the window is a quarter of a
// side and the corner still reads ~90 degrees. The floor is the tremor
// scale, per the note above: below ~2 screen px of arc a turn reading is
// hand jitter, not shape, and sub-sample windows invent corners - it also
// guarantees a genuine corner on a tiny stroke is still measured over real
// arc rather than a vanishing window.
double shortStrokeStrideWindow(double strideWindow, double arc, double pixelScale,
                              double cornerAngleDeg)
{
    const double thresholdShare = (cornerAngleDeg / 360.0) * 0.6;
    return std::min(strideWindow, std::max(2.0 * pixelScale, arc * thresholdShare));
}

Vector<PointF> dedupePoints(const Vector<PointF> &points)
{
    Vector<PointF> out;
    out.reserve(points.size());
    for (const PointF &p : points) {
        if (out.isEmpty() || LineF(out.last(), p).length() > 0.01) {
            out.append(p);
        }
    }
    return out;
}

// 1D Gaussian over the point list, endpoints pinned: the first and last
// sample are where the user put the pen down and lifted it, and the fit must
// pass through them exactly (the same anchor rule the mapper follows).
Vector<PointF> gaussianSmooth(const Vector<PointF> &points, double sigma)
{
    if (points.size() < 3 || sigma <= 0.05) {
        return points;
    }
    const int radius = std::max(1, int(std::ceil(2.5 * sigma)));
    Vector<double> kernel(2 * radius + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-0.5 * (i * i) / (sigma * sigma));
        kernel[i + radius] = w;
        sum += w;
    }
    for (double &w : kernel) {
        w /= sum;
    }

    Vector<PointF> out(points.size());
    out.first() = points.first();
    out.last() = points.last();
    for (int i = 1; i + 1 < points.size(); ++i) {
        PointF acc(0.0, 0.0);
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
PointF strideTangent(const Vector<PointF> &pts, const Vector<double> &arc, int i, double window)
{
    int back = i;
    while (back > 0 && arc[i] - arc[back] < window) {
        --back;
    }
    int fwd = i;
    while (fwd + 1 < pts.size() && arc[fwd] - arc[i] < window) {
        ++fwd;
    }
    const PointF d = pts[fwd] - pts[back];
    const double len = std::hypot(d.x(), d.y());
    return len > 1e-9 ? d / len : PointF(1.0, 0.0);
}

double crossZ(const PointF &a, const PointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

double dotP(const PointF &a, const PointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}

// Max perpendicular distance of the samples in [first, last] from the chord.
double chordDeviation(const Vector<PointF> &pts, int first, int last)
{
    const PointF a = pts[first];
    const PointF b = pts[last];
    const PointF ab = b - a;
    const double len = std::hypot(ab.x(), ab.y());
    double worst = 0.0;
    if (len < 1e-9) {
        for (int i = first + 1; i < last; ++i) {
            worst = std::max(worst, LineF(a, pts[i]).length());
        }
        return worst;
    }
    for (int i = first + 1; i < last; ++i) {
        worst = std::max(worst, std::abs(crossZ(ab, pts[i] - a)) / len);
    }
    return worst;
}

// --- Schneider least-squares cubic fit (Graphics Gems "FitCurve") ---------
// Cubic evaluation comes from the shared exact-bezier home,
// AnimeBezierSplit::evaluateCubic (algorithm/beziersplit.h).

void chordLengthParameterize(const Vector<PointF> &pts, int first, int last, Vector<double> &u)
{
    u.resize(last - first + 1);
    u[0] = 0.0;
    for (int i = first + 1; i <= last; ++i) {
        u[i - first] = u[i - first - 1] + LineF(pts[i - 1], pts[i]).length();
    }
    const double total = u.last();
    if (total > 1e-12) {
        for (double &value : u) {
            value /= total;
        }
    }
}

// One Newton-Raphson step moving u_i toward the parameter whose curve point
// is closest to the sample.
double refineParameter(const PointF *bez, const PointF &point, double u)
{
    const PointF d1[3] = {3.0 * (bez[1] - bez[0]), 3.0 * (bez[2] - bez[1]), 3.0 * (bez[3] - bez[2])};
    const PointF d2[2] = {2.0 * (d1[1] - d1[0]), 2.0 * (d1[2] - d1[1])};

    const PointF q = AnimeBezierSplit::evaluateCubic(bez, u);
    const double v = 1.0 - u;
    const PointF q1 = v * v * d1[0] + 2.0 * v * u * d1[1] + u * u * d1[2];
    const PointF q2 = v * d2[0] + u * d2[1];

    const double numerator = dotP(q - point, q1);
    const double denominator = dotP(q1, q1) + dotP(q - point, q2);
    if (std::abs(denominator) < 1e-12) {
        return u;
    }
    return std::max<double>(0.0, std::min<double>(1.0, u - numerator / denominator));
}

// Least-squares placement of the two inner control points for FIXED end
// tangents tHat1/tHat2 (unit vectors, pointing INTO the segment).
void generateBezier(const Vector<PointF> &pts, int first, int last,
                    const Vector<double> &u,
                    const PointF &tHat1, const PointF &tHat2,
                    PointF *bez)
{
    const int n = last - first + 1;
    bez[0] = pts[first];
    bez[3] = pts[last];

    double c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = u[i];
        const double v = 1.0 - t;
        const PointF a0 = tHat1 * (3.0 * v * v * t);
        const PointF a1 = tHat2 * (3.0 * v * t * t);
        c00 += dotP(a0, a0);
        c01 += dotP(a0, a1);
        c11 += dotP(a1, a1);
        const PointF tmp = pts[first + i]
                            - (pts[first] * (v * v * v + 3.0 * v * v * t)
                               + pts[last] * (t * t * t + 3.0 * v * t * t));
        x0 += dotP(a0, tmp);
        x1 += dotP(a1, tmp);
    }

    const double det = c00 * c11 - c01 * c01;
    double alpha1 = 0.0;
    double alpha2 = 0.0;
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
    const double segLength = LineF(pts[first], pts[last]).length();
    const double epsilonAlpha = 1e-6 * segLength;
    const double maxAlpha = 3.0 * segLength;
    if (alpha1 < epsilonAlpha || alpha2 < epsilonAlpha
        || alpha1 > maxAlpha || alpha2 > maxAlpha) {
        alpha1 = alpha2 = segLength / 3.0;
    }
    bez[1] = bez[0] + tHat1 * alpha1;
    bez[2] = bez[3] + tHat2 * alpha2;
}

double computeMaxError(const Vector<PointF> &pts, int first, int last,
                      const PointF *bez, const Vector<double> &u, int *splitPoint)
{
    double maxDistance = 0.0;
    *splitPoint = (first + last) / 2;
    for (int i = first + 1; i < last; ++i) {
        const PointF p = AnimeBezierSplit::evaluateCubic(bez, u[i - first]);
        const double distance = dotP(p - pts[i], p - pts[i]);
        if (distance > maxDistance) {
            maxDistance = distance;
            *splitPoint = i;
        }
    }
    return std::sqrt(maxDistance);
}

PointF centerTangent(const Vector<PointF> &pts, int center)
{
    const PointF d = pts[std::min<int>(center + 1, pts.size() - 1)] - pts[std::max(center - 1, 0)];
    const double len = std::hypot(d.x(), d.y());
    return len > 1e-9 ? d / len : PointF(1.0, 0.0);
}

void fitCubicRecursive(const Vector<PointF> &pts, int first, int last,
                       PointF tHat1, PointF tHat2,
                       double tolerance, int depth, Path &path)
{
    // Two samples: nothing to fit - the chord, elevated to a cubic by the
    // shared home (algorithm/beziersplit.h).
    if (last - first < 2 || depth > 24) {
        PointF chord[4];
        AnimeBezierSplit::lineToCubic(pts[first], pts[last], chord);
        path.cubicTo(chord[1], chord[2], chord[3]);
        return;
    }
    // A single interior sample cannot constrain the least-squares solve: it
    // is interpolated exactly, computeMaxError reads 0, the tolerance test
    // is vacuous, and with near-antiparallel end tangents the alphas balloon
    // just under the runaway guard (measured: a 4 px chord emitted with a
    // 24 px control hull that folded back on itself). Wu/Barsky handles
    // along the requested tangents keep the joint G1 and the hull bounded.
    if (last - first == 2) {
        const double alpha = LineF(pts[first], pts[last]).length() / 3.0;
        path.cubicTo(pts[first] + tHat1 * alpha, pts[last] + tHat2 * alpha, pts[last]);
        return;
    }

    Vector<double> u;
    chordLengthParameterize(pts, first, last, u);
    PointF bez[4];
    generateBezier(pts, first, last, u, tHat1, tHat2, bez);

    int splitPoint = 0;
    double error = computeMaxError(pts, first, last, bez, u, &splitPoint);

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
    const PointF tCenter = centerTangent(pts, splitPoint);
    // Schneider convention: an end tangent points INTO its segment, so the
    // left half ends with -tCenter and the right half starts with +tCenter.
    fitCubicRecursive(pts, first, splitPoint, tHat1, -tCenter, tolerance, depth + 1, path);
    fitCubicRecursive(pts, splitPoint, last, tCenter, tHat2, tolerance, depth + 1, path);
}

} // namespace

namespace {

Vector<double> arcLengths(const Vector<PointF> &pts)
{
    Vector<double> arc(pts.size());
    if (pts.isEmpty()) {
        return arc;
    }
    arc[0] = 0.0;
    for (int i = 1; i < pts.size(); ++i) {
        arc[i] = arc[i - 1] + LineF(pts[i - 1], pts[i]).length();
    }
    return arc;
}

// Signed stride-window turn angle per sample (radians).
Vector<double> turnAngles(const Vector<PointF> &pts, const Vector<double> &arc, double window)
{
    const int n = pts.size();
    Vector<double> turn(n, 0.0);
    for (int i = 1; i + 1 < n; ++i) {
        int back = i;
        while (back > 0 && arc[i] - arc[back] < window) {
            --back;
        }
        int fwd = i;
        while (fwd + 1 < n && arc[fwd] - arc[i] < window) {
            ++fwd;
        }
        // The walks stop at the first sample PAST the window. Under a sparse
        // capture (the device reports screen distances, so a zoomed-out
        // drawing arrives with few, far-apart document samples) that one
        // step can overshoot the window by most of a sample spacing, and the
        // turn is then measured over up to twice the arc it should be -
        // sparse input reads as "sharper" than the same drawing captured
        // densely, and smooth curves get called corners. Take whichever
        // neighbour lands NEAREST the window instead, never collapsing onto
        // i itself.
        if (back + 1 < i
            && (arc[i] - arc[back]) - window > window - (arc[i] - arc[back + 1])) {
            ++back;
        }
        if (fwd - 1 > i
            && (arc[fwd] - arc[i]) - window > window - (arc[fwd - 1] - arc[i])) {
            --fwd;
        }
        PointF vIn = pts[i] - pts[back];
        PointF vOut = pts[fwd] - pts[i];
        const double lenIn = std::hypot(vIn.x(), vIn.y());
        const double lenOut = std::hypot(vOut.x(), vOut.y());
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
// `forcedEntry` / `forcedExit` (unit tangents, pointing along the direction
// of travel) pin the piece's boundary tangents - the seams of the live
// fitter's frozen prefix stay G1 because the baked side and the live side
// are handed the SAME tangent.
void fitPiece(const Vector<PointF> &piece, const FitParams &params, Path &path,
              const PointF *forcedEntry = nullptr, const PointF *forcedExit = nullptr)
{
    if (piece.size() < 2) {
        return;
    }
    if (piece.size() == 2) {
        path.lineTo(piece.last());
        return;
    }
    if (LineF(piece.first(), piece.last()).length() < 1e-6) {
        // Coincident endpoints with real samples between them is a CLOSED
        // LOOP, not a degenerate segment - collapsing it to a zero-length
        // lineTo deleted the whole circle (a drawn O rendered as nothing
        // while stroke.points still claimed its full length). Split at the
        // sample farthest from the shared endpoint: both halves then have
        // distinct endpoints and fit normally, and the two joints inherit
        // G1 through the usual exit-tangent handoff inside each half.
        int farthest = piece.size() / 2;
        double best = -1.0;
        for (int i = 1; i + 1 < piece.size(); ++i) {
            const double distance = LineF(piece.first(), piece[i]).length();
            if (distance > best) {
                best = distance;
                farthest = i;
            }
        }
        if (best < 1e-6) {
            return; // every sample coincides: nothing drawable
        }
        // The two halves meet at the split sample, so they must LEAVE and
        // ENTER it along one shared tangent - each half estimating its own
        // (the old behaviour, both slots null) let the estimates disagree by
        // the sampling noise, and a small drawn O carried a 70-degree kink
        // at the far side of the loop. Same handoff rule as every other
        // joint; degenerate direction falls back to independent estimates.
        const PointF seamDir = piece[farthest + 1] - piece[farthest - 1];
        const double seamLen = std::hypot(seamDir.x(), seamDir.y());
        const PointF seamTangent = seamLen > 1e-9 ? seamDir / seamLen : PointF();
        const PointF *seam = seamLen > 1e-9 ? &seamTangent : nullptr;
        fitPiece(piece.mid(0, farthest + 1), params, path, forcedEntry, seam);
        fitPiece(piece.mid(farthest), params, path, seam, forcedExit);
        return;
    }

    // The sigma budget is in PIXELS; the kernel walks SAMPLES. Convert with
    // the piece's mean sample spacing, otherwise the denoise strength rides
    // the pen speed: a fast pass at 8 px spacing was smoothed over 4x the arc
    // of a slow pass at 2 px spacing and visibly shrank tight arcs.
    double pieceArc = 0.0;
    for (int i = 1; i < piece.size(); ++i) {
        pieceArc += LineF(piece[i - 1], piece[i]).length();
    }
    const double meanSpacing =
        std::max<double>(0.25 * params.pixelScale, pieceArc / (piece.size() - 1));
    const FitParams &local = params;

    // Denoise, but never let it MOVE the drawing further than the fit is
    // allowed to deviate: the smoothed polyline is what the Beziers are
    // anchored to, so an unbounded smoothing displacement became a visible
    // jump between the ink the user just drew and the committed curve - on
    // a small drawing the same px of displacement is a large share of the
    // shape (a 10 px hook moved 21% of its size, a 12 px circle lost 12% of
    // its roundness). Displacement grows ~ sigma^2, so a sqrt-rescale homes
    // in quickly; if the bound still fails after the rounds, fall back to
    // the raw piece - zero displacement always satisfies it. The sample cap
    // is 20 (not 4): with a fine capture spacing the px sigma needs many
    // samples, and the old cap silently weakened Simplify at high zoom.
    double sigmaSamples = std::min<double>(20.0, local.gaussianSigma / meanSpacing);
    Vector<PointF> pts = gaussianSmooth(piece, sigmaSamples);
    for (int round = 0; round <= 3; ++round) {
        double worst = 0.0;
        for (int i = 0; i < piece.size(); ++i) {
            worst = std::max(worst, LineF(piece[i], pts[i]).length());
        }
        if (worst <= local.fitTolerance || sigmaSamples <= 0.1) {
            break;
        }
        if (round == 3) {
            pts = piece;   // the bound is a promise, not a hope
            break;
        }
        sigmaSamples *= std::sqrt(local.fitTolerance / worst);
        pts = sigmaSamples > 0.05 ? gaussianSmooth(piece, sigmaSamples) : piece;
    }

    const Vector<double> arc = arcLengths(pts);
    const Vector<double> turn = turnAngles(pts, arc, local.strideWindow);
    const int n = pts.size();

    // Inflections: the signed turn crosses zero with real turning on both
    // sides (the hysteresis keeps straight runs, whose sign is pure noise,
    // from spraying phantom boundaries).
    Vector<int> boundaries;
    boundaries.append(0);
    {
        int lastSignedIndex = -1;
        double lastSign = 0.0;
        for (int i = 1; i + 1 < n; ++i) {
            if (std::abs(turn[i]) < params.inflectionNoise) {
                continue;
            }
            const double sign = turn[i] > 0.0 ? 1.0 : -1.0;
            if (lastSign != 0.0 && sign != lastSign && lastSignedIndex >= 0) {
                const int mid = (lastSignedIndex + i) / 2;
                if (arc[mid] - arc[boundaries.last()] > local.strideWindow
                    && arc[n - 1] - arc[mid] > local.strideWindow) {
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
    PointF exitTangent;
    bool haveExitTangent = false;
    if (forcedEntry) {
        exitTangent = *forcedEntry;
        haveExitTangent = true;
    }
    int pendingLineStart = -1;
    auto flushLine = [&](int upTo) {
        if (pendingLineStart < 0) {
            return;
        }
        path.lineTo(pts[upTo]);
        const PointF d = pts[upTo] - pts[pendingLineStart];
        const double len = std::hypot(d.x(), d.y());
        exitTangent = len > 1e-9 ? d / len : PointF(1.0, 0.0);
        haveExitTangent = true;
        pendingLineStart = -1;
    };
    for (int span = 0; span + 1 < boundaries.size(); ++span) {
        const int first = boundaries[span];
        const int last = boundaries[span + 1];
        if (last <= first) {
            continue;
        }
        if (!local.forceCurved && chordDeviation(pts, first, last) <= local.lineTolerance) {
            if (pendingLineStart >= 0
                && chordDeviation(pts, pendingLineStart, last) <= local.lineTolerance) {
                continue; // extends the pending chord; keep absorbing
            }
            flushLine(first);
            pendingLineStart = first;
            continue;
        }

        flushLine(first);
        PointF tHat1 = haveExitTangent
                            ? exitTangent
                            : strideTangent(pts, arc, std::min(first + 1, last), local.strideWindow);
        // Schneider convention: an end tangent points INTO its segment, so a
        // forced forward-travel exit tangent lands here negated.
        const bool lastSpan = (span + 2 == boundaries.size());
        const PointF tHat2 = (forcedExit && lastSpan)
                                  ? -*forcedExit
                                  : -strideTangent(pts, arc, std::max(last - 1, first), local.strideWindow);
        fitCubicRecursive(pts, first, last, tHat1, tHat2, local.fitTolerance, 0, path);
        exitTangent = -tHat2;
        haveExitTangent = true;
    }
    flushLine(boundaries.last());
}

} // namespace

namespace {

// The corner-detect + per-piece fitting pipeline over one deduped run,
// APPENDED to `path` (whose current position must be the run's first
// sample). Boundary tangents can be forced so the live fitter's frozen
// seams stay G1.
void fitRun(const Vector<PointF> &deduped, const FitParams &params, Path &path,
            const PointF *forcedEntry = nullptr, const PointF *forcedExit = nullptr)
{
    if (deduped.size() < 2) {
        return;
    }
    if (deduped.size() == 2) {
        path.lineTo(deduped.last());
        return;
    }

    // CORNERS FIRST, on a lightly-smoothed copy. Running the full Gaussian
    // before corner detection was measured to round a 90-degree corner over
    // ~2.5 sigma of samples: the shoulder then failed the straight test on
    // both sides and the square came back as Beziers with 3-5 px corner
    // error. Detecting on light smoothing keeps the corner localized, and
    // smoothing PER PIECE afterwards pins the corner as a shared endpoint,
    // so it cannot move at all. The light sigma is a PX budget converted by
    // the sample spacing - as a fixed sample count it stopped smoothing at
    // all under a fine (zoomed-in) capture and the detector saw raw tremor.
    double rawArc = 0.0;
    for (int i = 1; i < deduped.size(); ++i) {
        rawArc += LineF(deduped[i - 1], deduped[i]).length();
    }
    const double captureSpacing = std::max<double>(0.05, rawArc / (deduped.size() - 1));
    const double lightSigma = std::max<double>(
        0.5, std::min<double>(6.0, 1.6 * params.pixelScale / captureSpacing));
    const Vector<PointF> light = gaussianSmooth(deduped, lightSigma);
    const Vector<double> arc = arcLengths(light);
    const double cornerWindow = params.strideWindow;
    const Vector<double> turn = turnAngles(light, arc, cornerWindow);
    const int n = light.size();
    const double cornerThreshold = params.cornerAngleDeg * (kTwoPi * 0.5) / 180.0;

    Vector<int> corners;
    corners.append(0);
    for (int i = 1; i + 1 < n; ++i) {
        if (std::abs(turn[i]) < cornerThreshold) {
            continue;
        }
        bool localMax = true;
        for (int j = i - 1; j > 0 && arc[i] - arc[j] < cornerWindow; --j) {
            if (std::abs(turn[j]) > std::abs(turn[i])) {
                localMax = false;
                break;
            }
        }
        for (int j = i + 1; j + 1 < n && arc[j] - arc[i] < cornerWindow; ++j) {
            if (std::abs(turn[j]) >= std::abs(turn[i])) {
                localMax = false;
                break;
            }
        }
        if (localMax && arc[i] - arc[corners.last()] > cornerWindow * 0.5) {
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
        fitPiece(deduped.mid(first, last - first + 1), params, path,
                 piece == 0 ? forcedEntry : nullptr,
                 piece + 2 == corners.size() ? forcedExit : nullptr);
    }
}

} // namespace

Path AnimeVectorLogic::fitStrokePath(const Vector<PointF> &points,
                                             const AnimeStrokeFitSettings &settings)
{
    Path path;
    const Vector<PointF> deduped = dedupePoints(points);
    if (deduped.isEmpty()) {
        return path;
    }
    path.moveTo(deduped.first());
    if (deduped.size() == 1) {
        path.lineTo(deduped.first() + PointF(0.01, 0.01));
        return path;
    }

    FitParams params = fitParamsFor(settings);
    double rawArc = 0.0;
    for (int i = 1; i < deduped.size(); ++i) {
        rawArc += LineF(deduped[i - 1], deduped[i]).length();
    }
    params.fitTolerance =
        shortStrokeFitTolerance(params.fitTolerance, rawArc, params.pixelScale);
    params.strideWindow = shortStrokeStrideWindow(params.strideWindow, rawArc,
                                                  params.pixelScale, params.cornerAngleDeg);
    fitRun(deduped, params, path);
    return path;
}

namespace {

// Re-emit elements of `src` into `dst`, starting at raw element index
// `first` (which must sit on an element boundary).
void appendPathElements(Path &dst, const Path &src, int first)
{
    int i = first;
    while (i < src.elementCount()) {
        const Path::Element e = src.elementAt(i);
        if (e.type == Path::MoveToElement) {
            dst.moveTo(e.x, e.y);
            ++i;
        } else if (e.type == Path::LineToElement) {
            dst.lineTo(e.x, e.y);
            ++i;
        } else {
            dst.cubicTo(PointF(e.x, e.y),
                        PointF(src.elementAt(i + 1).x, src.elementAt(i + 1).y),
                        PointF(src.elementAt(i + 2).x, src.elementAt(i + 2).y));
            i += 3;
        }
    }
}

// Dense samples of `src` from raw element index `first` on.
Vector<PointF> flattenPathFrom(const Path &src, int first)
{
    Vector<PointF> out;
    PointF current;
    int i = 0;
    while (i < src.elementCount()) {
        const Path::Element e = src.elementAt(i);
        if (e.type == Path::MoveToElement) {
            current = PointF(e.x, e.y);
            if (i >= first) {
                out.append(current);
            }
            ++i;
        } else if (e.type == Path::LineToElement) {
            const PointF to(e.x, e.y);
            if (i >= first) {
                if (out.isEmpty()) {
                    out.append(current);
                }
                for (int k = 1; k <= 8; ++k) {
                    out.append(current + (to - current) * (double(k) / 8.0));
                }
            }
            current = to;
            ++i;
        } else {
            const PointF bez[4] = {current,
                                    PointF(e.x, e.y),
                                    PointF(src.elementAt(i + 1).x, src.elementAt(i + 1).y),
                                    PointF(src.elementAt(i + 2).x, src.elementAt(i + 2).y)};
            if (i >= first) {
                if (out.isEmpty()) {
                    out.append(current);
                }
                for (int k = 1; k <= 16; ++k) {
                    out.append(AnimeBezierSplit::evaluateCubic(bez, double(k) / 16.0));
                }
            }
            current = bez[3];
            i += 3;
        }
    }
    return out;
}

double worstDistanceToPolyline(const Vector<PointF> &pts, const Vector<PointF> &poly)
{
    double worst = 0.0;
    for (const PointF &p : pts) {
        double best = std::numeric_limits<double>::max();
        for (int i = 1; i < poly.size(); ++i) {
            const PointF a = poly[i - 1];
            const PointF ab = poly[i] - a;
            const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double t = 0.0;
            if (len2 > 1e-12) {
                t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
                t = std::min<double>(1.0, std::max<double>(0.0, t));
            }
            const PointF q = a + ab * t;
            best = std::min(best,
                            (p.x() - q.x()) * (p.x() - q.x()) + (p.y() - q.y()) * (p.y() - q.y()));
        }
        worst = std::max(worst, std::sqrt(best));
        if (worst > 1e6) {
            break;
        }
    }
    return worst;
}

} // namespace

Path AnimeVectorLogic::liveFitStrokePath(AnimeLiveFitState &state,
                                                 const Vector<PointF> &points,
                                                 const AnimeStrokeFitSettings &settings,
                                                 bool finalTail)
{
    // Ink this far behind the pen can no longer be touched by anything the
    // pen does (double the widest smoothing kernel / tangent / corner
    // window), so it is baked once and cut off from recomputation - the
    // frozen prefix is bitwise stable, which is what stops the already-drawn
    // path from trembling while the user keeps drawing.
    //
    // Baking is MODAL, because the straight-vs-curved decision is only valid
    // at drawing scale - over one bake chunk the sagitta of any gentle curve
    // is below line tolerance, and a chunk-local test froze whole arcs as
    // chord chains with visible kinks. In STRAIGHT mode one open chord is
    // EXTENDED (never re-emitted) while the whole run stays within tolerance
    // of it - the same unbounded-union test the offline fit's chord merging
    // uses. In CURVED mode chunks bake as Beziers regardless of their own
    // sagitta, seamed G1 by shared forced tangents.
    // Screen px, like every other budget: what the pen can still influence
    // is a hand-and-eye distance, not a canvas one. The bake chunk is large
    // because the tail previews as a polyline anyway (append-only, cheap)
    // and longer bakes emit fewer, better-conditioned curve elements. The
    // straight-mode probe windows are ABSOLUTE, not chunk-relative, so
    // changing the chunk cannot change how modes classify the drawing.
    const double liveScale = fitPixelScale(settings);
    const double kFreezeMargin = 40.0 * liveScale;
    const double kFreezeChunk = 96.0 * liveScale;
    const double kStraightProbe = 96.0 * liveScale;
    const double kStraightContext = 76.8 * liveScale;
    const double kRunCap = 300.0 * liveScale;        // consolidation lookback
    const double kInvisibleBudget = 0.35 * liveScale; // max on-screen change

    // Retroactive edits of the prefix (axis snap rewriting captured points)
    // invalidate the frozen path: verify the boundary sample is still the
    // one it was frozen against, and start over if not.
    if (state.frozenSamples > 0
        && (state.frozenSamples >= points.size()
            || LineF(points[state.frozenSamples], state.boundaryPoint).length() > 1e-9)) {
        state = AnimeLiveFitState();
    }
    // The open-run and open-chord cursors index this same buffer, but they
    // need no check of their own: both are only ever assigned frozenSamples,
    // so they stay <= it, and the test above already retires the state
    // whenever frozenSamples leaves the buffer. The run-slice write further
    // down is additionally gated on runStartSample < frozenSamples.
    //
    // What this tripwire does NOT catch: a caller that rewrites samples
    // BEFORE the boundary while leaving points[frozenSamples] where it was
    // keeps a frozen prefix that no longer matches its samples. It is a
    // single-sample identity test, not a checksum. Callers that rewrite the
    // buffer wholesale (the pen's hold-still straight line) must retire the
    // state themselves rather than rely on being noticed here.

    if (points.size() < 2) {
        state = AnimeLiveFitState();
        return fitStrokePath(points, settings);
    }

    Vector<double> arc(points.size(), 0.0);
    for (int i = 1; i < points.size(); ++i) {
        arc[i] = arc[i - 1] + LineF(points[i - 1], points[i]).length();
    }
    FitParams params = fitParamsFor(settings);
    params.fitTolerance =
        shortStrokeFitTolerance(params.fitTolerance, arc.last(), params.pixelScale);
    // Strokes long enough to bake (kFreezeMargin + kFreezeChunk of arc) are
    // always past the point where the cap binds, so the frozen prefix never
    // sees a mid-stroke window change; only the short-stroke release fit -
    // the whole stroke as one tail - is affected, and it matches what the
    // offline fitStrokePath would do with the same points.
    params.strideWindow = shortStrokeStrideWindow(params.strideWindow, arc.last(),
                                                  params.pixelScale, params.cornerAngleDeg);

    while (arc.last() - arc[state.frozenSamples] > kFreezeMargin + kFreezeChunk) {
        // The bake boundary is nudged to the FLATTEST sample of its window,
        // so a seam never sits on a corner (a forced tangent there would
        // smear the turn).
        const double target = arc.last() - kFreezeMargin;
        int lo = state.frozenSamples + 1;
        while (lo + 1 < points.size() - 1 && arc[lo] < target - kFreezeChunk * 0.5) {
            ++lo;
        }
        int hi = lo;
        while (hi + 1 < points.size() - 1 && arc[hi + 1] < target + kFreezeChunk * 0.5) {
            ++hi;
        }
        int boundary = -1;
        double flattest = std::numeric_limits<double>::max();
        for (int i = lo; i <= hi; ++i) {
            if (i <= state.frozenSamples || i + 1 >= points.size()) {
                continue;
            }
            const PointF in = points[i] - points[i - 1];
            const PointF out = points[i + 1] - points[i];
            const double turnHere = std::abs(std::atan2(crossZ(in, out), dotP(in, out)));
            if (turnHere < flattest) {
                flattest = turnHere;
                boundary = i;
            }
        }
        if (boundary <= state.frozenSamples) {
            break;   // no room to advance
        }
        if (state.frozenPath.elementCount() == 0) {
            state.frozenPath.moveTo(points.first());
        }

        // A denoised endpoint for straight-mode seams: the offline fit
        // chords the SMOOTHED polyline, and pinning seams to raw tremor made
        // the pivot cap slice a ruler-straight drag into many chords.
        const auto smoothedAt = [&points](int i) {
            PointF sum(0.0, 0.0);
            int count = 0;
            for (int k = std::max(0, i - 5); k <= std::min(int(points.size()) - 1, i + 5); ++k) {
                sum += points[k];
                ++count;
            }
            return sum / double(count);
        };
        // The polyline slice from the frozen boundary, pinned to the seam so
        // every bake and tail fit starts exactly where the frozen path ends.
        const auto sliceFrom = [&points, &state](int last) {
            Vector<PointF> out =
                points.mid(state.frozenSamples, last - state.frozenSamples + 1);
            if (state.hasSeam && !out.isEmpty()) {
                out[0] = state.seamPoint;
            }
            return dedupePoints(out);
        };
        // A fold doubles back over its own ink with almost no PERPENDICULAR
        // deviation, so chord tests are blind to it - the straight-line
        // distance vs arc-length ratio is not (a retrace's chord is far
        // shorter than its arc). Guards both chord extension and opening;
        // failing it routes the chunk to the curved bake, whose corner
        // detection splits the fold apex properly.
        const auto foldFree = [&points, &arc](int from, int to) {
            const double span = arc[to] - arc[from];
            return span <= 1e-9
                   || LineF(points[from], points[to]).length() >= 0.9 * span;
        };

        if (state.chordStartSample >= 0) {
            // STRAIGHT mode. Extend the open chord while the WHOLE run still
            // passes the union test (with slack: the offline merge tests
            // denoised samples, this one sees raw tremor) and keeps moving
            // FORWARD. Extending pivots the chord around its anchor; the
            // pivot is CAPPED - if the old seam would swing further than a
            // fraction of a pixel, the chord closes there and a nearly
            // collinear successor opens, so settled ink never visibly moves.
            const PointF newSeam = smoothedAt(boundary);
            PointF newDir = newSeam - state.chordAnchor;
            const double newLen = std::hypot(newDir.x(), newDir.y());
            double pivot = 0.0;
            if (newLen > 1e-9) {
                newDir /= newLen;
                pivot = std::abs(crossZ(newDir, state.seamPoint - state.chordAnchor));
            }
            // The fold test is per CHUNK, not per run: over the whole run a
            // short return leg is diluted below the ratio threshold and the
            // apex of a retrace was swallowed proportionally to the run
            // length (measured 18px on a 400px out-leg).
            if (foldFree(state.frozenSamples, boundary)
                && chordDeviation(points, state.chordStartSample, boundary)
                       <= params.lineTolerance * 1.25) {
                if (pivot > 0.45 * liveScale) {
                    // Still straight, but extending would move settled ink:
                    // close here and continue with a fresh collinear chord.
                    state.frozenPath.lineTo(state.seamPoint);
                    state.chordAnchor = state.seamPoint;
                    state.chordStartSample = state.frozenSamples;
                }
                state.frozenSamples = boundary;
                state.boundaryPoint = points[boundary];
                state.seamPoint = newSeam;
                state.hasSeam = true;
                continue;   // the open chord is implicit, nothing re-emitted
            }
            // The run stopped being straight: close the chord where it last
            // held and switch to curved baking for this chunk. The curve
            // ENTERS along the chord's own direction, so the seam is G1 by
            // construction rather than within tangent-estimation noise.
            state.frozenPath.lineTo(state.seamPoint);
            const PointF closedDir = state.seamPoint - state.chordAnchor;
            const double closedLen = std::hypot(closedDir.x(), closedDir.y());
            state.entryTangent =
                closedLen > 1e-9
                    ? closedDir / closedLen
                    : strideTangent(points, arc, state.frozenSamples, params.strideWindow);
            state.hasEntryTangent = true;
            state.chordStartSample = -1;
        } else {
            // CURVED (or initial) mode. Open a chord only when a LONG recent
            // window is straight - a single chunk cannot tell a gentle curve
            // from tremor (their deviations are the same size), two can. The
            // window is allowed to look back PAST the frozen boundary: it is
            // a classifier over the raw samples, and clamping it to the last
            // bake made the decision depend on event cadence instead of the
            // drawing. The chord must also leave along the frozen curve's
            // exit tangent, or the seam would kink where the curve ends.
            int probeStart = boundary;
            while (probeStart > 0 && arc[boundary] - arc[probeStart - 1] < kStraightProbe) {
                --probeStart;
            }
            const bool enoughContext =
                state.frozenSamples == 0
                || arc[boundary] - arc[probeStart] >= kStraightContext;
            const PointF openDir = points[boundary] - points[state.frozenSamples];
            const double openLen = std::hypot(openDir.x(), openDir.y());
            const bool tangentAligned =
                !state.hasEntryTangent
                || (openLen > 1e-9
                    && dotP(openDir / openLen, state.entryTangent)
                           > std::cos(5.0 * (kTwoPi * 0.5) / 180.0));
            if (enoughContext && tangentAligned
                && foldFree(probeStart, boundary)
                && chordDeviation(points, probeStart, boundary) <= params.lineTolerance) {
                state.chordStartSample = state.frozenSamples;
                state.chordAnchor = state.hasSeam ? state.seamPoint : points[state.frozenSamples];
                state.frozenSamples = boundary;
                state.boundaryPoint = points[boundary];
                state.seamPoint = smoothedAt(boundary);
                state.hasSeam = true;
                state.runStartSample = -1;   // a chord ends the curved run
                continue;
            }
        }

        // Curved bake of [frozenSamples .. boundary].
        const PointF tangent = strideTangent(points, arc, boundary, params.strideWindow);
        FitParams bakeParams = params;
        bakeParams.forceCurved = true;

        if (state.runStartSample < 0) {
            state.runStartSample = state.frozenSamples;
            state.runStartElement = state.frozenPath.elementCount();
            state.runStartPoint = state.hasSeam ? state.seamPoint : points[state.frozenSamples];
            state.runEntryTangent = state.entryTangent;
            state.hasRunEntryTangent = state.hasEntryTangent;
        }

        // Consolidation: refit the WHOLE open run and replace its elements,
        // but only when the part already on screen would move imperceptibly
        // - per-chunk bakes alone described one doodle with 2.3x the nodes
        // of the offline fit. A failed gate keeps the shown geometry and
        // closes the run, so consolidation retries on fresher ink.
        bool consolidated = false;
        if (state.runStartSample < state.frozenSamples
            && arc[boundary] - arc[state.runStartSample] < kRunCap) {
            Vector<PointF> runSlice =
                points.mid(state.runStartSample, boundary - state.runStartSample + 1);
            runSlice[0] = state.runStartPoint;
            Path candidate;
            candidate.moveTo(state.runStartPoint);
            fitRun(dedupePoints(runSlice), bakeParams, candidate,
                   state.hasRunEntryTangent ? &state.runEntryTangent : nullptr, &tangent);
            const Vector<PointF> shown =
                flattenPathFrom(state.frozenPath, state.runStartElement);
            const Vector<PointF> replacement = flattenPathFrom(candidate, 0);
            if (candidate.elementCount() >= 2 && !shown.isEmpty()
                && worstDistanceToPolyline(shown, replacement) <= kInvisibleBudget) {
                Path rebuilt;
                // The prefix before the run, verbatim; then the refit run
                // (skipping its moveTo - the prefix already ends there).
                {
                    Path fresh;
                    int i = 0;
                    while (i < state.runStartElement) {
                        const Path::Element e = state.frozenPath.elementAt(i);
                        if (e.type == Path::MoveToElement) {
                            fresh.moveTo(e.x, e.y);
                            ++i;
                        } else if (e.type == Path::LineToElement) {
                            fresh.lineTo(e.x, e.y);
                            ++i;
                        } else {
                            fresh.cubicTo(
                                PointF(e.x, e.y),
                                PointF(state.frozenPath.elementAt(i + 1).x,
                                        state.frozenPath.elementAt(i + 1).y),
                                PointF(state.frozenPath.elementAt(i + 2).x,
                                        state.frozenPath.elementAt(i + 2).y));
                            i += 3;
                        }
                    }
                    rebuilt = fresh;
                }
                appendPathElements(rebuilt, candidate, 1);
                state.frozenPath = rebuilt;
                consolidated = true;
            }
        }
        if (!consolidated) {
            fitRun(sliceFrom(boundary), bakeParams, state.frozenPath,
                   state.hasEntryTangent ? &state.entryTangent : nullptr, &tangent);
            if (state.runStartSample < state.frozenSamples) {
                state.runStartSample = -1;   // gate failed or run too long: restart
            }
        }
        state.frozenSamples = boundary;
        state.boundaryPoint = points[boundary];
        state.seamPoint = points[boundary];
        state.hasSeam = true;
        state.entryTangent = tangent;
        state.hasEntryTangent = true;
    }

    // The live tail, fitted onto a COPY of the frozen prefix (plus the open
    // chord, which is preview-only until it closes). The tail inherits the
    // current mode so curves do not render flat and then pop when baked.
    Path combined = state.frozenPath;
    if (combined.elementCount() == 0) {
        combined.moveTo(points.first());
    }
    PointF chordDirection;
    const PointF *entry = nullptr;
    if (state.chordStartSample >= 0) {
        combined.lineTo(state.seamPoint);
        const PointF d = state.seamPoint - state.chordAnchor;
        const double len = std::hypot(d.x(), d.y());
        if (len > 1e-9) {
            chordDirection = d / len;
            entry = &chordDirection;
        }
    } else if (state.hasEntryTangent) {
        entry = &state.entryTangent;
    }
    Vector<PointF> tail = points.mid(state.frozenSamples);
    if (state.hasSeam && !tail.isEmpty()) {
        tail[0] = state.seamPoint;
    }
    if (finalTail) {
        FitParams tailParams = params;
        tailParams.forceCurved = (state.chordStartSample < 0 && state.frozenSamples > 0);
        fitRun(dedupePoints(tail), tailParams, combined, entry, nullptr);
        return combined;
    }

    // While drawing, the tail is a denoised POLYLINE, not a fit: a per-frame
    // least-squares re-solve made the whole tail wobble within tolerance,
    // and zoomed-in detail work stares at exactly that region for seconds.
    // The smoothing displacement is capped POINTWISE (the same budget the
    // fit uses), which keeps the operator local: beyond the kernel's reach
    // behind the pen every previewed point is final, so the only thing that
    // moves on screen is the last few px under the pen tip.
    const Vector<PointF> deduped = dedupePoints(tail);
    if (deduped.size() < 2) {
        return combined;
    }
    double tailArcLength = 0.0;
    for (int i = 1; i < deduped.size(); ++i) {
        tailArcLength += LineF(deduped[i - 1], deduped[i]).length();
    }
    const double spacing =
        std::max<double>(0.25 * params.pixelScale, tailArcLength / (deduped.size() - 1));
    const double sigmaSamples = std::min<double>(20.0, params.gaussianSigma / spacing);

    // Corners are PINNED, exactly as the fit pins them (fitRun splits at
    // corners and smooths per piece with the endpoints held). One smoothing
    // pass across the whole tail rounded a just-drawn corner by up to the
    // displacement cap for seconds, then snapped it sharp in a single frame
    // when the bake reached it - a pop concentrated on the very feature the
    // eye tracks. Split the tail at high-turn samples and smooth each run
    // with its ends held instead.
    const double cornerThreshold = params.cornerAngleDeg * (kTwoPi * 0.5) / 180.0;
    Vector<int> pins;
    pins.append(0);
    for (int i = 2; i + 2 < deduped.size(); ++i) {
        const PointF in = deduped[i] - deduped[i - 2];
        const PointF out = deduped[i + 2] - deduped[i];
        const double lenIn = std::hypot(in.x(), in.y());
        const double lenOut = std::hypot(out.x(), out.y());
        if (lenIn < 1e-9 || lenOut < 1e-9) {
            continue;
        }
        const double turn = std::abs(std::atan2(crossZ(in, out), dotP(in, out)));
        if (turn > cornerThreshold && i - pins.last() >= 2) {
            pins.append(i);
        }
    }
    pins.append(deduped.size() - 1);

    Vector<PointF> smooth;
    for (int seg = 0; seg + 1 < pins.size(); ++seg) {
        const int first = pins[seg];
        const int last = pins[seg + 1];
        const Vector<PointF> run =
            gaussianSmooth(deduped.mid(first, last - first + 1), sigmaSamples);
        for (int i = (seg == 0 ? 0 : 1); i < run.size(); ++i) {
            smooth.append(run[i]);
        }
    }
    for (int i = 0; i < deduped.size(); ++i) {
        const PointF d = smooth[i] - deduped[i];
        const double len = std::hypot(d.x(), d.y());
        if (len > params.fitTolerance) {
            smooth[i] = deduped[i] + d * (params.fitTolerance / len);
        }
    }
    for (int i = 1; i < smooth.size(); ++i) {
        combined.lineTo(smooth[i]);
    }
    return combined;
}

AnimeVectorStroke AnimeVectorLogic::makeStroke(const Vector<PointF> &points,
                                               const Color &color,
                                               double width,
                                               int id,
                                               bool filterInput,
                                               bool smoothPath,
                                               const AnimeStrokeFitSettings &settings)
{
    AnimeVectorStroke stroke;
    stroke.id = id;
    stroke.points = filterInput ? filteredPoints(points) : points;
    stroke.lengths.clear();
    stroke.lengths.reserve(stroke.points.size());
    stroke.totalLength = 0.0;
    for (int i = 0; i < stroke.points.size(); ++i) {
        if (i > 0) {
            stroke.totalLength += LineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }
    // smoothPath now means the HYBRID FIT: straight runs become chords,
    // curved runs become least-squares cubics, corners stay corners. The old
    // midpoint-quad smoother (makeSmoothedPath) remains only for callers that
    // ask for it explicitly.
    stroke.path = smoothPath ? fitStrokePath(stroke.points, settings) : makePolylinePath(stroke.points);
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

AnimeVectorStroke AnimeVectorLogic::makeStrokeFromPath(const Path &path,
                                                       const Vector<PointF> &points,
                                                       const Color &color,
                                                       double width,
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
            stroke.totalLength += LineF(stroke.points[i - 1], stroke.points[i]).length();
        }
        stroke.lengths.append(stroke.totalLength);
    }
    stroke.path = path;
    stroke.bounds = stroke.path.boundingRect().adjusted(-width, -width, width, width);
    stroke.color = color;
    stroke.width = width;
    return stroke;
}

bool AnimeVectorLogic::strokeHitsCircle(const AnimeVectorStroke &stroke, const PointF &center, double radius)
{
    if (stroke.points.isEmpty()) {
        return false;
    }

    const double effectiveRadius = radius + stroke.width * 0.5;
    const double effectiveRadius2 = effectiveRadius * effectiveRadius;
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

bool AnimeVectorLogic::strokeHitsCapsule(const AnimeVectorStroke &stroke, const PointF &from, const PointF &to, double radius)
{
    if (LineF(from, to).length() <= kEpsilon) {
        return strokeHitsCircle(stroke, from, radius);
    }

    const double effectiveRadius = radius + stroke.width * 0.5;
    const double effectiveRadius2 = effectiveRadius * effectiveRadius;
    for (int i = 1; i < stroke.points.size(); ++i) {
        if (segmentSegmentDistanceSquared(stroke.points[i - 1], stroke.points[i], from, to) <= effectiveRadius2) {
            return true;
        }
    }
    return false;
}

double AnimeVectorLogic::strokeDistanceToBrush(const AnimeVectorStroke &stroke,
                                              const PointF &from,
                                              const PointF &to)
{
    if (stroke.points.isEmpty()) {
        return std::numeric_limits<double>::max();
    }
    if (stroke.points.size() == 1) {
        return std::sqrt(distancePointToSegmentSquared(stroke.points.first(), from, to));
    }

    double best = std::numeric_limits<double>::max();
    const bool isPoint = LineF(from, to).length() <= kEpsilon;
    for (int i = 1; i < stroke.points.size(); ++i) {
        const double distanceSquared =
            isPoint ? distancePointToSegmentSquared(from, stroke.points[i - 1], stroke.points[i])
                    : segmentSegmentDistanceSquared(stroke.points[i - 1], stroke.points[i], from, to);
        best = std::min(best, distanceSquared);
    }
    return std::sqrt(best);
}

Vector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCircle(const AnimeVectorStroke &stroke, const PointF &center, double radius)
{
    Vector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return Vector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
    }

    const double effectiveRadius = radius + stroke.width * 0.5;
    const double effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    double eraseStart = 0.0;
    auto inside = [&](const PointF &point) {
        return distanceSquared(point, center) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const PointF a = stroke.points[i - 1];
        const PointF b = stroke.points[i];
        if (distancePointToSegmentSquared(center, a, b) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const Vector<PointF> samples = densifySegment(a, b, std::max(double(1), effectiveRadius * 0.25));
        const double segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const double t = static_cast<double>(sampleIndex + 1) / samples.size();
            const double w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
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

Vector<AnimeVectorRange> AnimeVectorLogic::keepRangesForCapsule(const AnimeVectorStroke &stroke, const PointF &from, const PointF &to, double radius)
{
    if (LineF(from, to).length() <= kEpsilon) {
        return keepRangesForCircle(stroke, from, radius);
    }

    Vector<AnimeVectorRange> eraseRanges;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return Vector<AnimeVectorRange>{AnimeVectorRange{0.0, 1.0}};
    }

    const double effectiveRadius = radius + stroke.width * 0.5;
    const double effectiveRadius2 = effectiveRadius * effectiveRadius;
    bool inErase = false;
    double eraseStart = 0.0;
    auto inside = [&](const PointF &point) {
        return distancePointToSegmentSquared(point, from, to) <= effectiveRadius2;
    };
    if (inside(stroke.points.first())) {
        inErase = true;
        eraseStart = 0.0;
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        const PointF a = stroke.points[i - 1];
        const PointF b = stroke.points[i];
        if (segmentSegmentDistanceSquared(a, b, from, to) > effectiveRadius2 &&
            !inside(a) && !inside(b)) {
            continue;
        }

        const Vector<PointF> samples = densifySegment(a, b, std::max(double(1), effectiveRadius * 0.25));
        const double segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const double t = static_cast<double>(sampleIndex + 1) / samples.size();
            const double w = (stroke.lengths[i - 1] + segmentLength * t) / stroke.totalLength;
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

Vector<AnimeVectorRange> AnimeVectorLogic::complementRanges(const Vector<AnimeVectorRange> &eraseRanges)
{
    Vector<AnimeVectorRange> keepRanges;
    double last = 0.0;

    for (const AnimeVectorRange &range : eraseRanges) {
        const double first = clamp01(range.first);
        const double second = clamp01(range.second);
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

Vector<AnimeStrokeCrossing> AnimeVectorLogic::strokeCrossings(const AnimeVectorStroke &stroke,
                                                               const Vector<AnimeVectorStroke> &walls,
                                                               double mergeTolerance)
{
    Vector<AnimeStrokeCrossing> crossings;
    if (stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return crossings;
    }

    // A wall whose bounding box misses this stroke's cannot cross it: the
    // all-pairs segment walk below is quadratic, and on densely captured
    // artwork most walls are nowhere near the stroke.
    Vector<int> nearWalls;
    for (int w = 0; w < walls.size(); ++w) {
        const AnimeVectorStroke &wall = walls[w];
        if (wall.points.size() >= 2 && wall.totalLength > kEpsilon
            && (wall.bounds.isNull() || stroke.bounds.isNull()
                || wall.bounds.intersects(stroke.bounds))) {
            nearWalls.append(w);
        }
    }

    for (int i = 0; i + 1 < stroke.points.size(); ++i) {
        const PointF a = stroke.points[i];
        const PointF b = stroke.points[i + 1];
        const LineF segment(a, b);
        const double segmentLength = segment.length();
        if (segmentLength <= kEpsilon) {
            continue;
        }
        // Arc at the start of this segment, from the prefix table the stroke
        // already carries, so a crossing's position is exact rather than
        // re-integrated.
        const double arcStart = (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;
        // An axis-aligned segment spans a ZERO-AREA rect, and RectF's
        // intersects() is defined false for ANY empty rect - so a snapped
        // horizontal or vertical stroke silently saw no walls at all and
        // CutMode deleted it whole instead of trimming to its crossings.
        // The hair of padding keeps the prefilter a prefilter.
        const RectF segmentBounds =
            RectF(a, b).normalized().adjusted(-kEpsilon, -kEpsilon, kEpsilon, kEpsilon);

        for (int w : nearWalls) {
            const AnimeVectorStroke &wall = walls[w];
            if (!wall.bounds.isNull() && !wall.bounds.intersects(segmentBounds)) {
                continue;   // this segment is nowhere near that wall
            }
            for (int k = 0; k + 1 < wall.points.size(); ++k) {
                const LineF wallSegment(wall.points[k], wall.points[k + 1]);
                const double wallSegmentLength = wallSegment.length();
                if (wallSegmentLength <= kEpsilon) {
                    continue;
                }
                PointF hit;
                if (segment.intersects(wallSegment, &hit) != LineF::BoundedIntersection) {
                    continue;
                }
                AnimeStrokeCrossing crossing;
                const double along = std::min(LineF(a, hit).length(), segmentLength);
                crossing.arc = clamp01((arcStart + along) / stroke.totalLength);
                crossing.wallIndex = w;
                const double wallArcStart = (k < wall.lengths.size()) ? wall.lengths[k] : 0.0;
                const double wallAlong =
                    std::min(LineF(wall.points[k], hit).length(), wallSegmentLength);
                crossing.wallArc = clamp01((wallArcStart + wallAlong) / wall.totalLength);
                crossings.append(crossing);
            }
        }
    }

    // T-JUNCTIONS. A wall whose ENDPOINT rests on this stroke joins it
    // without crossing it: the Connect tool builds exactly such joints (a
    // bridge's end lands on a snapped vertex - often an INTERIOR corner or
    // curvature apex), and hand-drawn corners touch the same way. The
    // transversal walk above cannot see them - the contact is measure zero,
    // and the snapped point sits on the FITTED path, a fit tolerance away
    // from this polyline - so a cut on the bridged side fell through to the
    // stroke end and deleted the whole line. An endpoint within touching
    // distance (the two half-widths of ink: if it looks joined, it is)
    // becomes a crossing at its projection; a joint within that distance of
    // the stroke's own end snaps TO the end, so it bounds cleanly instead
    // of leaving a crumb.
    for (int w : nearWalls) {
        const AnimeVectorStroke &wall = walls[w];
        const double touch = std::max<double>(1.0, (stroke.width + wall.width) * 0.5);
        for (int endIndex = 0; endIndex < 2; ++endIndex) {
            const PointF endpoint =
                endIndex == 0 ? wall.points.first() : wall.points.last();
            double bestDistanceSquared = touch * touch;
            double bestArcLength = -1.0;
            for (int i = 0; i + 1 < stroke.points.size(); ++i) {
                const PointF a = stroke.points[i];
                const PointF b = stroke.points[i + 1];
                const double dx = b.x() - a.x();
                const double dy = b.y() - a.y();
                const double lengthSquared = dx * dx + dy * dy;
                if (lengthSquared <= kEpsilon) {
                    continue;
                }
                double t = ((endpoint.x() - a.x()) * dx + (endpoint.y() - a.y()) * dy)
                          / lengthSquared;
                t = std::min<double>(1.0, std::max<double>(0.0, t));
                const PointF projected(a.x() + dx * t, a.y() + dy * t);
                const double distanceSq = distanceSquared(endpoint, projected);
                if (distanceSq < bestDistanceSquared) {
                    bestDistanceSquared = distanceSq;
                    const double arcStart =
                        (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;
                    bestArcLength = arcStart + std::sqrt(lengthSquared) * t;
                }
            }
            if (bestArcLength < 0.0) {
                continue;
            }
            if (bestArcLength < touch) {
                bestArcLength = 0.0;
            } else if (stroke.totalLength - bestArcLength < touch) {
                bestArcLength = stroke.totalLength;
            }
            AnimeStrokeCrossing crossing;
            crossing.arc = clamp01(bestArcLength / stroke.totalLength);
            crossing.wallIndex = w;
            crossing.wallArc = endIndex == 0 ? 0.0 : 1.0;
            crossings.append(crossing);
        }
    }

    // The stroke against ITSELF. A line that loops back through its own path
    // makes a junction there exactly as if a second line had crossed it, and
    // the loop is usually the very piece the user wants to trim off.
    //
    // Adjoining segments are skipped: they meet at a shared vertex by
    // construction, which is how a polyline is joined, not where it cuts
    // across itself. The closing vertex of a closed stroke is the same story
    // between the first and last segment, so a hit sitting on both ends is
    // skipped too.
    const bool closed = stroke.points.size() > 2
                        && LineF(stroke.points.first(), stroke.points.last()).length() <= kEpsilon;
    for (int i = 0; i + 1 < stroke.points.size(); ++i) {
        const LineF a(stroke.points[i], stroke.points[i + 1]);
        if (a.length() <= kEpsilon) {
            continue;
        }
        const double arcA = (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;

        for (int j = i + 2; j + 1 < stroke.points.size(); ++j) {
            const LineF b(stroke.points[j], stroke.points[j + 1]);
            if (b.length() <= kEpsilon) {
                continue;
            }
            PointF hit;
            if (a.intersects(b, &hit) != LineF::BoundedIntersection) {
                continue;
            }
            if (closed && i == 0 && j + 2 == stroke.points.size()) {
                continue;   // the closure vertex, not a crossing
            }
            const double arcB = (j < stroke.lengths.size()) ? stroke.lengths[j] : 0.0;
            const double first =
                clamp01((arcA + std::min(LineF(a.p1(), hit).length(), a.length())) / stroke.totalLength);
            const double second =
                clamp01((arcB + std::min(LineF(b.p1(), hit).length(), b.length())) / stroke.totalLength);

            // Both sides of the junction, each pointing at the other: the cut
            // can be bounded by either, and whichever bounds it knows where
            // its partner sits so the survivor can be divided there too.
            AnimeStrokeCrossing lower;
            lower.arc = first;
            lower.wallIndex = AnimeStrokeCrossing::SelfCrossing;
            lower.wallArc = second;
            crossings.append(lower);

            AnimeStrokeCrossing upper;
            upper.arc = second;
            upper.wallIndex = AnimeStrokeCrossing::SelfCrossing;
            upper.wallArc = first;
            crossings.append(upper);
        }
    }

    // Stable, so that when a wall crossing and a self-crossing land on the
    // same arc the survivor of the merge below is decided by discovery order
    // rather than by the sort's internals.
    std::stable_sort(crossings.begin(), crossings.end(),
                     [](const AnimeStrokeCrossing &lhs, const AnimeStrokeCrossing &rhs) {
                         return lhs.arc < rhs.arc;
                     });
    // One crossing found on two adjoining segments (it sits on their shared
    // vertex) must not count twice, or a cut would collapse to nothing.
    Vector<AnimeStrokeCrossing> merged;
    for (const AnimeStrokeCrossing &crossing : crossings) {
        if (merged.isEmpty() || crossing.arc - merged.last().arc > mergeTolerance) {
            merged.append(crossing);
        }
    }
    return merged;
}

Vector<AnimeVectorStroke> AnimeVectorLogic::splitStrokeAt(const AnimeVectorStroke &stroke,
                                                           Vector<double> arcs,
                                                           const AnimeStrokeFitSettings &settings,
                                                           double endTolerance,
                                                           Vector<double> *usedArcs)
{
    Vector<AnimeVectorStroke> pieces;
    std::sort(arcs.begin(), arcs.end());

    Vector<double> cuts;
    for (double arc : arcs) {
        const double value = clamp01(arc);
        if (value <= endTolerance || value >= 1.0 - endTolerance) {
            continue;   // already an endpoint: nothing to divide there
        }
        if (cuts.isEmpty() || value - cuts.last() > endTolerance) {
            cuts.append(value);
        }
    }
    if (usedArcs) {
        *usedArcs = cuts;
    }
    if (cuts.isEmpty()) {
        pieces.append(stroke);
        return pieces;
    }

    double previous = 0.0;
    for (double cut : cuts) {
        pieces.append(subStroke(stroke, previous, cut, settings));
        previous = cut;
    }
    pieces.append(subStroke(stroke, previous, 1.0, settings));
    return pieces;
}

bool AnimeVectorLogic::cutSpanAt(const AnimeVectorStroke &stroke,
                                 const Vector<AnimeVectorStroke> &walls,
                                 const PointF &pos,
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
                                 const Vector<AnimeVectorStroke> &walls,
                                 const PointF &pos,
                                 AnimeCutPlan *plan)
{
    if (!plan || stroke.points.size() < 2 || stroke.totalLength <= kEpsilon) {
        return false;
    }

    // Where the click sits along the stroke, in the same normalised arc the
    // crossings use.
    double bestDistanceSq = std::numeric_limits<double>::max();
    double clickArc = 0.0;
    for (int i = 0; i + 1 < stroke.points.size(); ++i) {
        const PointF a = stroke.points[i];
        const PointF b = stroke.points[i + 1];
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double lengthSq = dx * dx + dy * dy;
        double t = 0.0;
        if (lengthSq > kEpsilon) {
            t = ((pos.x() - a.x()) * dx + (pos.y() - a.y()) * dy) / lengthSq;
            t = std::min<double>(1.0, std::max<double>(0.0, t));
        }
        const double projX = a.x() + dx * t;
        const double projY = a.y() + dy * t;
        const double distanceSq = (pos.x() - projX) * (pos.x() - projX)
                                 + (pos.y() - projY) * (pos.y() - projY);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            const double arcStart = (i < stroke.lengths.size()) ? stroke.lengths[i] : 0.0;
            clickArc = clamp01((arcStart + std::sqrt(lengthSq) * t) / stroke.totalLength);
        }
    }

    const Vector<AnimeStrokeCrossing> crossings = strokeCrossings(stroke, walls);

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

AnimeVectorStroke AnimeVectorLogic::subStroke(const AnimeVectorStroke &stroke, double fromW, double toW,
                                              const AnimeStrokeFitSettings &settings)
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

    const double fromLength = clamp01(fromW) * stroke.totalLength;
    const double toLength = clamp01(toW) * stroke.totalLength;
    Vector<PointF> points;
    if (toLength <= fromLength + kEpsilon) {
        return inherit(makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, settings));
    }

    // The piece's path is CUT OUT of the stroke's fitted path with the shared
    // exact splitting from algorithm/beziersplit.h, so it is the original
    // curve restricted to the kept span, exactly. Re-fitting a slice of the
    // samples (the old way) re-ran denoising, corner detection and tangent
    // estimation on the slice and visibly changed the shape of what remained
    // - inserting a junction must not move a pixel of the drawing. The
    // piece's polyline is re-sampled off the path too: it has to describe
    // the geometry the piece renders. The locate anchor is the point the
    // caller's arc means on the DENSE polyline; the fitted path runs within
    // the fit tolerance of it.
    const AnimeBezierPathRun run = AnimeBezierSplit::buildRun(stroke.path);
    if (run.isValid() && run.totalLength() > kEpsilon) {
        const AnimeBezierPathPosition from = AnimeBezierSplit::locate(
            run, clamp01(fromW), pointAtLength(stroke, fromLength));
        const AnimeBezierPathPosition to = AnimeBezierSplit::locate(
            run, clamp01(toW), pointAtLength(stroke, toLength));
        Path piecePath;
        Vector<PointF> piecePoints;
        if (AnimeBezierSplit::slice(run, from, to, &piecePath, &piecePoints)) {
            return inherit(makeStrokeFromPath(piecePath, piecePoints,
                                              stroke.color, stroke.width, stroke.id));
        }
    }

    // No usable single-run path (empty, or several subpaths): slice the
    // polyline and fit the slice, as before.
    points.append(pointAtLength(stroke, fromLength));
    for (int i = 1; i + 1 < stroke.points.size(); ++i) {
        if (stroke.lengths[i] > fromLength + kEpsilon &&
            stroke.lengths[i] < toLength - kEpsilon) {
            points.append(stroke.points[i]);
        }
    }
    points.append(pointAtLength(stroke, toLength));

    return inherit(makeStroke(points, stroke.color, stroke.width, stroke.id, false, true, settings));
}

AnimeVectorStroke AnimeVectorLogic::snapStrokeEndpoint(const AnimeVectorStroke &stroke, bool atEnd,
                                                       const PointF &target)
{
    if (stroke.points.size() < 2 || stroke.path.elementCount() < 2) {
        return stroke;
    }
    Path path = stroke.path;
    path.setElementPositionAt(atEnd ? path.elementCount() - 1 : 0, target.x(), target.y());
    Vector<PointF> points = stroke.points;
    points[atEnd ? points.size() - 1 : 0] = target;
    AnimeVectorStroke snapped =
        makeStrokeFromPath(path, points, stroke.color, stroke.width, stroke.id);
    snapped.property = stroke.property;
    snapped.penStyle = stroke.penStyle;
    return snapped;
}

double AnimeVectorLogic::displayStrokeWidth(double documentWidth, double zoom, double minScreenPx)
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
    const double s = (documentWidth * zoom) / minScreenPx;   // screen widths, in floors
    const double smooth = std::pow(1.0 + std::pow(s, 6.0), 1.0 / 6.0);
    return smooth * minScreenPx / zoom;
}

PointF AnimeVectorLogic::pointAtLength(const AnimeVectorStroke &stroke, double length)
{
    if (stroke.points.isEmpty()) {
        return PointF();
    }
    if (length <= 0 || stroke.points.size() == 1) {
        return stroke.points.first();
    }
    if (length >= stroke.totalLength) {
        return stroke.points.last();
    }

    for (int i = 1; i < stroke.points.size(); ++i) {
        if (stroke.lengths[i] >= length) {
            const double segmentLength = stroke.lengths[i] - stroke.lengths[i - 1];
            if (segmentLength <= kEpsilon) {
                return stroke.points[i];
            }
            const double t = (length - stroke.lengths[i - 1]) / segmentLength;
            return stroke.points[i - 1] + (stroke.points[i] - stroke.points[i - 1]) * t;
        }
    }

    return stroke.points.last();
}

Vector<LineF> AnimeVectorLogic::segmentsFromPath(const Path &path)
{
    Vector<LineF> segments;
    const Vector<Vector<PointF>> subpaths = path.toSubpathPolygons();
    for (const Vector<PointF> &polyline : subpaths) {
        for (int i = 1; i < polyline.size(); ++i) {
            const PointF a = polyline[i - 1];
            const PointF b = polyline[i];
            if (LineF(a, b).length() > kGraphEpsilon) {
                segments.append(LineF(a, b));
            }
        }
    }
    return segments;
}

Vector<AnimeVectorRegionFace> AnimeVectorLogic::computeVectorRegionFaces(const Vector<LineF> &segments,
                                                                          double healRadius)
{
    Vector<AnimeVectorRegionFace> faces;
    if (segments.isEmpty()) {
        return faces;
    }

    if (healRadius > 0.0) {
        // A point where exactly ONE segment terminates is a dangling tip - a
        // flattening's interior vertex always closes two. Sub-pixel junction
        // gaps (strokes cut before the endpoint snap existed, corners drawn
        // almost closed, a cut end floating a fit tolerance off the wall it
        // was cut against) leave such tips just short of the ink they were
        // meant to touch. Each tip attaches INDEPENDENTLY to the nearest
        // point of a segment it does not terminate: attaching to existing
        // ink closes an almost-closed loop but can never invent a boundary
        // between two separate lines, a three-way near-miss attaches all
        // three, and a tip against a wall's INTERIOR (or a shared split
        // vertex, where no partner tip exists) works the same as a tip
        // against another tip's segment. The intersection pass below turns
        // every touch into a shared graph vertex. The radius is a
        // document-unit constant: with fresh cuts snapped at the source it
        // only serves legacy content, which was drawn near 1:1 zoom.
        Map<String, int> endCount;
        for (const LineF &segment : segments) {
            ++endCount[vertexKey(segment.p1())];
            ++endCount[vertexKey(segment.p2())];
        }
        Vector<PointF> tips;
        Vector<String> tipKeys;
        Map<String, bool> seenTips;
        for (const LineF &segment : segments) {
            for (const PointF &end : {segment.p1(), segment.p2()}) {
                const String key = vertexKey(end);
                if (endCount.value(key) == 1 && !seenTips.contains(key)) {
                    seenTips.insert(key, true);
                    tips.append(end);
                    tipKeys.append(key);
                }
            }
        }

        // PHASE 1 - mutual pairs: two tips that are each other's nearest tip
        // merge onto their midpoint (a trimmed corner, where BOTH sides end
        // free). Attaching such a pair each to the OTHER's segment instead
        // just swapped their positions and still left no shared vertex.
        Map<String, PointF> remap;
        Vector<bool> merged(tips.size(), false);
        auto nearestTip = [&](int index) {
            int best = -1;
            double bestDistance = healRadius;
            for (int other = 0; other < tips.size(); ++other) {
                if (other == index) {
                    continue;
                }
                const double distance = LineF(tips[index], tips[other]).length();
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    best = other;
                }
            }
            return best;
        };
        for (int a = 0; a < tips.size(); ++a) {
            if (merged[a]) {
                continue;
            }
            const int b = nearestTip(a);
            if (b >= 0 && !merged[b] && nearestTip(b) == a) {
                merged[a] = merged[b] = true;
                const PointF middle = (tips[a] + tips[b]) * 0.5;
                remap.insert(tipKeys[a], middle);
                remap.insert(tipKeys[b], middle);
            }
        }
        Vector<LineF> healed = segments;
        bool changed = !remap.isEmpty();
        if (changed) {
            for (LineF &segment : healed) {
                const auto p1 = remap.constFind(vertexKey(segment.p1()));
                if (p1 != remap.constEnd()) {
                    segment.setP1(p1.value());
                }
                const auto p2 = remap.constFind(vertexKey(segment.p2()));
                if (p2 != remap.constEnd()) {
                    segment.setP2(p2.value());
                }
            }
        }

        // PHASE 2 - lone tips (a cut end floating against a wall the pieces
        // of which share their split vertex exactly, or the odd arm of a
        // three-way near-miss) attach to the nearest point of a segment they
        // do not terminate. The intersection pass below turns the touch into
        // a shared graph vertex.
        for (LineF &segment : healed) {
            for (int endIndex = 0; endIndex < 2; ++endIndex) {
                const PointF tip = endIndex == 0 ? segment.p1() : segment.p2();
                const String tipKey = vertexKey(tip);
                if (endCount.value(tipKey) != 1 || remap.contains(tipKey)) {
                    continue;
                }
                PointF best;
                double bestDistance = healRadius;
                bool found = false;
                for (const LineF &other : healed) {
                    if (vertexKey(other.p1()) == tipKey || vertexKey(other.p2()) == tipKey) {
                        continue;   // a segment that terminates at this tip
                    }
                    const PointF d = other.p2() - other.p1();
                    const double len2 = d.x() * d.x() + d.y() * d.y();
                    if (len2 <= kEpsilon * kEpsilon) {
                        continue;
                    }
                    double t = ((tip.x() - other.p1().x()) * d.x()
                               + (tip.y() - other.p1().y()) * d.y()) / len2;
                    t = std::min<double>(1.0, std::max<double>(0.0, t));
                    const PointF q = other.p1() + d * t;
                    const double distance = LineF(tip, q).length();
                    if (distance > 1e-9 && distance <= bestDistance) {
                        bestDistance = distance;
                        best = q;
                        found = true;
                    }
                }
                if (found) {
                    if (endIndex == 0) {
                        segment.setP1(best);
                    } else {
                        segment.setP2(best);
                    }
                    changed = true;
                }
            }
        }
        if (changed) {
            return computeVectorRegionFaces(healed, 0.0);
        }
    }

    Vector<Vector<double>> splitParameters;
    splitParameters.resize(segments.size());
    for (int i = 0; i < segments.size(); ++i) {
        splitParameters[i].append(0.0);
        splitParameters[i].append(1.0);
    }

    for (int i = 0; i < segments.size(); ++i) {
        for (int j = i + 1; j < segments.size(); ++j) {
            double ti = 0.0;
            double tj = 0.0;
            if (!segmentIntersectionParameters(segments[i], segments[j], ti, tj)) {
                continue;
            }
            appendUniqueParameter(splitParameters[i], ti);
            appendUniqueParameter(splitParameters[j], tj);
        }
    }

    Vector<GraphVertex> vertices;
    Map<String, int> vertexByKey;
    auto vertexIndex = [&](const PointF &point) {
        const String key = vertexKey(point);
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
        Vector<double> params = splitParameters[i];
        std::sort(params.begin(), params.end());
        for (int j = 1; j < params.size(); ++j) {
            if (params[j] - params[j - 1] <= kGraphEpsilon) {
                continue;
            }
            const PointF delta = segments[i].p2() - segments[i].p1();
            const PointF p0 = segments[i].p1() + delta * params[j - 1];
            const PointF p1 = segments[i].p1() + delta * params[j];
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
            const PointF dl = vertices[lhs].point - vertex.point;
            const PointF dr = vertices[rhs].point - vertex.point;
            return std::atan2(dl.y(), dl.x()) < std::atan2(dr.y(), dr.x());
        });
    }

    Map<String, bool> visited;
    auto edgeKey = [](int from, int to) {
        return String::number(from) + Latin1Char('>') + String::number(to);
    };

    for (int from = 0; from < vertices.size(); ++from) {
        for (int to : vertices[from].neighbors) {
            const String firstKey = edgeKey(from, to);
            if (visited.value(firstKey, false)) {
                continue;
            }

            Vector<PointF> facePoints;
            int currentFrom = from;
            int currentTo = to;
            bool closed = false;
            for (int guard = 0; guard < 10000; ++guard) {
                visited.insert(edgeKey(currentFrom, currentTo), true);
                facePoints.append(vertices[currentFrom].point);

                const Vector<int> &nextNeighbors = vertices[currentTo].neighbors;
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

            const double area = polygonSignedArea(facePoints);
            if (area <= kGraphEpsilon) {
                continue;
            }

            Path facePath;
            facePath.moveTo(facePoints.first());
            for (int i = 1; i < facePoints.size(); ++i) {
                facePath.lineTo(facePoints[i]);
            }
            facePath.closeSubpath();
            faces.append(AnimeVectorRegionFace{simplified(facePath), area});
        }
    }

    return faces;
}

Path AnimeVectorLogic::vectorRegionPathAt(const PointF &seed, const Vector<LineF> &segments, const Rect &canvasRect)
{
    if (!canvasRect.contains(seed.toPoint())) {
        return Path();
    }

    // Filling heals sub-pixel junction gaps (dangling-tip pairs): strokes
    // cut before the endpoint snap existed, or corners drawn almost closed,
    // still bound a fillable face.
    const Vector<AnimeVectorRegionFace> faces =
        computeVectorRegionFaces(segments, kFillGapHealPx);
    const AnimeVectorRegionFace *bestFace = nullptr;
    double bestArea = std::numeric_limits<double>::max();
    for (const AnimeVectorRegionFace &face : faces) {
        const double area = std::abs(face.signedArea);
        if (area <= kGraphEpsilon ||
            area >= bestArea ||
            !face.path.contains(seed)) {
            continue;
        }
        bestFace = &face;
        bestArea = area;
    }

    if (!bestFace) {
        return Path();
    }

    // HOLES. A face is traced as ONE cycle, so when the ink inside a region is
    // a separate loop - the counter of an "A", a window inside a shape - that
    // loop is its own face and the outer face's polygon simply spans it. Paint
    // that polygon and the hole is coloured in with everything else, which is
    // what "the fill leaked into the middle of the A" means.
    //
    // The graph cannot tell us the nesting (the two loops share no edge, which
    // is precisely why they are separate faces), so it is decided
    // geometrically: any other face that lies wholly inside the chosen one,
    // and that the seed is not in, is a hole of it. Path::contains
    // returns false as soon as the two outlines touch, so a face merely
    // ADJACENT to the chosen one - the ordinary case, sharing a wall with it -
    // is never mistaken for a hole.
    Path region = bestFace->path;
    const RectF bestBounds = bestFace->path.boundingRect();
    for (const AnimeVectorRegionFace &face : faces) {
        if (&face == bestFace || std::abs(face.signedArea) >= bestArea) {
            continue;
        }
        // Cheap reject first: containment of paths is the expensive test.
        if (!bestBounds.contains(face.path.boundingRect())) {
            continue;
        }
        if (face.path.contains(seed) || !contains(bestFace->path, face.path)) {
            continue;
        }
        region = subtracted(region, face.path);
    }

    Path canvas;
    canvas.addRect(canvasRect);

    // Stroking the region AFTER the holes are cut tucks the paint under the
    // hole's outline too, the same way it tucks under the outer one - so a
    // hole does not show a hairline of paper around its ink.
    return simplified(intersected(united(region, strokeOutline(region, kVectorRegionOverpaintWidth)), canvas));
}

Path AnimeVectorLogic::fillPathFromMask(const Point &seed, const Image &boundary)
{
    Path path;
    if (boundary.isNull() || !Rect(0, 0, boundary.width(), boundary.height()).contains(seed)) {
        return path;
    }
    if (boundary.gray(seed.x(), seed.y()) > 0) {
        return path;
    }

    Image visited(boundary.width(), boundary.height(), Image::Format_Grayscale8);
    visited.fill(0);

    Queue<Point> queue;
    queue.enqueue(seed);
    visited.setPixel(seed.x(), seed.y(), 255);

    bool touchesCanvasEdge = false;
    while (!queue.isEmpty()) {
        const Point p = queue.dequeue();
        if (p.x() == 0 || p.y() == 0 || p.x() == boundary.width() - 1 || p.y() == boundary.height() - 1) {
            touchesCanvasEdge = true;
        }

        const Point neighbors[4] = {
            Point(p.x() + 1, p.y()),
            Point(p.x() - 1, p.y()),
            Point(p.x(), p.y() + 1),
            Point(p.x(), p.y() - 1)
        };

        for (const Point &next : neighbors) {
            if (!Rect(0, 0, boundary.width(), boundary.height()).contains(next)) {
                continue;
            }
            if (visited.gray(next.x(), next.y()) > 0 ||
                boundary.gray(next.x(), next.y()) > 0) {
                continue;
            }
            visited.setPixel(next.x(), next.y(), 255);
            queue.enqueue(next);
        }
    }

    if (touchesCanvasEdge) {
        return Path();
    }

    for (int y = 0; y < visited.height(); ++y) {
        int runStart = -1;
        for (int x = 0; x < visited.width(); ++x) {
            const bool filled = visited.gray(x, y) > 0;
            if (filled && runStart < 0) {
                runStart = x;
            } else if (!filled && runStart >= 0) {
                path.addRect(RectF(runStart, y, x - runStart, 1));
                runStart = -1;
            }
        }
        if (runStart >= 0) {
            path.addRect(RectF(runStart, y, visited.width() - runStart, 1));
        }
    }

    return simplified(path);
}
