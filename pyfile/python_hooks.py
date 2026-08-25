import importlib
import inspect
import json


_HOOKS = []

# view name -> [button definition dicts]. Tool modules register buttons at
# import time; C++ renders them on the view (currently the child window's
# option row) and dispatches presses as "viewbutton" hook events. The C++
# side stays generic - it only knows "a named toggle whose state goes to
# script", never what any button means.
_VIEW_BUTTONS = {}


def register_view_button(view, definition):
    """Add (or replace, by name) a script-defined button on a paint view.

    definition: {"name": str, "title": str, "tooltip": str,
                 "checkable": bool (default True)}
    """
    name = definition.get("name")
    if not name:
        raise ValueError("view button definition needs a 'name'.")
    buttons = _VIEW_BUTTONS.setdefault(view, [])
    buttons[:] = [entry for entry in buttons if entry.get("name") != name]
    buttons.append(dict(definition))


def view_buttons_json(view):
    """The registered buttons for a view, as JSON for the C++ builder."""
    return json.dumps(_VIEW_BUTTONS.get(view, []))


# Stroke properties that stay editable while a view's content is protected.
# C++ enforces "only these"; which ones they are is a tool fact, so it asks.
_PROTECTED_PROPERTIES = {}


def register_protected_properties(view, properties):
    """Name the tools that may still draw on `view` when it is protected."""
    _PROTECTED_PROPERTIES[view] = [str(p) for p in properties or []]


def protected_properties_json(view):
    return json.dumps(_PROTECTED_PROPERTIES.get(view, []))


# Stroke properties that read as GUIDE LINES rather than artwork. The onion
# skin drops them unless the timeline's Guide Line toggle asks for them; which
# properties those are is a tool fact, so the same ask-don't-assume split.
_ONION_GUIDE_PROPERTIES = []


def register_onion_guide_properties(properties):
    """Name the stroke properties the onion skin treats as guide lines."""
    for entry in properties or []:
        name = str(entry)
        if name and name not in _ONION_GUIDE_PROPERTIES:
            _ONION_GUIDE_PROPERTIES.append(name)


def onion_guide_properties_json():
    return json.dumps(_ONION_GUIDE_PROPERTIES)


# The layer-group TAG whose groups read as AUTO-MAPPING LAYERS. The onion skin
# can drop a whole tagged layer's content - strokes and fills alike - from the
# ghosts (the timeline's AM LAYER toggle). Which tag that is is a tool fact, so
# C++ asks instead of hardcoding a string it has no business knowing.
_ONION_LAYER_TAG = ""


def register_onion_layer_tag(tag):
    """Name the layer-group tag the onion skin's per-layer gate applies to."""
    global _ONION_LAYER_TAG
    _ONION_LAYER_TAG = str(tag or "")


def onion_layer_tag_json():
    return json.dumps(_ONION_LAYER_TAG)


# Context-menu providers. C++ asks at right-click time what the menu for the
# clicked row should contain, renders whatever it is handed, and reports the
# choice back as a "layermenu" hook event. It never learns what any entry
# means - the same split as view buttons and extra tools.
_MENU_PROVIDERS = []


# Menu-bar menus a tool module contributes. C++ builds whatever it is handed
# and reports the choice as a "menu" hook event; it never learns what an entry
# means, and it re-asks every time a menu opens so check marks are current.
_MENUS = []


def register_menu(definition):
    """Add (or replace) a menu-bar menu.

    definition: {"name": str, "title": str, "items": [item, ...],
                 "host": "main" (default) | "child"}
    item:       {"name": str, "title": str,
                 "kind": "action" | "check" | "radio" | "separator" | "submenu",
                 "checked": bool, "items": [...] (submenu only)}

    HOST is which menu bar the menu belongs to. It is part of the identity,
    not just a filter: the same name may legitimately appear on both bars -
    a "View" menu on the main window and a "View" menu on the texture window
    are different menus about different views - and deduping by name alone
    would let one silently replace the other.
    """
    name = definition.get("name")
    if not name:
        raise ValueError("menu definition needs a 'name'.")
    host = definition.get("host") or "main"
    entry = dict(definition)
    entry["host"] = host
    _MENUS[:] = [item for item in _MENUS
                 if not (item.get("name") == name and (item.get("host") or "main") == host)]
    _MENUS.append(entry)


def menus_json(host=None):
    """Registered menus, freshly evaluated, as JSON for the C++ builder.

    `host` filters to one menu bar ("main" / "child"); None returns all, which
    is what a caller that predates hosts gets.

    An "items" value may be a CALLABLE, so a menu that shows state (which
    curve mode is active, say) reports the truth at the moment it opens
    instead of whatever was true when it was registered.
    """
    def resolve(items):
        if callable(items):
            items = items() or []
        out = []
        for item in items or []:
            entry = dict(item)
            if "items" in entry:
                entry["items"] = resolve(entry["items"])
            out.append(entry)
        return out

    menus = []
    for menu in _MENUS:
        if host is not None and (menu.get("host") or "main") != host:
            continue
        entry = dict(menu)
        try:
            entry["items"] = resolve(entry.get("items"))
        except Exception as error:
            print(f"[python_hooks] menu '{entry.get('name')}' failed: {error}")
            continue
        menus.append(entry)
    return json.dumps(menus)


# Settings windows a tool module contributes, keyed by name. The layout is the
# same control schema the tool options panel uses, so one builder serves both.
_SETTINGS = {}


def register_settings(name, layout):
    """`layout` is a dict or a callable returning one (see menus_json)."""
    _SETTINGS[name] = layout


def settings_layout_json(name):
    layout = _SETTINGS.get(name)
    if callable(layout):
        try:
            layout = layout()
        except Exception as error:
            print(f"[python_hooks] settings '{name}' failed: {error}")
            layout = None
    return json.dumps(layout or {})


def register_menu_provider(function):
    """Add a callable(context) -> [{"name","title"[, "enabled"]}] provider."""
    resolved = _resolve_function(function)
    _MENU_PROVIDERS[:] = [p for p in _MENU_PROVIDERS if not _same_function(p, resolved)]
    _MENU_PROVIDERS.append(resolved)
    return resolved


def menu_items_json(context):
    """Every provider's entries for this click, as JSON for the C++ builder.

    A provider that raises is skipped rather than taking the menu down with
    it: a broken tool module must not make right-click stop working.
    """
    items = []
    for provider in tuple(_MENU_PROVIDERS):
        try:
            produced = provider(dict(context)) or []
        except Exception as error:
            print(f"[python_hooks] menu provider failed: {error}")
            continue
        for item in produced:
            kind = item.get("kind") or "action"
            if kind == "separator":
                items.append({"kind": "separator", "name": "-"})
                continue
            name = item.get("name")
            if not name:
                continue
            entry = {
                "name": name,
                "title": item.get("title") or name,
                "enabled": bool(item.get("enabled", True)),
                "kind": kind,
            }
            if kind == "settings":
                # Declarative, like the menu bar: C++ opens the registered
                # settings window itself after the menu closes. The provider
                # saw the full row context when it produced this entry, so
                # per-row state (which layer the window edits) is stashed by
                # the provider, not carried here.
                entry["settings"] = item.get("settings") or name
            items.append(entry)
    return json.dumps(items)


def _push_subscriptions():
    """Tell C++ which events have at least one hook.

    C++ skips the whole cross-language dispatch (GIL, message dict) for
    events outside this set, which keeps unsubscribed events - "update" in
    particular - off the drawing hot path.
    """
    events = set()
    for hook in _HOOKS:
        events |= hook["events"]
    try:
        import animean_python

        animean_python.ui.set_hook_events(sorted(events))
    except ImportError:
        pass  # embedded module not ready; C++ stays conservative (all pass)
    except Exception as error:
        # The C++ mask is sticky once valid - a silently dropped push would
        # silently drop events, so make the failure visible.
        print(f"[python_hooks] failed to push event subscriptions: {error}")


def _resolve_function(function):
    if callable(function):
        return function
    if isinstance(function, str):
        module_name, separator, function_name = function.rpartition(".")
        if not separator:
            function_name = function
            module_name = "__main__"
        module = importlib.import_module(module_name)
        resolved = getattr(module, function_name)
        if callable(resolved):
            return resolved
    raise TypeError("python_hooks.set_hook expects a callable or 'module.function' name.")


def _same_function(a, b):
    return getattr(a, "__module__", None) == getattr(b, "__module__", None) and getattr(
        a, "__qualname__", None
    ) == getattr(b, "__qualname__", None)


def set_hook(
    function,
    *,
    update=False,
    linefinish=False,
    erasefinish=False,
    deletefinish=False,
    fillfinish=False,
    movefinish=False,
    fillrequest=False,
    visibility=False,
    extra=False,
    option=False,
    overlayremove=False,
    overlayaction=False,
    historyrestore=False,
    framechange=False,
    pad=False,
    viewbutton=False,
    layermenu=False,
    layerchange=False,
    onion=False,
    handle=False,
    menu=False,
    tool=None,
    property=None,
):
    resolved = _resolve_function(function)
    flags = {
        "update": update,
        "linefinish": linefinish,
        "erasefinish": erasefinish,
        "deletefinish": deletefinish,
        "fillfinish": fillfinish,
        "movefinish": movefinish,
        "fillrequest": fillrequest,
        "visibility": visibility,
        "extra": extra,
        "option": option,
        "overlayremove": overlayremove,
        "overlayaction": overlayaction,
        "historyrestore": historyrestore,
        "framechange": framechange,
        "pad": pad,
        "viewbutton": viewbutton,
        "layermenu": layermenu,
        "layerchange": layerchange,
        # Onion-skin state changed on a board (ghost set, the content flags
        # "guides"/"lines"/"fills"/"am_layers", or the playhead while ghosts
        # are on).
        # Ghosts render the layer stack only, so a tool that draws through
        # ui.set_overlay has to ghost its own overlays; this is how it learns
        # which frames are showing.
        "onion": onion,
        "handle": handle,
        "menu": menu,
    }
    events = {name for name, enabled in flags.items() if enabled}
    if not events:
        return resolved

    del_hook(resolved)
    _HOOKS.append(
        {
            "function": resolved,
            "events": events,
            "tool": tool,
            "property": property,
        }
    )
    _push_subscriptions()
    return resolved


def del_hook(function=None):
    if function is None:
        _HOOKS.clear()
        _push_subscriptions()
        return
    resolved = _resolve_function(function)
    _HOOKS[:] = [hook for hook in _HOOKS if not _same_function(hook["function"], resolved)]
    _push_subscriptions()


def clear_tool_hooks():
    _HOOKS.clear()
    _push_subscriptions()


def _matches(hook, message):
    event = message.get("event")
    if event not in hook["events"]:
        return False
    tool = hook.get("tool")
    if tool and tool != message.get("tool"):
        return False
    property_value = hook.get("property")
    if property_value and property_value != message.get("property"):
        return False
    return True


def has_hooks(message):
    return any(_matches(hook, message) for hook in tuple(_HOOKS))


def _call(function, message):
    parameters = inspect.signature(function).parameters
    if len(parameters) <= 1:
        return function(message)
    return function(message.get("cell", {}), message.get("stroke", {}), message)


def dispatch(message):
    count = 0
    for hook in tuple(_HOOKS):
        if not _matches(hook, message):
            continue
        _call(hook["function"], message)
        count += 1
    return count


addhook = set_hook
delhook = del_hook
