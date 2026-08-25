#include "selectionattention.h"

AttentionUpdate constrainAttention(const AnimeSceneModel &model, SelectionAttention *attention, AttentionChange change)
{
    AttentionUpdate update;
    if (!attention) {
        return update;
    }

    update.frame = change == AttentionChange::FrameChange;
    update.layer = true;
    update.asset = true;

    if (model.frameCount() <= 0) {
        attention->frame = -1;
    } else if (attention->frame < 0) {
        // A document WITH frames always has the playhead on one of them. The
        // asset branch used to keep -1 here so an unexposed asset could park
        // the board "nowhere"; now that nothing produces that state, an
        // incoming -1 is a stale value, not an intent.
        attention->frame = 0;
    } else if (attention->frame >= model.frameCount()) {
        attention->frame = model.frameCount() - 1;
    }

    if (attention->asset < 0 || attention->asset >= model.assetCount()) {
        attention->asset = -1;
    }
    if (attention->layer < 0 || attention->layer >= model.layerCount()) {
        attention->layer = -1;
    }

    const int layerAsset = model.assetIndexAt(attention->frame, attention->layer);

    if (change == AttentionChange::FrameChange) {
        attention->layer = topLayerForFrame(model, attention->frame);
        attention->asset = attention->layer >= 0
                                ? model.assetIndexAt(attention->frame, attention->layer)
                                : -1;
        return update;
    }

    if (change == AttentionChange::AssetChange) {
        attention->layer = attention->asset >= 0
                                ? firstLayerForAsset(model, attention->frame, attention->asset)
                                : -1;
        // An asset that is exposed on no layer HERE clears the layer pairing
        // and nothing else. It must NOT clear the frame: the frame is where
        // the user is in time, and -1 is not "no exposure", it is "no
        // position" - AnimeSceneModel::setCurrentFrame(-1) wipes the model's
        // frame outright (animemodel.cpp:800-807), which blanks the canvas,
        // empties the layers panel, and makes every later "create on the
        // current frame" collapse to row 0 (addLayer's own m_currentFrame < 0
        // guard, animemodel.cpp:1665-1669). That is how an Auto Mapping run
        // started on frame N put its output on frame 1, and it stuck: the run
        // saves and restores the selection, so the -1 outlived it and every
        // later run landed on frame 1 too. Unexposed assets are the NORM here
        // - every mapping run leaves its private asset behind when its layer
        // is retired - so this fired on an ordinary Assets-panel click.
        return update;
    }

    if (change == AttentionChange::LayerChange) {
        if (layerAsset >= 0) {
            attention->asset = layerAsset;
        } else {
            attention->layer = -1;
            attention->asset = -1;
        }
    }
    return update;
}

int topLayerForFrame(const AnimeSceneModel &model, int frame)
{
    for (int i = 0; i < model.layerCount(); ++i) {
        if (model.layerInternal(i)) {
            continue; // script-owned working layers are not selectable
        }
        if (model.assetIndexAt(frame, i) >= 0) {
            return i;
        }
    }
    return -1;
}

int firstLayerForAsset(const AnimeSceneModel &model, int frame, int asset)
{
    if (asset < 0) {
        return -1;
    }
    for (int i = 0; i < model.layerCount(); ++i) {
        if (model.layerInternal(i)) {
            continue;
        }
        if (model.assetIndexAt(frame, i) == asset) {
            return i;
        }
    }
    return -1;
}
