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

    // A displacement field from ui.set_field, kept until it is sent and resent
    // to a new client.
    struct FieldEntry {
        int w = 0;
        int h = 0;
        std::uint64_t version = 0;
        std::string data;
        bool pending = false;
    };

    void bindPaintViewToBusiness(const String &viewName, PaintOpenGLWidget *paintView);
    void setupBoardSinks(const String &viewName, PaintOpenGLWidget *paintView);
    void setupBusinessConnections();
    void registerPythonUiCallbacks();
    void initWindowInfos();
    void loadTimelineSettings();
    void saveTimelineSettings() const;
    void loadProtectedPropertiesForChild();
    void surfaceTextureView();
    void surfaceMainView();
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
    // replay: kept as the `event` state a page that connects later is sent
    // first. A transient one (board.effect) passes false.
    void emitUiEvent(const String &name, const JsonObject &args = JsonObject(), bool replay = true);

    // Hands one hook event to python_hooks.dispatch. Returns 1 when a hook set
    // message["handled"], 0 when none did, -1 when one of them raised (the text
    // in *errorOut).
    int dispatchPythonEvent(const JsonObject &event, String *errorOut = nullptr);

    // A menu item Python declared with its own id ("file.open", "windows.timeline"):
    // which bar and menu declared it, and the name its "menu" event carries.
    struct MenuRoute {
        String host;
        String menu;
        String name;
    };
    bool findMenuRoute(const String &id, MenuRoute *out) const;
    bool forwardMenuToPython(const MenuRoute &route, const JsonObject &params, String *errorOut);
    // The open dialogs are Python's (dialogs.py); these keep the broadcast copy.
    void setDialogsFromPython(const String &json);
    void dropDialog(const String &id);

    void handleFrameSink(const String &viewName, const BoardFrame &frame);
    void handleImageSink(const String &view, const String &key, const AnimeCore::Image &rgba);
    void handleCursorSink(const String &viewName, const AnimeCore::Cursor &cursor);
    void sendImagePacket(const String &key, const Image &rgba, const String &view = String());
    void sendFieldPacket(const String &key, const FieldEntry &entry);
    void flushPendingFieldsForBoard(const PaintOpenGLWidget *board);

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
    JsonObject buildTextureSettingsState() const;
    JsonArray buildTextureViewButtonsState() const;
    // One bar being built: the menus Python declares (python_hooks.menus_json,
    // fetched once per host, in Python's order) and the routes the bar's items
    // with their own ids leave behind.
    struct MenuBuild {
        Hash<String, JsonArray> menus;
        Hash<String, MenuRoute> routes;
    };
    JsonArray buildMenuBar(const String &host) const;
    JsonArray pythonMenus(const String &host, MenuBuild *build) const;
    JsonObject pythonMenu(const String &host, const String &name, MenuBuild *build) const;
    JsonArray buildScriptMenuItems(const String &host,
                                    const String &menuName,
                                    const JsonArray &items,
                                    MenuBuild *build,
                                    int depth) const;
    JsonObject buildToolsState() const;
    JsonObject buildToolOptionsState() const;
    JsonObject buildDrawColorState() const;
    JsonObject buildDrawSettingsState() const;
    JsonObject buildLayersState(const String &viewName) const;
    JsonObject buildAssetsState(const String &viewName) const;
    JsonObject buildTimelineState() const;
    JsonObject buildHistoryState() const;
    JsonObject buildWindowsState() const;
    Vector<AnimeanWindowInfo> windowInfos() const;
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

    // Learnt while the bars are built, per bar and replaced when that bar is
    // built again: a trigger of one of these ids goes back to the Python menu
    // that declared it (the main bar's first when both bars have the id).
    mutable Hash<String, Hash<String, MenuRoute>> m_menuRoutes;

    PointF m_forcePadValue{0.0, 0.0};
    Hash<String, PointF> m_padValues;

    JsonObject m_lastEvent;
    StringList m_pythonDebugLog;

    // The "dialogs" state. A dialog dropped because its window closed or failed
    // stays dropped: its id is remembered, so a stale stack cannot bring it back.
    JsonArray m_dialogs;
    Set<String> m_droppedDialogIds;
    // States Python broadcasts for the page (ui.broadcast), keyed "py/<name>".
    Hash<String, JsonValue> m_pythonStates;

    Hash<String, CursorState> m_cursors;
    Hash<String, Image> m_cachedImages;
    Hash<String, std::uint64_t> m_lastBoardFrameId;
    std::uint64_t m_thumbnailRevision = 1;
    std::uint64_t m_nextFieldVersion = 1;
    Hash<String, FieldEntry> m_fields;
    Hash<String, bool> m_fieldDisplaySupported;
};

#endif // BACKENDSESSION_H
