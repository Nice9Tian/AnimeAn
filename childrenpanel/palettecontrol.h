#ifndef PALETTECONTROL_H
#define PALETTECONTROL_H

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QPushButton;

namespace AnimePalette {

// HSV is the authority here, not RGB. A colour at zero saturation or zero
// value has no hue left to read back, so a drag that passes through black or
// grey would reset the wheel behind the user's hand. Every picker edits this
// and converts on the way out.
struct Hsva {
    int hue = 0;        // 0-359
    qreal sat = 0.0;    // 0-1
    qreal val = 0.0;    // 0-1
    int alpha = 255;

    QColor toColor() const;
    // Keeps the current hue (and saturation) where the incoming colour has
    // none of its own.
    void setColor(const QColor &color);
    void setRgb(int r, int g, int b);
};

}

// One picking surface. Subclasses paint their own geometry and answer where a
// press landed; the drag bookkeeping and the preview/commit split live here,
// because "the chip follows the drag, Python hears the release" is a rule of
// the control, not of any one picker.
class PalettePicker : public QWidget
{
    Q_OBJECT

public:
    explicit PalettePicker(QWidget *parent = nullptr);

    QColor color() const;
    // Programmatic: repaints, emits nothing.
    void setColor(const QColor &color);

signals:
    void colorPreview(const QColor &color);
    void colorCommitted(const QColor &color);

protected:
    // True when `pos` lands in an interactive zone. On `begin` the zone is
    // latched, so a drag that wanders out of its groove keeps driving the
    // channel it started on instead of jumping to a neighbour.
    virtual bool applyAt(const QPoint &pos, bool begin) = 0;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    AnimePalette::Hsva m_color;
    int m_zone = -1;

private:
    bool m_dragging = false;
};

// Hue ring around an inscribed SV square.
class PaletteCirclePicker : public PalettePicker
{
    Q_OBJECT

public:
    explicit PaletteCirclePicker(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool applyAt(const QPoint &pos, bool begin) override;

private:
    QRectF ringRect() const;
    QRect squareRect() const;
};

// SV square over a horizontal hue strip and a horizontal alpha strip.
class PaletteHsvPicker : public PalettePicker
{
    Q_OBJECT

public:
    explicit PaletteHsvPicker(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool applyAt(const QPoint &pos, bool begin) override;

private:
    void zoneRects(QRect *square, QRect *hue, QRect *alpha) const;
};

// R/G/B/A tracks: each groove is the channel's own ramp with the other three
// held, so the groove shows the colours the drag will actually produce.
class PaletteRgbPicker : public PalettePicker
{
    Q_OBJECT

public:
    explicit PaletteRgbPicker(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool applyAt(const QPoint &pos, bool begin) override;

private:
    QRect rowRect(int index) const;
    QRect grooveRect(int index) const;
    QRect readoutRect(int index) const;
    int channel(int index) const;
    void setChannel(int index, int value);
};

// The saved set: a wrapping grid of swatches plus the trailing "+" tile.
class PaletteBoxPage : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteBoxPage(QWidget *parent = nullptr);

    void setSwatches(const QList<QColor> &swatches);
    void setCurrentColor(const QColor &color);
    // False when the colour is already saved - the caller must not report an
    // add that did not happen.
    bool addSwatch(const QColor &color);

    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void swatchPicked(const QColor &color);
    void swatchRemoved(const QColor &color);
    void newColorRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Grid {
        int columns = 1;
        int cell = 24;
    };
    Grid grid(int width) const;
    QRect tileRect(int index) const;
    int tileAt(const QPoint &pos) const;

    QList<QColor> m_swatches;
    QColor m_current;
    int m_hover = -1;
};

// The BOX | CIRCLE | HSV | RGB strip. The active tab takes the accent as a
// field, which is the only place the strip differs between the two themes.
class PaletteTabStrip : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteTabStrip(const QStringList &labels, QWidget *parent = nullptr);

    int currentIndex() const;
    void setCurrentIndex(int index);

    QSize sizeHint() const override;

signals:
    void tabSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect tabRect(int index) const;
    int tabAt(const QPoint &pos) const;

    QStringList m_labels;
    int m_current = 0;
    int m_hover = -1;
};

// The chip, the hex and the channel readout.
class PaletteCurrentRow : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteCurrentRow(QWidget *parent = nullptr);

    void setColor(const QColor &color);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color = QColor(0, 0, 0, 255);
};

// The "+" window: the same three pickers, a preview and Add. It is a
// Qt::Popup, so an outside click dismisses it without a decision, which is
// what "cancel" means for a picker.
class PaletteNewColorPopup : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteNewColorPopup(const QColor &initial, QWidget *parent = nullptr);

signals:
    void colorAccepted(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyTheme();
    void showPage(int index);
    void setColor(const QColor &color);
    QRect closeRect() const;

    PaletteTabStrip *m_tabs = nullptr;
    QList<PalettePicker *> m_pickers;
    PaletteCurrentRow *m_preview = nullptr;
    QPushButton *m_addButton = nullptr;
    QColor m_color;
};

// The tool-option control. Its value hook is whatever the JSON declares
// ("color", so the existing C++ arm-then-paint path applies the pick); the
// saved set travels separately on "palette_box", because which colours EXIST
// is a policy question C++ has no answer for.
class PaletteControl : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteControl(const QJsonObject &control, QWidget *parent = nullptr);

    QColor color() const;
    QSize sizeHint() const override;

signals:
    // Fires on a swatch click and on the release of a drag - never during
    // one. A live drag only moves the chip.
    void valueCommitted(const QString &argb);
    // "add:#AARRGGBB" / "remove:#AARRGGBB".
    void boxEdited(const QString &action);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void showPage(int index);
    void previewColor(const QColor &color);
    void commitColor(const QColor &color);
    void openNewColorPopup();
    void acceptNewColor(const QColor &color);

    PaletteCurrentRow *m_currentRow = nullptr;
    PaletteTabStrip *m_tabs = nullptr;
    PaletteBoxPage *m_box = nullptr;
    QList<QWidget *> m_pages;
    QList<PalettePicker *> m_pickers;
    QPointer<PaletteNewColorPopup> m_popup;
    QColor m_color = QColor(0, 0, 0, 255);
};

#endif // PALETTECONTROL_H
