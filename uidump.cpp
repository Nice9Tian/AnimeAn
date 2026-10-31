#include "uidump.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPalette>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QWidget>

namespace {

QJsonObject rectJson(const QRect &r)
{
    return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
            {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
}

QJsonObject widgetJson(QWidget *w, QWidget *window)
{
    QJsonObject out;
    out[QStringLiteral("class")] = QString::fromLatin1(w->metaObject()->className());
    out[QStringLiteral("name")] = w->objectName();
    out[QStringLiteral("visible")] = w->isVisible();
    out[QStringLiteral("enabled")] = w->isEnabled();
    const QPoint origin = w->mapTo(window, QPoint(0, 0));
    out[QStringLiteral("rect")] = rectJson(QRect(origin, w->size()));

    const QFont font = w->font();
    out[QStringLiteral("font")] = QJsonObject{
        {QStringLiteral("family"), font.family()},
        {QStringLiteral("pt"), font.pointSizeF()},
        {QStringLiteral("px"), font.pixelSize()},
        {QStringLiteral("weight"), font.weight()},
    };
    const QPalette palette = w->palette();
    out[QStringLiteral("palette")] = QJsonObject{
        {QStringLiteral("window"), palette.color(QPalette::Window).name()},
        {QStringLiteral("text"), palette.color(QPalette::WindowText).name()},
        {QStringLiteral("button"), palette.color(QPalette::Button).name()},
        {QStringLiteral("base"), palette.color(QPalette::Base).name()},
        {QStringLiteral("highlight"), palette.color(QPalette::Highlight).name()},
    };
    if (!w->styleSheet().isEmpty()) {
        out[QStringLiteral("styleSheet")] = w->styleSheet();
    }

    if (auto *button = qobject_cast<QAbstractButton *>(w)) {
        out[QStringLiteral("text")] = button->text();
        out[QStringLiteral("checkable")] = button->isCheckable();
        out[QStringLiteral("checked")] = button->isChecked();
    } else if (auto *label = qobject_cast<QLabel *>(w)) {
        out[QStringLiteral("text")] = label->text();
    } else if (auto *edit = qobject_cast<QLineEdit *>(w)) {
        out[QStringLiteral("text")] = edit->text();
    } else if (auto *spin = qobject_cast<QAbstractSpinBox *>(w)) {
        out[QStringLiteral("text")] = spin->text();
    } else if (auto *combo = qobject_cast<QComboBox *>(w)) {
        out[QStringLiteral("text")] = combo->currentText();
    } else if (auto *slider = qobject_cast<QAbstractSlider *>(w)) {
        out[QStringLiteral("value")] = slider->value();
        out[QStringLiteral("min")] = slider->minimum();
        out[QStringLiteral("max")] = slider->maximum();
        out[QStringLiteral("orientation")] = int(slider->orientation());
    } else if (auto *dock = qobject_cast<QDockWidget *>(w)) {
        out[QStringLiteral("text")] = dock->windowTitle();
        out[QStringLiteral("floating")] = dock->isFloating();
    } else if (auto *tabs = qobject_cast<QTabBar *>(w)) {
        QJsonArray list;
        for (int i = 0; i < tabs->count(); ++i) {
            list.append(QJsonObject{{QStringLiteral("text"), tabs->tabText(i)},
                                    {QStringLiteral("rect"), rectJson(tabs->tabRect(i).translated(origin))}});
        }
        out[QStringLiteral("tabs")] = list;
        out[QStringLiteral("current")] = tabs->currentIndex();
    } else if (auto *bar = qobject_cast<QMenuBar *>(w)) {
        QJsonArray list;
        for (QAction *action : bar->actions()) {
            list.append(QJsonObject{{QStringLiteral("text"), action->text()},
                                    {QStringLiteral("rect"), rectJson(bar->actionGeometry(action).translated(origin))}});
        }
        out[QStringLiteral("actions")] = list;
    }

    QJsonArray children;
    for (QObject *child : w->children()) {
        if (auto *cw = qobject_cast<QWidget *>(child)) {
            if (!cw->isWindow()) {
                children.append(widgetJson(cw, window));
            }
        }
    }
    if (!children.isEmpty()) {
        out[QStringLiteral("children")] = children;
    }
    return out;
}

} // namespace

void scheduleUiDump(QWidget *window)
{
    const QString dir = qEnvironmentVariable("ANIMEAN_UI_DUMP");
    if (dir.isEmpty()) {
        return;
    }
    QTimer::singleShot(2500, window, [window, dir]() {
        QDir().mkpath(dir);
        window->grab().save(QDir(dir).filePath(QStringLiteral("qt_grab.png")));
        QFile file(QDir(dir).filePath(QStringLiteral("qt_widgets.json")));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(widgetJson(window, window)).toJson());
        }
    });
}
