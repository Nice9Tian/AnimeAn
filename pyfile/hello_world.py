def hello_world():
    return "hello world - python"


def draw_axis_test(model, canvas_width, canvas_height):
    row = model.current_frame()
    layer = model.current_layer()
    half_w = canvas_width / 2.0
    half_h = canvas_height / 2.0

    model.add_polyline(
        row,
        layer,
        [(0.0, half_h), (float(canvas_width), half_h)],
        220,
        0,
        180,
        255,
        2.0,
    )
    model.add_polyline(
        row,
        layer,
        [(half_w, 0.0), (half_w, float(canvas_height))],
        220,
        0,
        180,
        255,
        2.0,
    )
    return f"Python axis test OK: frame={row + 1}, layer={layer + 1}"
