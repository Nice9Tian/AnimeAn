#include "openglwidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
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

#include <cstdio>

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);

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
