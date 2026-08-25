#include "toolspanel.h"
#include "ui_toolspanel.h"

#include "../theme.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QtGlobal>
#include <QtMath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// The rail's metrics, straight from the design: a two column grid of 44px
// chips, 6px between them, 8px of ground around the lot.
constexpr int kChipSize = 44;
constexpr int kChipRadius = 8;
constexpr int kGlyphSize = 24;
constexpr int kGutter = 6;
constexpr int kRailPadding = 8;
constexpr int kRailRadius = 10;
constexpr int kColumns = 2;
// The glyph sheet is drawn on a 24x24 viewBox and the chip carries that box at
// 1:1, so every number below is both a sheet unit and a pixel.
constexpr qreal kStroke = 1.5;
constexpr qreal kArrowheadStroke = 1.4;
constexpr qreal kRingStroke = 1.2;

// The ground the chips sit ON. The two modes put their chip colour on opposite
// sides of the surface ramp - a dark chip is LIGHTER than Surface, a light chip
// is lighter than SurfaceAlt - so the rail takes whichever role stays behind
// it. One role for both modes would make a resting chip invisible in one.
QColor railGround()
{
    return AnimeTheme::mode() == AnimeTheme::Mode::Dark
               ? AnimeTheme::color(AnimeTheme::Role::Surface)
               : AnimeTheme::color(AnimeTheme::Role::SurfaceAlt);
}

// The design's rounded corners are SVG endpoint arcs ("A r r 0 0 sweep x y"),
// and every one of them is circular. Qt describes an arc by a bounding box and
// two angles, so the endpoint form has to be solved for its centre first.
void svgArcTo(QPainterPath &path, qreal radius, bool sweep, qreal x, qreal y)
{
    const QPointF from = path.currentPosition();
    const QPointF to(x, y);
    const qreal dx = to.x() - from.x();
    const qreal dy = to.y() - from.y();
    const qreal chord = std::hypot(dx, dy);
    if (chord < 1e-6) {
        return;
    }
    // A radius too small to reach both ends is grown until it can, which is
    // what SVG does rather than dropping the segment.
    const qreal r = std::max(radius, chord / 2.0);
    const qreal offset = std::sqrt(std::max(qreal(0), r * r - chord * chord / 4.0));
    const QPointF mid((from.x() + to.x()) / 2.0, (from.y() + to.y()) / 2.0);
    const QPointF perpendicular(-dy / chord, dx / chord);
    // Screen y grows downward, so SVG's positive sweep is a clockwise turn and
    // its centre lies on the far side of the chord from a negative sweep's.
    const QPointF centre = sweep ? mid + perpendicular * offset : mid - perpendicular * offset;

    // Qt measures from three o'clock, counter-clockwise, with y flipped.
    const qreal start =
        qRadiansToDegrees(std::atan2(centre.y() - from.y(), from.x() - centre.x()));
    const qreal end = qRadiansToDegrees(std::atan2(centre.y() - to.y(), to.x() - centre.x()));
    qreal span = end - start;
    if (sweep && span > 0.0) {
        span -= 360.0;
    } else if (!sweep && span < 0.0) {
        span += 360.0;
    }
    path.arcTo(QRectF(centre.x() - r, centre.y() - r, 2.0 * r, 2.0 * r), start, span);
}

// Draws one glyph in the 24x24 sheet space the painter is already positioned
// on. Every sub-shape carries the sheet's own stroke width and fill, so the
// tools that mix strokes with solids (Fill's paint, Transfer's grips, Connect's
// port) come out of one routine rather than a shared path.
void drawGlyph(QPainter &painter, ToolChip::Glyph glyph, const QColor &ink)
{
    const QPen line(ink, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(line);
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case ToolChip::Glyph::Arrow: {
        painter.save();
        painter.translate(0.8, 0.5);
        QPainterPath cursor;
        cursor.moveTo(5.5, 3.0);
        cursor.lineTo(5.5, 18.0);
        cursor.lineTo(9.8, 14.0);
        cursor.lineTo(12.4, 20.5);
        cursor.lineTo(14.9, 19.4);
        cursor.lineTo(12.3, 13.0);
        cursor.lineTo(17.6, 13.0);
        cursor.closeSubpath();
        painter.drawPath(cursor);
        painter.restore();
        break;
    }
    case ToolChip::Glyph::Pen: {
        QPainterPath body;
        body.moveTo(3.0, 21.0);
        body.lineTo(4.8, 16.2);
        body.lineTo(16.2, 4.8);
        body.lineTo(19.2, 7.8);
        body.lineTo(7.8, 19.2);
        body.closeSubpath();
        painter.drawPath(body);
        // The ferrule: where the nib ends and the barrel begins.
        painter.drawLine(QPointF(14.4, 6.6), QPointF(17.4, 9.6));
        break;
    }
    case ToolChip::Glyph::Eraser: {
        painter.save();
        painter.translate(12.0, 11.0);
        painter.rotate(-40.0);
        painter.translate(-12.0, -11.0);
        painter.drawRect(QRectF(4.5, 7.5, 15.0, 7.0));
        painter.drawLine(QPointF(11.0, 7.5), QPointF(11.0, 14.5));
        painter.restore();
        // The paper line stays level: only the block is tilted.
        painter.drawLine(QPointF(4.0, 21.0), QPointF(20.0, 21.0));
        break;
    }
    case ToolChip::Glyph::Fill: {
        painter.save();
        painter.translate(12.0, 12.0);
        painter.rotate(-35.0);
        painter.translate(-12.0, -12.0);

        QPainterPath handle;
        handle.moveTo(7.7, 8.1);
        handle.cubicTo(8.4, 4.4, 15.6, 4.4, 16.3, 8.1);
        painter.drawPath(handle);

        // The paint inside the bucket: solid, and drawn before the outline so
        // the outline's stroke sits on top of its own edge.
        QPainterPath paint;
        paint.moveTo(7.3, 13.2);
        paint.lineTo(16.7, 13.2);
        paint.lineTo(15.9, 18.4);
        svgArcTo(paint, 1.7, true, 14.2, 19.9);
        paint.lineTo(9.8, 19.9);
        svgArcTo(paint, 1.7, true, 8.1, 18.4);
        paint.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawPath(paint);
        painter.setPen(line);
        painter.setBrush(Qt::NoBrush);

        painter.drawLine(QPointF(5.2, 8.6), QPointF(18.8, 8.6));
        QPainterPath bucket;
        bucket.moveTo(6.6, 8.6);
        bucket.lineTo(8.1, 18.4);
        svgArcTo(bucket, 1.7, false, 9.8, 19.9);
        bucket.lineTo(14.2, 19.9);
        svgArcTo(bucket, 1.7, false, 15.9, 18.4);
        bucket.lineTo(17.4, 8.6);
        painter.drawPath(bucket);
        painter.restore();
        break;
    }
    case ToolChip::Glyph::Transfer: {
        // The selection box is masked by the move cross's circle: the dashes
        // and the grip nearest the cross stop at the circle instead of running
        // under it. Qt has no mask, so the hole is a clip path.
        QPainterPath sheet;
        sheet.addRect(-2.0, -2.0, 28.0, 28.0);
        QPainterPath clearing;
        clearing.addEllipse(QPointF(16.8, 16.8), 8.1, 8.1);

        painter.save();
        painter.setClipPath(sheet.subtracted(clearing));
        QPen dashed(ink, kStroke, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
        // The sheet's stroke-dasharray is in user units; Qt's is in multiples
        // of the pen width, and the offset with it.
        dashed.setDashPattern({1.0, 1.0});
        dashed.setDashOffset(1.5);
        painter.setPen(dashed);
        painter.drawRect(QRectF(4.5, 4.5, 15.0, 15.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        for (const QPointF &grip : {QPointF(3.2, 3.2), QPointF(18.2, 3.2), QPointF(3.2, 18.2)}) {
            painter.drawRect(QRectF(grip.x(), grip.y(), 2.6, 2.6));
        }
        painter.restore();

        painter.setPen(line);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(16.8, 13.9), QPointF(16.8, 19.7));
        painter.drawLine(QPointF(13.9, 16.8), QPointF(19.7, 16.8));
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        static const QPointF heads[4][3] = {
            {QPointF(16.8, 10.4), QPointF(19.1, 13.9), QPointF(14.5, 13.9)},
            {QPointF(16.8, 23.2), QPointF(14.5, 19.7), QPointF(19.1, 19.7)},
            {QPointF(10.4, 16.8), QPointF(13.9, 14.5), QPointF(13.9, 19.1)},
            {QPointF(23.2, 16.8), QPointF(19.7, 14.5), QPointF(19.7, 19.1)}
        };
        for (const QPointF *head : heads) {
            painter.drawPolygon(head, 3);
        }
        break;
    }
    case ToolChip::Glyph::Connect: {
        QPainterPath gap;
        gap.moveTo(17.2, 4.7);
        gap.cubicTo(17.2, 12.4, 9.4, 12.0, 6.8, 16.0);
        painter.drawPath(gap);

        QPainterPath head;
        head.moveTo(9.2, 15.89);
        head.lineTo(6.8, 16.0);
        head.lineTo(7.11, 13.62);
        painter.setPen(QPen(ink, kArrowheadStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(head);

        // The two open endpoints the tool joins: the one the curve springs
        // from is filled, the one it is heading for is still open.
        painter.setPen(QPen(ink, kRingStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(6.8, 19.9), 1.9, 1.9);
        painter.drawEllipse(QPointF(17.2, 4.7), 1.9, 1.9);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(17.2, 4.7), 0.85, 0.85);
        break;
    }
    }
}

// The eraser chip covers the whole eraser family: Delete Line and Cut Line are
// eraser sub-modes chosen in the tool options, not tools of their own, and a
// rail that disarmed for them would say no tool is active.
bool chipArmsTool(PaintOpenGLWidget::Tool chipTool, PaintOpenGLWidget::Tool current)
{
    if (chipTool == PaintOpenGLWidget::Tool::Eraser) {
        return current == PaintOpenGLWidget::Tool::Eraser
               || current == PaintOpenGLWidget::Tool::DeleteLine
               || current == PaintOpenGLWidget::Tool::CutLine;
    }
    return chipTool == current;
}

}   // namespace

ToolChip::ToolChip(Glyph glyph, QWidget *parent)
    : QWidget(parent)
    , m_glyph(glyph)
{
    setFixedSize(kChipSize, kChipSize);
    // Arming a tool is a mouse act, and the canvas keeps the keyboard: a chip
    // that took focus would take the view's shortcuts with it.
    setFocusPolicy(Qt::NoFocus);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

void ToolChip::setChecked(bool checked)
{
    if (m_checked == checked) {
        return;
    }
    m_checked = checked;
    update();
}

void ToolChip::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QColor ground;
    QColor ink;
    if (m_checked) {
        ground = AnimeTheme::color(m_pressed ? AnimeTheme::Role::AccentActive
                                             : AnimeTheme::Role::Accent);
        // The armed glyph rides on the accent fill. ChipHoverFg is the near
        // white the design asks for in dark mode, but in light mode it is the
        // near black text colour, which would sink into the accent.
        ink = AnimeTheme::mode() == AnimeTheme::Mode::Dark
                  ? AnimeTheme::color(AnimeTheme::Role::ChipHoverFg)
                  : palette().color(QPalette::HighlightedText);
    } else if (m_pressed || m_hovered) {
        ground = AnimeTheme::color(AnimeTheme::Role::ChipHover);
        ink = AnimeTheme::color(AnimeTheme::Role::ChipHoverFg);
    } else {
        ground = AnimeTheme::color(AnimeTheme::Role::ChipRest);
        ink = AnimeTheme::color(AnimeTheme::Role::ChipRestFg);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ground);
    painter.drawRoundedRect(QRectF(rect()), kChipRadius, kChipRadius);

    // The sheet is carried at 1:1, so centring is the only transform - and on
    // a 44px chip the offset is a whole number, which keeps the strokes off
    // the half pixel.
    painter.translate((width() - kGlyphSize) / 2.0, (height() - kGlyphSize) / 2.0);
    drawGlyph(painter, m_glyph, ink);
}

void ToolChip::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hovered = true;
    update();
}

void ToolChip::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hovered = false;
    m_pressed = false;
    update();
}

void ToolChip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    update();
    event->accept();
}

void ToolChip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    event->accept();
    // A press that wandered off the chip before the release is a cancelled
    // click, which is what a button does with it too.
    if (wasPressed && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
}

ToolsPanel::ToolsPanel(QWidget *parent, bool showBuiltIns)
    : QWidget(parent)
    , ui(new Ui::ToolsPanel)
    , m_showBuiltIns(showBuiltIns)
{
    ui->setupUi(this);

    m_layout = new QVBoxLayout(this);
    if (m_showBuiltIns) {
        // The rail keeps its own padding inside its border; this margin is the
        // gap between that border and the dock frame. A page without built-ins
        // keeps the style's default margins, so its list of script buttons
        // sits exactly where it always did.
        m_layout->setContentsMargins(kRailPadding, kRailPadding, kRailPadding, kRailPadding);
        m_layout->setSpacing(kGutter);
        buildBuiltInRail();
        setTool(PaintOpenGLWidget::Tool::Pen);
    }
    m_layout->addStretch();

    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

ToolsPanel::~ToolsPanel()
{
    delete ui;
}

void ToolsPanel::buildBuiltInRail()
{
    // Grid order is the design's, read across then down: pointer tools first,
    // then the two marking tools, then the two that work on what is already
    // drawn.
    struct ChipSpec {
        PaintOpenGLWidget::Tool tool;
        ToolChip::Glyph glyph;
        const char *tooltip;
    };
    static const ChipSpec specs[] = {
        {PaintOpenGLWidget::Tool::Arrow, ToolChip::Glyph::Arrow, "Select strokes and points"},
        {PaintOpenGLWidget::Tool::Pen, ToolChip::Glyph::Pen,
         "Draw a stroke — width, smooth, colour"},
        {PaintOpenGLWidget::Tool::Eraser, ToolChip::Glyph::Eraser, "Erase, delete line, cut line"},
        {PaintOpenGLWidget::Tool::Fill, ToolChip::Glyph::Fill,
         "Flood a region — scope All or Current"},
        {PaintOpenGLWidget::Tool::Transfer, ToolChip::Glyph::Transfer,
         "Move and free transform: drag inside the box, scale from eight grips, "
         "rotate outside the corners"},
        {PaintOpenGLWidget::Tool::Connect, ToolChip::Glyph::Connect,
         "Close the gap between two open endpoints — poly, curve, smooth"}
    };

    m_builtInRail = new QWidget(this);
    m_builtInRail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QGridLayout *grid = new QGridLayout(m_builtInRail);
    grid->setContentsMargins(kRailPadding, kRailPadding, kRailPadding, kRailPadding);
    grid->setSpacing(kGutter);

    for (int index = 0; index < int(sizeof(specs) / sizeof(specs[0])); ++index) {
        const ChipSpec &spec = specs[index];
        ToolChip *chip = new ToolChip(spec.glyph, m_builtInRail);
        chip->setToolTip(QString::fromUtf8(spec.tooltip));
        const PaintOpenGLWidget::Tool tool = spec.tool;
        connect(chip, &ToolChip::clicked, this, [this, tool]() {
            setTool(tool);
            emit toolSelected(tool);
        });
        grid->addWidget(chip, index / kColumns, index % kColumns);
        m_chips.append(chip);
        m_chipTools.append(tool);
    }

    m_layout->addWidget(m_builtInRail, 0, Qt::AlignLeft | Qt::AlignTop);
}

void ToolsPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (!m_builtInRail) {
        return;
    }

    // The rail is the chip grid's own geometry: the container is transparent,
    // so what is drawn here is the ground its chips sit on.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(railGround());
    painter.setPen(QPen(AnimeTheme::color(AnimeTheme::Role::Divider), 1.0));
    painter.drawRoundedRect(QRectF(m_builtInRail->geometry()).adjusted(0.5, 0.5, -0.5, -0.5),
                            kRailRadius, kRailRadius);
}

void ToolsPanel::setTool(PaintOpenGLWidget::Tool tool)
{
    // Chips are silent on setChecked - the shell echoes the armed tool back
    // into every panel, and only the extra buttons need blocking.
    for (int index = 0; index < m_chips.size(); ++index) {
        m_chips[index]->setChecked(chipArmsTool(m_chipTools.at(index), tool));
    }
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            const QSignalBlocker blocker(button);
            button->setChecked(false);
        }
    }
}

void ToolsPanel::clearSelection()
{
    for (ToolChip *chip : m_chips) {
        chip->setChecked(false);
    }
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            const QSignalBlocker blocker(button);
            button->setChecked(false);
        }
    }
}

void ToolsPanel::setExtraTools(const QVector<ExtraToolDefinition> &tools)
{
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            // Out of the layout and off the page now, deleted later: a widget
            // waiting for deleteLater is still a visible child, and one left
            // in the layout would hold its row while the new buttons arrive.
            m_layout->removeWidget(button);
            button->hide();
            button->deleteLater();
        }
    }
    m_extraButtons.clear();
    m_extraTools = tools;

    // Script tools have no glyph, so they stay text buttons: they follow the
    // application palette, which is what makes them answer a theme change.
    const int insertIndex = m_layout ? qMax(0, m_layout->count() - 1) : 0;
    for (int index = 0; index < m_extraTools.size(); ++index) {
        const ExtraToolDefinition definition = m_extraTools.at(index);
        QPushButton *button = new QPushButton(definition.title.isEmpty() ? definition.name : definition.title, this);
        button->setCheckable(true);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            for (ToolChip *chip : m_chips) {
                chip->setChecked(false);
            }
            for (int buttonIndex = 0; buttonIndex < m_extraButtons.size(); ++buttonIndex) {
                const QSignalBlocker blocker(m_extraButtons[buttonIndex]);
                m_extraButtons[buttonIndex]->setChecked(buttonIndex == index);
            }
            emit extraToolSelected(m_extraTools.at(index));
        });
        m_extraButtons.append(button);
        m_layout->insertWidget(insertIndex + index, button);
    }
}
