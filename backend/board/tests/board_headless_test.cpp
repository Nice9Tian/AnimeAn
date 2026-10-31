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
