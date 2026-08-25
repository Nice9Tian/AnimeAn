#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QHash>
#include <QImage>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class PaintViewContainer;
class QLineEdit;

// What a press on a piece of timeline chrome MEANS. The transport bar, the
// vertical strip's header and the floating panel all raise the same ids, so a
// command is wired once however the user reached it.
enum class TimelineCommand {
    AddFrame,
    AddHold,
    DeleteFrame,
    Onion,
    GuideLines,
    Prev,
    Pause,
    Play,
    Loop,
    Next,
    Rate,
    Orientation,
    Float,
    Collapse,
    Close
};

// Everything the chrome draws itself from. One struct rather than a setter per
// value: the bar and the strip show the same state from two angles, and a
// partial update of one of them is a way for them to disagree.
struct TimelineState {
    int frameCount = 1;
    int currentFrame = 0;
    // Per row, from the model's derived hold test. Empty means "all keys".
    QVector<bool> holds;
    bool playing = false;
    bool loop = true;
    int fps = 12;
    bool onion = false;
    bool guideLines = false;
    QSet<int> lanes;
    bool collapsed = false;
    // The strip's orientation, which also decides where the collapse chevron
    // points and whether the bar carries the frame commands.
    bool vertical = false;
    // Which side the vertical strip is on: the collapse control sits on the
    // transport's inner edge, so the bar has to know.
    bool leftAligned = false;
    bool floating = false;
};

// The transport: frame commands at the left, the onion pair + playback +
// rate + frame field as one centred assembly, window chrome at the right.
// Custom painted rather than a row of QPushButtons - the design is a flat
// 34px band of equal cells, which no button style reproduces.
class TimelineTransportBar : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineTransportBar(QWidget *parent = nullptr);

    void setState(const TimelineState &state);
    QSize sizeHint() const override;

signals:
    void commandTriggered(TimelineCommand command);
    void frameTyped(int frame);
    void fpsPicked(int fps);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Item {
        TimelineCommand command = TimelineCommand::Play;
        QRect rect;
        bool enabled = true;
        bool active = false;
    };

    void relayout();
    void showRateMenu(const QRect &anchor);
    int itemAt(const QPoint &pos) const;

    TimelineState m_state;
    QVector<Item> m_items;
    QVector<QRect> m_dividers;
    QLineEdit *m_frameField = nullptr;
    int m_hoverItem = -1;
    int m_pressedItem = -1;
};

// The frame cells, horizontally under the canvas or vertically beside it. In
// the vertical layout it also carries the "ANIMATION" header and the frame
// commands, because there is no bar left of the canvas to put them on.
class TimelineStrip : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineStrip(QWidget *parent = nullptr);

    void setState(const TimelineState &state);
    void setVertical(bool vertical);
    void setThumbnailProvider(std::function<QImage(int, QSize)> provider);
    void clearThumbnails();
    QSize sizeHint() const override;

signals:
    void frameActivated(int frame);
    void laneToggled(int frame, bool on);
    void moveFrameRequested(int from, int to);
    void commandTriggered(TimelineCommand command);
    // The vertical header is the strip's own grip: dragging it re-sides or
    // detaches the strip, and only the owner knows where the edges are.
    void headerDragged(const QPoint &globalPos);
    void headerReleased();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Cell {
        int frame = 0;
        bool hold = false;
        QRect rect;
        QRect lane;
    };

    void relayout();
    void clampScroll();
    int cellAt(const QPoint &pos) const;
    int dropTargetAt(const QPoint &pos) const;
    QRect commandRect(int index) const;
    QImage thumbnail(int frame, const QSize &size);

    TimelineState m_state;
    bool m_vertical = false;
    QVector<Cell> m_cells;
    QHash<int, QImage> m_thumbnails;
    std::function<QImage(int, QSize)> m_provider;
    int m_scroll = 0;
    int m_extent = 0;      // total length of the cell run along the axis
    int m_hoverCommand = -1;
    int m_pressedCommand = -1;
    int m_pressedCell = -1;
    bool m_dragging = false;
    bool m_headerDrag = false;
    QPoint m_pressPos;
};

// Layout 4: the whole transport plus the horizontal cells as their own
// frameless window. It owns no controls of its own beyond the title bar -
// the bar and the strip are the same instances, reparented.
class TimelineFloatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineFloatPanel(QWidget *parent = nullptr);

    // The widgets to stack under the title bar, top first.
    void setContentWidgets(QWidget *strip, QWidget *bar);

signals:
    void closeRequested();
    void titleDragged(const QPoint &globalPos);
    void titleReleased();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect closeRect() const;

    bool m_dragging = false;
    bool m_hoverClose = false;
    QPoint m_grabOffset;
};

// What is left of the timeline when it is closed: one chip at the foot of the
// canvas, so the window is never lost behind a menu.
class TimelineReopenPill : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineReopenPill(QWidget *parent = nullptr);

    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hover = false;
};

// The timeline itself: the docked bottom chrome AND the owner of every other
// piece. It holds no model or playback policy - it reports what the user did
// and redraws what it is told, exactly as the frame panel it replaces did.
class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Layout {
        TransportOnly,
        HorizontalStrip,
        VerticalStrip,
        Floating,
        Hidden
    };

    explicit TimelineWidget(PaintViewContainer *container, QWidget *parent = nullptr);

    void setFrameData(int frameCount, int currentFrame, const QVector<bool> &holds);
    void setPlaybackActive(bool active);
    void setFps(int fps);
    void setLoop(bool loop);
    void setOnionState(bool enabled, bool guides, const QSet<int> &lanes);
    void setThumbnailProvider(std::function<QImage(int, QSize)> provider);
    void clearThumbnails();

    bool timelineVisible() const;
    void setTimelineVisible(bool visible);

    // The cadence table, moved here from the frame panel: presets first, then
    // a WHOLE-string number. Whole-string on purpose - the preset titles start
    // with a digit, so a half-typed cadence would otherwise resolve to 1 fps,
    // which is a plausible-looking wrong answer instead of a refusal.
    static int fpsForText(const QString &text, int fallback);
    static QString textForFps(int fps);
    // What the bar's rate chip reads: the cadence, not the sentence.
    static QString shortTextForFps(int fps);

signals:
    void frameActivated(int frame);
    void addFrameRequested();
    void addHoldRequested();
    void deleteFrameRequested();
    void moveFrameRequested(int from, int to);
    void playRequested();
    void pauseRequested();
    void loopToggled(bool on);
    void fpsChanged(int fps);
    void prevRequested();
    void nextRequested();
    void onionToggled(bool on);
    void onionGuideToggled(bool on);
    void onionLaneToggled(int frame, bool on);
    void visibilityChanged(bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void handleCommand(TimelineCommand command);
    void applyLayout();
    void pushState();
    void positionPill();
    void loadSettings();
    void saveSettings();
    // Where a drag of the strip header or the panel title bar wants to land.
    // `threshold` is how far from every edge the pointer has to be before the
    // answer is Floating.
    Layout dropTargetFor(const QPoint &globalPos, bool *leftAligned, int threshold) const;
    Layout dropTargetFor(const QPoint &globalPos, bool *leftAligned) const;

    PaintViewContainer *m_container = nullptr;
    TimelineTransportBar *m_bar = nullptr;
    TimelineStrip *m_strip = nullptr;
    TimelineFloatPanel *m_panel = nullptr;
    TimelineReopenPill *m_pill = nullptr;
    TimelineState m_state;
    Layout m_layout = Layout::TransportOnly;
    // What Close and the pill restore; never Hidden or Floating.
    Layout m_dockedLayout = Layout::HorizontalStrip;
    bool m_leftAligned = false;
    bool m_applyingLayout = false;
};

#endif // TIMELINEWIDGET_H
