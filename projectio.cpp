#include "projectio.h"

#include <QBuffer>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <functional>

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

bool isUuidText(const QString &text)
{
    static const QRegularExpression uuidPattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return uuidPattern.match(text.trimmed()).hasMatch();
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
    object[QStringLiteral("property")] = stroke.property;
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
    if (stroke.penStyle != 1) {
        // Written only when non-solid, so files without styled strokes stay
        // byte-identical to what older builds produced.
        object[QStringLiteral("penStyle")] = stroke.penStyle;
    }
    return object;
}

AnimeVectorStroke strokeFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    AnimeVectorStroke stroke;
    stroke.id = object.value(QStringLiteral("id")).toInt();
    stroke.property = object.value(QStringLiteral("property")).toString();
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
    stroke.penStyle = object.value(QStringLiteral("penStyle")).toInt(1);
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
    object[QStringLiteral("property")] = fill.property;
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
    fill.property = object.value(QStringLiteral("property")).toString();
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
    root[QStringLiteral("sceneName")] = model.textId();
    root[QStringLiteral("sceneId")] = model.intId();
    root[QStringLiteral("currentFrame")] = model.currentFrame();
    root[QStringLiteral("currentLayer")] = model.currentLayer();
    root[QStringLiteral("currentAsset")] = model.currentAsset();
    root[QStringLiteral("scriptData")] = model.scriptData();

    const AnimeScene &scene = model.scene();
    QJsonObject xsheet;
    xsheet[QStringLiteral("frameCount")] = scene.xsheet.frameCount;

    QJsonArray columns;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (column.internal) {
            // Script-owned working layers are ephemeral. They only ever live
            // at the end of the column list (appended for the duration of a
            // gesture), so skipping them cannot shift saved layer indices.
            continue;
        }
        QJsonObject columnObject;
        columnObject[QStringLiteral("name")] = column.name;
        columnObject[QStringLiteral("id")] = column.id;
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
            cellObject[QStringLiteral("assetIndex")] = cell.assetIndex;
            cellObject[QStringLiteral("frameId")] = cell.frameId;
            cells.append(cellObject);
        }
        columnObject[QStringLiteral("cells")] = cells;
        columns.append(columnObject);
    }
    xsheet[QStringLiteral("columns")] = columns;
    root[QStringLiteral("xsheet")] = xsheet;

    // Layer groups. Internal (script-owned) columns are skipped above and
    // their leaves are simply dropped here; on load, normalizeLayerTree
    // re-appends anything the tree does not mention, so a file written by an
    // older build - or one whose tree lost entries - still opens with every
    // layer present and ungrouped.
    std::function<QJsonValue(const AnimeLayerNode &)> nodeToJson =
        [&](const AnimeLayerNode &node) -> QJsonValue {
        if (!node.isGroup()) {
            return QJsonValue(node.layerId);
        }
        QJsonObject groupObject;
        groupObject[QStringLiteral("groupId")] = node.groupId;
        groupObject[QStringLiteral("name")] = node.name;
        groupObject[QStringLiteral("collapsed")] = node.collapsed;
        QJsonArray children;
        for (const AnimeLayerNode &child : node.children) {
            children.append(nodeToJson(child));
        }
        groupObject[QStringLiteral("children")] = children;
        return groupObject;
    };
    QSet<int> savedColumnIds;
    for (const AnimeColumn &column : scene.xsheet.columns) {
        if (!column.internal) {
            savedColumnIds.insert(column.id);
        }
    }
    std::function<QJsonArray(const QVector<AnimeLayerNode> &)> treeToJson =
        [&](const QVector<AnimeLayerNode> &nodes) -> QJsonArray {
        QJsonArray array;
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                QJsonObject groupObject = nodeToJson(node).toObject();
                groupObject[QStringLiteral("children")] = treeToJson(node.children);
                if (!groupObject.value(QStringLiteral("children")).toArray().isEmpty()) {
                    array.append(groupObject);
                }
            } else if (savedColumnIds.contains(node.layerId)) {
                array.append(node.layerId);
            }
        }
        return array;
    };
    root[QStringLiteral("layerTree")] = treeToJson(scene.layerTree);

    QJsonArray assets;
    for (const AnimeAsset &asset : scene.assets) {
        if (asset.internal) {
            // Ephemeral working-layer asset: like internal columns it only
            // ever lives at the end of the list, so skipping cannot shift
            // the asset indices the saved cells reference.
            continue;
        }
        QJsonObject assetObject;
        assetObject[QStringLiteral("name")] = asset.name;
        assetObject[QStringLiteral("type")] = columnTypeName(asset.type);

        QJsonArray frames;
        for (int frameId : asset.frameIds()) {
            const AnimeVectorImageModel *image = asset.frame(frameId);
            if (!image) {
                continue;
            }
            QJsonObject frameObject;
            frameObject[QStringLiteral("frameId")] = frameId;
            frameObject[QStringLiteral("image")] = imageToJson(*image);
            frames.append(frameObject);
        }
        assetObject[QStringLiteral("frames")] = frames;
        assets.append(assetObject);
    }
    root[QStringLiteral("assets")] = assets;
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
    const QJsonValue sceneNameValue = root.value(QStringLiteral("sceneName"));
    const QJsonValue sceneIdValue = root.value(QStringLiteral("sceneId"));
    if (sceneIdValue.isDouble()) {
        scene.setIntId(sceneIdValue.toInt());
    }
    QString sceneName = sceneNameValue.toString(sceneIdValue.isString() ? sceneIdValue.toString() : QString());
    if (isUuidText(sceneName)) {
        sceneName.clear();
    }
    scene.setTextId(sceneName);
    scene.scriptData = root.value(QStringLiteral("scriptData")).toString();

    const QJsonObject xsheet = root.value(QStringLiteral("xsheet")).toObject();
    scene.xsheet.frameCount = qMax(1, xsheet.value(QStringLiteral("frameCount")).toInt(1));

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    scene.assets.reserve(assets.size());
    for (const QJsonValue &assetValue : assets) {
        const QJsonObject assetObject = assetValue.toObject();
        AnimeAsset asset;
        asset.name = assetObject.value(QStringLiteral("name")).toString();
        asset.type = columnTypeFromName(assetObject.value(QStringLiteral("type")).toString());

        const QJsonArray frames = assetObject.value(QStringLiteral("frames")).toArray();
        for (const QJsonValue &frameValue : frames) {
            const QJsonObject frameObject = frameValue.toObject();
            const int frameId = frameObject.value(QStringLiteral("frameId")).toInt();
            if (frameId <= 0) {
                continue;
            }
            loadImageFromJson(asset.frame(frameId, true), frameObject.value(QStringLiteral("image")));
        }
        scene.assets.append(asset);
    }

    const QJsonArray columns = xsheet.value(QStringLiteral("columns")).toArray();
    scene.xsheet.columns.reserve(columns.size());
    for (const QJsonValue &columnValue : columns) {
        const QJsonObject columnObject = columnValue.toObject();
        AnimeColumn column;
        column.name = columnObject.value(QStringLiteral("name")).toString();
        column.id = columnObject.value(QStringLiteral("id")).toInt(0);
        column.type = columnTypeFromName(columnObject.value(QStringLiteral("type")).toString());
        column.visible = columnObject.value(QStringLiteral("visible")).toBool(true);
        column.locked = columnObject.value(QStringLiteral("locked")).toBool(false);
        column.opacity = columnObject.value(QStringLiteral("opacity")).toDouble(1.0);

        const QJsonArray cells = columnObject.value(QStringLiteral("cells")).toArray();
        for (const QJsonValue &cellValue : cells) {
            const QJsonObject cellObject = cellValue.toObject();
            AnimeCell cell;
            cell.assetIndex = cellObject.value(QStringLiteral("assetIndex")).toInt(-1);
            cell.frameId = cellObject.value(QStringLiteral("frameId")).toInt(0);
            if (cell.assetIndex >= 0 && cell.assetIndex < scene.assets.size() && cell.frameId > 0) {
                column.setCell(cellObject.value(QStringLiteral("row")).toInt(), cell);
            }
        }
        scene.xsheet.columns.append(column);
    }

    // Layer groups. A file from an older build has no "layerTree" at all and
    // no column ids either; normalizeLayerTree (called below through the
    // model) then hands out fresh ids and puts every layer at the top level,
    // which is exactly the pre-grouping look.
    std::function<QVector<AnimeLayerNode>(const QJsonArray &)> treeFromJson =
        [&](const QJsonArray &array) -> QVector<AnimeLayerNode> {
        QVector<AnimeLayerNode> nodes;
        for (const QJsonValue &value : array) {
            AnimeLayerNode node;
            if (value.isObject()) {
                const QJsonObject groupObject = value.toObject();
                node.groupId = groupObject.value(QStringLiteral("groupId")).toInt(0);
                if (node.groupId <= 0) {
                    continue;
                }
                node.name = groupObject.value(QStringLiteral("name")).toString();
                node.collapsed = groupObject.value(QStringLiteral("collapsed")).toBool(false);
                node.children = treeFromJson(groupObject.value(QStringLiteral("children")).toArray());
            } else {
                node.layerId = value.toInt(0);
                if (node.layerId <= 0) {
                    continue;
                }
            }
            nodes.append(node);
        }
        return nodes;
    };
    scene.layerTree = treeFromJson(root.value(QStringLiteral("layerTree")).toArray());

    loaded.setCurrentFrame(root.value(QStringLiteral("currentFrame")).toInt(0));
    loaded.setCurrentLayer(root.value(QStringLiteral("currentLayer")).toInt(-1));
    loaded.setCurrentAsset(root.value(QStringLiteral("currentAsset")).toInt(-1));
    loaded.normalizeLayerTree();
    *model = loaded;
    return true;
}
