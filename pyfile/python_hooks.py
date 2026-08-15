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


# Context-menu providers. C++ asks at right-click time what the menu for the
# clicked row should contain, renders whatever it is handed, and reports the
# choice back as a "layermenu" hook event. It never learns what any entry
# means - the same split as view buttons and extra tools.
_MENU_PROVIDERS = []


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
            name = item.get("name")
            if not name:
                continue
            items.append({
                "name": name,
                "title": item.get("title") or name,
                "enabled": bool(item.get("enabled", True)),
            })
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
    historyrestore=False,
    pad=False,
    viewbutton=False,
    layermenu=False,
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
        "historyrestore": historyrestore,
        "pad": pad,
        "viewbutton": viewbutton,
        "layermenu": layermenu,
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
