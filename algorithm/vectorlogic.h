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

struct AnimeVectorRegionFace {
    QPainterPath path;
    qreal signedArea = 0.0;
};

namespace AnimeVectorLogic {
qreal epsilon();

QVector<QPointF> filteredPoints(const QVector<QPointF> &points);
QPainterPath makeSmoothedPath(const QVector<QPointF> &points, int smoothValue);
QPainterPath makePolylinePath(const QVector<QPointF> &points);
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
