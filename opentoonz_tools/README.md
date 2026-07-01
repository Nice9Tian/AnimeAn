# OpenToonz Tools / OpenToonz 工具说明

This document consolidates the OpenToonz scene-structure notes and the
`toonz_to_dict.py` parser reference. English and Chinese sections are kept
together so the folder has one documentation entry point.

本文整合 `opentoonz_tools` 里原本分散的 OpenToonz 场景结构说明和
`toonz_to_dict.py` 解析器说明。英文和中文保留在同一份文档中，方便查找。

## English

### What These Tools Parse

OpenToonz scenes are usually saved as a graph of files rather than one
self-contained drawing file.

```text
scene.tnz
drawings/
  A.tlv
  B.pli
  C.0001.png
extras/
  audio.wav
palettes/
  level_palette.tpl
```

The `.tnz` scene file is the map. It stores scene settings, level references,
xsheet columns, cells, stage objects, and effects. Actual drawing data normally
lives in referenced level files such as `.pli`, `.tlv`, raster image sequences,
sound files, and palette files.

`toonz_to_dict.py` currently supports:

- TOStream text files such as `.tnz`.
- OpenToonz vector level files such as `.pli`.

It does not yet fully decode `.tlv` Toonz Raster pixel data.

### Main File Types

`.tnz`

The scene file. It uses OpenToonz `TOStream`, an XML-like tag stream.

Typical root:

```xml
<tnz version="1.7">
  <generator>...</generator>
  <properties>...</properties>
  <levelSet>...</levelSet>
  <xsheet>...</xsheet>
  <history>...</history>
</tnz>
```

Important sections:

- `generator`: application name/version that wrote the scene.
- `properties`: camera, frame rate, output, cleanup, and scene settings.
- `levelSet`: registry of levels used by the scene, including paths to drawing files.
- `xsheet`: timeline/exposure sheet columns and cells.
- `history`: optional history data.

`.pli`

Vector level file. It can contain one or more frames of editable vector
drawings. Strokes are stored as chains of thick quadratic Bezier segments.

A parsed stroke contains:

- `style_id`
- `is_loop`
- `max_thickness`
- `quadratics`
- `p0`, `p1`, `p2` control points
- point fields `x`, `y`, and `thick`

`.tlv`

Toonz Raster Level. It stores color-mapped raster pixels, not editable vector
strokes. The core pixel concept is:

```text
pixel = ink id + paint id + tone
```

If vector geometry is required from `.tlv`, the raster data would need a tracing
or vectorization step after parsing.

Raster image levels

OpenToonz can also reference ordinary raster files or image sequences:

- `.png`
- `.tif`
- `.jpg`
- `.exr`

Palette and sound files

Palette styles may live in `.tpl` files or level-specific palette data. Sound
levels reference external files such as `.wav`, `.aiff`, or `.mp3`.

### Level, Column, And Layer Relationship

The relationship is:

```text
Scene (.tnz)
  levelSet
    Level A -> drawings/A.tlv
    Level B -> drawings/B.pli
    Level C -> drawings/C..png sequence
  xsheet
    Column 1
      Row 1 -> Level A, Frame 1
      Row 2 -> Level A, Frame 2
    Column 2
      Row 1 -> Level B, Frame 1
      Row 2 -> Level B, Frame 1
```

So not all layers are stored in one `.pli`. A `.pli` represents one vector
level. Multiple columns can reference the same `.pli`, but separate vector
levels usually have separate `.pli` files.

### Python Usage

Import as a Python module:

```python
from opentoonz_tools.toonz_to_dict import (
    parse_open_toonz_file,
    parse_toonz_file,
    read_vector_level,
    read_vector_level_strokes,
)

scene_data = parse_open_toonz_file("path/to/scene.tnz")
level = read_vector_level("path/to/drawing.pli")
all_strokes = read_vector_level_strokes("path/to/drawing.pli")
frame_1_strokes = read_vector_level_strokes("path/to/drawing.pli", frame_id="1")
```

Command line:

```powershell
python opentoonz_tools\toonz_to_dict.py path\to\scene.tnz
python opentoonz_tools\toonz_to_dict.py path\to\drawing.pli
```

Options:

- `--no-coerce`: keep TOStream text tokens as strings instead of converting to
  `int`, `float`, or `bool`.
- `--compact`: print compact one-line JSON.

Some compressed TOStream variants use LZ4. If a file starts with `TABc` or
`TNZC`, install the optional dependency:

```powershell
pip install lz4
```

`.xz` data is handled through Python's standard `lzma` module.

### TOStream Text Mapping

For `.tnz` and other TOStream text files, tags become nested dictionaries.

Input:

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

Output:

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

Mapping rules:

- Tag names become dictionary keys.
- Attributes are stored under `@attributes`.
- Direct text is stored under `#text`.
- Repeated tags become lists.
- Self-closing tags become empty dicts, or dicts containing only `@attributes`.
- Multiple root tags are represented as `{"#roots": [...]}`.

By default, text tokens are coerced:

- `true` / `false` -> boolean
- integer-looking tokens -> `int`
- decimal or scientific notation tokens -> `float`
- other tokens -> string

With `--no-coerce` or `parse_toonz_file(..., coerce_scalars=False)`, direct text
is kept as its original string form.

### `.pli` Output Shape

`parse_open_toonz_file("drawing.pli")` and `parse_pli_file("drawing.pli")`
return a dict like:

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

Important fields:

- `version`: `.pli` format version.
- `creator`: application string stored in the file.
- `frame_count`: number of vector frames in the level.
- `autoclose_tolerance`: OpenToonz vector autoclose tolerance.
- `precision_scale`: scale used to convert stored integer coordinates back to floats.
- `frames`: frame list.
- `frame`: OpenToonz frame id, such as `1` or `1a`.
- `strokes`: vector strokes in the frame.
- `style_id`: palette style id used by the stroke.
- `is_loop`: whether the stroke is closed.
- `max_thickness`: maximum stored stroke thickness.
- `quadratics`: thick quadratic Bezier segments.
- `outline_options`: non-default outline/cap/miter settings when available.
- `source_offset`: byte offset of the stroke tag, useful for debugging.

### Practical Guidance

- Need the whole scene structure: parse `.tnz`.
- Need editable vector drawing geometry: parse referenced `.pli` files.
- Need raster ink/tone data: add or use `.tlv` parsing; this parser does not
  fully decode it yet.
- Need normal full-color raster pixels: read referenced image files with an image
  library.
- The `.tnz` file is the map; level files are the drawing data.

### Current Limits

The parser preserves generic structure well, but it does not infer all
OpenToonz semantic meaning. It does not currently guarantee:

- Full `.tlv` pixel decoding.
- Path alias expansion such as `$scenefolder` or `$projectroot`.
- Complete xsheet, fx graph, and object transform semantic reconstruction.
- Full `.pli` region/fill intersection semantics.

## 中文

### 这些工具解析什么

OpenToonz 场景通常不是一个包含全部绘图内容的单文件。主场景文件通常是
`.tnz`，它保存场景设置、level 引用、xsheet 列、cell、舞台对象和特效。真实的绘图数据通常保存在外部 level 文件中，例如 `.pli`、`.tlv`、图片序列、声音文件和调色板文件。

一个保存后的场景大致像这样：

```text
scene.tnz
drawings/
  A.tlv
  B.pli
  C.0001.png
extras/
  audio.wav
palettes/
  level_palette.tpl
```

`toonz_to_dict.py` 目前支持：

- `.tnz` 等 TOStream 文本文件。
- `.pli` OpenToonz 矢量 level 文件。

目前还没有完整解析 `.tlv` 的 Toonz Raster 像素数据。

### 主要文件类型

`.tnz`

场景文件，使用 OpenToonz 的 `TOStream` 保存，是一种类似 XML 的标签流。

典型根结构：

```xml
<tnz version="1.7">
  <generator>...</generator>
  <properties>...</properties>
  <levelSet>...</levelSet>
  <xsheet>...</xsheet>
  <history>...</history>
</tnz>
```

重要节点：

- `generator`：写出该场景的应用信息。
- `properties`：相机、帧率、输出、cleanup 和场景设置。
- `levelSet`：当前场景使用的 level 注册表，包含绘图文件路径。
- `xsheet`：时间轴 / 摄影表列和 cell。
- `history`：可选的历史数据。

`.pli`

矢量 level 文件，可以包含一帧或多帧可编辑矢量绘图。每条 stroke 由带粗细的二次 Bezier 曲线段组成。

解析出的 stroke 通常包含：

- `style_id`
- `is_loop`
- `max_thickness`
- `quadratics`
- `p0`、`p1`、`p2` 控制点
- 每个点的 `x`、`y`、`thick`

`.tlv`

Toonz Raster Level。它保存的是带 ink / paint / tone 的色表栅格像素，不是可编辑矢量 stroke。

核心像素概念：

```text
pixel = ink id + paint id + tone
```

如果要从 `.tlv` 得到矢量线条，需要在解析栅格后再做追踪或矢量化。

普通 raster image level

OpenToonz 也可以引用普通图片或图片序列：

- `.png`
- `.tif`
- `.jpg`
- `.exr`

调色板和声音文件

调色板样式可能保存在 `.tpl` 或 level 相关数据里。声音 level 引用外部音频，例如 `.wav`、`.aiff`、`.mp3`。

### Level、Column 和 Layer 的关系

关系可以理解为：

```text
Scene (.tnz)
  levelSet
    Level A -> drawings/A.tlv
    Level B -> drawings/B.pli
    Level C -> drawings/C..png sequence
  xsheet
    Column 1
      Row 1 -> Level A, Frame 1
      Row 2 -> Level A, Frame 2
    Column 2
      Row 1 -> Level B, Frame 1
      Row 2 -> Level B, Frame 1
```

所以，不是所有图层都保存在一个 `.pli` 里。一个 `.pli` 表示一个矢量 level。多个 column 可以引用同一个 `.pli`，但不同矢量 level 通常会有各自的 `.pli` 文件。

### Python 用法

作为模块导入：

```python
from opentoonz_tools.toonz_to_dict import (
    parse_open_toonz_file,
    parse_toonz_file,
    read_vector_level,
    read_vector_level_strokes,
)

scene_data = parse_open_toonz_file("path/to/scene.tnz")
level = read_vector_level("path/to/drawing.pli")
all_strokes = read_vector_level_strokes("path/to/drawing.pli")
frame_1_strokes = read_vector_level_strokes("path/to/drawing.pli", frame_id="1")
```

命令行：

```powershell
python opentoonz_tools\toonz_to_dict.py path\to\scene.tnz
python opentoonz_tools\toonz_to_dict.py path\to\drawing.pli
```

选项：

- `--no-coerce`：不要把 TOStream 文本 token 自动转成 `int`、`float` 或 `bool`，保留字符串。
- `--compact`：输出单行紧凑 JSON。

部分压缩 TOStream 变体使用 LZ4。如果文件头是 `TABc` 或 `TNZC`，需要安装可选依赖：

```powershell
pip install lz4
```

`.xz` 数据使用 Python 标准库 `lzma` 处理。

### TOStream 文本映射

`.tnz` 和其他 TOStream 文本文件会被解析为嵌套字典。

输入：

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

输出：

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

映射规则：

- tag 名变成字典 key。
- 属性放在 `@attributes`。
- 直接文本放在 `#text`。
- 重复 tag 变成 list。
- 自闭合 tag 变成空 dict，或者只包含 `@attributes` 的 dict。
- 多个根 tag 用 `{"#roots": [...]}` 表示。

默认会自动转换文本 token：

- `true` / `false` -> 布尔值
- 整数形式 -> `int`
- 小数或科学计数法 -> `float`
- 其他 token -> 字符串

使用 `--no-coerce` 或 `parse_toonz_file(..., coerce_scalars=False)` 时，直接文本会保留为原始字符串。

### `.pli` 输出结构

`parse_open_toonz_file("drawing.pli")` 和 `parse_pli_file("drawing.pli")`
返回类似这样的结构：

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

重要字段：

- `version`：`.pli` 格式版本。
- `creator`：文件中的应用字符串。
- `frame_count`：该 level 中的矢量帧数量。
- `autoclose_tolerance`：OpenToonz 矢量自动闭合容差。
- `precision_scale`：把文件内整数坐标还原为浮点坐标的比例。
- `frames`：帧列表。
- `frame`：OpenToonz 帧号，例如 `1` 或 `1a`。
- `strokes`：该帧中的矢量 stroke。
- `style_id`：stroke 使用的调色板 style id。
- `is_loop`：stroke 是否闭合。
- `max_thickness`：文件中记录的最大粗细。
- `quadratics`：带粗细的二次 Bezier 曲线段。
- `outline_options`：非默认线帽 / miter 等设置；没有时为 `null`。
- `source_offset`：stroke tag 在 `.pli` 文件中的字节偏移，主要用于调试。

### 实用判断

- 需要完整场景结构：解析 `.tnz`。
- 需要可编辑矢量线条几何：解析 `.tnz` 中引用的 `.pli`。
- 需要 raster ink/tone 数据：需要补充或使用 `.tlv` 解析；当前解析器尚未完整支持。
- 需要普通全彩图片像素：用图像库读取 `.png`、`.tif` 等引用文件。
- `.tnz` 是地图，level 文件才是真正的绘图数据。

### 当前限制

解析器能可靠保留通用结构，但不会自动理解所有 OpenToonz 语义。目前不保证：

- 完整 `.tlv` 像素解析。
- `$scenefolder`、`$projectroot` 等路径别名展开。
- xsheet、fx 图和 stage object transform 的完整语义重建。
- `.pli` region / fill intersection 数据的完整语义解析。
