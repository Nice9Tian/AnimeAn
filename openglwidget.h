#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include "algorithm/animemodel.h"
#include "algorithm/scenehistory.h"
#include "algorithm/vectorlogic.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLineF>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QString>
#include <QVariant>
#include <QVector>

class QPainter;
class QTimer;

class PaintOpenGLWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    enum class Tool {
        Pen,
        Eraser,
        DeleteLine,
        // Trim to the crossings: removes the piece of a stroke between the
        // two places it crosses its neighbours on either side of the click.
        CutLine,
        Fill,
        Move,
        Arrow,
        // Bridge two snapped vertices with a new stroke. Pure mechanism here
        // (brush ring, hover/click forwarding, handle hints); the snapping
        // and the connection geometry live in pyfile/connect_tool.py.
        Connect
    };

    enum class FillScope {
        CurrentLayer,
        AllLayers
    };

    struct ImportedVectorStroke {
        QVector<QPointF> points;
        QPainterPath path;
        QColor color = Qt::black;
        qreal width = 3.0;
    };

    struct ImportedVectorFrame {
        int row = 0;
        QVector<ImportedVectorStroke> strokes;
    };

    struct OverlayItem {
        QString id;
        QVector<QPointF> points;
        bool closed = false;
        QColor strokeColor = QColor(0, 0, 0, 255);
        QColor fillColor = QColor(0, 0, 0, 0);
        qreal width = 3.0;
        // Qt::PenStyle as an int, same convention as AnimeVectorStroke.
        int penStyle = 1;
        bool removable = true;
    };

    // Draggable edit handle, drawn at constant SCREEN size above everything.
    // What a handle means lives in Python; this widget only renders it,
    // hit-tests it and reports presses and drags ("handle" hook events).
    struct EditHandle {
        QString id;
        QPointF pos;      // document coordinates
        int shape = 0;    // 0 square, 1 circle, 2 diamond, 3 accept, 4 delete
        QColor color = QColor(255, 255, 255, 255);
        // Display-only markers (snap hints) render but never hit-test: a
        // hint that swallowed the very click it advertised armed nothing.
        bool interactive = true;
    };

    explicit PaintOpenGLWidget(QWidget *parent = nullptr);

    void setOverlayItems(const QVector<OverlayItem> &items);
    void setEditHandles(const QVector<EditHandle> &handles);

    void setViewName(const QString &name);
    QString viewName() const;
    void setActiveIndicator(bool active);
    qreal zoom() const;
    QPointF panOffset() const;
    void setScrollPosition(int horizontal, int vertical);
    // What sits behind the drawing. A VIEW preference, not a document one: it
    // is not saved with the project, and the texture export suppresses it so
    // the choice never reaches a file.
    enum class BackgroundMode { White, Black, Transparent };
    void setBackgroundMode(BackgroundMode mode);
    BackgroundMode backgroundMode() const;

    // Content lock. When editing is off, only strokes whose active property is
    // in the allowlist may be drawn or erased - the generic half: C++ enforces
    // "these properties only", Python says WHICH (setEditablePropertyFilter).
    void setContentEditable(bool editable);
    bool contentEditable() const;
    void setEditablePropertyFilter(const QStringList &properties);
    // True when the tool currently armed is allowed to modify this view.
    bool editingAllowed() const;

    void setUnboundedCanvas(bool unbounded);
    bool unboundedCanvas() const;
    // The page, in document coordinates. Comes from the scene's canvas size,
    // NOT from the widget geometry.
    QRectF documentRect() const;
    void setCanvasSize(const QSize &size);
    // Call after copy-assigning model(): the page may have changed with it.
    void modelReplaced();
    // Outer wall for a bucket fill, in document space.
    QRectF fillBoundsRect() const;
    // Timeline playback: every frame is rendered to pixels up front, then the
    // cached images are blitted per tick so playback never pays the vector
    // drawing cost. Vector rendering resumes when playback ends.
    bool buildPlaybackCache(int frameCount, QString *error);
    void showPlaybackFrame(int index);
    void endPlayback();
    bool playbackActive() const;
    void setPenColor(const QColor &color);
    void setDrawingColor(const QColor &color);
    void setPenWidth(qreal width);
    void setStrokeProperty(const QString &property);
    void sendPythonExtraToolMessage(const QString &name, const QString &property);
    void sendPythonToolOptionMessage(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn);
    void sendPythonPadMessage(const QString &pad, const QString &phase, double x, double y);
    // "visibility" hook event: Python decides what a layer-visibility toggle
    // means (and applies it through the bindings). Returns true when a hook
    // set message["handled"]; the caller then skips its own default action.
    bool sendPythonLayerVisibilityMessage(int layerIndex, bool visible);
    // Generic dispatch for script-defined view buttons ("viewbutton" event);
    // the button semantics live entirely in Python.
    void sendPythonViewButtonMessage(const QString &name, bool on);
    // Generic dispatch for a script-defined menu-bar entry ("menu" event).
    void sendPythonMenuMessage(const QString &menu, const QString &item, bool checked);
    // Generic dispatch for a script-defined layer-panel context menu entry.
    void sendPythonLayerMenuMessage(const QString &action,
                                    int groupId,
                                    const QString &groupName,
                                    int layerIndex,
                                    const QString &layerName,
                                    const QVector<int> &memberLayers);
    void setTool(Tool tool);
    Tool tool() const;
    void setFillScope(FillScope scope);
    // The realtime stabilizer strength (0-100). Named "smooth" in the tool
    // panel, where it is the ONLY drawing knob; it no longer touches how the
    // committed stroke is fitted - that is setStrokeFitSettings.
    void setSmoothValue(int value);
    void setStrokeFitSettings(const AnimeStrokeFitSettings &settings);
    AnimeStrokeFitSettings strokeFitSettings() const;
    void setAxisSnapThreshold(qreal threshold);
    qreal axisSnapThreshold() const;
    SceneHistory &history();
    const SceneHistory &history() const;
    void commitHistory(const QString &label);
    void resetHistory(const QString &label);
    void dropRedoTail();
    bool undoHistory();
    bool redoHistory();
    bool goToHistory(int index);
    void setCurrentLayer(int layerIndex);
    void setCurrentFrame(int frameIndex);
    int layerCount() const;
    int frameCount() const;
    int assetCount() const;
    QString layerName(int layerIndex) const;
    QString frameName(int frameIndex) const;
    QString assetName(int assetIndex) const;
    int importRasterLayer(const QImage &image, const QString &layerName);
    int importVectorLineLayer(const QVector<ImportedVectorFrame> &frames, const QString &layerName);
    int addLayer();
    bool deleteLayer(int layerIndex);
    // Deletes a layer group and everything inside it, as one history entry.
    int deleteLayerGroup(int groupId);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    // Appends a frame that HOLDS the last one: same cells, so one drawing.
    int addHoldFrame();
    bool deleteFrame(int frameIndex);
    bool moveFrame(int fromIndex, int toIndex);
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector, const QString &name = QString());
    void setCurrentAsset(int assetIndex);
    bool assignAssetToLayer(int layerIndex, int assetIndex);
    int addLayerForAsset(int assetIndex);
    AnimeSceneModel &model();
    const AnimeSceneModel &model() const;

signals:
    void layerListChanged(int selectedLayer);
    void assetListChanged(int selectedAsset);
    void pythonDebugMessage(const QString &message);
    void focusGained();
    void historyChanged();
    void historyCommitted();
    void viewTransformChanged();
    void playbackInterrupted();

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

public:
    using VectorStroke = AnimeVectorStroke;
    using VectorGroupId = AnimeVectorGroupId;
    using VectorStrokeNode = AnimeVectorStrokeNode;
    using VectorImageModel = AnimeVectorImageModel;

private:
    struct OverlayHandle {
        QString id;
        QColor badgeColor;
        QRectF rect;
        // Union of every part drawn under this id, and whether the badge
        // follows it (closed items) or an end point (open ones).
        QRectF extent;
        bool anchorIsExtent = false;
    };

    enum class AxisSnapState {
        Inactive,
        Pending,
        Horizontal,
        Vertical
    };

    void paintSceneContent(QPainter &painter, int frameIndex, bool includeCurrentStroke);
    // Re-render the playback cache for the current view, once the gesture that
    // changed it has settled.
    void schedulePlaybackCacheRefresh();
    void paintOverlayItems(QPainter &painter);
    // Badge just above-right of `anchor` (an item's end point), clamped into view.
    QRectF overlayHandleRect(const QPointF &anchor) const;
    bool removeOverlayItemAt(const QPointF &pos);
    void sendOverlayRemoveMessage(const QString &overlayId);
    void paintEditHandles(QPainter &painter);
    // Topmost handle whose SCREEN-space box contains the screen position.
    QString editHandleAt(const QPointF &screenPos) const;
    // "handle" hook event. Phases: "pick" (arrow click on no handle),
    // "press"/"move"/"release" (a handle drag), "view" (zoom changed while
    // handles are shown). Carries the document position and the current zoom.
    void sendPythonHandleMessage(const QString &phase, const QString &handleId, const QPointF &pos);
    void resetAxisSnap(Qt::KeyboardModifiers modifiers, const QPointF &anchor);
    QPointF applyAxisSnap(Qt::KeyboardModifiers modifiers, const QPointF &point, bool *retroChanged);
    void paintBackground(QPainter &painter, const QRectF &target) const;
    QPointF mapToDocument(const QPointF &screenPos) const;
    void clampPan();
    void notifyViewTransformChanged();
    void updateCurrentStroke();
    void finishCurrentStroke();
    // Dispatches the hook event; returns true when a hook set
    // "cancel_history" in the message, vetoing the follow-up history commit.
    bool pythonHookSendMessage(const QString &event, const QPointF &pos = QPointF(), const QPointF &delta = QPointF(), bool changed = true, int strokeIndex = -1);
    // "fillrequest" hook event: offers the click to Python BEFORE the built-in
    // fill runs. Returns true when a hook set message["handled"], i.e. the
    // fill policy ran in Python and the default C++ behavior must not.
    bool sendPythonFillRequestMessage(const QPointF &pos);
    bool eraseAt(const QPointF &pos);
    bool eraseBetween(const QPointF &from, const QPointF &to);
    bool deleteLineAt(const QPointF &pos);
    bool deleteLineBetween(const QPointF &from, const QPointF &to);
    // CutMode: trims the stroke under `pos` back to its crossings with the
    // OTHER strokes of the same layer.
    bool cutLineAt(const QPointF &pos);
    // The single stroke a brush stroke from `from` to `to` is aimed at:
    // within reach of the radius, and nearest the brush's own centre line.
    // `from == to` for a click. -1 when nothing is in reach.
    int nearestStrokeToBrush(const VectorImageModel *image,
                             const QPointF &from,
                             const QPointF &to) const;
    bool fillAt(const QPointF &pos);
    bool moveCurrentLayerBy(const QPointF &delta);
    bool currentLayerAcceptsFill() const;
    QVector<QLineF> fillGraphSegments(FillScope scope, int layerIndex) const;
    QPainterPath vectorRegionPathAt(const QPointF &seed, FillScope scope, int layerIndex) const;
    void removeInvalidFillRegions();
    bool eraseStrokeAt(int strokeIndex, const QPointF &pos);
    bool eraseStrokeBetween(int strokeIndex, const QPointF &from, const QPointF &to);
    VectorStroke makeStroke(const QVector<QPointF> &points, const QColor &color, qreal width, int id = 0, bool filterInput = true, bool smoothPath = true) const;
    bool appendPoint(const QPointF &point);
    VectorImageModel *currentImage(bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;

    Tool m_tool = Tool::Pen;
    FillScope m_fillScope = FillScope::CurrentLayer;
    QString m_viewName = QStringLiteral("main");
    bool m_activeIndicator = false;
    QColor m_penColor = Qt::black;
    QVector<QPointF> m_points;
    // Realtime input conditioning: 1 Euro filter with lag compensation over
    // the raw samples of the CURRENT stroke, plus the raw pen tip so paintGL
    // can draw the unfiltered predictive preview at the very front.
    AnimeOneEuroFilter m_inputFilter;
    QPointF m_rawPenPos;
    bool m_hasRawPenPos = false;
    AnimeSceneModel m_model;
    VectorStroke m_currentStroke;
    bool m_hasCurrentStroke = false;
    QPointF m_hoverPos;
    bool m_hasHoverPos = false;
    QPointF m_lastEraserPos;
    bool m_hasLastEraserPos = false;
    QPointF m_lastMovePos;
    bool m_hasLastMovePos = false;
    qreal m_penWidth = 5.0;
    QString m_strokeProperty;
    QString m_activePythonTool;
    bool m_eraseGestureChanged = false;
    bool m_moveGestureChanged = false;
    qreal m_eraserRadius = 12.0;
    qreal m_minPointDistance = 2.0;
    int m_smoothValue = 50;              // stabilizer strength
    AnimeStrokeFitSettings m_fitSettings; // how a committed stroke is fitted
    // "update" fires on every accepted point while drawing; hooks get at most
    // one dispatch per this interval so subscribers never run at tablet rate.
    QElapsedTimer m_updateHookThrottle;
    static constexpr qint64 kUpdateHookIntervalMs = 33;
    // Connect-tool hover forwarding shares the same cadence: Python resolves
    // the snap hint per event, so the interpreter must not run at mouse rate.
    QElapsedTimer m_hoverHookThrottle;
    // The live-preview hybrid fit runs at display cadence, not event cadence
    // (a per-event whole-stroke refit is quadratic in stroke length).
    QElapsedTimer m_liveFitThrottle;
    static constexpr qint64 kLiveFitIntervalMs = 16;
    // Incremental-fit state of the active stroke: the frozen prefix that is
    // never recomputed, so the already-drawn path cannot tremble.
    AnimeLiveFitState m_liveFit;
    // Document px per screen px, captured at pen-down: the fit's budgets are
    // screen-sized (tremor, report rate, the eye), converted once per stroke.
    qreal m_liveFitPixelScale = 1.0;
    AxisSnapState m_axisSnapState = AxisSnapState::Inactive;
    QPointF m_axisSnapAnchor;
    int m_axisSnapAnchorIndex = 0;
    qreal m_axisSnapThreshold = 5.0;
    qreal m_zoom = 1.0;
    QPointF m_panOffset;
    bool m_panning = false;
    QVector<EditHandle> m_editHandles;
    // Id of the handle being dragged; empty when no drag is in flight.
    QString m_activeHandleDrag;
    bool m_unboundedCanvas = false;
    BackgroundMode m_backgroundMode = BackgroundMode::White;
    bool m_contentEditable = true;
    QStringList m_editableProperties;
    QPointF m_lastPanPos;
    SceneHistory m_history;
    QVector<QImage> m_playbackFrames;
    int m_playbackIndex = -1;
    bool m_playbackActive = false;
    // The view the cached frames were rendered for. paintGL maps them from
    // this onto the current view, so zoom/scroll/resize during playback are
    // re-mapped rather than fatal; the timer re-renders once the gesture ends.
    QPointF m_playbackCachePan;
    qreal m_playbackCacheZoom = 1.0;
    int m_playbackCacheFrameCount = 0;
    QTimer *m_playbackCacheTimer = nullptr;
    bool m_swallowNextPress = false;
    QVector<OverlayItem> m_overlayItems;
    QVector<OverlayHandle> m_overlayHandles;
};

#endif // OPENGLWIDGET_H
