#ifndef PROJECTIO_H
#define PROJECTIO_H

#include "algorithm/animemodel.h"

#include <QJsonObject>
#include <QString>

QString projectFilter();
QJsonObject modelToJson(const AnimeSceneModel &model);
bool modelFromJson(const QJsonObject &root, AnimeSceneModel *model, QString *error);

#endif // PROJECTIO_H
