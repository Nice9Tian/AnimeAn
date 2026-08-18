#ifndef ANIMEMODEL_H
#define ANIMEMODEL_H

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

struct AnimeVectorStroke {
    int id = 0;
    QString property;
    QVector<QPointF> points;
    QVector<qreal> lengths;
    qreal totalLength = 0.0;
    QPainterPath path;
    QRectF bounds;
    QColor color;
    qreal width = 3.0;
    // Qt::PenStyle as an int (1 = SolidLine, 2 = DashLine, ...). Generic
    // per-stroke line style; rendering clamps invalid values to solid.
    int penStyle = 1;
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
    QString property;
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
    void translate(const QPointF &delta);
    void remapFillSourceLayersAfterDelete(int deletedLayerIndex);
    void remapFillSourceLayersAfterMove(int fromIndex, int toIndex);
    bool setFillRegionAt(int index, const AnimeVectorFillRegion &fill);
    bool setFillRegionPath(int index, const QPainterPath &path);
    bool setFillRegionColor(int index, const QColor &color);
    bool removeFillRegionAt(int index);
    void removeStrokeAt(int index);
    int replaceStrokeWithPieces(int index, const QVector<AnimeVectorStroke> &pieces);
    void clear();

private:
    void rebuildBounds();

    QVector<AnimeVectorStrokeNode> m_strokes;
    QVector<AnimeVectorFillRegion> m_fills;
    AnimeRasterImage m_raster;
    QRectF m_bounds;
};

struct AnimeCell {
    int assetIndex = -1;
    int frameId = 0;

    bool isEmpty() const { return assetIndex < 0 || frameId <= 0; }
};

enum class AnimeColumnType {
    Vector,
    Raster,
    Fill
};

class AnimeAsset {
public:
    QString name;
    AnimeColumnType type = AnimeColumnType::Vector;
    // Backing asset of a script-owned working layer: excluded from the asset
    // panel and project files, same contract as AnimeColumn::internal.
    bool internal = false;
    AnimeVectorImageModel *frame(int frameId, bool create);
    const AnimeVectorImageModel *frame(int frameId) const;
    QVector<int> frameIds() const;

private:
    QMap<int, AnimeVectorImageModel> m_frames;
};

class AnimeColumn {
public:
    QString name;
    // Stable identity, handed out by the scene and never reused. The layer
    // GROUP tree references columns by this rather than by index, so adding,
    // deleting or reordering layers needs no index fix-up in the tree.
    // 0 means "not assigned yet" (legacy project, or a column appended by a
    // path that predates ids); normalizeLayerTree fills those in.
    int id = 0;
    AnimeColumnType type = AnimeColumnType::Vector;
    bool visible = true;
    bool locked = false;
    qreal opacity = 1.0;
    // Ephemeral working layer owned by a script (e.g. a tool preview): renders
    // normally (on top of regular columns) but is excluded from the layer
    // panel, selection attention, and project files. Never persisted.
    bool internal = false;

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

// One entry in the layer group tree. A node is EITHER a leaf naming a column
// (layerId > 0) or a group (groupId > 0) holding an ordered child list; groups
// nest arbitrarily. This is the "nested integer array" scheme: an element that
// is a plain id is a layer, an element with children is a group, and the id is
// looked up in the column table for the name, asset and everything else.
//
// The tree is a GROUPING view, not the z-order. Draw order stays the column
// order (paintGL walks columns last-to-first), so grouping never silently
// restacks a drawing; a group's members are simply the layers it names.
struct AnimeLayerNode {
    int layerId = 0;
    int groupId = 0;
    QString name;          // groups only
    bool collapsed = false; // groups only
    QVector<AnimeLayerNode> children;

    bool isGroup() const { return groupId > 0; }
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
    AnimeScene();

    QString id() const;
    void setId(const QString &id);
    QString textId() const;
    void setTextId(const QString &id);
    int intId() const;
    void setIntId(int id);

    QVector<AnimeAsset> assets;
    AnimeXsheet xsheet;
    // Layer group tree (top level). Reconciled against the columns by
    // AnimeSceneModel::normalizeLayerTree, so it survives any layer edit and
    // any project file that predates it.
    QVector<AnimeLayerNode> layerTree;
    int nextColumnId = 1;
    int nextGroupId = 1;
    // The page. A DOCUMENT property, not a view one: it decides where the
    // paper is, what an export covers and what a bucket fill is bounded by,
    // none of which may change because someone resized the window.
    QSize canvasSize = QSize(1280, 720);
    // Timeline playback rate. 24 is "on ones"; 12 (the default) is on twos.
    int playbackFps = 12;
    // Opaque storage for script-side (Python) state that must travel with the
    // scene: copied into history snapshots and saved with the project. The
    // C++ side never interprets it.
    QString scriptData;

private:
    QString m_textId;
    int m_intId = 0;
};

class AnimeSceneModel {
public:
    AnimeSceneModel();

    const AnimeScene &scene() const;
    AnimeScene &scene();
    QString id() const;
    void setId(const QString &id);
    QString textId() const;
    void setTextId(const QString &id);
    int intId() const;
    void setIntId(int id);

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
    QString uniqueLayerName(const QString &baseName, int excludeLayerIndex = -1, int excludeAssetIndex = -1) const;
    QString frameName(int frameIndex) const;
    QString assetName(int assetIndex) const;
    void setAssetName(int assetIndex, const QString &name);
    AnimeColumnType assetType(int assetIndex) const;
    bool assetInternal(int assetIndex) const;
    void setAssetInternal(int assetIndex, bool internal);

    bool layerVisible(int layerIndex) const;
    void setLayerVisible(int layerIndex, bool visible);
    bool layerInternal(int layerIndex) const;
    void setLayerInternal(int layerIndex, bool internal);
    bool layerLocked(int layerIndex) const;
    void setLayerLocked(int layerIndex, bool locked);
    qreal layerOpacity(int layerIndex) const;
    void setLayerOpacity(int layerIndex, qreal opacity);
    AnimeColumnType layerType(int layerIndex) const;
    bool isFillLayer(int layerIndex) const;

    QString scriptData() const;
    void setScriptData(const QString &data);

    QSize canvasSize() const;
    // Clamped to a sane range; a zero or negative page would make the view
    // and every bounds-driven algorithm degenerate.
    void setCanvasSize(const QSize &size);
    static QSize defaultCanvasSize();

    // --- layer groups -----------------------------------------------------
    // The tree is RECONCILED rather than maintained in lockstep: every read
    // assigns ids to new columns, drops leaves whose column is gone, and
    // appends columns nobody has grouped yet. That is what lets addLayer,
    // deleteLayer, moveLayer, project loading and history snapshots stay
    // untouched by grouping.
    void normalizeLayerTree();
    const QVector<AnimeLayerNode> &layerTree() const;
    // Replaces the whole tree (then reconciles it, so anything the caller
    // forgot to mention simply reappears ungrouped rather than vanishing).
    void setLayerTree(const QVector<AnimeLayerNode> &tree);
    int layerIdAt(int layerIndex) const;
    int layerIndexForId(int layerId) const;
    // Members are given as layer INDICES and existing group ids; both are
    // detached from wherever they sit now and moved into the new group, which
    // takes the position of the first member. Returns the new group id, or 0
    // if nothing valid was named.
    int createLayerGroup(const QString &name,
                         const QVector<int> &layerIndices,
                         const QVector<int> &groupIds,
                         bool collapsed = false);
    bool setLayerGroupCollapsed(int groupId, bool collapsed);
    bool setLayerGroupName(int groupId, const QString &name);
    // Removes the group itself, splicing its children into its parent.
    bool dissolveLayerGroup(int groupId);
    // Every column id at or below this group, in tree order.
    QVector<int> layerIdsInGroup(int groupId) const;
    // Deletes the group AND the layers inside it (subgroups included),
    // remapping fill sources as it goes. Returns how many layers went.
    int deleteLayerGroup(int groupId);
    bool layerGroupCollapsed(int groupId) const;

    int addLayer();
    int addFillLayer();
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector, const QString &name = QString());
    bool deleteAsset(int assetIndex);
    bool deleteLayer(int layerIndex);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    // A HELD frame: the new row reuses the previous row's cells verbatim, so
    // both rows resolve to the SAME drawing. Editing on either shows on both,
    // which is the whole point of a hold - it is one exposure shown twice,
    // not a copy that can drift.
    int addHoldFrame();
    // True when this row holds the one above it: it has content and every
    // non-empty cell is the same cell as the row above. Derived rather than
    // flagged, so it stays honest if the user later repoints a cell.
    bool isHoldFrame(int frameIndex) const;
    bool deleteFrame(int frameIndex);

    int playbackFps() const;
    void setPlaybackFps(int fps);
    bool moveFrame(int fromIndex, int toIndex);

    AnimeCell cellAt(int row, int layerIndex) const;
    void setCell(int row, int layerIndex, const AnimeCell &cell);
    void clearCell(int row, int layerIndex);

    AnimeVectorImageModel *imageAt(int row, int layerIndex, bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    const AnimeVectorImageModel *imageAt(int row, int layerIndex) const;
    AnimeVectorImageModel *assetImage(int assetIndex, bool create);
    AnimeVectorImageModel *assetImage(int assetIndex, int frameId, bool create);
    const AnimeVectorImageModel *assetImage(int assetIndex) const;
    const AnimeVectorImageModel *assetImage(int assetIndex, int frameId) const;
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

    // Boundary geometry for region filling: every stroke segment on visible,
    // non-fill, non-internal columns at the given frame. layerIndex >= 0
    // restricts the walls to that single column.
    QVector<QLineF> fillBoundarySegments(int frame, int layerIndex = -1) const;
    // The bounding rect of exactly those segments. A bucket fill is clipped
    // against the ARTWORK, so the clip has to come from the same walls the
    // trace runs on - a bound that disagreed with them would slice regions
    // the tracer legitimately found.
    QRectF fillBoundaryBounds(int frame, int layerIndex = -1) const;

private:
    // normalizeLayerTree is logically const (it only reconciles bookkeeping),
    // but it writes, so the const readers go through this.
    void normalizeLayerTreeInternal();

    AnimeScene m_scene;
    int m_currentLayer = 0;
    int m_currentFrame = 0;
    int m_currentAsset = -1;
};

#endif // ANIMEMODEL_H
