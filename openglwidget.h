#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include "algorithm/animemodel.h"
#include "algorithm/vectorlogic.h"

#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QString>
#include <QVariant>
#include <QVector>

class QPainter;

class PaintOpenGLWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    enum class Tool {
        Pen,
        Eraser,
        DeleteLine,
        Fill,
        Move
    };

    enum class FillScope {
        CurrentLayer,
        AllLayers
    };

    struct ImportedVectorStroke {
        QVector<QPointF> points;
        QPainterPath path;
        QColor color = Qt::black;
        qreal width = 3.0;
    };

    struct ImportedVectorFrame {
        int row = 0;
        QVector<ImportedVectorStroke> strokes;
    };

    struct OverlayItem {
        QString id;
        QVector<QPointF> points;
        bool closed = false;
        QColor strokeColor = QColor(0, 0, 0, 255);
        QColor fillColor = QColor(0, 0, 0, 0);
        qreal width = 3.0;
        bool removable = true;
    };

    explicit PaintOpenGLWidget(QWidget *parent = nullptr);

    void setOverlayItems(const QVector<OverlayItem> &items);

    void setViewName(const QString &name);
    QString viewName() const;
    void setPenColor(const QColor &color);
    void setDrawingColor(const QColor &color);
    void setPenWidth(qreal width);
    void setStrokeProperty(const QString &property);
    void sendPythonExtraToolMessage(const QString &name, const QString &property);
    void sendPythonToolOptionMessage(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn);
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
    int importVectorLineLayer(const QVector<ImportedVectorFrame> &frames, const QString &layerName);
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
    void pythonDebugMessage(const QString &message);
    void focusGained();

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

public:
    using VectorStroke = AnimeVectorStroke;
    using VectorGroupId = AnimeVectorGroupId;
    using VectorStrokeNode = AnimeVectorStrokeNode;
    using VectorImageModel = AnimeVectorImageModel;

private:
    struct OverlayHandle {
        QString id;
        QColor badgeColor;
        QRectF rect;
    };

    void paintOverlayItems(QPainter &painter);
    QRectF overlayHandleRect(const QRectF &bounds) const;
    bool removeOverlayItemAt(const QPointF &pos);
    void sendOverlayRemoveMessage(const QString &overlayId);
    void updateCurrentStroke();
    void finishCurrentStroke();
    void pythonHookSendMessage(const QString &event, const QPointF &pos = QPointF(), const QPointF &delta = QPointF(), bool changed = true, int strokeIndex = -1);
    bool eraseAt(const QPointF &pos);
    bool eraseBetween(const QPointF &from, const QPointF &to);
    bool deleteLineAt(const QPointF &pos);
    bool deleteLineBetween(const QPointF &from, const QPointF &to);
    bool fillAt(const QPointF &pos);
    bool moveCurrentLayerBy(const QPointF &delta);
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
    QString m_viewName = QStringLiteral("main");
    QColor m_penColor = Qt::black;
    QVector<QPointF> m_points;
    AnimeSceneModel m_model;
    VectorStroke m_currentStroke;
    bool m_hasCurrentStroke = false;
    QPointF m_hoverPos;
    bool m_hasHoverPos = false;
    QPointF m_lastEraserPos;
    bool m_hasLastEraserPos = false;
    QPointF m_lastMovePos;
    bool m_hasLastMovePos = false;
    qreal m_penWidth = 5.0;
    QString m_strokeProperty;
    QString m_activePythonTool;
    bool m_eraseGestureChanged = false;
    bool m_moveGestureChanged = false;
    qreal m_eraserRadius = 12.0;
    qreal m_minPointDistance = 2.0;
    int m_smoothValue = 50;
    QVector<OverlayItem> m_overlayItems;
    QVector<OverlayHandle> m_overlayHandles;
};

#endif // OPENGLWIDGET_H
