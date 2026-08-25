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
        // Which Tools page the button belongs on. Declared by the script
        // (pyfile/extra_tools.py); the panel itself never reads it - the shell
        // routes the definition to the instance that owns that page.
        QString page;
    };

    // showBuiltIns false leaves the enum tools out entirely: the mapping and
    // fukusato pages are lists of script buttons, and a second copy of Pen on
    // each of them would be three armable Pens.
    explicit ToolsPanel(QWidget *parent = nullptr, bool showBuiltIns = true);
    ~ToolsPanel();

    void setTool(PaintOpenGLWidget::Tool tool);
    // Drops every check in this instance. The shell calls it on the pages that
    // did NOT make the selection, because exclusivity now spans three panels.
    void clearSelection();
    void setExtraTools(const QVector<ExtraToolDefinition> &tools);

signals:
    void toolSelected(PaintOpenGLWidget::Tool tool);
    void extraToolSelected(const ToolsPanel::ExtraToolDefinition &tool);

private:
    Ui::ToolsPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    bool m_showBuiltIns = true;
    QPushButton *m_arrowButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_transferButton = nullptr;
    QVector<QPushButton *> m_extraButtons;
    QVector<ExtraToolDefinition> m_extraTools;
};

#endif // TOOLSPANEL_H
