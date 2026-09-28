# AnimeAn Python 绑定中文说明

> 三级机制（Python 绑定，中文）。2026-09-28 从仓库根目录 python_bind_chinese_readme.md 移入；「双画板与工具」一节按现行界面改写（原 2026-07 的三段式布局、Changable Layer、顶层 Texture View File 菜单、mapped layer 复用等描述已过时，用户授权直接改）。界面与工作流以一级文档为准：`../user-workflow.md`、`../tools_user_workflow/`。

本文说明 AnimeAn 当前通过 `pybind11` 暴露给 Python 的能力，以及 ExtraTool 算法开发的适用边界。底层模块名是 `animean_python`，高层封装在 `pythonbind/animemodel.py`。

## 模块结构

- `animean_python`：C++ 扩展或嵌入式模块，来自 `pythonbind/python_bindings.cpp`。
- `pythonbind/animemodel.py`：Python 友好封装，提供 `AnimeModel`、`animemodel`、`annimemodel`、`ui`、`model_pybind`。
- `animean_python.model_pybind`：把 Python 的 tuple/list/dict 转成 Qt/C++ 值，再返回标准 dict/list。
- `animean_python.vectorlogic`：暴露矢量绘制、命中测试、擦除区间、路径分段和区域填充相关几何算法。
- `animean_python.get_scene()` / `animean_python.get_current()`：在 AnimeAn 内嵌 Python 环境中读取当前 UI scene 和当前帧/层/asset。

## 快速示例

```python
from animemodel import AnimeModel, ui

model = AnimeModel()
model.initialize(layer_count=2, frame_count=24)
ui.set_current(frame=0, layer=0)

model.add_polyline(
    [(0, 0), (100, 80), (160, 20)],
    color=(0, 0, 0, 255),
    width=3.0,
)

print(model.cell(to_poly=True))
```

也可以直接使用底层模型：

```python
import animean_python

scene = animean_python.SceneModel()
scene.initialize_scene(2, 24)
scene.add_polyline(0, 0, [(0, 0), (100, 80)], r=0, g=0, b=0, a=255, width=3.0)
```

## 索引约定

- 底层 `SceneModel` 使用 0 基索引：`row=0` 是第一帧，`layer_index=0` 是第一层。
- 高层封装中，参数名为 `id`、`index`、`frame_id` 时也使用 0 基索引。
- `get_stroke(num=1)` 和 `fillarea(1)` 使用用户可见序号；需要 0 基索引时使用 `index=0`。

## UI facade

内嵌运行时可以从 `animemodel` 导入 `ui`：

```python
from animemodel import ui

ui.refresh()
ui.main.refresh()
ui.children.refresh()
ui.frame.refresh()
ui.layer.refresh()
ui.asset.refresh()
ui.widget.refresh()

ui.set_current(frame=0, layer=0, asset=None)
```

`animean_python.ui` 还提供两个通用显示服务（脚本工具用，具体语义由脚本决定）：

```python
# 在指定画板上层显示一组 overlay（折线或闭合多边形），完全替换之前的 overlay
animean_python.ui.set_overlay("main", [
    {"id": "my_item", "points": [(0, 0), (100, 100)],
     "color": (255, 0, 0, 255), "width": 3.0, "removable": True},
    {"id": "my_zone", "points": [(0, 0), (80, 0), (80, 80)], "closed": True,
     "color": (0, 0, 255, 190), "fill_color": (0, 0, 255, 60)},
])

# 设置当前绘制颜色（不切换工具）
animean_python.ui.set_draw_color((40, 110, 255, 255))
```

overlay 始终绘制在所有图层之上，不属于场景模型，也不会被保存。`removable=True` 的 overlay 会显示 "x" 按钮，点击时 C++ 不做任何删除，只派发 `overlayremove` hook 事件（带 `overlay.id`），由脚本决定如何处理。`confirmable=True` 会在 x 左侧增加勾按钮，并派发 `overlayaction`（`overlay.action == "accept"`）；待确认的开放曲线会把这一对按钮放在包围盒右上角。

脚本修改模型后，如果需要界面立刻更新，调用对应的 `ui.*.refresh()`。长时间同步计算可以用：

```python
ui.freeze()
try:
    run_long_algorithm()
finally:
    ui.unfreeze()
```

这不会把 Python 变成异步执行，只是阻止 UI 在线程繁忙时继续交互。

## ExtraTool 开发边界

当前 ExtraTool 是“Python 辅助的 Pen 工作流”，不是完整自定义鼠标工具框架。用户点击 ExtraTool 后，绘图窗口仍使用原生 Pen，C++ 会给后续笔画设置 `property`，Python 通过 hook 接收事件。

适合现在开始开发的算法：

- 用户画完一笔后做后处理，例如中线提取、清理、自动补线、stroke 替换。
- 读取当前 cell/stroke，调用 `vectorlogic` 或纯 Python 算法，再写回新的 stroke。
- 根据当前帧/层/asset 做矢量区域、路径、命中测试相关分析。

暂时不适合的算法：

- 完全接管鼠标按下、拖动、释放的自定义交互。
- Python 端实时绘制 preview overlay。
- 大量 raster 图像写入或复杂 fill layer 编辑；这些入口目前没有完整 Python API。

ExtraTool 在 `pyfile/extra_tools.py` 中注册。每一项给出按钮名、工具激活期间 Pen 给笔画打上的 `property`、以及选中工具时调用的 `module.function` 处理器；`page` 指定按钮放在 Tools 的哪一页（`"painting"` 或 `"mapping"`，省略即 `"mapping"`）。下面的 `example_tool` 只是供照抄的占位示例，不是随软件发布的工具；实际清单是 `pyfile/auto_mapping.py` 里的 Auto Mapping 一族：

```python
def extra_tools():
    return [
        {
            "name": "example_tool",
            "title": "Example Tool",
            "property": "example_tool",
            "handler": "example_tool.activate_example_tool",
            "page": "mapping",
        },
    ]
```

典型 handler（下例把用户画完的每一笔重画成一条细红线）：

```python
import python_hooks
from animemodel import get_current, ui


def example_process(cell, stroke, message):
    if message.get("property") != "example_tool":
        return

    current = get_current()
    if current is None:
        return

    scene = current.raw_scene
    row = cell["row"]
    layer = cell["layer"]
    stroke_index = stroke.get("index")
    if stroke_index is None:
        return

    stroke_data = scene.cell_to_dict(layer, row, True, 2.0)["image"]["strokes"][stroke_index]
    points = stroke_data.get("polylines", [[]])[0]
    if len(points) >= 2:
        scene.add_polyline(row, layer, points, r=255, g=0, b=0, a=255, width=1.0)
        ui.widget.refresh()


def activate_example_tool(name="example_tool", property_value="example_tool"):
    python_hooks.set_hook(
        example_process,
        linefinish=True,
        tool="extra",
        property=property_value,
    )
    print(f"{name} tool activated with property={property_value}")
```

注意：选择 ExtraTool 时，C++ 会先发一次 `extra` 事件，再调用 handler 注册 hook。因此 handler 里注册的 hook 通常应该监听后续 `linefinish` 或 `update`，不要依赖捕获同一次点击产生的 `extra` 事件。

## Hook 消息

常见消息字段：

```python
{
    "event": "update" | "linefinish" | "erasefinish" | "deletefinish" | "fillfinish" | "movefinish" | "extra" | "option" | "overlayremove" | "overlayaction" | "framechange",
    "view": "main" | "child",
    "tool": "pen" | "move" | "eraser" | "delete_line" | "fill" | "extra",
    "base_tool": "pen" | "move" | "eraser" | "delete_line" | "fill",
    "property": "...",
    "cell": {"row": int, "layer": int, "asset": int, "frame_id": int},
    "stroke": {"id": int, "property": str, "width": float, "point_count": int, "total_length": float, "index": int},
    "position": {"x": float, "y": float},
    "delta": {"x": float, "y": float},
}
```

`view` 表示事件来自哪个画板：`main` 是主画板（main_paint_view），`child` 是子画板（child_paint_view）。

`overlayremove` 事件在用户点击 overlay 的 "x" 按钮时派发，额外携带 `"overlay": {"id": "..."}`；模型不会被自动修改，由 hook 自行决定删除什么。

`historyrestore` 事件在撤销/重做/历史跳转/历史重置（含打开工程）之后派发，脚本可借此从场景的 `script_data()` 重建自己的状态。

**否决历史提交**：`linefinish` / `erasefinish` / `deletefinish` / `fillfinish` / `movefinish` 的 hook 若发现这次操作实际没有改变任何东西（例如工具点击落空），可在消息里写 `message["cancel_history"] = True`，C++ 会跳过随后的历史记录，避免产生空记录并误清 redo。

`option` 事件还会携带：

```python
{
    "option": {
        "name": "...",
        "type": "button" | "list" | "slider",
        "value": object,
        "hook": "...",
        "row": int,
        "start_column": int,
        "end_column": int,
    }
}
```

## 双画板与工具（现行，2026-09-28 改写）

界面怎么用不在本文：主线见 `../user-workflow.md`，每个工具见 `../tools_user_workflow/`。这里只写脚本能依赖的事实。

- 两个场景：`animean_python.get_scene()` 返回两个信息字典，`sceneName` 分别是 `main_paint_view`（主画板）和 `child_paint_view`（纹理板）；字典里 `scene` 才是模型对象。内嵌全局变量（选择变化时同步）：`model`（活动板）、`main_model`、`child_model`、`active_view`（`"main"` / `"child"`）、`canvas_width/height`、`main_canvas_width/height`、`child_canvas_width/height`。hook 消息带 `"view"`。
- 主画板是中央区 Drawing 页，只有一个；纹理板是可搬迁的面板（中央 Texture 页、`texture_view` 子控件框、停车位三个家之一），无限画布。将来纹理板有分页栏、每页一个纹理（规划，见 `../product_features.md`）。
- 纹理板 Setting 菜单：`Changable Timeline`（默认关，关着时时间轴始终指主画板）、`Changable Texture`（默认开，关着时纹理板图案受保护，只有 `register_protected_properties("child", …)` 登记的属性能画）、Background。两个图层面板目标固定（Main / Child）。
- 视口：滚轮缩放、中键平移、滚动条；屏幕 ↔ 画布换算经 `pyfile/viewscale.py`（`algorithm/viewscale.h` 的镜像）。
- 撤销：快照制，两块板各一份，全局顺序；脚本改模型后调 `ui.history_commit(label, view)`；手势 hook 可置 `message["cancel_history"]`；撤销 / 重做 / 打开文件后派发 `historyrestore`，脚本据此重建缓存。机制见 `history_scriptdata.md`。
- 文件：File 菜单的 Save / Open 是完整 `.anproj`（两块板加 scriptData）；纹理板的 File 菜单读写 `.textureview`；导入分别进主画板或纹理板。见 `project_files.md`。
- Auto Mapping 一族（H/V Center Line、Mapping Area、Additional Line、Auto Mapping、视平线、To 3D）全在 `pyfile/auto_mapping.py`；资产不进图层，存 scriptData；输出进 Auto-Mapping 单元（带 `tag="automapping"` 的图层组），单元有焦点时就地重跑。规格是一级文档：`../auto_mapping/`。

分工原则不变：C++ 只提供通用机制（场景模型、几何绑定、hook 派发、覆盖层显示、把手事件、声明式界面），工具的属性名、颜色、数据结构、区域探测、裁剪逻辑全部在 Python，改工具不需要重新编译。

## AnimeModel 常用接口

```python
AnimeModel(scene=None)
AnimeModel.from_scene(scene)

model.initialize(layer_count=2, frame_count=2)
model.get_structure()
model.get_frame(id=None, index=None, name=None, Name=None)
model.get_layer(id=None, name=None, Name=None, asset_name=None, frame_id=None)
model.cell_image(frame=None, layer=None, create=True)
model.add_polyline(points, frame=None, layer=None, color=(0,0,0,255), width=3.0)
model.add_stroke(points, frame=None, layer=None, color=(0,0,0,255), width=3.0, smooth=True, smooth_value=50)
model.cell(frame=None, layer=None, to_poly=False)
model.strokes(frame=None, layer=None, to_poly=False)
model.clear_image(frame=None, layer=None)
model.remove_stroke(frame, layer, stroke)
model.remove_fill_area(frame, layer, fill_area)
model.clear_raster(frame, layer)
```

## SceneModel 常用接口

```python
scene.get_structure()
scene.set_current_layer(layer_index)
scene.set_current_frame(frame_index)
scene.current_layer()
scene.current_frame()
scene.add_layer()
scene.add_frame()
scene.delete_layer(layer_index)
scene.delete_frame(frame_index)
scene.cell_at(row, layer_index)
scene.image_at(row, layer_index, create=False)
scene.current_image(create=False)
scene.cell_to_dict(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.cell_strokes(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.stroke_line_list(row, layer_index, stroke_index, ploy=False, simplify=0.0)
scene.add_polyline(row, layer_index, points, r=0, g=0, b=0, a=255, width=3.0)
scene.add_stroke_object(row, layer_index, stroke)
scene.remove_stroke(row, layer_index, stroke_index)
scene.remove_fill_area(row, layer_index, fill_index)
scene.clear_raster(row, layer_index)
```

## model_pybind 数据格式

- 点：`(x, y)` 或 `{"x": x, "y": y}`。
- 矩形：`(x, y, width, height)`、`{"x","y","width","height"}` 或 `{"left","top","right","bottom"}`。
- 颜色：`(r, g, b)`、`(r, g, b, a)` 或 `{"r","g","b","a"}`，缺省 alpha 为 `255`。
- 线段：`(p0, p1)`、`(x1, y1, x2, y2)`、`{"from": p0, "to": p1}` 或 `{"p1": p0, "p2": p1}`。
- 区间：`(first, second)` 或 `{"first": first, "second": second}`。
- 路径：点列表或命令列表。

路径命令示例：

```python
[
    {"type": "move", "to": (0, 0)},
    {"type": "line", "to": (100, 0)},
    {"type": "quad", "control": (120, 40), "to": (100, 80)},
    {"type": "cubic", "control1": (60, 90), "control2": (20, 90), "to": (0, 80)},
    {"type": "rect", "rect": {"x": 0, "y": 0, "width": 100, "height": 80}},
    {"type": "close"},
]
```

## vectorlogic 常用接口

```python
epsilon()
filtered_points(points)
make_smoothed_path(points, smooth_value=50, to_poly=False, poly_step=4.0)
make_polyline_path(points, to_poly=False, poly_step=4.0)
make_stroke(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50, to_poly=False, poly_step=4.0)
make_stroke_object(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50)
stroke_hits_circle(stroke_or_points, center, radius, width=3.0)
stroke_hits_capsule(stroke_or_points, from_point, to_point, radius, width=3.0)
keep_ranges_for_circle(stroke, center, radius)
keep_ranges_for_capsule(stroke, from_point, to_point, radius)
complement_ranges(ranges)
sub_stroke(stroke, from_w, to_w, smooth_value=50, to_poly=False, poly_step=4.0)
point_at_length(stroke, length)
segments_from_path(path_like)
compute_vector_region_faces(segments, to_poly=False, poly_step=4.0)
vector_region_path_at(seed, segments, canvas_rect, to_poly=False, poly_step=4.0)
fill_path_from_mask(seed, boundary, to_poly=False, poly_step=4.0)
```

`to_poly=True` 会在返回值中增加采样后的 `polylines`，`poly_step` 控制曲线采样密度。

## 当前边界

- 文档只描述 Python 绑定功能，不包含构建命令。
- Python 侧目前主要适合矢量图像和场景结构编辑。
- 栅格图像写入、fill layer 的更多编辑入口存在于 C++ 模型中，但尚未完整绑定成 Python API。
- `VectorStroke` 的点列、颜色和路径主要通过 `make_stroke_object()` 或 `add_polyline()` 构造；不建议直接在 Python 修改复杂内部字段。
