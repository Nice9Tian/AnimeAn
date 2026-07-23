#ifndef PYTHON_BINDINGS_H
#define PYTHON_BINDINGS_H

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class AnimeSceneModel;

struct AnimeanOverlayItem {
    QString id;
    QVector<QPointF> points;
    bool closed = false;
    QColor strokeColor = QColor(0, 0, 0, 255);
    QColor fillColor = QColor(0, 0, 0, 0);
    qreal width = 3.0;
    bool removable = true;
};

void registerAnimeanUiScene(AnimeSceneModel *model);
void unregisterAnimeanUiScene(AnimeSceneModel *model);
void registerAnimeanUiRefreshCallback(std::function<void(bool frame, bool layer, bool asset, bool widget)> callback);
void clearAnimeanUiRefreshCallback();
void registerAnimeanUiFreezeCallback(std::function<void(bool frozen)> callback);
void clearAnimeanUiFreezeCallback();
void registerAnimeanUiOverlayCallback(std::function<void(const QString &view, const QVector<AnimeanOverlayItem> &items)> callback);
void clearAnimeanUiOverlayCallback();
void registerAnimeanUiDrawColorCallback(std::function<void(const QColor &color)> callback);
void clearAnimeanUiDrawColorCallback();

#endif // PYTHON_BINDINGS_H
