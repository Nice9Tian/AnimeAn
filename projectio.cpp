#include "projectio.h"


#include <functional>

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/json.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"

using namespace AnimeCore;

namespace {
String columnTypeName(AnimeColumnType type)
{
    switch (type) {
    case AnimeColumnType::Raster:
        return ANIME_STR("raster");
    case AnimeColumnType::Fill:
        return ANIME_STR("fill");
    case AnimeColumnType::Vector:
    default:
        return ANIME_STR("vector");
    }
}

AnimeColumnType columnTypeFromName(const String &name)
{
    if (name == ANIME_STR("raster")) {
        return AnimeColumnType::Raster;
    }
    if (name == ANIME_STR("fill")) {
        return AnimeColumnType::Fill;
    }
    return AnimeColumnType::Vector;
}

bool isUuidText(const String &text)
{
    static const QRegularExpression uuidPattern(
        ANIME_STR("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return uuidPattern.match(text.trimmed()).hasMatch();
}

JsonObject pointToJson(const PointF &point)
{
    JsonObject object;
    object[ANIME_STR("x")] = point.x();
    object[ANIME_STR("y")] = point.y();
    return object;
}

PointF pointFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    return PointF(object.value(ANIME_STR("x")).toDouble(),
                   object.value(ANIME_STR("y")).toDouble());
}

JsonObject colorToJson(const Color &color)
{
    JsonObject object;
    object[ANIME_STR("name")] = color.name(Color::HexArgb);
    return object;
}

Color colorFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    const Color color(object.value(ANIME_STR("name")).toString(ANIME_STR("#ff000000")));
    return color.isValid() ? color : Color(Qt::black);
}

JsonObject pathToJson(const Path &path)
{
    JsonObject object;
    object[ANIME_STR("fillRule")] = static_cast<int>(path.fillRule());

    JsonArray elements;
    for (int i = 0; i < path.elementCount(); ++i) {
        const Path::Element element = path.elementAt(i);
        JsonObject elementObject;
        elementObject[ANIME_STR("type")] = static_cast<int>(element.type);
        elementObject[ANIME_STR("x")] = element.x;
        elementObject[ANIME_STR("y")] = element.y;
        elements.append(elementObject);
    }
    object[ANIME_STR("elements")] = elements;
    return object;
}

Path pathFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    const JsonArray elements = object.value(ANIME_STR("elements")).toArray();
    Path path;
    path.setFillRule(static_cast<Qt::FillRule>(object.value(ANIME_STR("fillRule")).toInt(Qt::OddEvenFill)));

    for (int i = 0; i < elements.size(); ++i) {
        const JsonObject element = elements[i].toObject();
        const int type = element.value(ANIME_STR("type")).toInt();
        const PointF point(element.value(ANIME_STR("x")).toDouble(),
                            element.value(ANIME_STR("y")).toDouble());
        if (type == Path::MoveToElement) {
            path.moveTo(point);
        } else if (type == Path::LineToElement) {
            path.lineTo(point);
        } else if (type == Path::CurveToElement && i + 2 < elements.size()) {
            const JsonObject controlElement = elements[i + 1].toObject();
            const JsonObject endElement = elements[i + 2].toObject();
            const PointF control2(controlElement.value(ANIME_STR("x")).toDouble(),
                                   controlElement.value(ANIME_STR("y")).toDouble());
            const PointF endPoint(endElement.value(ANIME_STR("x")).toDouble(),
                                   endElement.value(ANIME_STR("y")).toDouble());
            path.cubicTo(point, control2, endPoint);
            i += 2;
        }
    }
    return path;
}

Vector<PointF> pointsFromJson(const JsonValue &value)
{
    Vector<PointF> points;
    const JsonArray array = value.toArray();
    points.reserve(array.size());
    for (const JsonValue &point : array) {
        points.append(pointFromJson(point));
    }
    return points;
}

JsonObject strokeToJson(const AnimeVectorStroke &stroke)
{
    JsonObject object;
    object[ANIME_STR("id")] = stroke.id;
    object[ANIME_STR("property")] = stroke.property;
    JsonArray points;
    for (const PointF &point : stroke.points) {
        points.append(pointToJson(point));
    }
    object[ANIME_STR("points")] = points;
    JsonArray lengths;
    for (double length : stroke.lengths) {
        lengths.append(length);
    }
    object[ANIME_STR("lengths")] = lengths;
    object[ANIME_STR("totalLength")] = stroke.totalLength;
    object[ANIME_STR("path")] = pathToJson(stroke.path);
    object[ANIME_STR("boundsX")] = stroke.bounds.x();
    object[ANIME_STR("boundsY")] = stroke.bounds.y();
    object[ANIME_STR("boundsW")] = stroke.bounds.width();
    object[ANIME_STR("boundsH")] = stroke.bounds.height();
    object[ANIME_STR("color")] = colorToJson(stroke.color);
    object[ANIME_STR("width")] = stroke.width;
    if (stroke.penStyle != 1) {
        // Written only when non-solid, so files without styled strokes stay
        // byte-identical to what older builds produced.
        object[ANIME_STR("penStyle")] = stroke.penStyle;
    }
    return object;
}

AnimeVectorStroke strokeFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    AnimeVectorStroke stroke;
    stroke.id = object.value(ANIME_STR("id")).toInt();
    stroke.property = object.value(ANIME_STR("property")).toString();
    stroke.points = pointsFromJson(object.value(ANIME_STR("points")));
    const JsonArray lengths = object.value(ANIME_STR("lengths")).toArray();
    stroke.lengths.reserve(lengths.size());
    for (const JsonValue &length : lengths) {
        stroke.lengths.append(length.toDouble());
    }
    stroke.totalLength = object.value(ANIME_STR("totalLength")).toDouble();
    stroke.path = pathFromJson(object.value(ANIME_STR("path")));
    stroke.bounds = RectF(object.value(ANIME_STR("boundsX")).toDouble(),
                           object.value(ANIME_STR("boundsY")).toDouble(),
                           object.value(ANIME_STR("boundsW")).toDouble(),
                           object.value(ANIME_STR("boundsH")).toDouble());
    stroke.color = colorFromJson(object.value(ANIME_STR("color")));
    stroke.width = object.value(ANIME_STR("width")).toDouble(3.0);
    stroke.penStyle = object.value(ANIME_STR("penStyle")).toInt(1);
    return stroke;
}

JsonObject strokeNodeToJson(const AnimeVectorStrokeNode &node)
{
    JsonObject object;
    object[ANIME_STR("stroke")] = strokeToJson(node.stroke);
    object[ANIME_STR("groupId")] = JsonArray();
    object[ANIME_STR("isPoint")] = node.isPoint;
    object[ANIME_STR("isNewForFill")] = node.isNewForFill;
    object[ANIME_STR("selected")] = node.selected;
    return object;
}

AnimeVectorStrokeNode strokeNodeFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    AnimeVectorStrokeNode node;
    node.stroke = strokeFromJson(object.value(ANIME_STR("stroke")));
    node.isPoint = object.value(ANIME_STR("isPoint")).toBool(false);
    node.isNewForFill = object.value(ANIME_STR("isNewForFill")).toBool(true);
    node.selected = object.value(ANIME_STR("selected")).toBool(false);
    return node;
}

JsonObject fillRegionToJson(const AnimeVectorFillRegion &fill)
{
    JsonObject object;
    object[ANIME_STR("id")] = fill.id;
    object[ANIME_STR("property")] = fill.property;
    object[ANIME_STR("seedPoint")] = pointToJson(fill.seedPoint);
    object[ANIME_STR("path")] = pathToJson(fill.path);
    object[ANIME_STR("boundsX")] = fill.bounds.x();
    object[ANIME_STR("boundsY")] = fill.bounds.y();
    object[ANIME_STR("boundsW")] = fill.bounds.width();
    object[ANIME_STR("boundsH")] = fill.bounds.height();
    object[ANIME_STR("color")] = colorToJson(fill.color);
    object[ANIME_STR("sourceLayerIndex")] = fill.sourceLayerIndex;
    object[ANIME_STR("basedOnAllLayers")] = fill.basedOnAllLayers;
    return object;
}

AnimeVectorFillRegion fillRegionFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    AnimeVectorFillRegion fill;
    fill.id = object.value(ANIME_STR("id")).toInt();
    fill.property = object.value(ANIME_STR("property")).toString();
    fill.seedPoint = pointFromJson(object.value(ANIME_STR("seedPoint")));
    fill.path = pathFromJson(object.value(ANIME_STR("path")));
    fill.bounds = RectF(object.value(ANIME_STR("boundsX")).toDouble(),
                         object.value(ANIME_STR("boundsY")).toDouble(),
                         object.value(ANIME_STR("boundsW")).toDouble(),
                         object.value(ANIME_STR("boundsH")).toDouble());
    fill.color = colorFromJson(object.value(ANIME_STR("color")));
    fill.sourceLayerIndex = object.value(ANIME_STR("sourceLayerIndex")).toInt(-1);
    fill.basedOnAllLayers = object.value(ANIME_STR("basedOnAllLayers")).toBool(false);
    return fill;
}

JsonObject rasterToJson(const AnimeRasterImage &raster)
{
    JsonObject object;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    raster.image.save(&buffer, "PNG");
    object[ANIME_STR("imageBase64")] = String::fromLatin1(bytes.toBase64());
    object[ANIME_STR("topLeftX")] = raster.topLeft.x();
    object[ANIME_STR("topLeftY")] = raster.topLeft.y();
    return object;
}

AnimeRasterImage rasterFromJson(const JsonValue &value)
{
    const JsonObject object = value.toObject();
    AnimeRasterImage raster;
    const QByteArray bytes = QByteArray::fromBase64(object.value(ANIME_STR("imageBase64")).toString().toLatin1());
    raster.image.loadFromData(bytes, "PNG");
    raster.topLeft = PointF(object.value(ANIME_STR("topLeftX")).toDouble(),
                             object.value(ANIME_STR("topLeftY")).toDouble());
    return raster;
}

JsonObject imageToJson(const AnimeVectorImageModel &image)
{
    JsonObject object;
    if (image.hasRaster()) {
        object[ANIME_STR("raster")] = rasterToJson(image.raster());
    }

    JsonArray strokes;
    for (const AnimeVectorStrokeNode &node : image.strokeNodes()) {
        strokes.append(strokeNodeToJson(node));
    }
    object[ANIME_STR("strokes")] = strokes;

    JsonArray fills;
    for (const AnimeVectorFillRegion &fill : image.fillRegions()) {
        fills.append(fillRegionToJson(fill));
    }
    object[ANIME_STR("fills")] = fills;
    return object;
}

void loadImageFromJson(AnimeVectorImageModel *image, const JsonValue &value)
{
    if (!image) {
        return;
    }

    image->clear();
    const JsonObject object = value.toObject();
    if (object.contains(ANIME_STR("raster"))) {
        const AnimeRasterImage raster = rasterFromJson(object.value(ANIME_STR("raster")));
        if (!raster.image.isNull()) {
            image->setRasterImage(raster.image, raster.topLeft);
        }
    }

    const JsonArray strokes = object.value(ANIME_STR("strokes")).toArray();
    for (const JsonValue &stroke : strokes) {
        image->addStrokeNode(strokeNodeFromJson(stroke));
    }

    const JsonArray fills = object.value(ANIME_STR("fills")).toArray();
    for (const JsonValue &fill : fills) {
        image->addFillRegion(fillRegionFromJson(fill));
    }
}
}

String projectOpenFilter()
{
    return ANIME_STR("AnimeAn Projects (*.anproj);;All Files (*)");
}

String projectSaveFilter()
{
    return ANIME_STR("AnimeAn Projects (*.anproj);;All Files (*)");
}

String textureViewOpenFilter()
{
    return ANIME_STR("AnimeAn Texture Views (*.textureview);;All Files (*)");
}

String textureViewSaveFilter()
{
    return ANIME_STR("AnimeAn Texture Views (*.textureview);;All Files (*)");
}

namespace {
String ensureFileExtension(const String &fileName, const String &extension)
{
    if (fileName.isEmpty()) {
        return fileName;
    }

    const QFileInfo info(fileName);
    const String suffix = info.suffix();
    if (suffix.compare(extension, Qt::CaseInsensitive) == 0) {
        return fileName;
    }

    // Replace extensions that AnimeAn owns, so saving one document type over
    // a name carrying the other type's suffix produces drawing.anproj rather
    // than drawing.textureview.anproj.
    if (suffix.compare(ANIME_STR("anproj"), Qt::CaseInsensitive) == 0
        || suffix.compare(ANIME_STR("textureview"), Qt::CaseInsensitive) == 0) {
        return fileName.left(fileName.size() - suffix.size()) + extension;
    }

    return fileName + Latin1Char('.') + extension;
}

// The format/version envelope belongs to the FILE; a scene payload describes
// one board and carries no envelope at all. Each document writer stamps its
// own envelope on top of sceneToJson, and each reader validates it before
// handing the object to sceneFromJson. This keeps embedded payloads free to
// evolve without a second, hidden version field.
JsonObject sceneToJson(const AnimeSceneModel &model);
bool sceneFromJson(const JsonObject &root, AnimeSceneModel *model, String *error);

bool sceneHasData(const JsonObject &root)
{
    // Every writer emits both keys, even for an empty scene. A file carrying
    // a valid envelope but neither key is damaged; loading it as a blank
    // scene and reporting success would invite an overwrite of the original.
    return root.value(ANIME_STR("xsheet")).isObject()
           || root.value(ANIME_STR("assets")).isArray();
}
}

String ensureProjectFileExtension(const String &fileName)
{
    return ensureFileExtension(fileName, ANIME_STR("anproj"));
}

String ensureTextureViewFileExtension(const String &fileName)
{
    return ensureFileExtension(fileName, ANIME_STR("textureview"));
}

namespace {
JsonObject sceneToJson(const AnimeSceneModel &model)
{
    JsonObject root;
    root[ANIME_STR("sceneName")] = model.textId();
    root[ANIME_STR("sceneId")] = model.intId();
    root[ANIME_STR("currentFrame")] = model.currentFrame();
    root[ANIME_STR("currentLayer")] = model.currentLayer();
    root[ANIME_STR("currentAsset")] = model.currentAsset();
    root[ANIME_STR("scriptData")] = model.scriptData();

    const AnimeScene &scene = model.scene();
    root[ANIME_STR("canvasWidth")] = model.canvasSize().width();
    root[ANIME_STR("canvasHeight")] = model.canvasSize().height();
    root[ANIME_STR("playbackFps")] = model.playbackFps();
    JsonObject xsheet;
    xsheet[ANIME_STR("frameCount")] = scene.xsheet.frameCount;

    // Which column ids actually reach the file. Internal columns are skipped
    // below, so both the layer tree AND a parent link that names one has to
    // be filtered against this - a saved id nobody writes is a dangling
    // reference the loader would have to guess about.
    Set<int> savedColumnIds;
    for (const AnimeColumn &column : scene.xsheet.columns) {
        if (!column.internal) {
            savedColumnIds.insert(column.id);
        }
    }

    JsonArray columns;
    for (int columnIndex = 0; columnIndex < scene.xsheet.columns.size(); ++columnIndex) {
        const AnimeColumn &column = scene.xsheet.columns[columnIndex];
        if (column.internal) {
            // Script-owned working layers are ephemeral. They only ever live
            // at the end of the column list (appended for the duration of a
            // gesture), so skipping them cannot shift saved layer indices.
            continue;
        }
        JsonObject columnObject;
        columnObject[ANIME_STR("name")] = column.name;
        columnObject[ANIME_STR("id")] = column.id;
        columnObject[ANIME_STR("type")] = columnTypeName(column.type);
        columnObject[ANIME_STR("visible")] = column.visible;
        columnObject[ANIME_STR("locked")] = column.locked;
        columnObject[ANIME_STR("opacity")] = column.opacity;
        // Optional, like penStyle: a project with no parented layer stays
        // byte-identical to what earlier builds wrote, and a link to a column
        // that is not being saved is dropped rather than written dangling.
        if (column.parentLayerId > 0 && savedColumnIds.contains(column.parentLayerId)) {
            columnObject[ANIME_STR("parentLayerId")] = column.parentLayerId;
        }

        JsonArray cells;
        for (int row = 0; row < scene.xsheet.frameCount; ++row) {
            const AnimeCell cell = column.cellAt(row);
            if (cell.isEmpty()) {
                continue;
            }
            JsonObject cellObject;
            cellObject[ANIME_STR("row")] = row;
            cellObject[ANIME_STR("assetIndex")] = cell.assetIndex;
            cellObject[ANIME_STR("frameId")] = cell.frameId;
            cells.append(cellObject);
        }
        columnObject[ANIME_STR("cells")] = cells;
        columns.append(columnObject);
    }
    xsheet[ANIME_STR("columns")] = columns;

    // Frame names. Written only when a row actually carries one, so a file
    // from a project nobody renamed stays byte-identical to what earlier
    // builds produced; a reader without the key sees the row numbers.
    JsonObject frameNames;
    for (auto it = scene.xsheet.frameNames.constBegin();
         it != scene.xsheet.frameNames.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= scene.xsheet.frameCount || it.value().isEmpty()) {
            continue;
        }
        frameNames[String::number(it.key())] = it.value();
    }
    if (!frameNames.isEmpty()) {
        xsheet[ANIME_STR("frameNames")] = frameNames;
    }
    root[ANIME_STR("xsheet")] = xsheet;

    // Layer groups. Internal (script-owned) columns are skipped above and
    // their leaves are simply dropped here; on load, normalizeLayerTree
    // re-appends anything the tree does not mention, so a file written by an
    // older build - or one whose tree lost entries - still opens with every
    // layer present and ungrouped.
    std::function<JsonValue(const AnimeLayerNode &)> nodeToJson =
        [&](const AnimeLayerNode &node) -> JsonValue {
        if (!node.isGroup()) {
            return JsonValue(node.layerId);
        }
        JsonObject groupObject;
        groupObject[ANIME_STR("groupId")] = node.groupId;
        groupObject[ANIME_STR("name")] = node.name;
        groupObject[ANIME_STR("collapsed")] = node.collapsed;
        if (!node.tag.isEmpty()) {
            groupObject[ANIME_STR("tag")] = node.tag;
        }
        JsonArray children;
        for (const AnimeLayerNode &child : node.children) {
            children.append(nodeToJson(child));
        }
        groupObject[ANIME_STR("children")] = children;
        return groupObject;
    };
    std::function<JsonArray(const Vector<AnimeLayerNode> &)> treeToJson =
        [&](const Vector<AnimeLayerNode> &nodes) -> JsonArray {
        JsonArray array;
        for (const AnimeLayerNode &node : nodes) {
            if (node.isGroup()) {
                JsonObject groupObject = nodeToJson(node).toObject();
                groupObject[ANIME_STR("children")] = treeToJson(node.children);
                if (!groupObject.value(ANIME_STR("children")).toArray().isEmpty()) {
                    array.append(groupObject);
                }
            } else if (savedColumnIds.contains(node.layerId)) {
                array.append(node.layerId);
            }
        }
        return array;
    };
    root[ANIME_STR("layerTree")] = treeToJson(scene.layerTree);

    JsonArray assets;
    for (const AnimeAsset &asset : scene.assets) {
        if (asset.internal) {
            // Ephemeral working-layer asset: like internal columns it only
            // ever lives at the end of the list, so skipping cannot shift
            // the asset indices the saved cells reference.
            continue;
        }
        JsonObject assetObject;
        assetObject[ANIME_STR("name")] = asset.name;
        assetObject[ANIME_STR("type")] = columnTypeName(asset.type);

        JsonArray frames;
        for (int frameId : asset.frameIds()) {
            const AnimeVectorImageModel *image = asset.frame(frameId);
            if (!image) {
                continue;
            }
            JsonObject frameObject;
            frameObject[ANIME_STR("frameId")] = frameId;
            frameObject[ANIME_STR("image")] = imageToJson(*image);
            frames.append(frameObject);
        }
        assetObject[ANIME_STR("frames")] = frames;
        assets.append(assetObject);
    }
    root[ANIME_STR("assets")] = assets;
    return root;
}

bool sceneFromJson(const JsonObject &root, AnimeSceneModel *model, String *error)
{
    Q_UNUSED(error);
    AnimeSceneModel loaded;
    AnimeScene &scene = loaded.scene();
    scene = AnimeScene();
    const JsonValue sceneNameValue = root.value(ANIME_STR("sceneName"));
    const JsonValue sceneIdValue = root.value(ANIME_STR("sceneId"));
    if (sceneIdValue.isDouble()) {
        scene.setIntId(sceneIdValue.toInt());
    }
    String sceneName = sceneNameValue.toString(sceneIdValue.isString() ? sceneIdValue.toString() : String());
    if (isUuidText(sceneName)) {
        sceneName.clear();
    }
    scene.setTextId(sceneName);
    scene.scriptData = root.value(ANIME_STR("scriptData")).toString();

    const JsonObject xsheet = root.value(ANIME_STR("xsheet")).toObject();
    scene.xsheet.frameCount = std::max(1, xsheet.value(ANIME_STR("frameCount")).toInt(1));

    // Optional: a file without the key simply has no named rows.
    const JsonObject frameNames = xsheet.value(ANIME_STR("frameNames")).toObject();
    for (auto it = frameNames.constBegin(); it != frameNames.constEnd(); ++it) {
        bool ok = false;
        const int row = it.key().toInt(&ok);
        const String name = it.value().toString();
        if (!ok || row < 0 || row >= scene.xsheet.frameCount || name.isEmpty()) {
            continue;
        }
        scene.xsheet.frameNames.insert(row, name);
    }

    const JsonArray assets = root.value(ANIME_STR("assets")).toArray();
    scene.assets.reserve(assets.size());
    for (const JsonValue &assetValue : assets) {
        const JsonObject assetObject = assetValue.toObject();
        AnimeAsset asset;
        asset.name = assetObject.value(ANIME_STR("name")).toString();
        asset.type = columnTypeFromName(assetObject.value(ANIME_STR("type")).toString());

        const JsonArray frames = assetObject.value(ANIME_STR("frames")).toArray();
        for (const JsonValue &frameValue : frames) {
            const JsonObject frameObject = frameValue.toObject();
            const int frameId = frameObject.value(ANIME_STR("frameId")).toInt();
            if (frameId <= 0) {
                continue;
            }
            loadImageFromJson(asset.frame(frameId, true), frameObject.value(ANIME_STR("image")));
        }
        scene.assets.append(asset);
    }

    const JsonArray columns = xsheet.value(ANIME_STR("columns")).toArray();
    scene.xsheet.columns.reserve(columns.size());
    for (const JsonValue &columnValue : columns) {
        const JsonObject columnObject = columnValue.toObject();
        AnimeColumn column;
        column.name = columnObject.value(ANIME_STR("name")).toString();
        column.id = columnObject.value(ANIME_STR("id")).toInt(0);
        // Absent in files from before layer parenting, and in every file where
        // no layer tracks another: default independent. normalizeLayerTree
        // zeroes anything that turns out not to name a live column.
        column.parentLayerId = columnObject.value(ANIME_STR("parentLayerId")).toInt(0);
        column.type = columnTypeFromName(columnObject.value(ANIME_STR("type")).toString());
        column.visible = columnObject.value(ANIME_STR("visible")).toBool(true);
        column.locked = columnObject.value(ANIME_STR("locked")).toBool(false);
        column.opacity = columnObject.value(ANIME_STR("opacity")).toDouble(1.0);

        const JsonArray cells = columnObject.value(ANIME_STR("cells")).toArray();
        for (const JsonValue &cellValue : cells) {
            const JsonObject cellObject = cellValue.toObject();
            AnimeCell cell;
            cell.assetIndex = cellObject.value(ANIME_STR("assetIndex")).toInt(-1);
            cell.frameId = cellObject.value(ANIME_STR("frameId")).toInt(0);
            if (cell.assetIndex >= 0 && cell.assetIndex < scene.assets.size() && cell.frameId > 0) {
                column.setCell(cellObject.value(ANIME_STR("row")).toInt(), cell);
            }
        }
        scene.xsheet.columns.append(column);
    }

    // Layer groups. A file from an older build has no "layerTree" at all and
    // no column ids either; normalizeLayerTree (called below through the
    // model) then hands out fresh ids and puts every layer at the top level,
    // which is exactly the pre-grouping look.
    std::function<Vector<AnimeLayerNode>(const JsonArray &)> treeFromJson =
        [&](const JsonArray &array) -> Vector<AnimeLayerNode> {
        Vector<AnimeLayerNode> nodes;
        for (const JsonValue &value : array) {
            AnimeLayerNode node;
            if (value.isObject()) {
                const JsonObject groupObject = value.toObject();
                node.groupId = groupObject.value(ANIME_STR("groupId")).toInt(0);
                if (node.groupId <= 0) {
                    continue;
                }
                node.name = groupObject.value(ANIME_STR("name")).toString();
                node.collapsed = groupObject.value(ANIME_STR("collapsed")).toBool(false);
                node.tag = groupObject.value(ANIME_STR("tag")).toString();
                node.children = treeFromJson(groupObject.value(ANIME_STR("children")).toArray());
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
    scene.layerTree = treeFromJson(root.value(ANIME_STR("layerTree")).toArray());

    // A file from before the canvas was a document property has neither key;
    // it gets the default page rather than a zero one.
    const Size defaultCanvas = AnimeSceneModel::defaultCanvasSize();
    loaded.setCanvasSize(Size(root.value(ANIME_STR("canvasWidth")).toInt(defaultCanvas.width()),
                               root.value(ANIME_STR("canvasHeight")).toInt(defaultCanvas.height())));

    loaded.setPlaybackFps(root.value(ANIME_STR("playbackFps")).toInt(12));

    loaded.setCurrentFrame(root.value(ANIME_STR("currentFrame")).toInt(0));
    loaded.setCurrentLayer(root.value(ANIME_STR("currentLayer")).toInt(-1));
    loaded.setCurrentAsset(root.value(ANIME_STR("currentAsset")).toInt(-1));
    loaded.normalizeLayerTree();
    *model = loaded;
    return true;
}
}

JsonObject projectToJson(const AnimeSceneModel &mainModel, const AnimeSceneModel &textureModel)
{
    JsonObject root;
    // A distinct format id, not a version bump on the v1 marker: pre-bundle
    // builds validate only the format string, so they must fail cleanly on a
    // bundled project instead of opening it as a blank scene and offering to
    // save that blankness back over it.
    root[ANIME_STR("format")] = ANIME_STR("AnimeAn Project 2");
    root[ANIME_STR("version")] = 2;
    root[ANIME_STR("mainView")] = sceneToJson(mainModel);
    root[ANIME_STR("textureView")] = sceneToJson(textureModel);
    return root;
}

bool projectFromJson(const JsonObject &root,
                     AnimeSceneModel *mainModel,
                     AnimeSceneModel *textureModel,
                     String *error)
{
    if (!mainModel || !textureModel) {
        if (error) {
            *error = ANIME_STR("Project destination is missing.");
        }
        return false;
    }

    if (root.value(ANIME_STR("format")).toString() != ANIME_STR("AnimeAn Project 2")) {
        if (error) {
            *error = ANIME_STR("Unsupported project file.");
        }
        return false;
    }
    if (root.value(ANIME_STR("version")).toInt(0) != 2) {
        if (error) {
            *error = ANIME_STR("Unsupported project version.");
        }
        return false;
    }
    const JsonValue mainView = root.value(ANIME_STR("mainView"));
    const JsonValue textureView = root.value(ANIME_STR("textureView"));
    if (!mainView.isObject() || !textureView.isObject()) {
        if (error) {
            *error = ANIME_STR("The project file is missing view data.");
        }
        return false;
    }
    // Guard each embedded payload exactly like a standalone document: a
    // damaged bundle whose view lost its scene keys must be rejected, not
    // loaded as a blank board that the next silent Ctrl+S writes back.
    const JsonObject mainObject = mainView.toObject();
    const JsonObject textureObject = textureView.toObject();
    if (!sceneHasData(mainObject) || !sceneHasData(textureObject)) {
        if (error) {
            *error = ANIME_STR("The project file does not contain scene data.");
        }
        return false;
    }

    AnimeSceneModel loadedMain;
    AnimeSceneModel loadedTexture;
    String modelError;
    if (!sceneFromJson(mainObject, &loadedMain, &modelError)) {
        if (error) {
            *error = ANIME_STR("Invalid main view: %1").arg(modelError);
        }
        return false;
    }
    if (!sceneFromJson(textureObject, &loadedTexture, &modelError)) {
        if (error) {
            *error = ANIME_STR("Invalid texture view: %1").arg(modelError);
        }
        return false;
    }

    *mainModel = loadedMain;
    *textureModel = loadedTexture;
    return true;
}

JsonObject textureViewToJson(const AnimeSceneModel &model)
{
    JsonObject root = sceneToJson(model);
    root[ANIME_STR("format")] = ANIME_STR("AnimeAn Texture View");
    root[ANIME_STR("version")] = 1;
    return root;
}

bool textureViewFromJson(const JsonObject &root, AnimeSceneModel *model, String *error)
{
    if (!model) {
        if (error) {
            *error = ANIME_STR("Texture view destination is missing.");
        }
        return false;
    }

    const String format = root.value(ANIME_STR("format")).toString();
    if (format == ANIME_STR("AnimeAn Texture View")) {
        if (root.value(ANIME_STR("version")).toInt(1) != 1) {
            if (error) {
                *error = ANIME_STR("Unsupported texture view version.");
            }
            return false;
        }
        if (!sceneHasData(root)) {
            if (error) {
                *error = ANIME_STR("The file does not contain scene data.");
            }
            return false;
        }
        return sceneFromJson(root, model, error);
    }

    if (error) {
        *error = ANIME_STR("Unsupported texture view file.");
    }
    return false;
}
