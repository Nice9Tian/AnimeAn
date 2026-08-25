#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QVector>

#include <functional>

#include "selectionattention.h"

class AssetPanel;
class ChildPaintWindow;
class ForcePadPanel;
class HistoryPanel;
class QAction;
class QDockWidget;
class FramePanel;
class LayerPanel;
class ParentWindow;
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
    void createListDocks();
    void createChildPaintDock();
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
    void refreshFpsCombo();
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
    void createTextureFileMenu();
    void createHistoryDock();
    void createForcePadDock();
    void refreshHistoryList();
    void scheduleHistoryRefresh();
    PaintOpenGLWidget *undoTargetView() const;
    PaintOpenGLWidget *redoTargetView() const;
    void applyHistoryRestore(PaintOpenGLWidget *view);
    void showTextureView();
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
    void refreshFrameList(int selectedRow);
    void refreshAssetList(int selectedRow);
    void setPythonUiFrozen(bool frozen);
    ParentWindow *parentWindowNamed(const QString &name) const;

    Ui::MainWindow *ui;
    PaintOpenGLWidget *m_paintWidget = nullptr;
    PaintOpenGLWidget *m_childPaintWidget = nullptr;
    PaintOpenGLWidget *m_activePaintWidget = nullptr;
    ChildPaintWindow *m_childPaintWindow = nullptr;
    QVector<PaintOpenGLWidget *> m_paintViews;
    LayerPanel *m_mainLayerPanel = nullptr;
    LayerPanel *m_childLayerPanel = nullptr;
    FramePanel *m_framePanel = nullptr;
    AssetPanel *m_assetPanel = nullptr;
    ParentWindow *m_layerDock = nullptr;
    ParentWindow *m_frameDock = nullptr;
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
