#include "openglwidget.h"

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/file.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/json.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/path.h"
#include "algorithm/core/qtbridge.h"
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

int main(int argc, char *argv[])
{
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
