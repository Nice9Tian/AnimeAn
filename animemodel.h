#ifndef ANIMEMODEL_H
#define ANIMEMODEL_H

#include <QColor>
#include <QImage>
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
    QPointF seedPoint;
    QPainterPath path;
    QRectF bounds;
    QColor color;
    int sourceLayerIndex = -1;
    bool basedOnAllLayers = false;
};

struct AnimeRasterImage {
    QImage image;
    QPointF topLeft;

    bool isEmpty() const { return image.isNull(); }
    QRectF bounds() const { return QRectF(topLeft, image.size()); }
};

class AnimeVectorImageModel {
public:
    const QVector<AnimeVectorStrokeNode> &strokeNodes() const;
    const QVector<AnimeVectorFillRegion> &fillRegions() const;
    int strokeCount() const;
    int fillCount() const;
    bool hasRaster() const;
    const AnimeRasterImage &raster() const;
    const AnimeVectorStroke &strokeAt(int index) const;
    const AnimeVectorStrokeNode &strokeNodeAt(int index) const;
    QRectF bounds() const;
    void setRasterImage(const QImage &image, const QPointF &topLeft = QPointF());
    void clearRasterImage();
    void addStroke(const AnimeVectorStroke &stroke);
    void addStrokeNode(const AnimeVectorStrokeNode &node);
    void addFillRegion(const AnimeVectorFillRegion &fill);
    void remapFillSourceLayersAfterDelete(int deletedLayerIndex);
    void remapFillSourceLayersAfterMove(int fromIndex, int toIndex);
    bool setFillRegionAt(int index, const AnimeVectorFillRegion &fill);
    bool setFillRegionPath(int index, const QPainterPath &path);
    bool setFillRegionColor(int index, const QColor &color);
    bool removeFillRegionAt(int index);
    void removeStrokeAt(int index);
    void replaceStrokeWithPieces(int index, const QVector<AnimeVectorStroke> &pieces);
    void clear();

private:
    void rebuildBounds();

    QVector<AnimeVectorStrokeNode> m_strokes;
    QVector<AnimeVectorFillRegion> m_fills;
    AnimeRasterImage m_raster;
    QRectF m_bounds;
};

struct AnimeCell {
    int levelIndex = -1;
    int frameId = 0;

    bool isEmpty() const { return levelIndex < 0 || frameId <= 0; }
};

enum class AnimeColumnType {
    Vector,
    Raster,
    Fill
};

class AnimeLevel {
public:
    QString name;
    AnimeColumnType type = AnimeColumnType::Vector;
    AnimeVectorImageModel *frame(int frameId, bool create);
    const AnimeVectorImageModel *frame(int frameId) const;
    QVector<int> frameIds() const;

private:
    QMap<int, AnimeVectorImageModel> m_frames;
};

class AnimeColumn {
public:
    QString name;
    AnimeColumnType type = AnimeColumnType::Vector;
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
    void setCurrentAsset(int assetIndex);
    int currentLayer() const;
    int currentFrame() const;
    int currentAsset() const;

    int layerCount() const;
    int frameCount() const;
    int assetCount() const;
    QString layerName(int layerIndex) const;
    void setLayerName(int layerIndex, const QString &name);
    QString frameName(int frameIndex) const;
    QString assetName(int assetIndex) const;
    AnimeColumnType assetType(int assetIndex) const;

    bool layerVisible(int layerIndex) const;
    void setLayerVisible(int layerIndex, bool visible);
    bool layerLocked(int layerIndex) const;
    void setLayerLocked(int layerIndex, bool locked);
    qreal layerOpacity(int layerIndex) const;
    void setLayerOpacity(int layerIndex, qreal opacity);
    AnimeColumnType layerType(int layerIndex) const;
    bool isFillLayer(int layerIndex) const;

    int addLayer();
    int addFillLayer();
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector, const QString &name = QString());
    bool deleteLayer(int layerIndex);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    bool deleteFrame(int frameIndex);
    bool moveFrame(int fromIndex, int toIndex);

    AnimeCell cellAt(int row, int layerIndex) const;
    void setCell(int row, int layerIndex, const AnimeCell &cell);
    void clearCell(int row, int layerIndex);

    AnimeVectorImageModel *imageAt(int row, int layerIndex, bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    const AnimeVectorImageModel *imageAt(int row, int layerIndex) const;
    AnimeVectorImageModel *assetImage(int assetIndex, bool create);
    const AnimeVectorImageModel *assetImage(int assetIndex) const;
    int assetIndexAt(int row, int layerIndex) const;
    bool assignAssetToLayer(int row, int layerIndex, int assetIndex);
    int addLayerForAsset(int row, int assetIndex);
    AnimeVectorImageModel *currentImage(bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    const AnimeVectorImageModel *imageForCell(const AnimeCell &cell) const;
    bool setRasterImageAt(int row, int layerIndex, const QImage &image, const QPointF &topLeft);
    int addRasterLayer(const QString &name, int frameIndex, const QImage &image, const QPointF &topLeft);
    void remapFillSourceLayersAfterDelete(int deletedLayerIndex);
    void remapFillSourceLayersAfterMove(int fromIndex, int toIndex);
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;

private:
    AnimeScene m_scene;
    int m_currentLayer = 0;
    int m_currentFrame = 0;
    int m_currentAsset = -1;
};

#endif // ANIMEMODEL_H
