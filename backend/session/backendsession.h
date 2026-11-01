#ifndef BACKENDSESSION_H
#define BACKENDSESSION_H

#include "algorithm/core/image.h"
#include "board/cursor.h"
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

#include "pythonbind/python_bindings.h"

class WsServer;
class AnimeanBusiness;
class PaintOpenGLWidget;
struct BoardFrame;

class BackendSession : public QObject
{
    Q_OBJECT
public:
    explicit BackendSession(WsServer *ws, QObject *parent = nullptr);
    ~BackendSession() override;

    void initializePythonStartup();
    void setStatusText(const QString &text);

    WsServer *wsServer() const { return m_ws; }
    AnimeanBusiness *business() const { return m_business.get(); }
    PaintOpenGLWidget *mainBoard() const { return m_mainBoard.get(); }
    PaintOpenGLWidget *childBoard() const { return m_childBoard.get(); }

private:
    struct CursorState {
        QString shape = QStringLiteral("arrow");
        QString css = QStringLiteral("default");
        QString key;
        int hx = 0;
        int hy = 0;
        int w = 0;
        int h = 0;
        double scale = 1.0;
    };

    void bindPaintViewToBusiness(const QString &viewName, PaintOpenGLWidget *paintView);
    void setupBoardSinks(const QString &viewName, PaintOpenGLWidget *paintView);
    void setupBusinessConnections();
    void registerPythonUiCallbacks();
    void initWindowInfos();
    void loadTimelineSettings();
    void saveTimelineSettings() const;
    void captureScriptMenuNames();
    void loadProtectedPropertiesForChild();
    void surfaceTextureView();

    PaintOpenGLWidget *boardForView(const QString &viewName) const;
    QString resolveViewName(const QString &viewName) const;

    void onClientConnected();
    void onClientDisconnected();
    void onMessageReceived(const QJsonObject &msg);

    void handleCallMessage(const QJsonObject &msg);
    void handleInputMessage(const QJsonObject &msg);
    void handleAckMessage(const QJsonObject &msg);

    QJsonObject dispatchCall(const QString &method, const QJsonObject &params);
    QJsonObject handleFileImportCall(const QJsonObject &params);
    QJsonObject handleFileTextureCall(const QJsonObject &params);
    bool triggerMenuById(const QString &id, const QJsonObject &params, QString *errorOut);
    void openScriptSettings(const QString &name, const QString &title);
    void emitUiEvent(const QString &name, const QJsonObject &args = QJsonObject());

    void handleFrameSink(const QString &viewName, const BoardFrame &frame);
    void handleImageSink(const QString &key, const AnimeCore::Image &rgba);
    void handleCursorSink(const QString &viewName, const AnimeCore::Cursor &cursor);
    void sendImagePacket(const QString &key, const QImage &rgba);

    void setPythonUiFrozen(bool frozen);
    void appendPythonDebug(const QString &message);

    void markDirty(const QString &key);
    void markAllDirty();
    void scheduleStateFlush();
    void flushDirtyState();
    void sendStateKey(const QString &key);
    void sendJson(const QJsonObject &obj);
    QJsonValue buildStateValue(const QString &key) const;

    QJsonObject buildWindowState() const;
    QJsonObject buildThemeState() const;
    QJsonArray buildMenusState() const;
    QJsonArray buildScriptMenuItems(const QString &host,
                                    const QString &menuName,
                                    const QJsonArray &items) const;
    QJsonObject buildToolsState() const;
    QJsonObject buildToolOptionsState() const;
    QJsonObject buildDrawColorState() const;
    QJsonObject buildDrawSettingsState() const;
    QJsonObject buildLayersState(const QString &viewName) const;
    QJsonObject buildAssetsState(const QString &viewName) const;
    QJsonObject buildTimelineState() const;
    QJsonObject buildHistoryState() const;
    QJsonObject buildWindowsState() const;
    QJsonObject buildCursorState(const QString &viewName) const;
    QJsonObject buildViewportState(const QString &viewName) const;
    QJsonObject buildPadState() const;

    WsServer *m_ws = nullptr;
    std::unique_ptr<AnimeanBusiness> m_business;
    std::unique_ptr<PaintOpenGLWidget> m_mainBoard;
    std::unique_ptr<PaintOpenGLWidget> m_childBoard;

    QSet<QString> m_dirtyKeys;
    bool m_stateFlushScheduled = false;

    int m_pythonFreezeDepth = 0;
    int m_textureAutoFitFrame = -1;

    QVector<AnimeanWindowInfo> m_windowInfos;
    bool m_timelineVisible = true;
    bool m_timelineCollapsed = false;
    bool m_timelineFloating = false;
    QString m_timelineDockArea = QStringLiteral("bottom");
    bool m_textureSubControlVisible = false;

    QVector<QPair<QString, QString>> m_mainScriptMenus;
    QVector<QPair<QString, QString>> m_childScriptMenus;

    QPointF m_forcePadValue{0.0, 0.0};
    QHash<QString, QPointF> m_padValues;

    QJsonObject m_lastToolOptions;
    QJsonObject m_lastEvent;
    QStringList m_pythonDebugLog;

    QHash<QString, CursorState> m_cursors;
    QHash<QString, QImage> m_cachedImages;
    QHash<QString, quint64> m_lastBoardFrameId;
};

#endif // BACKENDSESSION_H
