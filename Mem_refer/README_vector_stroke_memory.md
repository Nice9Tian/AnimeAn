# Vector Stroke Memory Notes

This folder collects the main OpenToonz sources related to vector stroke data
and how strokes are represented, stored, edited, and converted in memory.

## Core memory model

- `sources/include/tstroke.h`
- `sources/common/tvectorimage/tstroke.cpp`

`TStroke` represents a single vector stroke. It uses a private `Imp` object and
stores the stroke as a sequence of thick quadratic chunks. A stroke with `n`
chunks has `2*n+1` control points. Each control point is a `TThickPoint`, meaning
position plus thickness.

Important concepts:

- control points: `getControlPoint()`, `setControlPoint()`, `reshape()`
- chunks: `getChunkCount()`, `getChunk()`
- parameter/length mapping: `getThickPoint()`, `getParameterAtLength()`
- cached geometry: bbox, length, outline, dirty/invalidate state
- style/property: `getStyle()`, `setStyle()`, `TStrokeProp`

## Vector image storage

- `sources/include/tvectorimage.h`
- `sources/common/tvectorimage/tvectorimage.cpp`
- `sources/common/tvectorimage/tvectorimageP.h`

`TVectorImage` represents a vector drawing. Its private implementation owns:

- `std::vector<VIStroke *> m_strokes`
- `std::vector<TRegion *> m_regions`
- grouping state
- region/fill validity flags
- stroke-region edge topology

`VIStroke` wraps a `TStroke*` with extra image-level metadata:

- group id
- edge list for regions/fills
- fill/new-stroke flags
- ownership/destruction of the underlying stroke

## Geometry and chunks

- `sources/include/tcurves.h`
- `sources/include/tgeometry.h`
- `sources/common/tvectorimage/tstrokeoutline.cpp`
- `sources/include/tstrokeoutline.h`

These define and operate on geometric primitives such as `TThickPoint`,
`TThickQuadratic`, `TThickCubic`, outlines, and bounds.

## Regions and fills

- `sources/include/tregion.h`
- `sources/common/tvectorimage/tregion.cpp`
- `sources/common/tvectorimage/tregionutil.cpp`
- `sources/common/tvectorimage/tcomputeregions.cpp`
- `sources/common/tvectorimage/tsweepboundary.cpp`

These files connect strokes into fillable regions. Stroke edits usually require
region invalidation or recomputation through `TVectorImage::notifyChangedStrokes`
or related methods.

## Stroke creation from input points

- `sources/include/toonz/strokegenerator.h`
- `sources/toonzlib/strokegenerator.cpp`
- `sources/tnztools/toonzvectorbrushtool.cpp`

`StrokeGenerator` collects sampled `TThickPoint` values and creates final
`TStroke` objects through `TStroke::interpolate()`.

## Editing existing strokes

- `sources/tnztools/controlpointselection.cpp`
- `sources/tnztools/controlpointeditortool.cpp`
- `sources/tnztools/vectorselectiontool.cpp`
- `sources/tnztools/vectorerasertool.cpp`
- `sources/tnztools/strokeselection.cpp`

These are useful for understanding how existing in-memory strokes are selected,
moved, reshaped, split, erased, and restored for undo.
