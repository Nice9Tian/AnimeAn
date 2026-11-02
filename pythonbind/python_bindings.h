#ifndef PYTHON_BINDINGS_H
#define PYTHON_BINDINGS_H


#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/path.h"
#include <cstdint>
#include <string>
#include <utility>
#include "algorithm/core/color.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/string.h"
#include "algorithm/core/containers.h"

#include <functional>
using namespace AnimeCore;

class AnimeSceneModel;

// One parent window, as ui.windows reports it. Names are the stable identity
// Python addresses ("tools", "layers", ...); titles are what the user reads.
struct AnimeanWindowInfo {
    String name;
    String title;
    bool visible = false;
    StringList pages;
    String current;
};

// The window-management surface, registered as one unit like the freeze
// callback. `current` is not a member: it is list() filtered by name, and two
// answers to the same question drift.
struct AnimeanWindowsApi {
    std::function<Vector<AnimeanWindowInfo>()> list;
    std::function<void(const String &name, bool on)> show;
    std::function<void(const String &name, const String &page)> select;
};

struct AnimeanOverlayItem {
    String id;
    Vector<PointF> points;
    bool closed = false;
    Color strokeColor = Color(0, 0, 0, 255);
    Color fillColor = Color(0, 0, 0, 0);
    double width = 3.0;
    int penStyle = 1;
    bool removable = true;
    // Draw a check badge beside the remove badge. Clicking it emits the
    // generic "overlayaction" event with action="accept".
    bool confirmable = false;
    // Dragging a draggable item routes through the "handle" hook events with
    // the item's id, exactly like an edit handle - pure mechanism; what the
    // drag MEANS lives in Python.
    bool draggable = false;
    // Text for a bubble at the item's first point; see
    // PaintOpenGLWidget::paintOverlayLabels. An item with a label may be one point.
    String label;
};

// A draggable edit handle, drawn at constant SCREEN size above everything.
// Pure mechanism: C++ renders it, hit-tests it and reports presses/drags as
// "handle" hook events; what a handle means - which control point of which
// stroke, or a synthesized key point - lives entirely in Python.
struct AnimeanEditHandle {
    String id;
    PointF pos;                              // document coordinates
    int shape = 0;                            // 0 square, 1 circle, 2 diamond,
                                              // 3 accept button, 4 delete button
    Color color = Color(255, 255, 255, 255);
    // False for display-only markers (e.g. a snap HINT drawn under the
    // cursor): rendered normally, but invisible to the hit test - a hint
    // that swallowed the very click it advertised armed nothing at all.
    bool interactive = true;
};

void registerAnimeanUiScene(AnimeSceneModel *model);
void unregisterAnimeanUiScene(AnimeSceneModel *model);
void registerAnimeanUiRefreshCallback(std::function<void(bool frame, bool layer, bool asset, bool widget)> callback);
void clearAnimeanUiRefreshCallback();
void registerAnimeanUiToolOptionsCallback(std::function<void()> callback);
void clearAnimeanUiToolOptionsCallback();
void registerAnimeanUiFreezeCallback(std::function<void(bool frozen)> callback);
void clearAnimeanUiFreezeCallback();
void registerAnimeanUiWindowsCallback(AnimeanWindowsApi api);
void clearAnimeanUiWindowsCallback();
void registerAnimeanUiOverlayCallback(std::function<void(const String &view, const Vector<AnimeanOverlayItem> &items)> callback);
void clearAnimeanUiOverlayCallback();
void registerAnimeanUiEditHandleCallback(std::function<void(const String &view, const Vector<AnimeanEditHandle> &handles)> callback);
void clearAnimeanUiEditHandleCallback();
void registerAnimeanUiDrawColorCallback(std::function<void(const Color &color)> callback);
// Stabilizer / simplify / corner, the three drawing parameters (0-100 each).
void registerAnimeanUiDrawSettingsCallback(std::function<void(int, int, int)> callback);
void clearAnimeanUiDrawColorCallback();
void registerAnimeanUiHistoryCallback(std::function<void(const String &op, const String &view, const String &label)> callback);
void clearAnimeanUiHistoryCallback();
void registerAnimeanUiPadValueCallback(std::function<void(const String &pad, double x, double y)> callback);
void clearAnimeanUiPadValueCallback();
// The mouse pointer a tool asks for, by NAME ("", "arrow", "size_all",
// "size_h", "size_v", "size_bdiag", "size_fdiag", "cross", "rotate"). Generic
// on purpose: which region of which tool deserves which pointer is policy and
// lives in Python; C++ only knows how to draw each name.
void registerAnimeanUiCursorCallback(std::function<void(const String &view, const String &name)> callback);
void clearAnimeanUiCursorCallback();

// Fill-paint mode for one view (ui.set_fill_paint_mode). The view knows how to
// relax its Fill-column gesture ban and what the eraser then rubs out; WHICH
// board deserves the mode - and what the pen's stroke is turned into
// afterwards - is policy and lives in pyfile/layer_tool_policy.py.
void registerAnimeanUiFillPaintModeCallback(std::function<void(const String &view, bool on)> callback);
void clearAnimeanUiFillPaintModeCallback();

// Tools the shell must refuse for one view (ui.set_locked_tools). The names
// are the built-in tool strings ("pen", "eraser", "fill", "connect",
// "transfer", "arrow"); an empty list clears. C++ owns disabling the chips and
// refusing the arm, Python owns which layer forbids what.
void registerAnimeanUiLockedToolsCallback(std::function<void(const String &view, const StringList &tools)> callback);
void clearAnimeanUiLockedToolsCallback();

// The active theme mode ("dark" / "light"), mirrored for Python to read back
// through animean_python.ui.theme(). C++ pushes it whenever the theme is
// applied; there is no setter on the Python side - a script asks what the app
// looks like, it does not decide.
void setAnimeanUiTheme(const String &mode);

// True when at least one Python hook subscribes to the event (python_hooks
// pushes the subscription set via ui.set_hook_events). Lets dispatch sites
// skip the GIL/message work entirely for events nobody listens to. Until the
// first push arrives, every event counts as subscribed.
bool animeanHookEventSubscribed(const String &event);

struct AnimeanFieldPass {
    double tagLo = 0.0;
    double tagHi = 0.0;
    double opacity = 1.0;
    int tintR = 0;
    int tintG = 0;
    int tintB = 0;
    int tintA = 0;
};

// What ui.set_field_view asks of one board: which layers of which view to show
// through the field named fieldKey, and how.
struct AnimeanFieldViewSpec {
    bool active = false;
    String fieldKey;
    RectF rect;
    double maxEdge = 8.0;
    String sourceView;
    Vector<int> columns;
    int frame = 0;
    double widthScale = 1.0;
    Vector<AnimeanFieldPass> passes;
    Vector<int> hideColumns;
    StringList skipProperties;
};

// A displacement field for the page (ui.set_field): width * height cells of
// 16 bytes each, passed on as they are.
void registerAnimeanUiFieldCallback(
    std::function<void(const String &key, int width, int height, const std::string &data)> callback);
void clearAnimeanUiFieldCallback();
void registerAnimeanUiFieldViewCallback(
    std::function<void(const String &view, const AnimeanFieldViewSpec &spec)> callback);
void clearAnimeanUiFieldViewCallback();
// Whether the page showing this view can draw a field scope (board.capabilities).
void registerAnimeanUiFieldDisplaySupportedCallback(std::function<bool(const String &view)> callback);
void clearAnimeanUiFieldDisplaySupportedCallback();
// Runs a callable later on the host's event loop (ui.call_later). With none
// registered, as in the bare Python module, the call is dropped.
void registerAnimeanUiCallLaterCallback(std::function<void(int ms, std::function<void()> callback)> callback);
void clearAnimeanUiCallLaterCallback();
AnimeSceneModel *animeanUiSceneForViewName(const String &view);

#endif // PYTHON_BINDINGS_H
