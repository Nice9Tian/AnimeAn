# OpenToonz Output And Internal Structure

This document summarizes how OpenToonz stores a scene on disk and how that saved data maps to the internal scene structure.

## Big Picture

An OpenToonz scene is not usually saved as one self-contained drawing file.

The main scene file, usually `.tnz`, stores the project structure: scene settings, level references, xsheet columns, cells, object transforms, and effects. The actual drawing data is usually saved in separate level files, such as `.tlv`, `.pli`, image sequences, sound files, or palette files.

In practice, a saved scene is a small graph of files:

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

The exact folder names depend on the project settings and path aliases used by the scene.

## Main File Types

### `.tnz`

`.tnz` is the scene file. It is written with OpenToonz `TOStream`, an XML-like tag stream.

It stores:

- scene metadata
- scene properties
- level records and their paths
- xsheet / timeline structure
- columns and cells
- stage object information
- fx nodes and fx links
- optional history data

It normally does not contain the full pixel or vector drawing data for each level.

### `.pli`

`.pli` is a vector level file.

It can contain one or more frames of vector drawings. Each vector stroke is stored as a chain of thick quadratic Bezier curves. A stroke is not stored as a rendered bitmap; it is stored as editable vector geometry.

A `.pli` stroke is built from:

- style id
- loop / non-loop state
- outline options
- maximum thickness
- one or more thick quadratic segments
- each segment has `p0`, `p1`, `p2`
- each control point has `x`, `y`, and `thick`

### `.tlv`

`.tlv` is a Toonz Raster Level.

It stores raster frames using color-mapped pixels. The core pixel type is `TPixelCM32`, which represents:

- ink id
- paint id
- tone

This is not vector stroke geometry. The apparent line art is represented by raster ink/tone pixels.

### Raster Image Levels

OpenToonz can also reference ordinary raster files or image sequences, such as:

- `.png`
- `.tif`
- `.jpg`
- `.exr`

These are full-color raster levels, not Toonz raster levels.

### Palette Files

Palette data can be saved separately or embedded in level-specific data depending on level type and workflow.

Common palette-related files include:

- `.tpl`
- palette data inside `.tlv` / `.pli` level workflows

Palette styles are referenced by style ids. For vector strokes, the `style_id` in `.pli` points to a palette style.

### Sound Files

Sound levels reference external audio files, such as:

- `.wav`
- `.aiff`
- `.mp3`

The `.tnz` scene stores the level and xsheet references; the audio data itself is external.

## `.tnz` Internal Structure

A typical `.tnz` root looks like:

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

Stores the application name that wrote the scene.

### `properties`

Stores scene-level settings, such as camera, frame rate, output settings, cleanup settings, and other scene properties.

### `levelSet`

Stores all levels known to the scene.

Each level record stores metadata and a path. A simple level includes:

- level name
- level type
- level properties
- path to the actual drawing file
- optional scanned path

Important point: `levelSet` does not mean all drawings are merged into one file. It is a registry of levels used by the scene.

### `xsheet`

Stores the timeline / exposure sheet.

The xsheet contains columns. A level column stores cells. Each cell references:

- a level object
- a frame id inside that level
- row range / repetition information

This means a scene layer does not directly contain a copy of the drawing data. It points to a level and a frame.

### `history`

Stores optional content history data when available.

## Level, Column, And Layer Relationship

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

So, not all layers are stored in one `.pli`.

A `.pli` represents one vector level. Multiple columns can reference the same `.pli`, but separate vector levels normally have separate `.pli` files. Raster levels are usually `.tlv` or raster image sequences.

## `.pli` Internal Structure

`.pli` is a binary tag format.

Important tag concepts include:

- image/frame tags
- group tags
- color/style tags
- thick quadratic chain tags
- thick quadratic loop tags
- outline option tags
- precision scale tags
- region intersection data tags

The vector stroke geometry is stored in thick quadratic chain tags.

A parsed stroke has this conceptual structure:

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

Coordinates are stored internally as scaled integers. The `precision_scale` value is used to convert them back to floating-point coordinates.

## `.tlv` Internal Structure

`.tlv` stores Toonz raster frames.

Unlike `.pli`, it does not store editable Bezier strokes. It stores color-mapped raster pixels. Each meaningful pixel contains ids and tone data:

```text
pixel = ink id + paint id + tone
```

For line extraction from `.tlv`, the useful data is usually:

- image dimensions
- savebox
- frame id
- non-empty ink pixels
- ink id
- paint id
- tone value

If vector geometry is required, `.tlv` data would need to be traced or vectorized. That is a separate process from parsing.

## How The Python Parser Maps This

`tools/toonz_to_dict.py` currently supports:

- TOStream text files such as `.tnz`
- vector level files such as `.pli`

For `.tnz`, it outputs a generic tag tree:

- tag names become dictionary keys
- attributes are stored under `@attributes`
- direct text is stored under `#text`
- repeated tags become lists

For `.pli`, it outputs vector stroke data:

- `pli.version`
- `pli.creator`
- `pli.frame_count`
- `pli.autoclose_tolerance`
- `pli.precision_scale`
- `pli.frames[]`
- `pli.frames[].strokes[]`
- stroke style id, loop state, thickness, and quadratic control points

The parser does not yet fully decode `.tlv` pixels. If `.tlv` support is added, it should be treated as raster pixel extraction, not vector stroke extraction.

## Practical Interpretation

If OpenToonz outputs `B.tlv`, that layer is a Toonz Raster Level.

If you need editable vector line geometry, create or export a Vector Level. The expected level file is `.pli`.

If you need the whole scene structure, parse `.tnz`.

If you need drawing geometry for vector levels, parse each referenced `.pli`.

If you need raster ink/tone data, parse each referenced `.tlv`.

The `.tnz` file is the map. The level files are the actual drawing data.

