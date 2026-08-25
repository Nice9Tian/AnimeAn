#ifndef CENTRALPAINTAREA_H
#define CENTRALPAINTAREA_H

#include <QString>
#include <QStringList>
#include <QWidget>

class PaintViewContainer;
class QLabel;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;

// The central widget: a two page tab strip over a stack. Page "drawing" holds
// the main board and is the surface the window's growth is absorbed by; page
// "texture" is where the texture panel goes when the user asks to see it full
// size. Same strip as a ParentWindow wears, on purpose - a page is a page
// wherever it lives - but this is NOT a dock: the central area cannot be
// hidden, which is why ui.windows treats show() on it as a no-op.
class CentralPaintArea : public QWidget
{
    Q_OBJECT

public:
    explicit CentralPaintArea(QWidget *parent = nullptr);

    // The stable identity Python addresses through ui.windows.
    QString name() const;

    // The main board's container. Everything that used to find it by casting
    // centralWidget() asks for it here instead.
    PaintViewContainer *mainContainer() const;

    // Puts a widget on the texture page (null empties it back to the
    // placeholder). The page does NOT own the widget: the shell moves the one
    // texture panel between here, a sub-control frame and a parking spot.
    void setTextureContent(QWidget *widget);
    QWidget *textureContent() const;

    QStringList pageNames() const;
    QString currentPage() const;
    bool selectPage(const QString &pageName);

signals:
    void pageChanged(const QString &pageName);

private:
    void applyTheme();

    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_stack = nullptr;
    QStringList m_pageNames;
    PaintViewContainer *m_mainContainer = nullptr;
    QWidget *m_texturePage = nullptr;
    QVBoxLayout *m_textureLayout = nullptr;
    QLabel *m_texturePlaceholder = nullptr;
    QWidget *m_textureContent = nullptr;
};

#endif // CENTRALPAINTAREA_H
