#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class AttentionChange {
        FrameChange,
        LayerChange,
        AssetChange
    };

    struct SelectionAttention {
        int frame = 0;
        int layer = -1;
        int asset = -1;
    };

    struct AttentionUpdate {
        bool frame = false;
        bool layer = false;
        bool asset = false;
    };

    void createToolDocks();
    void createListDocks();
    void importRaster();
    void requestAttentionUpdate(AttentionChange change, int frame, int layer, int asset);
    void updateAttention(AttentionChange change, int frame, int layer, int asset);
    AttentionUpdate constrainAttention(AttentionChange change);
    int topLayerForFrame(int frame) const;
    int firstLayerForAsset(int frame, int asset) const;
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
