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
    int penStyle = 1;
    bool removable = true;
};

// A draggable edit handle, drawn at constant SCREEN size above everything.
// Pure mechanism: C++ renders it, hit-tests it and reports presses/drags as
// "handle" hook events; what a handle means - which control point of which
// stroke, or a synthesized key point - lives entirely in Python.
struct AnimeanEditHandle {
    QString id;
    QPointF pos;                              // document coordinates
    int shape = 0;                            // 0 square, 1 circle, 2 diamond,
                                              // 3 accept button, 4 delete button
    QColor color = QColor(255, 255, 255, 255);
    // False for display-only markers (e.g. a snap HINT drawn under the
    // cursor): rendered normally, but invisible to the hit test - a hint
    // that swallowed the very click it advertised armed nothing at all.
    bool interactive = true;
};

void registerAnimeanUiScene(AnimeSceneModel *model);
void unregisterAnimeanUiScene(AnimeSceneModel *model);
void registerAnimeanUiRefreshCallback(std::function<void(bool frame, bool layer, bool asset, bool widget)> callback);
void clearAnimeanUiRefreshCallback();
void registerAnimeanUiToolOptionsCallback(std::function<void()> callback);
void clearAnimeanUiToolOptionsCallback();
void registerAnimeanUiFreezeCallback(std::function<void(bool frozen)> callback);
void clearAnimeanUiFreezeCallback();
void registerAnimeanUiOverlayCallback(std::function<void(const QString &view, const QVector<AnimeanOverlayItem> &items)> callback);
void clearAnimeanUiOverlayCallback();
void registerAnimeanUiEditHandleCallback(std::function<void(const QString &view, const QVector<AnimeanEditHandle> &handles)> callback);
void clearAnimeanUiEditHandleCallback();
void registerAnimeanUiDrawColorCallback(std::function<void(const QColor &color)> callback);
// Stabilizer / simplify / corner, the three drawing parameters (0-100 each).
void registerAnimeanUiDrawSettingsCallback(std::function<void(int, int, int)> callback);
void clearAnimeanUiDrawColorCallback();
void registerAnimeanUiHistoryCallback(std::function<void(const QString &op, const QString &view, const QString &label)> callback);
void clearAnimeanUiHistoryCallback();
void registerAnimeanUiPadValueCallback(std::function<void(const QString &pad, double x, double y)> callback);
void clearAnimeanUiPadValueCallback();

// True when at least one Python hook subscribes to the event (python_hooks
// pushes the subscription set via ui.set_hook_events). Lets dispatch sites
// skip the GIL/message work entirely for events nobody listens to. Until the
// first push arrives, every event counts as subscribed.
bool animeanHookEventSubscribed(const QString &event);

#endif // PYTHON_BINDINGS_H
