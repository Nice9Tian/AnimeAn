#ifndef TOOLOPTPANEL_H
#define TOOLOPTPANEL_H

#include "openglwidget.h"
#include "../subcontrolframe.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QWidget;
class QVBoxLayout;

namespace Ui {
class ToolOptPanel;
}

class ToolOptPanel : public QWidget, public SubControlHost
{
    Q_OBJECT

public:
    // `subControlHost` opts this instance in as a drop target for sub-control
    // frames. Only the docked options panel takes them: the same class also
    // renders the modal script-settings window, and a frame dropped in there
    // would go down with the dialog.
    explicit ToolOptPanel(QWidget *parent = nullptr, bool subControlHost = false);
    ~ToolOptPanel();

    PaintOpenGLWidget::Tool tool() const;
    void setTool(PaintOpenGLWidget::Tool tool);
    void configureLayout(const QJsonObject &layout);
    void configureControls(const QJsonArray &controls);
    void setFillScope(PaintOpenGLWidget::FillScope scope);
    void setSmoothValue(int value);

    // SubControlHost: a drop lands as one more full-width row of the options
    // column, which is exactly what the declarative "subwindow" control builds
    // when a layout asks for the frame by name. revealSubControl is left at the
    // base no-op on purpose: every row of this column is on screen together, so
    // holding a frame is already showing it.
    QWidget *subControlHostWidget() override;
    QRect subControlPreviewRect(const QPoint &globalPos) const override;
    void embedSubControl(SubControlFrame *frame) override;

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

    // Rebuilds the prototype stylesheets from AnimeTheme and re-applies them
    // to the controls already on screen. Runs at construction and on every
    // theme change: a generated control copied its style when it was built, so
    // a new application palette alone would leave it on the old colours.
    void applyTheme();
    QWidget *createButtonControl(const QJsonObject &control);
    QWidget *createListControl(const QJsonObject &control);
    QWidget *createSliderControl(const QJsonObject &control);
    QWidget *createCheckControl(const QJsonObject &control);
    // Generic colour swatch: opens QColorDialog and reports #AARRGGBB.
    QWidget *createColorControl(const QJsonObject &control);
    // The palette: the same #AARRGGBB value on the declared hook, plus
    // add/remove of the saved set on a hook of its own.
    QWidget *createPaletteControl(const QJsonObject &control);
    // A named SubControlFrame from the registry, embedded at this grid slot.
    // Returns a thin explanatory row instead when the frame is floating: the
    // frame's position is the USER's, and a layout rebuild must not yank it
    // back off their screen.
    QWidget *createSubWindowControl(const QJsonObject &control);
    void configureControls(const QJsonArray &controls, int rowSpacing, int columnSpacing);
    void emitOptionChanged(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn);
    void applyVisibilityRules();
    void clearControls();
    // Frames leave for the registry's keeper before the controls they sat in
    // are deleted: the registry owns their lifetime, not this panel's layout.
    void parkSubControlFrames();
    // The column's trailing spacer, traded between its two jobs. Greedy (the
    // ordinary case) it holds the controls at the top and leaves empty air
    // below; stood down, the leftover height goes to a greedy ROW instead - a
    // sub-control frame carrying a live board, whose size hint is a thumbnail
    // of one. The two cannot both be expansive or they would split it.
    void setTrailingStretchGreedy(bool greedy);
    // Re-asks the question after a frame has arrived or left: greedy again as
    // soon as no frame of ours is actually laid out here.
    void refreshSubControlGreed();

    Ui::ToolOptPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    QMap<QString, QWidget *> m_controls;
    QList<VisibilityRule> m_visibilityRules;
    QMap<QString, QString> m_controlValues;
    // Every frame currently parented into this panel, however it got here.
    QVector<QPointer<SubControlFrame>> m_subControlFrames;
    // What the trailing spacer currently is, so a redundant trade does not
    // delete and rebuild it on every panel refresh.
    bool m_trailingStretchGreedy = true;
    bool m_isSubControlHost = false;
    PaintOpenGLWidget::Tool m_tool = PaintOpenGLWidget::Tool::Pen;
};

#endif // TOOLOPTPANEL_H
