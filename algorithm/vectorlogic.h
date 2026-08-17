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
QPainterPath fitStrokePath(const QVector<QPointF> &points, int smoothValue);
AnimeVectorStroke makeStroke(const QVector<QPointF> &points,
                             const QColor &color,
                             qreal width,
                             int id = 0,
                             bool filterInput = true,
                             bool smoothPath = true,
                             int smoothValue = 50);
AnimeVectorStroke makeStrokeFromPath(const QPainterPath &path,
                                     const QVector<QPointF> &points,
                                     const QColor &color,
                                     qreal width,
                                     int id = 0);

bool strokeHitsCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius);
bool strokeHitsCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius);
QVector<AnimeVectorRange> keepRangesForCircle(const AnimeVectorStroke &stroke, const QPointF &center, qreal radius);
QVector<AnimeVectorRange> keepRangesForCapsule(const AnimeVectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius);
QVector<AnimeVectorRange> complementRanges(const QVector<AnimeVectorRange> &eraseRanges);
AnimeVectorStroke subStroke(const AnimeVectorStroke &stroke, qreal fromW, qreal toW, int smoothValue = 50);
QPointF pointAtLength(const AnimeVectorStroke &stroke, qreal length);

QVector<QLineF> segmentsFromPath(const QPainterPath &path);
QVector<AnimeVectorRegionFace> computeVectorRegionFaces(const QVector<QLineF> &segments);
QPainterPath vectorRegionPathAt(const QPointF &seed, const QVector<QLineF> &segments, const QRect &canvasRect);
QPainterPath fillPathFromMask(const QPoint &seed, const QImage &boundary);
}

#endif // VECTORLOGIC_H
