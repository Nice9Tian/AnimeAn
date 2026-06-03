#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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

private:
    void importRaster();
    void refreshLayerList(int selectedRow);
    void refreshFrameList(int selectedRow);

    Ui::MainWindow *ui;
    PaintOpenGLWidget *m_paintWidget = nullptr;
    bool m_refreshingLists = false;
};

#endif // MAINWINDOW_H
