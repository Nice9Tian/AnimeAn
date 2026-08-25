#include "timelinewindow.h"
#include "../paintviewcontainer.h"
#include "../theme.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Design metrics. Everything the timeline draws is a multiple of these, so a
// single number is the whole answer to "why is it that size".
constexpr int kBarHeight = 34;
constexpr int kButtonWidth = 33;
constexpr int kPlayWidth = 38;
constexpr int kGlyphPx = 17;
constexpr int kSmallGlyphPx = 15;
constexpr int kDividerGap = 6;
constexpr int kFrameFieldWidth = 42;
constexpr int kFrameFieldHeight = 22;
constexpr int kFrameFieldGap = 8;
// The clear space a family keeps from its neighbours before the transport
// gives up on one row and folds.
constexpr int kFamilyGap = 8;
constexpr int kKeyCellWidth = 126;
constexpr int kKeyCellHeight = 74;
// A hold sliver is a fifth of a key cell along the run, at every scale: wide
// enough to click, narrow enough to read as "the same drawing again".
constexpr qreal kHoldFraction = 0.2;
// The preview is the PAGE, so it keeps 4:3 whatever the cell does.
constexpr qreal kPreviewAspect = 4.0 / 3.0;
constexpr int kLaneThickness = 12;
constexpr int kLaneDot = 9;
constexpr int kScrollThickness = 6;
// How much of the neighbouring cell stays visible when the run is scrolled to
// follow the playhead: enough to read as "there is more this way".
constexpr int kScrollIntoViewMargin = 10;
constexpr int kStripColumnWidth = 126;
constexpr int kStripCommandHeight = 28;
constexpr int kStripCommandWidth = 30;
constexpr int kStripCommandCount = 4;
// Floors for the resizable strip: below these a cell has no room for a
// preview and the run stops being readable. The column floor is the COMMAND
// ROW's own width: the transport drops the frame family while docked to a
// side, so a narrower strip would put Delete Frame past the widget's edge
// with nothing else left to reach it by.
constexpr int kMinStripThickness = 44;
constexpr int kMinStripColumn = kStripCommandCount * kStripCommandWidth + kScrollThickness;
constexpr int kSideTitleHeight = 22;
// What a collapsed side dock shrinks to: the title bar needs its name.
constexpr int kCollapsedColumnWidth = 132;

// Shooting cadences. "1s with N" reads as one drawing held for N frames of a
// 24 fps base, so the rate is 24/N.
struct Cadence {
    const char *title;
    const char *shortTitle;
    int fps;
};
const Cadence kCadences[] = {
    {"1s with 4", "1s×4", 6},
    {"1s with 3", "1s×3", 8},
    {"1s with 2", "1s×2", 12},
    {"1s with 1", "1s×1", 24},
};
constexpr int kCadenceCount = int(sizeof(kCadences) / sizeof(kCadences[0]));

// The frame commands, in the order both the transport's left family and the
// vertical strip's command row show them.
const TimelineCommand kFrameCommands[kStripCommandCount] = {
    TimelineCommand::AddFrame, TimelineCommand::AddHold, TimelineCommand::Duplicate,
    TimelineCommand::DeleteFrame};

enum class Glyph {
    Onion,
    Prev,
    Pause,
    Play,
    Loop,
    Next,
    ChevronDown,
    CollapseUp,
    CollapseDown,
    Close,
    Float,
    ToVertical,
    ToHorizontal,
    AddFrame,
    AddHold,
    Duplicate,
    DeleteFrame,
    PillUp
};

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor(int(std::lround(a.red() * (1.0 - t) + b.red() * t)),
                  int(std::lround(a.green() * (1.0 - t) + b.green() * t)),
                  int(std::lround(a.blue() * (1.0 - t) + b.blue() * t)));
}

QColor role(AnimeTheme::Role r)
{
    return AnimeTheme::color(r);
}

// The pale accent field a hovered control sits in, in whichever direction the
// mode's ground runs.
QColor accentWash()
{
    return mix(role(AnimeTheme::Role::Surface), role(AnimeTheme::Role::Accent), 0.22);
}

QColor stripGround()
{
    return role(AnimeTheme::Role::Window);
}

QPainterPath glyphPath(Glyph glyph)
{
    QPainterPath path;
    switch (glyph) {
    case Glyph::Onion:
        break;   // drawn from circles, not a path
    case Glyph::Prev:
        path.moveTo(17, 5);
        path.lineTo(17, 19);
        path.lineTo(7, 12);
        path.closeSubpath();
        path.moveTo(4, 5);
        path.lineTo(4, 19);
        break;
    case Glyph::Pause:
        path.moveTo(9, 5);
        path.lineTo(9, 19);
        path.moveTo(15, 5);
        path.lineTo(15, 19);
        break;
    case Glyph::Play:
        path.moveTo(7, 4);
        path.lineTo(20, 12);
        path.lineTo(7, 20);
        path.closeSubpath();
        break;
    case Glyph::Loop: {
        const QRectF circle(4, 3, 16, 16);
        path.arcMoveTo(circle, 0);
        path.arcTo(circle, 0, 310);
        path.moveTo(20, 5);
        path.lineTo(20, 11);
        path.lineTo(14, 11);
        break;
    }
    case Glyph::Next:
        path.moveTo(7, 5);
        path.lineTo(7, 19);
        path.lineTo(17, 12);
        path.closeSubpath();
        path.moveTo(20, 5);
        path.lineTo(20, 19);
        break;
    case Glyph::ChevronDown:
        path.moveTo(6, 9);
        path.lineTo(12, 15);
        path.lineTo(18, 9);
        break;
    case Glyph::CollapseUp:
        path.moveTo(6, 14);
        path.lineTo(12, 8);
        path.lineTo(18, 14);
        path.moveTo(6, 19);
        path.lineTo(12, 13);
        path.lineTo(18, 19);
        break;
    case Glyph::CollapseDown:
        path.moveTo(6, 10);
        path.lineTo(12, 16);
        path.lineTo(18, 10);
        path.moveTo(6, 5);
        path.lineTo(12, 11);
        path.lineTo(18, 5);
        break;
    case Glyph::Close:
        path.moveTo(5, 5);
        path.lineTo(19, 19);
        path.moveTo(19, 5);
        path.lineTo(5, 19);
        break;
    case Glyph::Float:
        path.moveTo(3, 15);
        path.lineTo(3, 4);
        path.lineTo(16, 4);
        path.lineTo(16, 7);
        path.addRect(QRectF(8, 8, 13, 12));
        path.moveTo(8, 12);
        path.lineTo(21, 12);
        break;
    case Glyph::ToVertical:
        path.addRect(QRectF(3, 11, 12, 9));
        path.moveTo(6, 7);
        path.cubicTo(10, 2.8, 17, 3.8, 20, 8);
        path.moveTo(20, 8);
        path.lineTo(16.6, 7.5);
        path.moveTo(20, 8);
        path.lineTo(19.5, 4.6);
        break;
    case Glyph::ToHorizontal:
        path.addRect(QRectF(11, 8, 9, 12));
        path.moveTo(7, 5);
        path.cubicTo(2.8, 9, 3.8, 16, 8, 19);
        path.moveTo(8, 19);
        path.lineTo(7.5, 15.6);
        path.moveTo(8, 19);
        path.lineTo(11.4, 18.5);
        break;
    case Glyph::AddFrame:
        path.moveTo(5, 3);
        path.lineTo(13, 3);
        path.lineTo(18, 8);
        path.lineTo(18, 21);
        path.lineTo(5, 21);
        path.closeSubpath();
        path.moveTo(13, 3);
        path.lineTo(13, 8);
        path.lineTo(18, 8);
        path.moveTo(11.5, 11.5);
        path.lineTo(11.5, 17.5);
        path.moveTo(8.5, 14.5);
        path.lineTo(14.5, 14.5);
        break;
    case Glyph::AddHold:
        path.addRect(QRectF(4, 4, 4, 16));
        path.moveTo(14, 8);
        path.lineTo(14, 18);
        path.moveTo(9, 13);
        path.lineTo(19, 13);
        break;
    case Glyph::Duplicate:
        // Two sheets, offset: the copy is a second drawing, not a second
        // exposure of the first (which is what the hold glyph says).
        path.addRect(QRectF(8, 3, 12, 12));
        path.addRect(QRectF(4, 9, 12, 12));
        break;
    case Glyph::DeleteFrame:
        path.moveTo(4, 7);
        path.lineTo(20, 7);
        path.moveTo(9, 7);
        path.lineTo(9, 4);
        path.lineTo(15, 4);
        path.lineTo(15, 7);
        path.moveTo(6, 7);
        path.lineTo(7, 20);
        path.lineTo(17, 20);
        path.lineTo(18, 7);
        path.moveTo(10, 11);
        path.lineTo(10, 17);
        path.moveTo(14, 11);
        path.lineTo(14, 17);
        break;
    case Glyph::PillUp:
        path.moveTo(6, 14);
        path.lineTo(12, 8);
        path.lineTo(18, 14);
        break;
    }
    return path;
}

void drawGlyph(QPainter &painter, const QRect &box, Glyph glyph, const QColor &color, int glyphPx)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = qreal(glyphPx) / 24.0;
    painter.translate(box.x() + (box.width() - glyphPx) / 2.0,
                      box.y() + (box.height() - glyphPx) / 2.0);
    painter.scale(scale, scale);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (glyph == Glyph::Onion) {
        // Three overlapping sheets, the nearest one solid: the sheet the pen
        // is on reads as the present, the fainter ones as the frames behind.
        const qreal centres[] = {8.4, 11.8, 15.6};
        const qreal fills[] = {0.16, 0.36, 1.0};
        for (int i = 0; i < 3; ++i) {
            QColor fill = color;
            fill.setAlphaF(fills[i]);
            painter.setBrush(fill);
            painter.setPen(i == 2 ? QPen(Qt::NoPen) : pen);
            painter.drawEllipse(QPointF(centres[i], 12.0), 7.2, 7.2);
        }
        painter.restore();
        return;
    }

    painter.drawPath(glyphPath(glyph));
    painter.restore();
}

// One flat cell of chrome: no bevel, no radius, the state carried entirely by
// the ground it sits on.
void drawChromeCell(QPainter &painter, const QRect &rect, bool hover, bool active, bool enabled)
{
    if (active) {
        painter.fillRect(rect, role(AnimeTheme::Role::Accent));
    } else if (hover && enabled) {
        painter.fillRect(rect, accentWash());
    }
}

QColor chromeForeground(bool hover, bool active, bool enabled)
{
    if (active) {
        return QColor(Qt::white);
    }
    if (!enabled) {
        QColor dim = role(AnimeTheme::Role::TextDim);
        dim.setAlpha(110);
        return dim;
    }
    return hover ? role(AnimeTheme::Role::Text) : role(AnimeTheme::Role::TextDim);
}

QFont monoFont(int pixelSize, bool bold)
{
    QFont font(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return font;
}

QFont labelFont(int pixelSize)
{
    QFont font;
    font.setPixelSize(pixelSize);
    font.setBold(true);
    font.setLetterSpacing(QFont::PercentageSpacing, 110.0);
    return font;
}

QString paddedFrame(int frame)
{
    return QString::number(frame + 1).rightJustified(3, QLatin1Char('0'));
}

Glyph glyphFor(TimelineCommand command, const TimelineState &state)
{
    switch (command) {
    case TimelineCommand::AddFrame:
        return Glyph::AddFrame;
    case TimelineCommand::AddHold:
        return Glyph::AddHold;
    case TimelineCommand::Duplicate:
        return Glyph::Duplicate;
    case TimelineCommand::DeleteFrame:
        return Glyph::DeleteFrame;
    case TimelineCommand::Onion:
        return Glyph::Onion;
    case TimelineCommand::Prev:
        return Glyph::Prev;
    case TimelineCommand::Pause:
        return Glyph::Pause;
    case TimelineCommand::Play:
        return Glyph::Play;
    case TimelineCommand::Loop:
        return Glyph::Loop;
    case TimelineCommand::Next:
        return Glyph::Next;
    case TimelineCommand::Float:
        return Glyph::Float;
    case TimelineCommand::Orientation:
        return state.vertical ? Glyph::ToHorizontal : Glyph::ToVertical;
    case TimelineCommand::Collapse:
        return state.collapsed ? Glyph::CollapseDown : Glyph::CollapseUp;
    case TimelineCommand::Close:
        return Glyph::Close;
    default:
        break;
    }
    return Glyph::ChevronDown;
}

QString tooltipFor(TimelineCommand command, const TimelineState &state)
{
    switch (command) {
    case TimelineCommand::AddFrame:
        return QStringLiteral("Add frame at the end of the sheet");
    case TimelineCommand::AddHold:
        return QStringLiteral("Hold the current frame - the new row right after it "
                              "shows the same drawing; editing either one changes both");
    case TimelineCommand::Duplicate:
        return QStringLiteral("Duplicate frame - copies the current drawing to a new frame");
    case TimelineCommand::DeleteFrame:
        return QStringLiteral("Delete frame");
    case TimelineCommand::Onion:
        return state.onionAvailable
                   ? QStringLiteral("Onion skin")
                   : QStringLiteral("Onion skin is a main board feature - point the "
                                    "timeline back at the main board to use it");
    case TimelineCommand::GuideLines:
        if (!state.onionAvailable) {
            return QStringLiteral("Onion skin is a main board feature - point the "
                                  "timeline back at the main board to use it");
        }
        return state.onion ? QStringLiteral("Include guide lines in the ghosts")
                           : QStringLiteral("Turn onion skin on first");
    case TimelineCommand::Prev:
        return QStringLiteral("Previous frame");
    case TimelineCommand::Pause:
        return QStringLiteral("Pause");
    case TimelineCommand::Play:
        return QStringLiteral("Play");
    case TimelineCommand::Loop:
        return QStringLiteral("Loop");
    case TimelineCommand::Next:
        return QStringLiteral("Next frame");
    case TimelineCommand::Rate:
        return QStringLiteral("Playback rate");
    case TimelineCommand::Orientation:
        return state.vertical ? QStringLiteral("Dock the strip under the canvas")
                              : QStringLiteral("Dock the strip beside the canvas");
    case TimelineCommand::Float:
        return state.floating ? QStringLiteral("Dock the timeline again")
                              : QStringLiteral("Float the timeline as a window");
    case TimelineCommand::Collapse:
        return state.collapsed ? QStringLiteral("Show the frame strip")
                               : QStringLiteral("Hide the frame strip");
    case TimelineCommand::Close:
        return QStringLiteral("Close the timeline window");
    }
    return QString();
}

}   // namespace

// ---------------------------------------------------------------- transport

TimelineTransportBar::TimelineTransportBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_frameField = new QLineEdit(this);
    m_frameField->setFrame(false);
    m_frameField->setAlignment(Qt::AlignCenter);
    m_frameField->setMaxLength(4);
    m_frameField->setValidator(new QIntValidator(1, 9999, m_frameField));
    m_frameField->setFont(monoFont(12, true));
    m_frameField->setToolTip(QStringLiteral("Current frame - type a number to jump"));
    connect(m_frameField, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const int typed = m_frameField->text().toInt(&ok);
        if (ok && typed >= 1) {
            emit frameTyped(typed - 1);
        }
        m_frameField->setText(paddedFrame(m_state.currentFrame));
    });

    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() {
        QPalette fieldPalette = m_frameField->palette();
        fieldPalette.setColor(QPalette::Base, Qt::transparent);
        fieldPalette.setColor(QPalette::Text, role(AnimeTheme::Role::Text));
        m_frameField->setPalette(fieldPalette);
        update();
    });
    QPalette fieldPalette = m_frameField->palette();
    fieldPalette.setColor(QPalette::Base, Qt::transparent);
    fieldPalette.setColor(QPalette::Text, role(AnimeTheme::Role::Text));
    m_frameField->setPalette(fieldPalette);
}

QSize TimelineTransportBar::sizeHint() const
{
    return QSize(320, kBarHeight * m_rows);
}

QSize TimelineTransportBar::minimumSizeHint() const
{
    // The dock reads the title bar's height from here as well as from the
    // hint, and a two-row bar that reported one row would be clipped.
    return QSize(kButtonWidth * 3, kBarHeight * m_rows);
}

void TimelineTransportBar::setState(const TimelineState &state)
{
    m_state = state;
    if (!m_frameField->hasFocus()) {
        m_frameField->setText(paddedFrame(m_state.currentFrame));
    }
    relayout();
    update();
}

void TimelineTransportBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TimelineTransportBar::relayout()
{
    m_items.clear();
    m_dividers.clear();
    const QFontMetrics guideMetrics(labelFont(10));
    const int guideWidth = guideMetrics.horizontalAdvance(QStringLiteral("GUIDE")) + 14;
    const QFontMetrics rateMetrics(monoFont(11, true));
    const int rateWidth =
        rateMetrics.horizontalAdvance(TimelineWindow::shortTextForFps(m_state.fps)) + 9 + 7 + 11 + 9;

    const auto add = [&](TimelineCommand command, const QRect &rect, bool enabled, bool active) {
        Item item;
        item.command = command;
        item.rect = rect;
        item.enabled = enabled;
        item.active = active;
        m_items.append(item);
    };

    // The five families. A family is laid out as one contiguous run and never
    // folds internally: the whole point of the two-row mode is that a control
    // keeps the neighbours it is read against.
    const bool collapseAtLeft = m_state.vertical && m_state.leftAligned && !m_state.floating;
    const bool showFrameCommands = !m_state.vertical;
    const int leftWidth = (collapseAtLeft ? kButtonWidth : 0)
                          + (showFrameCommands ? kStripCommandCount * kButtonWidth : 0);
    // Window chrome, from the far edge inwards, so the close box is always the
    // last thing before the frame.
    QVector<TimelineCommand> chrome;
    chrome.append(TimelineCommand::Close);
    if (!collapseAtLeft && !m_state.floating) {
        chrome.append(TimelineCommand::Collapse);
    }
    chrome.append(TimelineCommand::Orientation);
    chrome.append(TimelineCommand::Float);
    const int chromeWidth = chrome.size() * kButtonWidth;

    const int dividerWidth = 1 + 2 * kDividerGap;
    const int onionWidth = kButtonWidth + guideWidth;
    const int playbackWidth = kButtonWidth * 4 + kPlayWidth;
    const int rateFieldWidth = rateWidth + kFrameFieldWidth + 2 * kFrameFieldGap;
    const int assemblyWidth = onionWidth + dividerWidth + playbackWidth + dividerWidth
                              + rateFieldWidth;

    // One row while the side families and the centred assembly can all stand
    // clear of each other; otherwise the assembly drops to a second row and
    // the window chrome keeps the top-right corner it belongs in.
    // TWICE the WIDER side, not the sum: the assembly is centred on the whole
    // bar (below), so it reaches as far towards the narrow side as towards the
    // wide one - summing them lets the vertical layout (no frame commands at
    // the left, full chrome at the right) run the frame field over the
    // float/orientation cells.
    const int oneRowWidth = 2 * std::max(leftWidth, chromeWidth) + assemblyWidth
                            + 2 * kFamilyGap;
    const int rows = width() >= oneRowWidth ? 1 : 2;
    if (rows != m_rows) {
        m_rows = rows;
        // The dock reads the title height from the hint, so the fold has to be
        // announced rather than just drawn.
        setFixedHeight(kBarHeight * m_rows);
        updateGeometry();
    }
    // Rows are measured from the band, not from height(): a fold announced
    // above has not been granted by the parent layout yet.
    const int h = kBarHeight;

    int x = 0;
    if (collapseAtLeft) {
        add(TimelineCommand::Collapse, QRect(x, 0, kButtonWidth, h), true, false);
        x += kButtonWidth;
    }
    if (showFrameCommands) {
        // In the vertical layout the strip's own command row carries these, so
        // the bar must not show a second copy.
        for (TimelineCommand command : kFrameCommands) {
            add(command, QRect(x, 0, kButtonWidth, h), true, false);
            x += kButtonWidth;
        }
    }

    int right = width();
    for (TimelineCommand command : chrome) {
        right -= kButtonWidth;
        add(command, QRect(right, 0, kButtonWidth, h), true, false);
    }

    // Centre: one assembly, always the same order, centred on the bar rather
    // than on what is left of it - the design's whole point is that the
    // playback controls do not move when the side groups change.
    const int assemblyRow = rows == 1 ? 0 : 1;
    const int assemblyY = assemblyRow * kBarHeight;
    int cx = std::max(0, (width() - assemblyWidth) / 2);
    if (rows == 1) {
        // Both side families are cleared explicitly. The threshold above
        // already guarantees the room; this is what makes the guarantee local
        // to the line that places the assembly.
        cx = std::max(x, std::min(cx, width() - chromeWidth - kFamilyGap - assemblyWidth));
    }
    add(TimelineCommand::Onion, QRect(cx, assemblyY, kButtonWidth, h), m_state.onionAvailable,
        m_state.onionAvailable && m_state.onion);
    cx += kButtonWidth;
    add(TimelineCommand::GuideLines, QRect(cx, assemblyY, guideWidth, h),
        m_state.onionAvailable && m_state.onion,
        m_state.onionAvailable && m_state.onion && m_state.guideLines);
    cx += guideWidth;
    m_dividers.append(QRect(cx + kDividerGap, assemblyY + (h - 20) / 2, 1, 20));
    cx += dividerWidth;
    add(TimelineCommand::Prev, QRect(cx, assemblyY, kButtonWidth, h), true, false);
    cx += kButtonWidth;
    add(TimelineCommand::Pause, QRect(cx, assemblyY, kButtonWidth, h), m_state.playing, false);
    cx += kButtonWidth;
    add(TimelineCommand::Play, QRect(cx, assemblyY, kPlayWidth, h), !m_state.playing,
        m_state.playing);
    cx += kPlayWidth;
    add(TimelineCommand::Loop, QRect(cx, assemblyY, kButtonWidth, h), true, m_state.loop);
    cx += kButtonWidth;
    add(TimelineCommand::Next, QRect(cx, assemblyY, kButtonWidth, h), true, false);
    cx += kButtonWidth;
    m_dividers.append(QRect(cx + kDividerGap, assemblyY + (h - 20) / 2, 1, 20));
    cx += dividerWidth;
    add(TimelineCommand::Rate, QRect(cx, assemblyY, rateWidth, h), true, false);
    cx += rateWidth;
    m_frameField->setGeometry(cx + kFrameFieldGap,
                              assemblyY + (h - kFrameFieldHeight) / 2,
                              kFrameFieldWidth, kFrameFieldHeight);
}

int TimelineTransportBar::itemAt(const QPoint &pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void TimelineTransportBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
    painter.drawLine(0, 0, width(), 0);
    if (m_rows > 1) {
        painter.drawLine(0, kBarHeight, width(), kBarHeight);
    }

    for (const QRect &divider : m_dividers) {
        painter.fillRect(divider, role(AnimeTheme::Role::Divider));
    }

    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        const bool hover = (i == m_hoverItem);
        drawChromeCell(painter, item.rect, hover, item.active, item.enabled);
        const QColor foreground = chromeForeground(hover, item.active, item.enabled);

        if (item.command == TimelineCommand::GuideLines) {
            painter.setPen(foreground);
            painter.setFont(labelFont(10));
            const int half = item.rect.height() / 2;
            painter.drawText(QRect(item.rect.x(), item.rect.y() + half - 11, item.rect.width(), 11),
                             Qt::AlignCenter, QStringLiteral("GUIDE"));
            painter.drawText(QRect(item.rect.x(), item.rect.y() + half, item.rect.width(), 11),
                             Qt::AlignCenter, QStringLiteral("LINE"));
            continue;
        }
        if (item.command == TimelineCommand::Rate) {
            painter.setPen(foreground);
            painter.setFont(monoFont(11, true));
            const QRect textRect = item.rect.adjusted(9, 0, -(11 + 9), 0);
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                             TimelineWindow::shortTextForFps(m_state.fps));
            drawGlyph(painter, QRect(item.rect.right() - 9 - 11, item.rect.y(), 11,
                                     item.rect.height()),
                      Glyph::ChevronDown, foreground, 11);
            continue;
        }
        const int glyphPx = (item.command == TimelineCommand::Orientation
                             || item.command == TimelineCommand::Collapse
                             || item.command == TimelineCommand::Close
                             || item.command == TimelineCommand::Float)
                                ? kSmallGlyphPx
                                : kGlyphPx;
        drawGlyph(painter, item.rect, glyphFor(item.command, m_state), foreground,
                  item.command == TimelineCommand::Play ? 19 : glyphPx);
    }
}

void TimelineTransportBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressedItem = itemAt(event->pos());
    if (m_pressedItem < 0) {
        // Empty space on a title bar belongs to the WINDOW. Ignoring sends the
        // press on to the QDockWidget, which starts its own drag with the
        // native docking preview; swallowing it would pin the dock in place.
        event->ignore();
        return;
    }
    update();
}

void TimelineTransportBar::mouseReleaseEvent(QMouseEvent *event)
{
    const int pressed = m_pressedItem;
    m_pressedItem = -1;
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (pressed < 0) {
        event->ignore();   // the gesture belongs to the dock, and so does its end
        return;
    }
    if (pressed >= m_items.size()) {
        update();
        return;
    }
    const Item item = m_items[pressed];
    if (!item.rect.contains(event->pos()) || !item.enabled) {
        update();
        return;
    }
    if (item.command == TimelineCommand::Rate) {
        showRateMenu(item.rect);
        update();
        return;
    }
    emit commandTriggered(item.command);
    update();
}

void TimelineTransportBar::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = itemAt(event->pos());
    if (hover != m_hoverItem) {
        m_hoverItem = hover;
        setToolTip(hover >= 0 ? tooltipFor(m_items[hover].command, m_state) : QString());
        update();
    }
    if ((event->buttons() & Qt::LeftButton) && m_pressedItem < 0) {
        // Mouse tracking is on for the hover, so buttonless moves arrive here
        // too; only a move that continues an ignored press is the dock's.
        event->ignore();
    }
}

void TimelineTransportBar::leaveEvent(QEvent *event)
{
    m_hoverItem = -1;
    setToolTip(QString());
    update();
    QWidget::leaveEvent(event);
}

void TimelineTransportBar::showRateMenu(const QRect &anchor)
{
    QMenu menu(this);
    for (int i = 0; i < kCadenceCount; ++i) {
        QAction *action = menu.addAction(QStringLiteral("%1  -  %2 fps")
                                             .arg(QString::fromUtf8(kCadences[i].title))
                                             .arg(kCadences[i].fps));
        action->setCheckable(true);
        action->setChecked(m_state.fps == kCadences[i].fps);
        const int fps = kCadences[i].fps;
        connect(action, &QAction::triggered, this, [this, fps]() { emit fpsPicked(fps); });
    }
    menu.addSeparator();

    QWidget *typed = new QWidget(&menu);
    QHBoxLayout *typedLayout = new QHBoxLayout(typed);
    typedLayout->setContentsMargins(9, 4, 9, 4);
    typedLayout->setSpacing(7);
    QLabel *prefix = new QLabel(QStringLiteral("or type"), typed);
    QLineEdit *field = new QLineEdit(QString::number(m_state.fps), typed);
    field->setValidator(new QIntValidator(1, 120, field));
    field->setFixedWidth(46);
    field->setAlignment(Qt::AlignCenter);
    QLabel *suffix = new QLabel(QStringLiteral("fps · 1-120"), typed);
    typedLayout->addWidget(prefix);
    typedLayout->addWidget(field);
    typedLayout->addWidget(suffix);
    QWidgetAction *typedAction = new QWidgetAction(&menu);
    typedAction->setDefaultWidget(typed);
    menu.addAction(typedAction);
    connect(field, &QLineEdit::returnPressed, &menu, [this, field, &menu]() {
        const int fps = TimelineWindow::fpsForText(field->text(), m_state.fps);
        menu.close();
        emit fpsPicked(fps);
    });

    menu.exec(mapToGlobal(QPoint(anchor.left(), anchor.top() - menu.sizeHint().height() - 4)));
}

// ------------------------------------------------------------------- strip

TimelineStrip::TimelineStrip(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    // Both axes expand: the dock's splitter is the handle, and relayout reads
    // the cell size back off whatever the user left the widget at.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(kMinStripThickness);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

QSize TimelineStrip::sizeHint() const
{
    return m_vertical ? QSize(kStripColumnWidth + kScrollThickness, 240)
                      : QSize(320, kKeyCellHeight + kScrollThickness);
}

void TimelineStrip::setVertical(bool vertical)
{
    if (m_vertical == vertical) {
        return;
    }
    m_vertical = vertical;
    m_scroll = 0;
    if (vertical) {
        setMinimumWidth(kMinStripColumn);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        setMinimumHeight(kMinStripThickness);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    }
    // A re-orientation re-renders every cell at a new size.
    m_thumbnails.clear();
    updateGeometry();
    relayout();
    update();
}

void TimelineStrip::setState(const TimelineState &state)
{
    m_state = state;
    relayout();
    ensureCurrentVisible();
    update();
}

void TimelineStrip::setThumbnailProvider(std::function<QImage(int, QSize)> provider)
{
    m_provider = std::move(provider);
    m_thumbnails.clear();
    update();
}

void TimelineStrip::clearThumbnails()
{
    m_thumbnails.clear();
    update();
}

void TimelineStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int before = m_cellThickness;
    relayout();
    if (m_cellThickness != before) {
        // The cache is keyed by frame and validated by size; a resize makes
        // every entry stale at once, so drop them rather than pay a compare
        // per cell for the rest of the session.
        m_thumbnails.clear();
    }
}

int TimelineStrip::cellOrigin() const
{
    return m_vertical ? kStripCommandHeight : 0;
}

QRect TimelineStrip::commandRect(int index) const
{
    if (!m_vertical || index < 0 || index >= kStripCommandCount) {
        return QRect();
    }
    return QRect(index * kStripCommandWidth, 0, kStripCommandWidth, kStripCommandHeight);
}

QString TimelineStrip::nameFor(int frame) const
{
    if (frame >= 0 && frame < m_state.names.size() && !m_state.names[frame].isEmpty()) {
        return m_state.names[frame];
    }
    return QString::number(frame + 1);
}

void TimelineStrip::relayout()
{
    m_cells.clear();
    const int count = std::max(1, m_state.frameCount);
    int offset = 0;
    if (m_vertical) {
        // Cells scale with the column's width; the key cell keeps 126:74 and
        // the hold sliver keeps its fifth of the key.
        const int rowWidth = std::max(1, width() - kScrollThickness);
        const qreal scale = qreal(rowWidth) / qreal(kStripColumnWidth);
        m_cellThickness = rowWidth;
        m_keyExtent = std::max(16, int(std::lround(kKeyCellHeight * scale)));
        m_holdExtent = std::max(5, int(std::lround(m_keyExtent * kHoldFraction)));
        const int top = cellOrigin();
        for (int frame = 0; frame < count; ++frame) {
            const bool hold = frame < m_state.holds.size() && m_state.holds[frame];
            const int rowHeight = hold ? m_holdExtent : m_keyExtent;
            Cell cell;
            cell.frame = frame;
            cell.hold = hold;
            cell.rect = QRect(0, top + offset - m_scroll, rowWidth, rowHeight);
            cell.lane = QRect(rowWidth - kLaneThickness - 2, cell.rect.y(), kLaneThickness,
                              rowHeight);
            m_cells.append(cell);
            offset += rowHeight;
        }
    } else {
        const int cellHeight = std::max(1, height() - kScrollThickness);
        const qreal scale = qreal(cellHeight) / qreal(kKeyCellHeight);
        m_cellThickness = cellHeight;
        m_keyExtent = std::max(28, int(std::lround(kKeyCellWidth * scale)));
        m_holdExtent = std::max(8, int(std::lround(m_keyExtent * kHoldFraction)));
        for (int frame = 0; frame < count; ++frame) {
            const bool hold = frame < m_state.holds.size() && m_state.holds[frame];
            const int cellWidth = hold ? m_holdExtent : m_keyExtent;
            Cell cell;
            cell.frame = frame;
            cell.hold = hold;
            cell.rect = QRect(offset - m_scroll, 0, cellWidth, cellHeight);
            cell.lane = QRect(cell.rect.x(), 0, cellWidth, kLaneThickness);
            m_cells.append(cell);
            offset += cellWidth;
        }
    }
    m_extent = offset;
    clampScroll();
}

void TimelineStrip::clampScroll()
{
    const int viewport = m_vertical ? height() - cellOrigin() : width();
    const int maximum = std::max(0, m_extent - std::max(0, viewport));
    const int clamped = std::min(std::max(0, m_scroll), maximum);
    if (clamped != m_scroll) {
        const int delta = m_scroll - clamped;
        m_scroll = clamped;
        for (Cell &cell : m_cells) {
            if (m_vertical) {
                cell.rect.moveTop(cell.rect.y() + delta);
                cell.lane.moveTop(cell.lane.y() + delta);
            } else {
                cell.rect.moveLeft(cell.rect.x() + delta);
                cell.lane.moveLeft(cell.lane.x() + delta);
            }
        }
    }
}

void TimelineStrip::ensureCurrentVisible()
{
    // A drag is the one time the run must hold still: the pointer is measured
    // against the cells it was pressed on, so moving them under it would drop
    // the frame somewhere the user never aimed at.
    if (m_dragging) {
        return;
    }
    if (m_state.currentFrame < 0 || m_state.currentFrame >= m_cells.size()) {
        return;
    }
    // Same axis split as clampScroll: the scroll bar is drawn across the OTHER
    // edge, so it never shortens the run's own axis.
    const int viewport = m_vertical ? height() - cellOrigin() : width();
    if (viewport <= 0) {
        return;
    }
    const int origin = cellOrigin();
    const QRect &cell = m_cells[m_state.currentFrame].rect;
    const int cellNear = m_vertical ? cell.top() : cell.left();
    const int cellFar = m_vertical ? cell.bottom() : cell.right();
    int delta = 0;
    if (cellNear - kScrollIntoViewMargin < origin) {
        delta = cellNear - kScrollIntoViewMargin - origin;
    } else if (cellFar + kScrollIntoViewMargin > origin + viewport - 1) {
        delta = cellFar + kScrollIntoViewMargin - (origin + viewport - 1);
    }
    if (delta == 0) {
        return;
    }
    m_scroll += delta;
    // relayout re-places every cell from the new offset and clamps it, so a
    // margin that ran off either end is absorbed there.
    relayout();
}

QImage TimelineStrip::thumbnail(int frame, const QSize &size)
{
    const auto cached = m_thumbnails.constFind(frame);
    if (cached != m_thumbnails.constEnd() && cached->size() == size) {
        return *cached;
    }
    if (!m_provider) {
        return QImage();
    }
    const QImage rendered = m_provider(frame, size);
    m_thumbnails.insert(frame, rendered);
    return rendered;
}

void TimelineStrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), stripGround());

    if (m_vertical) {
        // The dock's title bar carries the name; what is left for the strip is
        // the frame commands, which no side title bar has room for.
        const QRect commandRow(0, 0, width(), kStripCommandHeight);
        painter.fillRect(commandRow, role(AnimeTheme::Role::Surface));
        painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
        painter.drawLine(0, commandRow.bottom(), width(), commandRow.bottom());
        for (int i = 0; i < kStripCommandCount; ++i) {
            const QRect box = commandRect(i);
            const bool hover = (i == m_hoverCommand);
            drawChromeCell(painter, box, hover, false, true);
            drawGlyph(painter, box, glyphFor(kFrameCommands[i], m_state),
                      chromeForeground(hover, false, true), kGlyphPx);
        }
        painter.setClipRect(QRect(0, cellOrigin(), width(), height() - cellOrigin()));
    }

    const int current = m_state.currentFrame;
    int firstPast = std::numeric_limits<int>::max();
    int lastAhead = std::numeric_limits<int>::min();
    // The lane set belongs to the main board. While the strip is showing
    // another document the sentinels stay untouched, so no run is drawn - the
    // dots would otherwise describe main-board frames over these cells.
    if (m_state.onionAvailable) {
        for (int frame : m_state.lanes) {
            if (frame < current) {
                firstPast = std::min(firstPast, frame);
            } else if (frame > current) {
                lastAhead = std::max(lastAhead, frame);
            }
        }
    }

    for (const Cell &cell : m_cells) {
        if (!cell.rect.intersects(rect())) {
            continue;
        }
        // A hold re-exposes the row above, so it gets no preview and no
        // number: what it shows is the key frame's drawing, not its own.
        const QColor ground = cell.hold ? mix(stripGround(), role(AnimeTheme::Role::Text), 0.10)
                                        : stripGround();
        painter.fillRect(cell.rect, ground);
        painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
        if (m_vertical) {
            painter.drawLine(cell.rect.left(), cell.rect.bottom(), cell.rect.right(),
                             cell.rect.bottom());
        } else {
            painter.drawLine(cell.rect.right(), cell.rect.top(), cell.rect.right(),
                             cell.rect.bottom());
        }

        if (!cell.hold) {
            // The preview is the PAGE: 4:3 whatever shape the cell took when
            // the dock was resized.
            int previewHeight = m_vertical ? cell.rect.height() - 8 : cell.rect.height();
            const int previewRoom = m_vertical ? cell.rect.width() - kLaneThickness - 2 - 4
                                               : cell.rect.width() - 4;
            int previewWidth = std::min(previewRoom, int(std::lround(previewHeight * kPreviewAspect)));
            if (previewWidth > 0 && previewHeight > 0) {
                previewHeight = std::min(previewHeight,
                                         int(std::lround(previewWidth / kPreviewAspect)));
                QRect preview;
                if (m_vertical) {
                    preview = QRect(cell.rect.x() + (cell.rect.width() - kLaneThickness - 2
                                                     - previewWidth) / 2,
                                    cell.rect.y() + 4, previewWidth, previewHeight);
                } else {
                    preview = QRect(cell.rect.x() + (cell.rect.width() - previewWidth) / 2,
                                    cell.rect.y() + (cell.rect.height() - previewHeight) / 2,
                                    previewWidth, previewHeight);
                }
                painter.fillRect(preview, Qt::white);
                const QImage image = thumbnail(cell.frame, preview.size());
                if (!image.isNull()) {
                    painter.drawImage(preview.topLeft(), image);
                }
                painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(preview.adjusted(0, 0, -1, -1));

                // The name is printed ON the drawing, the way a sheet is
                // numbered in the corner rather than in a column beside it.
                const bool currentCell = cell.frame == current;
                const QString name = nameFor(cell.frame);
                painter.setFont(monoFont(10, true));
                const QFontMetrics metrics(painter.font());
                // Under the lane, not behind it: the band is a hit target, and
                // a name printed inside it would be a click that misses.
                const int chipWidth = std::min(preview.width() - 4,
                                               metrics.horizontalAdvance(name) + 10);
                const QRect chip(preview.x() + 2,
                                 preview.y() + (m_vertical ? 1 : kLaneThickness + 1),
                                 std::max(12, chipWidth), 13);
                painter.fillRect(chip, currentCell ? role(AnimeTheme::Role::Accent)
                                                   : role(AnimeTheme::Role::Surface));
                painter.setPen(currentCell ? QColor(Qt::white) : role(AnimeTheme::Role::TextDim));
                painter.drawText(chip, Qt::AlignCenter,
                                 metrics.elidedText(name, Qt::ElideRight, chip.width() - 4));
            }
        } else {
            // A hold is MARKED, not drawn: the square says "the row above's
            // drawing again" without pretending to be a second frame.
            painter.setPen(QPen(role(AnimeTheme::Role::TextDim), 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(cell.rect.center().x() - 4, cell.rect.center().y() - 4, 8, 8));
        }

        // The lane: a line while the ghost run passes through, a dot where the
        // run is actually anchored. It stops at the playhead because a ghost
        // of the frame you are drawing is the frame you are drawing.
        QColor lineColor;
        if (cell.frame < current && cell.frame >= firstPast) {
            lineColor = role(AnimeTheme::Role::OnionPast);
        } else if (cell.frame > current && cell.frame <= lastAhead) {
            lineColor = role(AnimeTheme::Role::OnionAhead);
        }
        if (lineColor.isValid()) {
            QColor faded = lineColor;
            faded.setAlpha(170);
            painter.setPen(QPen(faded, 1.0));
            if (m_vertical) {
                painter.drawLine(cell.lane.center().x(), cell.lane.top(), cell.lane.center().x(),
                                 cell.lane.bottom());
            } else {
                painter.drawLine(cell.lane.left(), cell.lane.center().y(), cell.lane.right(),
                                 cell.lane.center().y());
            }
        }
        if (!cell.hold && m_state.onionAvailable && m_state.lanes.contains(cell.frame)) {
            QColor dot = cell.frame < current ? role(AnimeTheme::Role::OnionPast)
                       : cell.frame > current ? role(AnimeTheme::Role::OnionAhead)
                                              : role(AnimeTheme::Role::TextDim);
            painter.setPen(Qt::NoPen);
            painter.setBrush(dot);
            painter.drawEllipse(QPointF(cell.lane.center()) + QPointF(0.5, 0.5), kLaneDot / 2.0,
                                kLaneDot / 2.0);
            painter.setBrush(Qt::NoBrush);
        }

        if (cell.frame == current) {
            painter.setPen(QPen(role(AnimeTheme::Role::Accent), 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(cell.rect).adjusted(1.0, 1.0, -1.0, -1.0));
        }
    }
    painter.setClipping(false);

    // The scroll bar is drawn, not a widget: it is a read-out of how much of
    // the run is off screen and never takes a click away from a cell.
    const int viewport = m_vertical ? height() - cellOrigin() : width();
    if (m_extent > viewport && viewport > 0) {
        const qreal fraction = qreal(viewport) / qreal(m_extent);
        const qreal position = qreal(m_scroll) / qreal(m_extent);
        const QColor track = mix(stripGround(), role(AnimeTheme::Role::Text), 0.12);
        if (m_vertical) {
            const QRect bar(width() - kScrollThickness, cellOrigin(), kScrollThickness, viewport);
            painter.fillRect(bar, track);
            painter.fillRect(QRect(bar.x(), bar.y() + int(position * viewport), kScrollThickness,
                                   std::max(12, int(fraction * viewport))),
                             role(AnimeTheme::Role::TextDim));
        } else {
            const QRect bar(0, height() - kScrollThickness, width(), kScrollThickness);
            painter.fillRect(bar, track);
            painter.fillRect(QRect(bar.x() + int(position * viewport), bar.y(),
                                   std::max(12, int(fraction * viewport)), kScrollThickness),
                             role(AnimeTheme::Role::TextDim));
        }
    }
}

int TimelineStrip::cellAt(const QPoint &pos) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

int TimelineStrip::dropTargetAt(const QPoint &pos) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        const QRect &r = m_cells[i].rect;
        if (m_vertical ? pos.y() < r.center().y() : pos.x() < r.center().x()) {
            return i;
        }
    }
    // Past the last centre is a real slot of its own - the end of the run.
    // Answering size()-1 here would make "drop at the end" indistinguishable
    // from "drop before the last cell" once the caller converts to a
    // post-removal index.
    return int(m_cells.size());
}

void TimelineStrip::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y()
                                                   : event->angleDelta().x();
    m_scroll -= delta / 2;
    relayout();
    update();
    event->accept();
}

void TimelineStrip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressPos = event->pos();
    m_dragging = false;
    m_pressedCell = -1;
    m_pressedCommand = -1;

    if (m_vertical && event->pos().y() < kStripCommandHeight) {
        for (int i = 0; i < kStripCommandCount; ++i) {
            if (commandRect(i).contains(event->pos())) {
                m_pressedCommand = i;
                update();
                return;
            }
        }
        return;
    }

    m_pressedCell = cellAt(event->pos());
}

void TimelineStrip::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressedCell >= 0 && !m_dragging
        && (event->pos() - m_pressPos).manhattanLength() >= 6) {
        m_dragging = true;
    }
    int hover = -1;
    if (m_vertical) {
        for (int i = 0; i < kStripCommandCount; ++i) {
            if (commandRect(i).contains(event->pos())) {
                hover = i;
            }
        }
    }
    if (hover != m_hoverCommand) {
        m_hoverCommand = hover;
        update();
    }
    if (hover >= 0) {
        setToolTip(tooltipFor(kFrameCommands[hover], m_state));
        return;
    }
    const int cellIndex = cellAt(event->pos());
    if (cellIndex < 0) {
        setToolTip(QString());
        return;
    }
    const Cell &cell = m_cells[cellIndex];
    if (cell.hold) {
        setToolTip(QStringLiteral("Held: shows the same drawing as frame %1. "
                                  "Editing either one changes both.")
                       .arg(nameFor(cell.frame - 1)));
    } else if (m_state.onionAvailable && cell.lane.contains(event->pos())) {
        setToolTip(m_state.lanes.contains(cell.frame)
                       ? QStringLiteral("Remove frame %1 from the onion skin")
                             .arg(nameFor(cell.frame))
                       : QStringLiteral("Ghost frame %1 under the current one")
                             .arg(nameFor(cell.frame)));
    } else {
        setToolTip(QStringLiteral("Frame %1").arg(nameFor(cell.frame)));
    }
}

void TimelineStrip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const int command = m_pressedCommand;
    const int cellIndex = m_pressedCell;
    const bool dragged = m_dragging;
    m_pressedCommand = -1;
    m_pressedCell = -1;
    m_dragging = false;

    if (command >= 0) {
        if (command < kStripCommandCount) {
            emit commandTriggered(kFrameCommands[command]);
        }
        update();
        return;
    }
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return;
    }
    const Cell cell = m_cells[cellIndex];
    if (dragged) {
        // dropTargetAt answers in PRE-removal slots (the gap the pointer is
        // in, with the dragged cell still occupying its own). The model moves
        // by takeAt+insert, so a rightward move has already lost that slot by
        // the time the index is used - hence the -1, and hence the no-op test
        // AFTER it rather than before.
        int target = dropTargetAt(event->pos());
        if (cell.frame < target) {
            --target;
        }
        if (target != cell.frame) {
            emit moveFrameRequested(cell.frame, target);
        }
        return;
    }
    if (!cell.hold && m_state.onionAvailable && cell.lane.contains(event->pos())) {
        emit laneToggled(cell.frame, !m_state.lanes.contains(cell.frame));
        return;
    }
    emit frameActivated(cell.frame);
}

void TimelineStrip::leaveEvent(QEvent *event)
{
    if (m_hoverCommand != -1) {
        m_hoverCommand = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

// -------------------------------------------------------- side title bar

TimelineSideTitleBar::TimelineSideTitleBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(kSideTitleHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

QSize TimelineSideTitleBar::sizeHint() const
{
    const QFontMetrics metrics(labelFont(10));
    return QSize(metrics.horizontalAdvance(QStringLiteral("ANIMATION")) + 48, kSideTitleHeight);
}

QRect TimelineSideTitleBar::closeRect() const
{
    return QRect(width() - kSideTitleHeight, 1, kSideTitleHeight - 2, kSideTitleHeight - 2);
}

void TimelineSideTitleBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
    painter.drawLine(0, height() - 1, width(), height() - 1);
    painter.setPen(role(AnimeTheme::Role::TextDim));
    painter.setFont(labelFont(10));
    painter.drawText(rect().adjusted(8, 0, -(kSideTitleHeight + 4), 0),
                     Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("ANIMATION"));
    const QRect close = closeRect();
    drawChromeCell(painter, close, m_hoverClose, false, true);
    drawGlyph(painter, close, Glyph::Close, chromeForeground(m_hoverClose, false, true), 13);
}

void TimelineSideTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && closeRect().contains(event->position().toPoint())) {
        m_pressedClose = true;
        return;
    }
    // Everything else is the dock's grip: ignoring hands the press to the
    // QDockWidget, which drags with the native preview.
    m_pressedClose = false;
    event->ignore();
}

void TimelineSideTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_pressedClose) {
        event->ignore();
        return;
    }
    m_pressedClose = false;
    if (event->button() == Qt::LeftButton && closeRect().contains(event->position().toPoint())) {
        emit commandTriggered(TimelineCommand::Close);
    }
}

void TimelineSideTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    const bool hover = closeRect().contains(event->position().toPoint());
    if (hover != m_hoverClose) {
        m_hoverClose = hover;
        setToolTip(hover ? tooltipFor(TimelineCommand::Close, TimelineState()) : QString());
        update();
    }
    if ((event->buttons() & Qt::LeftButton) && !m_pressedClose) {
        event->ignore();
    }
}

void TimelineSideTitleBar::leaveEvent(QEvent *event)
{
    if (m_hoverClose) {
        m_hoverClose = false;
        update();
    }
    QWidget::leaveEvent(event);
}

// ------------------------------------------------------------- reopen pill

TimelineReopenPill::TimelineReopenPill(QWidget *parent)
    : QWidget(parent)
{
    setToolTip(QStringLiteral("Show the timeline again"));
    setCursor(Qt::PointingHandCursor);
    hide();
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

QSize TimelineReopenPill::sizeHint() const
{
    const QFontMetrics metrics(labelFont(10));
    return QSize(metrics.horizontalAdvance(QStringLiteral("TIMELINE")) + 40, 24);
}

void TimelineReopenPill::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_hover ? accentWash() : role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    const QColor foreground = m_hover ? role(AnimeTheme::Role::Text)
                                      : role(AnimeTheme::Role::TextDim);
    drawGlyph(painter, QRect(8, 0, 12, height()), Glyph::PillUp, foreground, 12);
    painter.setPen(foreground);
    painter.setFont(labelFont(10));
    painter.drawText(QRect(24, 0, width() - 32, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("TIMELINE"));
}

void TimelineReopenPill::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void TimelineReopenPill::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
}

void TimelineReopenPill::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void TimelineReopenPill::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}

// ----------------------------------------------------------- the timeline

TimelineWindow::TimelineWindow(PaintViewContainer *container, QMainWindow *mainWindow)
    : QDockWidget(QStringLiteral("Timeline"), mainWindow)
    , m_container(container)
    , m_mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("TimelineWindow"));
    // No top area: the transport is a title bar, and a timeline over the menu
    // bar would put the playback controls where the document tools belong.
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea
                    | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);

    m_bar = new TimelineTransportBar(this);
    m_sideTitle = new TimelineSideTitleBar(this);
    m_sideTitle->hide();
    m_strip = new TimelineStrip(this);
    setWidget(m_strip);
    setTitleBarWidget(m_bar);

    m_pill = new TimelineReopenPill(container ? container->canvasArea() : nullptr);
    if (container) {
        container->canvasArea()->installEventFilter(this);
    }

    connect(m_bar, &TimelineTransportBar::commandTriggered,
            this, &TimelineWindow::handleCommand);
    connect(m_strip, &TimelineStrip::commandTriggered, this, &TimelineWindow::handleCommand);
    connect(m_sideTitle, &TimelineSideTitleBar::commandTriggered,
            this, &TimelineWindow::handleCommand);
    connect(m_bar, &TimelineTransportBar::fpsPicked, this, [this](int fps) {
        emit fpsChanged(fps);
    });
    connect(m_bar, &TimelineTransportBar::frameTyped, this, [this](int frame) {
        emit frameActivated(std::min(std::max(0, frame), std::max(0, m_state.frameCount - 1)));
    });
    connect(m_strip, &TimelineStrip::frameActivated, this, &TimelineWindow::frameActivated);
    connect(m_strip, &TimelineStrip::moveFrameRequested, this, &TimelineWindow::moveFrameRequested);
    connect(m_strip, &TimelineStrip::laneToggled, this, &TimelineWindow::onionLaneToggled);
    connect(m_pill, &TimelineReopenPill::clicked, this, [this]() {
        show();
        raise();
    });

    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea area) {
        m_area = area;
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            m_sideArea = area;
        }
        applyLayout();
        saveSettings();
    });
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool) {
        applyLayout();
        saveSettings();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // In the vertical layout the transport is NOT inside the dock, so
        // closing the dock has to take it down by hand.
        if (m_bar && m_bar->parentWidget() != this) {
            m_bar->setVisible(visible);
        }
        if (m_pill) {
            m_pill->setVisible(!visible);
            positionPill();
        }
        saveSettings();
    });

    loadSettings();
    applyLayout();
}

void TimelineWindow::restoreLayout()
{
    m_restoring = true;
    if (m_mainWindow) {
        // Re-dock even when the area is unchanged: this is what makes the
        // stored area authoritative over wherever the owner parked it.
        m_mainWindow->addDockWidget(m_restoreArea, this);
    } else {
        m_area = m_restoreArea;
    }
    if (m_restoreFloating) {
        setFloating(true);
        if (m_floatGeometry.isValid()) {
            setGeometry(m_floatGeometry);
        }
    }
    applyLayout();
    setVisible(m_restoreVisible);
    // Driven by hand rather than through visibilityChanged: the main window has
    // not been shown yet, so this setVisible takes Qt's deferred path and no
    // show/hide event - and therefore no signal - is ever delivered. Without
    // this a timeline restored CLOSED leaves no reopen chip on the canvas.
    if (m_bar && m_bar->parentWidget() != this) {
        m_bar->setVisible(!isHidden());
    }
    if (m_pill) {
        m_pill->setVisible(isHidden());
        positionPill();
    }
    m_restoring = false;
    // No resizeDocks here on purpose: a dock joining the layout for the first
    // time is given its size hint, which is already title + strip (or title
    // alone when the strip starts collapsed).
}

void TimelineWindow::setFrameData(int frameCount, int currentFrame, const QVector<bool> &holds)
{
    m_state.frameCount = std::max(1, frameCount);
    m_state.currentFrame = std::min(std::max(0, currentFrame), m_state.frameCount - 1);
    m_state.holds = holds;
    pushState();
}

void TimelineWindow::setFrameNames(const QVector<QString> &names)
{
    if (m_state.names == names) {
        return;
    }
    m_state.names = names;
    pushState();
}

void TimelineWindow::setPlaybackActive(bool active)
{
    if (m_state.playing == active) {
        return;
    }
    m_state.playing = active;
    pushState();
}

void TimelineWindow::setFps(int fps)
{
    if (m_state.fps == fps) {
        return;
    }
    m_state.fps = fps;
    pushState();
}

void TimelineWindow::setLoop(bool loop)
{
    if (m_state.loop == loop) {
        return;
    }
    m_state.loop = loop;
    pushState();
}

void TimelineWindow::setOnionState(bool enabled, bool guides, const QSet<int> &lanes)
{
    m_state.onion = enabled;
    m_state.guideLines = guides;
    m_state.lanes = lanes;
    pushState();
}

void TimelineWindow::setOnionAvailable(bool available)
{
    if (m_state.onionAvailable == available) {
        return;
    }
    m_state.onionAvailable = available;
    pushState();
}

void TimelineWindow::setThumbnailProvider(std::function<QImage(int, QSize)> provider)
{
    m_strip->setThumbnailProvider(std::move(provider));
}

void TimelineWindow::clearThumbnails()
{
    m_strip->clearThumbnails();
}

Qt::DockWidgetArea TimelineWindow::sideArea() const
{
    return m_sideArea == Qt::LeftDockWidgetArea ? Qt::LeftDockWidgetArea
                                                : Qt::RightDockWidgetArea;
}

void TimelineWindow::handleCommand(TimelineCommand command)
{
    switch (command) {
    case TimelineCommand::AddFrame:
        emit addFrameRequested();
        break;
    case TimelineCommand::AddHold:
        emit addHoldRequested();
        break;
    case TimelineCommand::Duplicate:
        emit duplicateFrameRequested();
        break;
    case TimelineCommand::DeleteFrame:
        emit deleteFrameRequested();
        break;
    case TimelineCommand::Onion:
        if (m_state.onionAvailable) {
            emit onionToggled(!m_state.onion);
        }
        break;
    case TimelineCommand::GuideLines:
        if (m_state.onionAvailable && m_state.onion) {
            emit onionGuideToggled(!m_state.guideLines);
        }
        break;
    case TimelineCommand::Prev:
        emit prevRequested();
        break;
    case TimelineCommand::Next:
        emit nextRequested();
        break;
    case TimelineCommand::Play:
        emit playRequested();
        break;
    case TimelineCommand::Pause:
        emit pauseRequested();
        break;
    case TimelineCommand::Loop:
        m_state.loop = !m_state.loop;
        pushState();
        emit loopToggled(m_state.loop);
        break;
    case TimelineCommand::Rate:
        break;   // the bar opens its own menu and reports the pick
    case TimelineCommand::Orientation: {
        // Re-docking IS the orientation switch: the area decides which way the
        // strip runs, so there is no second source of truth to keep in step.
        const Qt::DockWidgetArea target = m_state.vertical ? Qt::BottomDockWidgetArea
                                                           : sideArea();
        m_area = target;
        // The remembered extent measured the OTHER axis; carrying it over
        // would make a tall strip into a wide column.
        m_expandedExtent = 0;
        if (m_mainWindow) {
            m_mainWindow->addDockWidget(target, this);
        }
        applyLayout();
        applyDockExtent();
        saveSettings();
        break;
    }
    case TimelineCommand::Float:
        setFloating(!isFloating());
        saveSettings();
        break;
    case TimelineCommand::Collapse:
        if (!m_state.collapsed) {
            // Remember what the user had sized the strip to; the expand below
            // is otherwise a reset to the default extent.
            m_expandedExtent = m_state.vertical ? width() : height();
        }
        m_state.collapsed = !m_state.collapsed;
        applyLayout();
        applyDockExtent();
        saveSettings();
        break;
    case TimelineCommand::Close:
        hide();
        break;
    }
}

void TimelineWindow::applyLayout()
{
    if (m_applyingLayout) {
        return;
    }
    m_applyingLayout = true;

    const bool floating = isFloating();
    const bool vertical = !floating
                          && (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea);
    m_state.vertical = vertical;
    m_state.floating = floating;
    m_state.leftAligned = (m_area == Qt::LeftDockWidgetArea);
    // A floating timeline has nothing to collapse INTO - the window would be a
    // bare title bar - so the flag only applies while docked.
    const bool stripVisible = !(m_state.collapsed && !floating);

    m_strip->setVertical(vertical);
    if (vertical) {
        // The transport's assembly is far wider than a side column, so it stays
        // under the canvas and the dock gets a slim named bar instead. The
        // title role has to be handed over BEFORE the transport is re-homed:
        // a widget cannot be in the dock's title layout and the container's
        // bottom row at once.
        if (titleBarWidget() != m_sideTitle) {
            setTitleBarWidget(m_sideTitle);
        }
        m_sideTitle->show();
        if (m_container) {
            m_container->setBottomChrome(m_bar);
        }
        // isHidden, not isVisible: at restore time no ancestor is on screen
        // yet, and the question is whether the dock was CLOSED.
        m_bar->setVisible(!isHidden());
    } else {
        if (m_container) {
            m_container->setBottomChrome(nullptr);
        }
        if (titleBarWidget() != m_bar) {
            setTitleBarWidget(m_bar);
        }
        m_sideTitle->hide();
        m_bar->show();
    }
    m_strip->setVisible(stripVisible);

    m_applyingLayout = false;
    pushState();
}

void TimelineWindow::applyDockExtent()
{
    if (!m_mainWindow || isFloating() || isHidden()) {
        return;
    }
    // Only ever called on a MODE change (dock area, collapse, restore): at any
    // other moment the dock's size is whatever the user dragged it to.
    if (m_state.vertical) {
        int width = kCollapsedColumnWidth;
        if (!m_state.collapsed) {
            width = std::max({kCollapsedColumnWidth, m_strip->sizeHint().width(),
                              m_expandedExtent});
        }
        m_mainWindow->resizeDocks({this}, {width}, Qt::Horizontal);
    } else {
        const int title = m_bar->sizeHint().height();
        int height = title;
        if (!m_state.collapsed) {
            height = std::max(title + m_strip->sizeHint().height(), m_expandedExtent);
        }
        m_mainWindow->resizeDocks({this}, {height}, Qt::Vertical);
    }
}

void TimelineWindow::pushState()
{
    m_bar->setState(m_state);
    m_strip->setState(m_state);
}

void TimelineWindow::positionPill()
{
    if (!m_container || !m_pill) {
        return;
    }
    QWidget *host = m_container->canvasArea();
    const QSize hint = m_pill->sizeHint();
    m_pill->resize(hint);
    m_pill->move((host->width() - hint.width()) / 2, host->height() - hint.height() - 10);
    m_pill->raise();
}

bool TimelineWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_container && watched == m_container->canvasArea() && event->type() == QEvent::Resize) {
        positionPill();
    }
    return QDockWidget::eventFilter(watched, event);
}

void TimelineWindow::moveEvent(QMoveEvent *event)
{
    QDockWidget::moveEvent(event);
    rememberFloatGeometry();
}

void TimelineWindow::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    rememberFloatGeometry();
}

void TimelineWindow::rememberFloatGeometry()
{
    if (m_restoring || !isFloating()) {
        return;
    }
    m_floatGeometry = geometry();
    if (m_floatGeometryQueued) {
        return;
    }
    m_floatGeometryQueued = true;
    // One write per gesture rather than one per mouse step; the rect above is
    // already current, so a settle that lands mid-drag still records what the
    // dock is at when it fires.
    QTimer::singleShot(400, this, [this]() {
        m_floatGeometryQueued = false;
        if (isFloating()) {
            m_floatGeometry = geometry();
            saveSettings();
        }
    });
}

void TimelineWindow::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() != QEvent::EnabledChange) {
        return;
    }
    // In the vertical layout the transport lives in the canvas container and
    // the pill always does, so Qt's own enable propagation stops before them.
    // A freeze that reached the dock would otherwise leave the buttons that
    // mutate the document the script is working on fully live.
    const bool on = isEnabled();
    for (QWidget *piece : {static_cast<QWidget *>(m_bar), static_cast<QWidget *>(m_pill)}) {
        if (piece && !isAncestorOf(piece)) {
            piece->setEnabled(on);
        }
    }
}

void TimelineWindow::loadSettings()
{
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));
    // Round-1 stored a layout enum and an edge flag; the dock area says both,
    // so the old keys are dropped rather than migrated.
    settings.remove(QStringLiteral("layout"));
    settings.remove(QStringLiteral("leftAligned"));
    const QString area = settings.value(QStringLiteral("dockArea"),
                                        QStringLiteral("bottom")).toString();
    if (area == QStringLiteral("left")) {
        m_restoreArea = Qt::LeftDockWidgetArea;
        m_sideArea = Qt::LeftDockWidgetArea;
    } else if (area == QStringLiteral("right")) {
        m_restoreArea = Qt::RightDockWidgetArea;
        m_sideArea = Qt::RightDockWidgetArea;
    } else {
        m_restoreArea = Qt::BottomDockWidgetArea;
    }
    m_state.collapsed = settings.value(QStringLiteral("collapsed"), false).toBool();
    m_restoreFloating = settings.value(QStringLiteral("floating"), false).toBool();
    m_restoreVisible = settings.value(QStringLiteral("visible"), true).toBool();
    m_floatGeometry = settings.value(QStringLiteral("floatGeometry"), QRect()).toRect();
    settings.endGroup();
}

void TimelineWindow::saveSettings()
{
    if (m_restoring) {
        return;
    }
    if (m_mainWindow && !m_mainWindow->isVisible()) {
        // Startup and shutdown both hide every dock; writing then would record
        // a state the user never asked for.
        return;
    }
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));
    settings.setValue(QStringLiteral("dockArea"),
                      m_area == Qt::LeftDockWidgetArea    ? QStringLiteral("left")
                      : m_area == Qt::RightDockWidgetArea ? QStringLiteral("right")
                                                          : QStringLiteral("bottom"));
    settings.setValue(QStringLiteral("floating"), isFloating());
    settings.setValue(QStringLiteral("collapsed"), m_state.collapsed);
    settings.setValue(QStringLiteral("visible"), !isHidden());
    if (isFloating()) {
        settings.setValue(QStringLiteral("floatGeometry"), geometry());
    }
    settings.endGroup();
}

int TimelineWindow::fpsForText(const QString &text, int fallback)
{
    const QString trimmed = text.trimmed();
    for (int i = 0; i < kCadenceCount; ++i) {
        if (trimmed == QString::fromUtf8(kCadences[i].title)
            || trimmed == QString::fromUtf8(kCadences[i].shortTitle)) {
            return kCadences[i].fps;
        }
    }
    static const QRegularExpression numeric(
        QStringLiteral("^(\\d{1,3})\\s*(?:fps)?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = numeric.match(trimmed);
    if (match.hasMatch()) {
        const int typed = match.captured(1).toInt();
        if (typed >= 1 && typed <= 120) {
            return typed;
        }
    }
    return fallback;
}

QString TimelineWindow::textForFps(int fps)
{
    for (int i = 0; i < kCadenceCount; ++i) {
        if (kCadences[i].fps == fps) {
            return QStringLiteral("%1  (%2 fps)").arg(QString::fromUtf8(kCadences[i].title))
                       .arg(fps);
        }
    }
    return QStringLiteral("%1 fps").arg(fps);
}

QString TimelineWindow::shortTextForFps(int fps)
{
    for (int i = 0; i < kCadenceCount; ++i) {
        if (kCadences[i].fps == fps) {
            return QString::fromUtf8(kCadences[i].shortTitle);
        }
    }
    return QStringLiteral("%1 fps").arg(fps);
}
