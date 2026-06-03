#ifndef ANIMEMODEL_H
#define ANIMEMODEL_H

#include <QColor>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

struct AnimeVectorStroke {
    int id = 0;
    QVector<QPointF> points;
    QVector<qreal> lengths;
    qreal totalLength = 0.0;
    QPainterPath path;
    QRectF bounds;
    QColor color;
    qreal width = 3.0;
};

struct AnimeVectorGroupId {
    QVector<int> ids;

    bool isGrouped() const { return !ids.isEmpty(); }
    int depth() const { return ids.size(); }
};

struct AnimeVectorStrokeNode {
    AnimeVectorStroke stroke;
    AnimeVectorGroupId groupId;
    bool isPoint = false;
    bool isNewForFill = true;
    bool selected = false;
};

struct AnimeVectorFillRegion {
    int id = 0;
    QPainterPath path;
    QRectF bounds;
    QColor color;
    int sourceLayerIndex = -1;
    bool basedOnAllLayers = false;
};

class AnimeVectorImageModel {
public:
    const QVector<AnimeVectorStrokeNode> &strokeNodes() const;
    const QVector<AnimeVectorFillRegion> &fillRegions() const;
    int strokeCount() const;
    int fillCount() const;
    const AnimeVectorStroke &strokeAt(int index) const;
    const AnimeVectorStrokeNode &strokeNodeAt(int index) const;
    QRectF bounds() const;
    void addStroke(const AnimeVectorStroke &stroke);
    void addStrokeNode(const AnimeVectorStrokeNode &node);
    void addFillRegion(const AnimeVectorFillRegion &fill);
    void removeStrokeAt(int index);
    void replaceStrokeWithPieces(int index, const QVector<AnimeVectorStroke> &pieces);
    void clear();

private:
    void rebuildBounds();

    QVector<AnimeVectorStrokeNode> m_strokes;
    QVector<AnimeVectorFillRegion> m_fills;
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
    AnimeVectorImageModel *frame(int frameId, bool create);
    const AnimeVectorImageModel *frame(int frameId) const;
    void removeFrame(int frameId);
    QVector<int> frameIds() const;

private:
    QMap<int, AnimeVectorImageModel> m_frames;
};

class AnimeColumn {
public:
    QString name;
    bool visible = true;
    bool locked = false;
    qreal opacity = 1.0;

    AnimeCell cellAt(int row) const;
    void setCell(int row, const AnimeCell &cell);
    void insertCell(int row);
    void removeCell(int row);
    void moveCell(int fromRow, int toRow);
    int maxRow() const;

private:
    QVector<AnimeCell> denseCells(int rowCount) const;
    void setDenseCells(const QVector<AnimeCell> &cells);

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

class AnimeSceneModel {
public:
    AnimeSceneModel();

    const AnimeScene &scene() const;
    AnimeScene &scene();

    void initializeScene(int layerCount, int frameCount);
    void setCurrentLayer(int layerIndex);
    void setCurrentFrame(int frameIndex);
    int currentLayer() const;
    int currentFrame() const;

    int layerCount() const;
    int frameCount() const;
    QString layerName(int layerIndex) const;
    void setLayerName(int layerIndex, const QString &name);
    QString frameName(int frameIndex) const;

    bool layerVisible(int layerIndex) const;
    void setLayerVisible(int layerIndex, bool visible);
    bool layerLocked(int layerIndex) const;
    void setLayerLocked(int layerIndex, bool locked);
    qreal layerOpacity(int layerIndex) const;
    void setLayerOpacity(int layerIndex, qreal opacity);

    int addLayer();
    bool deleteLayer(int layerIndex);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    bool deleteFrame(int frameIndex);
    bool moveFrame(int fromIndex, int toIndex);

    AnimeCell cellAt(int row, int layerIndex) const;
    void setCell(int row, int layerIndex, const AnimeCell &cell);
    void clearCell(int row, int layerIndex);

    AnimeVectorImageModel *imageAt(int row, int layerIndex, bool create);
    const AnimeVectorImageModel *imageAt(int row, int layerIndex) const;
    AnimeVectorImageModel *currentImage(bool create);
    const AnimeVectorImageModel *imageForCell(const AnimeCell &cell) const;
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;

private:
    int ensureLevelForCurrentColumn();
    int ensureLevelForColumn(int columnIndex);
    bool cellIsReferenced(const AnimeCell &cell) const;
    void pruneUnusedLevels();
    int nextFrameId() const;

    AnimeScene m_scene;
    int m_currentLayer = 0;
    int m_currentFrame = 0;
};

#endif // ANIMEMODEL_H
