#include "animemodel.h"
#include "algorithm/vectorlogic.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <climits>
#include <functional>
#include <cmath>

namespace {
int &nextSceneIntIdValue()
{
    static int nextId = 1;
    return nextId;
}

int nextSceneIntId()
{
    int &nextId = nextSceneIntIdValue();
    return nextId++;
}

void reserveSceneIntId(int id)
{
    int &nextId = nextSceneIntIdValue();
    if (id >= nextId) {
        nextId = id + 1;
    }
}

QString defaultSceneName(int id)
{
    return QStringLiteral("Scene %1").arg(id);
}

int maxInt(int a, int b)
{
    return a > b ? a : b;
}

// The duplicate suffix alphabet: 0 -> "a", 25 -> "z", 26 -> "aa". Bijective
// base 26, so every index has exactly one spelling and no "-" run repeats.
QString duplicateSuffix(int index)
{
    QString suffix;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) {
        suffix.prepend(QChar(QLatin1Char('a' + ((n - 1) % 26))));
    }
    return suffix;
}

// The stem a duplicate chain hangs off: a name that already ends in a
// duplicate suffix names the SAME drawing family, so "3-b" duplicates to
// "3-c" rather than "3-b-a".
QString duplicateBaseName(const QString &name)
{
    static const QRegularExpression suffixPattern(QStringLiteral("-[a-z]+$"));
    const QRegularExpressionMatch match = suffixPattern.match(name);
    return match.hasMatch() ? name.left(match.capturedStart()) : name;
}

QString nextNumberedColumnName(const QVector<AnimeColumn> &columns, const QString &baseName)
{
    const QString prefix = baseName + QLatin1Char(' ');
    int nextNumber = 1;
    for (const AnimeColumn &column : columns) {
        if (!column.name.startsWith(prefix)) {
            continue;
        }

        bool ok = false;
        const int number = column.name.mid(prefix.size()).toInt(&ok);
        if (ok && number >= nextNumber) {
            nextNumber = number + 1;
        }
    }
    return QStringLiteral("%1 %2").arg(baseName).arg(nextNumber);
}

bool columnNameExists(const QVector<AnimeColumn> &columns, const QString &name, int excludeIndex)
{
    for (int i = 0; i < columns.size(); ++i) {
        if (i != excludeIndex && columns[i].name == name) {
            return true;
        }
    }
    return false;
}

QString assetTypeName(AnimeColumnType type)
{
    switch (type) {
    case AnimeColumnType::Raster:
        return QStringLiteral("Raster");
    case AnimeColumnType::Fill:
        return QStringLiteral("Fill");
    case AnimeColumnType::Vector:
    default:
        return QStringLiteral("Vector");
    }
}
}

const QVector<AnimeVectorStrokeNode> &AnimeVectorImageModel::strokeNodes() const
{
    return m_strokes;
}

const QVector<AnimeVectorFillRegion> &AnimeVectorImageModel::fillRegions() const
{
    return m_fills;
}

int AnimeVectorImageModel::strokeCount() const
{
    return m_strokes.size();
}

int AnimeVectorImageModel::fillCount() const
{
    return m_fills.size();
}

bool AnimeVectorImageModel::hasRaster() const
{
    return !m_raster.isEmpty();
}

const AnimeRasterImage &AnimeVectorImageModel::raster() const
{
    return m_raster;
}

const AnimeVectorStroke &AnimeVectorImageModel::strokeAt(int index) const
{
    return m_strokes[index].stroke;
}

const AnimeVectorStrokeNode &AnimeVectorImageModel::strokeNodeAt(int index) const
{
    return m_strokes[index];
}

QRectF AnimeVectorImageModel::bounds() const
{
    return m_bounds;
}

void AnimeVectorImageModel::setRasterImage(const QImage &image, const QPointF &topLeft)
{
    m_raster.image = image;
    m_raster.topLeft = topLeft;
    rebuildBounds();
}

void AnimeVectorImageModel::clearRasterImage()
{
    m_raster = AnimeRasterImage();
    rebuildBounds();
}

void AnimeVectorImageModel::addStroke(const AnimeVectorStroke &stroke)
{
    AnimeVectorStrokeNode node;
    node.stroke = stroke;
    addStrokeNode(node);
}

void AnimeVectorImageModel::addStrokeNode(const AnimeVectorStrokeNode &node)
{
    m_strokes.append(node);
    if (m_bounds.isNull()) {
        m_bounds = node.stroke.bounds;
    } else {
        m_bounds = m_bounds.united(node.stroke.bounds);
    }
}

void AnimeVectorImageModel::addFillRegion(const AnimeVectorFillRegion &fill)
{
    m_fills.append(fill);
    if (m_bounds.isNull()) {
        m_bounds = fill.bounds;
    } else {
        m_bounds = m_bounds.united(fill.bounds);
    }
}

void AnimeVectorImageModel::translate(const QPointF &delta)
{
    if (delta.isNull()) {
        return;
    }

    if (!m_raster.isEmpty()) {
        m_raster.topLeft += delta;
    }
    for (AnimeVectorFillRegion &fill : m_fills) {
        fill.seedPoint += delta;
        fill.path.translate(delta);
        fill.bounds.translate(delta);
    }
    for (AnimeVectorStrokeNode &node : m_strokes) {
        for (QPointF &point : node.stroke.points) {
            point += delta;
        }
        node.stroke.path.translate(delta);
        node.stroke.bounds.translate(delta);
    }
    rebuildBounds();
}

void AnimeVectorImageModel::transform(const QTransform &matrix)
{
    if (matrix.isIdentity()) {
        return;
    }

    // Widths are a LENGTH, not a coordinate: they follow the transform's
    // mean linear scale (sqrt of |det|), so a uniformly scaled drawing keeps
    // its line weight in proportion and a pure translation leaves it alone.
    const qreal determinant = std::abs(matrix.m11() * matrix.m22() - matrix.m12() * matrix.m21());
    const qreal widthScale = determinant > 0.0 ? std::sqrt(determinant) : 1.0;

    if (!m_raster.isEmpty()) {
        // The raster's PLACEMENT and its SIZE both follow the matrix: mapping
        // only the corner left a scaled layer with a full-size bitmap
        // hanging off it, so the scale grips read as no-ops on a raster.
        const QRectF mapped = matrix.mapRect(m_raster.bounds());
        const QSize target(qMax(1, qRound(mapped.width())),
                           qMax(1, qRound(mapped.height())));
        if (target != m_raster.image.size() && !m_raster.image.isNull()) {
            m_raster.image = m_raster.image.scaled(target, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
        }
        // A negative determinant is a mirror; the pixels have to flip with
        // the box or the image reads back-to-front inside a correct frame.
        const bool flipX = matrix.m11() < 0.0;
        const bool flipY = matrix.m22() < 0.0;
        if ((flipX || flipY) && !m_raster.image.isNull()) {
            m_raster.image = m_raster.image.mirrored(flipX, flipY);
        }
        m_raster.topLeft = mapped.topLeft();
    }
    for (AnimeVectorFillRegion &fill : m_fills) {
        fill.seedPoint = matrix.map(fill.seedPoint);
        fill.path = matrix.map(fill.path);
        fill.bounds = fill.path.boundingRect();
    }
    for (AnimeVectorStrokeNode &node : m_strokes) {
        for (QPointF &point : node.stroke.points) {
            point = matrix.map(point);
        }
        if (widthScale > 0.0 && !qFuzzyCompare(widthScale, qreal(1.0))) {
            // Floored at a hair rather than at 0.1: the Transfer tool applies
            // a drag as a chain of incremental matrices, and a saturating
            // clamp is not invertible - shrinking and growing back left every
            // stroke permanently fattened.
            node.stroke.width = std::max(qreal(1e-4), node.stroke.width * widthScale);
        }
        node.stroke.path = matrix.map(node.stroke.path);
        if (!node.stroke.path.isEmpty()) {
            // PADDED by the stroke width, like every other producer of these
            // bounds: they are the cull box for erase/delete/cut and for the
            // fill-boundary extent, and an unpadded rect is EMPTY for an
            // axis-aligned stroke - which made such a stroke uneraseable.
            const qreal pad = node.stroke.width;
            node.stroke.bounds = node.stroke.path.boundingRect()
                                     .adjusted(-pad, -pad, pad, pad);
        }
        // Arc-length tables are geometry, so they scale with it; leaving them
        // describing the pre-transform stroke made every arc-length consumer
        // (splitting, sampling, the mapping) read a scaled stroke wrong.
        if (widthScale > 0.0 && !qFuzzyCompare(widthScale, qreal(1.0))) {
            for (qreal &length : node.stroke.lengths) {
                length *= widthScale;
            }
            node.stroke.totalLength *= widthScale;
        }
    }
    rebuildBounds();
}

void AnimeVectorImageModel::remapFillSourceLayersAfterDelete(int deletedLayerIndex)
{
    for (AnimeVectorFillRegion &fill : m_fills) {
        if (fill.sourceLayerIndex == deletedLayerIndex) {
            fill.sourceLayerIndex = -1;
        } else if (fill.sourceLayerIndex > deletedLayerIndex) {
            --fill.sourceLayerIndex;
        }
    }
}

void AnimeVectorImageModel::remapFillSourceLayersAfterMove(int fromIndex, int toIndex)
{
    for (AnimeVectorFillRegion &fill : m_fills) {
        if (fill.sourceLayerIndex == fromIndex) {
            fill.sourceLayerIndex = toIndex;
        } else if (fromIndex < toIndex &&
                   fill.sourceLayerIndex > fromIndex &&
                   fill.sourceLayerIndex <= toIndex) {
            --fill.sourceLayerIndex;
        } else if (toIndex < fromIndex &&
                   fill.sourceLayerIndex >= toIndex &&
                   fill.sourceLayerIndex < fromIndex) {
            ++fill.sourceLayerIndex;
        }
    }
}

bool AnimeVectorImageModel::setFillRegionAt(int index, const AnimeVectorFillRegion &fill)
{
    if (index < 0 || index >= m_fills.size()) {
        return false;
    }

    m_fills[index] = fill;
    rebuildBounds();
    return true;
}

bool AnimeVectorImageModel::setFillRegionPath(int index, const QPainterPath &path)
{
    if (index < 0 || index >= m_fills.size()) {
        return false;
    }

    m_fills[index].path = path;
    m_fills[index].bounds = path.boundingRect();
    rebuildBounds();
    return true;
}

bool AnimeVectorImageModel::setFillRegionColor(int index, const QColor &color)
{
    if (index < 0 || index >= m_fills.size()) {
        return false;
    }

    m_fills[index].color = color;
    return true;
}

bool AnimeVectorImageModel::removeFillRegionAt(int index)
{
    if (index < 0 || index >= m_fills.size()) {
        return false;
    }

    m_fills.removeAt(index);
    rebuildBounds();
    return true;
}

void AnimeVectorImageModel::removeStrokeAt(int index)
{
    if (index < 0 || index >= m_strokes.size()) {
        return;
    }

    m_strokes.removeAt(index);
    rebuildBounds();
}

int AnimeVectorImageModel::replaceStrokeWithPieces(int index, const QVector<AnimeVectorStroke> &pieces)
{
    // Returns the number of pieces actually inserted, or -1 for an invalid
    // index, so callers can tell a real replacement from a silent no-op
    // (degenerate pieces are dropped) before e.g. committing history.
    if (index < 0 || index >= m_strokes.size()) {
        return -1;
    }

    m_strokes.removeAt(index);
    int inserted = 0;
    for (int i = pieces.size() - 1; i >= 0; --i) {
        if (pieces[i].points.size() >= 2 && pieces[i].totalLength > 0.0001) {
            AnimeVectorStrokeNode node;
            node.stroke = pieces[i];
            m_strokes.insert(index, node);
            ++inserted;
        }
    }
    rebuildBounds();
    return inserted;
}

void AnimeVectorImageModel::clear()
{
    m_strokes.clear();
    m_fills.clear();
    m_raster = AnimeRasterImage();
    m_bounds = QRectF();
}

void AnimeVectorImageModel::rebuildBounds()
{
    m_bounds = QRectF();
    if (!m_raster.isEmpty()) {
        m_bounds = m_raster.bounds();
    }
    for (const AnimeVectorFillRegion &fill : m_fills) {
        if (m_bounds.isNull()) {
            m_bounds = fill.bounds;
        } else {
            m_bounds = m_bounds.united(fill.bounds);
        }
    }
    for (const AnimeVectorStrokeNode &node : m_strokes) {
        if (m_bounds.isNull()) {
            m_bounds = node.stroke.bounds;
        } else {
            m_bounds = m_bounds.united(node.stroke.bounds);
        }
    }
}

AnimeVectorImageModel *AnimeAsset::frame(int frameId, bool create)
{
    if (frameId <= 0) {
        return nullptr;
    }

    auto it = m_frames.find(frameId);
    if (it == m_frames.end()) {
        if (!create) {
            return nullptr;
        }
        it = m_frames.insert(frameId, AnimeVectorImageModel());
    }
    return &it.value();
}

const AnimeVectorImageModel *AnimeAsset::frame(int frameId) const
{
    if (frameId <= 0) {
        return nullptr;
    }

    auto it = m_frames.constFind(frameId);
    if (it == m_frames.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

QVector<int> AnimeAsset::frameIds() const
{
    return m_frames.keys().toVector();
}

AnimeCell AnimeColumn::cellAt(int row) const
{
    if (row < 0 || row < m_firstRow || row >= m_firstRow + m_cells.size()) {
        return AnimeCell();
    }
    return m_cells[row - m_firstRow];
}

void AnimeColumn::setCell(int row, const AnimeCell &cell)
{
    if (row < 0) {
        return;
    }

    if (m_cells.isEmpty()) {
        if (!cell.isEmpty()) {
            m_firstRow = row;
            m_cells.append(cell);
        }
        return;
    }

    const int lastRow = m_firstRow + m_cells.size() - 1;
    if (row < m_firstRow) {
        if (cell.isEmpty()) {
            return;
        }
        const int delta = m_firstRow - row;
        m_cells.insert(0, delta - 1, AnimeCell());
        m_cells.insert(0, cell);
        m_firstRow = row;
        return;
    }

    if (row > lastRow) {
        if (cell.isEmpty()) {
            return;
        }
        const int gap = row - lastRow - 1;
        for (int i = 0; i < gap; ++i) {
            m_cells.append(AnimeCell());
        }
        m_cells.append(cell);
        return;
    }

    m_cells[row - m_firstRow] = cell;
    while (!m_cells.isEmpty() && m_cells.last().isEmpty()) {
        m_cells.removeLast();
    }
    while (!m_cells.isEmpty() && m_cells.first().isEmpty()) {
        m_cells.removeFirst();
        ++m_firstRow;
    }
    if (m_cells.isEmpty()) {
        m_firstRow = 0;
    }
}

void AnimeColumn::insertCell(int row)
{
    if (row < 0) {
        return;
    }

    QVector<AnimeCell> cells = denseCells(maxInt(maxRow() + 1, row + 1));
    cells.insert(row, AnimeCell());
    setDenseCells(cells);
}

void AnimeColumn::removeCell(int row)
{
    if (row < 0) {
        return;
    }

    QVector<AnimeCell> cells = denseCells(maxInt(maxRow() + 1, row + 1));
    if (row >= cells.size()) {
        return;
    }
    cells.removeAt(row);
    setDenseCells(cells);
}

void AnimeColumn::moveCell(int fromRow, int toRow)
{
    if (fromRow < 0 || toRow < 0 || fromRow == toRow) {
        return;
    }

    QVector<AnimeCell> cells = denseCells(maxInt(maxRow() + 1, maxInt(fromRow + 1, toRow + 1)));
    if (fromRow >= cells.size() || toRow >= cells.size()) {
        return;
    }

    const AnimeCell cell = cells.takeAt(fromRow);
    cells.insert(toRow, cell);
    setDenseCells(cells);
}

int AnimeColumn::maxRow() const
{
    if (m_cells.isEmpty()) {
        return -1;
    }
    return m_firstRow + m_cells.size() - 1;
}

QVector<AnimeCell> AnimeColumn::denseCells(int rowCount) const
{
    QVector<AnimeCell> cells;
    cells.resize(maxInt(0, rowCount));
    for (int row = 0; row < cells.size(); ++row) {
        cells[row] = cellAt(row);
    }
    return cells;
}

void AnimeColumn::setDenseCells(const QVector<AnimeCell> &cells)
{
    m_cells.clear();
    m_firstRow = 0;

    int first = 0;
    while (first < cells.size() && cells[first].isEmpty()) {
        ++first;
    }
    if (first >= cells.size()) {
        return;
    }

    int last = cells.size() - 1;
    while (last >= first && cells[last].isEmpty()) {
        --last;
    }

    m_firstRow = first;
    for (int row = first; row <= last; ++row) {
        m_cells.append(cells[row]);
    }
}

AnimeCell AnimeXsheet::cellAt(int row, int column) const
{
    if (row < 0 || column < 0 || column >= columns.size()) {
        return AnimeCell();
    }
    return columns[column].cellAt(row);
}

void AnimeXsheet::setCell(int row, int column, const AnimeCell &cell)
{
    if (row < 0 || column < 0) {
        return;
    }
    ensureColumnCount(column + 1);
    columns[column].setCell(row, cell);
    if (!cell.isEmpty() && row >= frameCount) {
        frameCount = row + 1;
    }
}

void AnimeXsheet::ensureColumnCount(int count)
{
    while (columns.size() < count) {
        AnimeColumn column;
        column.name = nextNumberedColumnName(columns, QStringLiteral("Layer"));
        columns.append(column);
    }
}

void AnimeXsheet::ensureFrameCount(int count)
{
    if (count > frameCount) {
        frameCount = count;
    }
}

void AnimeXsheet::shiftFrameNamesForInsert(int row)
{
    if (row < 0 || frameNames.isEmpty()) {
        return;
    }
    QHash<int, QString> shifted;
    shifted.reserve(frameNames.size());
    for (auto it = frameNames.constBegin(); it != frameNames.constEnd(); ++it) {
        shifted.insert(it.key() >= row ? it.key() + 1 : it.key(), it.value());
    }
    frameNames = shifted;
}

void AnimeXsheet::shiftFrameNamesForDelete(int row)
{
    if (row < 0 || frameNames.isEmpty()) {
        return;
    }
    QHash<int, QString> shifted;
    shifted.reserve(frameNames.size());
    for (auto it = frameNames.constBegin(); it != frameNames.constEnd(); ++it) {
        if (it.key() == row) {
            continue;   // the row is gone, and so is what it was called
        }
        shifted.insert(it.key() > row ? it.key() - 1 : it.key(), it.value());
    }
    frameNames = shifted;
}

void AnimeXsheet::shiftFrameNamesForMove(int fromRow, int toRow)
{
    if (fromRow < 0 || toRow < 0 || fromRow == toRow || frameNames.isEmpty()) {
        return;
    }
    // Same take-then-insert the cells go through, so the name lands wherever
    // its row landed: remove `fromRow`, then re-index against the insert slot.
    const bool named = frameNames.contains(fromRow);
    const QString moved = frameNames.value(fromRow);
    QHash<int, QString> shifted;
    shifted.reserve(frameNames.size());
    for (auto it = frameNames.constBegin(); it != frameNames.constEnd(); ++it) {
        if (it.key() == fromRow) {
            continue;
        }
        int row = it.key() > fromRow ? it.key() - 1 : it.key();
        if (row >= toRow) {
            ++row;
        }
        shifted.insert(row, it.value());
    }
    if (named) {
        shifted.insert(toRow, moved);
    }
    frameNames = shifted;
}

AnimeScene::AnimeScene()
    : m_intId(nextSceneIntId())
{
    m_textId = defaultSceneName(m_intId);
}

QString AnimeScene::id() const
{
    return textId();
}

void AnimeScene::setId(const QString &id)
{
    setTextId(id);
}

QString AnimeScene::textId() const
{
    return m_textId;
}

void AnimeScene::setTextId(const QString &id)
{
    const QString name = id.trimmed();
    m_textId = name.isEmpty() ? defaultSceneName(m_intId) : name;
}

int AnimeScene::intId() const
{
    return m_intId;
}

void AnimeScene::setIntId(int id)
{
    if (id <= 0) {
        m_intId = nextSceneIntId();
        return;
    }
    m_intId = id;
    reserveSceneIntId(id);
}

AnimeSceneModel::AnimeSceneModel()
{
    initializeScene(2, 2);
}

const AnimeScene &AnimeSceneModel::scene() const
{
    return m_scene;
}

AnimeScene &AnimeSceneModel::scene()
{
    return m_scene;
}

QString AnimeSceneModel::id() const
{
    return textId();
}

void AnimeSceneModel::setId(const QString &id)
{
    setTextId(id);
}

QString AnimeSceneModel::textId() const
{
    return m_scene.textId();
}

void AnimeSceneModel::setTextId(const QString &id)
{
    m_scene.setTextId(id);
}

int AnimeSceneModel::intId() const
{
    return m_scene.intId();
}

void AnimeSceneModel::setIntId(int id)
{
    m_scene.setIntId(id);
}

void AnimeSceneModel::initializeScene(int layerCount, int frameCount)
{
    m_scene = AnimeScene();
    m_scene.xsheet.ensureColumnCount(layerCount);
    m_scene.xsheet.ensureFrameCount(frameCount);

    m_currentLayer = m_scene.xsheet.columns.isEmpty() ? -1 : 0;
    m_currentFrame = 0;
    m_currentAsset = -1;
    normalizeLayerTreeInternal();
}

void AnimeSceneModel::setCurrentLayer(int layerIndex)
{
    if (layerIndex < 0) {
        m_currentLayer = -1;
        m_currentAsset = -1;
        return;
    }
    m_scene.xsheet.ensureColumnCount(layerIndex + 1);
    m_currentLayer = layerIndex;
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
}

void AnimeSceneModel::setCurrentFrame(int frameIndex)
{
    if (frameIndex < 0) {
        m_currentFrame = -1;
        m_currentLayer = -1;
        m_currentAsset = -1;
        return;
    }
    m_scene.xsheet.ensureFrameCount(frameIndex + 1);
    m_currentFrame = frameIndex;
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
}

void AnimeSceneModel::setCurrentAsset(int assetIndex)
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        m_currentAsset = -1;
        return;
    }
    m_currentAsset = assetIndex;
}

int AnimeSceneModel::currentLayer() const
{
    return m_currentLayer;
}

int AnimeSceneModel::currentFrame() const
{
    return m_currentFrame;
}

int AnimeSceneModel::currentAsset() const
{
    return m_currentAsset;
}

int AnimeSceneModel::layerCount() const
{
    return m_scene.xsheet.columns.size();
}

int AnimeSceneModel::frameCount() const
{
    return m_scene.xsheet.frameCount;
}

int AnimeSceneModel::assetCount() const
{
    return m_scene.assets.size();
}

QString AnimeSceneModel::layerName(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return QString();
    }
    return m_scene.xsheet.columns[layerIndex].name;
}

void AnimeSceneModel::setLayerName(int layerIndex, const QString &name)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    const int assetIndex = assetIndexAt(m_currentFrame, layerIndex);
    const QString uniqueName = uniqueLayerName(name, layerIndex, assetIndex);
    m_scene.xsheet.columns[layerIndex].name = uniqueName;
    if (assetIndex >= 0 && assetIndex < m_scene.assets.size()) {
        m_scene.assets[assetIndex].name = uniqueName;
    }
}

QString AnimeSceneModel::uniqueLayerName(const QString &baseName, int excludeLayerIndex, int excludeAssetIndex) const
{
    QString candidate = baseName.trimmed();
    if (candidate.isEmpty()) {
        candidate = QStringLiteral("Layer");
    }

    QString uniqueName = candidate;
    int number = 1;
    auto exists = [&](const QString &name) {
        if (columnNameExists(m_scene.xsheet.columns, name, excludeLayerIndex)) {
            return true;
        }
        for (int i = 0; i < m_scene.assets.size(); ++i) {
            if (i != excludeAssetIndex && m_scene.assets[i].name == name) {
                return true;
            }
        }
        return false;
    };

    while (exists(uniqueName)) {
        uniqueName = QStringLiteral("%1%2").arg(candidate).arg(number);
        ++number;
    }
    return uniqueName;
}

QString AnimeSceneModel::frameName(int frameIndex) const
{
    if (frameIndex < 0) {
        return QString();
    }
    const auto stored = m_scene.xsheet.frameNames.constFind(frameIndex);
    if (stored != m_scene.xsheet.frameNames.constEnd() && !stored->isEmpty()) {
        return *stored;
    }
    return QString::number(frameIndex + 1);
}

void AnimeSceneModel::setFrameName(int frameIndex, const QString &name)
{
    if (frameIndex < 0) {
        return;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QString::number(frameIndex + 1)) {
        m_scene.xsheet.frameNames.remove(frameIndex);
        return;
    }
    m_scene.xsheet.frameNames.insert(frameIndex, trimmed);
}

QString AnimeSceneModel::nextDuplicateName(int frameIndex) const
{
    if (frameIndex < 0) {
        return QString();
    }
    const QString base = duplicateBaseName(frameName(frameIndex));
    QSet<QString> taken;
    taken.reserve(m_scene.xsheet.frameCount);
    for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
        taken.insert(frameName(row));
    }
    for (int index = 0;; ++index) {
        const QString candidate = base + QLatin1Char('-') + duplicateSuffix(index);
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
}

QString AnimeSceneModel::assetName(int assetIndex) const
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return QString();
    }
    return m_scene.assets[assetIndex].name;
}

void AnimeSceneModel::setAssetName(int assetIndex, const QString &name)
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return;
    }
    const QString uniqueName = uniqueLayerName(name, -1, assetIndex);
    m_scene.assets[assetIndex].name = uniqueName;
    for (int layerIndex = 0; layerIndex < m_scene.xsheet.columns.size(); ++layerIndex) {
        for (int row = 0; row < frameCount(); ++row) {
            if (assetIndexAt(row, layerIndex) == assetIndex) {
                m_scene.xsheet.columns[layerIndex].name = uniqueName;
                break;
            }
        }
    }
}

bool AnimeSceneModel::assetInternal(int assetIndex) const
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return false;
    }
    return m_scene.assets[assetIndex].internal;
}

void AnimeSceneModel::setAssetInternal(int assetIndex, bool internal)
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return;
    }
    m_scene.assets[assetIndex].internal = internal;
}

AnimeColumnType AnimeSceneModel::assetType(int assetIndex) const
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return AnimeColumnType::Vector;
    }
    return m_scene.assets[assetIndex].type;
}

bool AnimeSceneModel::layerVisible(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return false;
    }
    return m_scene.xsheet.columns[layerIndex].visible;
}

void AnimeSceneModel::setLayerVisible(int layerIndex, bool visible)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    m_scene.xsheet.columns[layerIndex].visible = visible;
}

bool AnimeSceneModel::layerInternal(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return false;
    }
    return m_scene.xsheet.columns[layerIndex].internal;
}

void AnimeSceneModel::setLayerInternal(int layerIndex, bool internal)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    m_scene.xsheet.columns[layerIndex].internal = internal;
}

bool AnimeSceneModel::layerLocked(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return false;
    }
    return m_scene.xsheet.columns[layerIndex].locked;
}

void AnimeSceneModel::setLayerLocked(int layerIndex, bool locked)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    m_scene.xsheet.columns[layerIndex].locked = locked;
}

qreal AnimeSceneModel::layerOpacity(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return 0.0;
    }
    return m_scene.xsheet.columns[layerIndex].opacity;
}

void AnimeSceneModel::setLayerOpacity(int layerIndex, qreal opacity)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    if (opacity < 0.0) {
        opacity = 0.0;
    } else if (opacity > 1.0) {
        opacity = 1.0;
    }
    m_scene.xsheet.columns[layerIndex].opacity = opacity;
}

AnimeColumnType AnimeSceneModel::layerType(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return AnimeColumnType::Vector;
    }
    return m_scene.xsheet.columns[layerIndex].type;
}

bool AnimeSceneModel::isFillLayer(int layerIndex) const
{
    return layerType(layerIndex) == AnimeColumnType::Fill;
}

int AnimeSceneModel::layerParentId(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return 0;
    }
    // Through normalize, so a caller never sees an id that names nothing.
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    return m_scene.xsheet.columns[layerIndex].parentLayerId;
}

void AnimeSceneModel::setLayerParentId(int layerIndex, int parentLayerId)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return;
    }
    // Self-parenting is the one cycle a single assignment can create, and it
    // would make the panel try to nest a row under itself.
    if (parentLayerId > 0 && parentLayerId == m_scene.xsheet.columns[layerIndex].id) {
        return;
    }
    m_scene.xsheet.columns[layerIndex].parentLayerId = parentLayerId > 0 ? parentLayerId : 0;
}

QVector<int> AnimeSceneModel::childLayerIndices(int parentLayerIndex) const
{
    QVector<int> children;
    // layerIdAt normalizes, so dangling parent ids are already zeroed here.
    const int parentId = layerIdAt(parentLayerIndex);
    if (parentId <= 0) {
        return children;
    }
    for (int i = 0; i < m_scene.xsheet.columns.size(); ++i) {
        if (m_scene.xsheet.columns[i].parentLayerId == parentId) {
            children.append(i);
        }
    }
    return children;
}

QString AnimeSceneModel::scriptData() const
{
    return m_scene.scriptData;
}

void AnimeSceneModel::setScriptData(const QString &data)
{
    m_scene.scriptData = data;
}

QSize AnimeSceneModel::defaultCanvasSize()
{
    return QSize(1280, 720);
}

QSize AnimeSceneModel::canvasSize() const
{
    const QSize size = m_scene.canvasSize;
    return size.isValid() && size.width() > 0 && size.height() > 0
               ? size
               : defaultCanvasSize();
}

void AnimeSceneModel::setCanvasSize(const QSize &size)
{
    // 16 keeps a page usable at any zoom; 16384 is well past any raster the
    // app can hold and stops a typo from allocating an export nobody wanted.
    const int width = std::max(16, std::min(16384, size.width()));
    const int height = std::max(16, std::min(16384, size.height()));
    m_scene.canvasSize = QSize(width, height);
}

namespace {

void collectTreeIds(const QVector<AnimeLayerNode> &nodes, QSet<int> &layerIds, QSet<int> &groupIds)
{
    for (const AnimeLayerNode &node : nodes) {
        if (node.isGroup()) {
            groupIds.insert(node.groupId);
            collectTreeIds(node.children, layerIds, groupIds);
        } else if (node.layerId > 0) {
            layerIds.insert(node.layerId);
        }
    }
}

// Drops leaves whose column no longer exists, duplicate references, and the
// groups that end up empty. Returns false when this node itself should go.
bool pruneTree(QVector<AnimeLayerNode> &nodes, const QSet<int> &liveLayerIds, QSet<int> &seen)
{
    for (int i = nodes.size() - 1; i >= 0; --i) {
        AnimeLayerNode &node = nodes[i];
        if (node.isGroup()) {
            if (!pruneTree(node.children, liveLayerIds, seen)) {
                nodes.removeAt(i);
            }
            continue;
        }
        if (node.layerId <= 0 || !liveLayerIds.contains(node.layerId) || seen.contains(node.layerId)) {
            nodes.removeAt(i);
            continue;
        }
        seen.insert(node.layerId);
    }
    return !nodes.isEmpty();
}

AnimeLayerNode *findGroup(QVector<AnimeLayerNode> &nodes, int groupId)
{
    for (AnimeLayerNode &node : nodes) {
        if (!node.isGroup()) {
            continue;
        }
        if (node.groupId == groupId) {
            return &node;
        }
        if (AnimeLayerNode *found = findGroup(node.children, groupId)) {
            return found;
        }
    }
    return nullptr;
}

const AnimeLayerNode *findGroupConst(const QVector<AnimeLayerNode> &nodes, int groupId)
{
    for (const AnimeLayerNode &node : nodes) {
        if (!node.isGroup()) {
            continue;
        }
        if (node.groupId == groupId) {
            return &node;
        }
        if (const AnimeLayerNode *found = findGroupConst(node.children, groupId)) {
            return found;
        }
    }
    return nullptr;
}

// Detaches the nodes named by `layerIds`/`groupIds`, appending them to `taken`
// in TREE order so a group keeps the visual order the user already sees.
//
// The insertion point is recorded as the first member's PARENT GROUP ID plus
// its row, never as a pointer: `nodes` here is a child vector living inside
// its parent node, and a later removal in an outer vector can move that node,
// so a pointer taken on the way down is only safe under an ordering argument
// nobody will remember. An id survives anything. `parents` collects every
// distinct parent the members came from.
void takeNodes(QVector<AnimeLayerNode> &nodes,
               int parentGroupId,
               const QSet<int> &layerIds,
               const QSet<int> &groupIds,
               QVector<AnimeLayerNode> &taken,
               QSet<int> &parents,
               int *firstParentId,
               int *firstRow)
{
    for (int i = 0; i < nodes.size();) {
        AnimeLayerNode &node = nodes[i];
        const bool wanted = node.isGroup() ? groupIds.contains(node.groupId)
                                           : layerIds.contains(node.layerId);
        if (wanted) {
            if (taken.isEmpty()) {
                *firstParentId = parentGroupId;
                *firstRow = i;
            }
            parents.insert(parentGroupId);
            taken.append(node);
            nodes.removeAt(i);
            continue;
        }
        if (node.isGroup()) {
            // A wanted group is taken whole above, so this only ever descends
            // into groups that stay put - a group can never end up inside
            // itself.
            takeNodes(node.children, node.groupId, layerIds, groupIds,
                      taken, parents, firstParentId, firstRow);
        }
        ++i;
    }
}

bool spliceOutGroup(QVector<AnimeLayerNode> &nodes, int groupId)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isGroup() && nodes[i].groupId == groupId) {
            const QVector<AnimeLayerNode> children = nodes[i].children;
            nodes.removeAt(i);
            for (int k = 0; k < children.size(); ++k) {
                nodes.insert(i + k, children[k]);
            }
            return true;
        }
        if (nodes[i].isGroup() && spliceOutGroup(nodes[i].children, groupId)) {
            return true;
        }
    }
    return false;
}

} // namespace

void AnimeSceneModel::normalizeLayerTreeInternal()
{
    // 1. every column gets an id.
    //
    // The counter is pushed past everything ALREADY present before a single
    // new id is handed out. Doing it the other way round (assign as you scan,
    // fix the counter after) mints duplicates the moment a file mixes columns
    // that carry ids with columns that do not - which happens as soon as a
    // layer is added to a scene whose tree nobody has read since, because
    // sceneToJson (projectio.cpp) writes column.id verbatim and that column still has 0.
    // A duplicate is not a cosmetic problem: the second column collapses into
    // the first in liveLayerIds, so it never gets a node, never appears in the
    // panel, and layerIdAt reports the wrong column's identity.
    for (const AnimeColumn &column : m_scene.xsheet.columns) {
        m_scene.nextColumnId = std::max(m_scene.nextColumnId, column.id + 1);
    }
    QSet<int> liveLayerIds;
    bool anyParentLink = false;
    for (AnimeColumn &column : m_scene.xsheet.columns) {
        if (column.id <= 0) {
            column.id = m_scene.nextColumnId++;
        }
        liveLayerIds.insert(column.id);
        anyParentLink = anyParentLink || column.parentLayerId != 0;
    }

    // 1b. parent links must name a LIVE column, must not name themselves and
    //     must not close a loop. Linear (each column is walked once and then
    //     settled), and skipped outright when nothing is parented - this runs
    //     on the panel hot path, where every layerTree() and every layerIdAt()
    //     comes through here, and a scene with no tracked layer must not pay
    //     an allocation for the feature.
    if (anyParentLink) {
        QHash<int, int> indexById;
        indexById.reserve(m_scene.xsheet.columns.size());
        for (int i = 0; i < m_scene.xsheet.columns.size(); ++i) {
            indexById.insert(m_scene.xsheet.columns[i].id, i);
        }
        // 0 = not walked yet, 1 = on the chain being walked, 2 = settled.
        QVector<char> parentState(m_scene.xsheet.columns.size(), 0);
        QVector<int> chain;
        for (int start = 0; start < m_scene.xsheet.columns.size(); ++start) {
            if (parentState[start] != 0) {
                continue;
            }
            chain.clear();
            int at = start;
            while (at >= 0 && parentState[at] == 0) {
                parentState[at] = 1;
                chain.append(at);
                AnimeColumn &column = m_scene.xsheet.columns[at];
                if (column.parentLayerId <= 0 || column.parentLayerId == column.id) {
                    column.parentLayerId = 0;
                    at = -1;
                    break;
                }
                const auto it = indexById.constFind(column.parentLayerId);
                if (it == indexById.constEnd()) {
                    column.parentLayerId = 0;
                    at = -1;
                    break;
                }
                at = it.value();
            }
            if (at >= 0 && parentState[at] == 1) {
                // The chain closed back onto itself: cut only the link that
                // closed it, so every other parenting the user set survives.
                m_scene.xsheet.columns[chain.last()].parentLayerId = 0;
            }
            for (int index : chain) {
                parentState[index] = 2;
            }
        }
    }

    // 2. drop dead references, duplicates and emptied groups
    QSet<int> seen;
    pruneTree(m_scene.layerTree, liveLayerIds, seen);

    // 3. anything ungrouped joins the top level AT ITS PLACE IN THE COLUMN
    //    ORDER, not at the end. Appending was wrong in the one case that
    //    matters most: an Auto Mapping run moves its layers to column index 0
    //    so it paints over everything, and the panel then listed it LAST -
    //    the panel would claim the result sits under the artwork it covers.
    auto minColumnIndex = [this](const AnimeLayerNode &node) {
        std::function<int(const AnimeLayerNode &)> walk = [&](const AnimeLayerNode &n) {
            if (!n.isGroup()) {
                const int index = layerIndexForId(n.layerId);
                return index < 0 ? INT_MAX : index;
            }
            int best = INT_MAX;
            for (const AnimeLayerNode &child : n.children) {
                best = std::min(best, walk(child));
            }
            return best;
        };
        return walk(node);
    };
    for (int columnIndex = 0; columnIndex < m_scene.xsheet.columns.size(); ++columnIndex) {
        const AnimeColumn &column = m_scene.xsheet.columns[columnIndex];
        if (seen.contains(column.id)) {
            continue;
        }
        AnimeLayerNode node;
        node.layerId = column.id;
        int row = m_scene.layerTree.size();
        for (int i = 0; i < m_scene.layerTree.size(); ++i) {
            if (minColumnIndex(m_scene.layerTree[i]) > columnIndex) {
                row = i;
                break;
            }
        }
        m_scene.layerTree.insert(row, node);
        seen.insert(column.id);
    }

    // 4. keep the group counter ahead of anything loaded from a file
    QSet<int> layerIds;
    QSet<int> groupIds;
    collectTreeIds(m_scene.layerTree, layerIds, groupIds);
    int highestGroup = 0;
    for (int id : groupIds) {
        highestGroup = std::max(highestGroup, id);
    }
    m_scene.nextGroupId = std::max(m_scene.nextGroupId, highestGroup + 1);
}

void AnimeSceneModel::normalizeLayerTree()
{
    normalizeLayerTreeInternal();
}

const QVector<AnimeLayerNode> &AnimeSceneModel::layerTree() const
{
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    return m_scene.layerTree;
}

void AnimeSceneModel::setLayerTree(const QVector<AnimeLayerNode> &tree)
{
    m_scene.layerTree = tree;
    normalizeLayerTreeInternal();
}

int AnimeSceneModel::layerIdAt(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return 0;
    }
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    return m_scene.xsheet.columns[layerIndex].id;
}

int AnimeSceneModel::layerIndexForId(int layerId) const
{
    if (layerId <= 0) {
        return -1;
    }
    for (int i = 0; i < m_scene.xsheet.columns.size(); ++i) {
        if (m_scene.xsheet.columns[i].id == layerId) {
            return i;
        }
    }
    return -1;
}

int AnimeSceneModel::createLayerGroup(const QString &name,
                                      const QVector<int> &layerIndices,
                                      const QVector<int> &groupIds,
                                      bool collapsed)
{
    normalizeLayerTreeInternal();

    QSet<int> wantedLayers;
    for (int index : layerIndices) {
        const int id = layerIdAt(index);
        if (id > 0) {
            wantedLayers.insert(id);
        }
    }
    QSet<int> wantedGroups;
    for (int id : groupIds) {
        if (id > 0 && findGroup(m_scene.layerTree, id)) {
            wantedGroups.insert(id);
        }
    }
    if (wantedLayers.isEmpty() && wantedGroups.isEmpty()) {
        return 0;
    }

    AnimeLayerNode group;
    group.groupId = m_scene.nextGroupId++;
    group.name = name.isEmpty() ? QStringLiteral("Group %1").arg(group.groupId) : name;
    group.collapsed = collapsed;

    QSet<int> parents;
    int firstParentId = 0;
    int row = 0;
    takeNodes(m_scene.layerTree, 0, wantedLayers, wantedGroups,
              group.children, parents, &firstParentId, &row);
    if (group.children.isEmpty()) {
        return 0;
    }

    // The new group takes the first member's place - but only when every
    // member came from the SAME parent. Members pulled out of different
    // groups have no common place to sit, and burying the result inside
    // whichever one happened to come first (possibly a group the take just
    // emptied) would be arbitrary, so those land at the top level.
    QVector<AnimeLayerNode> *parent = &m_scene.layerTree;
    if (parents.size() == 1 && firstParentId > 0) {
        if (AnimeLayerNode *host = findGroup(m_scene.layerTree, firstParentId)) {
            parent = &host->children;
        } else {
            row = static_cast<int>(m_scene.layerTree.size());
        }
    } else if (parents.size() != 1) {
        row = static_cast<int>(m_scene.layerTree.size());
    }
    row = std::max(0, std::min(row, static_cast<int>(parent->size())));
    parent->insert(row, group);
    return group.groupId;
}

bool AnimeSceneModel::setLayerGroupCollapsed(int groupId, bool collapsed)
{
    normalizeLayerTreeInternal();
    if (AnimeLayerNode *group = findGroup(m_scene.layerTree, groupId)) {
        group->collapsed = collapsed;
        return true;
    }
    return false;
}

bool AnimeSceneModel::setLayerGroupName(int groupId, const QString &name)
{
    normalizeLayerTreeInternal();
    if (AnimeLayerNode *group = findGroup(m_scene.layerTree, groupId)) {
        if (!name.isEmpty()) {
            group->name = name;
        }
        return true;
    }
    return false;
}

bool AnimeSceneModel::dissolveLayerGroup(int groupId)
{
    normalizeLayerTreeInternal();
    return spliceOutGroup(m_scene.layerTree, groupId);
}

QVector<int> AnimeSceneModel::layerIdsInGroup(int groupId) const
{
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    QVector<int> ids;
    const AnimeLayerNode *group = findGroupConst(m_scene.layerTree, groupId);
    if (!group) {
        return ids;
    }
    std::function<void(const QVector<AnimeLayerNode> &)> walk =
        [&](const QVector<AnimeLayerNode> &nodes) {
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                walk(node.children);
            } else if (node.layerId > 0) {
                ids.append(node.layerId);
            }
        }
    };
    walk(group->children);
    return ids;
}

int AnimeSceneModel::deleteLayerGroup(int groupId)
{
    // By ID, resolved one at a time: every deletion renumbers the columns
    // after it, so a list of indices captured up front would delete the wrong
    // layers from the second one on.
    const QVector<int> ids = layerIdsInGroup(groupId);
    int deleted = 0;
    for (int id : ids) {
        const int index = layerIndexForId(id);
        if (index < 0) {
            continue;
        }
        if (deleteLayer(index)) {
            remapFillSourceLayersAfterDelete(index);
            ++deleted;
        }
    }
    // The now-empty group is dropped by the reconcile; dissolve covers the
    // case where the group held nothing deletable so the node would linger.
    normalizeLayerTreeInternal();
    dissolveLayerGroup(groupId);
    return deleted;
}

bool AnimeSceneModel::layerGroupCollapsed(int groupId) const
{
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    const AnimeLayerNode *group = findGroupConst(m_scene.layerTree, groupId);
    return group && group->collapsed;
}

int AnimeSceneModel::addLayersToGroup(int groupId, const QVector<int> &layerIndices)
{
    normalizeLayerTreeInternal();
    if (!findGroup(m_scene.layerTree, groupId)) {
        return 0;
    }
    QSet<int> wantedLayers;
    for (int index : layerIndices) {
        const int id = layerIdAt(index);
        if (id > 0) {
            wantedLayers.insert(id);
        }
    }
    if (wantedLayers.isEmpty()) {
        return 0;
    }
    QVector<AnimeLayerNode> taken;
    QSet<int> parents;
    int firstParentId = 0;
    int row = 0;
    takeNodes(m_scene.layerTree, 0, wantedLayers, QSet<int>(),
              taken, parents, &firstParentId, &row);
    // Re-find after the take: detaching runs over the whole tree and the
    // by-id lookup survives any reshuffle a pointer would not.
    AnimeLayerNode *group = findGroup(m_scene.layerTree, groupId);
    if (!group) {
        normalizeLayerTreeInternal(); // re-adopt the detached leaves
        return 0;
    }
    for (const AnimeLayerNode &node : taken) {
        group->children.append(node);
    }
    return taken.size();
}

bool AnimeSceneModel::setLayerGroupTag(int groupId, const QString &tag)
{
    normalizeLayerTreeInternal();
    if (AnimeLayerNode *group = findGroup(m_scene.layerTree, groupId)) {
        group->tag = tag;
        return true;
    }
    return false;
}

QString AnimeSceneModel::layerGroupTag(int groupId) const
{
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    const AnimeLayerNode *group = findGroupConst(m_scene.layerTree, groupId);
    return group ? group->tag : QString();
}

int AnimeSceneModel::groupIdForLayer(int layerIndex, const QString &tag) const
{
    const int layerId = layerIdAt(layerIndex);
    if (layerId <= 0) {
        return 0;
    }
    // Walk to the leaf, remembering the deepest matching group along the
    // PATH to it - innermost wins, and an untagged wrapper around a tagged
    // unit does not hide the unit.
    const_cast<AnimeSceneModel *>(this)->normalizeLayerTreeInternal();
    int found = 0;
    std::function<bool(const QVector<AnimeLayerNode> &, int)> walk =
        [&](const QVector<AnimeLayerNode> &nodes, int best) -> bool {
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                const int next =
                    (tag.isEmpty() || node.tag == tag) ? node.groupId : best;
                if (walk(node.children, next)) {
                    return true;
                }
            } else if (node.layerId == layerId) {
                found = best;
                return true;
            }
        }
        return false;
    };
    walk(m_scene.layerTree, 0);
    return found;
}

int AnimeSceneModel::addLayer()
{
    if (m_currentFrame < 0) {
        m_currentFrame = 0;
    }

    const int assetIndex = addAsset(AnimeColumnType::Vector);
    return addLayerForAsset(m_currentFrame, assetIndex);
}

int AnimeSceneModel::addFillLayer()
{
    if (m_currentFrame < 0) {
        m_currentFrame = 0;
    }

    const int assetIndex = addAsset(AnimeColumnType::Fill);
    return addLayerForAsset(m_currentFrame, assetIndex);
}

int AnimeSceneModel::addAsset(AnimeColumnType type, const QString &name)
{
    AnimeAsset asset;
    asset.type = type;
    const QString baseName = name.isEmpty()
                                 ? QStringLiteral("%1 %2").arg(assetTypeName(type)).arg(m_scene.assets.size() + 1)
                                 : name;
    asset.name = uniqueLayerName(baseName);
    const int assetIndex = m_scene.assets.size();
    m_scene.assets.append(asset);
    m_scene.assets[assetIndex].frame(1, true);
    setCurrentAsset(assetIndex);
    return assetIndex;
}

bool AnimeSceneModel::deleteAsset(int assetIndex)
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return false;
    }

    m_scene.assets.removeAt(assetIndex);

    // Every cell keeps addressing the same asset it did before: references to
    // the removed asset are cleared, higher indices shift down by one.
    for (AnimeColumn &column : m_scene.xsheet.columns) {
        for (int row = 0; row <= column.maxRow(); ++row) {
            AnimeCell cell = column.cellAt(row);
            if (cell.assetIndex == assetIndex) {
                column.setCell(row, AnimeCell());
            } else if (cell.assetIndex > assetIndex) {
                --cell.assetIndex;
                column.setCell(row, cell);
            }
        }
    }

    if (m_currentAsset == assetIndex) {
        m_currentAsset = -1;
    } else if (m_currentAsset > assetIndex) {
        --m_currentAsset;
    }
    return true;
}

bool AnimeSceneModel::deleteLayer(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size()) {
        return false;
    }

    m_scene.xsheet.columns.removeAt(layerIndex);
    if (m_scene.xsheet.columns.isEmpty()) {
        m_currentLayer = -1;
    } else if (m_currentLayer >= m_scene.xsheet.columns.size()) {
        m_currentLayer = m_scene.xsheet.columns.size() - 1;
    } else if (m_currentLayer > layerIndex) {
        --m_currentLayer;
    }
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
    return true;
}

bool AnimeSceneModel::moveLayer(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_scene.xsheet.columns.size() ||
        toIndex < 0 || toIndex >= m_scene.xsheet.columns.size() ||
        fromIndex == toIndex) {
        return false;
    }

    const AnimeColumn column = m_scene.xsheet.columns.takeAt(fromIndex);
    m_scene.xsheet.columns.insert(toIndex, column);
    if (m_currentLayer == fromIndex) {
        m_currentLayer = toIndex;
    } else if (fromIndex < m_currentLayer && m_currentLayer <= toIndex) {
        --m_currentLayer;
    } else if (toIndex <= m_currentLayer && m_currentLayer < fromIndex) {
        ++m_currentLayer;
    }
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
    return true;
}

int AnimeSceneModel::addFrame()
{
    const int row = m_scene.xsheet.frameCount;
    m_scene.xsheet.ensureFrameCount(row + 1);

    setCurrentFrame(row);
    return row;
}

int AnimeSceneModel::insertHoldFrameAfter(int row)
{
    if (row < 0 || row >= m_scene.xsheet.frameCount) {
        return -1;
    }
    const int target = row + 1;
    for (AnimeColumn &column : m_scene.xsheet.columns) {
        const AnimeCell held = column.cellAt(row);
        column.insertCell(target);
        if (!held.isEmpty()) {
            // The SAME cell, not a copy of the drawing: both rows point at one
            // asset and one frame id, so a stroke drawn on either appears on
            // both. That is what makes the new row a hold of `row`.
            column.setCell(target, held);
        }
    }
    ++m_scene.xsheet.frameCount;
    m_scene.xsheet.shiftFrameNamesForInsert(target);
    setCurrentFrame(target);
    return target;
}

int AnimeSceneModel::duplicateFrame(int row)
{
    if (row < 0 || row >= m_scene.xsheet.frameCount) {
        return -1;
    }
    // Past the run that already re-exposes this drawing: dropping the copy
    // INSIDE the hold run would split one exposure into two.
    int target = row + 1;
    while (target < m_scene.xsheet.frameCount && isHoldFrame(target)) {
        ++target;
    }

    const QString name = nextDuplicateName(row);
    for (AnimeColumn &column : m_scene.xsheet.columns) {
        const AnimeCell source = column.cellAt(row);
        column.insertCell(target);
        if (source.isEmpty() || source.assetIndex < 0
            || source.assetIndex >= m_scene.assets.size()) {
            continue;   // an empty cell duplicates as an empty cell
        }
        AnimeAsset &asset = m_scene.assets[source.assetIndex];
        const AnimeVectorImageModel *original = asset.frame(source.frameId);
        if (!original) {
            continue;
        }
        // A fresh frame id in the SAME asset: the copy belongs to the same
        // layer's drawing set, but nothing else may resolve to it.
        int frameId = source.frameId;
        for (int existing : asset.frameIds()) {
            frameId = maxInt(frameId, existing);
        }
        ++frameId;
        const AnimeVectorImageModel copy = *original;
        if (AnimeVectorImageModel *created = asset.frame(frameId, true)) {
            *created = copy;
        }
        AnimeCell cell;
        cell.assetIndex = source.assetIndex;
        cell.frameId = frameId;
        column.setCell(target, cell);
    }
    ++m_scene.xsheet.frameCount;
    m_scene.xsheet.shiftFrameNamesForInsert(target);
    setFrameName(target, name);
    setCurrentFrame(target);
    return target;
}

bool AnimeSceneModel::isHoldFrame(int frameIndex) const
{
    if (frameIndex <= 0 || frameIndex >= m_scene.xsheet.frameCount) {
        return false;
    }
    bool hasContent = false;
    for (const AnimeColumn &column : m_scene.xsheet.columns) {
        if (column.internal) {
            continue;   // script working layers are not part of the exposure
        }
        const AnimeCell here = column.cellAt(frameIndex);
        if (here.isEmpty()) {
            continue;
        }
        hasContent = true;
        const AnimeCell above = column.cellAt(frameIndex - 1);
        if (above.assetIndex != here.assetIndex || above.frameId != here.frameId) {
            return false;
        }
    }
    return hasContent;
}

int AnimeSceneModel::playbackFps() const
{
    return std::max(1, std::min(120, m_scene.playbackFps));
}

void AnimeSceneModel::setPlaybackFps(int fps)
{
    m_scene.playbackFps = std::max(1, std::min(120, fps));
}

bool AnimeSceneModel::deleteFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= m_scene.xsheet.frameCount ||
        m_scene.xsheet.frameCount <= 1) {
        return false;
    }

    for (AnimeColumn &column : m_scene.xsheet.columns) {
        column.removeCell(frameIndex);
    }
    --m_scene.xsheet.frameCount;
    m_scene.xsheet.shiftFrameNamesForDelete(frameIndex);
    if (m_currentFrame >= m_scene.xsheet.frameCount) {
        m_currentFrame = m_scene.xsheet.frameCount - 1;
    } else if (m_currentFrame > frameIndex) {
        --m_currentFrame;
    }
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
    return true;
}

bool AnimeSceneModel::moveFrame(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_scene.xsheet.frameCount ||
        toIndex < 0 || toIndex >= m_scene.xsheet.frameCount ||
        fromIndex == toIndex) {
        return false;
    }

    for (AnimeColumn &column : m_scene.xsheet.columns) {
        column.moveCell(fromIndex, toIndex);
    }
    m_scene.xsheet.shiftFrameNamesForMove(fromIndex, toIndex);
    if (m_currentFrame == fromIndex) {
        m_currentFrame = toIndex;
    } else if (fromIndex < m_currentFrame && m_currentFrame <= toIndex) {
        --m_currentFrame;
    } else if (toIndex <= m_currentFrame && m_currentFrame < fromIndex) {
        ++m_currentFrame;
    }
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
    return true;
}

AnimeCell AnimeSceneModel::cellAt(int row, int layerIndex) const
{
    return m_scene.xsheet.cellAt(row, layerIndex);
}

void AnimeSceneModel::setCell(int row, int layerIndex, const AnimeCell &cell)
{
    if (!cell.isEmpty()) {
        if (cell.assetIndex >= m_scene.assets.size()) {
            m_scene.assets.resize(cell.assetIndex + 1);
        }
        m_scene.assets[cell.assetIndex].frame(cell.frameId, true);
    }
    m_scene.xsheet.setCell(row, layerIndex, cell);
}

void AnimeSceneModel::clearCell(int row, int layerIndex)
{
    m_scene.xsheet.setCell(row, layerIndex, AnimeCell());
}

AnimeVectorImageModel *AnimeSceneModel::imageAt(int row, int layerIndex, bool create, AnimeColumnType assetType)
{
    if (row < 0 || layerIndex < 0) {
        return nullptr;
    }
    m_scene.xsheet.ensureColumnCount(layerIndex + 1);
    m_scene.xsheet.ensureFrameCount(row + 1);

    AnimeCell cell = m_scene.xsheet.cellAt(row, layerIndex);
    if (cell.isEmpty()) {
        if (!create) {
            return nullptr;
        }
        const int assetIndex = addAsset(assetType);
        cell.assetIndex = assetIndex;
        cell.frameId = 1;
        m_scene.xsheet.setCell(row, layerIndex, cell);
        m_scene.xsheet.columns[layerIndex].name = assetName(assetIndex);
        m_scene.xsheet.columns[layerIndex].type = assetType;
    }

    if (cell.assetIndex < 0 || cell.assetIndex >= m_scene.assets.size()) {
        return nullptr;
    }
    return m_scene.assets[cell.assetIndex].frame(cell.frameId, create);
}

const AnimeVectorImageModel *AnimeSceneModel::imageAt(int row, int layerIndex) const
{
    const AnimeCell cell = m_scene.xsheet.cellAt(row, layerIndex);
    return imageForCell(cell);
}

int AnimeSceneModel::assetIndexAt(int row, int layerIndex) const
{
    const AnimeCell cell = m_scene.xsheet.cellAt(row, layerIndex);
    if (cell.isEmpty()) {
        return -1;
    }
    return cell.assetIndex;
}

AnimeVectorImageModel *AnimeSceneModel::assetImage(int assetIndex, bool create)
{
    return assetImage(assetIndex, 1, create);
}

AnimeVectorImageModel *AnimeSceneModel::assetImage(int assetIndex, int frameId, bool create)
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return nullptr;
    }
    return m_scene.assets[assetIndex].frame(frameId, create);
}

const AnimeVectorImageModel *AnimeSceneModel::assetImage(int assetIndex) const
{
    return assetImage(assetIndex, 1);
}

const AnimeVectorImageModel *AnimeSceneModel::assetImage(int assetIndex, int frameId) const
{
    if (assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return nullptr;
    }
    return m_scene.assets[assetIndex].frame(frameId);
}

bool AnimeSceneModel::assignAssetToLayer(int row, int layerIndex, int assetIndex)
{
    if (row < 0 || layerIndex < 0 || assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return false;
    }
    m_scene.xsheet.ensureColumnCount(layerIndex + 1);
    m_scene.xsheet.ensureFrameCount(row + 1);

    AnimeCell cell;
    cell.assetIndex = assetIndex;
    cell.frameId = 1;
    m_scene.xsheet.setCell(row, layerIndex, cell);
    m_scene.xsheet.columns[layerIndex].name = assetName(assetIndex);
    m_scene.xsheet.columns[layerIndex].type = assetType(assetIndex);
    m_scene.assets[assetIndex].frame(1, true);
    setCurrentLayer(layerIndex);
    setCurrentFrame(row);
    setCurrentAsset(assetIndex);
    return true;
}

int AnimeSceneModel::addLayerForAsset(int row, int assetIndex)
{
    if (row < 0 || assetIndex < 0 || assetIndex >= m_scene.assets.size()) {
        return -1;
    }

    AnimeColumn column;
    column.name = assetName(assetIndex);
    column.type = assetType(assetIndex);
    const int layerIndex = m_scene.xsheet.columns.size();
    m_scene.xsheet.columns.append(column);
    if (!assignAssetToLayer(row, layerIndex, assetIndex)) {
        return -1;
    }
    return layerIndex;
}

AnimeVectorImageModel *AnimeSceneModel::currentImage(bool create, AnimeColumnType assetType)
{
    if (m_currentFrame < 0) {
        if (!create) {
            return nullptr;
        }
        m_currentFrame = 0;
    }

    if (m_currentLayer < 0 && !create) {
        return nullptr;
    }

    if (m_currentLayer < 0) {
        const int assetIndex = addAsset(assetType);
        addLayerForAsset(m_currentFrame, assetIndex);
    }

    AnimeVectorImageModel *image = imageAt(m_currentFrame, m_currentLayer, create, assetType);
    m_currentAsset = assetIndexAt(m_currentFrame, m_currentLayer);
    return image;
}

const AnimeVectorImageModel *AnimeSceneModel::imageForCell(const AnimeCell &cell) const
{
    if (cell.isEmpty() || cell.assetIndex < 0 || cell.assetIndex >= m_scene.assets.size()) {
        return nullptr;
    }
    return m_scene.assets[cell.assetIndex].frame(cell.frameId);
}

bool AnimeSceneModel::setRasterImageAt(int row, int layerIndex, const QImage &image, const QPointF &topLeft)
{
    if (image.isNull()) {
        return false;
    }

    AnimeVectorImageModel *cellImage = imageAt(row, layerIndex, true, AnimeColumnType::Raster);
    if (!cellImage) {
        return false;
    }

    cellImage->setRasterImage(image, topLeft);
    return true;
}

int AnimeSceneModel::addRasterLayer(const QString &name, int frameIndex, const QImage &image, const QPointF &topLeft)
{
    if (frameIndex < 0 || image.isNull()) {
        return -1;
    }

    const QString assetName = uniqueLayerName(name.isEmpty()
                                                  ? QStringLiteral("Raster %1").arg(m_scene.assets.size() + 1)
                                                  : QStringLiteral("Raster %1").arg(name));
    AnimeColumn column;
    column.name = assetName;
    column.type = AnimeColumnType::Raster;
    const int columnIndex = m_scene.xsheet.columns.size();
    m_scene.xsheet.columns.append(column);

    AnimeAsset asset;
    asset.name = assetName;
    asset.type = AnimeColumnType::Raster;
    const int assetIndex = m_scene.assets.size();
    m_scene.assets.append(asset);

    m_scene.xsheet.ensureFrameCount(frameIndex + 1);

    AnimeCell cell;
    cell.assetIndex = assetIndex;
    cell.frameId = 1;
    m_scene.xsheet.setCell(frameIndex, columnIndex, cell);
    m_scene.assets[assetIndex].frame(cell.frameId, true)->setRasterImage(image, topLeft);

    setCurrentLayer(columnIndex);
    setCurrentFrame(frameIndex);
    setCurrentAsset(assetIndex);
    return columnIndex;
}

void AnimeSceneModel::remapFillSourceLayersAfterDelete(int deletedLayerIndex)
{
    for (AnimeAsset &asset : m_scene.assets) {
        for (int frameId : asset.frameIds()) {
            if (AnimeVectorImageModel *image = asset.frame(frameId, false)) {
                image->remapFillSourceLayersAfterDelete(deletedLayerIndex);
            }
        }
    }
}

void AnimeSceneModel::remapFillSourceLayersAfterMove(int fromIndex, int toIndex)
{
    for (AnimeAsset &asset : m_scene.assets) {
        for (int frameId : asset.frameIds()) {
            if (AnimeVectorImageModel *image = asset.frame(frameId, false)) {
                image->remapFillSourceLayersAfterMove(fromIndex, toIndex);
            }
        }
    }
}

AnimeColumn *AnimeSceneModel::currentColumn()
{
    if (m_currentLayer < 0 || m_currentLayer >= m_scene.xsheet.columns.size()) {
        return nullptr;
    }
    return &m_scene.xsheet.columns[m_currentLayer];
}

const AnimeColumn *AnimeSceneModel::currentColumn() const
{
    if (m_currentLayer < 0 || m_currentLayer >= m_scene.xsheet.columns.size()) {
        return nullptr;
    }
    return &m_scene.xsheet.columns[m_currentLayer];
}

bool AnimeSceneModel::currentColumnEditable() const
{
    const AnimeColumn *column = currentColumn();
    return column && !column->locked && column->type == AnimeColumnType::Vector;
}

QVector<QLineF> AnimeSceneModel::fillBoundarySegments(int frame, int layerIndex) const
{
    QVector<QLineF> segments;
    if (frame < 0) {
        return segments;
    }

    for (int columnIndex = 0; columnIndex < m_scene.xsheet.columns.size(); ++columnIndex) {
        if (layerIndex >= 0 && columnIndex != layerIndex) {
            continue;
        }

        const AnimeColumn &column = m_scene.xsheet.columns[columnIndex];
        if (column.type == AnimeColumnType::Fill || !column.visible || column.internal) {
            continue;
        }

        const AnimeCell cell = column.cellAt(frame);
        const AnimeVectorImageModel *image = imageForCell(cell);
        if (!image) {
            continue;
        }

        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            segments += AnimeVectorLogic::segmentsFromPath(node.stroke.path);
        }
    }
    return segments;
}

QRectF AnimeSceneModel::contentBounds(int frame) const
{
    // Deliberately NOT filtered by visibility or type: a hidden layer becomes
    // visible again, and a fill layer is content too. Reachability that came
    // and went with a checkbox would be worse than none.
    QRectF bounds;
    if (frame < 0) {
        return bounds;
    }
    for (const AnimeColumn &column : m_scene.xsheet.columns) {
        if (column.internal) {
            continue;
        }
        const AnimeVectorImageModel *image = imageForCell(column.cellAt(frame));
        if (!image) {
            continue;
        }
        const QRectF cell = image->bounds();
        if (cell.isNull()) {
            continue;
        }
        bounds = bounds.isNull() ? cell : bounds.united(cell);
    }
    return bounds;
}

QRectF AnimeSceneModel::fillBoundaryBounds(int frame, int layerIndex) const
{
    // Deliberately a line-for-line twin of fillBoundarySegments' column filter:
    // this rect bounds those segments, so the two must agree on which columns
    // are walls. stroke.bounds is already padded by the stroke width.
    QRectF bounds;
    if (frame < 0) {
        return bounds;
    }

    for (int columnIndex = 0; columnIndex < m_scene.xsheet.columns.size(); ++columnIndex) {
        if (layerIndex >= 0 && columnIndex != layerIndex) {
            continue;
        }

        const AnimeColumn &column = m_scene.xsheet.columns[columnIndex];
        if (column.type == AnimeColumnType::Fill || !column.visible || column.internal) {
            continue;
        }

        const AnimeCell cell = column.cellAt(frame);
        const AnimeVectorImageModel *image = imageForCell(cell);
        if (!image) {
            continue;
        }

        for (const AnimeVectorStrokeNode &node : image->strokeNodes()) {
            bounds = bounds.isNull() ? node.stroke.bounds : bounds.united(node.stroke.bounds);
        }
    }
    return bounds;
}
