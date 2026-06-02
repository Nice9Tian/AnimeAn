#ifndef PAINTOPENGLWIDGET_H
#define PAINTOPENGLWIDGET_H

#include <QColor>
#include <QMap>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QRectF>
#include <QString>
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
    void setCurrentLayer(int layerIndex);
    void setCurrentFrame(int frameIndex);
    int layerCount() const;
    int frameCount() const;
    QString layerName(int layerIndex) const;
    QString frameName(int frameIndex) const;

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

    struct VectorGroupId {
        QVector<int> ids;

        bool isGrouped() const { return !ids.isEmpty(); }
        int depth() const { return ids.size(); }
    };

    struct VectorStrokeNode {
        VectorStroke stroke;
        VectorGroupId groupId;
        bool isPoint = false;
        bool isNewForFill = true;
        bool selected = false;
    };

    class VectorImageModel {
    public:
        const QVector<VectorStrokeNode> &strokeNodes() const;
        int strokeCount() const;
        const VectorStroke &strokeAt(int index) const;
        const VectorStrokeNode &strokeNodeAt(int index) const;
        QRectF bounds() const;
        void addStroke(const VectorStroke &stroke);
        void addStrokeNode(const VectorStrokeNode &node);
        void removeStrokeAt(int index);
        void replaceStrokeWithPieces(int index, const QVector<VectorStroke> &pieces);
        void clear();

    private:
        void rebuildBounds();

        QVector<VectorStrokeNode> m_strokes;
        QRectF m_bounds;
    };

    struct AnimeCell {
        int levelIndex = -1;
        int frameId = 0;

        bool isEmpty() const { return levelIndex < 0 || frameId <= 0; }
    };

    class AnimeLevel {
    public:
        QString name;
        VectorImageModel *frame(int frameId, bool create);
        const VectorImageModel *frame(int frameId) const;
        QVector<int> frameIds() const;

    private:
        QMap<int, VectorImageModel> m_frames;
    };

    class AnimeColumn {
    public:
        QString name;
        bool visible = true;
        bool locked = false;
        qreal opacity = 1.0;

        AnimeCell cellAt(int row) const;
        void setCell(int row, const AnimeCell &cell);
        int maxRow() const;

    private:
        QVector<AnimeCell> m_cells;
        int m_firstRow = 0;
    };

    class AnimeXsheet {
    public:
        QVector<AnimeColumn> columns;
        int frameCount = 1;

        AnimeCell cellAt(int row, int column) const;
        void setCell(int row, int column, const AnimeCell &cell);
        void ensureColumnCount(int count);
        void ensureFrameCount(int count);
    };

    class AnimeScene {
    public:
        QVector<AnimeLevel> levels;
        AnimeXsheet xsheet;
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
    void initializeScene(int layerCount, int frameCount);
    VectorImageModel *currentImage(bool create);
    const VectorImageModel *imageForCell(const AnimeCell &cell) const;
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;
    int ensureLevelForCurrentColumn();

    Tool m_tool = Tool::Pen;
    QColor m_penColor = Qt::black;
    QVector<QPointF> m_points;
    AnimeScene m_scene;
    int m_currentLayer = 0;
    int m_currentFrame = 0;
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
