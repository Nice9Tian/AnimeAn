#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include "algorithm/animemodel.h"
#include "algorithm/vectorlogic.h"

#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QVector>

class PaintOpenGLWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    enum class Tool {
        Pen,
        Eraser,
        DeleteLine,
        Fill
    };

    enum class FillScope {
        CurrentLayer,
        AllLayers
    };

    explicit PaintOpenGLWidget(QWidget *parent = nullptr);

    void setPenColor(const QColor &color);
    void setDrawingColor(const QColor &color);
    void setPenWidth(qreal width);
    void setTool(Tool tool);
    Tool tool() const;
    void setFillScope(FillScope scope);
    void setSmoothValue(int value);
    void setCurrentLayer(int layerIndex);
    void setCurrentFrame(int frameIndex);
    int layerCount() const;
    int frameCount() const;
    int assetCount() const;
    QString layerName(int layerIndex) const;
    QString frameName(int frameIndex) const;
    QString assetName(int assetIndex) const;
    int importRasterLayer(const QImage &image, const QString &layerName);
    int addLayer();
    bool deleteLayer(int layerIndex);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    bool deleteFrame(int frameIndex);
    bool moveFrame(int fromIndex, int toIndex);
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector, const QString &name = QString());
    void setCurrentAsset(int assetIndex);
    bool assignAssetToLayer(int layerIndex, int assetIndex);
    int addLayerForAsset(int assetIndex);
    AnimeSceneModel &model();
    const AnimeSceneModel &model() const;

signals:
    void layerListChanged(int selectedLayer);
    void assetListChanged(int selectedAsset);

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public:
    using VectorStroke = AnimeVectorStroke;
    using VectorGroupId = AnimeVectorGroupId;
    using VectorStrokeNode = AnimeVectorStrokeNode;
    using VectorImageModel = AnimeVectorImageModel;

private:
    void updateCurrentStroke();
    void finishCurrentStroke();
    bool eraseAt(const QPointF &pos);
    bool eraseBetween(const QPointF &from, const QPointF &to);
    bool deleteLineAt(const QPointF &pos);
    bool deleteLineBetween(const QPointF &from, const QPointF &to);
    bool fillAt(const QPointF &pos);
    bool currentLayerAcceptsFill() const;
    QVector<QLineF> fillGraphSegments(FillScope scope, int layerIndex) const;
    QPainterPath vectorRegionPathAt(const QPointF &seed, FillScope scope, int layerIndex) const;
    void removeInvalidFillRegions();
    bool eraseStrokeAt(int strokeIndex, const QPointF &pos);
    bool eraseStrokeBetween(int strokeIndex, const QPointF &from, const QPointF &to);
    VectorStroke makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id = 0, bool filterInput = true, bool smoothPath = true) const;
    bool appendPoint(const QPointF &point);
    VectorImageModel *currentImage(bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;

    Tool m_tool = Tool::Pen;
    FillScope m_fillScope = FillScope::CurrentLayer;
    QColor m_penColor = Qt::black;
    QVector<QPointF> m_points;
    AnimeSceneModel m_model;
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

#endif // OPENGLWIDGET_H
