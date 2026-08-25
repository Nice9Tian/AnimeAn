#include "timelinewidget.h"
#include "../paintviewcontainer.h"
#include "../theme.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
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
constexpr int kKeyCellWidth = 126;
constexpr int kKeyCellHeight = 74;
constexpr int kPreviewWidth = 98;
constexpr int kHoldCellWidth = 25;
constexpr int kHoldRowHeight = 15;
constexpr int kLaneThickness = 12;
constexpr int kLaneDot = 9;
constexpr int kScrollThickness = 6;
constexpr int kStripColumnWidth = 126;
constexpr int kStripHeaderHeight = 32;
constexpr int kStripCommandHeight = 28;
constexpr int kStripCommandWidth = 30;
constexpr int kFloatTitleHeight = 24;
constexpr int kFloatPanelWidth = 748;
// How near an edge a dragged panel snaps back, and how far from every edge a
// dragged strip has to be before it detaches. The two differ on purpose: a
// snap that reached as far as the detach threshold would make a panel
// impossible to park along the side of the canvas.
constexpr int kSnapDistance = 40;
constexpr int kDetachDistance = 80;

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
        return QStringLiteral("Add frame");
    case TimelineCommand::AddHold:
        return QStringLiteral("Add hold frame - it shows the same drawing; "
                              "editing either one changes both");
    case TimelineCommand::DeleteFrame:
        return QStringLiteral("Delete frame");
    case TimelineCommand::Onion:
        return QStringLiteral("Onion skin");
    case TimelineCommand::GuideLines:
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
        return state.vertical ? QStringLiteral("Horizontal strip layout")
                              : QStringLiteral("Vertical strip layout");
    case TimelineCommand::Float:
        return QStringLiteral("Float the strip as a window");
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
    return QSize(320, kBarHeight);
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
    const int h = height();
    const bool chrome = !m_state.collapsed;
    const QFontMetrics guideMetrics(labelFont(10));
    const int guideWidth = guideMetrics.horizontalAdvance(QStringLiteral("GUIDE")) + 14;
    const QFontMetrics rateMetrics(monoFont(11, true));
    const int rateWidth =
        rateMetrics.horizontalAdvance(TimelineWidget::shortTextForFps(m_state.fps)) + 9 + 7 + 11 + 9;

    const auto add = [&](TimelineCommand command, const QRect &rect, bool enabled, bool active) {
        Item item;
        item.command = command;
        item.rect = rect;
        item.enabled = enabled;
        item.active = active;
        m_items.append(item);
    };

    // Left: the frame commands. In the vertical layout the strip's own header
    // carries them, so the bar must not show a second copy.
    int x = 0;
    const bool collapseAtLeft = m_state.vertical && m_state.leftAligned && !m_state.floating;
    if (collapseAtLeft) {
        add(TimelineCommand::Collapse, QRect(x, 0, kButtonWidth, h), true, false);
        x += kButtonWidth;
    }
    if (chrome && !m_state.vertical) {
        for (TimelineCommand command : {TimelineCommand::AddFrame, TimelineCommand::AddHold,
                                        TimelineCommand::DeleteFrame}) {
            add(command, QRect(x, 0, kButtonWidth, h), true, false);
            x += kButtonWidth;
        }
    }

    // Right: window chrome, laid out from the far edge inwards so the close
    // box is always the last thing before the frame.
    int right = width();
    if (!m_state.floating) {
        if (chrome) {
            right -= kButtonWidth;
            add(TimelineCommand::Close, QRect(right, 0, kButtonWidth, h), true, false);
        }
        if (!collapseAtLeft) {
            right -= kButtonWidth;
            add(TimelineCommand::Collapse, QRect(right, 0, kButtonWidth, h), true, false);
        }
        if (chrome) {
            right -= kButtonWidth;
            add(TimelineCommand::Orientation, QRect(right, 0, kButtonWidth, h), true, false);
            right -= kButtonWidth;
            add(TimelineCommand::Float, QRect(right, 0, kButtonWidth, h), true, false);
        }
    }

    // Centre: one assembly, always the same order, centred on the bar rather
    // than on what is left of it - the design's whole point is that the
    // playback controls do not move when the side groups change.
    const int assemblyWidth = kButtonWidth + guideWidth + (1 + 2 * kDividerGap)
                              + kButtonWidth * 3 + kPlayWidth + kButtonWidth
                              + (1 + 2 * kDividerGap) + rateWidth
                              + kFrameFieldWidth + 2 * kFrameFieldGap;
    // Centred, but never underneath the left group: on a narrow bar the
    // assembly slides right rather than overlapping what it would hide.
    int cx = std::max(x, (width() - assemblyWidth) / 2);
    add(TimelineCommand::Onion, QRect(cx, 0, kButtonWidth, h), true, m_state.onion);
    cx += kButtonWidth;
    add(TimelineCommand::GuideLines, QRect(cx, 0, guideWidth, h), m_state.onion,
        m_state.onion && m_state.guideLines);
    cx += guideWidth;
    m_dividers.append(QRect(cx + kDividerGap, (h - 20) / 2, 1, 20));
    cx += 1 + 2 * kDividerGap;
    add(TimelineCommand::Prev, QRect(cx, 0, kButtonWidth, h), true, false);
    cx += kButtonWidth;
    add(TimelineCommand::Pause, QRect(cx, 0, kButtonWidth, h), m_state.playing, false);
    cx += kButtonWidth;
    add(TimelineCommand::Play, QRect(cx, 0, kPlayWidth, h), !m_state.playing, m_state.playing);
    cx += kPlayWidth;
    add(TimelineCommand::Loop, QRect(cx, 0, kButtonWidth, h), true, m_state.loop);
    cx += kButtonWidth;
    add(TimelineCommand::Next, QRect(cx, 0, kButtonWidth, h), true, false);
    cx += kButtonWidth;
    m_dividers.append(QRect(cx + kDividerGap, (h - 20) / 2, 1, 20));
    cx += 1 + 2 * kDividerGap;
    add(TimelineCommand::Rate, QRect(cx, 0, rateWidth, h), true, false);
    cx += rateWidth;
    m_frameField->setGeometry(cx + kFrameFieldGap, (h - kFrameFieldHeight) / 2,
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
                             TimelineWidget::shortTextForFps(m_state.fps));
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
    update();
}

void TimelineTransportBar::mouseReleaseEvent(QMouseEvent *event)
{
    const int pressed = m_pressedItem;
    m_pressedItem = -1;
    if (event->button() != Qt::LeftButton || pressed < 0 || pressed >= m_items.size()) {
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
        const int fps = TimelineWidget::fpsForText(field->text(), m_state.fps);
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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kKeyCellHeight + kScrollThickness);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

QSize TimelineStrip::sizeHint() const
{
    return m_vertical ? QSize(kStripColumnWidth, 240)
                      : QSize(320, kKeyCellHeight + kScrollThickness);
}

void TimelineStrip::setVertical(bool vertical)
{
    if (m_vertical == vertical) {
        return;
    }
    m_vertical = vertical;
    m_scroll = 0;
    // A header drag that ends in a re-orientation has just lost the grip it
    // started on; keeping the flag would let the next mouse move re-decide.
    m_headerDrag = false;
    if (vertical) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(kStripColumnWidth);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
    } else {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kKeyCellHeight + kScrollThickness);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setMinimumWidth(0);
    }
    relayout();
    update();
}

void TimelineStrip::setState(const TimelineState &state)
{
    m_state = state;
    relayout();
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
    relayout();
}

QRect TimelineStrip::commandRect(int index) const
{
    if (!m_vertical || index < 0 || index > 2) {
        return QRect();
    }
    return QRect(index * kStripCommandWidth, kStripHeaderHeight, kStripCommandWidth,
                 kStripCommandHeight);
}

void TimelineStrip::relayout()
{
    m_cells.clear();
    const int count = std::max(1, m_state.frameCount);
    int offset = 0;
    if (m_vertical) {
        const int top = kStripHeaderHeight + kStripCommandHeight;
        const int rowWidth = std::max(0, width() - kScrollThickness);
        for (int frame = 0; frame < count; ++frame) {
            const bool hold = frame < m_state.holds.size() && m_state.holds[frame];
            const int rowHeight = hold ? kHoldRowHeight : kKeyCellHeight;
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
        const int cellHeight = std::max(0, height() - kScrollThickness);
        for (int frame = 0; frame < count; ++frame) {
            const bool hold = frame < m_state.holds.size() && m_state.holds[frame];
            const int cellWidth = hold ? kHoldCellWidth : kKeyCellWidth;
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
    const int viewport = m_vertical
                             ? height() - kStripHeaderHeight - kStripCommandHeight
                             : width();
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
        const QRect header(0, 0, width(), kStripHeaderHeight);
        painter.fillRect(header, role(AnimeTheme::Role::Surface));
        painter.setPen(role(AnimeTheme::Role::TextDim));
        painter.setFont(labelFont(10));
        painter.drawText(header.adjusted(8, 0, -46, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("ANIMATION"));
        const QRect commandRow(0, kStripHeaderHeight, width(), kStripCommandHeight);
        painter.fillRect(commandRow, role(AnimeTheme::Role::Surface));
        painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
        painter.drawLine(0, kStripHeaderHeight, width(), kStripHeaderHeight);
        painter.drawLine(0, commandRow.bottom(), width(), commandRow.bottom());
        const TimelineCommand commands[] = {TimelineCommand::AddFrame, TimelineCommand::AddHold,
                                            TimelineCommand::DeleteFrame};
        for (int i = 0; i < 3; ++i) {
            const QRect box = commandRect(i);
            const bool hover = (i == m_hoverCommand);
            drawChromeCell(painter, box, hover, false, true);
            drawGlyph(painter, box, glyphFor(commands[i], m_state),
                      chromeForeground(hover, false, true), kGlyphPx);
        }
        // Orientation and float live in the header, where the bar's own right
        // group cannot reach when the strip is beside the canvas.
        for (int i = 0; i < 2; ++i) {
            const QRect box(width() - 8 - (2 - i) * 20, 6, 20, 20);
            const bool hover = (m_hoverCommand == 3 + i);
            drawChromeCell(painter, box, hover, false, true);
            drawGlyph(painter, box, i == 0 ? Glyph::ToHorizontal : Glyph::Float,
                      chromeForeground(hover, false, true), 14);
        }
        painter.setClipRect(QRect(0, kStripHeaderHeight + kStripCommandHeight, width(),
                                  height() - kStripHeaderHeight - kStripCommandHeight));
    }

    const int current = m_state.currentFrame;
    int firstPast = std::numeric_limits<int>::max();
    int lastAhead = std::numeric_limits<int>::min();
    for (int frame : m_state.lanes) {
        if (frame < current) {
            firstPast = std::min(firstPast, frame);
        } else if (frame > current) {
            lastAhead = std::max(lastAhead, frame);
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
            const int previewWidth = std::min(kPreviewWidth, cell.rect.width() - 4);
            const int previewHeight = m_vertical ? cell.rect.height() - 8 : cell.rect.height();
            QRect preview;
            if (m_vertical) {
                preview = QRect(cell.rect.x() + (cell.rect.width() - kLaneThickness - 2
                                                 - previewWidth) / 2,
                                cell.rect.y() + 4, previewWidth, previewHeight);
            } else {
                preview = QRect(cell.rect.x() + (cell.rect.width() - previewWidth) / 2,
                                cell.rect.y(), previewWidth, previewHeight);
            }
            painter.fillRect(preview, Qt::white);
            const QImage image = thumbnail(cell.frame, preview.size());
            if (!image.isNull()) {
                painter.drawImage(preview.topLeft(), image);
            }
            painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(preview.adjusted(0, 0, -1, -1));

            // The number is printed ON the drawing, the way a sheet is
            // numbered in the corner rather than in a column beside it.
            const bool currentCell = cell.frame == current;
            const QString number = QString::number(cell.frame + 1);
            painter.setFont(monoFont(10, true));
            const QFontMetrics metrics(painter.font());
            // Under the lane, not behind it: the band is a hit target, and a
            // number printed inside it would be a click that misses.
            const QRect chip(preview.x() + 2,
                             preview.y() + (m_vertical ? 1 : kLaneThickness + 1),
                             metrics.horizontalAdvance(number) + 10, 13);
            painter.fillRect(chip, currentCell ? role(AnimeTheme::Role::Accent)
                                               : role(AnimeTheme::Role::Surface));
            painter.setPen(currentCell ? QColor(Qt::white) : role(AnimeTheme::Role::TextDim));
            painter.drawText(chip, Qt::AlignCenter, number);
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
        if (!cell.hold && m_state.lanes.contains(cell.frame)) {
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
    const int viewport = m_vertical ? height() - kStripHeaderHeight - kStripCommandHeight
                                    : width();
    if (m_extent > viewport && viewport > 0) {
        const qreal fraction = qreal(viewport) / qreal(m_extent);
        const qreal position = qreal(m_scroll) / qreal(m_extent);
        const QColor track = mix(stripGround(), role(AnimeTheme::Role::Text), 0.12);
        if (m_vertical) {
            const QRect bar(width() - kScrollThickness, kStripHeaderHeight + kStripCommandHeight,
                            kScrollThickness, viewport);
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
    return std::max(0, int(m_cells.size()) - 1);
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
    m_headerDrag = false;

    if (m_vertical) {
        if (event->pos().y() < kStripHeaderHeight) {
            for (int i = 0; i < 2; ++i) {
                const QRect box(width() - 8 - (2 - i) * 20, 6, 20, 20);
                if (box.contains(event->pos())) {
                    m_pressedCommand = 3 + i;
                    update();
                    return;
                }
            }
            m_headerDrag = true;
            return;
        }
        if (event->pos().y() < kStripHeaderHeight + kStripCommandHeight) {
            for (int i = 0; i < 3; ++i) {
                if (commandRect(i).contains(event->pos())) {
                    m_pressedCommand = i;
                    update();
                    return;
                }
            }
            return;
        }
    }

    m_pressedCell = cellAt(event->pos());
}

void TimelineStrip::mouseMoveEvent(QMouseEvent *event)
{
    if (m_headerDrag && m_vertical) {
        emit headerDragged(event->globalPosition().toPoint());
        return;
    }
    if (m_pressedCell >= 0 && !m_dragging
        && (event->pos() - m_pressPos).manhattanLength() >= 6) {
        m_dragging = true;
    }
    int hover = -1;
    if (m_vertical) {
        for (int i = 0; i < 3; ++i) {
            if (commandRect(i).contains(event->pos())) {
                hover = i;
            }
        }
        for (int i = 0; i < 2; ++i) {
            const QRect box(width() - 8 - (2 - i) * 20, 6, 20, 20);
            if (box.contains(event->pos())) {
                hover = 3 + i;
            }
        }
    }
    if (hover != m_hoverCommand) {
        m_hoverCommand = hover;
        update();
    }
    if (hover >= 0) {
        static const TimelineCommand kCommands[] = {
            TimelineCommand::AddFrame, TimelineCommand::AddHold, TimelineCommand::DeleteFrame,
            TimelineCommand::Orientation, TimelineCommand::Float};
        setToolTip(tooltipFor(kCommands[hover], m_state));
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
                                  "Editing either one changes both.").arg(cell.frame));
    } else if (cell.lane.contains(event->pos())) {
        setToolTip(m_state.lanes.contains(cell.frame)
                       ? QStringLiteral("Remove frame %1 from the onion skin").arg(cell.frame + 1)
                       : QStringLiteral("Ghost frame %1 under the current one").arg(cell.frame + 1));
    } else {
        setToolTip(QStringLiteral("Frame %1").arg(cell.frame + 1));
    }
}

void TimelineStrip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (m_headerDrag) {
        m_headerDrag = false;
        emit headerReleased();
        return;
    }
    const int command = m_pressedCommand;
    const int cellIndex = m_pressedCell;
    const bool dragged = m_dragging;
    m_pressedCommand = -1;
    m_pressedCell = -1;
    m_dragging = false;

    if (command >= 0) {
        static const TimelineCommand kCommands[] = {
            TimelineCommand::AddFrame, TimelineCommand::AddHold, TimelineCommand::DeleteFrame,
            TimelineCommand::Orientation, TimelineCommand::Float};
        if (command < 5) {
            emit commandTriggered(kCommands[command]);
        }
        update();
        return;
    }
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return;
    }
    const Cell cell = m_cells[cellIndex];
    if (dragged) {
        const int target = dropTargetAt(event->pos());
        if (target != cell.frame) {
            emit moveFrameRequested(cell.frame, target);
        }
        return;
    }
    if (!cell.hold && cell.lane.contains(event->pos())) {
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

// ------------------------------------------------------------ float panel

TimelineFloatPanel::TimelineFloatPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setMouseTracking(true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, kFloatTitleHeight, 1, 1);
    layout->setSpacing(0);
    resize(kFloatPanelWidth, kFloatTitleHeight + kKeyCellHeight + kScrollThickness + kBarHeight);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

void TimelineFloatPanel::setContentWidgets(QWidget *strip, QWidget *bar)
{
    QVBoxLayout *box = qobject_cast<QVBoxLayout *>(layout());
    if (!box) {
        return;
    }
    if (strip) {
        box->addWidget(strip);
    }
    if (bar) {
        box->addWidget(bar);
    }
}

QRect TimelineFloatPanel::closeRect() const
{
    return QRect(width() - 24, 1, 22, 22);
}

void TimelineFloatPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    painter.drawLine(0, kFloatTitleHeight, width(), kFloatTitleHeight);
    painter.setPen(role(AnimeTheme::Role::TextDim));
    painter.setFont(labelFont(10));
    painter.drawText(QRect(9, 0, width() - 40, kFloatTitleHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("TIMELINE"));
    const QRect close = closeRect();
    drawChromeCell(painter, close, m_hoverClose, false, true);
    drawGlyph(painter, close, Glyph::Close, chromeForeground(m_hoverClose, false, true), 13);
}

void TimelineFloatPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->position().y() > kFloatTitleHeight) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (closeRect().contains(event->position().toPoint())) {
        emit closeRequested();
        return;
    }
    m_dragging = true;
    m_grabOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void TimelineFloatPanel::mouseMoveEvent(QMouseEvent *event)
{
    const bool hover = closeRect().contains(event->position().toPoint());
    if (hover != m_hoverClose) {
        m_hoverClose = hover;
        update();
    }
    if (!m_dragging) {
        return;
    }
    move(event->globalPosition().toPoint() - m_grabOffset);
    emit titleDragged(event->globalPosition().toPoint());
}

void TimelineFloatPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        emit titleReleased();
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineFloatPanel::leaveEvent(QEvent *event)
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

TimelineWidget::TimelineWidget(PaintViewContainer *container, QWidget *parent)
    : QWidget(parent)
    , m_container(container)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout *box = new QVBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);

    m_bar = new TimelineTransportBar(this);
    m_strip = new TimelineStrip(this);
    m_panel = new TimelineFloatPanel(container ? container->window() : nullptr);
    m_pill = new TimelineReopenPill(container ? container->canvasArea() : nullptr);
    if (container) {
        container->setBottomChrome(this);
        container->canvasArea()->installEventFilter(this);
    }

    connect(m_bar, &TimelineTransportBar::commandTriggered,
            this, &TimelineWidget::handleCommand);
    connect(m_strip, &TimelineStrip::commandTriggered, this, &TimelineWidget::handleCommand);
    connect(m_bar, &TimelineTransportBar::fpsPicked, this, [this](int fps) {
        emit fpsChanged(fps);
    });
    connect(m_bar, &TimelineTransportBar::frameTyped, this, [this](int frame) {
        emit frameActivated(std::min(std::max(0, frame), std::max(0, m_state.frameCount - 1)));
    });
    connect(m_strip, &TimelineStrip::frameActivated, this, &TimelineWidget::frameActivated);
    connect(m_strip, &TimelineStrip::moveFrameRequested, this, &TimelineWidget::moveFrameRequested);
    connect(m_strip, &TimelineStrip::laneToggled, this, &TimelineWidget::onionLaneToggled);
    connect(m_strip, &TimelineStrip::headerDragged, this, [this](const QPoint &globalPos) {
        bool left = m_leftAligned;
        const Layout target = dropTargetFor(globalPos, &left);
        if (target == m_layout && left == m_leftAligned) {
            return;
        }
        if (target == Layout::Floating) {
            m_panel->move(globalPos - QPoint(m_panel->width() / 2, kFloatTitleHeight / 2));
        }
        m_dockedLayout = target;
        m_layout = target;
        m_leftAligned = left;
        applyLayout();
        saveSettings();
    });
    connect(m_strip, &TimelineStrip::headerReleased, this, [this]() { saveSettings(); });
    connect(m_panel, &TimelineFloatPanel::closeRequested, this, [this]() {
        setTimelineVisible(false);
    });
    connect(m_panel, &TimelineFloatPanel::titleDragged, this, [this](const QPoint &globalPos) {
        bool left = m_leftAligned;
        // A floating panel snaps back from CLOSER in than a docked strip
        // detaches: one threshold for both would make the side of the canvas
        // an impossible place to park the window.
        const Layout target = dropTargetFor(globalPos, &left, kSnapDistance);
        if (target == Layout::Floating) {
            return;
        }
        m_layout = target;
        m_dockedLayout = target;
        m_leftAligned = left;
        applyLayout();
        saveSettings();
    });
    connect(m_pill, &TimelineReopenPill::clicked, this, [this]() { setTimelineVisible(true); });

    loadSettings();
    applyLayout();
}

void TimelineWidget::setFrameData(int frameCount, int currentFrame, const QVector<bool> &holds)
{
    m_state.frameCount = std::max(1, frameCount);
    m_state.currentFrame = std::min(std::max(0, currentFrame), m_state.frameCount - 1);
    m_state.holds = holds;
    pushState();
}

void TimelineWidget::setPlaybackActive(bool active)
{
    if (m_state.playing == active) {
        return;
    }
    m_state.playing = active;
    pushState();
}

void TimelineWidget::setFps(int fps)
{
    if (m_state.fps == fps) {
        return;
    }
    m_state.fps = fps;
    pushState();
}

void TimelineWidget::setLoop(bool loop)
{
    if (m_state.loop == loop) {
        return;
    }
    m_state.loop = loop;
    pushState();
}

void TimelineWidget::setOnionState(bool enabled, bool guides, const QSet<int> &lanes)
{
    m_state.onion = enabled;
    m_state.guideLines = guides;
    m_state.lanes = lanes;
    pushState();
}

void TimelineWidget::setThumbnailProvider(std::function<QImage(int, QSize)> provider)
{
    m_strip->setThumbnailProvider(std::move(provider));
}

void TimelineWidget::clearThumbnails()
{
    m_strip->clearThumbnails();
}

bool TimelineWidget::timelineVisible() const
{
    return m_layout != Layout::Hidden;
}

void TimelineWidget::setTimelineVisible(bool visible)
{
    if (visible == timelineVisible()) {
        return;
    }
    if (visible) {
        m_layout = m_dockedLayout;
    } else {
        m_dockedLayout = m_layout;
        m_layout = Layout::Hidden;
    }
    applyLayout();
    saveSettings();
    emit visibilityChanged(visible);
}

void TimelineWidget::handleCommand(TimelineCommand command)
{
    switch (command) {
    case TimelineCommand::AddFrame:
        emit addFrameRequested();
        break;
    case TimelineCommand::AddHold:
        emit addHoldRequested();
        break;
    case TimelineCommand::DeleteFrame:
        emit deleteFrameRequested();
        break;
    case TimelineCommand::Onion:
        emit onionToggled(!m_state.onion);
        break;
    case TimelineCommand::GuideLines:
        if (m_state.onion) {
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
    case TimelineCommand::Orientation:
        m_layout = (m_layout == Layout::VerticalStrip) ? Layout::HorizontalStrip
                                                       : Layout::VerticalStrip;
        m_dockedLayout = m_layout;
        applyLayout();
        saveSettings();
        break;
    case TimelineCommand::Float:
        if (m_container) {
            const QRect canvas(m_container->canvasArea()->mapToGlobal(QPoint(0, 0)),
                               m_container->canvasArea()->size());
            m_panel->move(canvas.center().x() - m_panel->width() / 2,
                          canvas.bottom() - m_panel->height() - 26);
        }
        m_layout = Layout::Floating;
        m_dockedLayout = Layout::Floating;
        applyLayout();
        saveSettings();
        break;
    case TimelineCommand::Collapse:
        m_state.collapsed = !m_state.collapsed;
        applyLayout();
        saveSettings();
        break;
    case TimelineCommand::Close:
        setTimelineVisible(false);
        break;
    }
}

void TimelineWidget::applyLayout()
{
    if (m_applyingLayout) {
        return;
    }
    m_applyingLayout = true;

    m_state.vertical = (m_layout == Layout::VerticalStrip);
    m_state.floating = (m_layout == Layout::Floating);
    m_state.leftAligned = m_leftAligned;

    QVBoxLayout *box = qobject_cast<QVBoxLayout *>(layout());
    // Reparenting is what removes a widget from its old layout (Qt turns the
    // ChildRemoved into a layout item removal), so every move starts from
    // here and the placement below is the only thing that has to be right.
    if (m_container) {
        m_container->setSideChrome(nullptr, Qt::LeftEdge);
    }
    m_strip->setParent(this);
    m_bar->setParent(this);

    const bool stripVisible = (m_layout == Layout::HorizontalStrip
                               || m_layout == Layout::VerticalStrip
                               || m_layout == Layout::Floating)
                              && !(m_state.collapsed && m_layout != Layout::Floating);
    m_strip->setVertical(m_layout == Layout::VerticalStrip);

    switch (m_layout) {
    case Layout::TransportOnly:
    case Layout::HorizontalStrip:
        if (stripVisible) {
            box->addWidget(m_strip);
        }
        box->addWidget(m_bar);
        show();
        m_panel->hide();
        break;
    case Layout::VerticalStrip:
        if (stripVisible && m_container) {
            m_container->setSideChrome(m_strip, m_leftAligned ? Qt::LeftEdge : Qt::RightEdge);
        }
        box->addWidget(m_bar);
        show();
        m_panel->hide();
        break;
    case Layout::Floating:
        if (m_panel->pos().isNull() && m_container) {
            // A panel restored from settings has never been placed; park it
            // over the foot of the canvas rather than in the screen corner.
            QWidget *host = m_container->canvasArea();
            const QRect canvas(host->mapToGlobal(QPoint(0, 0)), host->size());
            m_panel->move(canvas.center().x() - m_panel->width() / 2,
                          canvas.bottom() - m_panel->height() - 26);
        }
        m_panel->setContentWidgets(m_strip, m_bar);
        m_panel->show();
        m_panel->raise();
        hide();
        break;
    case Layout::Hidden:
        m_panel->hide();
        hide();
        break;
    }

    m_strip->setVisible(stripVisible);
    m_bar->setVisible(m_layout != Layout::Hidden);
    m_pill->setVisible(m_layout == Layout::Hidden);
    positionPill();

    m_applyingLayout = false;
    pushState();
}

void TimelineWidget::pushState()
{
    m_bar->setState(m_state);
    m_strip->setState(m_state);
}

void TimelineWidget::positionPill()
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

bool TimelineWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_container && watched == m_container->canvasArea() && event->type() == QEvent::Resize) {
        positionPill();
    }
    return QWidget::eventFilter(watched, event);
}

TimelineWidget::Layout TimelineWidget::dropTargetFor(const QPoint &globalPos,
                                                     bool *leftAligned) const
{
    return dropTargetFor(globalPos, leftAligned, kDetachDistance);
}

TimelineWidget::Layout TimelineWidget::dropTargetFor(const QPoint &globalPos, bool *leftAligned,
                                                     int threshold) const
{
    if (!m_container) {
        return Layout::Floating;
    }
    QWidget *host = m_container->canvasArea();
    const QRect canvas(host->mapToGlobal(QPoint(0, 0)), host->size());
    const int distLeft = globalPos.x() - canvas.left();
    const int distRight = canvas.right() - globalPos.x();
    const int distBottom = canvas.bottom() - globalPos.y();
    if (distLeft >= threshold && distRight >= threshold && distBottom >= threshold) {
        return Layout::Floating;
    }
    if (distBottom <= distLeft && distBottom <= distRight) {
        return Layout::HorizontalStrip;
    }
    if (leftAligned) {
        // The midline decides the side: whichever edge the pointer is nearer
        // is the edge the strip wants to live on.
        *leftAligned = distLeft <= distRight;
    }
    return Layout::VerticalStrip;
}

void TimelineWidget::loadSettings()
{
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));
    const int stored = settings.value(QStringLiteral("layout"), int(Layout::TransportOnly)).toInt();
    m_dockedLayout = (stored >= int(Layout::TransportOnly) && stored <= int(Layout::Floating))
                         ? Layout(stored)
                         : Layout::TransportOnly;
    m_leftAligned = settings.value(QStringLiteral("leftAligned"), false).toBool();
    m_state.collapsed = settings.value(QStringLiteral("collapsed"), false).toBool();
    const bool visible = settings.value(QStringLiteral("visible"), true).toBool();
    settings.endGroup();
    m_layout = visible ? m_dockedLayout : Layout::Hidden;
}

void TimelineWidget::saveSettings()
{
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.beginGroup(QStringLiteral("timeline"));
    settings.setValue(QStringLiteral("layout"), int(m_dockedLayout));
    settings.setValue(QStringLiteral("leftAligned"), m_leftAligned);
    settings.setValue(QStringLiteral("collapsed"), m_state.collapsed);
    settings.setValue(QStringLiteral("visible"), m_layout != Layout::Hidden);
    settings.endGroup();
}

int TimelineWidget::fpsForText(const QString &text, int fallback)
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

QString TimelineWidget::textForFps(int fps)
{
    for (int i = 0; i < kCadenceCount; ++i) {
        if (kCadences[i].fps == fps) {
            return QStringLiteral("%1  (%2 fps)").arg(QString::fromUtf8(kCadences[i].title))
                       .arg(fps);
        }
    }
    return QStringLiteral("%1 fps").arg(fps);
}

QString TimelineWidget::shortTextForFps(int fps)
{
    for (int i = 0; i < kCadenceCount; ++i) {
        if (kCadences[i].fps == fps) {
            return QString::fromUtf8(kCadences[i].shortTitle);
        }
    }
    return QStringLiteral("%1 fps").arg(fps);
}
