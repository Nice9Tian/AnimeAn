#include "animemodel.h"

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

int AnimeVectorImageModel::strokeCount() const
{
    return m_strokes.size();
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
    m_bounds = QRectF();
}

void AnimeVectorImageModel::rebuildBounds()
{
    m_bounds = QRectF();
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

bool AnimeSceneModel::deleteLayer(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_scene.xsheet.columns.size() ||
        m_scene.xsheet.columns.size() <= 1) {
        return false;
    }

    m_scene.xsheet.columns.removeAt(layerIndex);
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
    m_scene.xsheet.setCell(row, layerIndex, AnimeCell());
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
        cell.frameId = row + 1;
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
    return column && !column->locked;
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

int AnimeSceneModel::nextFrameId() const
{
    int maxFrameId = 0;
    for (const AnimeLevel &level : m_scene.levels) {
        for (int frameId : level.frameIds()) {
            if (frameId > maxFrameId) {
                maxFrameId = frameId;
            }
        }
    }
    return maxFrameId + 1;
}
