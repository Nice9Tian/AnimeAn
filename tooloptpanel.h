#ifndef TOOLOPTPANEL_H
#define TOOLOPTPANEL_H

#include "paintopenglwidget.h"

#include <QColor>
#include <QWidget>

class QWidget;

namespace Ui {
class ToolOptPanel;
}

class ToolOptPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolOptPanel(QWidget *parent = nullptr);
    ~ToolOptPanel();

    PaintOpenGLWidget::Tool tool() const;
    void setTool(PaintOpenGLWidget::Tool tool);
    void setFillScope(PaintOpenGLWidget::FillScope scope);
    void setSmoothValue(int value);

signals:
    void colorSelected(const QColor &color);
    void fillScopeSelected(PaintOpenGLWidget::FillScope scope);
    void eraserModeSelected(PaintOpenGLWidget::Tool tool);
    void smoothValueChanged(int value);
    void penWidthChanged(int value);

private:
    void setColorButtonStyle();
    void setControlVisible(QWidget *widget, bool visible);
    void updateVisibleControls();

    Ui::ToolOptPanel *ui;
    QWidget *m_colorRow = nullptr;
    PaintOpenGLWidget::Tool m_tool = PaintOpenGLWidget::Tool::Pen;
};

#endif // TOOLOPTPANEL_H
