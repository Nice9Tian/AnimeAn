#include "projectio.h"

#include <cstdio>

namespace {
int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

void checkModel(const AnimeSceneModel &model,
                const AnimeCore::String &scriptData,
                const AnimeCore::Size &canvasSize,
                int frameCount,
                const AnimeCore::String &firstLayerName)
{
    check(model.scriptData() == scriptData, "script data did not round-trip");
    check(model.canvasSize() == canvasSize, "canvas size did not round-trip");
    check(model.frameCount() == frameCount, "frame count did not round-trip");
    check(model.layerName(0) == firstLayerName, "layer name did not round-trip");
}
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    AnimeSceneModel mainModel;
    mainModel.initializeScene(3, 5);
    mainModel.setTextId(ANIME_STR("main_paint_view"));
    mainModel.setIntId(1);
    mainModel.setCanvasSize(AnimeCore::Size(1920, 1080));
    mainModel.setLayerName(0, ANIME_STR("Main ink"));
    mainModel.setScriptData(ANIME_STR("{\"owner\":\"main\"}"));
    // Two named rows, one of them a duplicate-style name, and one row left
    // alone: the untouched row must still read as its number after a load.
    mainModel.setFrameName(1, ANIME_STR("key"));
    mainModel.setFrameName(2, ANIME_STR("2-a"));

    AnimeSceneModel textureModel;
    textureModel.initializeScene(2, 7);
    textureModel.setTextId(ANIME_STR("child_paint_view"));
    textureModel.setIntId(2);
    textureModel.setCanvasSize(AnimeCore::Size(512, 512));
    textureModel.setLayerName(0, ANIME_STR("Texture ink"));
    textureModel.setScriptData(ANIME_STR("{\"owner\":\"texture\"}"));

    const AnimeCore::JsonObject project = projectToJson(mainModel, textureModel);
    // The bundled format must NOT reuse the v1 marker: pre-bundle builds
    // validate only the format string and would open a bundle as a blank
    // scene instead of rejecting it.
    check(project.value(ANIME_STR("format")).toString() == ANIME_STR("AnimeAn Project 2"),
          "project format marker is wrong");
    check(project.value(ANIME_STR("version")).toInt() == 2,
          "project format version is wrong");
    check(project.value(ANIME_STR("mainView")).isObject(),
          "project is missing the main view");
    check(project.value(ANIME_STR("textureView")).isObject(),
          "project is missing the texture view");

    AnimeSceneModel loadedMain;
    AnimeSceneModel loadedTexture;
    AnimeCore::String error;
    check(projectFromJson(project, &loadedMain, &loadedTexture, &error),
          ANIME_STR("project round-trip failed: %1").arg(error).toUtf8().c_str());
    checkModel(loadedMain,
               ANIME_STR("{\"owner\":\"main\"}"),
               AnimeCore::Size(1920, 1080),
               5,
               ANIME_STR("Main ink"));
    checkModel(loadedTexture,
               ANIME_STR("{\"owner\":\"texture\"}"),
               AnimeCore::Size(512, 512),
               7,
               ANIME_STR("Texture ink"));

    check(loadedMain.frameName(1) == ANIME_STR("key"), "frame name did not round-trip");
    check(loadedMain.frameName(2) == ANIME_STR("2-a"),
          "duplicate-style frame name did not round-trip");
    check(loadedMain.frameName(0) == ANIME_STR("1"),
          "an unnamed row did not fall back to its number");
    // Names are optional: a scene without any must not write the key at all,
    // so files from projects nobody renamed stay as they were.
    check(!project.value(ANIME_STR("textureView")).toObject()
               .value(ANIME_STR("xsheet")).toObject()
               .contains(ANIME_STR("frameNames")),
          "an unnamed scene wrote a frame-name table");

    const AnimeCore::JsonObject textureView = textureViewToJson(textureModel);
    check(textureView.value(ANIME_STR("format")).toString()
              == ANIME_STR("AnimeAn Texture View"),
          "texture view format marker is wrong");
    AnimeSceneModel loadedTextureView;
    error.clear();
    check(textureViewFromJson(textureView, &loadedTextureView, &error),
          ANIME_STR("texture view round-trip failed: %1").arg(error).toUtf8().c_str());
    checkModel(loadedTextureView,
               ANIME_STR("{\"owner\":\"texture\"}"),
               AnimeCore::Size(512, 512),
               7,
               ANIME_STR("Texture ink"));

    error.clear();
    check(!projectFromJson(textureView, &loadedMain, &loadedTexture, &error),
          "a texture view was accepted as a complete project");
    error.clear();
    check(!textureViewFromJson(project, &loadedTextureView, &error),
          "a complete project was accepted as a texture view");

    // Pre-release: the v1 flat .animean format has no reader any more.
    AnimeCore::JsonObject flatV1;
    flatV1[ANIME_STR("format")] = ANIME_STR("AnimeAn Project");
    flatV1[ANIME_STR("version")] = 1;
    flatV1[ANIME_STR("xsheet")] = AnimeCore::JsonObject();
    error.clear();
    check(!projectFromJson(flatV1, &loadedMain, &loadedTexture, &error),
          "a v1 flat file was accepted as a project");
    error.clear();
    check(!textureViewFromJson(flatV1, &loadedTextureView, &error),
          "a v1 flat file was accepted as a texture view");

    // A damaged file that kept its envelope but lost the scene data must be
    // rejected: loading it as a blank scene and reporting success would
    // invite a save that overwrites the original. This holds for a
    // standalone texture view AND for each payload embedded in a bundle.
    AnimeCore::JsonObject damagedTexture;
    damagedTexture[ANIME_STR("format")] = ANIME_STR("AnimeAn Texture View");
    damagedTexture[ANIME_STR("version")] = 1;
    error.clear();
    check(!textureViewFromJson(damagedTexture, &loadedTextureView, &error),
          "a data-less texture envelope was accepted");

    AnimeCore::JsonObject damagedBundle = project;
    damagedBundle[ANIME_STR("textureView")] = AnimeCore::JsonObject();
    error.clear();
    check(!projectFromJson(damagedBundle, &loadedMain, &loadedTexture, &error),
          "a bundle with a data-less view payload was accepted");

    AnimeCore::JsonObject wrongVersion = project;
    wrongVersion[ANIME_STR("version")] = ANIME_STR("2");
    error.clear();
    check(!projectFromJson(wrongVersion, &loadedMain, &loadedTexture, &error),
          "a bundle with a non-numeric version was accepted");

    // Frame operations. Not persistence, but the names they move are: a
    // rekey that drifted would be saved as a row describing another drawing.
    AnimeSceneModel ops;
    ops.initializeScene(2, 3);
    AnimeVectorStroke stroke;
    stroke.id = 1;
    AnimeVectorImageModel *drawing = ops.imageAt(0, 0, true);
    check(drawing != nullptr, "the test scene refused a drawing");
    drawing->addStroke(stroke);
    const AnimeCell source = ops.cellAt(0, 0);

    const int held = ops.insertHoldFrameAfter(0);
    check(held == 1, "a hold did not land directly after the frame it holds");
    check(ops.frameCount() == 4, "a hold did not grow the sheet");
    check(ops.isHoldFrame(1), "the inserted row does not read as a hold");
    check(ops.cellAt(1, 0).assetIndex == source.assetIndex
              && ops.cellAt(1, 0).frameId == source.frameId,
          "a hold did not re-expose the same cell");

    const int copy = ops.duplicateFrame(0);
    check(copy == 2, "a duplicate did not land after the hold run");
    check(!ops.isHoldFrame(2), "a duplicate reads as a hold");
    check(ops.cellAt(2, 0).assetIndex == source.assetIndex,
          "a duplicate left its layer's asset");
    check(ops.cellAt(2, 0).frameId != source.frameId,
          "a duplicate reused the source frame id");
    check(ops.frameName(2) == ANIME_STR("1-a"), "a duplicate was not named after its source");
    const AnimeVectorImageModel *copiedImage = ops.imageAt(2, 0);
    check(copiedImage && copiedImage->strokeCount() == 1, "a duplicate lost the drawing");
    drawing = ops.imageAt(0, 0, true);
    drawing->addStroke(stroke);
    copiedImage = ops.imageAt(2, 0);
    check(copiedImage && copiedImage->strokeCount() == 1,
          "editing the source changed the duplicate");

    // Rows: 0 "start", 1 hold, 2 "1-a", 3.
    ops.setFrameName(0, ANIME_STR("start"));
    ops.deleteFrame(1);
    check(ops.frameName(0) == ANIME_STR("start"), "a delete moved a name above it");
    check(ops.frameName(1) == ANIME_STR("1-a"), "a delete did not pull names up");
    ops.moveFrame(0, 2);
    check(ops.frameName(2) == ANIME_STR("start"), "a move left the name behind");
    check(ops.frameName(0) == ANIME_STR("1-a"), "a move did not shift the rows it passed");
    // A second duplicate of the same base has to find the next free suffix.
    ops.setFrameName(1, ANIME_STR("7"));
    check(ops.nextDuplicateName(0) == ANIME_STR("1-b"),
          "a duplicate chain reused a taken suffix");

    // Layer parenting (AnimeColumn::parentLayerId). A tracked fill layer has
    // to come back tracking the SAME column after a round-trip, which is the
    // whole argument for keying the link by stable id.
    AnimeSceneModel parented;
    parented.initializeScene(2, 2);
    parented.setTextId(ANIME_STR("main_paint_view"));
    parented.setLayerName(0, ANIME_STR("Ink"));
    parented.setLayerName(1, ANIME_STR("Ink fill"));
    const int inkId = parented.layerIdAt(0);
    check(inkId > 0, "the line column never got an id");
    parented.setLayerParentId(1, inkId);
    check(parented.layerParentId(1) == inkId, "the parent link did not stick");
    check(parented.childLayerIndices(0) == AnimeCore::Vector<int>{1},
          "childLayerIndices did not find the tracked layer");
    check(parented.layerParentId(0) == 0, "an untouched column reads as parented");

    AnimeSceneModel loadedParented;
    AnimeSceneModel unusedTexture;
    AnimeCore::JsonObject parentedProject = projectToJson(parented, textureModel);
    error.clear();
    check(projectFromJson(parentedProject, &loadedParented, &unusedTexture, &error),
          ANIME_STR("parented round-trip failed: %1").arg(error).toUtf8().c_str());
    check(loadedParented.layerParentId(1) == loadedParented.layerIdAt(0),
          "a tracked layer did not come back tracking its parent");
    check(loadedParented.childLayerIndices(0) == AnimeCore::Vector<int>{1},
          "a tracked layer is not listed as a child after a load");

    // Optional field: a project where nobody parented anything must not grow
    // the key, so files from earlier builds stay byte-identical.
    check(!parentedProject.value(ANIME_STR("textureView")).toObject()
               .value(ANIME_STR("xsheet")).toObject()
               .value(ANIME_STR("columns")).toArray().at(0).toObject()
               .contains(ANIME_STR("parentLayerId")),
          "an unparented column wrote a parent link");

    // A link to a column that is NOT saved (an internal, script-owned working
    // layer) is dropped on the way out rather than written dangling - the
    // same filter treeToJson applies to the layer tree.
    AnimeSceneModel internalParent;
    internalParent.initializeScene(2, 2);
    internalParent.setTextId(ANIME_STR("main_paint_view"));
    internalParent.setLayerInternal(0, true);
    const int hiddenId = internalParent.layerIdAt(0);
    internalParent.setLayerParentId(1, hiddenId);
    check(internalParent.layerParentId(1) == hiddenId,
          "the model refused a link to an internal column");
    const AnimeCore::JsonArray savedColumns =
        projectToJson(internalParent, textureModel)
            .value(ANIME_STR("mainView")).toObject()
            .value(ANIME_STR("xsheet")).toObject()
            .value(ANIME_STR("columns")).toArray();
    check(savedColumns.size() == 1, "the internal column was written to the file");
    check(!savedColumns.at(0).toObject().contains(ANIME_STR("parentLayerId")),
          "a link to an unsaved column was written dangling");

    // And a link that survives into a file anyway (hand-edited, or a column
    // deleted since) is zeroed by normalizeLayerTree rather than believed.
    AnimeCore::JsonObject dangling = parentedProject;
    AnimeCore::JsonObject danglingMain = dangling.value(ANIME_STR("mainView")).toObject();
    AnimeCore::JsonObject danglingSheet = danglingMain.value(ANIME_STR("xsheet")).toObject();
    AnimeCore::JsonArray danglingColumns = danglingSheet.value(ANIME_STR("columns")).toArray();
    AnimeCore::JsonObject danglingColumn = danglingColumns.at(1).toObject();
    danglingColumn[ANIME_STR("parentLayerId")] = 99999;
    danglingColumns.replace(1, danglingColumn);
    danglingSheet[ANIME_STR("columns")] = danglingColumns;
    danglingMain[ANIME_STR("xsheet")] = danglingSheet;
    dangling[ANIME_STR("mainView")] = danglingMain;
    AnimeSceneModel loadedDangling;
    error.clear();
    check(projectFromJson(dangling, &loadedDangling, &unusedTexture, &error),
          "a file with a dangling parent link was rejected outright");
    check(loadedDangling.layerParentId(1) == 0,
          "a dangling parent link survived the load");

    // Self-parenting is refused at the setter, so it can never reach a file.
    loadedDangling.setLayerParentId(1, loadedDangling.layerIdAt(1));
    check(loadedDangling.layerParentId(1) == 0, "a column was allowed to parent itself");

    check(ensureProjectFileExtension(ANIME_STR("drawing")) == ANIME_STR("drawing.anproj"),
          "project extension was not appended");
    check(ensureProjectFileExtension(ANIME_STR("cloth.textureview"))
              == ANIME_STR("cloth.anproj"),
          "cross-type extension was not replaced");
    check(ensureTextureViewFileExtension(ANIME_STR("cloth.TEXTUREVIEW"))
              == ANIME_STR("cloth.TEXTUREVIEW"),
          "existing texture extension was changed");

    if (failures == 0) {
        std::printf("projectio round-trip tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
