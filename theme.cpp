#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

#ifdef ANIMEAN_WITH_PYTHON
#include "pythonbind/python_bindings.h"
#endif

namespace {
// The two tables ARE the design system: every colour in the application is
// either one of these or a QPalette entry derived from them below.
struct ThemeColors {
    QColor window;
    QColor surface;
    QColor surfaceAlt;
    QColor text;
    QColor textDim;
    QColor divider;
    QColor accent;
    QColor accentHover;
    QColor accentActive;
    QColor chipRest;
    QColor chipRestFg;
    QColor chipHover;
    QColor chipHoverFg;
    QColor viewportSurround;
    QColor onionPast;
    QColor onionAhead;
};

const ThemeColors &colorsFor(AnimeTheme::Mode mode)
{
    static const ThemeColors dark = {
        QColor(QStringLiteral("#1d1f20")),   // window
        QColor(QStringLiteral("#232526")),   // surface
        QColor(QStringLiteral("#2a2c2e")),   // surfaceAlt
        QColor(QStringLiteral("#f5f5f8")),   // text
        QColor(QStringLiteral("#b4b6bc")),   // textDim
        QColor(QStringLiteral("#575a5c")),   // divider
        QColor(QStringLiteral("#3d8ec9")),   // accent
        QColor(QStringLiteral("#58a5dd")),   // accentHover
        QColor(QStringLiteral("#2f74a6")),   // accentActive
        QColor(QStringLiteral("#2a2c2e")),   // chipRest
        QColor(QStringLiteral("#b4b6bc")),   // chipRestFg
        QColor(QStringLiteral("#35383b")),   // chipHover
        QColor(QStringLiteral("#f5f5f8")),   // chipHoverFg
        QColor(QStringLiteral("#141516")),   // viewportSurround
        QColor(QStringLiteral("#b3392f")),   // onionPast
        QColor(QStringLiteral("#2f7a4f"))    // onionAhead
    };
    static const ThemeColors light = {
        QColor(QStringLiteral("#f5f5f6")),   // window
        QColor(QStringLiteral("#ffffff")),   // surface
        QColor(QStringLiteral("#e8e8ea")),   // surfaceAlt
        QColor(QStringLiteral("#1d1f20")),   // text
        QColor(QStringLiteral("#6b6d72")),   // textDim
        QColor(QStringLiteral("#bcbdc0")),   // divider
        QColor(QStringLiteral("#3d8ec9")),   // accent
        QColor(QStringLiteral("#2f74a6")),   // accentHover
        QColor(QStringLiteral("#245c85")),   // accentActive
        QColor(QStringLiteral("#fbfbfc")),   // chipRest
        QColor(QStringLiteral("#4a4c51")),   // chipRestFg
        QColor(QStringLiteral("#ffffff")),   // chipHover
        QColor(QStringLiteral("#1d1f20")),   // chipHoverFg
        QColor(QStringLiteral("#cfd0d3")),   // viewportSurround
        QColor(QStringLiteral("#b3392f")),   // onionPast
        QColor(QStringLiteral("#2f7a4f"))    // onionAhead
    };
    return mode == AnimeTheme::Mode::Dark ? dark : light;
}

QPalette buildPalette(AnimeTheme::Mode mode)
{
    const ThemeColors &colors = colorsFor(mode);
    const bool dark = mode == AnimeTheme::Mode::Dark;
    const QColor white(QStringLiteral("#ffffff"));
    const QColor disabledText(dark ? QStringLiteral("#6f7175") : QStringLiteral("#a6a7ab"));
    const QColor disabledHighlight(dark ? QStringLiteral("#2f3234") : QStringLiteral("#dcdcde"));

    QPalette palette;
    palette.setColor(QPalette::Window, colors.window);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::Base, colors.surface);
    palette.setColor(QPalette::AlternateBase, colors.surfaceAlt);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::PlaceholderText, colors.textDim);
    palette.setColor(QPalette::Button, colors.surfaceAlt);
    palette.setColor(QPalette::ButtonText, colors.text);
    palette.setColor(QPalette::BrightText, colors.onionPast);
    palette.setColor(QPalette::ToolTipBase, colors.surfaceAlt);
    palette.setColor(QPalette::ToolTipText, colors.text);
    palette.setColor(QPalette::Highlight, colors.accent);
    palette.setColor(QPalette::HighlightedText, white);
    palette.setColor(QPalette::Link, colors.accentHover);
    palette.setColor(QPalette::LinkVisited, colors.accentActive);

    // Fusion draws every bevel and frame from the shading roles, and Mid is
    // the hairline the tool-option stylesheets ask for as palette(mid). In a
    // dark theme the "light" end has to be lighter than Button, not darker,
    // or every raised edge reads as a hole.
    palette.setColor(QPalette::Light, QColor(dark ? QStringLiteral("#4a4d50") : QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Midlight, QColor(dark ? QStringLiteral("#3d4043") : QStringLiteral("#f2f2f4")));
    palette.setColor(QPalette::Mid, colors.divider);
    palette.setColor(QPalette::Dark, QColor(dark ? QStringLiteral("#141516") : QStringLiteral("#9a9b9f")));
    palette.setColor(QPalette::Shadow, QColor(dark ? QStringLiteral("#000000") : QStringLiteral("#6d6e72")));

    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                                     QPalette::ToolTipText, QPalette::HighlightedText,
                                     QPalette::PlaceholderText}) {
        palette.setColor(QPalette::Disabled, role, disabledText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Base, colors.window);
    palette.setColor(QPalette::Disabled, QPalette::Button, colors.window);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, disabledHighlight);
    return palette;
}

QString appStyleSheet(AnimeTheme::Mode mode)
{
    const ThemeColors &colors = colorsFor(mode);
    // QToolTip is the one common widget the style paints from its own tooltip
    // palette rather than the application palette, so it needs a rule of its
    // own; Fusion reads everything else straight off the palette.
    return QStringLiteral("QToolTip {"
                          " color: %1;"
                          " background-color: %2;"
                          " border: 1px solid %3;"
                          " padding: 2px 4px;"
                          "}")
        .arg(colors.text.name(), colors.surfaceAlt.name(), colors.divider.name());
}

AnimeTheme::Mode g_mode = AnimeTheme::Mode::Dark;
bool g_modeLoaded = false;
}

AnimeTheme::AnimeTheme(QObject *parent)
    : QObject(parent)
{
}

AnimeTheme *AnimeTheme::instance()
{
    static AnimeTheme theme;
    return &theme;
}

AnimeTheme::Mode AnimeTheme::mode()
{
    if (!g_modeLoaded) {
        // Anything but an explicit "light" means Dark: an absent or damaged
        // setting must land on the default, not on a half-applied theme.
        QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
        const QString saved = settings.value(QStringLiteral("theme/mode")).toString();
        g_mode = saved.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0 ? Mode::Light
                                                                                  : Mode::Dark;
        g_modeLoaded = true;
    }
    return g_mode;
}

void AnimeTheme::setMode(Mode mode)
{
    if (AnimeTheme::mode() == mode) {
        return;
    }

    g_mode = mode;
    QSettings settings(QStringLiteral("AnimeAn"), QStringLiteral("AnimeAn"));
    settings.setValue(QStringLiteral("theme/mode"), modeName(mode));
    apply(qApp);
    emit instance()->themeChanged();
}

QColor AnimeTheme::color(Role role)
{
    const ThemeColors &colors = colorsFor(mode());
    switch (role) {
    case Role::Window:
        return colors.window;
    case Role::Surface:
        return colors.surface;
    case Role::SurfaceAlt:
        return colors.surfaceAlt;
    case Role::Text:
        return colors.text;
    case Role::TextDim:
        return colors.textDim;
    case Role::Divider:
        return colors.divider;
    case Role::Accent:
        return colors.accent;
    case Role::AccentHover:
        return colors.accentHover;
    case Role::AccentActive:
        return colors.accentActive;
    case Role::ChipRest:
        return colors.chipRest;
    case Role::ChipRestFg:
        return colors.chipRestFg;
    case Role::ChipHover:
        return colors.chipHover;
    case Role::ChipHoverFg:
        return colors.chipHoverFg;
    case Role::ViewportSurround:
        return colors.viewportSurround;
    case Role::OnionPast:
        return colors.onionPast;
    case Role::OnionAhead:
        return colors.onionAhead;
    }
    return colors.text;
}

void AnimeTheme::apply(QApplication *app)
{
    if (!app) {
        return;
    }

    // Fusion is the only style that honours a full custom palette on every
    // platform - the native Windows styles paint their own chrome whatever the
    // palette says. Swapping the style again on a mode change would rebuild
    // every widget's style for nothing, so it is set once.
    if (!app->style()
        || app->style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
        app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }
    app->setPalette(buildPalette(mode()));
    app->setStyleSheet(appStyleSheet(mode()));
#ifdef ANIMEAN_WITH_PYTHON
    setAnimeanUiTheme(modeName(mode()));
#endif
}

QString AnimeTheme::modeName(Mode mode)
{
    return mode == Mode::Light ? QStringLiteral("light") : QStringLiteral("dark");
}
