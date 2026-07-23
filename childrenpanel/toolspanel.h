#ifndef TOOLSPANEL_H
#define TOOLSPANEL_H

#include "openglwidget.h"

#include <QVector>
#include <QWidget>

namespace Ui {
class ToolsPanel;
}

class QPushButton;
class QVBoxLayout;

class ToolsPanel : public QWidget
{
    Q_OBJECT

public:
    struct ExtraToolDefinition {
        QString name;
        QString title;
        QString property;
        QString handler;
        QString baseTool;
    };

    explicit ToolsPanel(QWidget *parent = nullptr);
    ~ToolsPanel();

    void setTool(PaintOpenGLWidget::Tool tool);
    void setExtraTools(const QVector<ExtraToolDefinition> &tools);

signals:
    void toolSelected(PaintOpenGLWidget::Tool tool);
    void extraToolSelected(const ToolsPanel::ExtraToolDefinition &tool);

private:
    Ui::ToolsPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    QVector<QPushButton *> m_extraButtons;
    QVector<ExtraToolDefinition> m_extraTools;
};

#endif // TOOLSPANEL_H
