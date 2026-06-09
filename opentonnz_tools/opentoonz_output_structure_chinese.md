# OpenToonz 输出结构与内部结构说明

本文总结 OpenToonz 保存场景时的磁盘文件结构，以及这些文件如何对应 OpenToonz 内部的场景结构。

## 总体结构

OpenToonz 的场景通常不是保存成一个包含所有绘画内容的单一文件。

主场景文件通常是 `.tnz`。它保存项目结构，例如场景设置、level 引用、xsheet 列、cell、舞台对象变换和特效。真正的绘画数据通常保存在独立的 level 文件中，例如 `.tlv`、`.pli`、图片序列、音频文件或调色板文件。

一个保存后的场景更像是一组互相引用的文件：

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

实际文件夹名称取决于项目设置和场景中使用的路径别名。

## 主要文件类型

### `.tnz`

`.tnz` 是场景文件。它由 OpenToonz 的 `TOStream` 写出，是一种类似 XML 的标签流格式。

它保存：

- 场景元信息
- 场景属性
- level 记录和路径
- xsheet / 时间轴结构
- 列和 cell
- 舞台对象信息
- fx 节点和连接
- 可选的历史记录数据

它通常不直接包含每个 level 的完整像素或矢量绘画数据。

### `.pli`

`.pli` 是矢量 level 文件。

它可以包含一个或多个帧的矢量绘画。每条矢量 stroke 保存为带粗细的二次贝塞尔曲线链。也就是说，`.pli` 保存的是可编辑的矢量几何，而不是渲染后的位图。

一条 `.pli` stroke 通常包含：

- style id
- 是否闭合
- outline options
- 最大粗细
- 一段或多段带粗细的二次贝塞尔曲线
- 每段曲线有 `p0`、`p1`、`p2`
- 每个控制点有 `x`、`y`、`thick`

### `.tlv`

`.tlv` 是 Toonz Raster Level。

它保存的是色彩图映射的栅格帧。核心像素类型是 `TPixelCM32`，大致表示：

- ink id
- paint id
- tone

这不是矢量 stroke 几何。你看到的线条是由 raster ink / tone 像素组成的。

### 普通栅格图片 Level

OpenToonz 也可以引用普通图片或图片序列，例如：

- `.png`
- `.tif`
- `.jpg`
- `.exr`

这些是全彩 raster level，不是 Toonz raster level。

### 调色板文件

调色板数据可能单独保存，也可能根据 level 类型和工作流保存在 level 相关数据中。

常见的调色板相关文件包括：

- `.tpl`
- `.tlv` / `.pli` 工作流中的 level palette 数据

调色板样式通过 style id 被引用。对于矢量 stroke，`.pli` 中的 `style_id` 指向一个 palette style。

### 声音文件

声音 level 会引用外部音频文件，例如：

- `.wav`
- `.aiff`
- `.mp3`

`.tnz` 场景文件保存 level 和 xsheet 引用，音频数据本身仍然是外部文件。

## `.tnz` 内部结构

典型 `.tnz` 根结构如下：

```xml
<tnz version="...">
  <generator>...</generator>
  <properties>...</properties>
  <levelSet>...</levelSet>
  <xsheet>...</xsheet>
  <history>...</history>
</tnz>
```

### `generator`

保存写出该场景的应用程序名称。

### `properties`

保存场景级设置，例如相机、帧率、输出设置、清稿设置和其他场景属性。

### `levelSet`

保存当前场景知道的所有 level。

每个 level 记录保存元数据和路径。一个 simple level 通常包含：

- level 名称
- level 类型
- level 属性
- 指向实际绘画文件的路径
- 可选的 scanned path

重点：`levelSet` 不表示所有绘画被合并进一个文件。它是场景所使用 level 的注册表。

### `xsheet`

保存时间轴 / 摄影表。

xsheet 中包含列。level column 中包含 cell。每个 cell 引用：

- 一个 level 对象
- 该 level 内的一个 frame id
- 行范围 / 重复信息

因此，场景图层本身并不直接持有绘画数据副本。它指向某个 level 和某个 frame。

### `history`

在可用时保存内容历史记录。

## Level、Column 和 Layer 的关系

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

所以，并不是所有图层都保存在一个 `.pli` 文件里。

一个 `.pli` 表示一个矢量 level。多个列可以引用同一个 `.pli`，但不同的矢量 level 通常有各自的 `.pli` 文件。栅格 level 通常是 `.tlv` 或普通图片序列。

## `.pli` 内部结构

`.pli` 是二进制 tag 格式。

重要 tag 概念包括：

- image / frame tag
- group tag
- color / style tag
- thick quadratic chain tag
- thick quadratic loop tag
- outline option tag
- precision scale tag
- region intersection data tag

矢量 stroke 几何保存在 thick quadratic chain tag 中。

解析后一条 stroke 的概念结构如下：

```json
{
  "style_id": 1,
  "is_loop": false,
  "max_thickness": 40,
  "quadratic_count": 2,
  "quadratics": [
    {
      "p0": {"x": 0.0, "y": 0.0, "thick": 2.0},
      "p1": {"x": 5.0, "y": 1.0, "thick": 2.0},
      "p2": {"x": 10.0, "y": 0.0, "thick": 2.0}
    }
  ]
}
```

坐标在文件内部以缩放后的整数保存。`precision_scale` 用来把它们还原为浮点坐标。

## `.tlv` 内部结构

`.tlv` 保存 Toonz raster 帧。

与 `.pli` 不同，它不保存可编辑贝塞尔 stroke，而是保存色彩图映射的 raster 像素。每个有意义的像素包含 id 和 tone 数据：

```text
pixel = ink id + paint id + tone
```

如果要从 `.tlv` 提取线条，通常有用的数据是：

- 图像尺寸
- savebox
- frame id
- 非空 ink 像素
- ink id
- paint id
- tone 值

如果需要矢量几何，则需要对 `.tlv` 做追踪或矢量化。这是解析之外的另一个步骤。

## Python 解析器如何映射这些结构

`tools/toonz_to_dict.py` 目前支持：

- `.tnz` 等 TOStream 文本文件
- `.pli` 矢量 level 文件

对于 `.tnz`，它输出通用标签树：

- 标签名变成字典 key
- 属性放在 `@attributes`
- 直接文本放在 `#text`
- 重复标签变成 list

对于 `.pli`，它输出矢量 stroke 数据：

- `pli.version`
- `pli.creator`
- `pli.frame_count`
- `pli.autoclose_tolerance`
- `pli.precision_scale`
- `pli.frames[]`
- `pli.frames[].strokes[]`
- stroke 的 style id、闭合状态、粗细和二次贝塞尔控制点

解析器目前还没有完整解码 `.tlv` 像素。如果后续添加 `.tlv` 支持，它应该被视为 raster 像素提取，而不是矢量 stroke 提取。

## 实际判断方式

如果 OpenToonz 输出的是 `B.tlv`，说明这个图层是 Toonz Raster Level。

如果你需要可编辑的矢量线条几何，需要创建或导出 Vector Level。预期的 level 文件是 `.pli`。

如果你需要整个场景结构，解析 `.tnz`。

如果你需要矢量 level 的绘画几何，解析 `.tnz` 中引用的每个 `.pli`。

如果你需要 raster ink / tone 数据，解析 `.tnz` 中引用的每个 `.tlv`。

`.tnz` 是地图，level 文件才是真正的绘画数据。

