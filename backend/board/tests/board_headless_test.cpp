#include "openglwidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots
#define ANIMEAN_RESTORE_QT_SLOTS
#endif
#include <pybind11/embed.h>
#ifdef ANIMEAN_RESTORE_QT_SLOTS
#define slots Q_SLOTS
#undef ANIMEAN_RESTORE_QT_SLOTS
#endif
#include "pythonbind/python_bindings.h"
namespace py = pybind11;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {

class ClipAndFlushTestSurface : public BoardSurface
{
public:
    bool throwInPaint = false;
    QVector<quint64> observedMouseTimestamps;

protected:
    void paintGL() override
    {
        QPainter painter(this);
        painter.fillRect(QRectF(0, 0, 100, 100), Qt::white);
        if (throwInPaint) {
            throw std::runtime_error("simulated python callback error in paintGL");
        }

        // 1. setClipRect with ReplaceClip then IntersectClip
        painter.setClipRect(QRectF(10, 20, 100, 80), Qt::ReplaceClip);
        painter.fillRect(QRectF(0, 0, 200, 200), Qt::red);
        painter.setClipRect(QRectF(50, 40, 100, 80), Qt::IntersectClip);
        painter.fillRect(QRectF(0, 0, 200, 200), Qt::blue);

        // 2. setClipPath with ReplaceClip then IntersectClip
        QPainterPath p1;
        p1.addRect(QRectF(0, 0, 90, 90));
        QPainterPath p2;
        p2.addRect(QRectF(30, 25, 90, 90));
        painter.setClipPath(p1, Qt::ReplaceClip);
        painter.setClipPath(p2, Qt::IntersectClip);
        painter.fillRect(QRectF(0, 0, 200, 200), Qt::green);

        painter.setClipping(false);
        painter.fillRect(QRectF(0, 0, 10, 10), Qt::black);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        observedMouseTimestamps.append(event->timestamp());
        event->accept();
    }
};

QRectF clipPathBounds(const QJsonArray &flatPath)
{
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    bool any = false;

    int i = 0;
    while (i < flatPath.size()) {
        const int cmd = flatPath.at(i++).toInt(-1);
        if (cmd == 0 || cmd == 1) {
            if (i + 1 >= flatPath.size()) {
                break;
            }
            const double x = flatPath.at(i++).toDouble();
            const double y = flatPath.at(i++).toDouble();
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            any = true;
        } else if (cmd == 2) {
            if (i + 5 >= flatPath.size()) {
                break;
            }
            for (int k = 0; k < 3; ++k) {
                const double x = flatPath.at(i++).toDouble();
                const double y = flatPath.at(i++).toDouble();
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            any = true;
        } else if (cmd == 3) {
            // close subpath
        } else {
            break;
        }
    }
    if (!any) {
        return QRectF();
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

bool runClipAndFlushUnitTests()
{
    ClipAndFlushTestSurface surface;
    surface.resizeSurface(200, 200, 1.0);

    // Flush with no client (no FrameSink attached): must not set waitingForAck or consume forceFullFrame.
    QCoreApplication::processEvents();
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
    QCoreApplication::processEvents();
    if (sinkCallCount != 0 || surface.isWaitingForAck() || !surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected no frame sent and forceFullFrame preserved when canSendFrame is false\n");
        return false;
    }

    // Client connects, but paintGL throws an exception mid-frame.
    clientPresent = true;
    surface.throwInPaint = true;
    surface.update();
    QCoreApplication::processEvents();
    if (sinkCallCount != 0 || surface.isWaitingForAck() || !surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected exception in paintGL to abort flush without setting waitingForAck or consuming forceFullFrame\n");
        return false;
    }

    // Now paintGL succeeds: should send full frame (keep == 0) and set waitingForAck.
    surface.throwInPaint = false;
    surface.update();
    QCoreApplication::processEvents();
    if (sinkCallCount != 1 || !surface.isWaitingForAck() || surface.forceFullFrame()) {
        std::fprintf(stderr, "Expected successful flush to send frame and set waitingForAck\n");
        return false;
    }
    if (receivedFrame.keep != 0) {
        std::fprintf(stderr, "Expected full frame keep == 0 after aborted frame, got %d\n", receivedFrame.keep);
        return false;
    }

    // Verify K ops in receivedFrame.ops have intersected clip bounds.
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(receivedFrame.ops, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        std::fprintf(stderr, "Failed to parse clip test ops JSON\n");
        return false;
    }
    QVector<QRectF> nonNullClipBounds;
    int nullClipCount = 0;
    for (const QJsonValue &val : doc.array()) {
        const QJsonArray op = val.toArray();
        if (op.isEmpty() || op.at(0).toString() != QStringLiteral("K")) {
            continue;
        }
        if (op.at(1).isNull()) {
            ++nullClipCount;
        } else if (op.at(1).isArray()) {
            nonNullClipBounds.append(clipPathBounds(op.at(1).toArray()));
        }
    }
    if (nonNullClipBounds.size() < 3 || nullClipCount < 1) {
        std::fprintf(stderr, "Expected at least 3 non-null K ops and 1 null K op, got %d and %d\n",
                     int(nonNullClipBounds.size()), nullClipCount);
        return false;
    }
    // K[0]: ReplaceClip (10, 20, 100, 80)
    // K[1]: IntersectClip with (50, 40, 100, 80) -> intersection is (50, 40, 60, 60)
    const QRectF rectIntersect = nonNullClipBounds.at(1);
    if (std::abs(rectIntersect.left() - 50.0) > 1e-3
        || std::abs(rectIntersect.top() - 40.0) > 1e-3
        || std::abs(rectIntersect.width() - 60.0) > 1e-3
        || std::abs(rectIntersect.height() - 60.0) > 1e-3) {
        std::fprintf(stderr, "Unexpected setClipRect IntersectClip bounds: (%f, %f, %f, %f), expected (50, 40, 60, 60)\n",
                     rectIntersect.left(), rectIntersect.top(), rectIntersect.width(), rectIntersect.height());
        return false;
    }
    // K[2]: setClipPath ReplaceClip (0,0,90,90) + IntersectClip (30,25,90,90) -> intersection is (30, 25, 60, 65)
    const QRectF pathIntersect = nonNullClipBounds.last();
    if (std::abs(pathIntersect.left() - 30.0) > 1e-3
        || std::abs(pathIntersect.top() - 25.0) > 1e-3
        || std::abs(pathIntersect.width() - 60.0) > 1e-3
        || std::abs(pathIntersect.height() - 65.0) > 1e-3) {
        std::fprintf(stderr, "Unexpected setClipPath IntersectClip bounds: (%f, %f, %f, %f), expected (30, 25, 60, 65)\n",
                     pathIntersect.left(), pathIntersect.top(), pathIntersect.width(), pathIntersect.height());
        return false;
    }

    // Verify injectMouse timestamps are set and monotonically non-decreasing.
    surface.injectMouse(QEvent::MouseButtonPress, QPointF(10, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, 1000);
    surface.injectMouse(QEvent::MouseMove, QPointF(20, 20), Qt::NoButton, Qt::LeftButton, Qt::NoModifier, 950); // out-of-order, clamped to 1000
    surface.injectMouse(QEvent::MouseButtonRelease, QPointF(30, 30), Qt::LeftButton, Qt::NoButton, Qt::NoModifier, 1050);
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
    mainBoard.setViewName(QStringLiteral("main"));
    mainBoard.model().setTextId(QStringLiteral("main_paint_view"));
    childBoard.setViewName(QStringLiteral("child"));
    childBoard.model().setTextId(QStringLiteral("child_paint_view"));

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
        fill.path.addRect(QRectF(10.0, 10.0, 40.0, 40.0));
        fill.bounds = fill.path.boundingRect();
        childImg->addFillRegion(fill);

        AnimeCore::Vector<AnimeCore::PointF> pts1;
        pts1.append(AnimeCore::PointF(15.0, 15.0));
        pts1.append(AnimeCore::PointF(85.0, 65.0));
        AnimeVectorStroke s1 = AnimeVectorLogic::makeStroke(pts1, AnimeCore::Color(20, 40, 220, 255), 4.0, 1, false, false);
        s1.property = QStringLiteral("keep_prop");
        childImg->addStroke(s1);

        AnimeCore::Vector<AnimeCore::PointF> pts2;
        pts2.append(AnimeCore::PointF(30.0, 30.0));
        pts2.append(AnimeCore::PointF(90.0, 90.0));
        AnimeVectorStroke s2 = AnimeVectorLogic::makeStroke(pts2, AnimeCore::Color(250, 120, 10, 255), 6.0, 2, false, false);
        s2.property = QStringLiteral("skip_prop");
        childImg->addStroke(s2);

        childBoard.model().setLayerVisible(0, false);
        childBoard.model().setLayerOpacity(0, 0.75);
    }

    QHash<QString, quint64> fieldVersions;
    quint64 nextVersion = 1;
    mainBoard.setFieldLookup(
        [&](const QString &key) -> quint64 { return fieldVersions.value(key, 0); },
        [&](const QString &viewName) -> const AnimeSceneModel * {
            if (viewName == QStringLiteral("child")) {
                return &childBoard.model();
            }
            if (viewName == QStringLiteral("main")) {
                return &mainBoard.model();
            }
            return nullptr;
        });

    QJsonArray fullOps;
    BoardFrame lastFrame;
    bool gotFrame = false;
    mainBoard.setFrameSink([&](const BoardFrame &frame) {
        lastFrame = frame;
        gotFrame = true;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(frame.ops, &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            std::fprintf(stderr, "Invalid ops JSON in field scope test\n");
            std::exit(1);
        }
        while (fullOps.size() > frame.keep) {
            fullOps.removeLast();
        }
        for (const QJsonValue &val : doc.array()) {
            fullOps.append(val);
        }
    });

    const auto waitFrame = [&]() -> BoardFrame {
        gotFrame = false;
        int spins = 0;
        while (!gotFrame && spins < 1000) {
            QCoreApplication::processEvents();
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
            QCoreApplication::processEvents();
        }
        if (ns["called"].cast<int>() != 1) {
            std::fprintf(stderr, "Expected call_later callback to run once, got %d\n", ns["called"].cast<int>());
            return false;
        }
    }

    registerAnimeanUiFieldCallback([&](const QString &key, int, int, const QByteArray &) {
        fieldVersions[key] = nextVersion++;
        if (mainBoard.fieldViewSpec().active && mainBoard.fieldViewSpec().fieldKey == key) {
            mainBoard.update();
        }
    });
    registerAnimeanUiFieldViewCallback([&](const QString &view, const AnimeanFieldViewSpec &spec) {
        if (view != QStringLiteral("main")) {
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
    registerAnimeanUiFieldDisplaySupportedCallback([&](const QString &view) {
        return view == QStringLiteral("main") ? mainFieldCap : false;
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
    fieldVersions[QStringLiteral("k")] = nextVersion++;
    PaintOpenGLWidget::FieldViewSpec spec;
    spec.active = true;
    spec.fieldKey = QStringLiteral("k");
    spec.rect = QRectF(10.0, 20.0, 100.0, 120.0);
    spec.maxEdge = 8.0;
    spec.sourceView = QStringLiteral("child");
    spec.columns = {0};
    spec.frame = 0;
    spec.widthScale = 1.5;
    spec.passes = {
        PaintOpenGLWidget::FieldPass{-1.0, 1.0, 0.8, 0, 0, 0, 0},
        PaintOpenGLWidget::FieldPass{1.0, 2.0, 0.5, 120, 140, 160, 200},
    };
    spec.skipProperties = {QStringLiteral("skip_prop")};
    mainBoard.setFieldViewSpec(spec);
#endif

    // 1. Verify F and f recording, hashing, invisible column drawing, opacity, width_scale, and skip_properties.
    BoardFrame f1 = waitFrame();
    mainBoard.acknowledgeFrame(f1.frame);

    int fOpIndex = -1;
    int endFOpIndex = -1;
    for (int i = 0; i < fullOps.size(); ++i) {
        const QJsonArray op = fullOps.at(i).toArray();
        if (op.isEmpty()) {
            continue;
        }
        if (op.at(0).toString() == QStringLiteral("F")) {
            fOpIndex = i;
        } else if (op.at(0).toString() == QStringLiteral("f")) {
            endFOpIndex = i;
        }
    }
    if (fOpIndex < 0 || endFOpIndex <= fOpIndex) {
        std::fprintf(stderr, "Expected F and f ops in recorded frame, got fOpIndex=%d endFOpIndex=%d\n",
                     fOpIndex, endFOpIndex);
        return false;
    }

    // Verify F op fields and non-zero hash in RecordingPaintEngine
    const QVector<RecordedOp> &rawOps = mainBoard.recordingEngine()->ops();
    if (fOpIndex >= rawOps.size() || endFOpIndex >= rawOps.size()
        || rawOps[fOpIndex].hash == 0 || rawOps[endFOpIndex].hash == 0
        || rawOps[fOpIndex].hash == rawOps[endFOpIndex].hash) {
        std::fprintf(stderr, "Expected non-zero distinct hashes for F and f ops\n");
        return false;
    }
    const QJsonArray fOp = fullOps.at(fOpIndex).toArray();
    if (fOp.at(1).toString() != QStringLiteral("k")
        || fOp.at(2).toInteger() != 1
        || fOp.at(3).toDouble() != 10.0
        || fOp.at(4).toDouble() != 20.0
        || fOp.at(5).toDouble() != 100.0
        || fOp.at(6).toDouble() != 120.0
        || fOp.at(7).toDouble() != 8.0
        || !fOp.at(8).isArray() || fOp.at(8).toArray().size() != 2) {
        std::fprintf(stderr, "Unexpected F op parameters: %s\n",
                     QJsonDocument(fOp).toJson(QJsonDocument::Compact).constData());
        return false;
    }
    const QJsonArray p0 = fOp.at(8).toArray().at(0).toArray();
    const QJsonArray p1 = fOp.at(8).toArray().at(1).toArray();
    if (p0.size() != 7 || p0.at(0).toDouble() != -1.0 || p0.at(1).toDouble() != 1.0
        || std::abs(p0.at(2).toDouble() - 0.8) > 1e-6
        || p0.at(3).toInt() != 0 || p0.at(4).toInt() != 0 || p0.at(5).toInt() != 0 || p0.at(6).toInt() != 0
        || p1.size() != 7 || p1.at(0).toDouble() != 1.0 || p1.at(1).toDouble() != 2.0
        || std::abs(p1.at(2).toDouble() - 0.5) > 1e-6
        || p1.at(3).toInt() != 120 || p1.at(4).toInt() != 140 || p1.at(5).toInt() != 160 || p1.at(6).toInt() != 200) {
        std::fprintf(stderr, "Unexpected F op passes: %s\n",
                     QJsonDocument(fOp.at(8).toArray()).toJson(QJsonDocument::Compact).constData());
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
        const QJsonArray op = fullOps.at(i).toArray();
        const QString kind = op.at(0).toString();
        if (kind == QStringLiteral("O") && std::abs(op.at(1).toDouble() - 0.75) < 1e-6) {
            sawColumnOpacity = true;
        } else if (kind == QStringLiteral("B")
                   && op.at(1).toInt() == 10 && op.at(2).toInt() == 200 && op.at(3).toInt() == 50) {
            sawChildFill = true;
        } else if (kind == QStringLiteral("P")) {
            if (op.at(1).toInt() == 20 && op.at(2).toInt() == 40 && op.at(3).toInt() == 220
                && std::abs(op.at(5).toDouble() - expectedWidth) < 1e-6) {
                for (int j = i + 1; j < endFOpIndex; ++j) {
                    const QString nextKind = fullOps.at(j).toArray().at(0).toString();
                    if (nextKind == QStringLiteral("S")) {
                        sawKeptStroke = true;
                        break;
                    }
                    if (nextKind != QStringLiteral("B")) {
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
            const QJsonArray op = fullOps.at(i).toArray();
            if (op.at(0).toString() == QStringLiteral("P")
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
    fieldVersions[QStringLiteral("k")] = nextVersion++;
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
        if (fullOps.at(i).toArray().at(0).toString() == QStringLiteral("F")) {
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
        const QImage thumb = mainBoard.renderFrameThumbnail(0, QSize(160, 120));
        bool foundRedThumbPixel = false;
        for (int y = 0; y < thumb.height() && !foundRedThumbPixel; ++y) {
            for (int x = 0; x < thumb.width(); ++x) {
                const QColor c = thumb.pixelColor(x, y);
                if (c.red() > 150 && c.green() < 100 && c.blue() < 100) {
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
        const QString kind = fullOps.at(i).toArray().at(0).toString();
        if (kind == QStringLiteral("F") || kind == QStringLiteral("f")) {
            std::fprintf(stderr, "Expected no F or f ops after clearing field view\n");
            return false;
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);

    if (!runClipAndFlushUnitTests()) {
        return 1;
    }

#ifdef ANIMEAN_WITH_PYTHON
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pythonHome = QStringLiteral(ANIMEAN_PYTHON_HOME);
    if (!QFileInfo::exists(QDir(pythonHome).filePath(QStringLiteral("python312.dll")))) {
        pythonHome = QDir(appDir).filePath(QStringLiteral(ANIMEAN_PYTHON_DIR_NAME));
    }

    QStringList pythonPath;
    if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_PYFILE_DIR)).filePath(QStringLiteral("hello_world.py")))) {
        pythonPath << QStringLiteral(ANIMEAN_PYFILE_DIR);
    }
    pythonPath << appDir;
    if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_PYTHONBIND_DIR)).filePath(QStringLiteral("animemodel.py")))) {
        pythonPath << QStringLiteral(ANIMEAN_PYTHONBIND_DIR);
    }
    if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_OPENTOONZ_TOOLS_DIR)).filePath(QStringLiteral("toonz_to_dict.py")))) {
        pythonPath << QStringLiteral(ANIMEAN_OPENTOONZ_TOOLS_DIR);
    }

    QStringList dllPath;
    dllPath << pythonHome << QDir(pythonHome).filePath(QStringLiteral("DLLs"));
    const QByteArray existingPath = qgetenv("PATH");
    if (!existingPath.isEmpty()) {
        dllPath << QString::fromLocal8Bit(existingPath);
    }

    qputenv("PATH", QDir::toNativeSeparators(dllPath.join(QLatin1Char(';'))).toLocal8Bit());
    qputenv("PYTHONHOME", QDir::toNativeSeparators(pythonHome).toLocal8Bit());
    qputenv("PYTHONPATH", QDir::toNativeSeparators(pythonPath.join(QLatin1Char(';'))).toLocal8Bit());

    py::scoped_interpreter guard;
    py::module_::import("animean_python");
#endif

    if (!runFieldScopeAndHideColumnsUnitTests()) {
        return 1;
    }
#ifdef ANIMEAN_WITH_PYTHON
#endif

    PaintOpenGLWidget board;
#ifdef ANIMEAN_WITH_PYTHON
    registerAnimeanUiScene(&board.model());
#endif

    QJsonArray fullOps;
    BoardFrame lastFrame;
    bool gotFrame = false;

    board.setFrameSink([&](const BoardFrame &frame) {
        lastFrame = frame;
        gotFrame = true;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(frame.ops, &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            std::fprintf(stderr, "Invalid ops JSON at frame %llu: %s\n",
                         static_cast<unsigned long long>(frame.frame),
                         err.errorString().toUtf8().constData());
            std::exit(1);
        }

        while (fullOps.size() > frame.keep) {
            fullOps.removeLast();
        }
        const QJsonArray delta = doc.array();
        for (const QJsonValue &val : delta) {
            fullOps.append(val);
        }
    });

    const auto waitForFrame = [&]() -> BoardFrame {
        gotFrame = false;
        int spins = 0;
        while (!gotFrame && spins < 1000) {
            QCoreApplication::processEvents();
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
    if (first.ops.isEmpty() || fullOps.isEmpty()) {
        std::fprintf(stderr, "Expected non-empty first frame ops\n");
        return 1;
    }
    board.acknowledgeFrame(first.frame);

    const QPointF startPos(100.0, 120.0);
    board.injectMouse(QEvent::MouseButtonPress, startPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    {
        const BoardFrame f = waitForFrame();
        if (f.keep <= 0) {
            std::fprintf(stderr, "Expected press frame keep > 0, got %d\n", f.keep);
            return 1;
        }
        board.acknowledgeFrame(f.frame);
    }

    for (int i = 1; i <= 10; ++i) {
        const QPointF movePos(100.0 + i * 15.0, 120.0 + i * 8.0);
        board.injectMouse(QEvent::MouseMove, movePos, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        const BoardFrame f = waitForFrame();
        if (f.keep <= 0) {
            std::fprintf(stderr, "Expected move %d frame keep > 0, got %d\n", i, f.keep);
            return 1;
        }
        board.acknowledgeFrame(f.frame);
    }

    const QPointF endPos(250.0, 200.0);
    board.injectMouse(QEvent::MouseButtonRelease, endPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
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
        for (const QJsonValue &val : fullOps) {
            const QJsonArray op = val.toArray();
            if (op.at(0).toString() == QStringLiteral("T")) {
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
        QFile outFile(QString::fromLocal8Bit(argv[1]));
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "Failed to open output file: %s\n", argv[1]);
            return 1;
        }
        outFile.write(QJsonDocument(fullOps).toJson(QJsonDocument::Compact));
        outFile.close();
    }

#ifdef ANIMEAN_WITH_PYTHON
    unregisterAnimeanUiScene(&board.model());
#endif

    std::printf("BOARD_HEADLESS_OK\n");
    std::fflush(stdout);
    return 0;
}
