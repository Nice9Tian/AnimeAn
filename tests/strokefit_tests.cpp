// Regression tests for the hybrid stroke fit's zoom behaviour.
//
// The bug these pin down: the corner detector measures turn over a SCREEN-px
// arc window, and on a stroke drawn zoomed out (a small gesture on screen,
// the window a large share of the whole stroke once converted to document
// px) smooth curvature itself read as "corner" - a small circle drawn at 1/4
// zoom committed as a chain of chords with 70-90 degree joints the moment
// the pen lifted, while the same circle drawn at 1x fitted as smooth cubics.
// Sparse captures (the tablet reports screen distances, so zoomed-out
// drawings arrive with few, far-apart document samples) overshot the window
// walk on top of that and doubled the measured turn.
//
// The fix caps the corner/tangent window by the stroke's own arc length
// (shortStrokeStrideWindow) and picks the window-walk sample nearest the
// window instead of the first one past it, so the corner decision is about
// the stroke's SHAPE, not about where the zoom slider sat while drawing.
// These tests drive the public fit entry points with synthetic pen input
// captured the way the paint view captures it, and check the committed
// path's joint structure: smooth input must stay smooth at every zoom,
// deliberate corners must stay corners at every corner-slider setting.
//
// The second bug, same family: a tight round bend that is only PART of a
// stroke. The cap cannot help there - the stroke is far longer than the
// bend - and a bend of radius r still reads window / r, so everything
// tighter than about 7 screen px (9 on a long stroke) was cut as a corner:
// the loop of a handwritten 6, the bowls of a 2, a 3 or a 5. The fix asks
// where the turn sits (isRoundBend in vectorlogic.cpp): a corner's turn is
// on one or two neighbouring samples and stops there, a bend's is spread
// and goes on beside them. The last blocks replay strokes recorded from a
// real hand and from the real Pen.

#include "vectorlogic.h"
#include "viewscale.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

// Deterministic hand tremor: incommensurate sinusoids, amplitude in SCREEN
// px (what the stabilizer still leaves in the captured points).
AnimeCore::PointF tremor(double t, double amplitudeScreenPx)
{
    return AnimeCore::PointF(amplitudeScreenPx
                                 * (0.6 * std::sin(12.9898 * t) + 0.4 * std::sin(78.233 * t + 1.3)),
                             amplitudeScreenPx
                                 * (0.6 * std::cos(37.719 * t) + 0.4 * std::sin(24.653 * t + 2.1)));
}

// Screen-space pen events -> document points, with the same capture floor
// appendPoint applies (m_minPointDistance = 2 screen px, zoom clamped by the
// shared wheel in algorithm/viewscale.h).
AnimeCore::Vector<AnimeCore::PointF> capture(const std::vector<AnimeCore::PointF> &screenSamples, double zoom)
{
    const double minDocDistance = 2.0 * AnimeViewScale::pixelScale(zoom);
    AnimeCore::Vector<AnimeCore::PointF> doc;
    for (const AnimeCore::PointF &s : screenSamples) {
        const AnimeCore::PointF p(s.x() / zoom, s.y() / zoom);
        if (doc.isEmpty() || AnimeCore::LineF(doc.last(), p).length() >= minDocDistance) {
            doc.append(p);
        }
    }
    return doc;
}

std::vector<AnimeCore::PointF> screenCircle(double radiusScreenPx, int events, double tremorAmp)
{
    std::vector<AnimeCore::PointF> out;
    for (int i = 0; i <= events; ++i) {
        const double a = 2.0 * kPi * i / events;
        AnimeCore::PointF p(radiusScreenPx * std::cos(a), radiusScreenPx * std::sin(a));
        if (i != 0 && i != events) {
            p += tremor(a * radiusScreenPx, tremorAmp);
        }
        out.push_back(p);
    }
    return out;
}

std::vector<AnimeCore::PointF> screenSquare(double sideScreenPx, int eventsPerSide, double tremorAmp)
{
    std::vector<AnimeCore::PointF> out;
    const AnimeCore::PointF corners[5] = {AnimeCore::PointF(0, 0), AnimeCore::PointF(sideScreenPx, 0),
                                          AnimeCore::PointF(sideScreenPx, sideScreenPx), AnimeCore::PointF(0, sideScreenPx),
                                          AnimeCore::PointF(0, 0)};
    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < eventsPerSide; ++i) {
            const double t = double(i) / eventsPerSide;
            AnimeCore::PointF p = corners[side] + (corners[side + 1] - corners[side]) * t;
            if (!(side == 0 && i == 0)) {
                p += tremor((side * eventsPerSide + i) * 0.7, tremorAmp);
            }
            out.push_back(p);
        }
    }
    out.push_back(corners[4]);
    return out;
}

std::vector<AnimeCore::PointF> screenV(double armScreenPx, int eventsPerArm, double tremorAmp)
{
    std::vector<AnimeCore::PointF> out;
    for (int i = 0; i <= eventsPerArm; ++i) {
        AnimeCore::PointF p(armScreenPx * i / eventsPerArm, armScreenPx - armScreenPx * i / eventsPerArm);
        if (i) {
            p += tremor(i * 0.9, tremorAmp);
        }
        out.push_back(p);
    }
    for (int i = 1; i <= eventsPerArm; ++i) {
        AnimeCore::PointF p(armScreenPx + armScreenPx * i / eventsPerArm, armScreenPx * i / eventsPerArm);
        p += tremor((eventsPerArm + i) * 0.9, tremorAmp);
        out.push_back(p);
    }
    return out;
}

// A pen path of straight legs and circular arcs in SCREEN px, emitted as
// dense pen events with tremor (capture() thins them to the capture floor).
// `phase` shifts the tremor: another hand drawing the same shape.
struct ScreenPath {
    std::vector<AnimeCore::PointF> events;
    AnimeCore::PointF pos{0.0, 0.0};
    double heading = 0.0;   // radians, direction of travel
    double travelled = 0.0; // px along the path; drives the tremor
    double phase = 0.0;
    double tremorAmp = 0.30;

    explicit ScreenPath(int hand = 0) : phase(hand * 7.31) {}

    void emit(const AnimeCore::PointF &p)
    {
        events.push_back(events.empty() ? p : p + tremor(travelled * 0.9 + phase, tremorAmp));
    }

    ScreenPath &line(double length)
    {
        const AnimeCore::PointF from = pos;
        const AnimeCore::PointF dir(std::cos(heading), std::sin(heading));
        const int steps = std::max(1, int(length / 0.7));
        if (events.empty()) {
            emit(pos);
        }
        for (int i = 1; i <= steps; ++i) {
            travelled += length / steps;
            emit(from + dir * (length * i / steps));
        }
        pos = from + dir * length;
        return *this;
    }

    // Positive sweep turns clockwise on screen (y grows downward).
    ScreenPath &arc(double sweepDeg, double radius)
    {
        const double sweep = sweepDeg * kPi / 180.0;
        const double side = sweep >= 0.0 ? 1.0 : -1.0;
        const AnimeCore::PointF centre =
            pos + AnimeCore::PointF(-std::sin(heading), std::cos(heading)) * (side * radius);
        const double start = std::atan2(pos.y() - centre.y(), pos.x() - centre.x());
        const double length = std::abs(sweep) * radius;
        const int steps = std::max(2, int(length / 0.7));
        if (events.empty()) {
            emit(pos);
        }
        for (int i = 1; i <= steps; ++i) {
            const double a = start + sweep * i / steps;
            travelled += length / steps;
            emit(centre + AnimeCore::PointF(std::cos(a), std::sin(a)) * radius);
        }
        pos = centre + AnimeCore::PointF(std::cos(start + sweep), std::sin(start + sweep)) * radius;
        heading += sweep;
        return *this;
    }

    ScreenPath &turn(double deg)
    {
        heading += deg * kPi / 180.0;
        return *this;
    }
};

// Strokes recorded from a real hand on 2026-11-13: digits about 25 x 35
// screen px, written with the pen at 1.2045x zoom, Simplify 50 / Corner 50.
// These are the pen's captured samples in document px (each stroke shifted
// to the origin). Before the bend test the two 6s came back with
// their loops cut twice, and the 2, the 5 and the 3 with a cut on every bowl.
constexpr double kRecordedZoom = 1.2045;
const double kRecordedSixA[] = {
    3.24, 0.45, 2.11, 2.08, 1.50, 4.49, 1.20, 6.55, 0.98, 8.62,
    0.82, 10.97, 0.88, 13.50, 0.97, 15.88, 1.20, 18.01, 1.42, 19.91,
    2.00, 21.63, 2.99, 23.75, 4.23, 25.28, 5.79, 26.16, 7.57, 26.46,
    9.45, 25.98, 11.07, 25.00, 12.23, 23.63, 12.78, 21.92, 13.55, 20.24,
    13.01, 18.56, 12.27, 16.76, 10.68, 16.00, 8.98, 16.13, 7.05, 16.85,
    4.57, 17.72,
};
const double kRecordedSixB[] = {
    3.82, 0.15, 2.40, 1.58, 1.56, 3.32, 1.06, 5.00, 0.65, 7.04,
    0.51, 9.35, 0.47, 11.83, 0.70, 14.19, 1.08, 16.57, 1.73, 18.88,
    2.63, 20.85, 3.43, 22.63, 4.45, 24.01, 6.20, 25.35, 8.24, 25.90,
    10.42, 25.63, 12.53, 24.52, 13.91, 23.52, 15.29, 22.39, 16.86, 20.56,
    17.88, 18.61, 18.36, 16.61, 18.17, 14.91, 17.24, 13.20, 15.85, 11.71,
    14.18, 10.73, 12.50, 10.29, 10.45, 10.53, 8.57, 11.22, 6.94, 12.35,
    4.49, 14.76,
};
const double kRecordedTwo[] = {
    0.42, 4.58, 2.48, 3.64, 4.44, 2.64, 6.61, 1.71, 8.77, 0.94,
    10.67, 0.68, 12.35, 0.83, 13.92, 2.09, 14.61, 3.73, 14.75, 6.16,
    14.61, 8.02, 14.29, 10.13, 13.76, 12.20, 13.16, 14.13, 12.52, 16.16,
    11.86, 17.96, 10.78, 20.09, 12.76, 20.70, 14.85, 19.57, 16.47, 18.56,
    18.23, 17.67, 19.86, 16.82, 21.41, 15.92, 23.41, 14.85, 25.66, 13.22,
};
const double kRecordedFive[] = {
    0.12, 0.37, 0.35, 2.43, 0.77, 4.47, 1.31, 6.85, 1.84, 9.02,
    2.27, 10.73, 4.23, 11.20, 6.37, 10.63, 8.20, 10.35, 10.15, 10.33,
    12.17, 10.84, 13.98, 11.72, 15.45, 12.81, 16.57, 14.35, 17.17, 16.12,
    17.25, 18.02, 16.49, 19.98, 15.16, 21.72, 13.49, 23.29, 11.39, 24.53,
    9.94, 25.48, 7.80, 26.40, 4.10, 25.61,
};
const double kRecordedThree[] = {
    0.98, 4.42, 2.03, 3.12, 3.87, 2.08, 5.64, 1.40, 7.75, 0.85,
    9.94, 0.46, 12.12, 0.19, 14.03, 0.25, 15.78, 0.68, 17.46, 1.84,
    17.75, 4.19, 17.12, 6.61, 16.01, 9.36, 14.50, 11.95, 13.05, 14.14,
    11.85, 16.09, 10.71, 17.61, 12.98, 17.19, 14.60, 16.41, 16.22, 15.74,
    18.55, 15.31, 20.78, 15.42, 22.48, 16.14, 23.81, 17.39, 24.53, 19.14,
    24.60, 21.33, 24.23, 23.03, 23.77, 24.84, 22.81, 26.74, 21.39, 28.80,
    19.99, 30.62, 18.60, 32.13, 15.60, 33.65,
};

// A right angle drawn through the real Pen at a steady 160 px/s (legs of 30
// screen px, zoom 1): what the stabilizer hands the fitter. It rounds the
// corner, so only about 0.62 of the turn is left on the two samples at the
// apex - right at the limit of the bend test's first half. Nothing follows
// on beside it, and the second half keeps it a corner either way.
const double kPenRightAngle[] = {
    0.00, 1.00, 2.27, 1.00, 4.77, 0.96, 7.06, 1.02, 9.31, 1.05,
    11.80, 0.98, 14.22, 0.98, 16.69, 0.99, 19.22, 1.00, 21.61, 1.05,
    24.18, 0.98, 26.56, 0.96, 28.59, 1.50, 29.80, 3.43, 30.34, 5.39,
    30.46, 7.62, 30.65, 9.85, 30.62, 12.18, 30.64, 14.63, 30.60, 17.06,
    30.55, 19.54, 30.52, 21.97, 30.47, 24.34, 30.42, 26.84, 29.79, 31.11,
};

template <int N>
AnimeCore::Vector<AnimeCore::PointF> recorded(const double (&xy)[N])
{
    AnimeCore::Vector<AnimeCore::PointF> points;
    for (int i = 0; i + 1 < N; i += 2) {
        points.append(AnimeCore::PointF(xy[i], xy[i + 1]));
    }
    return points;
}

struct JointStats {
    int lineSegments = 0;
    int curveSegments = 0;
    int cornersOver45Deg = 0;
    double maxKinkDeg = 0.0; // sharpest tangent break between elements
};

// Walk the committed path and measure the tangent break at every joint
// between consecutive elements. G1 handoffs read ~0; a chopped-up stroke
// reads the corner angles the detector inserted.
JointStats jointStats(const AnimeCore::Path &path)
{
    JointStats stats;
    AnimeCore::PointF current;
    AnimeCore::PointF lastDir;
    bool haveDir = false;
    int i = 0;
    while (i < path.elementCount()) {
        const AnimeCore::Path::Element e = path.elementAt(i);
        AnimeCore::PointF to, inDir, outDir;
        if (e.type == AnimeCore::Path::MoveToElement) {
            current = AnimeCore::PointF(e.x, e.y);
            haveDir = false;
            ++i;
            continue;
        }
        if (e.type == AnimeCore::Path::LineToElement) {
            to = AnimeCore::PointF(e.x, e.y);
            inDir = outDir = to - current;
            ++stats.lineSegments;
            ++i;
        } else {
            const AnimeCore::PointF c1(e.x, e.y);
            const AnimeCore::PointF c2(path.elementAt(i + 1).x, path.elementAt(i + 1).y);
            to = AnimeCore::PointF(path.elementAt(i + 2).x, path.elementAt(i + 2).y);
            inDir = c1 - current;
            if (std::hypot(inDir.x(), inDir.y()) < 1e-9) {
                inDir = c2 - current;
            }
            outDir = to - c2;
            if (std::hypot(outDir.x(), outDir.y()) < 1e-9) {
                outDir = to - c1;
            }
            ++stats.curveSegments;
            i += 3;
        }
        if (haveDir && std::hypot(lastDir.x(), lastDir.y()) > 1e-9
            && std::hypot(inDir.x(), inDir.y()) > 1e-9) {
            const double cross = lastDir.x() * inDir.y() - lastDir.y() * inDir.x();
            const double dot = lastDir.x() * inDir.x() + lastDir.y() * inDir.y();
            const double kink = std::abs(std::atan2(cross, dot)) * 180.0 / kPi;
            stats.maxKinkDeg = std::max(stats.maxKinkDeg, kink);
            if (kink > 45.0) {
                ++stats.cornersOver45Deg;
            }
        }
        lastDir = outDir;
        haveDir = true;
        current = to;
    }
    return stats;
}

AnimeStrokeFitSettings settingsFor(double zoom, int corner = 50)
{
    AnimeStrokeFitSettings settings;
    settings.simplify = 50;
    settings.corner = corner;
    settings.pixelScale = AnimeViewScale::pixelScale(zoom);
    return settings;
}

} // namespace

int main()
{
    // A small circle (30 doc px radius) drawn while zoomed out arrives as a
    // tiny on-screen gesture; it must still commit as smooth curves. Before
    // the window cap it came back as 3-5 chords with 70-90 degree joints.
    for (double zoom : {1.0, 0.5, 0.25, 0.125}) {
        const int events = std::max(16, int(2.0 * kPi * 30.0 * zoom));
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(screenCircle(30.0 * zoom, events, 0.30), zoom);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(zoom)));
        check(s.curveSegments >= 1, "small circle must contain curve segments at every zoom");
        check(s.maxKinkDeg <= 20.0, "small circle must stay G1-smooth at every zoom");
    }

    // Sparse capture of the same kind of circle (fast drawing: few events).
    // The window walk used to overshoot on sparse samples and double the
    // measured turn, cornering these even harder.
    for (int events : {12, 16, 24}) {
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(screenCircle(10.0, events, 0.15), 0.25);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(0.25)));
        check(s.curveSegments >= 1, "sparse circle must contain curve segments");
        check(s.maxKinkDeg <= 20.0, "sparse circle must stay G1-smooth");
    }

    // The release path is liveFitStrokePath over the final buffer: feed it
    // incrementally like move events, then release. Same smoothness bar.
    {
        const double zoom = 0.25;
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(screenCircle(7.5, 48, 0.30), zoom);
        AnimeLiveFitState state;
        for (int i = 3; i < doc.size(); i += 4) {
            AnimeVectorLogic::liveFitStrokePath(state, doc.mid(0, i + 1), settingsFor(zoom),
                                                false);
        }
        const AnimeCore::Path released =
            AnimeVectorLogic::liveFitStrokePath(state, doc, settingsFor(zoom), true);
        check(jointStats(released).maxKinkDeg <= 20.0,
              "released small circle must stay G1-smooth at 1/4 zoom");
    }

    // Deliberate corners must SURVIVE the cap: a square drawn zoomed out
    // keeps exactly its three interior ~90 degree joints (the fourth corner
    // is the open start/end), at 1x and at 1/4 zoom.
    for (double zoom : {1.0, 0.25}) {
        const double side = 60.0 * zoom;
        const int perSide = std::max(6, int(side / 2.0));
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(screenSquare(side, perSide, 0.30), zoom);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(zoom)));
        check(s.cornersOver45Deg == 3, "square must keep exactly its 3 interior corners");
        check(s.maxKinkDeg > 60.0, "square corners must stay sharp");
    }

    // A small V drawn zoomed out keeps its single apex corner at EVERY
    // corner-slider setting - the arc cap must never soften a real corner.
    for (int corner : {0, 50, 100}) {
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(screenV(10.0, 12, 0.25), 0.25);
        const JointStats s =
            jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(0.25, corner)));
        check(s.cornersOver45Deg == 1, "V must keep exactly its apex corner at every setting");
    }

    // A long stroke (past the live fitter's bake threshold) is untouched by
    // the cap: the frozen-prefix pipeline still hands over G1 seams.
    {
        const double zoom = 0.25;
        std::vector<AnimeCore::PointF> sweep;
        for (int i = 0; i <= 200; ++i) {
            const double t = i / 200.0;
            AnimeCore::PointF p(400.0 * t, 140.0 * std::sin(2.0 * kPi * t));
            if (i != 0 && i != 200) {
                p += tremor(t * 100.0, 0.30);
            }
            sweep.push_back(p);
        }
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(sweep, zoom);
        AnimeLiveFitState state;
        for (int i = 3; i < doc.size(); i += 4) {
            AnimeVectorLogic::liveFitStrokePath(state, doc.mid(0, i + 1), settingsFor(zoom),
                                                false);
        }
        const AnimeCore::Path released =
            AnimeVectorLogic::liveFitStrokePath(state, doc, settingsFor(zoom), true);
        check(jointStats(released).maxKinkDeg <= 20.0,
              "long sweep through the live bake must stay G1");
    }

    // A tight round bend that is only PART of a stroke - the turn of a
    // hairpin, the loop of a 6 - must stay smooth as well. The arc cap does
    // not reach these (the stroke is far longer than the bend); before the
    // bend test nearly all of them came back cut at the bend, with
    // joints of 65 to 140 degrees. Five hands, three zooms.
    for (double zoom : {2.0, 1.0, 0.25}) {
        for (int hand = 0; hand < 5; ++hand) {
            for (double radius : {4.0, 5.0, 6.0, 8.0}) {
                ScreenPath path(hand);
                path.line(30.0).arc(180.0, radius).line(30.0);
                const JointStats s = jointStats(
                    AnimeVectorLogic::fitStrokePath(capture(path.events, zoom), settingsFor(zoom)));
                check(s.curveSegments >= 1, "a U-turn inside a stroke must be fitted with curves");
                check(s.maxKinkDeg <= 20.0, "a U-turn inside a stroke must stay G1-smooth");
            }
            for (double radius : {4.0, 5.0, 8.0, 10.0}) {
                ScreenPath path(hand);
                path.turn(90.0).line(35.0).arc(-350.0, radius);
                const JointStats s = jointStats(
                    AnimeVectorLogic::fitStrokePath(capture(path.events, zoom), settingsFor(zoom)));
                check(s.curveSegments >= 1, "the loop of a 6 must be fitted with curves");
                check(s.maxKinkDeg <= 20.0, "the loop of a 6 must stay G1-smooth");
            }
        }
    }

    // ...at every corner-slider setting: the slider says how sharp a turn
    // has to be, not whether a round bend is one. (Before, no setting helped:
    // the threshold and the window move together.)
    for (int corner : {0, 50, 100}) {
        for (int hand = 0; hand < 5; ++hand) {
            ScreenPath path(hand);
            path.line(30.0).arc(180.0, 6.0).line(30.0);
            const JointStats s = jointStats(
                AnimeVectorLogic::fitStrokePath(capture(path.events, 1.0), settingsFor(1.0, corner)));
            check(s.maxKinkDeg <= 20.0, "a U-turn must stay smooth at every corner setting");
        }
    }

    // The same bend on a stroke long enough to bake: the live fitter's
    // chunks run the same corner test, so the release must be smooth too.
    for (int hand = 0; hand < 5; ++hand) {
        ScreenPath path(hand);
        path.line(150.0).arc(180.0, 6.0).line(150.0);
        const AnimeCore::Vector<AnimeCore::PointF> doc = capture(path.events, 1.0);
        AnimeLiveFitState state;
        for (int i = 3; i < doc.size(); i += 4) {
            AnimeVectorLogic::liveFitStrokePath(state, doc.mid(0, i + 1), settingsFor(1.0), false);
        }
        const AnimeCore::Path released =
            AnimeVectorLogic::liveFitStrokePath(state, doc, settingsFor(1.0), true);
        check(jointStats(released).maxKinkDeg <= 20.0,
              "a U-turn on a long stroke must stay smooth through the live bake");
    }

    // Real corners keep their place right next to round bends: the corner of
    // a 5 (leg, corner, bowl), the cusp of a 3 (bowl, cusp, bowl), a zigzag.
    // The bowls turn the same way as the 5's corner and the other way from
    // the 3's cusp; neither may take the corner with it, and neither may be
    // cut itself.
    for (double zoom : {2.0, 1.0, 0.25}) {
        for (int hand = 0; hand < 5; ++hand) {
            ScreenPath five(hand);
            five.turn(90.0).line(25.0).turn(-90.0).arc(200.0, 10.0);
            const JointStats s5 = jointStats(
                AnimeVectorLogic::fitStrokePath(capture(five.events, zoom), settingsFor(zoom)));
            check(s5.cornersOver45Deg == 1, "a 5 must keep exactly the corner before its bowl");

            ScreenPath three(hand);
            three.arc(200.0, 8.0).turn(-160.0).arc(200.0, 8.0);
            const JointStats s3 = jointStats(
                AnimeVectorLogic::fitStrokePath(capture(three.events, zoom), settingsFor(zoom)));
            check(s3.cornersOver45Deg == 1, "a 3 must keep exactly the cusp between its bowls");
            check(s3.maxKinkDeg > 60.0, "the cusp of a 3 must stay sharp");

            ScreenPath zigzag(hand);
            zigzag.line(14.0).turn(90.0).line(14.0).turn(-90.0).line(14.0).turn(90.0).line(14.0)
                .turn(-90.0).line(14.0);
            const JointStats sz = jointStats(
                AnimeVectorLogic::fitStrokePath(capture(zigzag.events, zoom), settingsFor(zoom)));
            check(sz.cornersOver45Deg == 4, "a zigzag must keep all four corners");
        }
    }

    // The recorded hand. The loops of the 6s carry no corner at all; the 2,
    // the 5 and the 3 keep the one corner each really has (the foot of the
    // 2, the corner before the 5's bowl, the cusp of the 3) and lose the
    // cuts on their bowls.
    {
        const AnimeStrokeFitSettings settings = settingsFor(kRecordedZoom);
        check(jointStats(AnimeVectorLogic::fitStrokePath(recorded(kRecordedSixA), settings)).maxKinkDeg
                  <= 20.0,
              "recorded 6 (first) must have a smooth loop");
        check(jointStats(AnimeVectorLogic::fitStrokePath(recorded(kRecordedSixB), settings)).maxKinkDeg
                  <= 20.0,
              "recorded 6 (second) must have a smooth loop");
        check(jointStats(AnimeVectorLogic::fitStrokePath(recorded(kRecordedTwo), settings))
                      .cornersOver45Deg
                  == 1,
              "recorded 2 must keep only the corner at its foot");
        check(jointStats(AnimeVectorLogic::fitStrokePath(recorded(kRecordedFive), settings))
                      .cornersOver45Deg
                  == 1,
              "recorded 5 must keep only the corner before its bowl");
        const JointStats three =
            jointStats(AnimeVectorLogic::fitStrokePath(recorded(kRecordedThree), settings));
        check(three.cornersOver45Deg == 1, "recorded 3 must keep only its cusp");
        check(three.maxKinkDeg > 100.0, "the cusp of the recorded 3 must stay sharp");
    }

    // A rounded corner is still a corner: its turn is spread over several
    // samples, but the stroke does not go on turning beside it. This is the
    // half of the bend test that the share of the core alone gets wrong - a
    // right angle with a 5 px fillet lost its corner in 18 of 20 hands on
    // that test alone. First what the real Pen hands over, then fillets.
    check(jointStats(AnimeVectorLogic::fitStrokePath(recorded(kPenRightAngle), settingsFor(1.0)))
                  .cornersOver45Deg
              == 1,
          "a right angle rounded by the stabilizer must keep its corner");
    for (double zoom : {2.0, 1.0, 0.25}) {
        for (int hand = 0; hand < 5; ++hand) {
            for (double fillet : {3.0, 4.0, 5.0}) {
                ScreenPath path(hand);
                path.line(30.0).arc(90.0, fillet).line(30.0);
                const JointStats s = jointStats(
                    AnimeVectorLogic::fitStrokePath(capture(path.events, zoom), settingsFor(zoom)));
                check(s.cornersOver45Deg == 1, "a rounded right angle must keep its corner");
            }
        }
    }

    if (failures == 0) {
        std::printf("strokefit tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d strokefit check(s) failed\n", failures);
    return 1;
}
