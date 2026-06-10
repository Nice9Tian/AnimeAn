import importlib
import json

import hook_test


def extra_tools():
    return [
        {
            "name": "midline",
            "title": "Midline",
            "property": "midline",
            "handler": "midline_tool.activate_midline_tool",
        },
    ]


def tools_json():
    hook_test.register_all_tool_hooks()
    return json.dumps(extra_tools(), ensure_ascii=False)


def _resolve_function(function):
    module_name, separator, function_name = str(function).rpartition(".")
    if not separator:
        raise ValueError("extra tool handler must be written as 'module.function'.")
    module = importlib.import_module(module_name)
    resolved = getattr(module, function_name)
    if not callable(resolved):
        raise TypeError(f"extra tool handler is not callable: {function}")
    return resolved


def run_tool_handler(handler, name, property_value):
    return _resolve_function(handler)(name=name, property_value=property_value)
