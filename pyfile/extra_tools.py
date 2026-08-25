import importlib
import json

import auto_mapping
import crease_line_tool
import fukusato_workflow


def extra_tools():
    # "page" names which Tools page the button belongs on. The shell owns the
    # pages ("painting" | "mapping" | "fukusato"); this list only says which
    # family each tool is part of, and an omitted page means "mapping".
    return [
        {
            "name": "midline",
            "title": "Midline",
            "property": "midline",
            "handler": "midline_tool.activate_midline_tool",
            "page": "mapping",
        },
        {
            "name": "h_center_line",
            "title": "H Center Line",
            "property": auto_mapping.H_PROPERTY,
            "handler": "auto_mapping.activate_center_line_tool",
            "page": "mapping",
        },
        {
            "name": "v_center_line",
            "title": "V Center Line",
            "property": auto_mapping.V_PROPERTY,
            "handler": "auto_mapping.activate_center_line_tool",
            "page": "mapping",
        },
        {
            "name": "mapping_area",
            "title": "Mapping Area",
            "property": auto_mapping.MAPPING_AREA_PROPERTY,
            "handler": "auto_mapping.activate_mapping_area_tool",
            "page": "mapping",
        },
        {
            "name": "additional_line",
            "title": "Additional Line",
            "property": auto_mapping.ADDITIONAL_PROPERTY,
            "handler": "auto_mapping.activate_additional_line_tool",
            "page": "mapping",
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
            # The canvas gestures of this tool are the ARROW's: it acts
            # through its own overlay, guides and handles, and leaving the
            # pen armed under it let a stray click draw into the artwork.
            # The Arrow's default mode then makes the H/V axes draggable.
            "base_tool": "arrow",
            "page": "mapping",
        },
        {
            "name": "fukusato_line",
            "title": "Fukusato Guide / 引导线",
            "property": fukusato_workflow.HANDLE_PROPERTY,
            "handler": "fukusato_workflow.activate_handle_tool",
            "page": "fukusato",
        },
        {
            "name": "fukusato_cut",
            "title": "Crease Line / 折角线",
            "property": crease_line_tool.PROPERTY,
            "handler": "crease_line_tool.activate_crease_line",
            "page": "fukusato",
        },
        {
            "name": "fukusato_guide_mapping",
            "title": "Fukusato Mapping",
            "property": fukusato_workflow.RUN_TOOL,
            "handler": "fukusato_workflow.run_mapping",
            "page": "fukusato",
        },
    ]


def tools_json():
    # NOTE: no debug hooks here. hook_test's verbose printer used to be
    # registered unconditionally, which put a print + debug-pane append on
    # every pen move; enable it explicitly with hook_test.enable_verbose().
    auto_mapping.register_hooks()
    crease_line_tool.register_hooks()
    fukusato_workflow.register_hooks()
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
