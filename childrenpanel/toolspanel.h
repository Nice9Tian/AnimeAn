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

// One built-in tool as a 44px icon chip. Custom painted rather than a styled
// QToolButton: the chip is a flat ground carrying a 24px line glyph, and both
// come from AnimeTheme roles that no button style knows how to read.
class ToolChip : public QWidget
{
    Q_OBJECT

public:
    // Named after the drawing rather than the tool - the panel owns the
    // tool/glyph pairing, the chip only knows how to draw one.
    enum class Glyph {
        Arrow,
        Pen,
        Eraser,
        Fill,
        Transfer,
        Connect
    };

    explicit ToolChip(Glyph glyph, QWidget *parent = nullptr);

    bool isChecked() const { return m_checked; }
    // Silent by design: the shell echoes the armed tool back into every panel,
    // and a chip that re-emitted on that echo would loop.
    void setChecked(bool checked);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    const Glyph m_glyph;
    bool m_checked = false;
    bool m_hovered = false;
    bool m_pressed = false;
};

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

protected:
    // Paints the rail the chip grid sits in. The rail is not a widget of its
    // own: it is exactly the grid container's geometry, so there is nothing
    // for a second widget to hold that the layout does not already hold.
    void paintEvent(QPaintEvent *event) override;

private:
    void buildBuiltInRail();

    Ui::ToolsPanel *ui;
    QVBoxLayout *m_layout = nullptr;
    bool m_showBuiltIns = true;
    QWidget *m_builtInRail = nullptr;
    QVector<ToolChip *> m_chips;
    // Parallel to m_chips: the tool each chip arms.
    QVector<PaintOpenGLWidget::Tool> m_chipTools;
    QVector<QPushButton *> m_extraButtons;
    QVector<ExtraToolDefinition> m_extraTools;
};

#endif // TOOLSPANEL_H
