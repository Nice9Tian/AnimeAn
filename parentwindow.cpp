#include "parentwindow.h"

#include "theme.h"

#include <QPalette>
#include <QTabBar>
#include <QTabWidget>

ParentWindow::ParentWindow(const QString &name, const QString &title, QWidget *parent)
    : QDockWidget(title, parent)
    , m_name(name)
{
    setObjectName(QStringLiteral("ParentWindow_%1").arg(name));

    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::North);
    // documentMode drops the frame Qt draws around the page stack, so the
    // tabs sit directly on the dock's ground instead of inside a second box.
    m_tabs->setDocumentMode(true);
    // Never auto-hide: the strip is how a window announces itself, and a
    // window that loses its label when it drops to one page reads as a
    // different kind of surface from its two-page neighbour.
    m_tabs->setTabBarAutoHide(false);
    m_tabs->tabBar()->setVisible(true);
    m_tabs->tabBar()->setExpanding(false);
    setWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_pageNames.size()) {
            emit pageChanged(m_pageNames.at(index));
        }
    });

    applyTheme();
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() {
        applyTheme();
    });
}

QString ParentWindow::name() const
{
    return m_name;
}

void ParentWindow::addPage(const QString &pageName, const QString &pageTitle, QWidget *widget)
{
    if (!widget || pageName.isEmpty() || m_pageNames.contains(pageName)) {
        return;
    }

    m_pageNames.append(pageName);
    m_tabs->addTab(widget, pageTitle.isEmpty() ? pageName : pageTitle);
    // addTab may re-run the auto-hide decision; re-assert it rather than trust
    // the order in which pages arrive.
    m_tabs->tabBar()->setVisible(true);
}

bool ParentWindow::selectPage(const QString &pageName)
{
    const int index = m_pageNames.indexOf(pageName);
    if (index < 0) {
        return false;
    }
    m_tabs->setCurrentIndex(index);
    return true;
}

QString ParentWindow::currentPage() const
{
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_pageNames.size() ? m_pageNames.at(index) : QString();
}

QWidget *ParentWindow::pageWidget(const QString &pageName) const
{
    const int index = m_pageNames.indexOf(pageName);
    return index >= 0 ? m_tabs->widget(index) : nullptr;
}

QStringList ParentWindow::pageNames() const
{
    return m_pageNames;
}

void ParentWindow::applyTheme()
{
    // Fusion paints tabs from the palette, but the selected tab has to read as
    // the ACTIVE page rather than as one more button, and that is an accent
    // the palette has no role for.
    const QString style =
        QStringLiteral("QTabWidget::pane { border: 0; background: %1; }"
                       "QTabBar { qproperty-drawBase: 0; background: %2; }"
                       "QTabBar::tab {"
                       " background: %3; color: %4;"
                       " border: 1px solid %5; border-bottom: 0;"
                       " padding: 3px 10px; margin-right: 2px;"
                       "}"
                       "QTabBar::tab:hover { background: %6; color: %7; }"
                       "QTabBar::tab:selected { background: %8; color: %9; border-color: %8; }")
            .arg(AnimeTheme::color(AnimeTheme::Role::Surface).name(),
                 AnimeTheme::color(AnimeTheme::Role::Window).name(),
                 AnimeTheme::color(AnimeTheme::Role::ChipRest).name(),
                 AnimeTheme::color(AnimeTheme::Role::ChipRestFg).name(),
                 AnimeTheme::color(AnimeTheme::Role::Divider).name(),
                 AnimeTheme::color(AnimeTheme::Role::ChipHover).name(),
                 AnimeTheme::color(AnimeTheme::Role::ChipHoverFg).name(),
                 AnimeTheme::color(AnimeTheme::Role::Accent).name(),
                 // Whatever reads on the accent in this mode - the same
                 // decision the palette already made for a selected row.
                 palette().color(QPalette::HighlightedText).name());
    m_tabs->setStyleSheet(style);
}
