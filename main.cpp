#include "mainwindow.h"

#include <QApplication>

#ifdef ANIMEAN_WITH_PYTHON
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifdef slots
#undef slots
#define ANIMEAN_RESTORE_QT_SLOTS
#endif
#include <pybind11/embed.h>
#ifdef ANIMEAN_RESTORE_QT_SLOTS
#define slots Q_SLOTS
#undef ANIMEAN_RESTORE_QT_SLOTS
#endif
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

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

    QStringList dllPath;
    dllPath << pythonHome << QDir(pythonHome).filePath(QStringLiteral("DLLs"));
    const QByteArray existingPath = qgetenv("PATH");
    if (!existingPath.isEmpty()) {
        dllPath << QString::fromLocal8Bit(existingPath);
    }

    qputenv("PATH", QDir::toNativeSeparators(dllPath.join(QLatin1Char(';'))).toLocal8Bit());
    qputenv("PYTHONHOME", QDir::toNativeSeparators(pythonHome).toLocal8Bit());
    qputenv("PYTHONPATH", QDir::toNativeSeparators(pythonPath.join(QLatin1Char(';'))).toLocal8Bit());

    pybind11::scoped_interpreter pythonGuard;
#endif

    MainWindow w;
    w.show();
    return a.exec();
}
