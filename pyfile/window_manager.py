"""Window policy for auto tools: which panel to surface, which page to show.

The mechanism is C++ (ui.windows knows the parent windows and their pages);
what a tool WANTS to be looking at is a decision, and decisions live here. A
name the shell does not know is a quiet no-op on purpose - a tool that asks
for a window a future layout dropped must not take the run down with it.

Window names: "tools", "tool_options", "layers", "frames", "assets",
"history", "repulsion_pad", "python_debug".
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


def layout():
    return _windows().list()
