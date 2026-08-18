#include "mainwindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

#include <string>

namespace py = pybind11;

static QString pythonStartupText()
{
    try {
        py::module_::import("animean_python");
        const std::string helloText = py::module_::import("hello_world")
                                          .attr("hello_world")()
                                          .cast<std::string>();
        return QString::fromUtf8(helloText.c_str());
    } catch (const py::error_already_set &error) {
        return QStringLiteral("Python error: %1").arg(QString::fromUtf8(error.what()));
    }
}
#endif

int main(int argc, char *argv[])
{
    // Multisampling for every GL surface in the app. QPainter's OpenGL engine
    // leans on the framebuffer's samples for smooth path edges - with the
    // default of 0 the paint views render visibly stair-stepped lines, which
    // no render hint can fix from above. 8x supersamples each screen pixel
    // and resolves (interpolates) the coverage, which is exactly the
    // "sample finer than the screen, then blend" the aliasing calls for.
    // MUST run before QApplication: QOpenGLWidget contexts are created from
    // the default format, and drivers clamp the count to what they support.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

#ifdef ANIMEAN_WITH_PYTHON
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pythonHome = QDir(appDir).filePath(QStringLiteral(ANIMEAN_PYTHON_DIR_NAME));
    if (!QFileInfo::exists(QDir(pythonHome).filePath(QStringLiteral("python312.dll")))) {
        pythonHome = QStringLiteral(ANIMEAN_PYTHON_HOME);
    }

    QStringList pythonPath;
    pythonPath << appDir;
    if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_PYFILE_DIR)).filePath(QStringLiteral("hello_world.py")))) {
        pythonPath << QStringLiteral(ANIMEAN_PYFILE_DIR);
    }
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

    MainWindow window;
#ifdef ANIMEAN_WITH_PYTHON
    window.setStatusText(pythonStartupText());
#else
    window.setStatusText(QStringLiteral("Python disabled"));
#endif
    window.show();
    return app.exec();
}
