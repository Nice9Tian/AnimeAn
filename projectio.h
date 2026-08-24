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

QJsonObject projectToJson(const AnimeSceneModel &mainModel, const AnimeSceneModel &textureModel);
bool projectFromJson(const QJsonObject &root,
                     AnimeSceneModel *mainModel,
                     AnimeSceneModel *textureModel,
                     QString *error);

QJsonObject textureViewToJson(const AnimeSceneModel &model);
bool textureViewFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error);

#endif // PROJECTIO_H
