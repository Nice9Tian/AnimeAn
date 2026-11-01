#ifndef PROJECTIO_H
#define PROJECTIO_H

#include "algorithm/animemodel.h"

#include "algorithm/core/json.h"
#include "algorithm/core/string.h"


AnimeCore::String projectOpenFilter();
AnimeCore::String projectSaveFilter();
AnimeCore::String textureViewOpenFilter();
AnimeCore::String textureViewSaveFilter();
AnimeCore::String ensureProjectFileExtension(const AnimeCore::String &fileName);
AnimeCore::String ensureTextureViewFileExtension(const AnimeCore::String &fileName);

AnimeCore::JsonObject projectToJson(const AnimeSceneModel &mainModel, const AnimeSceneModel &textureModel);
bool projectFromJson(const AnimeCore::JsonObject &root,
                     AnimeSceneModel *mainModel,
                     AnimeSceneModel *textureModel,
                     AnimeCore::String *error);

AnimeCore::JsonObject textureViewToJson(const AnimeSceneModel &model);
bool textureViewFromJson(const AnimeCore::JsonObject &root, AnimeSceneModel *model, AnimeCore::String *error);

#endif // PROJECTIO_H
