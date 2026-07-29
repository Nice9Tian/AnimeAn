import importlib
import json

import auto_mapping
import hook_test


def extra_tools():
    return [
        {
            "name": "midline",
            "title": "Midline",
            "property": "midline",
            "handler": "midline_tool.activate_midline_tool",
        },
        {
            "name": "h_center_line",
            "title": "H Center Line",
            "property": auto_mapping.H_PROPERTY,
            "handler": "auto_mapping.activate_center_line_tool",
        },
        {
            "name": "v_center_line",
            "title": "V Center Line",
            "property": auto_mapping.V_PROPERTY,
            "handler": "auto_mapping.activate_center_line_tool",
        },
        {
            "name": "mapping_area",
            "title": "Mapping Area",
            "property": auto_mapping.MAPPING_AREA_PROPERTY,
            "handler": "auto_mapping.activate_mapping_area_tool",
        },
        {
            "name": "auto_mapping",
            "title": "Auto Mapping",
            "property": auto_mapping.AUTO_MAPPING_TOOL,
            "handler": "auto_mapping.run_auto_mapping",
        },
        {
            "name": "auto_mapping_2",
            "title": "Auto Mapping 2",
            "property": auto_mapping.AUTO_MAPPING2_TOOL,
            "handler": "auto_mapping.run_auto_mapping_2",
        },
    ]


def tools_json():
    hook_test.register_all_tool_hooks()
    auto_mapping.register_hooks()
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
