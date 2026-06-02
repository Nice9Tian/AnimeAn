#ifndef PAINTOPENGLWIDGET_H
#define PAINTOPENGLWIDGET_H

#include <QColor>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QRectF>
#include <QVector>

class PaintOpenGLWidget : public QOpenGLWidget
{
public:
    enum class Tool {
        Pen,
        Eraser,
        DeleteLine
    };

    explicit PaintOpenGLWidget(QWidget *parent = nullptr);

    void setPenColor(const QColor &color);
    void setTool(Tool tool);
    void setSmoothValue(int value);

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public:
    struct Range {
        qreal first = 0.0;
        qreal second = 1.0;
    };

    struct VectorStroke {
        int id = 0;
        QVector<QPointF> points;
        QVector<qreal> lengths;
        qreal totalLength = 0.0;
        QPainterPath path;
        QRectF bounds;
        QColor color;
        qreal width = 3.0;
    };

    class VectorImageModel {
    public:
        const QVector<VectorStroke> &strokes() const;
        int strokeCount() const;
        const VectorStroke &strokeAt(int index) const;
        QRectF bounds() const;
        void addStroke(const VectorStroke &stroke);
        void removeStrokeAt(int index);
        void replaceStrokeWithPieces(int index, const QVector<VectorStroke> &pieces);
        void clear();

    private:
        void rebuildBounds();

        QVector<VectorStroke> m_strokes;
        QRectF m_bounds;
    };

private:
    QPainterPath makeVectorPath(const QVector<QPointF> &points) const;
    QPainterPath makePolylinePath(const QVector<QPointF> &points) const;
    QVector<QPointF> filteredPoints(const QVector<QPointF> &points) const;
    void updateCurrentStroke();
    void finishCurrentStroke();
    bool eraseAt(const QPointF &pos);
    bool eraseBetween(const QPointF &from, const QPointF &to);
    bool deleteLineAt(const QPointF &pos);
    bool deleteLineBetween(const QPointF &from, const QPointF &to);
    bool strokeHitsCircle(const VectorStroke &stroke, const QPointF &center, qreal radius) const;
    bool strokeHitsCapsule(const VectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius) const;
    bool eraseStrokeAt(int strokeIndex, const QPointF &pos);
    bool eraseStrokeBetween(int strokeIndex, const QPointF &from, const QPointF &to);
    QVector<Range> keepRangesForCircle(const VectorStroke &stroke, const QPointF &center, qreal radius) const;
    QVector<Range> keepRangesForCapsule(const VectorStroke &stroke, const QPointF &from, const QPointF &to, qreal radius) const;
    QVector<Range> complementRanges(const QVector<Range> &eraseRanges) const;
    VectorStroke makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id = 0, bool filterInput = true, bool smoothPath = true) const;
    VectorStroke subStroke(const VectorStroke &stroke, qreal fromW, qreal toW) const;
    QPointF pointAtLength(const VectorStroke &stroke, qreal length) const;
    bool appendPoint(const QPointF &point);

    Tool m_tool = Tool::Pen;
    QColor m_penColor = Qt::black;
    QVector<QPointF> m_points;
    VectorImageModel m_image;
    VectorStroke m_currentStroke;
    bool m_hasCurrentStroke = false;
    QPointF m_hoverPos;
    bool m_hasHoverPos = false;
    QPointF m_lastEraserPos;
    bool m_hasLastEraserPos = false;
    qreal m_penWidth = 5.0;
    qreal m_eraserRadius = 12.0;
    qreal m_minPointDistance = 2.0;
    int m_smoothValue = 50;
};

#endif // PAINTOPENGLWIDGET_H
