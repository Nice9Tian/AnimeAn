#ifndef THEME_H
#define THEME_H

#include "algorithm/core/color.h"
#include "algorithm/core/string.h"
#include "backend/platform/signalslot.h"

using namespace AnimeCore;

// The one place the application's colours live. The page reads them from the
// theme state and the board asks color(Role), so a mode switch reaches every
// surface.
class AnimeTheme
{
public:
    enum class Mode {
        Dark,
        Light
    };

    enum class Role {
        Window,             // application ground
        Surface,            // panel and field ground
        SurfaceAlt,         // raised ground: buttons, chips, slider grooves
        Text,
        TextDim,
        Divider,            // hairlines, frames, the page edge
        Accent,
        AccentHover,        // moves toward the mode's foreground contrast
        AccentActive,
        ChipRest,
        ChipRestFg,
        ChipHover,
        ChipHoverFg,
        // The board surround around the page: Deep for dark paper, the other
        // for light paper. Ask surroundFor() rather than picking one here.
        ViewportSurround,
        ViewportSurroundDeep,
        // The active board's frame while focus is elsewhere; Accent while it has focus.
        BoardFrameIdle,
        // Onion skin tints, identical in both modes: they mark time direction
        // on the artwork, not on the chrome.
        OnionPast,
        OnionAhead
    };

    // The signal source; also what a caller connects themeChanged() to.
    static AnimeTheme *instance();

    static Mode mode();
    // Applies, persists (Settings "AnimeAn"/"AnimeAn", key "theme/mode") and
    // announces the change.
    static void setMode(Mode mode);
    static Color color(Role role);
    // The surround that suits this paper: white paper on a near-black surround
    // glares, so the colour follows the paper and not only the mode.
    static Color surroundFor(const Color &paper);
    // "dark" / "light" - what animean_python.ui.theme() reports.
    static String modeName(Mode mode);

    Signal<> themeChanged;

private:
    AnimeTheme();
};

#endif // THEME_H
