#include "subcontrolframe.h"

#include "theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QSizeGrip>
#include <QVBoxLayout>

#include <cmath>

namespace {
// The title bar is 20px by the design, and every metric below is measured
// inside that band.
constexpr int kTitleHeight = 20;
constexpr int kTitlePadding = 6;
constexpr int kAffordance = 14;
constexpr int kDragThreshold = 4;
// The drop wash: an Accent fill light enough to read the panel through, with
// a solid edge so the slot has a boundary rather than a smudge.
constexpr int kPreviewAlpha = 64;
}

SubControlHost::~SubControlHost() = default;

void SubControlHost::revealSubControl(SubControlFrame *)
{
    // Nothing to do: a host that lays every frame it owns out on one visible
    // surface has already revealed it by holding it.
}

// ---------------------------------------------------------------- registry

SubControlRegistry *SubControlRegistry::instance()
{
    static SubControlRegistry registry;
    return &registry;
}

void SubControlRegistry::registerFrame(SubControlFrame *frame)
{
    if (frame && !m_frames.contains(frame)) {
        m_frames.append(frame);
    }
}

void SubControlRegistry::unregisterFrame(SubControlFrame *frame)
{
    m_frames.removeAll(frame);
}

SubControlFrame *SubControlRegistry::frame(const QString &name) const
{
    for (SubControlFrame *frame : m_frames) {
        if (frame && frame->name() == name) {
            return frame;
        }
    }
    return nullptr;
}

QList<SubControlFrame *> SubControlRegistry::frames() const
{
    return m_frames;
}

void SubControlRegistry::registerHost(SubControlHost *host)
{
    if (host && !m_hosts.contains(host)) {
        m_hosts.append(host);
    }
}

void SubControlRegistry::unregisterHost(SubControlHost *host)
{
    m_hosts.removeAll(host);
}

QList<SubControlHost *> SubControlRegistry::hosts() const
{
    return m_hosts;
}

QWidget *SubControlRegistry::keeper()
{
    if (!m_keeper) {
        m_keeper = new QWidget;
        m_keeper->setObjectName(QStringLiteral("SubControlKeeper"));
        // Never shown, never laid out: it only has to be a live parent.
        m_keeper->hide();
    }
    return m_keeper;
}

// --------------------------------------------------------------- title bar

SubControlTitleBar::SubControlTitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kTitleHeight);
    setMouseTracking(true);
    setCursor(Qt::SizeAllCursor);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { update(); });
}

void SubControlTitleBar::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void SubControlTitleBar::setFloating(bool floating)
{
    if (m_floating == floating) {
        return;
    }
    m_floating = floating;
    setToolTip(m_floating
                   ? QStringLiteral("Drag onto a panel to dock this control")
                   : QStringLiteral("Drag out to float this control"));
    update();
}

QSize SubControlTitleBar::sizeHint() const
{
    const QFontMetrics metrics(font());
    return QSize(metrics.horizontalAdvance(m_title) + kTitlePadding * 3 + kAffordance,
                 kTitleHeight);
}

QRect SubControlTitleBar::affordanceRect() const
{
    return QRect(width() - kTitlePadding - kAffordance,
                 (kTitleHeight - kAffordance) / 2,
                 kAffordance,
                 kAffordance);
}

void SubControlTitleBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), AnimeTheme::color(AnimeTheme::Role::SurfaceAlt));
    painter.setPen(QPen(AnimeTheme::color(AnimeTheme::Role::Divider), 1.0));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    QFont titleFont = font();
    titleFont.setPointSizeF(qMax(6.5, titleFont.pointSizeF() - 0.5));
    painter.setFont(titleFont);
    painter.setPen(AnimeTheme::color(AnimeTheme::Role::Text));
    const QRect textRect(kTitlePadding, 0,
                         width() - kTitlePadding * 3 - kAffordance, height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(m_title, Qt::ElideRight, textRect.width()));

    // The affordance: a small square with an arrow leaving it when embedded,
    // entering it when floating. One glyph says both what this is and what a
    // click would do.
    const QRect glyph = affordanceRect();
    const QColor ink = m_pressedAffordance
                           ? AnimeTheme::color(AnimeTheme::Role::AccentActive)
                           : (m_hoverAffordance ? AnimeTheme::color(AnimeTheme::Role::Accent)
                                                : AnimeTheme::color(AnimeTheme::Role::TextDim));
    painter.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QRectF box = QRectF(glyph).adjusted(1.5, 4.5, -4.5, -1.5);
    painter.drawRect(box);
    const QPointF from = m_floating ? QPointF(glyph.right() - 1.0, glyph.top() + 1.0)
                                    : QPointF(box.center());
    const QPointF to = m_floating ? QPointF(box.center())
                                  : QPointF(glyph.right() - 1.0, glyph.top() + 1.0);
    painter.drawLine(from, to);
    const QPointF tip = to;
    const QPointF direction = to - from;
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length > 0.001) {
        const QPointF unit(direction.x() / length, direction.y() / length);
        const QPointF normal(-unit.y(), unit.x());
        painter.drawLine(tip, tip - unit * 3.5 + normal * 2.2);
        painter.drawLine(tip, tip - unit * 3.5 - normal * 2.2);
    }
}

void SubControlTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (affordanceRect().contains(event->pos())) {
        m_pressedAffordance = true;
        update();
        event->accept();
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = event->globalPosition().toPoint();
    event->accept();
}

void SubControlTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint global = event->globalPosition().toPoint();
    if (!m_pressed) {
        const bool hover = affordanceRect().contains(event->pos());
        if (hover != m_hoverAffordance) {
            m_hoverAffordance = hover;
            update();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (!m_dragging) {
        if ((global - m_pressGlobal).manhattanLength() < kDragThreshold) {
            event->accept();
            return;
        }
        m_dragging = true;
        emit dragStarted(m_pressGlobal);
    }
    emit dragMoved(global);
    event->accept();
}

void SubControlTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const QPoint global = event->globalPosition().toPoint();
    if (m_pressedAffordance) {
        m_pressedAffordance = false;
        update();
        if (affordanceRect().contains(event->pos())) {
            emit affordanceClicked();
        }
        event->accept();
        return;
    }
    if (m_dragging) {
        m_dragging = false;
        m_pressed = false;
        emit dragFinished(global);
        event->accept();
        return;
    }
    m_pressed = false;
    event->accept();
}

void SubControlTitleBar::leaveEvent(QEvent *event)
{
    if (m_hoverAffordance) {
        m_hoverAffordance = false;
        update();
    }
    QWidget::leaveEvent(event);
}

// ------------------------------------------------------------------- frame

SubControlFrame::SubControlFrame(const QString &name, const QString &title, QWidget *owner)
    : QWidget(SubControlRegistry::instance()->keeper())
    , m_name(name)
    , m_title(title)
    , m_owner(owner)
{
    setObjectName(QStringLiteral("SubControlFrame_%1").arg(name));
    setWindowTitle(title);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    m_titleBar = new SubControlTitleBar(this);
    m_titleBar->setTitle(title);
    outer->addWidget(m_titleBar);

    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);
    m_placeholder = new QLabel(m_body);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setMargin(8);
    m_bodyLayout->addWidget(m_placeholder);
    outer->addWidget(m_body, 1);

    // Frameless windows have no native resize edge, so a floating frame gets
    // an explicit grip. Hidden while embedded: the host owns the size there.
    m_grip = new QSizeGrip(this);
    m_grip->setVisible(false);
    outer->addWidget(m_grip, 0, Qt::AlignRight | Qt::AlignBottom);

    connect(m_titleBar, &SubControlTitleBar::dragStarted, this, &SubControlFrame::beginDrag);
    connect(m_titleBar, &SubControlTitleBar::dragMoved, this, &SubControlFrame::dragTo);
    connect(m_titleBar, &SubControlTitleBar::dragFinished, this, &SubControlFrame::endDrag);
    connect(m_titleBar, &SubControlTitleBar::affordanceClicked, this, [this]() {
        if (!isFloating()) {
            floatFrame();
            return;
        }
        // Back to the last host it knew - m_host is already gone by the time a
        // frame floats, so the dock half of the glyph reads m_lastHost. Parked
        // only when it never had a host, or that host has since died: guessing
        // one from a click would drop the frame somewhere the user never
        // pointed at.
        if (m_lastHost && SubControlRegistry::instance()->hosts().contains(m_lastHost)) {
            embedInto(m_lastHost);
        } else {
            park();
        }
    });

    applyTheme();
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { applyTheme(); });

    hide();
    SubControlRegistry::instance()->registerFrame(this);
}

SubControlFrame::~SubControlFrame()
{
    SubControlRegistry::instance()->unregisterFrame(this);
}

QString SubControlFrame::name() const
{
    return m_name;
}

void SubControlFrame::setContent(QWidget *widget)
{
    if (m_content == widget) {
        return;
    }
    releaseContent();
    m_content = widget;
    if (widget) {
        widget->setParent(m_body);
        m_bodyLayout->addWidget(widget, 1);
        widget->show();
    }
    m_placeholder->setVisible(m_content == nullptr);
}

QWidget *SubControlFrame::content() const
{
    return m_content;
}

void SubControlFrame::releaseContent()
{
    if (m_content) {
        // Out of the layout only. Where it goes next is the caller's business,
        // and a null parent on the way there would flash it as a window.
        m_bodyLayout->removeWidget(m_content);
        m_content = nullptr;
    }
    m_placeholder->setVisible(true);
}

void SubControlFrame::setPlaceholderText(const QString &text)
{
    m_placeholder->setText(text);
}

SubControlFrame::Placement SubControlFrame::placement() const
{
    return m_placement;
}

bool SubControlFrame::isFloating() const
{
    return m_placement == Placement::Floating;
}

SubControlHost *SubControlFrame::host() const
{
    return m_host;
}

bool SubControlFrame::isLive() const
{
    if (m_placement == Placement::Parked) {
        return false;
    }
    if (isFloating()) {
        return !isHidden();
    }
    // Embedded: isHidden() answers only for the frame's OWN flag, and a
    // stacked page the user tabbed away from hides its children without
    // touching theirs - the router would then hand the board to a frame nobody
    // can see. isVisibleTo(window()) asks the whole chain and still reads true
    // before the window is first shown, which is what isHidden() was for.
    return isVisibleTo(window());
}

void SubControlFrame::detachFromHost()
{
    if (m_placement == Placement::Embedded && m_host) {
        // The host laid it out; taking it out of that layout is what leaving
        // means. The host's own layout drops a reparented child on its own.
        m_host = nullptr;
    }
}

void SubControlFrame::embedInto(SubControlHost *host)
{
    if (!host) {
        return;
    }
    const bool wasFloating = isFloating();
    detachFromHost();
    if (wasFloating) {
        m_floatGeometry = geometry();
        // Drop the window flags BEFORE the host lays it out, or Qt keeps
        // treating it as a top level inside the layout.
        setWindowFlags(Qt::Widget);
    }
    m_host = host;
    m_lastHost = host;
    m_placement = Placement::Embedded;
    m_grip->setVisible(false);
    m_titleBar->setFloating(false);
    host->embedSubControl(this);
    show();
    emit homeChanged();
}

void SubControlFrame::adoptedBy(SubControlHost *host)
{
    if (!host || (m_host == host && m_placement == Placement::Embedded)) {
        return;
    }
    if (isFloating()) {
        m_floatGeometry = geometry();
        setWindowFlags(Qt::Widget);
    }
    m_host = host;
    m_lastHost = host;
    m_placement = Placement::Embedded;
    m_grip->setVisible(false);
    m_titleBar->setFloating(false);
    emit homeChanged();
}

void SubControlFrame::floatFrame()
{
    QPoint topLeft = m_floatGeometry.topLeft();
    if (m_floatGeometry.isNull()) {
        if (m_owner) {
            topLeft = m_owner->mapToGlobal(QPoint(m_owner->width() / 3, m_owner->height() / 4));
        } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
            topLeft = screen->availableGeometry().center();
        }
    }
    floatFrame(topLeft);
}

void SubControlFrame::floatFrame(const QPoint &globalTopLeft)
{
    const QSize wanted = m_floatGeometry.isValid()
                             ? m_floatGeometry.size()
                             : size().expandedTo(QSize(320, 260));
    detachFromHost();
    m_placement = Placement::Floating;
    // Qt::Tool keeps it above the window it belongs to and off the task bar;
    // frameless because the 20px bar above IS this window's title bar.
    setParent(m_owner, Qt::Tool | Qt::FramelessWindowHint);
    m_titleBar->setFloating(true);
    m_grip->setVisible(true);
    setGeometry(QRect(globalTopLeft, wanted));
    show();
    raise();
    m_floatGeometry = geometry();
    emit homeChanged();
}

void SubControlFrame::park()
{
    if (m_placement == Placement::Parked && parentWidget() == SubControlRegistry::instance()->keeper()) {
        return;
    }
    if (isFloating()) {
        m_floatGeometry = geometry();
        setWindowFlags(Qt::Widget);
    }
    detachFromHost();
    m_host = nullptr;
    // Putting the frame AWAY is the one gesture that also forgets where it
    // came from: the next surface() floats it rather than re-entering a panel
    // the user has finished with.
    m_lastHost = nullptr;
    m_placement = Placement::Parked;
    m_grip->setVisible(false);
    setParent(SubControlRegistry::instance()->keeper());
    hide();
    emit homeChanged();
}

void SubControlFrame::surface()
{
    if (m_placement == Placement::Parked) {
        floatFrame();
        return;
    }
    show();
    raise();
    if (isFloating()) {
        activateWindow();
    } else {
        // The host knows how to make its own slot visible - a page host has to
        // SELECT the page rather than show it, which is why this is asked
        // rather than done here.
        if (m_host) {
            m_host->revealSubControl(this);
        }
        // A frame inside a closed dock is not "shown" by showing the frame:
        // the dock has to come up with it. DOCKS ONLY - calling show() on any
        // hidden ancestor would put a stacked-widget page on top of the page
        // the user actually selected.
        for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (qobject_cast<QDockWidget *>(ancestor)) {
                ancestor->show();
                ancestor->raise();
            }
            if (ancestor->isWindow()) {
                break;
            }
        }
    }
    emit homeChanged();
}

void SubControlFrame::applyTheme()
{
    m_placeholder->setStyleSheet(
        QStringLiteral("color: %1;").arg(AnimeTheme::color(AnimeTheme::Role::TextDim).name()));
    update();
}

void SubControlFrame::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), AnimeTheme::color(AnimeTheme::Role::Surface));
    painter.setPen(QPen(AnimeTheme::color(AnimeTheme::Role::Divider), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

void SubControlFrame::closeEvent(QCloseEvent *event)
{
    // Closing a floating sub-control puts it away rather than destroying it:
    // the registry owns these, and a deleted frame would take whatever the
    // shell parked inside it down with it.
    event->ignore();
    park();
}

void SubControlFrame::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (!m_emittingHomeChanged) {
        m_emittingHomeChanged = true;
        emit homeChanged();
        m_emittingHomeChanged = false;
    }
}

void SubControlFrame::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_emittingHomeChanged) {
        m_emittingHomeChanged = true;
        emit homeChanged();
        m_emittingHomeChanged = false;
    }
}

void SubControlFrame::beginDrag(const QPoint &globalPos)
{
    m_dragging = true;
    m_dragGrab = mapFromGlobal(globalPos);
    if (!isFloating()) {
        // Windowize where it already is, so the frame does not jump out from
        // under the cursor the moment it detaches.
        const QPoint topLeft = mapToGlobal(QPoint(0, 0));
        m_floatGeometry = QRect(topLeft, size());
        floatFrame(topLeft);
    }
    // The reparent above can cost the implicit grab the press installed; take
    // it back explicitly so the release still arrives here.
    m_titleBar->grabMouse();
}

void SubControlFrame::dragTo(const QPoint &globalPos)
{
    if (!m_dragging) {
        return;
    }
    move(globalPos - m_dragGrab);

    QRect previewRect;
    SubControlHost *candidate = hostAt(globalPos, &previewRect);
    if (candidate && !previewRect.isEmpty()) {
        showPreview(candidate, previewRect);
    } else {
        hidePreview();
    }
}

void SubControlFrame::endDrag(const QPoint &globalPos)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    m_titleBar->releaseMouse();
    hidePreview();

    QRect previewRect;
    if (SubControlHost *candidate = hostAt(globalPos, &previewRect)) {
        if (!previewRect.isEmpty()) {
            embedInto(candidate);
            return;
        }
    }
    m_floatGeometry = geometry();
}

SubControlHost *SubControlFrame::hostAt(const QPoint &globalPos, QRect *previewRect) const
{
    for (SubControlHost *host : SubControlRegistry::instance()->hosts()) {
        if (!host) {
            continue;
        }
        QWidget *widget = host->subControlHostWidget();
        // A hidden page is not a drop target: the frame would vanish into a
        // tab nobody is looking at.
        if (!widget || !widget->isVisible()) {
            continue;
        }
        const QRect rect = host->subControlPreviewRect(globalPos);
        if (!rect.isEmpty()) {
            if (previewRect) {
                *previewRect = rect;
            }
            return host;
        }
    }
    if (previewRect) {
        *previewRect = QRect();
    }
    return nullptr;
}

void SubControlFrame::showPreview(SubControlHost *host, const QRect &globalRect)
{
    QWidget *hostWidget = host ? host->subControlHostWidget() : nullptr;
    if (!hostWidget) {
        hidePreview();
        return;
    }
    if (!m_preview) {
        m_preview = new QWidget(hostWidget);
        m_preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    } else if (m_preview->parentWidget() != hostWidget) {
        m_preview->setParent(hostWidget);
    }
    const QColor accent = AnimeTheme::color(AnimeTheme::Role::Accent);
    m_preview->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,%4); border: 1px solid %5;")
                                 .arg(accent.red())
                                 .arg(accent.green())
                                 .arg(accent.blue())
                                 .arg(kPreviewAlpha)
                                 .arg(accent.name()));
    m_preview->setGeometry(QRect(hostWidget->mapFromGlobal(globalRect.topLeft()), globalRect.size()));
    m_preview->show();
    m_preview->raise();
}

void SubControlFrame::hidePreview()
{
    if (m_preview) {
        m_preview->hide();
    }
}
