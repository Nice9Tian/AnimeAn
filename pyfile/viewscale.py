"""THE shared home of screen <-> canvas conversion for the Python tools.

Python mirror of algorithm/viewscale.h - keep the two in step. Anything that
turns a viewport position into a canvas position, or sizes a threshold that
belongs to the USER's eye and hand rather than to the drawing, belongs here;
do not re-derive ``/ zoom`` in a tool. Every call site should carry a short
comment pointing back here so the next reader knows where the wheel lives.

The view transform is ``screen = document * zoom + pan``. Everything below is
that one equation, named.

WHICH SPACE IS A NUMBER IN?
    Screen px   - anything the hand or the eye owns: hit-test slop, handle pick
                  radii, the spacing at which a user can tell two anchors
                  apart. These do NOT change when the canvas is zoomed.
    Canvas px   - anything the artwork owns: stroke geometry, arc lengths,
                  flattening steps. These do not change when the view is
                  zoomed.

Hook messages carry ``zoom``; a tool that scales a screen-px constant should
read it through :func:`to_canvas_length` rather than dividing by hand, so a
missing or absurd zoom degrades to 1.0 in exactly one place.
"""


def safe_zoom(zoom):
    """Zoom is a divisor everywhere, so guard it once, here."""
    try:
        value = float(zoom)
    except (TypeError, ValueError):
        return 1.0
    if not (value > 0.0) or value >= 1e9 or value != value:   # NaN fails value == value
        return 1.0
    return value


# --- points -----------------------------------------------------------------

def to_canvas(screen, zoom, pan=(0.0, 0.0)):
    z = safe_zoom(zoom)
    return ((screen[0] - pan[0]) / z, (screen[1] - pan[1]) / z)


def to_screen(canvas, zoom, pan=(0.0, 0.0)):
    z = safe_zoom(zoom)
    return (canvas[0] * z + pan[0], canvas[1] * z + pan[1])


# --- lengths ----------------------------------------------------------------
# A length has no pan term: only the scale survives.

def to_canvas_length(screen_px, zoom):
    """How much artwork one screen-sized measurement covers.

    Use this to spend a screen-px budget (a pick radius, a spacing the user
    must be able to resolve by eye) on canvas geometry.
    """
    return screen_px / safe_zoom(zoom)


def to_screen_length(canvas_px, zoom):
    """How big a piece of artwork looks right now."""
    return canvas_px * safe_zoom(zoom)


# --- clamped budgets --------------------------------------------------------

def pixel_scale(zoom, min_zoom=0.25, max_zoom=16.0):
    """Canvas px per screen px, with the zoom clamped first.

    For budgets converted ONCE and then governing a whole gesture. Pure
    geometry (a cursor position, a hit test) must NOT use this - it needs the
    true zoom so it stays exact at every magnification.
    """
    z = min(max_zoom, max(min_zoom, safe_zoom(zoom)))
    return 1.0 / z
