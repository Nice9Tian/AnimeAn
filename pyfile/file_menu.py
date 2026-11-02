"""The File menu's flows and the canvas size asked at startup.

Which picker opens, the question before a file is replaced, the question before
an import is scaled to the canvas, and the message after a failure are decided
here (developer_guide.md, "分工"). C++ does the file work (project.*,
file.import, file.texture) and forwards the File menu's items here as "menu"
events with menu "file"; the page draws the dialogs.
"""

import dialogs
import python_hooks

MENU = "file"

PROJECT = [{"name": "AnimeAn Projects", "extensions": ["anproj"]}]
TEXTURE = [{"name": "AnimeAn Texture Views", "extensions": ["textureview"]}]
IMAGES = [{"name": "Images", "extensions": ["png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"]}]
PLI = [{"name": "OpenToonz Vector Levels", "extensions": ["pli"]}]
CLIP = [{"name": "Clip Studio Paint", "extensions": ["clip"]}]
PNG = [{"name": "PNG Image", "extensions": ["png"]}]

# menu item: (import kind, title, filters, board)
IMPORTS = {
    "file.import.raster": ("raster", "Import Raster", IMAGES, None),
    "file.import.opentoonz": ("opentoonz", "Import OpenToonz Lines", PLI, None),
    "file.import.csp": ("csp", "Import Clip Studio Paint", CLIP, None),
    "file.texture.import.raster": ("raster", "Import Raster", IMAGES, "child"),
    "file.texture.import.opentoonz": ("opentoonz", "Import OpenToonz Lines", PLI, "child"),
    "file.texture.import.csp": ("csp", "Import Clip Studio Paint", CLIP, "child"),
}
# The two vector formats can come in at a size of their own.
SCALED_IMPORTS = ("opentoonz", "csp")

# menu item: (file.texture kind, picker mode, picker title, filters, failure title)
TEXTURE_ACTIONS = {
    "file.texture.open": ("open", "open", "Open Texture View", TEXTURE, "Open Texture View"),
    "file.texture.save": ("saveAs", "save", "Save Texture View As", TEXTURE, "Save Texture View"),
    "file.texture.saveAs": ("saveAs", "save", "Save Texture View As", TEXTURE, "Save Texture View"),
    "file.texture.exportImage": ("exportImage", "save", "Export Texture View Image", PNG, "Export Texture View Image"),
}

REPLACE_TEXT = "{0} already exists.\nDo you want to replace it?"
FIT_TEXT = (
    "The imported drawing bounds are ({0}, {1}) {2} x {3}, but the current canvas is {4} x {5}.\n"
    "Fit it to the current canvas?"
)


def _window():
    try:
        return dialogs._ui().state("window") or {}
    except Exception:
        return {}


def _run(method, params, failure_title):
    """The backend call, or the failure shown under failure_title and None."""
    try:
        return dialogs.call(method, params) or {}
    except Exception as error:
        dialogs.message(failure_title, str(error))
        return None


def _number(value):
    """A number as the page wrote it: 12, not 12.0."""
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value)


def _file_name(path):
    text = str(path or "").strip()
    cut = max(text.rfind("/"), text.rfind("\\"))
    return text[cut + 1:] if cut >= 0 else text


def _with_extension(name, extension):
    if not name:
        return ""
    dot = name.rfind(".")
    if dot <= 0:
        return f"{name}.{extension}"
    if name[dot + 1:].lower() == "json":
        return f"{name[:dot]}.{extension}"
    return name


def default_save_name(kind, window):
    """The name a save picker opens on, from the file the board came from."""
    if kind == "project":
        return _with_extension(_file_name(window.get("currentFilePath")), "anproj")
    name = _file_name(window.get("childFilePath"))
    if kind == "exportImage":
        if not name:
            return "texture.png"
        dot = name.rfind(".")
        return f"{(name[:dot] if dot > 0 else name) or 'texture'}.png"
    return _with_extension(name, "textureview") if name else "texture.textureview"


def _confirm_replace(title, probe, save):
    """Save at once, or after the user agreed to replace the file there."""
    if not probe.get("needsConfirmOverwrite"):
        save()
        return
    text = REPLACE_TEXT.format(probe.get("fileName") or "")
    dialogs.message(title, text, ("Yes", "No"), lambda button: save() if button == "Yes" else None)


def canvas_size():
    window = _window()

    def chosen(size):
        if size:
            _run("project.new", size, "New Project")

    dialogs.canvas_size(window.get("canvasWidth") or 1280, window.get("canvasHeight") or 720, chosen)


def open_project():
    def picked(paths):
        if paths:
            _run("project.open", {"path": paths[0]}, "Open Project")

    dialogs.pick_file("open", "Open Project", PROJECT, picked)


def save_project():
    path = str(_window().get("currentFilePath") or "")
    if path.lower().endswith(".anproj"):
        _run("project.save", {}, "Save Project")
    else:
        save_project_as()


def save_project_as():
    def picked(paths):
        if not paths:
            return
        path = paths[0]
        probe = _run("project.saveAs", {"path": path, "probe": True}, "Save Project")
        if probe is None:
            return
        _confirm_replace(
            "Save Project As",
            probe,
            lambda: _run("project.saveAs", {"path": path, "overwrite": True}, "Save Project"),
        )

    default = default_save_name("project", _window())
    dialogs.pick_file("save", "Save Project As", PROJECT, picked, default_name=default)


def import_files(item):
    kind, title, filters, view = IMPORTS[item]

    def imported(params):
        _run("file.import", params, title)

    def picked(paths):
        if not paths:
            return
        params = {"kind": kind, "paths": paths}
        if view:
            params["view"] = view
        if kind not in SCALED_IMPORTS:
            imported(params)
            return
        probe = _run("file.import", {**params, "probe": True}, title)
        if not probe:
            return
        if not probe.get("needsScale"):
            imported({**params, "fitToCanvas": False})
            return
        bounds = probe.get("sourceBounds") or {}
        text = FIT_TEXT.format(
            *(_number(bounds.get(key, 0)) for key in ("x", "y", "width", "height")),
            _number(probe.get("canvasWidth", 0)),
            _number(probe.get("canvasHeight", 0)),
        )
        # Closing the question imports at the size it came in, as answering No does.
        dialogs.message(title, text, ("Yes", "No"),
                        lambda button: imported({**params, "fitToCanvas": button == "Yes"}))

    dialogs.pick_file("open", title, filters, picked)


def texture_file(item):
    if item == "file.texture.save":
        path = str(_window().get("childFilePath") or "")
        if path.lower().endswith(".textureview"):
            _run("file.texture", {"kind": "save"}, "Save Texture View")
            return
    kind, mode, title, filters, failure = TEXTURE_ACTIONS[item]

    def picked(paths):
        if not paths:
            return
        path = paths[0]
        if kind != "saveAs":
            _run("file.texture", {"kind": kind, "path": path}, failure)
            return
        probe = _run("file.texture", {"kind": kind, "path": path, "probe": True}, failure)
        if probe is None:
            return
        _confirm_replace(title, probe, lambda: _run("file.texture", {"kind": kind, "path": path, "overwrite": True}, failure))

    default = default_save_name(kind, _window()) if mode == "save" else ""
    dialogs.pick_file(mode, title, filters, picked, default_name=default)


_ACTIONS = {
    "file.new": canvas_size,
    "file.open": open_project,
    "file.save": save_project,
    "file.saveAs": save_project_as,
}


def run(item):
    """One File menu item. False when the item is not one of these flows."""
    if item in _ACTIONS:
        _ACTIONS[item]()
    elif item in IMPORTS:
        import_files(item)
    elif item in TEXTURE_ACTIONS:
        texture_file(item)
    else:
        return False
    return True


def _on_menu(message):
    if message.get("menu") == MENU:
        run(str(message.get("name") or ""))


def _ask_canvas_size_at_startup():
    # Every start opens on the canvas size (user-workflow.md, step 2). It waits
    # for the event loop, so it comes after the rest of startup.
    ui = dialogs._ui()
    if ui is not None and hasattr(ui, "call_later"):
        ui.call_later(0, canvas_size)


python_hooks.set_hook(_on_menu, menu=True)
_ask_canvas_size_at_startup()
