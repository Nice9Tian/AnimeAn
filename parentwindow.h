#ifndef PARENTWINDOW_H
#define PARENTWINDOW_H

#include <QDockWidget>
#include <QString>
#include <QStringList>

class QTabWidget;

// A dock that holds PAGES instead of one widget: the top tab bar is the whole
// point, so it stays visible even for a single page - a one-page window still
// says what it is, and gaining a second page changes nothing about how it
// reads. Pure mechanism: which windows exist and which pages they hold is the
// shell's business (MainWindow), and Python only asks for them by name
// through ui.windows.
class ParentWindow : public QDockWidget
{
    Q_OBJECT

public:
    // `name` is the stable identity Python addresses (also the objectName
    // suffix); `title` is what the user reads on the title bar.
    ParentWindow(const QString &name, const QString &title, QWidget *parent = nullptr);

    QString name() const;
    // Takes ownership of `widget` through the tab widget.
    void addPage(const QString &pageName, const QString &pageTitle, QWidget *widget);
    // False when no page carries that name - a caller asking for a page that
    // does not exist has a bug, and silently landing on page 0 would hide it.
    bool selectPage(const QString &pageName);
    QString currentPage() const;
    QWidget *pageWidget(const QString &pageName) const;
    QStringList pageNames() const;

signals:
    void pageChanged(const QString &pageName);

private:
    void applyTheme();

    QString m_name;
    QTabWidget *m_tabs = nullptr;
    // Index-aligned with the tab widget: tab text is the human title, and the
    // two must be free to differ.
    QStringList m_pageNames;
};

#endif // PARENTWINDOW_H
