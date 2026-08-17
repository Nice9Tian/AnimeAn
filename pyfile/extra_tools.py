import importlib
import json

import auto_mapping
import fukusato_mapping


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
            # THE automapping (Coons interpolation, formerly "Auto Mapping 2").
            # The internal name/property keep the historical "_2" suffix so
            # old sessions keep their meaning; the retired spine-rotation
            # algorithm is archived in old_history/auto_mapping_1.py.
            "name": "auto_mapping_2",
            "title": "Auto Mapping",
            "property": auto_mapping.AUTO_MAPPING2_TOOL,
            "handler": "auto_mapping.run_auto_mapping",
        },
        {
            "name": "fukusato_line",
            "title": "Fukusato Line",
            "property": fukusato_mapping.HANDLE_PROPERTY,
            "handler": "fukusato_mapping.activate_fukusato_line",
        },
        {
            "name": "fukusato_cut",
            "title": "Fukusato Cut",
            "property": fukusato_mapping.CUT_PROPERTY,
            "handler": "fukusato_mapping.activate_fukusato_cut",
        },
        {
            "name": "fukusato_guide_mapping",
            "title": "Fukusato Mapping",
            "property": fukusato_mapping.FUKUSATO_TOOL,
            "handler": "fukusato_mapping.run_fukusato_mapping",
        },
    ]


def tools_json():
    # NOTE: no debug hooks here. hook_test's verbose printer used to be
    # registered unconditionally, which put a print + debug-pane append on
    # every pen move; enable it explicitly with hook_test.enable_verbose().
    auto_mapping.register_hooks()
    fukusato_mapping.register_hooks()
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
