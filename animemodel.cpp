#include "animemodel.h"

#include <algorithm>

namespace {
int maxInt(int a, int b)
{
    return a > b ? a : b;
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

void AnimeVectorImageModel::replaceStrokeWithPieces(int index, const QVector<AnimeVectorStroke> &pieces)
{
    if (index < 0 || index >= m_strokes.size()) {
        return;
    }

    m_strokes.removeAt(index);
    for (int i = pieces.size() - 1; i >= 0; --i) {
        if (pieces[i].points.size() >= 2 && pieces[i].totalLength > 0.0001) {
            AnimeVectorStrokeNode node;
            node.stroke = pieces[i];
            m_strokes.insert(index, node);
        }
    }
    rebuildBounds();
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

AnimeVectorImageModel *AnimeLevel::frame(int frameId, bool create)
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

const AnimeVectorImageModel *AnimeLevel::frame(int frameId) const
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

void AnimeLevel::removeFrame(int frameId)
{
    m_frames.remove(frameId);
}

QVector<int> AnimeLevel::frameIds() const
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
        column.name = QStringLiteral("Layer %1").arg(columns.size() + 1);
        columns.append(column);
    }
}

void AnimeXsheet::ensureFrameCount(int count)
{
    if (count > frameCount) {
        frameCount = count;
    }
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

void AnimeSceneModel::initializeScene(int layerCount, int frameCount)
{
    m_scene = AnimeScene();
    m_scene.xsheet.ensureColumnCount(layerCount);
    m_scene.xsheet.ensureFrameCount(frameCount);

    for (int columnIndex = 0; columnIndex < m_scene.xsheet.columns.size(); ++columnIndex) {
        AnimeLevel level;
        level.name = QStringLiteral("Level %1").arg(columnIndex + 1);
        const int levelIndex = m_scene.levels.size();
        m_scene.levels.append(level);

        for (int row = 0; row < frameCount; ++row) {
            AnimeCell cell;
            cell.levelIndex = levelIndex;
            cell.frameId = row + 1;
            m_scene.xsheet.setCell(row, columnIndex, cell);
            m_scene.levels[levelIndex].frame(cell.frameId, true);
        }
    }

    m_currentLayer = 0;
    m_currentFrame = 0;
}

void AnimeSceneModel::setCurrentLayer(int layerIndex)
{
    if (layerIndex < 0) {
        return;
    }
    m_scene.xsheet.ensureColumnCount(layerIndex + 1);
    m_currentLayer = layerIndex;
}

void AnimeSceneModel::setCurrentFrame(int frameIndex)
{
    if (frameIndex < 0) {
        return;
    }
    m_scene.xsheet.ensureFrameCount(frameIndex + 1);
    m_currentFrame = frameIndex;
}

int AnimeSceneModel::currentLayer() const
{
    return m_currentLayer;
}

int AnimeSceneModel::currentFrame() const
{
    return m_currentFrame;
}

int AnimeSceneModel::layerCount() const
{
    return m_scene.xsheet.columns.size();
}

int AnimeSceneModel::frameCount() const
{
    return m_scene.xsheet.frameCount;
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
    m_scene.xsheet.columns[layerIndex].name = name;
}

QString AnimeSceneModel::frameName(int frameIndex) const
{
    if (frameIndex < 0) {
        return QString();
    }
    return QString::number(frameIndex + 1);
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

int AnimeSceneModel::addLayer()
{
    AnimeColumn column;
    column.name = QStringLiteral("Layer %1").arg(m_scene.xsheet.columns.size() + 1);
    const int columnIndex = m_scene.xsheet.columns.size();
    m_scene.xsheet.columns.append(column);

    AnimeLevel level;
    level.name = QStringLiteral("Level %1").arg(m_scene.levels.size() + 1);
    const int levelIndex = m_scene.levels.size();
    m_scene.levels.append(level);

    for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
        AnimeCell cell;
        cell.levelIndex = levelIndex;
        cell.frameId = nextFrameId();
        m_scene.xsheet.setCell(row, columnIndex, cell);
        m_scene.levels[levelIndex].frame(cell.frameId, true);
    }

    setCurrentLayer(columnIndex);
    return columnIndex;
}

int AnimeSceneModel::addFillLayer()
{
    int fillLayerCount = 0;
    for (const AnimeColumn &existingColumn : m_scene.xsheet.columns) {
        if (existingColumn.type == AnimeColumnType::Fill) {
            ++fillLayerCount;
        }
    }

    AnimeColumn column;
    column.name = QStringLiteral("FillLayer %1").arg(fillLayerCount + 1);
    column.type = AnimeColumnType::Fill;
    const int columnIndex = m_scene.xsheet.columns.size();
    m_scene.xsheet.columns.append(column);

    AnimeLevel level;
    level.name = column.name;
    const int levelIndex = m_scene.levels.size();
    m_scene.levels.append(level);

    for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
        AnimeCell cell;
        cell.levelIndex = levelIndex;
        cell.frameId = nextFrameId();
        m_scene.xsheet.setCell(row, columnIndex, cell);
        m_scene.levels[levelIndex].frame(cell.frameId, true);
    }

    setCurrentLayer(columnIndex);
    return columnIndex;
}

bool AnimeSceneModel::deleteLayer(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size() ||
        m_scene.xsheet.columns.size() <= 1) {
        return false;
    }

    m_scene.xsheet.columns.removeAt(layerIndex);
    pruneUnusedLevels();
    if (m_currentLayer >= m_scene.xsheet.columns.size()) {
        m_currentLayer = m_scene.xsheet.columns.size() - 1;
    } else if (m_currentLayer > layerIndex) {
        --m_currentLayer;
    }
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
    return true;
}

int AnimeSceneModel::addFrame()
{
    const int row = m_scene.xsheet.frameCount;
    m_scene.xsheet.ensureFrameCount(row + 1);

    for (int columnIndex = 0; columnIndex < m_scene.xsheet.columns.size(); ++columnIndex) {
        const int levelIndex = ensureLevelForColumn(columnIndex);
        AnimeCell cell;
        cell.levelIndex = levelIndex;
        cell.frameId = nextFrameId();
        m_scene.xsheet.setCell(row, columnIndex, cell);
        m_scene.levels[levelIndex].frame(cell.frameId, true);
    }

    setCurrentFrame(row);
    return row;
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
    if (m_currentFrame >= m_scene.xsheet.frameCount) {
        m_currentFrame = m_scene.xsheet.frameCount - 1;
    } else if (m_currentFrame > frameIndex) {
        --m_currentFrame;
    }
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
    if (m_currentFrame == fromIndex) {
        m_currentFrame = toIndex;
    } else if (fromIndex < m_currentFrame && m_currentFrame <= toIndex) {
        --m_currentFrame;
    } else if (toIndex <= m_currentFrame && m_currentFrame < fromIndex) {
        ++m_currentFrame;
    }
    return true;
}

AnimeCell AnimeSceneModel::cellAt(int row, int layerIndex) const
{
    return m_scene.xsheet.cellAt(row, layerIndex);
}

void AnimeSceneModel::setCell(int row, int layerIndex, const AnimeCell &cell)
{
    if (!cell.isEmpty()) {
        if (cell.levelIndex >= m_scene.levels.size()) {
            m_scene.levels.resize(cell.levelIndex + 1);
        }
        m_scene.levels[cell.levelIndex].frame(cell.frameId, true);
    }
    m_scene.xsheet.setCell(row, layerIndex, cell);
}

void AnimeSceneModel::clearCell(int row, int layerIndex)
{
    const AnimeCell oldCell = m_scene.xsheet.cellAt(row, layerIndex);
    m_scene.xsheet.setCell(row, layerIndex, AnimeCell());
    if (!oldCell.isEmpty() &&
        oldCell.levelIndex >= 0 &&
        oldCell.levelIndex < m_scene.levels.size() &&
        !cellIsReferenced(oldCell)) {
        m_scene.levels[oldCell.levelIndex].removeFrame(oldCell.frameId);
        pruneUnusedLevels();
    }
}

AnimeVectorImageModel *AnimeSceneModel::imageAt(int row, int layerIndex, bool create)
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
        const int levelIndex = ensureLevelForColumn(layerIndex);
        cell.levelIndex = levelIndex;
        cell.frameId = nextFrameId();
        m_scene.xsheet.setCell(row, layerIndex, cell);
    }

    if (cell.levelIndex < 0 || cell.levelIndex >= m_scene.levels.size()) {
        return nullptr;
    }
    return m_scene.levels[cell.levelIndex].frame(cell.frameId, create);
}

const AnimeVectorImageModel *AnimeSceneModel::imageAt(int row, int layerIndex) const
{
    const AnimeCell cell = m_scene.xsheet.cellAt(row, layerIndex);
    return imageForCell(cell);
}

AnimeVectorImageModel *AnimeSceneModel::currentImage(bool create)
{
    return imageAt(m_currentFrame, m_currentLayer, create);
}

const AnimeVectorImageModel *AnimeSceneModel::imageForCell(const AnimeCell &cell) const
{
    if (cell.isEmpty() || cell.levelIndex < 0 || cell.levelIndex >= m_scene.levels.size()) {
        return nullptr;
    }
    return m_scene.levels[cell.levelIndex].frame(cell.frameId);
}

bool AnimeSceneModel::setRasterImageAt(int row, int layerIndex, const QImage &image, const QPointF &topLeft)
{
    if (image.isNull()) {
        return false;
    }

    AnimeVectorImageModel *cellImage = imageAt(row, layerIndex, true);
    if (!cellImage) {
        return false;
    }

    cellImage->setRasterImage(image, topLeft);
    return true;
}

int AnimeSceneModel::addRasterLayer(const QString &name, int frameIndex, const QImage &image, const QPointF &topLeft)
{
    if (image.isNull()) {
        return -1;
    }

    AnimeColumn column;
    column.name = name.isEmpty()
                      ? QStringLiteral("Raster %1").arg(m_scene.xsheet.columns.size() + 1)
                      : name;
    column.type = AnimeColumnType::Raster;
    const int columnIndex = m_scene.xsheet.columns.size();
    m_scene.xsheet.columns.append(column);

    AnimeLevel level;
    level.name = column.name;
    const int levelIndex = m_scene.levels.size();
    m_scene.levels.append(level);

    m_scene.xsheet.ensureFrameCount(frameIndex + 1);

    AnimeCell cell;
    cell.levelIndex = levelIndex;
    cell.frameId = nextFrameId();
    m_scene.xsheet.setCell(frameIndex, columnIndex, cell);
    m_scene.levels[levelIndex].frame(cell.frameId, true)->setRasterImage(image, topLeft);

    setCurrentLayer(columnIndex);
    setCurrentFrame(frameIndex);
    return columnIndex;
}

void AnimeSceneModel::remapFillSourceLayersAfterDelete(int deletedLayerIndex)
{
    for (AnimeLevel &level : m_scene.levels) {
        for (int frameId : level.frameIds()) {
            if (AnimeVectorImageModel *image = level.frame(frameId, false)) {
                image->remapFillSourceLayersAfterDelete(deletedLayerIndex);
            }
        }
    }
}

void AnimeSceneModel::remapFillSourceLayersAfterMove(int fromIndex, int toIndex)
{
    for (AnimeLevel &level : m_scene.levels) {
        for (int frameId : level.frameIds()) {
            if (AnimeVectorImageModel *image = level.frame(frameId, false)) {
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

int AnimeSceneModel::ensureLevelForCurrentColumn()
{
    return ensureLevelForColumn(m_currentLayer);
}

int AnimeSceneModel::ensureLevelForColumn(int columnIndex)
{
    AnimeCell existingCell = m_scene.xsheet.cellAt(0, columnIndex);
    if (!existingCell.isEmpty() &&
        existingCell.levelIndex >= 0 &&
        existingCell.levelIndex < m_scene.levels.size()) {
        return existingCell.levelIndex;
    }

    AnimeLevel level;
    level.name = QStringLiteral("Level %1").arg(m_scene.levels.size() + 1);
    const int levelIndex = m_scene.levels.size();
    m_scene.levels.append(level);
    return levelIndex;
}

bool AnimeSceneModel::cellIsReferenced(const AnimeCell &cell) const
{
    if (cell.isEmpty()) {
        return false;
    }

    for (const AnimeColumn &column : m_scene.xsheet.columns) {
        for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
            const AnimeCell other = column.cellAt(row);
            if (other.levelIndex == cell.levelIndex && other.frameId == cell.frameId) {
                return true;
            }
        }
    }
    return false;
}

void AnimeSceneModel::pruneUnusedLevels()
{
    QVector<bool> used;
    used.resize(m_scene.levels.size());
    for (const AnimeColumn &column : m_scene.xsheet.columns) {
        for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
            const AnimeCell cell = column.cellAt(row);
            if (!cell.isEmpty() && cell.levelIndex >= 0 && cell.levelIndex < used.size()) {
                used[cell.levelIndex] = true;
            }
        }
    }

    QVector<int> remap;
    remap.resize(m_scene.levels.size());
    QVector<AnimeLevel> levels;
    for (int i = 0; i < m_scene.levels.size(); ++i) {
        if (used[i]) {
            remap[i] = levels.size();
            levels.append(m_scene.levels[i]);
        } else {
            remap[i] = -1;
        }
    }

    for (AnimeColumn &column : m_scene.xsheet.columns) {
        for (int row = 0; row < m_scene.xsheet.frameCount; ++row) {
            AnimeCell cell = column.cellAt(row);
            if (!cell.isEmpty() && cell.levelIndex >= 0 && cell.levelIndex < remap.size()) {
                cell.levelIndex = remap[cell.levelIndex];
                column.setCell(row, cell);
            }
        }
    }

    m_scene.levels = levels;
}

int AnimeSceneModel::nextFrameId() const
{
    int maxFrameId = 0;
    
    for (const AnimeLevel &level : m_scene.levels) {
        const auto& frameIds = level.frameIds(); 
        
        const auto maxFrameIdIt = std::max_element(frameIds.cbegin(), frameIds.cend());
        if (maxFrameIdIt != frameIds.cend()) {
            maxFrameId = std::max(maxFrameId, *maxFrameIdIt);
        }
    }
    
    return maxFrameId + 1;
}
