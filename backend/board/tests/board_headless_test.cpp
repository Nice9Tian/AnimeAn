#include "openglwidget.h"

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/file.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/json.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/path.h"

#include "algorithm/core/string.h"
#include "backend/platform/eventloop.h"
#include "backend/platform/timer.h"

#ifdef ANIMEAN_WITH_PYTHON
#undef slots
#include <pybind11/embed.h>
#include "pythonbind/python_bindings.h"
namespace py = pybind11;
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

using namespace AnimeCore;

namespace {

void setEnvVar(const char *name, const String &value)
{
#ifdef _WIN32
    _putenv_s(name, value.toLocal8Bit().c_str());
#else
    setenv(name, value.toLocal8Bit().c_str(), 1);
#endif
}

void pumpEventLoop()
{
    Timer::singleShot(0, []() {
        EventLoop::instance().post([]() {
            EventLoop::instance().quit(0);
        });
    });
    EventLoop::instance().exec();
}

class FlushTestSurface : public BoardSurface
{
public:
    bool throwInPaint = false;
    Vector<std::uint64_t> observedMouseTimestamps;

protected:
    void paintGL() override
    {
        Painter painter(this);
        painter.fillRect(RectF(0, 0, 100, 100), Color(255, 255, 255));
        if (throwInPaint) {
            throw std::runtime_error("simulated python callback error in paintGL");
        }
        painter.fillRect(RectF(0, 0, 10, 10), Color(0, 0, 0));
    }

    void mousePressEvent(MouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }

    void mouseMoveEvent(MouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }

    void mouseReleaseEvent(MouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }
};

class ImageTestSurface : public BoardSurface
{
public:
    Vector<Image> toDraw;

protected:
    void paintGL() override
    {
        Painter painter(this);
        for (const Image &image : toDraw) {
            painter.drawImage(PointF(0.0, 0.0), image);
        }
    }
};

Image solidImage(int shade)
{
    Image image(10, 10, Image::Format_RGBA8888);
    image.fill(Color(shade, shade, shade));
    return image;
}

// The client keeps every image it was sent until told otherwise, so the board has
// to bound that: a new onion ghost per view change once ran a page out of memory.
bool runImageBudgetTests()
{
    const std::int64_t oneImage = 10 * 10 * 4;
    ImageTestSurface surface;
    surface.setImageBudget(2 * oneImage);
    surface.resizeSurface(100, 100, 1.0);

    std::uint64_t lastFrame = 0;
    StringList sent;
    StringList forgotten;
    surface.setCanSendFrame([]() { return true; });
    surface.setFrameSink([&](const BoardFrame &frame) { lastFrame = frame.frame; });
    surface.setImageSink(
        [&](const String &key, const Image &) { sent.append(key); },
        [&](const String &key) { forgotten.append(key); });

    const auto draw = [&](const Vector<Image> &images) {
        surface.toDraw = images;
        surface.update();
        pumpEventLoop();
        surface.acknowledgeFrame(lastFrame);
    };

    const Image a = solidImage(10), b = solidImage(20), c = solidImage(30);
    draw({a});
    draw({b});
    if (sent.size() != 2 || !forgotten.isEmpty()) {
        std::fprintf(stderr, "Expected two images held within the budget\n");
        return false;
    }
    const String keyA = sent.at(0), keyB = sent.at(1);

    // Over budget: the one drawn longest ago goes, and only that one.
    draw({c});
    if (sent.size() != 3 || forgotten != StringList{keyA}) {
        std::fprintf(stderr, "Expected the oldest image evicted, got %d\n", int(forgotten.size()));
        return false;
    }
    const String keyC = sent.at(2);

    // Drawing a held image counts as use: b outlives c here.
    draw({b});
    draw({a});
    if (sent.size() != 4 || sent.at(3) != keyA || forgotten != (StringList{keyA, keyC})) {
        std::fprintf(stderr, "Expected a resent and c evicted after b was reused\n");
        return false;
    }

    // What the current frame draws stays, even past the budget.
    forgotten.clear();
    sent.clear();
    draw({a, b, c});
    if (sent != StringList{keyC} || !forgotten.isEmpty()) {
        std::fprintf(stderr, "Expected the images of the current frame kept\n");
        return false;
    }

    // A new client holds nothing.
    sent.clear();
    surface.forgetSentImages();
    draw({a, b});
    if (sent.size() != 2) {
        std::fprintf(stderr, "Expected images resent to a new client\n");
        return false;
    }
    return true;
}

bool runFlushUnitTests()
{
    FlushTestSurface surface;
    surface.resizeSurface(200, 200, 1.0);

    // Flush with no client (no FrameSink attached): must not set waitingForAck or consume forceFullFrame.
    pumpEventLoop();
    if (surface.isWaitingForAck()) {
        std::fprintf(stderr, "Expected isWaitingForAck() == false when no client is connected\n");
        return false;
    }
    if (!surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected forceFullFrame() == true when no client is connected\n");
        return false;
    }

    bool clientPresent = false;
    int sinkCallCount = 0;
    BoardFrame receivedFrame;
    surface.setCanSendFrame([&]() { return clientPresent; });
    surface.setFrameSink([&](const BoardFrame &frame) {
        ++sinkCallCount;
        receivedFrame = frame;
    });

    // Still no client because canSendFrame returns false.
    surface.update();
    pumpEventLoop();
    if (sinkCallCount != 0 || surface.isWaitingForAck() || !surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected no frame sent and forceFullFrame preserved when canSendFrame is false\n");
        return false;
    }

    // Client connects, but paintGL throws an exception mid-frame.
    clientPresent = true;
    surface.throwInPaint = true;
    surface.update();
    pumpEventLoop();
    if (sinkCallCount != 0 || surface.isWaitingForAck() || !surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected exception in paintGL to abort flush without setting waitingForAck or consuming forceFullFrame\n");
        return false;
    }

    // Now paintGL succeeds: should send full frame (keep == 0) and set waitingForAck.
    surface.throwInPaint = false;
    surface.update();
    pumpEventLoop();
    if (sinkCallCount != 1 || !surface.isWaitingForAck() || surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected successful flush to send frame and set waitingForAck\n");
        return false;
    }
    if (receivedFrame.keep != 0) {
        std::fprintf(stderr, "Expected full frame keep == 0 after aborted frame, got %d\n", receivedFrame.keep);
        return false;
    }

    JsonParseError err;
    const JsonDocument doc = JsonDocument::fromJson(receivedFrame.ops, &err);
    if (err.error != JsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) {
        std::fprintf(stderr, "Failed to parse flush test ops JSON\n");
        return false;
    }

    // Verify injectMouse timestamps are set and monotonically non-decreasing.
    surface.injectMouse(Event::MouseButtonPress, PointF(10, 10), LeftButton, LeftButton, NoModifier, 1000);
    surface.injectMouse(Event::MouseMove, PointF(20, 20), NoButton, LeftButton, NoModifier, 950); // out-of-order, clamped to 1000
    surface.injectMouse(Event::MouseButtonRelease, PointF(30, 30), LeftButton, NoButton, NoModifier, 1050);
    if (surface.observedMouseTimestamps.size() != 3
        || surface.observedMouseTimestamps[0] != 1000
        || surface.observedMouseTimestamps[1] != 1000
        || surface.observedMouseTimestamps[2] != 1050) {
        std::fprintf(stderr, "Unexpected mouse timestamps: [%llu, %llu, %llu]\n",
                     static_cast<unsigned long long>(surface.observedMouseTimestamps.value(0)),
                     static_cast<unsigned long long>(surface.observedMouseTimestamps.value(1)),
                     static_cast<unsigned long long>(surface.observedMouseTimestamps.value(2)));
        return false;
    }

    return true;
}

} // namespace

bool runFieldScopeAndHideColumnsUnitTests()
{
    PaintOpenGLWidget mainBoard;
    PaintOpenGLWidget childBoard;
    mainBoard.setViewName(String("main"));
    mainBoard.model().setTextId(String("main_paint_view"));
    childBoard.setViewName(String("child"));
    childBoard.model().setTextId(String("child_paint_view"));

    // Add a stroke on mainBoard column 0 at frame 0 (in red, R=210, G=30, B=40).
    {
        AnimeVectorImageModel *mainImg = mainBoard.model().imageAt(0, 0, true);
        if (!mainImg) {
            std::fprintf(stderr, "Failed to get main image at (0, 0)\n");
            return false;
        }
        AnimeCore::Vector<AnimeCore::PointF> pts;
        pts.append(AnimeCore::PointF(20.0, 20.0));
        pts.append(AnimeCore::PointF(180.0, 120.0));
        mainImg->addStroke(AnimeVectorLogic::makeStroke(pts, AnimeCore::Color(210, 30, 40, 255), 4.0, 1, false, false));
    }

    // Add fills and strokes on childBoard column 0 at frame 0, then mark column 0 invisible with opacity 0.75.
    {
        AnimeVectorImageModel *childImg = childBoard.model().imageAt(0, 0, true);
        if (!childImg) {
            std::fprintf(stderr, "Failed to get child image at (0, 0)\n");
            return false;
        }
        AnimeVectorFillRegion fill;
        fill.id = 1;
        fill.color = AnimeCore::Color(10, 200, 50, 255);
        fill.path.addRect(RectF(10.0, 10.0, 40.0, 40.0));
        fill.bounds = fill.path.boundingRect();
        childImg->addFillRegion(fill);

        AnimeCore::Vector<AnimeCore::PointF> pts1;
        pts1.append(AnimeCore::PointF(15.0, 15.0));
        pts1.append(AnimeCore::PointF(85.0, 65.0));
        AnimeVectorStroke s1 = AnimeVectorLogic::makeStroke(pts1, AnimeCore::Color(20, 40, 220, 255), 4.0, 1, false, false);
        s1.property = String("keep_prop");
        childImg->addStroke(s1);

        AnimeCore::Vector<AnimeCore::PointF> pts2;
        pts2.append(AnimeCore::PointF(30.0, 30.0));
        pts2.append(AnimeCore::PointF(90.0, 90.0));
        AnimeVectorStroke s2 = AnimeVectorLogic::makeStroke(pts2, AnimeCore::Color(250, 120, 10, 255), 6.0, 2, false, false);
        s2.property = String("skip_prop");
        childImg->addStroke(s2);

        childBoard.model().setLayerVisible(0, false);
        childBoard.model().setLayerOpacity(0, 0.75);
    }

    Hash<String, std::uint64_t> fieldVersions;
    std::uint64_t nextVersion = 1;
    mainBoard.setFieldLookup(
        [&](const String &key) -> std::uint64_t { return fieldVersions.value(key, 0); },
        [&](const String &viewName) -> const AnimeSceneModel * {
            if (viewName == String("child")) {
                return &childBoard.model();
            }
            if (viewName == String("main")) {
                return &mainBoard.model();
            }
            return nullptr;
        });

    JsonArray fullOps;
    BoardFrame lastFrame;
    bool gotFrame = false;
    mainBoard.setFrameSink([&](const BoardFrame &frame) {
        lastFrame = frame;
        gotFrame = true;
        JsonParseError err;
        const JsonDocument doc = JsonDocument::fromJson(frame.ops, &err);
        if (err.error != JsonParseError::NoError || !doc.isArray()) {
            std::fprintf(stderr, "Invalid ops JSON in field scope test\n");
            std::exit(1);
        }
        while (fullOps.size() > frame.keep) {
            fullOps.removeLast();
        }
        for (const JsonValue &val : doc.array()) {
            fullOps.append(val);
        }
    });

    const auto waitFrame = [&]() -> BoardFrame {
        gotFrame = false;
        int spins = 0;
        while (!gotFrame && spins < 1000) {
            pumpEventLoop();
            ++spins;
        }
        if (!gotFrame) {
            std::fprintf(stderr, "Timed out waiting for frame in field scope test\n");
            std::exit(1);
        }
        return lastFrame;
    };

    mainBoard.resizeSurface(300, 240, 1.0);
    BoardFrame f0 = waitFrame();
    mainBoard.acknowledgeFrame(f0.frame);

#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&mainBoard.model());
    registerAnimeanUiScene(&childBoard.model());

    // Without callbacks registered: set_field with bad length still raises ValueError,
    // valid set_field / set_field_view are no-ops, field_display_supported returns False,
    // and call_later works and swallows exceptions.
    {
        py::module_ ui = py::module_::import("animean_python").attr("ui");
        if (ui.attr("field_display_supported")("main").cast<bool>()) {
            std::fprintf(stderr, "Expected field_display_supported to be False without callback\n");
            return false;
        }
        bool caughtValueError = false;
        try {
            ui.attr("set_field")("k", 2, 2, py::bytes("short", 5));
        } catch (const py::error_already_set &err) {
            if (err.matches(PyExc_ValueError)) {
                caughtValueError = true;
            }
        }
        if (!caughtValueError) {
            std::fprintf(stderr, "Expected ValueError from set_field with wrong byte length\n");
            return false;
        }
        // Valid no-op calls without callback
        ui.attr("set_field")("k", 2, 2, py::bytes(std::string(64, '\0')));
        ui.attr("set_field_view")("main", py::none());

        // ui.call_later runs on the host's event loop; the session registers the same hook.
        registerAnimeanUiCallLaterCallback([](int ms, std::function<void()> callback) {
            Timer::singleShot(ms, std::move(callback));
        });
        // Test call_later execution and exception swallowing
        py::dict ns;
        ns["called"] = 0;
        py::exec(
            "import animean_python\n"
            "def _cb_ok():\n"
            "    global called\n"
            "    called += 1\n"
            "def _cb_boom():\n"
            "    raise RuntimeError('expected call_later test error')\n"
            "animean_python.ui.call_later(0, _cb_ok)\n"
            "animean_python.ui.call_later(0, _cb_boom)\n",
            ns, ns);
        for (int i = 0; i < 50 && ns["called"].cast<int>() == 0; ++i) {
            pumpEventLoop();
        }
        if (ns["called"].cast<int>() != 1) {
            std::fprintf(stderr, "Expected call_later callback to run once, got %d\n", ns["called"].cast<int>());
            return false;
        }
    }

    registerAnimeanUiFieldCallback([&](const String &key, int, int, const std::string &) {
        fieldVersions[key] = nextVersion++;
        if (mainBoard.fieldViewSpec().active && mainBoard.fieldViewSpec().fieldKey == key) {
            mainBoard.update();
        }
    });
    registerAnimeanUiFieldViewCallback([&](const String &view, const AnimeanFieldViewSpec &spec) {
        if (view != String("main")) {
            return;
        }
        if (!spec.active) {
            mainBoard.clearFieldViewSpec();
            return;
        }
        PaintOpenGLWidget::FieldViewSpec converted;
        converted.active = true;
        converted.fieldKey = spec.fieldKey;
        converted.rect = spec.rect;
        converted.maxEdge = spec.maxEdge;
        converted.sourceView = spec.sourceView;
        converted.columns = spec.columns;
        converted.frame = spec.frame;
        converted.widthScale = spec.widthScale;
        for (const AnimeanFieldPass &p : spec.passes) {
            PaintOpenGLWidget::FieldPass outPass;
            outPass.tagLo = p.tagLo;
            outPass.tagHi = p.tagHi;
            outPass.opacity = p.opacity;
            outPass.tintR = p.tintR;
            outPass.tintG = p.tintG;
            outPass.tintB = p.tintB;
            outPass.tintA = p.tintA;
            converted.passes.append(outPass);
        }
        converted.hideColumns = spec.hideColumns;
        converted.skipProperties = spec.skipProperties;
        mainBoard.setFieldViewSpec(converted);
    });
    bool mainFieldCap = true;
    registerAnimeanUiFieldDisplaySupportedCallback([&](const String &view) {
        return view == String("main") ? mainFieldCap : false;
    });

    {
        py::module_ ui = py::module_::import("animean_python").attr("ui");
        if (!ui.attr("field_display_supported")("main").cast<bool>()
            || ui.attr("field_display_supported")("child").cast<bool>()) {
            std::fprintf(stderr, "Unexpected field_display_supported result with callback\n");
            return false;
        }
        ui.attr("set_field")("k", 2, 2, py::bytes(std::string(64, '\0')));
        py::dict spec;
        spec["field"] = "k";
        spec["rect"] = py::make_tuple(10.0, 20.0, 100.0, 120.0);
        spec["max_edge"] = 8.0;
        spec["source_view"] = "child";
        py::list cols;
        cols.append(0);
        spec["columns"] = cols;
        spec["frame"] = 0;
        spec["width_scale"] = 1.5;
        py::list passes;
        py::dict pass0;
        pass0["tag"] = py::make_tuple(-1.0, 1.0);
        pass0["opacity"] = 0.8;
        passes.append(pass0);
        py::dict pass1;
        pass1["tag"] = py::make_tuple(1.0, 2.0);
        pass1["opacity"] = 0.5;
        pass1["tint"] = py::make_tuple(120, 140, 160, 200);
        passes.append(pass1);
        spec["passes"] = passes;
        spec["hide_columns"] = py::list();
        py::list skipProps;
        skipProps.append("skip_prop");
        spec["skip_properties"] = skipProps;
        ui.attr("set_field_view")("main", spec);
    }
#else
    fieldVersions[String("k")] = nextVersion++;
    PaintOpenGLWidget::FieldViewSpec spec;
    spec.active = true;
    spec.fieldKey = String("k");
    spec.rect = RectF(10.0, 20.0, 100.0, 120.0);
    spec.maxEdge = 8.0;
    spec.sourceView = String("child");
    spec.columns = {0};
    spec.frame = 0;
    spec.widthScale = 1.5;
    spec.passes = {
        PaintOpenGLWidget::FieldPass{-1.0, 1.0, 0.8, 0, 0, 0, 0},
        PaintOpenGLWidget::FieldPass{1.0, 2.0, 0.5, 120, 140, 160, 200},
    };
    spec.skipProperties = {String("skip_prop")};
    mainBoard.setFieldViewSpec(spec);
#endif

    // 1. Verify F and f recording, hashing, invisible column drawing, opacity, width_scale, and skip_properties.
    BoardFrame f1 = waitFrame();
    mainBoard.acknowledgeFrame(f1.frame);

    int fOpIndex = -1;
    int endFOpIndex = -1;
    for (int i = 0; i < fullOps.size(); ++i) {
        const JsonArray op = fullOps.at(i).toArray();
        if (op.isEmpty()) {
            continue;
        }
        if (op.at(0).toString() == String("F")) {
            fOpIndex = i;
        } else if (op.at(0).toString() == String("f")) {
            endFOpIndex = i;
        }
    }
    if (fOpIndex < 0 || endFOpIndex <= fOpIndex) {
        std::fprintf(stderr, "Expected F and f ops in recorded frame, got fOpIndex=%d endFOpIndex=%d\n",
                     fOpIndex, endFOpIndex);
        return false;
    }

    // Verify F op fields and non-zero hash in RecordingPaintEngine
    const Vector<RecordedOp> &rawOps = mainBoard.recordingDevice()->ops();
    if (fOpIndex >= rawOps.size() || endFOpIndex >= rawOps.size()
        || rawOps[fOpIndex].hash == 0 || rawOps[endFOpIndex].hash == 0
        || rawOps[fOpIndex].hash == rawOps[endFOpIndex].hash) {
        std::fprintf(stderr, "Expected non-zero distinct hashes for F and f ops\n");
        return false;
    }
    const JsonArray fOp = fullOps.at(fOpIndex).toArray();
    if (fOp.at(1).toString() != String("k")
        || fOp.at(2).toInteger() != 1
        || fOp.at(3).toDouble() != 10.0
        || fOp.at(4).toDouble() != 20.0
        || fOp.at(5).toDouble() != 100.0
        || fOp.at(6).toDouble() != 120.0
        || fOp.at(7).toDouble() != 8.0
        || !fOp.at(8).isArray() || fOp.at(8).toArray().size() != 2) {
        std::fprintf(stderr, "Unexpected F op parameters: %s\n",
                     JsonDocument(fOp).toJson(JsonDocument::Compact).c_str());
        return false;
    }
    const JsonArray p0 = fOp.at(8).toArray().at(0).toArray();
    const JsonArray p1 = fOp.at(8).toArray().at(1).toArray();
    if (p0.size() != 7 || p0.at(0).toDouble() != -1.0 || p0.at(1).toDouble() != 1.0
        || std::abs(p0.at(2).toDouble() - 0.8) > 1e-6
        || p0.at(3).toInt() != 0 || p0.at(4).toInt() != 0 || p0.at(5).toInt() != 0 || p0.at(6).toInt() != 0
        || p1.size() != 7 || p1.at(0).toDouble() != 1.0 || p1.at(1).toDouble() != 2.0
        || std::abs(p1.at(2).toDouble() - 0.5) > 1e-6
        || p1.at(3).toInt() != 120 || p1.at(4).toInt() != 140 || p1.at(5).toInt() != 160 || p1.at(6).toInt() != 200) {
        std::fprintf(stderr, "Unexpected F op passes: %s\n",
                     JsonDocument(fOp.at(8).toArray()).toJson(JsonDocument::Compact).c_str());
        return false;
    }

    // Inside F..f: verify opacity 0.75, fill color (10,200,50), stroke color (20,40,220) with
    // AnimeVectorLogic::displayStrokeWidth(4.0 * 1.5, mainBoard.zoomFactor()) followed by S path op,
    // and that skip_prop stroke (250,120,10) was NOT drawn.
    const double expectedWidth =
        AnimeVectorLogic::displayStrokeWidth(4.0 * 1.5, mainBoard.zoom());
    bool sawColumnOpacity = false;
    bool sawChildFill = false;
    bool sawKeptStroke = false;
    bool sawSkippedStroke = false;
    for (int i = fOpIndex + 1; i < endFOpIndex; ++i) {
        const JsonArray op = fullOps.at(i).toArray();
        const String kind = op.at(0).toString();
        if (kind == String("O") && std::abs(op.at(1).toDouble() - 0.75) < 1e-6) {
            sawColumnOpacity = true;
        } else if (kind == String("B")
                   && op.at(1).toInt() == 10 && op.at(2).toInt() == 200 && op.at(3).toInt() == 50) {
            sawChildFill = true;
        } else if (kind == String("P")) {
            if (op.at(1).toInt() == 20 && op.at(2).toInt() == 40 && op.at(3).toInt() == 220
                && std::abs(op.at(5).toDouble() - expectedWidth) < 1e-6) {
                for (int j = i + 1; j < endFOpIndex; ++j) {
                    const String nextKind = fullOps.at(j).toArray().at(0).toString();
                    if (nextKind == String("S")) {
                        sawKeptStroke = true;
                        break;
                    }
                    if (nextKind != String("B")) {
                        break;
                    }
                }
            }
            if (op.at(1).toInt() == 250 && op.at(2).toInt() == 120 && op.at(3).toInt() == 10) {
                sawSkippedStroke = true;
            }
        }
    }
    if (!sawColumnOpacity || !sawChildFill || !sawKeptStroke || sawSkippedStroke) {
        std::fprintf(stderr,
                     "Field scope content check failed: opacity=%d fill=%d kept=%d skipped=%d\n",
                     int(sawColumnOpacity), int(sawChildFill), int(sawKeptStroke), int(sawSkippedStroke));
        return false;
    }

    // Verify mainBoard's own stroke (210, 30, 40) was drawn BEFORE F when hide_columns is empty.
    const auto hasMainStrokeBeforeF = [&](int limitIndex) {
        for (int i = 0; i < limitIndex && i < fullOps.size(); ++i) {
            const JsonArray op = fullOps.at(i).toArray();
            if (op.at(0).toString() == String("P")
                && op.at(1).toInt() == 210 && op.at(2).toInt() == 30 && op.at(3).toInt() == 40) {
                return true;
            }
        }
        return false;
    };
    if (!hasMainStrokeBeforeF(fOpIndex)) {
        std::fprintf(stderr, "Expected mainBoard stroke before F when hide_columns is empty\n");
        return false;
    }

    // 2. When version changes, keep must stop at F!
    // First verify an unchanged update keeps all ops (keep == fullOps.size()).
    const int totalOpsBeforeVersionChange = fullOps.size();
    mainBoard.update();
    BoardFrame fSame = waitFrame();
    mainBoard.acknowledgeFrame(fSame.frame);
    if (fSame.keep != totalOpsBeforeVersionChange) {
        std::fprintf(stderr, "Expected unchanged frame keep == %d, got %d\n",
                     totalOpsBeforeVersionChange, fSame.keep);
        return false;
    }

    // Now bump field version via set_field (or direct map): keep must equal fOpIndex!
#ifdef ANIMEAN_WITH_PYTHON
    py::module_::import("animean_python").attr("ui").attr("set_field")(
        "k", 2, 2, py::bytes(std::string(64, '\x01')));
#else
    fieldVersions[String("k")] = nextVersion++;
    mainBoard.update();
#endif
    BoardFrame fVer2 = waitFrame();
    mainBoard.acknowledgeFrame(fVer2.frame);
    if (fVer2.keep != fOpIndex) {
        std::fprintf(stderr, "Expected keep to stop at F (%d) when version changed, got %d\n",
                     fOpIndex, fVer2.keep);
        return false;
    }
    if (fullOps.at(fOpIndex).toArray().at(2).toInteger() != 2) {
        std::fprintf(stderr, "Expected F op version == 2 after version bump\n");
        return false;
    }

    // 3. Test hide_columns: hides column 0 in paintGL (filter == nullptr),
    // does NOT affect paintSceneContent with non-null filter or renderFrameThumbnail,
    // and restores when cleared via set_field_view("main", None).
    {
        PaintOpenGLWidget::FieldViewSpec specWithHide = mainBoard.fieldViewSpec();
        specWithHide.hideColumns = {0};
        mainBoard.setFieldViewSpec(specWithHide);
    }
    BoardFrame fHidden = waitFrame();
    mainBoard.acknowledgeFrame(fHidden.frame);

    int newFOpIndex = -1;
    for (int i = 0; i < fullOps.size(); ++i) {
        if (fullOps.at(i).toArray().at(0).toString() == String("F")) {
            newFOpIndex = i;
            break;
        }
    }
    if (newFOpIndex < 0 || hasMainStrokeBeforeF(newFOpIndex)) {
        std::fprintf(stderr, "Expected mainBoard column 0 stroke to be hidden in paintGL when in hide_columns\n");
        return false;
    }

    // Verify renderFrameThumbnail (which passes a non-null SceneContentFilter) ignores hide_columns!
    {
        const Image thumb = mainBoard.renderFrameThumbnail(0, Size(160, 120));
        bool foundRedThumbPixel = false;
        for (int y = 0; y < thumb.height() && !foundRedThumbPixel; ++y) {
            for (int x = 0; x < thumb.width(); ++x) {
                const unsigned int c = thumb.pixel(x, y);
                if (qRed(c) > 150 && qGreen(c) < 100 && qBlue(c) < 100) {
                    foundRedThumbPixel = true;
                    break;
                }
            }
        }
        if (!foundRedThumbPixel) {
            std::fprintf(stderr, "Expected renderFrameThumbnail to ignore hide_columns\n");
            return false;
        }
    }

    // Clear with set_field_view("main", None) -> column 0 is visible again in paintGL and F/f are gone.
#ifdef ANIMEAN_WITH_PYTHON
    py::module_::import("animean_python").attr("ui").attr("set_field_view")("main", py::none());
    clearAnimeanUiFieldDisplaySupportedCallback();
    clearAnimeanUiFieldViewCallback();
    clearAnimeanUiFieldCallback();
    unregisterAnimeanUiScene(&childBoard.model());
    unregisterAnimeanUiScene(&mainBoard.model());
#else
    mainBoard.clearFieldViewSpec();
#endif
    BoardFrame fCleared = waitFrame();
    mainBoard.acknowledgeFrame(fCleared.frame);
    if (!hasMainStrokeBeforeF(fullOps.size())) {
        std::fprintf(stderr, "Expected mainBoard column 0 stroke to be restored after clearing field view\n");
        return false;
    }
    for (int i = 0; i < fullOps.size(); ++i) {
        const String kind = fullOps.at(i).toArray().at(0).toString();
        if (kind == String("F") || kind == String("f")) {
            std::fprintf(stderr, "Expected no F or f ops after clearing field view\n");
            return false;
        }
    }

    return true;
}

bool runOverlayDragThrottleUnitTests()
{
#ifdef ANIMEAN_WITH_PYTHON
    PaintOpenGLWidget board;
    board.setViewName(String("main"));
    board.resizeSurface(400, 300, 1.0);

    int frameCount = 0;
    board.setFrameSink([&](const BoardFrame &frame) {
        ++frameCount;
        board.acknowledgeFrame(frame.frame);
    });
    pumpEventLoop();

    PaintOpenGLWidget::OverlayItem item;
    item.id = String("drag_item_1");
    item.points = {PointF(50.0, 50.0), PointF(150.0, 50.0)};
    item.draggable = true;
    item.removable = false;
    board.setOverlayItems({item});
    pumpEventLoop();

    py::dict ns;
    py::exec(
        "import python_hooks\n"
        "events = []\n"
        "def _on_handle(msg):\n"
        "    if msg.get('handle') != 'drag_item_1':\n"
        "        return\n"
        "    pos = msg.get('position') or {}\n"
        "    mods = msg.get('modifiers') or {}\n"
        "    events.append({\n"
        "        'phase': msg.get('phase'),\n"
        "        'x': float(pos.get('x', 0.0)),\n"
        "        'y': float(pos.get('y', 0.0)),\n"
        "        'shift': bool(mods.get('shift', False)),\n"
        "        'ctrl': bool(mods.get('ctrl', False)),\n"
        "        'alt': bool(mods.get('alt', False)),\n"
        "    })\n"
        "python_hooks.set_hook(_on_handle, handle=True)\n",
        ns, ns);

    const auto cleanupHooks = [&]() {
        py::exec("import python_hooks; python_hooks.del_hook(_on_handle)\n", ns, ns);
    };

    const auto waitMs = [](int ms) {
        ElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            pumpEventLoop();
        }
        pumpEventLoop();
    };

    const auto moveEvents = [&]() -> py::list {
        return py::eval("[e for e in events if e['phase'] == 'move']", ns, ns).cast<py::list>();
    };

    // 1. Burst of moves within 33ms then pause 60ms: trailing timer delivers the last position
    //    and last modifiers, triggers update(), and subsequent release/wait sends no extra move.
    board.injectMouse(Event::MouseButtonPress, PointF(80.0, 50.0), LeftButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(90.0, 60.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(100.0, 70.0), NoButton, LeftButton, ShiftModifier);
    board.injectMouse(Event::MouseMove, PointF(110.0, 80.0), NoButton, LeftButton, ControlModifier);
    board.injectMouse(Event::MouseMove, PointF(125.0, 95.0), NoButton, LeftButton, AltModifier);

    if (moveEvents().size() != 1) {
        std::fprintf(stderr, "Expected 1 immediate move in 33ms burst, got %d\n", int(moveEvents().size()));
        cleanupHooks();
        return false;
    }
    pumpEventLoop();
    const int framesBeforeTail = frameCount;

    waitMs(60);

    const py::list movesAfterPause = moveEvents();
    if (movesAfterPause.size() != 2) {
        std::fprintf(stderr, "Expected trailing move after 60ms pause (total 2), got %d\n", int(movesAfterPause.size()));
        cleanupHooks();
        return false;
    }
    const py::dict tailMove = movesAfterPause[1].cast<py::dict>();
    if (std::abs(tailMove["x"].cast<double>() - 125.0) > 1e-6
        || std::abs(tailMove["y"].cast<double>() - 95.0) > 1e-6
        || !tailMove["alt"].cast<bool>()
        || tailMove["shift"].cast<bool>()
        || tailMove["ctrl"].cast<bool>()) {
        std::fprintf(stderr, "Trailing move did not carry last position/modifiers\n");
        cleanupHooks();
        return false;
    }
    if (frameCount <= framesBeforeTail) {
        std::fprintf(stderr, "Expected trailing move to call update() and produce a board frame\n");
        cleanupHooks();
        return false;
    }

    board.injectMouse(Event::MouseButtonRelease, PointF(125.0, 95.0), LeftButton, NoButton, NoModifier);
    const int movesAtRelease1 = int(moveEvents().size());
    waitMs(60);
    if (int(moveEvents().size()) != movesAtRelease1) {
        std::fprintf(stderr, "Expected no move after release following pause\n");
        cleanupHooks();
        return false;
    }

    // 2. Burst of moves within 33ms followed immediately by release: timer is invalidated,
    //    waiting 60ms after release must not deliver any trailing move.
    py::exec("events.clear()", ns, ns);
    board.injectMouse(Event::MouseButtonPress, PointF(80.0, 50.0), LeftButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(90.0, 60.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(105.0, 75.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(120.0, 90.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseButtonRelease, PointF(120.0, 90.0), LeftButton, NoButton, NoModifier);

    const int totalEventsAfterImmediateRelease = py::len(ns["events"]);
    const int movesAfterImmediateRelease = int(moveEvents().size());
    waitMs(60);
    if (int(py::len(ns["events"])) != totalEventsAfterImmediateRelease
        || int(moveEvents().size()) != movesAfterImmediateRelease) {
        std::fprintf(stderr, "Expected no move after immediate release when timer was pending\n");
        cleanupHooks();
        return false;
    }

    // 3. Burst of moves within 33ms followed immediately by cancel (and overlay clear):
    //    waiting 60ms must not deliver any trailing move.
    py::exec("events.clear()", ns, ns);
    board.injectMouse(Event::MouseButtonPress, PointF(80.0, 50.0), LeftButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(90.0, 60.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(105.0, 75.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(120.0, 90.0), NoButton, LeftButton, NoModifier);
    board.setTool(PaintOpenGLWidget::Tool::Arrow); // triggers cancelActiveOverlayDrag()

    const int movesAfterCancel = int(moveEvents().size());
    waitMs(60);
    if (int(moveEvents().size()) != movesAfterCancel) {
        std::fprintf(stderr, "Expected no move after cancelActiveOverlayDrag()\n");
        cleanupHooks();
        return false;
    }
    board.injectMouse(Event::MouseButtonRelease, PointF(120.0, 90.0), LeftButton, NoButton, NoModifier);
    board.setTool(PaintOpenGLWidget::Tool::Pen);

    // Also verify clearing the dragged overlay item via setOverlayItems invalidates the timer.
    py::exec("events.clear()", ns, ns);
    board.injectMouse(Event::MouseButtonPress, PointF(80.0, 50.0), LeftButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(90.0, 60.0), NoButton, LeftButton, NoModifier);
    board.injectMouse(Event::MouseMove, PointF(110.0, 80.0), NoButton, LeftButton, NoModifier);
    board.setOverlayItems({});
    const int movesAfterOverlayClear = int(moveEvents().size());
    waitMs(60);
    if (int(moveEvents().size()) != movesAfterOverlayClear) {
        std::fprintf(stderr, "Expected no move after active overlay item was removed\n");
        cleanupHooks();
        return false;
    }
    board.injectMouse(Event::MouseButtonRelease, PointF(110.0, 80.0), LeftButton, NoButton, NoModifier);
    board.setOverlayItems({item});

    // 4. Continuous movement: move frequency never exceeds the 33ms throttle window.
    py::exec("events.clear()", ns, ns);
    board.injectMouse(Event::MouseButtonPress, PointF(80.0, 50.0), LeftButton, LeftButton, NoModifier);
    ElapsedTimer burstClock;
    burstClock.start();
    constexpr int kStepCount = 20;
    for (int i = 1; i <= kStepCount; ++i) {
        board.injectMouse(Event::MouseMove, PointF(80.0 + i * 2.0, 50.0 + i), NoButton, LeftButton, NoModifier);
        if (i < kStepCount) {
            waitMs(5);
        }
    }
    const std::int64_t burstElapsedMs = std::max<std::int64_t>(1, burstClock.elapsed());
    const int movesInBurst = int(moveEvents().size());
    const int maxAllowedMoves = 1 + int(burstElapsedMs / 33) + 1;
    if (movesInBurst > maxAllowedMoves || movesInBurst >= kStepCount) {
        std::fprintf(stderr, "Continuous move exceeded throttle rate: %d moves in %lld ms (max allowed %d)\n",
                     movesInBurst, static_cast<long long>(burstElapsedMs), maxAllowedMoves);
        cleanupHooks();
        return false;
    }
    waitMs(60);
    const py::list finalMoves = moveEvents();
    const py::dict lastContinuousMove = finalMoves[py::len(finalMoves) - 1].cast<py::dict>();
    if (std::abs(lastContinuousMove["x"].cast<double>() - (80.0 + kStepCount * 2.0)) > 1e-6
        || std::abs(lastContinuousMove["y"].cast<double>() - (50.0 + kStepCount)) > 1e-6) {
        std::fprintf(stderr, "Expected final position after continuous move settles\n");
        cleanupHooks();
        return false;
    }
    board.injectMouse(Event::MouseButtonRelease, PointF(80.0 + kStepCount * 2.0, 50.0 + kStepCount),
                      LeftButton, NoButton, NoModifier);

    cleanupHooks();
#endif
    return true;
}

int main(int argc, char *argv[])
{
    if (!runImageBudgetTests()) {
        return 1;
    }

    if (!runFlushUnitTests()) {
        return 1;
    }

#ifdef ANIMEAN_WITH_PYTHON
    const String appDir = CoreApplication::applicationDirPath();
    String pythonHome = String(ANIMEAN_PYTHON_HOME);
    if (!FileInfo::exists(Dir(pythonHome).filePath(String("python312.dll")))) {
        pythonHome = Dir(appDir).filePath(String(ANIMEAN_PYTHON_DIR_NAME));
    }

    StringList pythonPath;
    if (FileInfo::exists(Dir(String(ANIMEAN_PYFILE_DIR)).filePath(String("hello_world.py")))) {
        pythonPath << String(ANIMEAN_PYFILE_DIR);
    }
    pythonPath << appDir;
    if (FileInfo::exists(Dir(String(ANIMEAN_PYTHONBIND_DIR)).filePath(String("animemodel.py")))) {
        pythonPath << String(ANIMEAN_PYTHONBIND_DIR);
    }
    if (FileInfo::exists(Dir(String(ANIMEAN_OPENTOONZ_TOOLS_DIR)).filePath(String("toonz_to_dict.py")))) {
        pythonPath << String(ANIMEAN_OPENTOONZ_TOOLS_DIR);
    }

    StringList dllPath;
    dllPath << pythonHome << Dir(pythonHome).filePath(String("DLLs"));
    const char *existingPath = std::getenv("PATH");
    if (existingPath && existingPath[0] != '\0') {
        dllPath << String::fromLocal8Bit(existingPath);
    }

    setEnvVar("PATH", Dir::toNativeSeparators(dllPath.join(Char(u';'))));
    setEnvVar("PYTHONHOME", Dir::toNativeSeparators(pythonHome));
    setEnvVar("PYTHONPATH", Dir::toNativeSeparators(pythonPath.join(Char(u';'))));

    py::scoped_interpreter guard;
    py::module_::import("animean_python");
#endif

    if (!runFieldScopeAndHideColumnsUnitTests()) {
        return 1;
    }
    if (!runOverlayDragThrottleUnitTests()) {
        return 1;
    }
#ifdef ANIMEAN_WITH_PYTHON
#endif

    PaintOpenGLWidget board;
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&board.model());
#endif

    JsonArray fullOps;
    BoardFrame lastFrame;
    bool gotFrame = false;

    board.setFrameSink([&](const BoardFrame &frame) {
        lastFrame = frame;
        gotFrame = true;

        JsonParseError err;
        const JsonDocument doc = JsonDocument::fromJson(frame.ops, &err);
        if (err.error != JsonParseError::NoError || !doc.isArray()) {
            std::fprintf(stderr, "Invalid ops JSON at frame %llu: %s\n",
                         static_cast<unsigned long long>(frame.frame),
                         err.errorString().toUtf8().c_str());
            std::exit(1);
        }

        while (fullOps.size() > frame.keep) {
            fullOps.removeLast();
        }
        const JsonArray delta = doc.array();
        for (const JsonValue &val : delta) {
            fullOps.append(val);
        }
    });

    const auto waitForFrame = [&]() -> BoardFrame {
        gotFrame = false;
        int spins = 0;
        while (!gotFrame && spins < 1000) {
            pumpEventLoop();
            ++spins;
        }
        if (!gotFrame) {
            std::fprintf(stderr, "Timed out waiting for board frame\n");
            std::exit(1);
        }
        return lastFrame;
    };

    board.setTool(PaintOpenGLWidget::Tool::Pen);
    board.resizeSurface(567, 690, 1.0);

    const BoardFrame first = waitForFrame();
    if (first.w != 567 || first.h != 690) {
        std::fprintf(stderr, "Unexpected first frame size: %dx%d\n", first.w, first.h);
        return 1;
    }
    if (first.keep != 0) {
        std::fprintf(stderr, "Expected first frame keep == 0, got %d\n", first.keep);
        return 1;
    }
    if (first.ops.empty() || fullOps.isEmpty()) {
        std::fprintf(stderr, "Expected non-empty first frame ops\n");
        return 1;
    }
    board.acknowledgeFrame(first.frame);

    const PointF startPos(100.0, 120.0);
    board.injectMouse(Event::MouseButtonPress, startPos, LeftButton, LeftButton, NoModifier);
    {
        const BoardFrame f = waitForFrame();
        if (f.keep <= 0) {
            std::fprintf(stderr, "Expected press frame keep > 0, got %d\n", f.keep);
            return 1;
        }
        board.acknowledgeFrame(f.frame);
    }

    for (int i = 1; i <= 10; ++i) {
        const PointF movePos(100.0 + i * 15.0, 120.0 + i * 8.0);
        board.injectMouse(Event::MouseMove, movePos, NoButton, LeftButton, NoModifier);
        const BoardFrame f = waitForFrame();
        if (f.keep <= 0) {
            std::fprintf(stderr, "Expected move %d frame keep > 0, got %d\n", i, f.keep);
            return 1;
        }
        board.acknowledgeFrame(f.frame);
    }

    const PointF endPos(250.0, 200.0);
    board.injectMouse(Event::MouseButtonRelease, endPos, LeftButton, NoButton, NoModifier);
    {
        const BoardFrame f = waitForFrame();
        if (f.keep <= 0) {
            std::fprintf(stderr, "Expected release frame keep > 0, got %d\n", f.keep);
            return 1;
        }
        board.acknowledgeFrame(f.frame);
    }

    // The op list stays in logical pixels whatever the screen's ratio: the page
    // applies the ratio once, and pointer positions arrive in logical pixels.
    board.resizeSurface(567, 690, 1.25);
    {
        const BoardFrame f = waitForFrame();
        board.acknowledgeFrame(f.frame);
        for (const JsonValue &val : fullOps) {
            const JsonArray op = val.toArray();
            if (op.at(0).toString() == String("T")) {
                if (op.at(1).toDouble() != 1.0 || op.at(4).toDouble() != 1.0) {
                    std::fprintf(stderr, "Expected logical transform at ratio 1.25, got scale %g\n",
                                 op.at(1).toDouble());
                    return 1;
                }
                break;
            }
        }
    }

    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        File outFile(String::fromLocal8Bit(argv[1]));
        if (!outFile.open(IODevice::WriteOnly | IODevice::Truncate)) {
            std::fprintf(stderr, "Failed to open output file: %s\n", argv[1]);
            return 1;
        }
        outFile.write(JsonDocument(fullOps).toJson(JsonDocument::Compact));
        outFile.close();
    }

#ifdef ANIMEAN_WITH_PYTHON
    unregisterAnimeanUiScene(&board.model());
#endif

    std::printf("BOARD_HEADLESS_OK\n");
    std::fflush(stdout);
    return 0;
}
