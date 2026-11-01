#include "theme.h"

#include "algorithm/core/settings.h"

#ifdef ANIMEAN_WITH_PYTHON
#include "pythonbind/python_bindings.h"
#endif

using namespace AnimeCore;

namespace {
// The two tables ARE the design system: the page gets them as the theme state.
struct ThemeColors {
    Color window;
    Color surface;
    Color surfaceAlt;
    Color text;
    Color textDim;
    Color divider;
    Color accent;
    Color accentHover;
    Color accentActive;
    Color chipRest;
    Color chipRestFg;
    Color chipHover;
    Color chipHoverFg;
    Color viewportSurround;
    Color viewportSurroundDeep;
    Color boardFrameIdle;
    Color onionPast;
    Color onionAhead;
};

const ThemeColors &colorsFor(AnimeTheme::Mode mode)
{
    static const ThemeColors dark = {
        Color(String("#1d1f20")),   // window
        Color(String("#232526")),   // surface
        Color(String("#2a2c2e")),   // surfaceAlt
        Color(String("#f5f5f8")),   // text
        Color(String("#b4b6bc")),   // textDim
        Color(String("#575a5c")),   // divider
        Color(String("#3d8ec9")),   // accent
        Color(String("#58a5dd")),   // accentHover
        Color(String("#2f74a6")),   // accentActive
        Color(String("#2a2c2e")),   // chipRest
        Color(String("#b4b6bc")),   // chipRestFg
        Color(String("#35383b")),   // chipHover
        Color(String("#f5f5f8")),   // chipHoverFg
        Color(String("#2a2c2e")),   // viewportSurround
        Color(String("#141516")),   // viewportSurroundDeep
        Color(String("#b4b6bc")),   // boardFrameIdle
        Color(String("#b3392f")),   // onionPast
        Color(String("#2f7a4f"))    // onionAhead
    };
    static const ThemeColors light = {
        Color(String("#f5f5f6")),   // window
        Color(String("#ffffff")),   // surface
        Color(String("#e8e8ea")),   // surfaceAlt
        Color(String("#1d1f20")),   // text
        Color(String("#6b6d72")),   // textDim
        Color(String("#bcbdc0")),   // divider
        Color(String("#3d8ec9")),   // accent
        Color(String("#2f74a6")),   // accentHover
        Color(String("#245c85")),   // accentActive
        Color(String("#fbfbfc")),   // chipRest
        Color(String("#4a4c51")),   // chipRestFg
        Color(String("#ffffff")),   // chipHover
        Color(String("#1d1f20")),   // chipHoverFg
        Color(String("#cfd0d3")),   // viewportSurround
        Color(String("#141516")),   // viewportSurroundDeep
        Color(String("#bcbdc0")),   // boardFrameIdle
        Color(String("#b3392f")),   // onionPast
        Color(String("#2f7a4f"))    // onionAhead
    };
    return mode == AnimeTheme::Mode::Dark ? dark : light;
}

AnimeTheme::Mode g_mode = AnimeTheme::Mode::Dark;
bool g_modeLoaded = false;
}

AnimeTheme::AnimeTheme() = default;

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
        Settings settings;
        const String saved = settings.value(String("theme/mode")).toString();
        g_mode = saved.compare(String("light"), CaseInsensitive) == 0 ? Mode::Light
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
    Settings settings;
    settings.setValue(String("theme/mode"), modeName(mode));
#ifdef ANIMEAN_WITH_PYTHON
    setAnimeanUiTheme(modeName(mode));
#endif
    instance()->themeChanged();
}

Color AnimeTheme::color(Role role)
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

Color AnimeTheme::surroundFor(const Color &paper)
{
    return color(paper.lightnessF() < 0.5 ? Role::ViewportSurroundDeep : Role::ViewportSurround);
}

String AnimeTheme::modeName(Mode mode)
{
    return mode == Mode::Light ? String("light") : String("dark");
}
