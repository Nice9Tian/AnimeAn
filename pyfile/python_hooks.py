import importlib
import inspect


_HOOKS = []
_EVENT_FLAGS = {
    "update": "update",
    "linefinish": "linefinish",
    "erasefinish": "erasefinish",
    "deletefinish": "deletefinish",
    "fillfinish": "fillfinish",
    "movefinish": "movefinish",
    "extra": "extra",
    "option": "option",
}


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
    extra=False,
    option=False,
    tool=None,
    property=None,
):
    resolved = _resolve_function(function)
    events = {
        name
        for name, enabled in {
            "update": update,
            "linefinish": linefinish,
            "erasefinish": erasefinish,
            "deletefinish": deletefinish,
            "fillfinish": fillfinish,
            "movefinish": movefinish,
            "extra": extra,
            "option": option,
        }.items()
        if enabled
    }
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
    return resolved


def del_hook(function=None):
    if function is None:
        _HOOKS.clear()
        return
    resolved = _resolve_function(function)
    _HOOKS[:] = [hook for hook in _HOOKS if not _same_function(hook["function"], resolved)]


def clear_tool_hooks():
    _HOOKS.clear()


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
