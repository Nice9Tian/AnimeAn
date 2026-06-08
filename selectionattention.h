#ifndef SELECTIONATTENTION_H
#define SELECTIONATTENTION_H

#include "algorithm/animemodel.h"

struct AttentionUpdate {
    bool frame = false;
    bool layer = false;
    bool asset = false;
};

enum class AttentionChange {
    FrameChange,
    LayerChange,
    AssetChange
};

struct SelectionAttention {
    int frame = 0;
    int layer = -1;
    int asset = -1;
};

AttentionUpdate constrainAttention(const AnimeSceneModel &model, SelectionAttention *attention, AttentionChange change);
int topLayerForFrame(const AnimeSceneModel &model, int frame);
int firstLayerForAsset(const AnimeSceneModel &model, int frame, int asset);

#endif // SELECTIONATTENTION_H
