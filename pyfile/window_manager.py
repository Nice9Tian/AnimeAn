"""Window policy for auto tools: which panel to surface, which page to show.

The mechanism is C++ (ui.windows knows the parent windows and their pages);
what a tool WANTS to be looking at is a decision, and decisions live here. A
name the shell does not know is a quiet no-op on purpose - a tool that asks
for a window a future layout dropped must not take the run down with it.

Window names: "paint", "tools", "tool_options", "layers", "assets", "history",
"repulsion_pad", "python_debug". The timeline is NOT one of them: it is a dock
window, but not a PARENT window - it has no pages to address, so ui.windows
does not list it.

"paint" is the CENTRAL area, pages "drawing" (the main board) and "texture"
(the texture board at full size). It answers list/select/current like any
other window, but show() on it is a no-op: the central widget is what the
window's growth is absorbed by and there is no state in which it is hidden.
Selecting "texture" also takes the texture board away from its sub-control -
exactly one surface owns that board at a time - and selecting "drawing" hands
it back.
"""


def _windows():
    import animean_python
    return animean_python.ui.windows


def show(name, on=True):
    _windows().show(str(name), bool(on))


def select(name, page):
    _windows().select(str(name), str(page))


def current(name):
    return _windows().current(str(name))


def focus_layers(view):
    """Point the Layers window at a board's page ("main" / "child").

    Selection only: showing a window the user hid is a bigger decision than
    "look at this board", and it stays the caller's to make through show().
    """
    page = "child" if str(view) == "child" else "main"
    select("layers", page)
    return page


def focus_board(view):
    """Bring a board's page up in the central paint area ("main" / "child").

    The one window whose pages are DOCUMENTS rather than panels, so a tool that
    wants the user looking at a particular board says so here instead of
    hunting for whatever surface happens to be showing it.
    """
    page = "texture" if str(view) == "child" else "drawing"
    select("paint", page)
    return page


def layout():
    return _windows().list()
