#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

#include "selectionattention.h"

class AssetPanel;
class CentralPaintArea;
class ForcePadPanel;
class HistoryPanel;
class QAction;
class QDockWidget;
class LayerPanel;
class ParentWindow;
class SubControlFrame;
class TexturePanel;
class TimelineWindow;
class ToolsPanel;
class PaintOpenGLWidget;
class QLineEdit;
class QPlainTextEdit;
class QTimer;
class QMenu;
class QTreeWidgetItem;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setStatusText(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupDocks();
    void setupListDragDrop();
    void setupConnections();
    void setupPythonDebugDock();
    void appendPythonDebugMessage(const QString &message);
    void syncEmbeddedPythonState();
    void runPythonInitializationScript();
    void createToolDocks();
    // ui.set_locked_tools(view, [...]) landing here: remember the set, dim the
    // matching painting chips and switch to Arrow when the armed tool has just
    // been locked out from under the user.
    void applyLockedTools(const QString &view, const QStringList &tools);
    // Re-reads the ACTIVE board's lock set onto the shared rail. Also run when
    // the active board changes: which set governs is a property of the board
    // the next gesture lands on, not of the set that arrived last.
    void refreshToolLockState();
    // The tool travels as its underlying int - PaintOpenGLWidget is only
    // forward-declared in this header, like m_reloadToolOptions below.
    bool isToolLocked(int tool) const;
    void createListDocks();
    // The texture board's panel plus the sub-control frame that can carry it.
    // Not a dock any more: the panel has three possible homes and the frame is
    // the one that travels.
    void createTexturePanel();
    // Puts the texture panel wherever the single-ownership rule says it
    // belongs right now: the central Texture page wins, then a live
    // sub-control frame, then parked. Cheap to call - it does nothing when the
    // answer has not changed, which matters because moving the panel
    // re-creates the board's GL context.
    void updateTextureHome();
    // One saved viewpoint per texture home. There is only ever ONE texture
    // board, and it reparents between the central page and the sub-control
    // frame - so the view transform cannot simply ride along with it: a board
    // the size of a panel row and a board the size of the central area want
    // different zooms of the same drawing. Saved on the way out of a home,
    // restored on the way into one.
    struct TextureViewSlot {
        qreal zoom = 1.0;
        QPointF pan;
        // Nothing has been saved for this home yet, so there is nothing to
        // restore - the control's empty slot is what makes its first visit an
        // auto-fit rather than an inherited viewpoint.
        bool valid = false;
        // The user moved the view by hand while the board was here. Only the
        // control home reads it, and only until the next home-in.
        bool autoFitSuspended = false;
    };
    // Null for "parked": nobody is looking at the board there, so there is no
    // viewpoint worth remembering.
    TextureViewSlot *textureViewSlotFor(QWidget *home);
    void saveTextureViewSlot(QWidget *home);
    // Restores the incoming home's viewpoint, and re-fits when that home is the
    // sub-control. Runs QUEUED from updateTextureHome: the board has only just
    // been reparented, and both the restore's pan clamp and the fit are
    // measured against a size the layout has not handed out yet.
    void restoreTextureViewSlot();
    // The bleed fit: the current frame's visible artwork COVERS the sub-control
    // frame. A no-op unless the board is homed there, and unless auto-fit is
    // still in charge of that home.
    void autoFitTextureControlView();
    void populateChildViewButtons();
    // Script-defined menu-bar menus: Python describes them, C++ renders them
    // and reports the choice back. Rebuilt on every open so check marks stay
    // in step with the script's own state.
    void createScriptMenus();
    // Declared unconditionally because it is DEFINED and CALLED
    // unconditionally (its body is what the #ifdef guards). Declaring it only
    // under ANIMEAN_WITH_PYTHON broke the Python-less build.
    void attachChildScriptMenus();
#ifdef ANIMEAN_WITH_PYTHON
    // `host` selects which menu bar the definitions come from ("main" /
    // "child"); `owner` is the view the menu acts on, so a per-view toggle
    // reports the right board rather than whichever one has focus.
    void rebuildScriptMenu(QMenu *menu, const QString &menuName,
                           const QString &host, PaintOpenGLWidget *owner);
    void fillScriptMenu(QMenu *menu, const QString &menuName, const QJsonArray &items,
                        const QString &host, PaintOpenGLWidget *owner);
#endif
    void openScriptSettings(const QString &name, const QString &title);
    // Rebuild the tool options panel for the current tool (the Draw Setting
    // window and the panel's Smooth slider show the same stabilizer value).
    void refreshToolOptions();
    // Everything the timeline draws itself from, gathered against the view it
    // currently follows. The single replacement for refreshFrameList and
    // refreshFpsCombo: frame data, rate and transport state move together.
    void refreshTimeline();
    void createTimeline();
    // Which stroke properties the onion pass drops, asked once at startup.
    void pullOnionGuideProperties();
    static QVector<QTreeWidgetItem *> layerPanelItems(LayerPanel *panel);
    // Writes the panel's current shape back into the model: the layer group
    // tree follows what the user dragged, and the dragged layer's z-order
    // follows the leaf it landed after.
    void applyLayerPanelStructure(LayerPanel *panel, PaintOpenGLWidget *view, int movedColumnId);
    void showLayerContextMenu(LayerPanel *panel, PaintOpenGLWidget *view, const QPoint &pos);
    void runPythonDebugCommand(const QString &command);
    QString runEmbeddedPythonCommand(const QString &command);
    QString resolvePythonScriptPath(const QString &scriptName) const;
    void newProject();
    void applyNewCanvasSize(const QSize &size);
    void promptForNewCanvasOnStartup();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString &fileName);
    bool loadProjectFrom(const QString &fileName);
    void importRaster(PaintOpenGLWidget *view);
    void importOpenToonzLines(PaintOpenGLWidget *view);
    void importClipStudioPaint(PaintOpenGLWidget *view);
    void createMainPaintView();
    void showMainPaintView();
    void startPlayback();
    void stopPlayback();
    void advancePlaybackFrame();
    // The texture document's File menu on the TEXTURE panel's own bar.
    void createTextureFileMenu();
    // The same entries again, as a "Texture" submenu of the application File
    // menu: the panel's bar is only reachable while the panel is on screen.
    void createMainTextureMenu();
    // Builds the entries into whichever menu is given, with its own QActions.
    // The two menus share the handlers, never the actions - a QAction has one
    // owner, and the second menu's copy would die with the first.
    void fillTextureFileMenu(QMenu *menu);
    void createHistoryDock();
    void createForcePadDock();
    void refreshHistoryList();
    void scheduleHistoryRefresh();
    PaintOpenGLWidget *undoTargetView() const;
    PaintOpenGLWidget *redoTargetView() const;
    void applyHistoryRestore(PaintOpenGLWidget *view);
    // Bring the texture board where the user can see it and make it active.
    // Surfaces whichever home currently owns it: a live sub-control frame is
    // raised, otherwise the central Texture page is selected.
    void showTextureView();
    // The Windows-menu entry: show and raise the sub-control itself, floating
    // it when it has never been embedded anywhere.
    void showTextureSubControl();
    // What mapping the texture board had to disturb, so a read-only caller can
    // put it back. Empty page means nothing was moved.
    struct TextureMappingRestore {
        QString centralPage;
        PaintOpenGLWidget *activeView = nullptr;
    };
    // Makes the texture board renderable for a framebuffer grab without
    // changing anything the user chose, when it already is. When it cannot, it
    // reports what it moved through `restore`.
    void ensureTextureBoardMapped(TextureMappingRestore *restore = nullptr);
    // Undoes exactly what ensureTextureBoardMapped reported, and nothing else.
    void restoreTextureBoardMapping(const TextureMappingRestore &restore);
    void openTextureView();
    bool saveTextureView();
    bool saveTextureViewAs();
    bool exportTextureImage();
    bool writeJsonToFile(const QJsonObject &object, const QString &fileName, const QString &dialogTitle);
    bool readJsonFromFile(const QString &fileName, const QString &dialogTitle, QJsonObject *object);
    // Asks before writing when the owned-extension rewrite moved the save
    // target onto an existing file the dialog never confirmed.
    bool confirmDivergentOverwrite(const QString &requestedName,
                                   const QString &targetName,
                                   const QString &dialogTitle);
    // Loading a scene into a board is two-phase so multi-board loads can
    // install every model BEFORE any activation fires python hooks that read
    // the other board: install assigns the model and the view identity;
    // activate runs modelReplaced, history reset, attention refresh, repaint.
    void installLoadedModel(PaintOpenGLWidget *view, const AnimeSceneModel &model);
    void activateLoadedModel(PaintOpenGLWidget *view, const QString &historyLabel);
    // install + activate, for single-board loads.
    void adoptLoadedModel(PaintOpenGLWidget *view,
                          const AnimeSceneModel &model,
                          const QString &historyLabel);
    // Re-asks toolcontrol.py for the active extra tool's layout and applies
    // it. A no-op when no extra tool is showing.
    void refreshExtraToolOptions();
    void updateWindowTitle();
    PaintOpenGLWidget *activePaintWidget() const;
    PaintOpenGLWidget *framePanelTarget() const;
    PaintOpenGLWidget *layerPanelTarget() const;
    PaintOpenGLWidget *assetPanelTarget() const;
    SelectionAttention &attentionFor(PaintOpenGLWidget *view);
    void setActivePaintView(PaintOpenGLWidget *view);
    void refreshPanelTargets();
    void requestAttentionUpdate(PaintOpenGLWidget *view, AttentionChange change, int frame, int layer, int asset);
    void updateAttention(PaintOpenGLWidget *view, AttentionChange change, int frame, int layer, int asset);
    // Each layers page is bound to ONE board for its whole life, so a refresh
    // names the pair rather than resolving a target: the child page keeps
    // showing the child board even while the main board has focus.
    LayerPanel *layerPanelForView(PaintOpenGLWidget *view) const;
    PaintOpenGLWidget *viewForLayerPanel(LayerPanel *panel) const;
    void connectLayerPanel(LayerPanel *panel, PaintOpenGLWidget *view);
    void refreshLayerList(LayerPanel *panel, PaintOpenGLWidget *view, int selectedRow);
    // Both pages, each against its own board's current selection.
    void refreshLayerLists();
    void refreshAssetList(int selectedRow);
    void setPythonUiFrozen(bool frozen);
    ParentWindow *parentWindowNamed(const QString &name) const;

    Ui::MainWindow *ui;
    PaintOpenGLWidget *m_paintWidget = nullptr;
    PaintOpenGLWidget *m_childPaintWidget = nullptr;
    PaintOpenGLWidget *m_activePaintWidget = nullptr;
    CentralPaintArea *m_centralArea = nullptr;
    TexturePanel *m_texturePanel = nullptr;
    // The travelling frame the texture panel rides in when it is not on the
    // central Texture page. Owned by the sub-control registry, not by whatever
    // panel it happens to be embedded in.
    SubControlFrame *m_textureFrame = nullptr;
    // Where the panel currently is, so a re-evaluation that lands on the same
    // answer costs nothing. The GL context is re-created on every reparent.
    QWidget *m_textureHome = nullptr;
    bool m_updatingTextureHome = false;
    TextureViewSlot m_textureViewCentral;
    TextureViewSlot m_textureViewControl;
    // How many times the queued restore has found the board still unlaid-out.
    // Bounded: a home that never gives the board a size would otherwise keep a
    // timer alive for the life of the session.
    int m_textureViewRestoreRetries = 0;
    // The child board's frame the last successful sub-control fit was computed
    // for. Only the scripted refresh path reads it: ui.refresh() carries a
    // frame FLAG, not a frame change, and a script raises it dozens of times
    // per run - without this the board would re-fit on every one of them.
    int m_textureAutoFitFrame = -1;
    QVector<PaintOpenGLWidget *> m_paintViews;
    LayerPanel *m_mainLayerPanel = nullptr;
    LayerPanel *m_childLayerPanel = nullptr;
    TimelineWindow *m_timeline = nullptr;
    AssetPanel *m_assetPanel = nullptr;
    ParentWindow *m_layerDock = nullptr;
    ParentWindow *m_assetDock = nullptr;
    ParentWindow *m_toolsDock = nullptr;
    ParentWindow *m_toolOptDock = nullptr;
    ParentWindow *m_pythonDebugDock = nullptr;
    ParentWindow *m_historyDock = nullptr;
    HistoryPanel *m_historyPanel = nullptr;
    ParentWindow *m_forcePadDock = nullptr;
    ForcePadPanel *m_forcePadPanel = nullptr;
    // Every ParentWindow, in Windows-menu order; also what ui.windows lists
    // and how a name from Python is resolved.
    QVector<ParentWindow *> m_parentWindows;
    // One instance per Tools page. The built-in buttons live on the painting
    // page only; selection exclusivity spans all three.
    QVector<ToolsPanel *> m_toolsPanels;
    ToolsPanel *m_paintingToolsPanel = nullptr;
    ToolsPanel *m_mappingToolsPanel = nullptr;
    ToolsPanel *m_fukusatoToolsPanel = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QTimer *m_playbackTimer = nullptr;
    PaintOpenGLWidget *m_playbackView = nullptr;
    int m_playbackIndex = 0;
    int m_playbackFrameCount = 0;
    // Loop is a SESSION preference, not a document one: it says how you want
    // to watch, not what the animation is.
    bool m_playbackLoop = true;
    // Onion state belongs to the session too, and only to the main board -
    // the child board is a texture reference, not a run of drawings.
    bool m_onionEnabled = false;
    bool m_onionGuideLines = false;
    QSet<int> m_onionFrames;
    QPlainTextEdit *m_pythonDebugOutput = nullptr;
    QLineEdit *m_pythonDebugCommand = nullptr;
    // The project path stores both views; the texture path is only for the
    // independently reusable .textureview file managed by its own menu.
    QString m_currentFilePath;
    QString m_childFilePath;
    QHash<PaintOpenGLWidget *, SelectionAttention> m_attentionByView;
    SelectionAttention m_pendingAttention;
    AttentionChange m_pendingAttentionChange = AttentionChange::FrameChange;
    PaintOpenGLWidget *m_pendingAttentionView = nullptr;
    QPoint m_listPressPos;
    int m_toolSmoothValue = 50;   // stabilizer, mirrored by the panel slider
    class ToolOptPanel *m_toolOptPanel = nullptr;
    // The extra tool whose options are on screen, so the panel can be rebuilt
    // when something OUTSIDE it changes what those options should be (the
    // Auto Mapping calculation mode lives in the menu bar, and it decides
    // whether the RDP tolerance applies at all).
    QString m_activeExtraTool;
    QString m_activeExtraToolProperty;
    // PaintOpenGLWidget is only forward-declared here, so the tool travels as
    // its underlying int and the lambda casts it back.
    std::function<void(int)> m_reloadToolOptions;
    // Arming a tool from outside the chip rail (the lock's Arrow fallback).
    // Same int-carried-tool reason as m_reloadToolOptions.
    std::function<void(int tool, bool reloadOptions)> m_applyTool;
    // Locked tool NAMES per view. Kept per view because the binding is, but
    // only the main board's set governs the rail: one tool is armed for the
    // whole application, so two boards cannot disagree about it.
    QHash<QString, QSet<QString>> m_lockedToolsByView;
    int m_currentToolForOptions = 0;
    int m_toolPenWidth = 5;
    bool m_toolFillAllLayers = false;
    bool m_refreshingLists = false;
    bool m_refreshingHistory = false;
    bool m_historyRefreshQueued = false;
    bool m_listMousePressed = false;
    bool m_listDragActive = false;
    // The layers page that took a drop, held only between that drop and the
    // rowsInserted it produces - the other page's tree must not read a
    // neighbour's drop as its own.
    LayerPanel *m_layerDropPanel = nullptr;
    bool m_hasPendingAttention = false;
    int m_pythonFreezeDepth = 0;
};

#endif // MAINWINDOW_H
