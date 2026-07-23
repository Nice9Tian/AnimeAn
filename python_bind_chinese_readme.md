# AnimeAn Python 绑定中文说明

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

overlay 始终绘制在所有图层之上，不属于场景模型，也不会被保存。`removable=True` 的 overlay 会在其包围盒右上角显示 "x" 按钮，点击时 C++ 不做任何删除，只派发 `overlayremove` hook 事件（带 `overlay.id`），由脚本决定如何处理。

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

ExtraTool 在 `pyfile/extra_tools.py` 中注册：

```python
def extra_tools():
    return [
        {
            "name": "midline",
            "title": "Midline",
            "property": "midline",
            "handler": "midline_tool.activate_midline_tool",
        },
    ]
```

典型 handler：

```python
import python_hooks
from animemodel import get_current, ui


def midline_process(cell, stroke, message):
    if message.get("property") != "midline":
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


def activate_midline_tool(name="midline", property_value="midline"):
    python_hooks.set_hook(
        midline_process,
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
    "event": "update" | "linefinish" | "erasefinish" | "deletefinish" | "fillfinish" | "movefinish" | "extra" | "option" | "overlayremove",
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

## 双画板（child_paint_view）

主窗口现在有两个画板：

- `main_paint_view`：中央主画板，项目保存/打开针对它。
- `child_paint_view`：左侧可停靠/浮动的子画板窗口（View 菜单可开关），拥有独立的场景模型，可以在里面画画或导入内容（Import Raster / Import OpenToonz Lines 会导入到当前焦点画板）。

焦点进入某个画板时，Layers/Frames/Assets 面板会切换到该画板的数据。子画板顶部有两个开关：

- `Changable Timeline`（默认关）：关闭时，焦点进入子画板后 Frames 面板仍显示主画板的时间轴（子画板通常只需要单帧）。
- `Changable Layer`（默认开）：关闭时，Layers/Assets 面板同样保持主画板不变。

### 文件导入/导出的画板归属

内容导入不再跟随焦点，而是按菜单显式区分目标画板：

- **File 菜单** 的 `Import Raster` / `Import OpenToonz Lines` / `Import Clip Studio Paint` **始终导入到主画板**（main_paint_view），无论当前焦点在哪，触发时会自动激活主画板。（`.clip` 由内置的无依赖 Clip Studio 矢量读取器 `clipreader` 解析。）
- **Texture View File 菜单**（新增顶层菜单，texture view = 子画板）用于操作子画板的文件：
  - `Import Raster / OpenToonz Lines / Clip Studio Paint into Texture View...`：把这三种格式导入到**子画板**（会先显示并激活子画板窗口）。
  - `Open Texture View...` / `Save Texture View As...`：把子画板场景作为 `.animean` 工程文件独立读写（与主工程互不影响，便于复用同一套纹理图案）。
  - `Export Texture View Image...`：把子画板当前画面导出为 PNG（抓取画布帧缓冲；若此时 overlay 引导线可见会一并出现在图里）。

内嵌 Python 全局变量（每次选择变化时同步）：

```python
model               # 当前焦点画板的 SceneModel
main_model          # 主画板 SceneModel
child_model         # 子画板 SceneModel
active_view         # "main" 或 "child"
canvas_width/height             # 当前焦点画板尺寸
main_canvas_width/height        # 主画板尺寸
child_canvas_width/height       # 子画板尺寸
```

`animean_python.get_scene()` 会返回两个场景，`sceneName` 分别是 `main_paint_view` 和 `child_paint_view`。

## auto_mapping 工具（脚本实现：pyfile/auto_mapping.py）

工具栏新增四个 ExtraTool：`H Center Line`、`V Center Line`、`Mapping Area`、`Auto Mapping`。

使用流程：

1. 在 child_paint_view 用 `H Center Line` / `V Center Line` 各画一条中心线。这两条线定义图案的 UV 坐标系。
2. 用普通画笔在 child_paint_view 里画图案内容。
3. 在 main_paint_view 里同样画一条 H 中心线和一条 V 中心线，位置/长度/方向随意（可以是曲线）。
4. 可选：用 `Mapping Area` 圈定映射范围（见下）。
5. 点击 `Auto Mapping`：child 里的图案笔画按 UV 分解（相对两条中心线交点，u=1 表示 H 线长度的一半），再沿 main 的两条中心线按弧长重建。main 的中心线是曲线时，图案会跟着弯曲。

### Mapping Area（映射区域，油漆桶式取区）

`Mapping Area` 在封闭形状内点一下，Python 用 `vectorlogic.vector_region_path_at`（与油漆桶同一套区域算法，以所有可见图层的描边为边界）探测出这块区域，显示为半透明浅蓝色。映射时按区域裁剪/筛选：

- 区域画在 **main_paint_view**：映射输出被裁剪到区域内（笔画在边界处精确截断），相当于"把图案填进这个形状里"。
- 区域画在 **child_paint_view**：只有区域内的图案内容参与映射（先裁剪源，再做 UV 变换）。
- 两个画板可以同时各设一个区域；重新点击会替换旧区域。

### mapping_asset：不进图层的引导数据

H 中心线、V 中心线、Mapping Area 三者统称 **mapping asset**，由 `auto_mapping.py` 里的字典 `_MAPPING_ASSETS`（按画板分组）维护：

- 画完的瞬间（linefinish hook）几何数据被取出存入字典，同时**从场景模型中删除** — 它们不会出现在 Layers/Assets 面板里，不占用图层，也不算图案内容。
- 显示走通用 overlay 通道（`ui.set_overlay`）：**始终置于顶层**，颜色固定绑定 — 水平线蓝色、垂直线绿色、区域半透明浅蓝；选择工具时画笔颜色也会自动切到对应色。
- 每个元素包围盒右上角有 **"x" 按钮**，点击派发 `overlayremove` 事件，脚本从字典删除并刷新 overlay，然后直接重画即可。
- 水平线/垂直线/区域**每个画板各只有一个**（字典键唯一），重画/重点自然替换旧的。
- mapping asset 是**会话级**数据：跨帧共用（一次设置可用于任何帧的映射），但不写入 `.animean` 项目文件。旧版本存放在图层里的中心线/区域会在下次运行 `Auto Mapping` 时自动迁移到字典。

说明：

- 映射生成的笔画带有 `property="auto_mapped"`，再次点击 `Auto Mapping` 会先删除上一次的映射结果再重新生成（可反复调整 main 中心线/区域后重跑）。
- 线宽按两个方向缩放比例的几何平均缩放。
- 目前映射矢量笔画；填充区域和栅格内容不参与映射。
- 反馈信息输出在 Python Debug 面板。

### 分工原则

C++ 端只提供通用机制：场景模型、几何算法绑定（vectorlogic）、hook 事件派发、overlay 显示服务（`set_overlay` / `set_draw_color` / `overlayremove` 事件）、ExtraTool 的 `base_tool` 声明。具体工具的属性名、颜色、字典结构、区域探测、裁剪逻辑全部在 Python 端（`pyfile/auto_mapping.py`），修改工具行为不需要重新编译。

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
