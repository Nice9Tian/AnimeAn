#include "net/wsserver.h"
#include "session/backendsession.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
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
#include <string>

namespace py = pybind11;

static void runPythonHelloWorld()
{
    try {
        py::module_::import("animean_python");
        (void)py::module_::import("hello_world").attr("hello_world")();
    } catch (const py::error_already_set &) {
    }
}
#endif

#include <cstdio>

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);

    quint16 port = 0;
    QString token;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--port") && i + 1 < args.size()) {
            bool ok = false;
            const uint parsed = args.at(++i).toUInt(&ok);
            if (ok && parsed <= 65535) {
                port = static_cast<quint16>(parsed);
            }
        } else if (arg.startsWith(QStringLiteral("--port="))) {
            bool ok = false;
            const uint parsed = arg.mid(7).toUInt(&ok);
            if (ok && parsed <= 65535) {
                port = static_cast<quint16>(parsed);
            }
        } else if (arg == QStringLiteral("--token") && i + 1 < args.size()) {
            token = args.at(++i);
        } else if (arg.startsWith(QStringLiteral("--token="))) {
            token = arg.mid(8);
        }
    }

    (void)AnimeTheme::instance();
    const AnimeTheme::Mode initialThemeMode = AnimeTheme::mode();
#ifdef ANIMEAN_WITH_PYTHON
    setAnimeanUiTheme(AnimeTheme::modeName(initialThemeMode));

    const QString appDir = QCoreApplication::applicationDirPath();
    QString pythonHome = QDir(appDir).filePath(QStringLiteral(ANIMEAN_PYTHON_DIR_NAME));
    if (!QFileInfo::exists(QDir(pythonHome).filePath(QStringLiteral("python312.dll")))) {
        pythonHome = QStringLiteral(ANIMEAN_PYTHON_HOME);
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
#endif

    WsServer ws(token);
    BackendSession session(&ws);
    session.initializePythonStartup();

#ifdef ANIMEAN_WITH_PYTHON
    runPythonHelloWorld();
#endif

    if (!ws.listen(port)) {
        std::fprintf(stderr, "Failed to listen on port %u\n", static_cast<unsigned int>(port));
        std::fflush(stderr);
        return 1;
    }

    std::printf("ANIMEAN_BACKEND_PORT=%u\n", static_cast<unsigned int>(ws.port()));
    std::fflush(stdout);

    return app.exec();
}
