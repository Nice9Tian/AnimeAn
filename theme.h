#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;

// The one place the application's colours live. Widgets Qt can style through
// QPalette need nothing from here; custom-painted widgets and the stylesheet
// strings built in C++ ask color(Role) instead of carrying a literal, so a
// mode switch reaches every surface.
class AnimeTheme : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Dark,
        Light
    };

    enum class Role {
        Window,             // application ground
        Surface,            // panel and field ground (QPalette::Base)
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
        // The board surround around the page. The paper stays white in both
        // modes, so the surround carries the whole light/dark difference.
        ViewportSurround,
        // Onion skin tints, identical in both modes: they mark time direction
        // on the artwork, not on the chrome.
        OnionPast,
        OnionAhead
    };

    // The signal source; also what a caller connects themeChanged() to.
    static AnimeTheme *instance();

    static Mode mode();
    // Applies, persists (QSettings "AnimeAn"/"AnimeAn", key "theme/mode") and
    // announces the change.
    static void setMode(Mode mode);
    static QColor color(Role role);
    // Fusion, the full palette, and the little stylesheet the palette cannot
    // reach. Must run before the first widget is constructed: a widget reads
    // the application palette as it is built.
    static void apply(QApplication *app);
    // "dark" / "light" - what animean_python.ui.theme() reports.
    static QString modeName(Mode mode);

signals:
    void themeChanged();

private:
    explicit AnimeTheme(QObject *parent = nullptr);
};

#endif // THEME_H
