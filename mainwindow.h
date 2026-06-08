#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QString>

#include "selectionattention.h"

class AssetPanel;
class QDockWidget;
class FramePanel;
class LayerPanel;
class PaintOpenGLWidget;

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
    void createToolDocks();
    void createListDocks();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString &fileName);
    bool loadProjectFrom(const QString &fileName);
    void importRaster();
    void updateWindowTitle();
    void requestAttentionUpdate(AttentionChange change, int frame, int layer, int asset);
    void updateAttention(AttentionChange change, int frame, int layer, int asset);
    void refreshLayerList(int selectedRow);
    void refreshFrameList(int selectedRow);
    void refreshAssetList(int selectedRow);

    Ui::MainWindow *ui;
    PaintOpenGLWidget *m_paintWidget = nullptr;
    LayerPanel *m_layerPanel = nullptr;
    FramePanel *m_framePanel = nullptr;
    AssetPanel *m_assetPanel = nullptr;
    QDockWidget *m_layerDock = nullptr;
    QDockWidget *m_frameDock = nullptr;
    QDockWidget *m_assetDock = nullptr;
    QDockWidget *m_toolsDock = nullptr;
    QDockWidget *m_toolOptDock = nullptr;
    QString m_currentFilePath;
    SelectionAttention m_attention;
    SelectionAttention m_pendingAttention;
    AttentionChange m_pendingAttentionChange = AttentionChange::FrameChange;
    QPoint m_listPressPos;
    bool m_refreshingLists = false;
    bool m_listMousePressed = false;
    bool m_listDragActive = false;
    bool m_hasPendingAttention = false;
};

#endif // MAINWINDOW_H
