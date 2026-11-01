#include "animemodel.h"

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
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // deleteLayer collects the column's private asset and renumbers the rest.
    {
        AnimeSceneModel model;
        model.initializeScene(0, 1);
        const int first = model.addLayer();
        const int second = model.addLayer();
        check(model.assetCount() == 2, "two addLayer calls must create two assets");
        const AnimeCore::String keptName = model.assetName(model.assetIndexAt(0, second));

        check(model.deleteLayer(first), "deleting the first layer failed");
        check(model.assetCount() == 1, "the deleted layer's asset was not collected");
        check(model.assetIndexAt(0, 0) == 0, "the surviving cell was not renumbered");
        check(model.assetName(0) == keptName, "the surviving asset changed identity");
        check(model.currentAsset() == model.assetIndexAt(model.currentFrame(), model.currentLayer()),
              "current asset does not match the current cell after the delete");
    }

    // An asset still exposed on another column survives until its last
    // exposure goes.
    {
        AnimeSceneModel model;
        model.initializeScene(0, 1);
        const int first = model.addLayer();
        const int shared = model.assetIndexAt(0, first);
        check(model.addLayerForAsset(0, shared) >= 0, "exposing the asset on a second column failed");

        check(model.deleteLayer(first), "deleting the first exposure failed");
        check(model.assetCount() == 1, "an asset still exposed elsewhere must survive");
        check(model.deleteLayer(0), "deleting the second exposure failed");
        check(model.assetCount() == 0, "the last exposure went - the asset must go with it");
    }

    // A panel asset never exposed on any column is not a candidate.
    {
        AnimeSceneModel model;
        model.initializeScene(0, 1);
        model.addLayer();
        model.addAsset(AnimeColumnType::Vector, ANIME_STR("panel asset"));

        check(model.deleteLayer(0), "deleting the drawn layer failed");
        check(model.assetCount() == 1 && model.assetName(0) == ANIME_STR("panel asset"),
              "an unexposed panel asset must survive an unrelated layer delete");
    }

    // deleteLayerGroup inherits the collection for every member layer.
    {
        AnimeSceneModel model;
        model.initializeScene(0, 1);
        const int first = model.addLayer();
        const int second = model.addLayer();
        model.addLayer(); // an outsider that must keep its asset
        const int groupId = model.createLayerGroup(ANIME_STR("unit"),
                                                   {first, second}, {}, false);
        check(groupId > 0, "creating the layer group failed");

        check(model.deleteLayerGroup(groupId) == 2, "the group delete missed a member");
        check(model.layerCount() == 1, "the outsider layer must survive the group delete");
        check(model.assetCount() == 1, "each member's private asset must be collected");
        check(model.assetIndexAt(0, 0) == 0, "the outsider's cell was not renumbered");
    }

    if (failures == 0) {
        std::printf("animemodel asset GC tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
