#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QVector>

#include "selectionattention.h"

class AssetPanel;
class ChildPaintWindow;
class QDockWidget;
class FramePanel;
class LayerPanel;
class PaintOpenGLWidget;
class QLineEdit;
class QPlainTextEdit;

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
    void runPythonDebugCommand(const QString &command);
    QString runEmbeddedPythonCommand(const QString &command);
    QString resolvePythonScriptPath(const QString &scriptName) const;
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString &fileName);
    bool loadProjectFrom(const QString &fileName);
    void importRaster(PaintOpenGLWidget *view);
    void importOpenToonzLines(PaintOpenGLWidget *view);
    void importClipStudioPaint(PaintOpenGLWidget *view);
    void createTextureFileMenu();
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
    QPlainTextEdit *m_pythonDebugOutput = nullptr;
    QLineEdit *m_pythonDebugCommand = nullptr;
    QString m_currentFilePath;
    QHash<PaintOpenGLWidget *, SelectionAttention> m_attentionByView;
    SelectionAttention m_pendingAttention;
    AttentionChange m_pendingAttentionChange = AttentionChange::FrameChange;
    PaintOpenGLWidget *m_pendingAttentionView = nullptr;
    QPoint m_listPressPos;
    int m_toolSmoothValue = 50;
    int m_toolPenWidth = 5;
    bool m_toolFillAllLayers = false;
    bool m_refreshingLists = false;
    bool m_listMousePressed = false;
    bool m_listDragActive = false;
    bool m_hasPendingAttention = false;
    int m_pythonFreezeDepth = 0;
};

#endif // MAINWINDOW_H
