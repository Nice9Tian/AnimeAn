import python_hooks


def print_hook_message(cell, stroke, message):
    position = message.get("position", {})
    delta = message.get("delta", {})
    print(
        "[hook-test] "
        f"event={message.get('event')} "
        f"tool={message.get('tool')} "
        f"base_tool={message.get('base_tool')} "
        f"property={message.get('property')} "
        f"row={cell.get('row')} "
        f"layer={cell.get('layer')} "
        f"stroke={stroke.get('index')} "
        f"points={stroke.get('point_count')} "
        f"pos=({position.get('x')}, {position.get('y')}) "
        f"delta=({delta.get('x')}, {delta.get('y')})"
    )


def register_all_tool_hooks():
    python_hooks.set_hook(
        print_hook_message,
        update=True,
        linefinish=True,
        erasefinish=True,
        deletefinish=True,
        fillfinish=True,
        movefinish=True,
        extra=True,
    )


def enable_verbose():
    """Debug aid: print every hook event (including per-move "update").

    Never registered automatically - the update stream runs on the drawing
    hot path. Call from the Python debug pane when needed, then
    disable_verbose() to stop.
    """
    register_all_tool_hooks()
    print("[hook-test] verbose hook logging ON (disable_verbose() to stop)")


def disable_verbose():
    python_hooks.del_hook(print_hook_message)
    print("[hook-test] verbose hook logging OFF")
