#include "projectio.h"

#include <QBuffer>
#include <QJsonArray>

namespace {
QString columnTypeName(AnimeColumnType type)
{
    switch (type) {
    case AnimeColumnType::Raster:
        return QStringLiteral("raster");
    case AnimeColumnType::Fill:
        return QStringLiteral("fill");
    case AnimeColumnType::Vector:
    default:
        return QStringLiteral("vector");
    }
}

AnimeColumnType columnTypeFromName(const QString &name)
{
    if (name == QStringLiteral("raster")) {
        return AnimeColumnType::Raster;
    }
    if (name == QStringLiteral("fill")) {
        return AnimeColumnType::Fill;
    }
    return AnimeColumnType::Vector;
}

QJsonObject pointToJson(const QPointF &point)
{
    QJsonObject object;
    object[QStringLiteral("x")] = point.x();
    object[QStringLiteral("y")] = point.y();
    return object;
}

QPointF pointFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return QPointF(object.value(QStringLiteral("x")).toDouble(),
                   object.value(QStringLiteral("y")).toDouble());
}

QJsonObject colorToJson(const QColor &color)
{
    QJsonObject object;
    object[QStringLiteral("name")] = color.name(QColor::HexArgb);
    return object;
}

QColor colorFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QColor color(object.value(QStringLiteral("name")).toString(QStringLiteral("#ff000000")));
    return color.isValid() ? color : QColor(Qt::black);
}

QJsonObject pathToJson(const QPainterPath &path)
{
    QJsonObject object;
    object[QStringLiteral("fillRule")] = static_cast<int>(path.fillRule());

    QJsonArray elements;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        QJsonObject elementObject;
        elementObject[QStringLiteral("type")] = static_cast<int>(element.type);
        elementObject[QStringLiteral("x")] = element.x;
        elementObject[QStringLiteral("y")] = element.y;
        elements.append(elementObject);
    }
    object[QStringLiteral("elements")] = elements;
    return object;
}

QPainterPath pathFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QJsonArray elements = object.value(QStringLiteral("elements")).toArray();
    QPainterPath path;
    path.setFillRule(static_cast<Qt::FillRule>(object.value(QStringLiteral("fillRule")).toInt(Qt::OddEvenFill)));

    for (int i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements[i].toObject();
        const int type = element.value(QStringLiteral("type")).toInt();
        const QPointF point(element.value(QStringLiteral("x")).toDouble(),
                            element.value(QStringLiteral("y")).toDouble());
        if (type == QPainterPath::MoveToElement) {
            path.moveTo(point);
        } else if (type == QPainterPath::LineToElement) {
            path.lineTo(point);
        } else if (type == QPainterPath::CurveToElement && i + 2 < elements.size()) {
            const QJsonObject controlElement = elements[i + 1].toObject();
            const QJsonObject endElement = elements[i + 2].toObject();
            const QPointF control2(controlElement.value(QStringLiteral("x")).toDouble(),
                                   controlElement.value(QStringLiteral("y")).toDouble());
            const QPointF endPoint(endElement.value(QStringLiteral("x")).toDouble(),
                                   endElement.value(QStringLiteral("y")).toDouble());
            path.cubicTo(point, control2, endPoint);
            i += 2;
        }
    }
    return path;
}

QVector<QPointF> pointsFromJson(const QJsonValue &value)
{
    QVector<QPointF> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue &point : array) {
        points.append(pointFromJson(point));
    }
    return points;
}

QJsonObject strokeToJson(const AnimeVectorStroke &stroke)
{
    QJsonObject object;
    object[QStringLiteral("id")] = stroke.id;
    QJsonArray points;
    for (const QPointF &point : stroke.points) {
        points.append(pointToJson(point));
    }
    object[QStringLiteral("points")] = points;
    QJsonArray lengths;
    for (qreal length : stroke.lengths) {
        lengths.append(length);
    }
    object[QStringLiteral("lengths")] = lengths;
    object[QStringLiteral("totalLength")] = stroke.totalLength;
    object[QStringLiteral("path")] = pathToJson(stroke.path);
    object[QStringLiteral("boundsX")] = stroke.bounds.x();
    object[QStringLiteral("boundsY")] = stroke.bounds.y();
    object[QStringLiteral("boundsW")] = stroke.bounds.width();
    object[QStringLiteral("boundsH")] = stroke.bounds.height();
    object[QStringLiteral("color")] = colorToJson(stroke.color);
    object[QStringLiteral("width")] = stroke.width;
    return object;
}

AnimeVectorStroke strokeFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorStroke stroke;
    stroke.id = object.value(QStringLiteral("id")).toInt();
    stroke.points = pointsFromJson(object.value(QStringLiteral("points")));
    const QJsonArray lengths = object.value(QStringLiteral("lengths")).toArray();
    stroke.lengths.reserve(lengths.size());
    for (const QJsonValue &length : lengths) {
        stroke.lengths.append(length.toDouble());
    }
    stroke.totalLength = object.value(QStringLiteral("totalLength")).toDouble();
    stroke.path = pathFromJson(object.value(QStringLiteral("path")));
    stroke.bounds = QRectF(object.value(QStringLiteral("boundsX")).toDouble(),
                           object.value(QStringLiteral("boundsY")).toDouble(),
                           object.value(QStringLiteral("boundsW")).toDouble(),
                           object.value(QStringLiteral("boundsH")).toDouble());
    stroke.color = colorFromJson(object.value(QStringLiteral("color")));
    stroke.width = object.value(QStringLiteral("width")).toDouble(3.0);
    return stroke;
}

QJsonObject strokeNodeToJson(const AnimeVectorStrokeNode &node)
{
    QJsonObject object;
    object[QStringLiteral("stroke")] = strokeToJson(node.stroke);
    object[QStringLiteral("groupId")] = QJsonArray();
    object[QStringLiteral("isPoint")] = node.isPoint;
    object[QStringLiteral("isNewForFill")] = node.isNewForFill;
    object[QStringLiteral("selected")] = node.selected;
    return object;
}

AnimeVectorStrokeNode strokeNodeFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorStrokeNode node;
    node.stroke = strokeFromJson(object.value(QStringLiteral("stroke")));
    node.isPoint = object.value(QStringLiteral("isPoint")).toBool(false);
    node.isNewForFill = object.value(QStringLiteral("isNewForFill")).toBool(true);
    node.selected = object.value(QStringLiteral("selected")).toBool(false);
    return node;
}

QJsonObject fillRegionToJson(const AnimeVectorFillRegion &fill)
{
    QJsonObject object;
    object[QStringLiteral("id")] = fill.id;
    object[QStringLiteral("seedPoint")] = pointToJson(fill.seedPoint);
    object[QStringLiteral("path")] = pathToJson(fill.path);
    object[QStringLiteral("boundsX")] = fill.bounds.x();
    object[QStringLiteral("boundsY")] = fill.bounds.y();
    object[QStringLiteral("boundsW")] = fill.bounds.width();
    object[QStringLiteral("boundsH")] = fill.bounds.height();
    object[QStringLiteral("color")] = colorToJson(fill.color);
    object[QStringLiteral("sourceLayerIndex")] = fill.sourceLayerIndex;
    object[QStringLiteral("basedOnAllLayers")] = fill.basedOnAllLayers;
    return object;
}

AnimeVectorFillRegion fillRegionFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorFillRegion fill;
    fill.id = object.value(QStringLiteral("id")).toInt();
    fill.seedPoint = pointFromJson(object.value(QStringLiteral("seedPoint")));
    fill.path = pathFromJson(object.value(QStringLiteral("path")));
    fill.bounds = QRectF(object.value(QStringLiteral("boundsX")).toDouble(),
                         object.value(QStringLiteral("boundsY")).toDouble(),
                         object.value(QStringLiteral("boundsW")).toDouble(),
                         object.value(QStringLiteral("boundsH")).toDouble());
    fill.color = colorFromJson(object.value(QStringLiteral("color")));
    fill.sourceLayerIndex = object.value(QStringLiteral("sourceLayerIndex")).toInt(-1);
    fill.basedOnAllLayers = object.value(QStringLiteral("basedOnAllLayers")).toBool(false);
    return fill;
}

QJsonObject rasterToJson(const AnimeRasterImage &raster)
{
    QJsonObject object;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    raster.image.save(&buffer, "PNG");
    object[QStringLiteral("imageBase64")] = QString::fromLatin1(bytes.toBase64());
    object[QStringLiteral("topLeftX")] = raster.topLeft.x();
    object[QStringLiteral("topLeftY")] = raster.topLeft.y();
    return object;
}

AnimeRasterImage rasterFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeRasterImage raster;
    const QByteArray bytes = QByteArray::fromBase64(object.value(QStringLiteral("imageBase64")).toString().toLatin1());
    raster.image.loadFromData(bytes, "PNG");
    raster.topLeft = QPointF(object.value(QStringLiteral("topLeftX")).toDouble(),
                             object.value(QStringLiteral("topLeftY")).toDouble());
    return raster;
}

QJsonObject imageToJson(const AnimeVectorImageModel &image)
{
    QJsonObject object;
    if (image.hasRaster()) {
        object[QStringLiteral("raster")] = rasterToJson(image.raster());
    }

    QJsonArray strokes;
    for (const AnimeVectorStrokeNode &node : image.strokeNodes()) {
        strokes.append(strokeNodeToJson(node));
    }
    object[QStringLiteral("strokes")] = strokes;

    QJsonArray fills;
    for (const AnimeVectorFillRegion &fill : image.fillRegions()) {
        fills.append(fillRegionToJson(fill));
    }
    object[QStringLiteral("fills")] = fills;
    return object;
}

void loadImageFromJson(AnimeVectorImageModel *image, const QJsonValue &value)
{
    if (!image) {
        return;
    }

    image->clear();
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("raster"))) {
        const AnimeRasterImage raster = rasterFromJson(object.value(QStringLiteral("raster")));
        if (!raster.image.isNull()) {
            image->setRasterImage(raster.image, raster.topLeft);
        }
    }

    const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
    for (const QJsonValue &stroke : strokes) {
        image->addStrokeNode(strokeNodeFromJson(stroke));
    }

    const QJsonArray fills = object.value(QStringLiteral("fills")).toArray();
    for (const QJsonValue &fill : fills) {
        image->addFillRegion(fillRegionFromJson(fill));
    }
}
}

QString projectFilter()
{
    return QStringLiteral("AnimeAn Projects (*.animean);;All Files (*)");
}

QJsonObject modelToJson(const AnimeSceneModel &model)
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("AnimeAn Project");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("currentFrame")] = model.currentFrame();
    root[QStringLiteral("currentLayer")] = model.currentLayer();
    root[QStringLiteral("currentAsset")] = model.currentAsset();

    const AnimeScene &scene = model.scene();
    QJsonObject xsheet;
    xsheet[QStringLiteral("frameCount")] = scene.xsheet.frameCount;

    QJsonArray columns;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        QJsonObject columnObject;
        columnObject[QStringLiteral("name")] = column.name;
        columnObject[QStringLiteral("type")] = columnTypeName(column.type);
        columnObject[QStringLiteral("visible")] = column.visible;
        columnObject[QStringLiteral("locked")] = column.locked;
        columnObject[QStringLiteral("opacity")] = column.opacity;

        QJsonArray cells;
        for (int row = 0; row < scene.xsheet.frameCount; ++row) {
            const AnimeCell cell = column.cellAt(row);
            if (cell.isEmpty()) {
                continue;
            }
            QJsonObject cellObject;
            cellObject[QStringLiteral("row")] = row;
            cellObject[QStringLiteral("levelIndex")] = cell.levelIndex;
            cellObject[QStringLiteral("frameId")] = cell.frameId;
            cells.append(cellObject);
        }
        columnObject[QStringLiteral("cells")] = cells;
        columns.append(columnObject);
    }
    xsheet[QStringLiteral("columns")] = columns;
    root[QStringLiteral("xsheet")] = xsheet;

    QJsonArray levels;
    for (const AnimeLevel &level : scene.levels) {
        QJsonObject levelObject;
        levelObject[QStringLiteral("name")] = level.name;
        levelObject[QStringLiteral("type")] = columnTypeName(level.type);

        QJsonArray frames;
        for (int frameId : level.frameIds()) {
            const AnimeVectorImageModel *image = level.frame(frameId);
            if (!image) {
                continue;
            }
            QJsonObject frameObject;
            frameObject[QStringLiteral("frameId")] = frameId;
            frameObject[QStringLiteral("image")] = imageToJson(*image);
            frames.append(frameObject);
        }
        levelObject[QStringLiteral("frames")] = frames;
        levels.append(levelObject);
    }
    root[QStringLiteral("levels")] = levels;
    return root;
}

bool modelFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error)
{
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("AnimeAn Project")) {
        if (error) {
            *error = QStringLiteral("Unsupported project file.");
        }
        return false;
    }

    AnimeSceneModel loaded;
    AnimeScene &scene = loaded.scene();
    scene = AnimeScene();

    const QJsonObject xsheet = root.value(QStringLiteral("xsheet")).toObject();
    scene.xsheet.frameCount = qMax(1, xsheet.value(QStringLiteral("frameCount")).toInt(1));

    const QJsonArray levels = root.value(QStringLiteral("levels")).toArray();
    scene.levels.reserve(levels.size());
    for (const QJsonValue &levelValue : levels) {
        const QJsonObject levelObject = levelValue.toObject();
        AnimeLevel level;
        level.name = levelObject.value(QStringLiteral("name")).toString();
        level.type = columnTypeFromName(levelObject.value(QStringLiteral("type")).toString());

        const QJsonArray frames = levelObject.value(QStringLiteral("frames")).toArray();
        for (const QJsonValue &frameValue : frames) {
            const QJsonObject frameObject = frameValue.toObject();
            const int frameId = frameObject.value(QStringLiteral("frameId")).toInt();
            if (frameId <= 0) {
                continue;
            }
            loadImageFromJson(level.frame(frameId, true), frameObject.value(QStringLiteral("image")));
        }
        scene.levels.append(level);
    }

    const QJsonArray columns = xsheet.value(QStringLiteral("columns")).toArray();
    scene.xsheet.columns.reserve(columns.size());
    for (const QJsonValue &columnValue : columns) {
        const QJsonObject columnObject = columnValue.toObject();
        AnimeColumn column;
        column.name = columnObject.value(QStringLiteral("name")).toString();
        column.type = columnTypeFromName(columnObject.value(QStringLiteral("type")).toString());
        column.visible = columnObject.value(QStringLiteral("visible")).toBool(true);
        column.locked = columnObject.value(QStringLiteral("locked")).toBool(false);
        column.opacity = columnObject.value(QStringLiteral("opacity")).toDouble(1.0);

        const QJsonArray cells = columnObject.value(QStringLiteral("cells")).toArray();
        for (const QJsonValue &cellValue : cells) {
            const QJsonObject cellObject = cellValue.toObject();
            AnimeCell cell;
            cell.levelIndex = cellObject.value(QStringLiteral("levelIndex")).toInt(-1);
            cell.frameId = cellObject.value(QStringLiteral("frameId")).toInt(0);
            if (cell.levelIndex >= 0 && cell.levelIndex < scene.levels.size() && cell.frameId > 0) {
                column.setCell(cellObject.value(QStringLiteral("row")).toInt(), cell);
            }
        }
        scene.xsheet.columns.append(column);
    }

    loaded.setCurrentFrame(root.value(QStringLiteral("currentFrame")).toInt(0));
    loaded.setCurrentLayer(root.value(QStringLiteral("currentLayer")).toInt(-1));
    loaded.setCurrentAsset(root.value(QStringLiteral("currentAsset")).toInt(-1));
    *model = loaded;
    return true;
}
