#include "palettecontrol.h"
#include "theme.h"

#include <QConicalGradient>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

namespace {

// Panel geometry, from the design: 264px wide, 16px side padding, 15/17 top
// and bottom, 12px between the rows.
const int kPanelWidth = 264;
const int kPanelMarginH = 16;
const int kPanelMarginTop = 15;
const int kPanelMarginBottom = 17;
const int kPanelSpacing = 12;
const int kChipSize = 38;
const int kTabHeight = 25;
const int kStripHeight = 15;
const int kStripGap = 9;
const int kRingThickness = 14;
const int kSwatchMin = 24;
const int kSwatchGap = 6;
const int kReadoutWidth = 44;
const int kReadoutHeight = 24;
const int kPopupWidth = 320;
const int kPopupHeader = 31;

// The only colours in this file that are not theme roles. A transparency
// checkerboard has to read as "nothing is here" in either theme, so it stays
// the neutral grey pair every drawing application uses.
const QColor kCheckerLight(255, 255, 255);
const QColor kCheckerDark(203, 203, 203);

QColor role(AnimeTheme::Role which)
{
    return AnimeTheme::color(which);
}

QColor blend(const QColor &base, const QColor &over, qreal amount)
{
    const qreal t = qBound(0.0, amount, 1.0);
    return QColor(qRound(base.red() * (1.0 - t) + over.red() * t),
                  qRound(base.green() * (1.0 - t) + over.green() * t),
                  qRound(base.blue() * (1.0 - t) + over.blue() * t));
}

// The accent as a FIELD rather than a line: strong enough in dark, a wash in
// light, which is the whole difference between the two themes here.
QColor accentField()
{
    const bool dark = AnimeTheme::mode() == AnimeTheme::Mode::Dark;
    return blend(role(AnimeTheme::Role::Surface), role(AnimeTheme::Role::Accent), dark ? 0.32 : 0.14);
}

QColor hoverWash()
{
    return blend(role(AnimeTheme::Role::Surface), role(AnimeTheme::Role::Accent), 0.10);
}

QFont monoFont(int pixelSize, bool bold)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return font;
}

QFont capsFont(const QWidget *widget, int pixelSize, qreal spacing)
{
    QFont font = widget->font();
    font.setPixelSize(pixelSize);
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return font;
}

void paintChecker(QPainter &painter, const QRectF &rect, int cell)
{
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, kCheckerLight);
    for (int y = 0; y * cell < rect.height() + cell; ++y) {
        for (int x = 0; x * cell < rect.width() + cell; ++x) {
            if ((x + y) % 2 == 0) {
                continue;
            }
            painter.fillRect(QRectF(rect.left() + x * cell, rect.top() + y * cell, cell, cell),
                             kCheckerDark);
        }
    }
    painter.restore();
}

// A colour over the checkerboard, framed by the theme's hairline.
void paintSwatchBody(QPainter &painter, const QRectF &rect, const QColor &color)
{
    if (color.alpha() < 255) {
        paintChecker(painter, rect.adjusted(1, 1, -1, -1), 8);
    }
    painter.fillRect(rect.adjusted(1, 1, -1, -1), color);
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
}

void paintSvSquare(QPainter &painter, const QRect &rect, int hue)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect, QColor::fromHsv(hue, 255, 255));

    QLinearGradient white(rect.topLeft(), rect.topRight());
    white.setColorAt(0.0, QColor(255, 255, 255, 255));
    white.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(rect, white);

    QLinearGradient black(rect.topLeft(), rect.bottomLeft());
    black.setColorAt(0.0, QColor(0, 0, 0, 0));
    black.setColorAt(1.0, QColor(0, 0, 0, 255));
    painter.fillRect(rect, black);

    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
    painter.restore();
}

// White inside, dark outside: the marker has to survive on both ends of the
// square it sits on, and no single theme colour does.
void paintSvMarker(QPainter &painter, const QPointF &center)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
    painter.drawEllipse(center, 7.0, 7.0);
    painter.setPen(QPen(QColor(255, 255, 255), 1.5));
    painter.drawEllipse(center, 5.5, 5.5);
    painter.restore();
}

void paintTrackMarker(QPainter &painter, const QRect &groove, qreal position)
{
    const qreal x = groove.left() + qBound(0.0, position, 1.0) * groove.width();
    const QRectF bar(x - 1.5, groove.top() - 3, 3.0, groove.height() + 6);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(bar.adjusted(-1, -1, 1, 1), role(AnimeTheme::Role::Surface));
    painter.fillRect(bar, role(AnimeTheme::Role::Text));
    painter.restore();
}

void paintHueTrack(QPainter &painter, const QRect &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    for (int i = 0; i <= 6; ++i) {
        gradient.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
    }
    painter.fillRect(rect, gradient);
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
}

void paintAlphaTrack(QPainter &painter, const QRect &rect, const QColor &color)
{
    paintChecker(painter, rect, 8);
    QColor from = color;
    from.setAlpha(0);
    QColor to = color;
    to.setAlpha(255);
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, from);
    gradient.setColorAt(1.0, to);
    painter.fillRect(rect, gradient);
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
}

void paintReadout(QPainter &painter, const QRect &rect, const QString &text)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect, role(AnimeTheme::Role::SurfaceAlt));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
    painter.setFont(monoFont(11, true));
    painter.setPen(role(AnimeTheme::Role::Text));
    painter.drawText(rect, Qt::AlignCenter, text);
    painter.restore();
}

// Hue 0 sits at 12 o'clock and grows clockwise, so the ring reads like a
// colour wheel rather than like a maths diagram.
qreal hueFromPoint(const QPointF &delta)
{
    const qreal degrees = qRadiansToDegrees(std::atan2(delta.x(), -delta.y()));
    return std::fmod(degrees + 360.0, 360.0);
}

QPointF pointForHue(const QPointF &center, qreal radius, int hue)
{
    const qreal radians = qDegreesToRadians(static_cast<qreal>(hue));
    return QPointF(center.x() + std::sin(radians) * radius,
                   center.y() - std::cos(radians) * radius);
}

QColor colorFromText(const QString &text, const QColor &fallback)
{
    const QColor parsed(text);
    return parsed.isValid() ? parsed : fallback;
}

}

namespace AnimePalette {

QColor Hsva::toColor() const
{
    QColor color = QColor::fromHsvF(qBound(0, hue, 359) / 360.0,
                                    qBound(0.0, sat, 1.0),
                                    qBound(0.0, val, 1.0));
    color.setAlpha(qBound(0, alpha, 255));
    return color;
}

void Hsva::setColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    const int incoming = color.hsvHue();
    // A grey has no hue and a black has neither hue nor saturation. Taking
    // their -1 back would spin the wheel to red every time a drag touched an
    // edge, so the last meaningful value stands.
    if (incoming >= 0) {
        hue = incoming;
    }
    const qreal incomingSat = color.hsvSaturationF();
    if (color.valueF() > 0.0) {
        sat = incomingSat;
    }
    val = color.valueF();
    alpha = color.alpha();
}

void Hsva::setRgb(int r, int g, int b)
{
    QColor color(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    color.setAlpha(alpha);
    setColor(color);
}

}

// --- PalettePicker ----------------------------------------------------------

PalettePicker::PalettePicker(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::NoFocus);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, qOverload<>(&QWidget::update));
}

QColor PalettePicker::color() const
{
    return m_color.toColor();
}

void PalettePicker::setColor(const QColor &color)
{
    m_color.setColor(color);
    update();
}

void PalettePicker::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !applyAt(event->pos(), true)) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    update();
    emit colorPreview(color());
}

void PalettePicker::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (applyAt(event->pos(), false)) {
        update();
        emit colorPreview(color());
    }
}

void PalettePicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    m_zone = -1;
    // One message per gesture: a continuous emit would arm a tool and repaint
    // every view on every mouse move.
    emit colorCommitted(color());
}

// --- PaletteCirclePicker ----------------------------------------------------

PaletteCirclePicker::PaletteCirclePicker(QWidget *parent)
    : PalettePicker(parent)
{
    setMinimumHeight(180);
}

QSize PaletteCirclePicker::sizeHint() const
{
    return QSize(208, 208);
}

QRectF PaletteCirclePicker::ringRect() const
{
    const qreal side = qMin(width(), height()) - 2.0;
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QRect PaletteCirclePicker::squareRect() const
{
    const QRectF ring = ringRect();
    const qreal inner = ring.width() / 2.0 - kRingThickness - 4.0;
    const qreal side = qMax(24.0, inner * std::sqrt(2.0) - 6.0);
    const QPointF center = ring.center();
    return QRectF(center.x() - side / 2.0, center.y() - side / 2.0, side, side).toRect();
}

void PaletteCirclePicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF ring = ringRect();
    const QPointF center = ring.center();
    const qreal outer = ring.width() / 2.0;
    const qreal inner = outer - kRingThickness;

    QConicalGradient wheel(center, 90.0);
    for (int i = 0; i <= 36; ++i) {
        const qreal position = i / 36.0;
        wheel.setColorAt(position, QColor::fromHsv(static_cast<int>((1.0 - position) * 360.0) % 360, 255, 255));
    }

    QPainterPath annulus;
    annulus.setFillRule(Qt::OddEvenFill);
    annulus.addEllipse(center, outer, outer);
    annulus.addEllipse(center, inner, inner);
    painter.setPen(Qt::NoPen);
    painter.setBrush(wheel);
    painter.drawPath(annulus);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1.0));
    painter.drawEllipse(center, outer - 0.5, outer - 0.5);
    painter.drawEllipse(center, inner + 0.5, inner + 0.5);

    const QPointF marker = pointForHue(center, (outer + inner) / 2.0, m_color.hue);
    painter.setBrush(QColor::fromHsv(m_color.hue, 255, 255));
    painter.setPen(QPen(role(AnimeTheme::Role::Surface), 2.0));
    painter.drawEllipse(marker, 5.0, 5.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(role(AnimeTheme::Role::Text), 1.0));
    painter.drawEllipse(marker, 6.0, 6.0);

    const QRect square = squareRect();
    paintSvSquare(painter, square, m_color.hue);
    paintSvMarker(painter, QPointF(square.left() + m_color.sat * square.width(),
                                   square.top() + (1.0 - m_color.val) * square.height()));
}

bool PaletteCirclePicker::applyAt(const QPoint &pos, bool begin)
{
    const QRect square = squareRect();
    const QRectF ring = ringRect();
    const QPointF center = ring.center();
    const qreal outer = ring.width() / 2.0;
    const qreal inner = outer - kRingThickness;

    if (begin) {
        const qreal distance = QLineF(center, QPointF(pos)).length();
        if (square.contains(pos)) {
            m_zone = 1;
        } else if (distance >= inner - 4.0 && distance <= outer + 4.0) {
            m_zone = 0;
        } else {
            m_zone = -1;
            return false;
        }
    }

    if (m_zone == 0) {
        m_color.hue = static_cast<int>(hueFromPoint(QPointF(pos) - center)) % 360;
        return true;
    }
    if (m_zone == 1) {
        m_color.sat = qBound(0.0, (pos.x() - square.left()) / qreal(qMax(1, square.width())), 1.0);
        m_color.val = qBound(0.0, 1.0 - (pos.y() - square.top()) / qreal(qMax(1, square.height())), 1.0);
        return true;
    }
    return false;
}

// --- PaletteHsvPicker -------------------------------------------------------

PaletteHsvPicker::PaletteHsvPicker(QWidget *parent)
    : PalettePicker(parent)
{
    setMinimumHeight(150);
}

QSize PaletteHsvPicker::sizeHint() const
{
    return QSize(232, 150 + 2 * (kStripGap + kStripHeight));
}

void PaletteHsvPicker::zoneRects(QRect *square, QRect *hue, QRect *alpha) const
{
    const int stripsHeight = 2 * kStripHeight + 2 * kStripGap;
    const int squareHeight = qMax(60, height() - stripsHeight);
    if (square) {
        *square = QRect(0, 0, width(), squareHeight);
    }
    if (hue) {
        *hue = QRect(0, squareHeight + kStripGap, width(), kStripHeight);
    }
    if (alpha) {
        *alpha = QRect(0, squareHeight + 2 * kStripGap + kStripHeight, width(), kStripHeight);
    }
}

void PaletteHsvPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QRect square;
    QRect hue;
    QRect alpha;
    zoneRects(&square, &hue, &alpha);

    paintSvSquare(painter, square, m_color.hue);
    paintSvMarker(painter, QPointF(square.left() + m_color.sat * square.width(),
                                   square.top() + (1.0 - m_color.val) * square.height()));

    paintHueTrack(painter, hue);
    paintTrackMarker(painter, hue, m_color.hue / 360.0);

    QColor opaque = m_color.toColor();
    opaque.setAlpha(255);
    paintAlphaTrack(painter, alpha, opaque);
    paintTrackMarker(painter, alpha, m_color.alpha / 255.0);
}

bool PaletteHsvPicker::applyAt(const QPoint &pos, bool begin)
{
    QRect square;
    QRect hue;
    QRect alpha;
    zoneRects(&square, &hue, &alpha);

    if (begin) {
        if (square.contains(pos)) {
            m_zone = 0;
        } else if (hue.adjusted(0, -4, 0, 4).contains(pos)) {
            m_zone = 1;
        } else if (alpha.adjusted(0, -4, 0, 4).contains(pos)) {
            m_zone = 2;
        } else {
            m_zone = -1;
            return false;
        }
    }

    switch (m_zone) {
    case 0:
        m_color.sat = qBound(0.0, (pos.x() - square.left()) / qreal(qMax(1, square.width())), 1.0);
        m_color.val = qBound(0.0, 1.0 - (pos.y() - square.top()) / qreal(qMax(1, square.height())), 1.0);
        return true;
    case 1:
        m_color.hue = qBound(0, qRound(359.0 * (pos.x() - hue.left()) / qMax(1, hue.width())), 359);
        return true;
    case 2:
        m_color.alpha = qBound(0, qRound(255.0 * (pos.x() - alpha.left()) / qMax(1, alpha.width())), 255);
        return true;
    default:
        return false;
    }
}

// --- PaletteRgbPicker -------------------------------------------------------

PaletteRgbPicker::PaletteRgbPicker(QWidget *parent)
    : PalettePicker(parent)
{
    setMinimumHeight(4 * kReadoutHeight + 3 * kStripGap);
}

QSize PaletteRgbPicker::sizeHint() const
{
    return QSize(232, 4 * kReadoutHeight + 3 * kStripGap);
}

QRect PaletteRgbPicker::rowRect(int index) const
{
    return QRect(0, index * (kReadoutHeight + kStripGap), width(), kReadoutHeight);
}

QRect PaletteRgbPicker::grooveRect(int index) const
{
    const QRect row = rowRect(index);
    const int left = row.left() + 9 + kStripGap;
    const int right = row.right() - kReadoutWidth - kStripGap;
    return QRect(left, row.center().y() - kStripHeight / 2, qMax(10, right - left), kStripHeight);
}

QRect PaletteRgbPicker::readoutRect(int index) const
{
    const QRect row = rowRect(index);
    return QRect(row.right() - kReadoutWidth + 1, row.top(), kReadoutWidth, kReadoutHeight);
}

int PaletteRgbPicker::channel(int index) const
{
    const QColor color = m_color.toColor();
    switch (index) {
    case 0:
        return color.red();
    case 1:
        return color.green();
    case 2:
        return color.blue();
    default:
        return m_color.alpha;
    }
}

void PaletteRgbPicker::setChannel(int index, int value)
{
    if (index == 3) {
        m_color.alpha = qBound(0, value, 255);
        return;
    }
    const QColor color = m_color.toColor();
    int rgb[3] = {color.red(), color.green(), color.blue()};
    rgb[index] = qBound(0, value, 255);
    m_color.setRgb(rgb[0], rgb[1], rgb[2]);
}

void PaletteRgbPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    static const char *labels[4] = {"R", "G", "B", "A"};
    const QColor color = m_color.toColor();

    for (int index = 0; index < 4; ++index) {
        const QRect row = rowRect(index);
        const QRect groove = grooveRect(index);

        painter.setFont(capsFont(this, 11, 0.0));
        painter.setPen(role(AnimeTheme::Role::TextDim));
        painter.drawText(QRect(row.left(), row.top(), 9, row.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, QString::fromLatin1(labels[index]));

        if (index == 3) {
            QColor opaque = color;
            opaque.setAlpha(255);
            paintAlphaTrack(painter, groove, opaque);
        } else {
            QColor from = color;
            QColor to = color;
            from.setAlpha(255);
            to.setAlpha(255);
            if (index == 0) {
                from.setRed(0);
                to.setRed(255);
            } else if (index == 1) {
                from.setGreen(0);
                to.setGreen(255);
            } else {
                from.setBlue(0);
                to.setBlue(255);
            }
            QLinearGradient gradient(groove.topLeft(), groove.topRight());
            gradient.setColorAt(0.0, from);
            gradient.setColorAt(1.0, to);
            painter.fillRect(groove, gradient);
            painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(groove).adjusted(0.5, 0.5, -0.5, -0.5));
        }

        paintTrackMarker(painter, groove, channel(index) / 255.0);
        paintReadout(painter, readoutRect(index), QString::number(channel(index)));
    }
}

bool PaletteRgbPicker::applyAt(const QPoint &pos, bool begin)
{
    if (begin) {
        m_zone = -1;
        for (int index = 0; index < 4; ++index) {
            if (rowRect(index).contains(pos)) {
                m_zone = index;
                break;
            }
        }
        if (m_zone < 0) {
            return false;
        }
    }
    if (m_zone < 0 || m_zone > 3) {
        return false;
    }
    const QRect groove = grooveRect(m_zone);
    setChannel(m_zone, qRound(255.0 * (pos.x() - groove.left()) / qMax(1, groove.width())));
    return true;
}

// --- PaletteBoxPage ---------------------------------------------------------

PaletteBoxPage::PaletteBoxPage(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, qOverload<>(&QWidget::update));
}

void PaletteBoxPage::setSwatches(const QList<QColor> &swatches)
{
    m_swatches = swatches;
    m_hover = -1;
    updateGeometry();
    update();
}

void PaletteBoxPage::setCurrentColor(const QColor &color)
{
    m_current = color;
    update();
}

bool PaletteBoxPage::addSwatch(const QColor &color)
{
    for (const QColor &existing : m_swatches) {
        if (existing.rgba() == color.rgba()) {
            return false;
        }
    }
    m_swatches.append(color);
    updateGeometry();
    update();
    return true;
}

PaletteBoxPage::Grid PaletteBoxPage::grid(int width) const
{
    Grid result;
    result.columns = qMax(1, (width + kSwatchGap) / (kSwatchMin + kSwatchGap));
    result.cell = qMax(kSwatchMin, (width - (result.columns - 1) * kSwatchGap) / result.columns);
    return result;
}

int PaletteBoxPage::heightForWidth(int width) const
{
    const Grid layout = grid(width);
    const int tiles = m_swatches.size() + 1;
    const int rows = (tiles + layout.columns - 1) / layout.columns;
    return rows * layout.cell + (rows - 1) * kSwatchGap;
}

QSize PaletteBoxPage::sizeHint() const
{
    const int w = width() > 0 ? width() : kPanelWidth - 2 * kPanelMarginH;
    return QSize(w, heightForWidth(w));
}

QRect PaletteBoxPage::tileRect(int index) const
{
    const Grid layout = grid(width());
    const int column = index % layout.columns;
    const int row = index / layout.columns;
    return QRect(column * (layout.cell + kSwatchGap), row * (layout.cell + kSwatchGap),
                 layout.cell, layout.cell);
}

int PaletteBoxPage::tileAt(const QPoint &pos) const
{
    const int tiles = m_swatches.size() + 1;
    for (int index = 0; index < tiles; ++index) {
        if (tileRect(index).contains(pos)) {
            return index;
        }
    }
    return -1;
}

void PaletteBoxPage::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    for (int index = 0; index < m_swatches.size(); ++index) {
        const QRect tile = tileRect(index);
        paintSwatchBody(painter, tile, m_swatches.at(index));
        if (m_current.isValid() && m_swatches.at(index).rgba() == m_current.rgba()) {
            painter.setPen(QPen(role(AnimeTheme::Role::Accent), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(tile).adjusted(-2.0, -2.0, 1.0, 1.0));
        }
    }

    // The trailing tile is the only affordance for a colour the box does not
    // hold yet, so it is dashed rather than filled: it stands for an absence.
    const QRect plus = tileRect(m_swatches.size());
    const bool hovered = m_hover == m_swatches.size();
    if (hovered) {
        painter.fillRect(plus, hoverWash());
    }
    QPen pen(hovered ? role(AnimeTheme::Role::Accent) : role(AnimeTheme::Role::Divider), 1);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(plus).adjusted(0.5, 0.5, -0.5, -0.5));

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(hovered ? role(AnimeTheme::Role::Accent) : role(AnimeTheme::Role::TextDim), 1.5));
    const QPoint center = plus.center();
    painter.drawLine(center.x() - 5, center.y(), center.x() + 5, center.y());
    painter.drawLine(center.x(), center.y() - 5, center.x(), center.y() + 5);
}

void PaletteBoxPage::mousePressEvent(QMouseEvent *event)
{
    const int index = tileAt(event->pos());
    if (index < 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (index == m_swatches.size()) {
        if (event->button() == Qt::LeftButton) {
            emit newColorRequested();
        }
        return;
    }
    if (event->button() == Qt::LeftButton) {
        emit swatchPicked(m_swatches.at(index));
        return;
    }
    if (event->button() == Qt::RightButton) {
        const QColor removed = m_swatches.takeAt(index);
        m_hover = -1;
        updateGeometry();
        update();
        emit swatchRemoved(removed);
    }
}

void PaletteBoxPage::mouseMoveEvent(QMouseEvent *event)
{
    const int index = tileAt(event->pos());
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PaletteBoxPage::leaveEvent(QEvent *event)
{
    m_hover = -1;
    update();
    QWidget::leaveEvent(event);
}

// --- PaletteTabStrip --------------------------------------------------------

PaletteTabStrip::PaletteTabStrip(const QStringList &labels, QWidget *parent)
    : QWidget(parent)
    , m_labels(labels)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(kTabHeight);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, qOverload<>(&QWidget::update));
}

int PaletteTabStrip::currentIndex() const
{
    return m_current;
}

void PaletteTabStrip::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_labels.size() || index == m_current) {
        return;
    }
    m_current = index;
    update();
}

QSize PaletteTabStrip::sizeHint() const
{
    return QSize(kPanelWidth - 2 * kPanelMarginH, kTabHeight);
}

QRect PaletteTabStrip::tabRect(int index) const
{
    if (m_labels.isEmpty()) {
        return QRect();
    }
    const int left = index * width() / m_labels.size();
    const int right = (index + 1) * width() / m_labels.size();
    return QRect(left, 0, right - left, height());
}

int PaletteTabStrip::tabAt(const QPoint &pos) const
{
    for (int index = 0; index < m_labels.size(); ++index) {
        if (tabRect(index).contains(pos)) {
            return index;
        }
    }
    return -1;
}

void PaletteTabStrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setFont(capsFont(this, 10, 0.8));

    for (int index = 0; index < m_labels.size(); ++index) {
        const QRect tab = tabRect(index);
        const bool active = index == m_current;
        if (active) {
            painter.fillRect(tab, accentField());
        } else if (index == m_hover) {
            painter.fillRect(tab, hoverWash());
        }
        painter.setPen(active ? role(AnimeTheme::Role::Text) : role(AnimeTheme::Role::TextDim));
        painter.drawText(tab, Qt::AlignCenter, m_labels.at(index));
        if (active) {
            painter.fillRect(QRect(tab.left(), tab.bottom() - 1, tab.width(), 2),
                             role(AnimeTheme::Role::Accent));
        }
        if (index > 0) {
            painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
            painter.drawLine(tab.left(), tab.top() + 1, tab.left(), tab.bottom() - 1);
        }
    }

    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

void PaletteTabStrip::mousePressEvent(QMouseEvent *event)
{
    const int index = tabAt(event->pos());
    if (event->button() != Qt::LeftButton || index < 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (index != m_current) {
        m_current = index;
        update();
    }
    emit tabSelected(index);
}

void PaletteTabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int index = tabAt(event->pos());
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PaletteTabStrip::leaveEvent(QEvent *event)
{
    m_hover = -1;
    update();
    QWidget::leaveEvent(event);
}

// --- PaletteCurrentRow ------------------------------------------------------

PaletteCurrentRow::PaletteCurrentRow(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kChipSize);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, qOverload<>(&QWidget::update));
}

void PaletteCurrentRow::setColor(const QColor &color)
{
    m_color = color;
    update();
}

QSize PaletteCurrentRow::sizeHint() const
{
    return QSize(kPanelWidth - 2 * kPanelMarginH, kChipSize);
}

void PaletteCurrentRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QRect chip(0, 0, kChipSize, kChipSize);
    paintSwatchBody(painter, chip, m_color);

    const int textLeft = chip.right() + 10;
    const QRect textRect(textLeft, 0, qMax(0, width() - textLeft), height());

    painter.setFont(monoFont(13, true));
    painter.setPen(role(AnimeTheme::Role::Text));
    painter.drawText(QRect(textRect.left(), textRect.top() + 4, textRect.width(), 15),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     m_color.name(QColor::HexRgb).toUpper());

    QFont channels = font();
    channels.setPixelSize(10);
    painter.setFont(channels);
    painter.setPen(role(AnimeTheme::Role::TextDim));
    painter.drawText(QRect(textRect.left(), textRect.top() + 20, textRect.width(), 14),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("R %1   G %2   B %3   A %4")
                         .arg(m_color.red())
                         .arg(m_color.green())
                         .arg(m_color.blue())
                         .arg(m_color.alpha()));
}

// --- PaletteNewColorPopup ---------------------------------------------------

PaletteNewColorPopup::PaletteNewColorPopup(const QColor &initial, QWidget *parent)
    : QWidget(parent, Qt::Popup)
    , m_color(initial)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(false);
    setFixedWidth(kPopupWidth);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPanelMarginH, kPopupHeader + 14, kPanelMarginH, kPanelMarginH);
    layout->setSpacing(kPanelSpacing);

    m_tabs = new PaletteTabStrip({QStringLiteral("Circle"), QStringLiteral("HSV"), QStringLiteral("RGB")}, this);
    layout->addWidget(m_tabs);

    m_pickers = {new PaletteCirclePicker(this), new PaletteHsvPicker(this), new PaletteRgbPicker(this)};
    for (PalettePicker *picker : m_pickers) {
        picker->setColor(m_color);
        layout->addWidget(picker);
        connect(picker, &PalettePicker::colorPreview, this, &PaletteNewColorPopup::setColor);
        connect(picker, &PalettePicker::colorCommitted, this, &PaletteNewColorPopup::setColor);
    }

    m_preview = new PaletteCurrentRow(this);
    m_preview->setColor(m_color);
    layout->addWidget(m_preview);

    m_addButton = new QPushButton(QStringLiteral("Add"), this);
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setMinimumHeight(28);
    layout->addWidget(m_addButton);

    connect(m_tabs, &PaletteTabStrip::tabSelected, this, &PaletteNewColorPopup::showPage);
    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        emit colorAccepted(m_color);
        close();
    });
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, &PaletteNewColorPopup::applyTheme);

    applyTheme();
    showPage(0);
}

void PaletteNewColorPopup::applyTheme()
{
    m_addButton->setStyleSheet(QStringLiteral("QPushButton {"
                                              " border: 1px solid %1;"
                                              " border-radius: 3px;"
                                              " padding: 5px 14px;"
                                              " color: %2;"
                                              " background: %1;"
                                              "}"
                                              "QPushButton:hover {"
                                              " background: %3;"
                                              " border-color: %3;"
                                              "}"
                                              "QPushButton:pressed {"
                                              " background: %4;"
                                              " border-color: %4;"
                                              "}")
                                   .arg(role(AnimeTheme::Role::Accent).name(),
                                        QStringLiteral("#ffffff"),
                                        role(AnimeTheme::Role::AccentHover).name(),
                                        role(AnimeTheme::Role::AccentActive).name()));
    update();
}

void PaletteNewColorPopup::showPage(int index)
{
    for (int i = 0; i < m_pickers.size(); ++i) {
        m_pickers.at(i)->setColor(m_color);
        m_pickers.at(i)->setVisible(i == index);
    }
    m_tabs->setCurrentIndex(index);
    adjustSize();
}

void PaletteNewColorPopup::setColor(const QColor &color)
{
    m_color = color;
    m_preview->setColor(color);
}

QRect PaletteNewColorPopup::closeRect() const
{
    return QRect(width() - kPanelMarginH - 20, (kPopupHeader - 20) / 2, 20, 20);
}

void PaletteNewColorPopup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));

    painter.setFont(capsFont(this, 11, 1.4));
    painter.setPen(role(AnimeTheme::Role::Text));
    painter.drawText(QRect(kPanelMarginH, 0, width() - 2 * kPanelMarginH - 20, kPopupHeader),
                     Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("New Color"));

    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.drawLine(0, kPopupHeader, width(), kPopupHeader);

    const QRect close = closeRect();
    painter.setPen(QPen(role(AnimeTheme::Role::TextDim), 1.5));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawLine(close.left() + 6, close.top() + 6, close.right() - 6, close.bottom() - 6);
    painter.drawLine(close.right() - 6, close.top() + 6, close.left() + 6, close.bottom() - 6);
}

void PaletteNewColorPopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && closeRect().contains(event->pos())) {
        close();
        return;
    }
    QWidget::mousePressEvent(event);
}

// --- PaletteControl ---------------------------------------------------------

PaletteControl::PaletteControl(const QJsonObject &control, QWidget *parent)
    : QWidget(parent)
{
    m_color = colorFromText(control.value(QStringLiteral("value")).toString(), QColor(0, 0, 0, 255));

    QList<QColor> swatches;
    const QJsonArray declared = control.value(QStringLiteral("swatches")).toArray();
    for (const QJsonValue &entry : declared) {
        const QColor parsed(entry.toString());
        if (parsed.isValid()) {
            swatches.append(parsed);
        }
    }

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPanelMarginH, kPanelMarginTop, kPanelMarginH, kPanelMarginBottom);
    layout->setSpacing(kPanelSpacing);

    m_currentRow = new PaletteCurrentRow(this);
    m_currentRow->setColor(m_color);
    layout->addWidget(m_currentRow);

    m_tabs = new PaletteTabStrip({QStringLiteral("Box"), QStringLiteral("Circle"),
                                  QStringLiteral("HSV"), QStringLiteral("RGB")}, this);
    layout->addWidget(m_tabs);

    m_box = new PaletteBoxPage(this);
    m_box->setSwatches(swatches);
    m_box->setCurrentColor(m_color);
    layout->addWidget(m_box);

    m_pickers = {new PaletteCirclePicker(this), new PaletteHsvPicker(this), new PaletteRgbPicker(this)};
    m_pages.append(m_box);
    for (PalettePicker *picker : m_pickers) {
        picker->setColor(m_color);
        layout->addWidget(picker);
        m_pages.append(picker);
        connect(picker, &PalettePicker::colorPreview, this, &PaletteControl::previewColor);
        connect(picker, &PalettePicker::colorCommitted, this, &PaletteControl::commitColor);
    }

    connect(m_tabs, &PaletteTabStrip::tabSelected, this, &PaletteControl::showPage);
    connect(m_box, &PaletteBoxPage::swatchPicked, this, &PaletteControl::commitColor);
    connect(m_box, &PaletteBoxPage::swatchRemoved, this, [this](const QColor &color) {
        emit boxEdited(QStringLiteral("remove:") + color.name(QColor::HexArgb));
    });
    connect(m_box, &PaletteBoxPage::newColorRequested, this, &PaletteControl::openNewColorPopup);
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, qOverload<>(&QWidget::update));

    // BOX first: the saved set is what a user reaches for, and it is the one
    // page whose height follows its content instead of a picker's minimum.
    showPage(0);
}

QColor PaletteControl::color() const
{
    return m_color;
}

QSize PaletteControl::sizeHint() const
{
    return QSize(kPanelWidth, QWidget::sizeHint().height());
}

void PaletteControl::showPage(int index)
{
    for (int i = 0; i < m_pages.size(); ++i) {
        // A hidden widget contributes nothing to a box layout, so switching
        // to BOX shrinks the panel back instead of holding a picker's height.
        m_pages.at(i)->setVisible(i == index);
    }
    for (PalettePicker *picker : m_pickers) {
        picker->setColor(m_color);
    }
    m_tabs->setCurrentIndex(index);
}

void PaletteControl::previewColor(const QColor &color)
{
    m_color = color;
    m_currentRow->setColor(color);
}

void PaletteControl::commitColor(const QColor &color)
{
    m_color = color;
    m_currentRow->setColor(color);
    m_box->setCurrentColor(color);
    for (PalettePicker *picker : m_pickers) {
        picker->setColor(color);
    }
    emit valueCommitted(color.name(QColor::HexArgb));
}

void PaletteControl::openNewColorPopup()
{
    if (m_popup) {
        return;
    }
    // Deferred by one turn of the event loop: the request comes from a mouse
    // press, and a popup that grabs the mouse mid-press would be handed the
    // release that ends the click on the "+" tile.
    QTimer::singleShot(0, this, [this]() {
        if (m_popup) {
            return;
        }
        PaletteNewColorPopup *popup = new PaletteNewColorPopup(m_color, this);
        m_popup = popup;
        connect(popup, &PaletteNewColorPopup::colorAccepted, this, &PaletteControl::acceptNewColor);

        QPoint anchor = mapToGlobal(QPoint(0, height()));
        popup->adjustSize();
        QScreen *screen = QGuiApplication::screenAt(anchor);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect available = screen->availableGeometry();
            anchor.setX(qBound(available.left(), anchor.x(), available.right() - popup->width()));
            anchor.setY(qBound(available.top(), anchor.y(), available.bottom() - popup->height()));
        }
        popup->move(anchor);
        popup->show();
    });
}

void PaletteControl::acceptNewColor(const QColor &color)
{
    // Value first: the pick has to reach the draw layer (and tool_colors) as a
    // colour before the box hears about it, because the rebuild that follows
    // the add reads the current colour back from there.
    commitColor(color);
    // The panel keeps its own visible set rather than waiting for the rebuild:
    // ui.refresh_tool_options() only rebuilds an EXTRA tool's panel, and the
    // palette lives on Pen and Fill.
    if (m_box->addSwatch(color)) {
        emit boxEdited(QStringLiteral("add:") + color.name(QColor::HexArgb));
    }
}

void PaletteControl::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), role(AnimeTheme::Role::Surface));
    painter.setPen(QPen(role(AnimeTheme::Role::Divider), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}
