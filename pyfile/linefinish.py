import importlib


_handlers = []


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
    raise TypeError("linefinish.addhange expects a callable or 'module.function' name.")


def addhange(function):
    resolved = _resolve_function(function)
    if resolved not in _handlers:
        _handlers.append(resolved)
    return resolved


def delhange(function=None):
    if function is None:
        _handlers.clear()
        return
    function = _resolve_function(function)
    try:
        _handlers.remove(function)
    except ValueError:
        pass


def linefinish(cell, stroke):
    for function in tuple(_handlers):
        function(cell, stroke)


addchange = addhange
delchange = delhange
