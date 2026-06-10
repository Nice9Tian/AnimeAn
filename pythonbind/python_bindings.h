#ifndef PYTHON_BINDINGS_H
#define PYTHON_BINDINGS_H

#include <functional>

class AnimeSceneModel;

void registerAnimeanUiScene(AnimeSceneModel *model);
void unregisterAnimeanUiScene(AnimeSceneModel *model);
void registerAnimeanUiRefreshCallback(std::function<void(bool frame, bool layer, bool asset, bool widget)> callback);
void clearAnimeanUiRefreshCallback();
void registerAnimeanUiFreezeCallback(std::function<void(bool frozen)> callback);
void clearAnimeanUiFreezeCallback();

#endif // PYTHON_BINDINGS_H
