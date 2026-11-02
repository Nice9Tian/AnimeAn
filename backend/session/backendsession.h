#ifndef BACKENDSESSION_H
#define BACKENDSESSION_H


#include "algorithm/core/color.h"
#include "algorithm/core/path.h"
#include <cstdint>
#include <string>
#include "algorithm/core/image.h"
#include "board/cursor.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/json.h"

#include <utility>
#include "algorithm/core/geometry.h"
#include "algorithm/core/string.h"

#include <memory>

#include "pythonbind/python_bindings.h"
using namespace AnimeCore;

class WsServer;
class AnimeanBusiness;
class PaintOpenGLWidget;
struct BoardFrame;

class BackendSession
{

public:
    explicit BackendSession(WsServer *ws);
    ~BackendSession();

    void initializePythonStartup();
    void setStatusText(const String &text);

    WsServer *wsServer() const { return m_ws; }
    AnimeanBusiness *business() const { return m_business.get(); }
    PaintOpenGLWidget *mainBoard() const { return m_mainBoard.get(); }
    PaintOpenGLWidget *childBoard() const { return m_childBoard.get(); }

private:
    struct CursorState {
        String shape = String("arrow");
        String css = String("default");
        String key;
        int hx = 0;
        int hy = 0;
        int w = 0;
        int h = 0;
        double scale = 1.0;
    };

    void bindPaintViewToBusiness(const String &viewName, PaintOpenGLWidget *paintView);
    void setupBoardSinks(const String &viewName, PaintOpenGLWidget *paintView);
    void setupBusinessConnections();
    void registerPythonUiCallbacks();
    void initWindowInfos();
    void loadTimelineSettings();
    void saveTimelineSettings() const;
    void captureScriptMenuNames();
    void loadProtectedPropertiesForChild();
    void surfaceTextureView();
    struct TextureViewSlot {
        double zoom = 1.0;
        PointF pan;
        bool valid = false;
        bool autoFitSuspended = false;
    };
    TextureViewSlot *textureViewSlotFor(const String &home);
    void saveTextureViewSlot(const String &home);
    void restoreTextureViewSlot();
    void autoFitTextureControlView();

    PaintOpenGLWidget *boardForView(const String &viewName) const;
    String resolveViewName(const String &viewName) const;

    void onClientConnected();
    void onClientDisconnected();
    void onMessageReceived(const JsonObject &msg);

    void handleCallMessage(const JsonObject &msg);
    void handleInputMessage(const JsonObject &msg);
    void handleAckMessage(const JsonObject &msg);

    JsonObject dispatchCall(const String &method, const JsonObject &params);
    JsonObject handleFileImportCall(const JsonObject &params);
    JsonObject handleFileTextureCall(const JsonObject &params);
    bool triggerMenuById(const String &id, const JsonObject &params, String *errorOut);
    void openScriptSettings(const String &name, const String &title);
    void emitUiEvent(const String &name, const JsonObject &args = JsonObject());

    void handleFrameSink(const String &viewName, const BoardFrame &frame);
    void handleImageSink(const String &key, const AnimeCore::Image &rgba);
    void handleCursorSink(const String &viewName, const AnimeCore::Cursor &cursor);
    void sendImagePacket(const String &key, const Image &rgba);

    void setPythonUiFrozen(bool frozen);
    void appendPythonDebug(const String &message);

    void markDirty(const String &key);
    void markAllDirty();
    void scheduleStateFlush();
    void flushDirtyState();
    void sendStateKey(const String &key);
    void sendJson(const JsonObject &obj);
    JsonValue buildStateValue(const String &key) const;

    JsonObject buildWindowState() const;
    JsonObject buildThemeState() const;
    JsonArray buildMenusState() const;
    JsonArray buildTextureMenusState() const;
    JsonArray buildTextureViewButtonsState() const;
    JsonArray buildScriptMenuItems(const String &host,
                                    const String &menuName,
                                    const JsonArray &items) const;
    JsonObject buildToolsState() const;
    JsonObject buildToolOptionsState() const;
    JsonObject buildDrawColorState() const;
    JsonObject buildDrawSettingsState() const;
    JsonObject buildLayersState(const String &viewName) const;
    JsonObject buildAssetsState(const String &viewName) const;
    JsonObject buildTimelineState() const;
    JsonObject buildHistoryState() const;
    JsonObject buildWindowsState() const;
    JsonObject buildCursorState(const String &viewName) const;
    JsonObject buildViewportState(const String &viewName) const;
    JsonObject buildPadState() const;

    WsServer *m_ws = nullptr;
    std::unique_ptr<AnimeanBusiness> m_business;
    std::unique_ptr<PaintOpenGLWidget> m_mainBoard;
    std::unique_ptr<PaintOpenGLWidget> m_childBoard;

    Set<String> m_dirtyKeys;
    bool m_stateFlushScheduled = false;

    int m_pythonFreezeDepth = 0;
    int m_textureAutoFitFrame = -1;

    Vector<AnimeanWindowInfo> m_windowInfos;
    bool m_timelineVisible = true;
    bool m_timelineCollapsed = false;
    bool m_timelineFloating = false;
    String m_timelineDockArea = String("bottom");
    bool m_textureSubControlVisible = false;
    String m_textureHome;
    TextureViewSlot m_textureViewCentral;
    TextureViewSlot m_textureViewControl;

    Vector<std::pair<String, String>> m_mainScriptMenus;
    Vector<std::pair<String, String>> m_childScriptMenus;

    PointF m_forcePadValue{0.0, 0.0};
    Hash<String, PointF> m_padValues;

    JsonObject m_lastToolOptions;
    JsonObject m_lastEvent;
    StringList m_pythonDebugLog;

    Hash<String, CursorState> m_cursors;
    Hash<String, Image> m_cachedImages;
    Hash<String, std::uint64_t> m_lastBoardFrameId;
    std::uint64_t m_thumbnailRevision = 1;
};

#endif // BACKENDSESSION_H
