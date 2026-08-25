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
QPointF tremor(double t, double amplitudeScreenPx)
{
    return QPointF(amplitudeScreenPx
                       * (0.6 * std::sin(12.9898 * t) + 0.4 * std::sin(78.233 * t + 1.3)),
                   amplitudeScreenPx
                       * (0.6 * std::cos(37.719 * t) + 0.4 * std::sin(24.653 * t + 2.1)));
}

// Screen-space pen events -> document points, with the same capture floor
// appendPoint applies (m_minPointDistance = 2 screen px, zoom clamped by the
// shared wheel in algorithm/viewscale.h).
QVector<QPointF> capture(const std::vector<QPointF> &screenSamples, double zoom)
{
    const double minDocDistance = 2.0 * AnimeViewScale::pixelScale(zoom);
    QVector<QPointF> doc;
    for (const QPointF &s : screenSamples) {
        const QPointF p(s.x() / zoom, s.y() / zoom);
        if (doc.isEmpty() || QLineF(doc.last(), p).length() >= minDocDistance) {
            doc.append(p);
        }
    }
    return doc;
}

std::vector<QPointF> screenCircle(double radiusScreenPx, int events, double tremorAmp)
{
    std::vector<QPointF> out;
    for (int i = 0; i <= events; ++i) {
        const double a = 2.0 * kPi * i / events;
        QPointF p(radiusScreenPx * std::cos(a), radiusScreenPx * std::sin(a));
        if (i != 0 && i != events) {
            p += tremor(a * radiusScreenPx, tremorAmp);
        }
        out.push_back(p);
    }
    return out;
}

std::vector<QPointF> screenSquare(double sideScreenPx, int eventsPerSide, double tremorAmp)
{
    std::vector<QPointF> out;
    const QPointF corners[5] = {QPointF(0, 0), QPointF(sideScreenPx, 0),
                                QPointF(sideScreenPx, sideScreenPx), QPointF(0, sideScreenPx),
                                QPointF(0, 0)};
    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < eventsPerSide; ++i) {
            const double t = double(i) / eventsPerSide;
            QPointF p = corners[side] + (corners[side + 1] - corners[side]) * t;
            if (!(side == 0 && i == 0)) {
                p += tremor((side * eventsPerSide + i) * 0.7, tremorAmp);
            }
            out.push_back(p);
        }
    }
    out.push_back(corners[4]);
    return out;
}

std::vector<QPointF> screenV(double armScreenPx, int eventsPerArm, double tremorAmp)
{
    std::vector<QPointF> out;
    for (int i = 0; i <= eventsPerArm; ++i) {
        QPointF p(armScreenPx * i / eventsPerArm, armScreenPx - armScreenPx * i / eventsPerArm);
        if (i) {
            p += tremor(i * 0.9, tremorAmp);
        }
        out.push_back(p);
    }
    for (int i = 1; i <= eventsPerArm; ++i) {
        QPointF p(armScreenPx + armScreenPx * i / eventsPerArm, armScreenPx * i / eventsPerArm);
        p += tremor((eventsPerArm + i) * 0.9, tremorAmp);
        out.push_back(p);
    }
    return out;
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
JointStats jointStats(const QPainterPath &path)
{
    JointStats stats;
    QPointF current;
    QPointF lastDir;
    bool haveDir = false;
    int i = 0;
    while (i < path.elementCount()) {
        const QPainterPath::Element e = path.elementAt(i);
        QPointF to, inDir, outDir;
        if (e.type == QPainterPath::MoveToElement) {
            current = QPointF(e.x, e.y);
            haveDir = false;
            ++i;
            continue;
        }
        if (e.type == QPainterPath::LineToElement) {
            to = QPointF(e.x, e.y);
            inDir = outDir = to - current;
            ++stats.lineSegments;
            ++i;
        } else {
            const QPointF c1(e.x, e.y);
            const QPointF c2(path.elementAt(i + 1).x, path.elementAt(i + 1).y);
            to = QPointF(path.elementAt(i + 2).x, path.elementAt(i + 2).y);
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
        const QVector<QPointF> doc = capture(screenCircle(30.0 * zoom, events, 0.30), zoom);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(zoom)));
        check(s.curveSegments >= 1, "small circle must contain curve segments at every zoom");
        check(s.maxKinkDeg <= 20.0, "small circle must stay G1-smooth at every zoom");
    }

    // Sparse capture of the same kind of circle (fast drawing: few events).
    // The window walk used to overshoot on sparse samples and double the
    // measured turn, cornering these even harder.
    for (int events : {12, 16, 24}) {
        const QVector<QPointF> doc = capture(screenCircle(10.0, events, 0.15), 0.25);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(0.25)));
        check(s.curveSegments >= 1, "sparse circle must contain curve segments");
        check(s.maxKinkDeg <= 20.0, "sparse circle must stay G1-smooth");
    }

    // The release path is liveFitStrokePath over the final buffer: feed it
    // incrementally like move events, then release. Same smoothness bar.
    {
        const double zoom = 0.25;
        const QVector<QPointF> doc = capture(screenCircle(7.5, 48, 0.30), zoom);
        AnimeLiveFitState state;
        for (int i = 3; i < doc.size(); i += 4) {
            AnimeVectorLogic::liveFitStrokePath(state, doc.mid(0, i + 1), settingsFor(zoom),
                                                false);
        }
        const QPainterPath released =
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
        const QVector<QPointF> doc = capture(screenSquare(side, perSide, 0.30), zoom);
        const JointStats s = jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(zoom)));
        check(s.cornersOver45Deg == 3, "square must keep exactly its 3 interior corners");
        check(s.maxKinkDeg > 60.0, "square corners must stay sharp");
    }

    // A small V drawn zoomed out keeps its single apex corner at EVERY
    // corner-slider setting - the arc cap must never soften a real corner.
    for (int corner : {0, 50, 100}) {
        const QVector<QPointF> doc = capture(screenV(10.0, 12, 0.25), 0.25);
        const JointStats s =
            jointStats(AnimeVectorLogic::fitStrokePath(doc, settingsFor(0.25, corner)));
        check(s.cornersOver45Deg == 1, "V must keep exactly its apex corner at every setting");
    }

    // A long stroke (past the live fitter's bake threshold) is untouched by
    // the cap: the frozen-prefix pipeline still hands over G1 seams.
    {
        const double zoom = 0.25;
        std::vector<QPointF> sweep;
        for (int i = 0; i <= 200; ++i) {
            const double t = i / 200.0;
            QPointF p(400.0 * t, 140.0 * std::sin(2.0 * kPi * t));
            if (i != 0 && i != 200) {
                p += tremor(t * 100.0, 0.30);
            }
            sweep.push_back(p);
        }
        const QVector<QPointF> doc = capture(sweep, zoom);
        AnimeLiveFitState state;
        for (int i = 3; i < doc.size(); i += 4) {
            AnimeVectorLogic::liveFitStrokePath(state, doc.mid(0, i + 1), settingsFor(zoom),
                                                false);
        }
        const QPainterPath released =
            AnimeVectorLogic::liveFitStrokePath(state, doc, settingsFor(zoom), true);
        check(jointStats(released).maxKinkDeg <= 20.0,
              "long sweep through the live bake must stay G1");
    }

    if (failures == 0) {
        std::printf("strokefit tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d strokefit check(s) failed\n", failures);
    return 1;
}
