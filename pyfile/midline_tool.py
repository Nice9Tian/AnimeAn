import python_hooks


def midline_process(cell, stroke, message=None):
    message = message or {}
    if message.get("property") != "midline":
        return
    print(
        "midline_process success "
        f"event={message.get('event')} row={cell.get('row')} layer={cell.get('layer')} "
        f"stroke={stroke.get('index')} property={message.get('property')}"
    )


def activate_midline_tool(name="midline", property_value="midline"):
    python_hooks.set_hook(
        midline_process,
        linefinish=True,
        tool="extra",
        property=property_value,
    )
    print(f"{name} tool activated with property={property_value}")
    return property_value
