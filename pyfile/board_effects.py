"""Board effects: the short animations a tool plays when its result lands.

What an effect MEANS lives here: which kind a tool plays, how long it runs,
and what it covers. The mechanisms underneath carry no meaning of their own:
C++ (`board.effect`, backend/session/backendsession.cpp) turns the canvas
geometry into board pixels and sends it to the page; the page
(webui/src/board/effects.ts) keeps the board as it looked before the change
and uncovers the new picture over it.

Call an effect BEFORE the change reaches the board: the page keeps the last
picture it drew when the effect arrives, and the next frame is the new one.

Kinds:
    radial  the new picture spreads from `center` until `radius`, inside `clip`
    stroke  the new picture is uncovered along `path`, `width` wide
    fade    the new picture fades in, over the whole board or inside `clip`

Geometry is canvas px (the artwork's space); C++ converts it with the view's
zoom and pan, the shared wheel of algorithm/viewscale.h.
"""

import math

# How long each effect runs, in ms.
FILL_MS = 750
CONNECT_MS = 450
MAPPING_FADE_MS = 800

# Added to a stroke's drawn width so its antialiased edge is covered too.
# Screen px: the page adds it after the zoom (see effects.ts).
STROKE_MARGIN_PX = 3.0


def _animean():
    import animean_python
    return animean_python


def _point(value):
    if isinstance(value, dict):
        return float(value.get("x", 0.0)), float(value.get("y", 0.0))
    return float(value[0]), float(value[1])


def flat_path(commands):
    """Path command dicts (move / line / cubic / close, as the bindings give
    them) to the board's flat path: [0, x, y, 1, x, y, 2, c1x, c1y, c2x, c2y,
    x, y, 3], the format of the `S` draw op."""
    flat = []
    for command in commands or []:
        kind = command.get("type", "line")
        if kind == "move":
            flat += [0, *_point(command["to"])]
        elif kind == "line":
            if not flat and "from" in command:
                flat += [0, *_point(command["from"])]
            flat += [1, *_point(command["to"])]
        elif kind == "cubic":
            if not flat and "from" in command:
                flat += [0, *_point(command["from"])]
            flat += [2, *_point(command["control1"]), *_point(command["control2"]),
                     *_point(command["to"])]
        elif kind == "close":
            flat.append(3)
    return flat


def path_points(flat):
    """Every coordinate pair of a flat path, control points included (they
    bound the curve, which is all a reach needs)."""
    points = []
    i = 0
    while i < len(flat):
        op = flat[i]
        count = {0: 2, 1: 2, 2: 6}.get(op, 0)
        coords = flat[i + 1:i + 1 + count]
        points += [(coords[k], coords[k + 1]) for k in range(0, len(coords) - 1, 2)]
        i += 1 + count
    return points


def reach(center, flat):
    """How far a spread from `center` must go to cover the whole path."""
    cx, cy = center
    return max((math.hypot(x - cx, y - cy) for x, y in path_points(flat)), default=0.0)


def play(view, kind, duration, **geometry):
    """Ask the page to play one effect. Never fails the tool: a board without
    a page (tests, a closed window) just has nothing to animate."""
    params = {"view": view or "main", "kind": kind, "duration": int(duration)}
    params.update({key: value for key, value in geometry.items() if value is not None})
    try:
        _animean().ui.call("board.effect", params)
        return True
    except Exception:
        return False


def _never_fails(build):
    """The tools below call these in the middle of their own work: geometry
    that cannot be read means no animation, never a failed fill or line."""
    def wrapped(*args, **kwargs):
        try:
            return build(*args, **kwargs)
        except Exception as error:
            print(f"[board_effects] {build.__name__} skipped: {error!r}")
            return False
    wrapped.__name__ = build.__name__
    wrapped.__doc__ = build.__doc__
    return wrapped


@_never_fails
def fill_spread(view, seed, commands):
    """A fill spreads from where it was clicked to the edge of its region."""
    clip = flat_path(commands)
    if not clip:
        return False
    center = _point(seed)
    return play(view, "radial", FILL_MS, center=list(center),
                radius=reach(center, clip), clip=clip)


@_never_fails
def stroke_draw(view, commands, width):
    """A generated stroke draws itself from its first point to its last."""
    path = flat_path(commands)
    if not path:
        return False
    return play(view, "stroke", CONNECT_MS, path=path, width=float(width),
                margin=STROKE_MARGIN_PX)


def mapping_fade(view="main"):
    """Auto Mapping's new output fades in over what the board showed before."""
    return play(view, "fade", MAPPING_FADE_MS)
