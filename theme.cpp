#include "theme.h"

#include <QSettings>

#ifdef ANIMEAN_WITH_PYTHON
#include "pythonbind/python_bindings.h"
#endif

namespace {
// The two tables ARE the design system: the page gets them as the theme state.
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
    QColor viewportSurroundDeep;
    QColor boardFrameIdle;
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
        QColor(QStringLiteral("#2a2c2e")),   // viewportSurround
        QColor(QStringLiteral("#141516")),   // viewportSurroundDeep
        QColor(QStringLiteral("#b4b6bc")),   // boardFrameIdle
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
        QColor(QStringLiteral("#141516")),   // viewportSurroundDeep
        QColor(QStringLiteral("#bcbdc0")),   // boardFrameIdle
        QColor(QStringLiteral("#b3392f")),   // onionPast
        QColor(QStringLiteral("#2f7a4f"))    // onionAhead
    };
    return mode == AnimeTheme::Mode::Dark ? dark : light;
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
#ifdef ANIMEAN_WITH_PYTHON
    setAnimeanUiTheme(modeName(mode));
#endif
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
    case Role::ViewportSurroundDeep:
        return colors.viewportSurroundDeep;
    case Role::BoardFrameIdle:
        return colors.boardFrameIdle;
    case Role::OnionPast:
        return colors.onionPast;
    case Role::OnionAhead:
        return colors.onionAhead;
    }
    return colors.text;
}

QColor AnimeTheme::surroundFor(const QColor &paper)
{
    return color(paper.lightnessF() < 0.5 ? Role::ViewportSurroundDeep : Role::ViewportSurround);
}

QString AnimeTheme::modeName(Mode mode)
{
    return mode == Mode::Light ? QStringLiteral("light") : QStringLiteral("dark");
}
