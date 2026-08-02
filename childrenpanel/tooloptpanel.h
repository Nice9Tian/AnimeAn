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
    void configureLayout(const QJsonObject &layout);
    void configureControls(const QJsonArray &controls);
    void setFillScope(PaintOpenGLWidget::FillScope scope);
    void setSmoothValue(int value);

signals:
    void optionChanged(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn);
    void colorSelected(const QColor &color);
    void fillScopeSelected(PaintOpenGLWidget::FillScope scope);
    void eraserModeSelected(PaintOpenGLWidget::Tool tool);
    void smoothValueChanged(int value);
    void penWidthChanged(int value);

private:
    // Generic "visible_when" support: a control's JSON may declare
    // {"visible_when": {"name": <other control>, "value"/"values": ...}} and
    // the panel shows/hides it as that control's value changes. Which control
    // depends on which stays declared in the Python layout.
    struct VisibilityRule {
        QString watch;
        QStringList values;
        QWidget *target = nullptr;
    };

    QWidget *createButtonControl(const QJsonObject &control);
    QWidget *createListControl(const QJsonObject &control);
    QWidget *createSliderControl(const QJsonObject &control);
    QWidget *createCheckControl(const QJsonObject &control);
    void configureControls(const QJsonArray &controls, int rowSpacing, int columnSpacing);
    void emitOptionChanged(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn);
    void applyVisibilityRules();
    void clearControls();

    Ui::ToolOptPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    QMap<QString, QWidget *> m_controls;
    QList<VisibilityRule> m_visibilityRules;
    QMap<QString, QString> m_controlValues;
    PaintOpenGLWidget::Tool m_tool = PaintOpenGLWidget::Tool::Pen;
};

#endif // TOOLOPTPANEL_H
