#include "mainwindow.h"

#include <QApplication>

#ifdef ANIMEAN_WITH_PYTHON
#include <QDir>

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
#ifdef ANIMEAN_WITH_PYTHON
    qputenv("PYTHONHOME", QDir::toNativeSeparators(QStringLiteral(ANIMEAN_PYTHON_HOME)).toLocal8Bit());
    qputenv("PYTHONPATH", QDir::toNativeSeparators(QStringLiteral(ANIMEAN_SOURCE_DIR)).toLocal8Bit());

    pybind11::scoped_interpreter pythonGuard;
#endif

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
