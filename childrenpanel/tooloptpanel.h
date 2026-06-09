#ifndef TOOLOPTPANEL_H
#define TOOLOPTPANEL_H

#include "openglwidget.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QWidget>

class QWidget;
class QVBoxLayout;

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
    void configureControls(const QJsonArray &controls);
    void setFillScope(PaintOpenGLWidget::FillScope scope);
    void setSmoothValue(int value);

signals:
    void optionChanged(const QString &hook, const QString &name, const QVariant &value);
    void colorSelected(const QColor &color);
    void fillScopeSelected(PaintOpenGLWidget::FillScope scope);
    void eraserModeSelected(PaintOpenGLWidget::Tool tool);
    void smoothValueChanged(int value);
    void penWidthChanged(int value);

private:
    QWidget *createButtonRow(const QJsonObject &control);
    QWidget *createListControl(const QJsonObject &control);
    QWidget *createSliderControl(const QJsonObject &control);
    void emitOptionChanged(const QString &hook, const QString &name, const QVariant &value);
    void clearControls();

    Ui::ToolOptPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    QMap<QString, QWidget *> m_controls;
    PaintOpenGLWidget::Tool m_tool = PaintOpenGLWidget::Tool::Pen;
};

#endif // TOOLOPTPANEL_H
