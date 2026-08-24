#ifndef PROJECTIO_H
#define PROJECTIO_H

#include "algorithm/animemodel.h"

#include <QJsonObject>
#include <QString>

QString projectOpenFilter();
QString projectSaveFilter();
QString textureViewOpenFilter();
QString textureViewSaveFilter();
QString ensureProjectFileExtension(const QString &fileName);
QString ensureTextureViewFileExtension(const QString &fileName);

QJsonObject modelToJson(const AnimeSceneModel &model);
bool modelFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error);

QJsonObject projectToJson(const AnimeSceneModel &mainModel, const AnimeSceneModel &textureModel);

// Loads a project document. A version-1 (.animean) file describes only the
// main view; in that case *textureModel is left untouched and *textureLoaded
// (when provided) is set to false, so callers know not to replace the board.
bool projectFromJson(const QJsonObject &root,
                     AnimeSceneModel *mainModel,
                     AnimeSceneModel *textureModel,
                     QString *error,
                     bool *textureLoaded = nullptr);

QJsonObject textureViewToJson(const AnimeSceneModel &model);
bool textureViewFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error);

#endif // PROJECTIO_H
