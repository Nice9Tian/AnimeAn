# `.tnz` / TOStream 格式与 `toonz_to_dict.py` 输出说明

本文说明 OpenToonz 场景文件 `.tnz` 的基本文本格式，以及
`tools/toonz_to_dict.py` 解析后生成的 Python `dict` / JSON 字段含义。

## 文件格式概览

OpenToonz 的 `.tnz` 场景文件由 `TOStream` 写出。它看起来像 XML，但并不是完整 XML 标准解析器在读写，而是 OpenToonz 自己的轻量级标签流格式。

常见场景文件结构大致如下：

```xml
<tnz version="1.7">
  <generator>
    OpenToonz ...
  </generator>
  <properties>
    ...
  </properties>
  <levelSet>
    ...
  </levelSet>
  <xsheet>
    ...
  </xsheet>
  <history>
    ...
  </history>
</tnz>
```

根节点通常是 `tnz`。`version` 属性来自保存场景时写入的 `version` 字段。根节点下常见子节点含义如下：

- `generator`：保存该文件的 OpenToonz 程序名称。
- `properties`：场景属性，例如相机、帧率、输出设置、清稿设置等。
- `levelSet`：场景中使用的 level 集合。level 通常对应绘画序列、音频、子场景等资源。
- `xsheet`：摄影表 / 时间轴主体，包含列、单元格、相机列、pegbar、fx 节点等。
- `history`：内容历史记录。并非所有文件都有。

具体子节点会随着 OpenToonz 版本、场景内容和功能模块不同而变化。

## 标签语法

TOStream 使用三类标签：

```xml
<tag>
  ...
</tag>

<tag attr="value">
  ...
</tag>

<tag attr="value"/>
```

说明：

- 起始标签：`<tag>` 或 `<tag attr="value">`
- 结束标签：`</tag>`
- 自闭合标签：`<tag attr="value"/>`
- 注释：`<!-- comment -->`
- 标签名和属性名可包含字母、数字、`_`、`.`、`-`
- 属性值必须用单引号或双引号包起来

属性值中的反斜杠会作为转义前缀。常见情况是 `\"`、`\'`、`\\`。

## 文本值与 token

标签内部可以直接写普通值，例如：

```xml
<frameRate>
  24
</frameRate>

<camera>
  1920 1080 72 72
</camera>

<path>
  "$scenefolder/drawings/A.pli"
</path>
```

TOStream 写入字符串时有一条规则：

- 如果字符串只包含字母、数字、`_`、`%`，通常不加引号。
- 如果字符串包含空格、路径分隔符、非 ASCII 字符或特殊字符，会用双引号包起来。
- 空字符串写作 `""`。
- 数字会直接以空格分隔。

因此，一个文本节点可能是单个值，也可能是一组值。比如：

```xml
<pixel>
  255 128 0 255
</pixel>
```

它在语义上可能表示一个颜色或四个通道值，但通用解析器无法知道业务类型，只会保留为 token 列表。

## 压缩文件

TOStream 支持过压缩写入。当前代码里普通场景保存使用未压缩文本，但解析器也识别几种文件头：

- 普通文本：直接以 `<` 开始。
- `TABc`：OpenToonz 的 LZ4 frame 压缩容器。
- `TNZC`：旧式 LZ4 压缩容器。
- `XZ` magic：如果文件是 `.xz` 风格数据，会尝试用 Python 标准库 `lzma` 解压。

如果遇到 `TABc` 或 `TNZC`，Python 需要安装可选依赖：

```powershell
pip install lz4
```

## Python 使用方式

导入使用：

```python
from tools.toonz_to_dict import parse_toonz_file

data = parse_toonz_file("path/to/scene.tnz")
```

命令行输出 JSON：

```powershell
python tools\toonz_to_dict.py path\to\scene.tnz
```

常用参数：

- `--no-coerce`：不把文本 token 自动转换成 `int`、`float`、`bool`，全部保留为字符串。
- `--compact`：输出单行紧凑 JSON。

## 输出 dict 结构

解析器会把每个标签变成一个字典字段。根标签名会成为最外层 key。

例如源文件：

```xml
<tnz version="1.7">
  <levelSet>
    <level id="1">
      42 "hello world"
    </level>
    <level id="2"/>
  </levelSet>
</tnz>
```

默认输出：

```json
{
  "tnz": {
    "@attributes": {
      "version": "1.7"
    },
    "levelSet": {
      "level": [
        {
          "@attributes": {
            "id": "1"
          },
          "#text": [
            42,
            "hello world"
          ]
        },
        {
          "@attributes": {
            "id": "2"
          }
        }
      ]
    }
  }
}
```

字段规则如下：

- 普通子标签：使用标签名作为 key。
- `@attributes`：保存该标签的属性字典。
- `#text`：保存该标签内部的直接文本内容，不包含子标签内容。
- 重复同名子标签：自动变成 list。
- 自闭合标签：如果没有属性和文本，输出为空 dict；如果有属性，只包含 `@attributes`。
- 多个根节点：正常 `.tnz` 不应出现；如果出现，输出为 `{"#roots": [...]}`。

## `#text` 的自动类型转换

默认情况下，解析器会把文本 token 自动转换为更像 Python 的类型：

```xml
<values>
  1 2.5 true false name
</values>
```

默认输出：

```json
{
  "values": {
    "#text": [1, 2.5, true, false, "name"]
  }
}
```

转换规则：

- `true` / `false` 转为布尔值。
- 没有小数点和科学计数法的数字转为 `int`。
- 有小数点或 `e` / `E` 的数字转为 `float`。
- 不能转换的 token 保留为字符串。
- 只有一个 token 时，`#text` 直接是单个值。
- 多个 token 时，`#text` 是列表。

如果使用 `--no-coerce` 或 `parse_toonz_file(..., coerce_scalars=False)`，`#text` 会保留为原始文本字符串：

```json
{
  "values": {
    "#text": "1 2.5 true false name"
  }
}
```

## `.pli` 线条信息输出

OpenToonz 的 `.tnz` 场景文件通常不直接保存画线几何，而是在 `levelSet` / `xsheet` 中引用外部 level 文件。矢量线条主要保存在 `.pli` 文件中。

现在 `toonz_to_dict.py` 可以直接读取 `.pli`：

```powershell
python tools\toonz_to_dict.py path\to\drawing.pli
```

导入使用：

```python
from tools.toonz_to_dict import read_vector_level, read_vector_level_strokes

level = read_vector_level("path/to/drawing.pli")
all_strokes = read_vector_level_strokes("path/to/drawing.pli")
frame_1_strokes = read_vector_level_strokes("path/to/drawing.pli", frame_id="1")
```

`read_vector_level()` 返回结构化的 `VectorLevel` 对象，而不是普通 dict。它包含：

- `level.frames`
- `frame.strokes`
- `stroke.style_id`
- `stroke.is_loop`
- `stroke.quadratics`
- `quadratic.p0` / `quadratic.p1` / `quadratic.p2`
- `point.x` / `point.y` / `point.thick`

如果仍然需要 JSON / dict，可以调用对象的 `to_dict()`：

```python
data = level.to_dict()
```

`.pli` 输出根节点是 `pli`，结构示例：

```json
{
  "pli": {
    "version": {
      "major": 150,
      "minor": 0
    },
    "creator": "OpenToonz",
    "frame_count": 1,
    "autoclose_tolerance": 0.0,
    "precision_scale": 16384,
    "frames": [
      {
        "frame": "1",
        "strokes": [
          {
            "style_id": 1,
            "is_loop": false,
            "max_thickness": 40,
            "quadratic_count": 1,
            "quadratics": [
              {
                "p0": {"x": 0.0, "y": 0.0, "thick": 3.13},
                "p1": {"x": 1.0, "y": 0.0, "thick": 3.13},
                "p2": {"x": 2.0, "y": 1.0, "thick": 3.13}
              }
            ],
            "outline_options": null,
            "source_offset": 31
          }
        ]
      }
    ]
  }
}
```

字段含义：

- `version`：`.pli` 文件格式版本。
- `creator`：写入该 `.pli` 的程序标识。
- `frame_count`：文件中保存的帧数量。
- `autoclose_tolerance`：OpenToonz 矢量自动闭合容差。
- `precision_scale`：坐标整数量化比例。`.pli` 内部用整数保存坐标，解析器会除以该比例还原为浮点坐标。
- `frames`：每一帧的矢量内容。
- `frame`：帧号，可能带字母后缀，例如 `1`、`1a`。
- `strokes`：该帧中的线条列表。
- `style_id`：线条使用的 palette style id。
- `is_loop`：该 stroke 是否是闭合线。
- `max_thickness`：该 stroke 在 `.pli` 中记录的最大粗细量化值。
- `quadratic_count`：该 stroke 包含多少段带粗细二次贝塞尔曲线。
- `quadratics`：线条的几何主体。每段都有 `p0`、`p1`、`p2` 三个控制点。
- `p0`、`p1`、`p2`：二次贝塞尔控制点；每个点包含 `x`、`y` 和 `thick`。
- `outline_options`：非默认线帽、连接和 miter 设置。如果文件没有为当前 stroke 写入特殊设置，则为 `null`。
- `source_offset`：该 stroke tag 在 `.pli` 二进制文件中的偏移量，主要用于调试和追踪。

注意：这里输出的是 OpenToonz 原始矢量 stroke 的中心线和粗细信息，不是渲染后的轮廓多边形。如果需要把二次贝塞尔曲线采样成折线，可以在 `quadratics` 上继续做几何采样。

## 重要限制

这个脚本是通用结构解析器，不是完整 OpenToonz 场景语义解释器。

它能可靠保留：

- 标签树结构
- 属性
- 文本 token
- 同名节点顺序
- 自闭合节点
- `.pli` 矢量文件中的 stroke 二次贝塞尔控制点与粗细

它不会自动判断：

- `.tnz` 中某个普通数字列表是否代表颜色、矩形、相机尺寸、关键帧或坐标。
- 某个 level 的资源文件是否真实存在。
- `$scenefolder`、`$projectroot` 等路径变量应该展开成哪个绝对路径。
- xsheet 单元格和 fx 网络的完整业务含义。
- `.pli` 中 region / fill intersection data 的完整封闭区域拓扑。

如果后续需要更高层的语义结构，可以在这个 dict 之上继续写二次转换，例如把 `levelSet` 解析成资源表，把 `xsheet` 解析成帧列矩阵。
