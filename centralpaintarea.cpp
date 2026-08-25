#include "centralpaintarea.h"

#include "paintviewcontainer.h"
#include "parentwindow.h"
#include "theme.h"

#include <QLabel>
#include <QPalette>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace {
const char kDrawingPage[] = "drawing";
const char kTexturePage[] = "texture";
}

CentralPaintArea::CentralPaintArea(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("CentralPaintArea"));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_tabs = new QTabBar(this);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(false);
    outer->addWidget(m_tabs);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack, 1);

    // The main board keeps its old minimum: it is still the surface the window
    // is sized against, and the tab strip above it costs 24px, not a page.
    m_mainContainer = new PaintViewContainer(m_stack);
    m_mainContainer->setMinimumSize(320, 240);
    m_stack->addWidget(m_mainContainer);
    m_tabs->addTab(QStringLiteral("Drawing"));
    m_pageNames.append(QString::fromLatin1(kDrawingPage));

    m_texturePage = new QWidget(m_stack);
    m_textureLayout = new QVBoxLayout(m_texturePage);
    m_textureLayout->setContentsMargins(0, 0, 0, 0);
    m_textureLayout->setSpacing(0);
    m_texturePlaceholder = new QLabel(
        QStringLiteral("The texture view is open in its own control."), m_texturePage);
    m_texturePlaceholder->setAlignment(Qt::AlignCenter);
    m_texturePlaceholder->setWordWrap(true);
    m_textureLayout->addWidget(m_texturePlaceholder, 1);
    m_stack->addWidget(m_texturePage);
    m_tabs->addTab(QStringLiteral("Texture"));
    m_pageNames.append(QString::fromLatin1(kTexturePage));

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        if (index >= 0 && index < m_pageNames.size()) {
            emit pageChanged(m_pageNames.at(index));
        }
    });
    m_tabs->setCurrentIndex(0);
    m_stack->setCurrentIndex(0);

    applyTheme();
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, [this]() { applyTheme(); });
}

QString CentralPaintArea::name() const
{
    return QStringLiteral("paint");
}

PaintViewContainer *CentralPaintArea::mainContainer() const
{
    return m_mainContainer;
}

void CentralPaintArea::setTextureContent(QWidget *widget)
{
    if (m_textureContent == widget) {
        return;
    }
    if (m_textureContent) {
        // Out of the layout only - the shell is moving it somewhere, and a
        // null parent on the way there would flash it as a window.
        m_textureLayout->removeWidget(m_textureContent);
    }
    m_textureContent = widget;
    if (widget) {
        widget->setParent(m_texturePage);
        m_textureLayout->addWidget(widget, 1);
        widget->show();
    }
    m_texturePlaceholder->setVisible(m_textureContent == nullptr);
}

QWidget *CentralPaintArea::textureContent() const
{
    return m_textureContent;
}

QStringList CentralPaintArea::pageNames() const
{
    return m_pageNames;
}

QString CentralPaintArea::currentPage() const
{
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_pageNames.size() ? m_pageNames.at(index) : QString();
}

bool CentralPaintArea::selectPage(const QString &pageName)
{
    const int index = m_pageNames.indexOf(pageName);
    if (index < 0) {
        return false;
    }
    m_tabs->setCurrentIndex(index);
    return true;
}

void CentralPaintArea::applyTheme()
{
    m_tabs->setStyleSheet(ParentWindow::tabStyleSheet(palette().color(QPalette::HighlightedText)));
    m_texturePlaceholder->setStyleSheet(
        QStringLiteral("color: %1;").arg(AnimeTheme::color(AnimeTheme::Role::TextDim).name()));
    setAutoFillBackground(true);
    QPalette areaPalette = palette();
    areaPalette.setColor(QPalette::Window, AnimeTheme::color(AnimeTheme::Role::Window));
    setPalette(areaPalette);
}
