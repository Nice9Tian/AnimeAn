#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QJsonArray>
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
    QVector<QTreeWidgetItem *> layerPanelItems() const;
    // Writes the panel's current shape back into the model: the layer group
    // tree follows what the user dragged, and the dragged layer's z-order
    // follows the leaf it landed after.
    void applyLayerPanelStructure(int movedColumnId);
    void showLayerContextMenu(const QPoint &pos);
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
    bool saveTextureViewAs();
    bool exportTextureImage();
    bool writeModelToFile(const AnimeSceneModel &model, const QString &fileName, const QString &dialogTitle);
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
    void refreshLayerList(int selectedRow);
    void refreshFrameList(int selectedRow);
    void refreshAssetList(int selectedRow);
    void setPythonUiFrozen(bool frozen);

    Ui::MainWindow *ui;
    PaintOpenGLWidget *m_paintWidget = nullptr;
    PaintOpenGLWidget *m_childPaintWidget = nullptr;
    PaintOpenGLWidget *m_activePaintWidget = nullptr;
    ChildPaintWindow *m_childPaintWindow = nullptr;
    QVector<PaintOpenGLWidget *> m_paintViews;
    LayerPanel *m_layerPanel = nullptr;
    FramePanel *m_framePanel = nullptr;
    AssetPanel *m_assetPanel = nullptr;
    QDockWidget *m_layerDock = nullptr;
    QDockWidget *m_frameDock = nullptr;
    QDockWidget *m_assetDock = nullptr;
    QDockWidget *m_toolsDock = nullptr;
    QDockWidget *m_toolOptDock = nullptr;
    QDockWidget *m_pythonDebugDock = nullptr;
    QDockWidget *m_historyDock = nullptr;
    HistoryPanel *m_historyPanel = nullptr;
    QDockWidget *m_forcePadDock = nullptr;
    ForcePadPanel *m_forcePadPanel = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QTimer *m_playbackTimer = nullptr;
    PaintOpenGLWidget *m_playbackView = nullptr;
    int m_playbackIndex = 0;
    int m_playbackFrameCount = 0;
    QPlainTextEdit *m_pythonDebugOutput = nullptr;
    QLineEdit *m_pythonDebugCommand = nullptr;
    QString m_currentFilePath;
    QHash<PaintOpenGLWidget *, SelectionAttention> m_attentionByView;
    SelectionAttention m_pendingAttention;
    AttentionChange m_pendingAttentionChange = AttentionChange::FrameChange;
    PaintOpenGLWidget *m_pendingAttentionView = nullptr;
    QPoint m_listPressPos;
    int m_toolSmoothValue = 50;   // stabilizer, mirrored by the panel slider
    class ToolOptPanel *m_toolOptPanel = nullptr;
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
    // True only between a layer-panel drop and the rowsInserted it produces.
    bool m_layerDropInProgress = false;
    bool m_hasPendingAttention = false;
    int m_pythonFreezeDepth = 0;
};

#endif // MAINWINDOW_H
