#ifndef TOOLSPANEL_H
#define TOOLSPANEL_H

#include "paintopenglwidget.h"

#include <QWidget>

namespace Ui {
class ToolsPanel;
}

class ToolsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolsPanel(QWidget *parent = nullptr);
    ~ToolsPanel();

    void setTool(PaintOpenGLWidget::Tool tool);

signals:
    void toolSelected(PaintOpenGLWidget::Tool tool);

private:
    Ui::ToolsPanel *ui;
};

#endif // TOOLSPANEL_H
