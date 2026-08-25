#ifndef TIMELINEWINDOW_H
#define TIMELINEWINDOW_H

#include <QDockWidget>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class PaintViewContainer;
class QLineEdit;
class QMainWindow;

// What a press on a piece of timeline chrome MEANS. The transport bar, the
// vertical strip's command row and the side title bar all raise the same ids,
// so a command is wired once however the user reached it.
enum class TimelineCommand {
    AddFrame,
    AddHold,
    Duplicate,
    DeleteFrame,
    Onion,
    OnionLines,
    OnionFills,
    OnionAmLayers,
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
    // Per row, from the model's frame names. A row past the end of this vector
    // reads as its own number, exactly as the model's default does.
    QVector<QString> names;
    bool playing = false;
    bool loop = true;
    int fps = 12;
    bool onion = false;
    // The three ghost content classes. Lines and fills start on: onion turned
    // on with nothing to show would read as a broken button.
    bool onionLines = true;
    bool onionFills = true;
    // A per-LAYER gate over the classes above: auto-mapping layers ghost their
    // content only while this is on. Starts on for the same reason.
    bool onionAmLayers = true;
    bool guideLines = false;
    // Whether the onion family applies to the board the strip is currently
    // showing. Onion renders on the MAIN view only, so when the timeline is
    // pointed at the child board the buttons dim and the lanes go dead rather
    // than quietly editing the other document's ghost set.
    bool onionAvailable = true;
    QSet<int> lanes;
    bool collapsed = false;
    // The strip's orientation, which also decides where the collapse chevron
    // points and whether the bar carries the frame commands.
    bool vertical = false;
    // Which side the vertical strip is docked on: the collapse control sits on
    // the transport's inner edge, so the bar has to know.
    bool leftAligned = false;
    bool floating = false;
};

// The transport: frame commands at the left, the onion family + playback +
// rate + frame field as one centred assembly, window chrome at the right.
// Custom painted rather than a row of QPushButtons - the design is a flat
// band of equal cells, which no button style reproduces.
//
// It is also the dock's TITLE BAR when the timeline is docked bottom or
// floating, so a press that hits no cell is ignored rather than swallowed:
// that is what lets QDockWidget start its own drag from the empty space.
class TimelineTransportBar : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineTransportBar(QWidget *parent = nullptr);

    void setState(const TimelineState &state);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

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
    // How many 34px bands the families currently need. A family is never split
    // across the fold, so this is 1 or 2 rather than a free-flowing wrap.
    int m_rows = 1;
};

// The frame cells, horizontally under the canvas or vertically beside it. In
// the vertical layout it also carries the frame commands, because there is no
// transport left of the canvas to put them on.
//
// Cell size is DERIVED from the widget's own cross-axis extent, so dragging
// the dock's splitter scales the run: the aspect rules (key cell 126:74,
// preview 4:3, hold sliver a fifth of a key) are what stay fixed.
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
    // Scrolls the current frame's cell back into the viewport. The playhead is
    // driven from outside (Play, Next, a typed frame) and the painted scroll
    // bar is not a grab handle, so nothing else can follow it.
    void ensureCurrentVisible();
    int cellAt(const QPoint &pos) const;
    int dropTargetAt(const QPoint &pos) const;
    // Where the cells start: the vertical layout reserves the command row
    // above them, the horizontal one starts at the top edge.
    int cellOrigin() const;
    QRect commandRect(int index) const;
    QString nameFor(int frame) const;
    QImage thumbnail(int frame, const QSize &size);

    TimelineState m_state;
    bool m_vertical = false;
    QVector<Cell> m_cells;
    QHash<int, QImage> m_thumbnails;
    std::function<QImage(int, QSize)> m_provider;
    int m_scroll = 0;
    int m_extent = 0;      // total length of the cell run along the axis
    // Cell metrics for the current widget size, recomputed by relayout.
    int m_keyExtent = 0;   // key cell length along the run
    int m_holdExtent = 0;  // hold sliver length along the run
    int m_cellThickness = 0;   // cell size across the run
    // Onion lane metrics for the current cell scale, recomputed by relayout
    // alongside the cell metrics: the band (which is also the click target),
    // the anchored-ghost dot and the run line between them all grow and shrink
    // with the cells, floored so the lane never fades to a hairline.
    int m_laneThickness = 0;
    qreal m_laneDotRadius = 0.0;
    qreal m_laneLine = 0.0;
    int m_hoverCommand = -1;
    int m_pressedCommand = -1;
    int m_pressedCell = -1;
    bool m_dragging = false;
    QPoint m_pressPos;
};

// The dock's title bar while the strip is docked left or right: the transport
// cannot ride there (its assembly is wider than the column), so the side gets
// a slim named bar and the transport stays under the canvas.
class TimelineSideTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineSideTitleBar(QWidget *parent = nullptr);

    QSize sizeHint() const override;

signals:
    void commandTriggered(TimelineCommand command);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect closeRect() const;

    bool m_hoverClose = false;
    bool m_pressedClose = false;
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

// The timeline itself: a real dock window whose CONTENT is the strip and whose
// TITLE BAR is the transport. Docking, floating, the drag preview and the
// resize splitter are then Qt's, not ours. It holds no model or playback
// policy - it reports what the user did and redraws what it is told.
class TimelineWindow : public QDockWidget
{
    Q_OBJECT

public:
    explicit TimelineWindow(PaintViewContainer *container, QMainWindow *mainWindow);

    // Called once the dock has been added to the main window: the stored area
    // is applied by re-docking, which only works from inside a dock layout.
    void restoreLayout();

    void setFrameData(int frameCount, int currentFrame, const QVector<bool> &holds);
    void setFrameNames(const QVector<QString> &names);
    void setPlaybackActive(bool active);
    void setFps(int fps);
    void setLoop(bool loop);
    // Arguments follow the family's VISUAL order: [Onion][LINE][FILL]
    // [AM LAYER][GUIDE LINE], then the lane set.
    void setOnionState(bool enabled, bool lines, bool fills, bool amLayers, bool guides,
                       const QSet<int> &lanes);
    // Onion is a main-board feature; the owner says whether the board the
    // strip is currently pointed at is that board.
    void setOnionAvailable(bool available);
    void setThumbnailProvider(std::function<QImage(int, QSize)> provider);
    void clearThumbnails();

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
    void duplicateFrameRequested();
    void deleteFrameRequested();
    void moveFrameRequested(int from, int to);
    void playRequested();
    void pauseRequested();
    void loopToggled(bool on);
    void fpsChanged(int fps);
    void prevRequested();
    void nextRequested();
    void onionToggled(bool on);
    void onionLinesToggled(bool on);
    void onionFillsToggled(bool on);
    void onionAmLayersToggled(bool on);
    void onionGuideToggled(bool on);
    void onionLaneToggled(int frame, bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    // QDockWidget emits nothing when an already-floating dock is dragged or
    // resized in place, so these two are the only record of where the user
    // left a floating timeline.
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void handleCommand(TimelineCommand command);
    // Records the current rect as the float geometry and schedules the write.
    // Deferred because a drag delivers one move event per mouse step and the
    // settings file is not a per-pixel journal.
    void rememberFloatGeometry();
    // Re-homes the transport and the title bar for the current dock area, and
    // shows or hides the strip for the collapsed flag.
    void applyLayout();
    // Asks the main window for the dock size the current mode wants. Only on
    // a mode change: at any other moment the size is the user's business.
    void applyDockExtent();
    void pushState();
    void positionPill();
    void loadSettings();
    void saveSettings();
    Qt::DockWidgetArea sideArea() const;

    PaintViewContainer *m_container = nullptr;
    QMainWindow *m_mainWindow = nullptr;
    TimelineTransportBar *m_bar = nullptr;
    TimelineStrip *m_strip = nullptr;
    TimelineSideTitleBar *m_sideTitle = nullptr;
    TimelineReopenPill *m_pill = nullptr;
    TimelineState m_state;
    // Where the dock last was. Kept alongside QDockWidget's own state because
    // a floating dock still has to remember which side it came from.
    Qt::DockWidgetArea m_area = Qt::BottomDockWidgetArea;
    // Which side the vertical layout goes back to. Kept separately because a
    // bottom-docked timeline has no side, and the orientation button still has
    // to answer "which one did you last use".
    Qt::DockWidgetArea m_sideArea = Qt::RightDockWidgetArea;
    // What the settings asked for. Separate from m_area because the owner
    // adds the dock to the bottom area first, which overwrites m_area before
    // restoreLayout ever runs.
    Qt::DockWidgetArea m_restoreArea = Qt::BottomDockWidgetArea;
    QRect m_floatGeometry;
    // The dock extent the user had before collapsing, so expanding gives back
    // the strip they sized rather than the default one.
    int m_expandedExtent = 0;
    bool m_restoreFloating = false;
    bool m_restoreVisible = true;
    bool m_applyingLayout = false;
    bool m_restoring = false;
    // A write is already queued for the geometry the drag is still changing.
    bool m_floatGeometryQueued = false;
};

#endif // TIMELINEWINDOW_H
