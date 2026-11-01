#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include "algorithm/core/json.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "algorithm/animemodel.h"
#include "algorithm/scenehistory.h"
#include "algorithm/vectorlogic.h"
#include "algorithm/viewscale.h"

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"
#include "backend/platform/elapsedtimer.h"
#include "backend/platform/signalslot.h"
#include "backend/platform/timer.h"
#include "boardbase.h"

class PaintOpenGLWidget : public PaintBoardBase
{
public:
    enum class Tool {
        Pen,
        Eraser,
        DeleteLine,
        // Trim to the crossings: removes the piece of a stroke between the
        // two places it crosses its neighbours on either side of the click.
        CutLine,
        Fill,
        Arrow,
        // Bridge two snapped vertices with a new stroke. Pure mechanism here
        // (brush ring, hover/click forwarding, handle hints); the snapping
        // and the connection geometry live in pyfile/connect_tool.py.
        Connect,
        // Free transform. Same pure mechanism as Arrow/Connect: handles are
        // rendered and hit-tested here and every press/drag/hover is reported
        // to Python; the box, its 8 handles, the rotation ring outside the
        // corners, the scale/stretch/translate/rotate maths, the modifier
        // constraints and which pointer belongs to which region all live in
        // pyfile/transfer_tool.py.
        Transfer
    };

    enum class FillScope {
        CurrentLayer,
        AllLayers
    };

    struct ImportedVectorStroke {
        AnimeCore::Vector<AnimeCore::PointF> points;
        AnimeCore::Path path;
        AnimeCore::Color color = AnimeCore::Color(0, 0, 0);
        double width = 3.0;
    };

    struct ImportedVectorFrame {
        int row = 0;
        AnimeCore::Vector<ImportedVectorStroke> strokes;
    };

    struct OverlayItem {
        AnimeCore::String id;
        AnimeCore::Vector<AnimeCore::PointF> points;
        bool closed = false;
        AnimeCore::Color strokeColor = AnimeCore::Color(0, 0, 0, 255);
        AnimeCore::Color fillColor = AnimeCore::Color(0, 0, 0, 0);
        double width = 3.0;
        // PenStyle as an int, same convention as AnimeVectorStroke.
        int penStyle = 1;
        bool removable = true;
        bool confirmable = false;
        // A draggable item reports press/move/release through the "handle"
        // hook events with its id, like an edit handle; Python owns meaning.
        bool draggable = false;
    };

    // Draggable edit handle, drawn at constant SCREEN size above everything.
    // What a handle means lives in Python; this widget only renders it,
    // hit-tests it and reports presses and drags ("handle" hook events).
    struct EditHandle {
        AnimeCore::String id;
        AnimeCore::PointF pos;      // document coordinates
        int shape = 0;              // 0 square, 1 circle, 2 diamond, 3 accept, 4 delete
        AnimeCore::Color color = AnimeCore::Color(255, 255, 255, 255);
        // Display-only markers (snap hints) render but never hit-test: a
        // hint that swallowed the very click it advertised armed nothing.
        bool interactive = true;
    };

    explicit PaintOpenGLWidget(void *parent = nullptr);

    void setOverlayItems(const AnimeCore::Vector<OverlayItem> &items);
    void setEditHandles(const AnimeCore::Vector<EditHandle> &handles);
    // The pointer a script asks for by NAME ("size_all", "rotate", ""); which
    // affordance sits under the cursor is the tool's decision, drawing the
    // name is this widget's. Empty restores the default. The pen ignores it:
    // its ring IS its cursor. Cleared by setTool, so a tool switch can never
    // leave another tool's pointer behind.
    void setScriptCursor(const AnimeCore::String &name);

    void setViewName(const AnimeCore::String &name);
    AnimeCore::String viewName() const;
    void setActiveIndicator(bool active);
    double zoom() const;
    AnimeCore::PointF panOffset() const;
    void setScrollPosition(int horizontal, int vertical);
    // Moves the view PROGRAMMATICALLY: the zoom is clamped to the same limits
    // the wheel enforces and the pan to the same reachable area, so a restored
    // viewpoint can never land somewhere a gesture could not. Deliberately
    // silent on userTransformed() - only a hand on the wheel, the middle button
    // or a scroll bar counts as manual.
    void setViewTransform(double zoom, const AnimeCore::PointF &pan);
    // Frames the current frame's visible artwork. `cover` fills the viewport -
    // the content spans it edge to edge and the shorter axis crops (bleed);
    // false fits the whole of it inside instead. False when there is nothing to
    // frame, or the widget has no size yet (a fit measured against an unlaid-out
    // widget frames the wrong rectangle).
    bool fitViewToContent(bool cover);
    // The union of the VISIBLE, non-internal layers' content on the current
    // frame - what a fit frames. Null when nothing is showing. Deliberately not
    // reachableRect(): that one answers reachability (page + everything drawn,
    // hidden layers included, plus half a viewport of slack) and would frame
    // empty paper.
    AnimeCore::RectF visibleContentBounds() const;
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
    void setEditablePropertyFilter(const AnimeCore::StringList &properties);
    // True when the tool currently armed is allowed to modify this view.
    bool editingAllowed() const;

    // Fill-paint mode. A Fill column refuses the pen and the eraser (it has no
    // strokes for either to work on); this relaxes that ban for the two
    // GESTURE paths only, and makes the eraser rub out fill REGIONS under the
    // brush. Generic on purpose: C++ knows how to obey, and WHEN a board
    // deserves it - which layer is current, what the pen means there - is
    // policy and lives in pyfile/layer_tool_policy.py.
    void setFillPaintMode(bool on);
    bool fillPaintMode() const;

    void setUnboundedCanvas(bool unbounded);
    bool unboundedCanvas() const;
    // The page, in document coordinates. Comes from the scene's canvas size,
    // NOT from the widget geometry.
    AnimeCore::RectF documentRect() const;
    // The document area the view is allowed to travel over: the page UNIONED
    // with whatever is drawn on this frame, plus a margin. The pan clamp and
    // the scroll bars both take their range from this one definition - they
    // used to derive it separately from the page alone, which pinned the view
    // inside the paper and made anything drawn past its edge unreachable, and
    // therefore uneditable, as soon as the canvas was zoomed in.
    AnimeCore::RectF reachableRect() const;
    void setCanvasSize(const AnimeCore::Size &size);
    // Call after copy-assigning model(): the page may have changed with it.
    void modelReplaced();
    // Outer wall for a bucket fill, in document space.
    AnimeCore::RectF fillBoundsRect() const;
    // Timeline playback: every frame is rendered to pixels up front, then the
    // cached images are blitted per tick so playback never pays the vector
    // drawing cost. Vector rendering resumes when playback ends.
    bool buildPlaybackCache(int frameCount, AnimeCore::String *error);
    void showPlaybackFrame(int index);
    void endPlayback();
    bool playbackActive() const;
    // Onion skin. Pure mechanism: WHICH frames are ghosted is the timeline's
    // decision (its lane), and which stroke properties count as guide lines
    // rather than artwork is Python's (setOnionExcludeProperties).
    void setOnionEnabled(bool enabled);
    bool onionEnabled() const;
    void setOnionFrames(const AnimeCore::Set<int> &frames);
    AnimeCore::Set<int> onionFrames() const;
    void setOnionGuideLines(bool include);
    bool onionGuideLines() const;
    // The other two ghost content classes, independent of the guide flag:
    // LINES are the painting strokes (and the raster artwork that reads as
    // drawing), FILLS the fill regions. The CURRENT frame is never affected.
    void setOnionLines(bool include);
    bool onionLines() const;
    void setOnionFills(bool include);
    bool onionFills() const;
    // A per-LAYER gate that multiplies the three content classes above: with
    // it off, every column inside a group carrying the onion layer tag drops
    // out of the ghosts wholesale - its strokes and its fills alike - while
    // untagged columns are untouched. Mechanism only: WHICH tag marks such a
    // layer is Python's (setOnionLayerTag), exactly as with the guide
    // properties, so C++ never learns the word "automapping".
    void setOnionAmLayers(bool include);
    bool onionAmLayers() const;
    void setOnionExcludeProperties(const AnimeCore::StringList &properties);
    AnimeCore::StringList onionExcludeProperties() const;
    void setOnionLayerTag(const AnimeCore::String &tag);
    AnimeCore::String onionLayerTag() const;
    // One frame rendered offscreen at thumbnail size: the page scaled to fit
    // on white, the artwork on top, nothing else. Plain Painter, so it needs
    // no GL context and works for a view that has never been shown.
    AnimeCore::Image renderFrameThumbnail(int frameIndex, const AnimeCore::Size &size);
    void setPenColor(const AnimeCore::Color &color);
    void setDrawingColor(const AnimeCore::Color &color);
    void setPenWidth(double width);
    void setStrokeProperty(const AnimeCore::String &property);
    void sendPythonExtraToolMessage(const AnimeCore::String &name, const AnimeCore::String &property);
    void sendPythonToolOptionMessage(const AnimeCore::String &hook, const AnimeCore::String &name, const AnimeCore::String &type, const AnimeCore::JsonVariant &value, int row, int startColumn, int endColumn);
    void sendPythonPadMessage(const AnimeCore::String &pad, const AnimeCore::String &phase, double x, double y);
    // "visibility" hook event: Python decides what a layer-visibility toggle
    // means (and applies it through the bindings). Returns true when a hook
    // set message["handled"]; the caller then skips its own default action.
    bool sendPythonLayerVisibilityMessage(int layerIndex, bool visible);
    // Generic dispatch for script-defined view buttons ("viewbutton" event);
    // the button semantics live entirely in Python.
    void sendPythonViewButtonMessage(const AnimeCore::String &name, bool on);
    // Generic dispatch for a script-defined menu-bar entry ("menu" event).
    void sendPythonMenuMessage(const AnimeCore::String &menu, const AnimeCore::String &item, bool checked);
    // Generic dispatch for a script-defined layer-panel context menu entry.
    void sendPythonLayerMenuMessage(const AnimeCore::String &action,
                                    int groupId,
                                    const AnimeCore::String &groupName,
                                    int layerIndex,
                                    const AnimeCore::String &layerName,
                                    const AnimeCore::Vector<int> &memberLayers);
    void setTool(Tool tool);
    Tool tool() const;
    void setFillScope(FillScope scope);
    // The realtime stabilizer strength (0-100). Named "smooth" in the tool
    // panel, where it is the ONLY drawing knob; it no longer touches how the
    // committed stroke is fitted - that is setStrokeFitSettings.
    void setSmoothValue(int value);
    void setStrokeFitSettings(const AnimeStrokeFitSettings &settings);
    AnimeStrokeFitSettings strokeFitSettings() const;
    void setAxisSnapThreshold(double threshold);
    double axisSnapThreshold() const;
    SceneHistory &history();
    const SceneHistory &history() const;
    void commitHistory(const AnimeCore::String &label);
    void resetHistory(const AnimeCore::String &label);
    void dropRedoTail();
    bool undoHistory();
    bool redoHistory();
    bool goToHistory(int index);
    void setCurrentLayer(int layerIndex);
    void setCurrentFrame(int frameIndex);
    int layerCount() const;
    int frameCount() const;
    int assetCount() const;
    AnimeCore::String layerName(int layerIndex) const;
    AnimeCore::String frameName(int frameIndex) const;
    AnimeCore::String assetName(int assetIndex) const;
    int importRasterLayer(const AnimeCore::Image &image, const AnimeCore::String &layerName);
    int importVectorLineLayer(const AnimeCore::Vector<ImportedVectorFrame> &frames, const AnimeCore::String &layerName);
    int addLayer();
    bool deleteLayer(int layerIndex);
    // Deletes a layer group and everything inside it, as one history entry.
    int deleteLayerGroup(int groupId);
    bool moveLayer(int fromIndex, int toIndex);
    int addFrame();
    // Inserts a frame that HOLDS `row` directly after it: same cells, so one
    // drawing. Directly after rather than at the end because a hold describes
    // the frame it was asked for, not the sheet's last row.
    int insertHoldFrameAfter(int row);
    // An independent copy of `row` after its hold run: new frame ids, deep
    // copied drawings, so the copy can be edited without changing the source.
    int duplicateFrame(int row);
    bool deleteFrame(int frameIndex);
    bool moveFrame(int fromIndex, int toIndex);
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector, const AnimeCore::String &name = AnimeCore::String());
    void setCurrentAsset(int assetIndex);
    bool assignAssetToLayer(int layerIndex, int assetIndex);
    int addLayerForAsset(int assetIndex);
    AnimeSceneModel &model();
    const AnimeSceneModel &model() const;

    AnimeCore::Signal<int> layerListChanged;
    AnimeCore::Signal<int> assetListChanged;
    AnimeCore::Signal<AnimeCore::String> pythonDebugMessage;
    AnimeCore::Signal<> focusGained;
    AnimeCore::Signal<> historyChanged;
    AnimeCore::Signal<> historyCommitted;
    AnimeCore::Signal<> viewTransformChanged;
    // The subset of viewTransformChanged the USER caused: wheel zoom,
    // middle-button pan, a scroll bar dragged. A shell that re-frames the view
    // by itself needs to know when to stop doing that, and it cannot tell from
    // viewTransformChanged - its own fit emits that one too.
    AnimeCore::Signal<> userTransformed;
    AnimeCore::Signal<> playbackInterrupted;

protected:
    void paintGL() override;
    void mousePressEvent(AnimeCore::MouseEvent *event) override;
    void mouseMoveEvent(AnimeCore::MouseEvent *event) override;
    void mouseReleaseEvent(AnimeCore::MouseEvent *event) override;
    void wheelEvent(AnimeCore::WheelEvent *event) override;
    void resizeEvent(AnimeCore::ResizeEvent *event) override;
    void focusInEvent(AnimeCore::FocusEvent *event) override;
    void focusOutEvent(AnimeCore::FocusEvent *event) override;
    // The brush ring belongs to the cursor, so it has to leave with it.
    void leaveEvent(AnimeCore::Event *event) override;
    // Rebuilds the pointer for the current tool: the pen wears a ring the
    // size of the mark it makes, drawn INTO the cursor rather than onto the
    // canvas so it cannot lag the pointer, and with no arrow beside it.
    void updateBrushCursor();

public:
    using VectorStroke = AnimeVectorStroke;
    using VectorGroupId = AnimeVectorGroupId;
    using VectorStrokeNode = AnimeVectorStrokeNode;
    using VectorImageModel = AnimeVectorImageModel;

private:
    struct OverlayHandle {
        AnimeCore::String id;
        AnimeCore::Color badgeColor;
        AnimeCore::RectF rect;
        // Union of every part drawn under this id, and whether the badge
        // follows it (closed items) or an end point (open ones).
        AnimeCore::RectF extent;
        bool anchorIsExtent = false;
        bool accept = false;
    };

    enum class AxisSnapState {
        Inactive,
        Pending,
        Horizontal,
        Vertical
    };

    // Which classes of scene content one pass draws. A null filter draws
    // everything, which is what every pass except the onion one wants.
    // `guideProperties` names the stroke properties that count as guide lines
    // rather than artwork; those strokes obey `guideStrokes`, all others
    // `strokes`. With no list every stroke is artwork.
    // `excludeLayerTag`, when given and non-empty, drops whole COLUMNS: any
    // column whose innermost tagged group carries that tag is skipped before
    // any content class is consulted. It is the layer gate, so it wins over
    // every flag above.
    struct SceneContentFilter {
        bool strokes = true;
        bool fills = true;
        bool raster = true;
        const AnimeCore::StringList *guideProperties = nullptr;
        bool guideStrokes = true;
        const AnimeCore::String *excludeLayerTag = nullptr;
    };

    void paintSceneContent(AnimeCore::Painter &painter, int frameIndex, bool includeCurrentStroke,
                           const SceneContentFilter *filter = nullptr);
    // Re-render the playback cache for the current view, once the gesture that
    // changed it has settled.
    void schedulePlaybackCacheRefresh();
    // The ghost frames, under the current frame and above the page.
    void paintOnionGhosts(AnimeCore::Painter &painter);
    // The tinted ghost for one frame, rendered on demand and cached.
    AnimeCore::Image onionGhost(int frameIndex, bool past);
    // How many ghosts this viewport can afford. kMaxOnionGhosts is a count,
    // not a size, so the answer is a byte budget divided by one full-viewport
    // image - the same reasoning buildPlaybackCache applies to its frames.
    int maxOnionGhosts() const;
    void clearOnionCache();
    // Same shape as the playback cache: a ghost bakes the transform in, so a
    // pan or zoom re-renders it once the gesture settles rather than per tick.
    void scheduleOnionCacheRefresh();
    void paintOverlayItems(AnimeCore::Painter &painter);
    // Badge just above-right of `anchor`, clamped into view. `slot` counts
    // leftwards from the x badge (1 = the check badge); `slotCount` is how
    // many badges the id carries, so an edge clamp keeps the pair side by
    // side instead of stacking or pushing one off-screen.
    AnimeCore::RectF overlayHandleRect(const AnimeCore::PointF &anchor, int slot = 0, int slotCount = 1) const;
    bool overlayActionItemAt(const AnimeCore::PointF &pos);
    // Topmost action badge containing the document position, or -1.
    int overlayActionHandleAt(const AnimeCore::PointF &pos) const;
    // Fires the action badge or starts the overlay drag under the press,
    // arbitrating an overlapping badge/grab in screen space; returns whether
    // the press was consumed.
    bool pressOverlayOrBadge(const AnimeCore::PointF &screenPos,
                             const AnimeCore::PointF &docPos,
                             AnimeCore::KeyboardModifiers modifiers);
    // Clears m_activeOverlayDrag and sends the owning tool a "cancel" so it
    // restores its persisted baseline. Safe to call with no drag in flight.
    void cancelActiveOverlayDrag();
    // Emits "framechange" when the model's frame moved past the last frame
    // Python was told about - regardless of who moved it.
    void notifyFrameChangedIfNeeded();
    // Emits "layerchange" when the model's current layer moved past the last
    // layer Python was told about - the layer counterpart of the frame
    // notifier, and the mechanism layer-focused tools (an auto-mapping
    // layer's overlays) hang their show/hide policy on.
    void notifyLayerChangedIfNeeded();
    // Emits "onion" when this board's ghost set, the content flags (guides,
    // lines, fills, am_layers) or - while onion is on - the current frame
    // moved past what Python was last told.
    // Ghosts render layer-stack CONTENT only; the H/V axes and additional lines
    // are Python OVERLAYS, so the policy side needs the ghost set to draw its
    // own ghost guides (auto_mapping). Mechanism only: any view may emit, only
    // the main board carries onion state today.
    void notifyOnionChangedIfNeeded();
    // Topmost draggable overlay item within grab range of the screen position.
    // When it returns an id and screenDistance is given, screenDistance is
    // the pointer's screen-space distance to that item's nearest segment.
    AnimeCore::String draggableOverlayItemAt(const AnimeCore::PointF &screenPos,
                                             double *screenDistance = nullptr) const;
    void sendOverlayActionMessage(const AnimeCore::String &overlayId, const AnimeCore::String &action);
    void paintEditHandles(AnimeCore::Painter &painter);
    // Topmost handle whose SCREEN-space box contains the screen position.
    AnimeCore::String editHandleAt(const AnimeCore::PointF &screenPos) const;
    // "handle" hook event. Phases: "pick" (arrow click on no handle),
    // "press"/"move"/"release" (a handle drag), "view" (zoom changed while
    // handles are shown). Carries the document position and the current zoom.
    // "handle" hook event; returns what Python CLAIMED, if anything. A
    // "pick" (press on canvas, no handle under it) may answer with a grab
    // id, and the gesture then runs as a drag under that id - the generic
    // half of "drag the thing you just clicked" (Transfer's box body,
    // Arrow's default-mode outline). Empty means the press was not claimed.
    AnimeCore::String sendPythonHandleMessage(const AnimeCore::String &phase,
                                              const AnimeCore::String &handleId,
                                              const AnimeCore::PointF &pos,
                                              AnimeCore::KeyboardModifiers modifiers = AnimeCore::NoModifier);
    void resetAxisSnap(AnimeCore::KeyboardModifiers modifiers, const AnimeCore::PointF &anchor);
    AnimeCore::PointF applyAxisSnap(AnimeCore::KeyboardModifiers modifiers, const AnimeCore::PointF &point, bool *retroChanged);
    // "Hold still to draw straight": the still window is (re)armed at a
    // document anchor, fires once, and collapses the stroke to a segment.
    void armHoldStill(const AnimeCore::PointF &anchor);
    void engageStraightLine();
    // Straight mode owns the geometry outright - taken BY VALUE because it
    // rewrites the point buffer the caller may have pointed into.
    void updateStraightLine(AnimeCore::PointF tip);
    // Latches the axis like applyAxisSnap does, so it mutates the snap state.
    AnimeCore::PointF straightLineTip(AnimeCore::KeyboardModifiers modifiers, const AnimeCore::PointF &cursor);
    void paintBackground(AnimeCore::Painter &painter, const AnimeCore::RectF &target) const;
    void paintActiveIndicator(AnimeCore::Painter &painter) const;
    AnimeCore::PointF mapToDocument(const AnimeCore::PointF &screenPos) const;
    void clampPan();
    void notifyViewTransformChanged();
    void updateCurrentStroke();
    void finishCurrentStroke();
    // Dispatches the hook event; returns true when a hook set
    // "cancel_history" in the message, vetoing the follow-up history commit.
    bool pythonHookSendMessage(const AnimeCore::String &event, const AnimeCore::PointF &pos = AnimeCore::PointF(), const AnimeCore::PointF &delta = AnimeCore::PointF(), bool changed = true, int strokeIndex = -1);
    // "fillrequest" hook event: offers the click to Python BEFORE the built-in
    // fill runs. Returns true when a hook set message["handled"], i.e. the
    // fill policy ran in Python and the default C++ behavior must not.
    bool sendPythonFillRequestMessage(const AnimeCore::PointF &pos);
    bool eraseAt(const AnimeCore::PointF &pos);
    bool eraseBetween(const AnimeCore::PointF &from, const AnimeCore::PointF &to);
    bool deleteLineAt(const AnimeCore::PointF &pos);
    bool deleteLineBetween(const AnimeCore::PointF &from, const AnimeCore::PointF &to);
    // CutMode: trims the stroke under `pos` back to its crossings with the
    // OTHER strokes of the same layer.
    bool cutLineAt(const AnimeCore::PointF &pos);
    // The single stroke a brush stroke from `from` to `to` is aimed at:
    // within reach of the radius, and nearest the brush's own centre line.
    // `from == to` for a click. -1 when nothing is in reach.
    int nearestStrokeToBrush(const VectorImageModel *image,
                             const AnimeCore::PointF &from,
                             const AnimeCore::PointF &to) const;
    bool fillAt(const AnimeCore::PointF &pos);
    bool currentLayerAcceptsFill() const;
    AnimeCore::Vector<AnimeCore::LineF> fillGraphSegments(FillScope scope, int layerIndex) const;
    AnimeCore::Path vectorRegionPathAt(const AnimeCore::PointF &seed, FillScope scope, int layerIndex) const;
    void removeInvalidFillRegions();
    bool eraseStrokeAt(int strokeIndex, const AnimeCore::PointF &pos);
    bool eraseStrokeBetween(int strokeIndex, const AnimeCore::PointF &from, const AnimeCore::PointF &to);
    VectorStroke makeStroke(const AnimeCore::Vector<AnimeCore::PointF> &points, const AnimeCore::Color &color, double width, int id = 0, bool filterInput = true, bool smoothPath = true) const;
    bool appendPoint(const AnimeCore::PointF &point);
    VectorImageModel *currentImage(bool create, AnimeColumnType assetType = AnimeColumnType::Vector);
    AnimeColumn *currentColumn();
    const AnimeColumn *currentColumn() const;
    bool currentColumnEditable() const;
    // The GESTURE-scoped reading of "may this column be marked on": editable,
    // or an unlocked Fill column while fill-paint mode is on. Only the pen and
    // eraser gesture paths ask it; fill, arrow, transfer, cut and delete-line
    // keep asking currentColumnEditable, so their semantics are untouched.
    bool columnAcceptsGesture() const;
    // Fill regions of the current Fill column swept by the eraser brush,
    // removed. Refuses outside fill-paint mode, so an ordinary erase can never
    // reach a fill layer's artwork.
    bool eraseFillRegionsBetween(const AnimeCore::PointF &from, const AnimeCore::PointF &to);

    Tool m_tool = Tool::Pen;
    FillScope m_fillScope = FillScope::CurrentLayer;
    AnimeCore::String m_viewName = AnimeCore::String("main");
    bool m_activeIndicator = false;
    bool m_hasInputFocus = false;
    AnimeCore::Color m_penColor = AnimeCore::Color(0, 0, 0);
    AnimeCore::Vector<AnimeCore::PointF> m_points;
    // Realtime input conditioning: 1 Euro filter with lag compensation over
    // the raw samples of the CURRENT stroke, plus the raw pen tip so paintGL
    // can draw the unfiltered predictive preview at the very front.
    AnimeOneEuroFilter m_inputFilter;
    AnimeCore::PointF m_rawPenPos;
    bool m_hasRawPenPos = false;
    AnimeSceneModel m_model;
    VectorStroke m_currentStroke;
    bool m_hasCurrentStroke = false;
    AnimeCore::PointF m_hoverPos;
    bool m_hasHoverPos = false;
    AnimeCore::PointF m_lastEraserPos;
    bool m_hasLastEraserPos = false;
    double m_penWidth = 5.0;
    AnimeCore::String m_strokeProperty;
    AnimeCore::String m_activePythonTool;
    bool m_eraseGestureChanged = false;
    double m_eraserRadius = 12.0;
    // The pointer a Python tool asked for (ui.set_cursor). Names only - C++
    // owns what each name looks like, the tool owns when it applies. Empty =
    // no request; the pen's ring always wins over it.
    AnimeCore::String m_scriptCursor;
    double m_minPointDistance = 2.0;
    int m_smoothValue = 50;              // stabilizer strength
    AnimeStrokeFitSettings m_fitSettings; // how a committed stroke is fitted
    // "update" fires on every accepted point while drawing; hooks get at most
    // one dispatch per this interval so subscribers never run at tablet rate.
    AnimeCore::ElapsedTimer m_updateHookThrottle;
    static constexpr std::int64_t kUpdateHookIntervalMs = 33;
    // Connect-tool hover forwarding shares the same cadence: Python resolves
    // the snap hint per event, so the interpreter must not run at mouse rate.
    AnimeCore::ElapsedTimer m_hoverHookThrottle;
    // The live-preview hybrid fit runs at display cadence, not event cadence
    // (a per-event whole-stroke refit is quadratic in stroke length).
    AnimeCore::ElapsedTimer m_liveFitThrottle;
    static constexpr std::int64_t kLiveFitIntervalMs = 16;
    // Incremental-fit state of the active stroke: the frozen prefix that is
    // never recomputed, so the already-drawn path cannot tremble.
    AnimeLiveFitState m_liveFit;
    // Document px per screen px, captured at pen-down: the fit's budgets are
    // screen-sized (tremor, report rate, the eye), converted once per stroke.
    double m_liveFitPixelScale = 1.0;
    AxisSnapState m_axisSnapState = AxisSnapState::Inactive;
    AnimeCore::PointF m_axisSnapAnchor;
    int m_axisSnapAnchorIndex = 0;
    double m_axisSnapThreshold = 5.0;
    // "Hold still to draw straight": while the pen is down, staying inside a
    // small circle for kHoldStillMs reads as "I want a straight line, not this
    // wobble" - the stroke collapses to one segment from its start to the
    // cursor and tracks the cursor, unfitted, until release. A still pen sends
    // no move events, so the wait needs a timer, not a poll.
    std::unique_ptr<AnimeCore::Timer> m_holdStillTimer;
    // Kept in DOCUMENT space: zooming mid-stroke would drift a screen anchor.
    AnimeCore::PointF m_holdStillAnchor;
    bool m_straightLineMode = false;
    AnimeCore::PointF m_straightLineStart;
    AnimeCore::KeyboardModifiers m_lastMouseModifiers = AnimeCore::NoModifier;
    static constexpr int kHoldStillMs = 1500;
    // SCREEN px: the hand holds still to within a few pixels of the display,
    // whatever the canvas is magnified to. Converted per test through
    // algorithm/viewscale.h, the shared home of screen<->canvas conversion.
    static constexpr double kHoldStillRadiusScreenPx = 10.0;
    // Past this the ring stops being a cursor and goes back on the canvas:
    // window systems cap cursor bitmaps, and a clipped ring would lie about
    // the width. Screen px, and the diameter, not the radius.
    static constexpr int kMaxCursorRingPx = 128;
    // True while the ring is too big to be a cursor: the pointer is blank
    // and paintGL draws the ring instead.
    bool m_penRingOnCanvas = false;
    double m_zoom = 1.0;
    AnimeCore::PointF m_panOffset;
    bool m_panning = false;
    AnimeCore::Vector<EditHandle> m_editHandles;
    // Id of the handle being dragged; empty when no drag is in flight.
    AnimeCore::String m_activeHandleDrag;
    // Id of the draggable OVERLAY item being dragged (any tool), empty when
    // none. Routed through the same "handle" hook events as edit handles.
    AnimeCore::String m_activeOverlayDrag;
    // Overlay-drag "move" messages share the pointer-rate throttle cadence:
    // the owning tool re-renders overlays per message.
    AnimeCore::ElapsedTimer m_overlayDragHookThrottle;
    // The frame Python was last told about via "framechange". The frame lives
    // on the model and can be mutated behind the widget's back (ui.set_current,
    // addFrame, project load), so the notification baseline must be the
    // widget's own, not a value re-read from the model.
    int m_pythonNotifiedFrame = -1;
    // The layer Python was last told about via "layerchange"; -1 is a valid
    // "nothing selected" state, so the unset sentinel is -2. The COLUMN ID
    // rides along: deleting or reordering layers can put a different column
    // under the same index, and an index-only baseline swallowed that
    // change entirely.
    int m_pythonNotifiedLayer = -2;
    int m_pythonNotifiedLayerId = -1;
    // The onion state Python was last told about via "onion". While onion is
    // OFF the whole family collapses to one canonical baseline (frames empty,
    // current -1), so plain frame changes on a board with no ghosts cost
    // nothing. The baseline starts VALID and equal to those defaults: a fresh
    // board has no ghosts, so the first message a script ever sees is the one
    // that turns onion on - never a redundant "off".
    bool m_pythonNotifiedOnionValid = true;
    bool m_pythonNotifiedOnionEnabled = false;
    bool m_pythonNotifiedOnionGuides = false;
    bool m_pythonNotifiedOnionLines = false;
    bool m_pythonNotifiedOnionFills = false;
    bool m_pythonNotifiedOnionAmLayers = false;
    int m_pythonNotifiedOnionCurrent = -1;
    AnimeCore::Vector<int> m_pythonNotifiedOnionFrames;
    bool m_unboundedCanvas = false;
    BackgroundMode m_backgroundMode = BackgroundMode::White;
    bool m_contentEditable = true;
    bool m_fillPaintMode = false;
    AnimeCore::StringList m_editableProperties;
    AnimeCore::PointF m_lastPanPos;
    SceneHistory m_history;
    AnimeCore::Vector<AnimeCore::Image> m_playbackFrames;
    int m_playbackIndex = -1;
    bool m_playbackActive = false;
    // The view the cached frames were rendered for. paintGL maps them from
    // this onto the current view, so zoom/scroll/resize during playback are
    // re-mapped rather than fatal; the timer re-renders once the gesture ends.
    AnimeCore::PointF m_playbackCachePan;
    double m_playbackCacheZoom = 1.0;
    int m_playbackCacheFrameCount = 0;
    std::unique_ptr<AnimeCore::Timer> m_playbackCacheTimer;
    bool m_onionEnabled = false;
    AnimeCore::Set<int> m_onionFrames;
    bool m_onionGuideLines = false;
    bool m_onionLines = true;
    bool m_onionFills = true;
    bool m_onionAmLayers = true;
    AnimeCore::StringList m_onionExcludeProperties;
    // Empty until Python registers one, and an empty tag gates nothing: a
    // session with no auto-mapping module loaded ghosts exactly as before.
    AnimeCore::String m_onionLayerTag;
    struct OnionGhostImage {
        AnimeCore::Image image;
        // Which tint it carries. The playhead moving past a frame flips it,
        // and only the frames that crossed have to be redrawn.
        bool past = true;
    };
    AnimeCore::Hash<int, OnionGhostImage> m_onionCache;
    // The view every cached ghost was rendered for; captured when the cache
    // refills, so a half-stale set can never mix two transforms.
    AnimeCore::PointF m_onionCachePan;
    double m_onionCacheZoom = 1.0;
    AnimeCore::Size m_onionCacheSize;
    std::unique_ptr<AnimeCore::Timer> m_onionCacheTimer;
    // Ghosts are full-viewport images; past this many the memory is worse
    // than the redraw, so the cache is dropped rather than grown.
    static constexpr int kMaxOnionGhosts = 16;
    bool m_swallowNextPress = false;
    AnimeCore::Hash<std::int64_t, AnimeCore::Image> m_rasterCache;
    AnimeCore::Vector<OverlayItem> m_overlayItems;
    AnimeCore::Vector<OverlayHandle> m_overlayHandles;
};

#endif // OPENGLWIDGET_H
