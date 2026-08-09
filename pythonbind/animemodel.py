from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable, Sequence

import animean_python as _cpp


PointLike = Sequence[float] | dict[str, float]
ColorLike = Sequence[int] | dict[str, int]
RectLike = Sequence[float] | dict[str, float]
LocationPath = list[int | None]

_SCENES: dict[str, "AnimeModel"] = {}
_SCENES_BY_INT: dict[int, "AnimeModel"] = {}


class LazyList:
    def __init__(self, loader: Any) -> None:
        self._loader = loader

    def _items(self) -> list[Any]:
        return list(self._loader())

    def __getitem__(self, index: Any) -> Any:
        return self._items()[index]

    def __iter__(self):
        return iter(self._items())

    def __len__(self) -> int:
        return len(self._items())

    def __bool__(self) -> bool:
        return bool(self._items())

    def __repr__(self) -> str:
        return repr(self._items())

    def to_list(self) -> list[Any]:
        return self._items()


def _contains(value: Any, needle: str | None) -> bool:
    if needle is None:
        return True
    return needle.lower() in str(value).lower()


@dataclass(frozen=True)
class ModelPybind:
    """Small Python facade for C++ model_pybind conversion helpers."""

    def qreal(self, value: Any) -> float:
        return _cpp.model_pybind.qreal(value)

    def point(self, value: PointLike) -> dict[str, float]:
        return _cpp.model_pybind.point(value)

    def point_i(self, value: PointLike) -> dict[str, int]:
        return _cpp.model_pybind.point_i(value)

    def rect(self, value: RectLike) -> dict[str, float]:
        return _cpp.model_pybind.rect(value)

    def rect_i(self, value: RectLike) -> dict[str, int]:
        return _cpp.model_pybind.rect_i(value)

    def color(self, value: ColorLike) -> dict[str, int]:
        return _cpp.model_pybind.color(value)

    def line(self, value: Any) -> dict[str, Any]:
        return _cpp.model_pybind.line(value)

    def points(self, value: Iterable[PointLike]) -> list[dict[str, float]]:
        return _cpp.model_pybind.points(list(value))

    def lines(self, value: Iterable[Any]) -> list[dict[str, Any]]:
        return _cpp.model_pybind.lines(list(value))

    def range(self, value: Any) -> dict[str, float]:
        return _cpp.model_pybind.range(value)

    def ranges(self, value: Iterable[Any]) -> list[dict[str, float]]:
        return _cpp.model_pybind.ranges(list(value))

    def path(self, value: Any, *, to_poly: bool = False, poly_step: float = 4.0) -> dict[str, Any]:
        return _cpp.model_pybind.path(value, to_poly=to_poly, poly_step=poly_step)


class AnimeModel:
    """High-level Python helper around animean_python.SceneModel."""

    def __init__(self, scene: _cpp.SceneModel | None = None) -> None:
        self.scene = scene if scene is not None else _cpp.SceneModel()
        self.model_pybind = ModelPybind()
        self.vectorlogic = _cpp.vectorlogic
        register_scene(self)

    @classmethod
    def from_scene(cls, scene: _cpp.SceneModel) -> "AnimeModel":
        return cls(scene)

    def id(self) -> str:
        return self.scene.id()

    def scene_name(self) -> str:
        return self.scene.scene_name()

    def set_scene_name(self, name: str) -> "AnimeModel":
        old_id = self.id()
        self.scene.set_scene_name(name)
        if old_id != self.id():
            _SCENES.pop(old_id, None)
        register_scene(self)
        return self

    def scene_id(self) -> int:
        return self.scene.scene_id()

    def set_scene_id(self, scene_id: int) -> "AnimeModel":
        self.scene.set_scene_id(scene_id)
        register_scene(self)
        return self

    def initialize(self, layer_count: int = 2, frame_count: int = 2) -> "AnimeModel":
        old_id = self.id()
        self.scene.initialize_scene(layer_count, frame_count)
        if old_id != self.id():
            _SCENES.pop(old_id, None)
        register_scene(self)
        return self

    def get_structure(self) -> dict[str, Any]:
        return self.scene.get_structure()

    @property
    def frame(self) -> LazyList:
        return LazyList(lambda: self.get_frame())

    @property
    def layer(self) -> LazyList:
        return LazyList(lambda: self.get_layer())

    @property
    def asset(self) -> LazyList:
        return LazyList(lambda: self.get_asset())

    def get_frame(
        self,
        *,
        id: int | None = None,
        index: int | None = None,
        name: str | None = None,
        Name: str | None = None,
    ) -> "FrameHandle | list[FrameHandle]":
        structure = self.get_structure()
        if index is not None:
            if index < 0 or index >= structure["frame_count"]:
                raise IndexError(f"Frame index {index} was not found.")
            return FrameHandle(self, index)
        if id is not None:
            frame_index = id
            if frame_index < 0 or frame_index >= structure["frame_count"]:
                raise IndexError(f"Frame id {id} was not found.")
            return FrameHandle(self, frame_index)
        frame_name = name if name is not None else Name
        return [
            FrameHandle(self, frame["index"])
            for frame in structure["frames"]
            if _contains(frame.get("name", ""), frame_name)
        ]

    def get_layer(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        asset_name: str | None = None,
        frame_id: int | None = None,
    ) -> list["LayerMatch"]:
        layer_name = name if name is not None else Name
        return _filter_layers(self, id, layer_name, asset_name, frame_id)

    def get_asset(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        index: int | None = None,
    ) -> AssetRef | list[AssetRef]:
        return _scene_ref_from_model(self).get_asset(id=id, name=name, Name=Name, index=index)

    @property
    def current_frame(self) -> int:
        return self.scene.current_frame()

    @property
    def current_layer(self) -> int:
        return self.scene.current_layer()

    def cell_image(self, frame: int | None = None, layer: int | None = None, *, create: bool = True) -> _cpp.VectorImage:
        row = self.current_frame if frame is None else frame
        layer_index = self.current_layer if layer is None else layer
        return self.scene.image_at(row, layer_index, create)

    def image(self, frame: int | None = None, layer: int | None = None, *, create: bool = True) -> _cpp.VectorImage:
        return self.cell_image(frame, layer, create=create)

    def asset_image(self, asset_index: int, frame_id: int = 1, *, create: bool = False) -> _cpp.VectorImage:
        return self.scene.asset_image(asset_index, frame_id, create)

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        frame: int | None = None,
        layer: int | None = None,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "AnimeModel":
        rgba = self.model_pybind.color(color)
        row = self.current_frame if frame is None else frame
        layer_index = self.current_layer if layer is None else layer
        point_pairs = [(p["x"], p["y"]) for p in self.model_pybind.points(points)]
        self.scene.add_polyline(
            row,
            layer_index,
            point_pairs,
            rgba["r"],
            rgba["g"],
            rgba["b"],
            rgba["a"],
            width,
        )
        return self

    def add_stroke(
        self,
        points: Iterable[PointLike],
        *,
        frame: int | None = None,
        layer: int | None = None,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
        smooth: bool = True,
        smooth_value: int = 50,
    ) -> _cpp.VectorStroke:
        point_list = list(points)
        stroke = self.vectorlogic.make_stroke_object(
            point_list,
            color=color,
            width=width,
            smooth_path=smooth,
            smooth_value=smooth_value,
        )
        row = self.current_frame if frame is None else frame
        layer_index = self.current_layer if layer is None else layer
        self.scene.add_stroke_object(row, layer_index, stroke)
        return stroke

    def cell(self, frame: int | None = None, layer: int | None = None, *, to_poly: bool = False) -> dict[str, Any]:
        row = self.current_frame if frame is None else frame
        layer_index = self.current_layer if layer is None else layer
        return self.scene.cell_to_dict(layer_index, row, to_poly)

    def strokes(self, frame: int | None = None, layer: int | None = None, *, to_poly: bool = False) -> list[dict[str, Any]]:
        return self.cell(frame, layer, to_poly=to_poly)["image"]["strokes"]

    def clear_image(self, frame: int | None = None, layer: int | None = None) -> "AnimeModel":
        row = self.current_frame if frame is None else frame
        layer_index = self.current_layer if layer is None else layer
        self.scene.clear_image(row, layer_index)
        return self

    def remove_frame(self, frame: int) -> bool:
        return bool(self.scene.delete_frame(frame))

    def remove_layer(self, layer: int) -> bool:
        removed = bool(self.scene.delete_layer(layer))
        if removed and hasattr(self.scene, "remap_fill_source_layers_after_delete"):
            self.scene.remap_fill_source_layers_after_delete(layer)
        return removed

    def remove_stroke(self, frame: int, layer: int, stroke: int) -> bool:
        return bool(self.scene.remove_stroke(frame, layer, stroke))

    def remove_fill_area(self, frame: int, layer: int, fill_area: int) -> bool:
        return bool(self.scene.remove_fill_area(frame, layer, fill_area))

    def clear_raster(self, frame: int, layer: int) -> bool:
        return bool(self.scene.clear_raster(frame, layer))


class DataObject:
    """Small object wrapper for dictionaries returned by the C++ binding."""

    def __init__(self, data: dict[str, Any]) -> None:
        self._data = data

    def __getitem__(self, key: str) -> Any:
        return self._data[key]

    def __contains__(self, key: str) -> bool:
        return key in self._data

    def __getattr__(self, name: str) -> Any:
        try:
            return self._data[name]
        except KeyError as error:
            raise AttributeError(name) from error

    def __repr__(self) -> str:
        values = ", ".join(f"{key}={value!r}" for key, value in self._data.items() if key != "scene")
        return f"{type(self).__name__}({values})"

    def keys(self):
        return self._data.keys()

    def values(self):
        return self._data.values()

    def items(self):
        return self._data.items()

    def get(self, key: str, default: Any = None) -> Any:
        return self._data.get(key, default)

    def to_dict(self) -> dict[str, Any]:
        return dict(self._data)


class StrokeRef(DataObject):
    def __init__(
        self,
        data: dict[str, Any],
        model: AnimeModel | None = None,
        frame_index: int | None = None,
        layer_index: int | None = None,
        stroke_index: int | None = None,
    ) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index
        self.layer_index = layer_index
        self.stroke_index = stroke_index if stroke_index is not None else data.get("index")

    @property
    def index(self) -> int | None:
        return self._data.get("index")

    @property
    def num(self) -> int | None:
        return self._data.get("num")

    def line_list(self, ploy: bool = False, simplify: float = 0.0) -> list[Any]:
        if self.model is not None and self.frame_index is not None and self.layer_index is not None and self.stroke_index is not None:
            return self.model.scene.stroke_line_list(
                self.frame_index,
                self.layer_index,
                self.stroke_index,
                ploy,
                simplify,
            )
        if ploy:
            return self._data.get("commands", [])
        return self._data.get("polylines", [])

    def location(self) -> list[LocationPath]:
        if self.model is None:
            return []
        asset_index = _asset_index_at(self.model, self.frame_index, self.layer_index)
        if asset_index is None:
            return []
        return _locations_for_asset(self.model, asset_index)

    @property
    def layerid(self) -> list[int]:
        return _unique_values(location[2] for location in self.location())

    @property
    def layerName(self) -> list[str]:
        if self.model is None:
            return []
        return _layer_names_at_locations(self.model, self.location())

    @property
    def frame(self) -> list[int]:
        return _unique_values(location[1] for location in self.location())

    @property
    def assetid(self) -> list[int]:
        return _unique_values(location[3] for location in self.location() if location[3] is not None)

    @property
    def assetName(self) -> list[str]:
        if self.model is None:
            return []
        return _asset_names_at_locations(self.model, self.location())

    def remove(self) -> bool:
        if self.model is None or self.frame_index is None or self.layer_index is None or self.stroke_index is None:
            return False
        return self.model.remove_stroke(self.frame_index, self.layer_index, self.stroke_index)


class FillAreaRef(DataObject):
    def __init__(
        self,
        data: dict[str, Any],
        model: AnimeModel | None = None,
        frame_index: int | None = None,
        layer_index: int | None = None,
        fill_index: int | None = None,
    ) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index
        self.layer_index = layer_index
        self.fill_index = fill_index if fill_index is not None else data.get("index")

    @property
    def index(self) -> int | None:
        return self._data.get("index")

    def remove(self) -> bool:
        if self.model is None or self.frame_index is None or self.layer_index is None or self.fill_index is None:
            return False
        return self.model.remove_fill_area(self.frame_index, self.layer_index, self.fill_index)


class RasterRef(DataObject):
    def __init__(
        self,
        data: dict[str, Any],
        model: AnimeModel | None = None,
        frame_index: int | None = None,
        layer_index: int | None = None,
    ) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index
        self.layer_index = layer_index

    @property
    def empty(self) -> bool:
        return bool(self._data.get("empty", True))

    def remove(self) -> bool:
        if self.model is None or self.frame_index is None or self.layer_index is None:
            return False
        return self.model.clear_raster(self.frame_index, self.layer_index)


def _unique_values(values: Iterable[Any]) -> list[Any]:
    result: list[Any] = []
    seen: set[Any] = set()
    for value in values:
        if value not in seen:
            result.append(value)
            seen.add(value)
    return result


def _scene_location_index(model: AnimeModel) -> int:
    scene_ids: list[int] = []
    for info in _cpp.get_scene():
        scene_ids.append(dict(info).get("sceneId"))
    for scene_id in _SCENES_BY_INT:
        if scene_id not in scene_ids:
            scene_ids.append(scene_id)
    try:
        return scene_ids.index(model.scene_id())
    except ValueError:
        return 0


def _location_path(
    model: AnimeModel,
    frame_index: int | None,
    layer_index: int | None,
    asset_index: int | None,
) -> LocationPath:
    return [_scene_location_index(model), frame_index, layer_index, asset_index]


def _cell_locations_for_layer(model: AnimeModel, layer_index: int) -> list[LocationPath]:
    structure = model.get_structure()
    if layer_index < 0 or layer_index >= len(structure["layers"]):
        return []

    layer = structure["layers"][layer_index]
    locations: list[LocationPath] = []
    for cell in layer["cells"]:
        if cell.get("empty"):
            continue
        locations.append(_location_path(model, cell["frame_index"], layer["index"], cell["asset_index"]))
    return locations


def _locations_for_asset(model: AnimeModel, asset_index: int) -> list[LocationPath]:
    structure = model.get_structure()
    locations: list[LocationPath] = []
    for layer in structure["layers"]:
        for cell in layer["cells"]:
            if cell.get("asset_index") != asset_index:
                continue
            locations.append(_location_path(model, cell["frame_index"], layer["index"], asset_index))
    return locations


def _asset_index_at(model: AnimeModel, frame_index: int | None, layer_index: int | None) -> int | None:
    if frame_index is None or layer_index is None:
        return None
    cell = model.scene.cell_at(frame_index, layer_index)
    asset_index = cell.asset_index
    return asset_index if asset_index >= 0 else None


def _layer_names_at_locations(model: AnimeModel, locations: list[LocationPath]) -> list[str]:
    return _unique_values(model.scene.layer_name(location[2]) for location in locations if location[2] is not None)


def _asset_names_at_locations(model: AnimeModel, locations: list[LocationPath]) -> list[str]:
    return _unique_values(
        model.scene.asset_name(location[3])
        for location in locations
        if location[3] is not None
    )


class AssetRef(DataObject):
    def __init__(self, data: dict[str, Any], model: AnimeModel, asset_index: int) -> None:
        super().__init__(data)
        self.model = model
        self.asset_index = asset_index

    @property
    def index(self) -> int:
        return self.asset_index

    @property
    def id(self) -> int:
        return self.asset_index

    @property
    def name(self) -> str:
        return self.model.scene.asset_name(self.asset_index)

    @property
    def num(self) -> int | None:
        return self._data.get("num")

    def setname(self, name: str) -> "AssetRef":
        self.model.scene.set_asset_name(self.asset_index, name)
        self._data["name"] = self.model.scene.asset_name(self.asset_index)
        return self

    def remove(self) -> bool:
        raise NotImplementedError("Asset removal is not exposed by the current scene model.")

    def location(self) -> list[LocationPath]:
        return _locations_for_asset(self.model, self.asset_index)

    @property
    def layerid(self) -> list[int]:
        return _unique_values(location[2] for location in self.location())

    @property
    def layerName(self) -> list[str]:
        return _layer_names_at_locations(self.model, self.location())

    @property
    def frame(self) -> list[int]:
        return _unique_values(location[1] for location in self.location())

    @property
    def assetid(self) -> list[int]:
        return _unique_values(location[3] for location in self.location() if location[3] is not None)

    @property
    def assetName(self) -> list[str]:
        return _asset_names_at_locations(self.model, self.location())


class CellRef(DataObject):
    def __init__(self, data: dict[str, Any], model: AnimeModel, frame_index: int, layer_index: int) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index
        self.layer_index = layer_index

    @property
    def stroke(self) -> LazyList:
        return LazyList(lambda: self.get_stroke())

    def get_stroke(
        self,
        id: int | None = None,
        *,
        index: int | None = None,
        num: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        to_poly: bool = False,
    ) -> StrokeRef | list[StrokeRef]:
        strokes = self.model.strokes(self.frame_index, self.layer_index, to_poly=to_poly)
        wrapped = [
            StrokeRef({"index": i, "num": i + 1, **stroke}, self.model, self.frame_index, self.layer_index, i)
            for i, stroke in enumerate(strokes)
        ]
        if index is not None:
            return wrapped[index]
        if num is not None:
            return wrapped[num - 1]
        stroke_name = name if name is not None else Name
        matches = [
            stroke for stroke in wrapped
            if (id is None or stroke.get("id") == id)
            and _contains(stroke.get("name", ""), stroke_name)
        ]
        if id is not None:
            if not matches:
                raise IndexError(f"Stroke id {id} was not found.")
            return matches[0]
        if stroke_name is not None and len(matches) == 1:
            return matches[0]
        return matches

    def strokes(self, *, to_poly: bool = False) -> list[StrokeRef]:
        return self.get_stroke(to_poly=to_poly)  # type: ignore[return-value]

    def fillarea(self, num: int | None = None, *, index: int | None = None, to_poly: bool = False) -> FillAreaRef | list[FillAreaRef]:
        fills = self.model.cell(self.frame_index, self.layer_index, to_poly=to_poly)["image"]["fills"]
        wrapped = [
            FillAreaRef({"index": i, "num": i + 1, **fill}, self.model, self.frame_index, self.layer_index, i)
            for i, fill in enumerate(fills)
        ]
        if index is not None:
            return wrapped[index]
        if num is not None:
            return wrapped[num - 1]
        return wrapped

    def fill_area(self, num: int | None = None, *, index: int | None = None, to_poly: bool = False) -> FillAreaRef | list[FillAreaRef]:
        return self.fillarea(num, index=index, to_poly=to_poly)

    def raster(self) -> RasterRef:
        return RasterRef(self.model.cell(self.frame_index, self.layer_index)["image"]["raster"],
                         self.model,
                         self.frame_index,
                         self.layer_index)

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "CellRef":
        self.model.add_polyline(points, frame=self.frame_index, layer=self.layer_index, color=color, width=width)
        self._data = self.model.cell(self.frame_index, self.layer_index)
        return self

    def add_stroke(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
        smooth: bool = True,
        smooth_value: int = 50,
    ) -> _cpp.VectorStroke:
        return self.model.add_stroke(
            points,
            frame=self.frame_index,
            layer=self.layer_index,
            color=color,
            width=width,
            smooth=smooth,
            smooth_value=smooth_value,
        )

    def clear(self) -> "CellRef":
        self.model.clear_image(self.frame_index, self.layer_index)
        self._data = self.model.cell(self.frame_index, self.layer_index)
        return self

    def remove(self) -> "CellRef":
        return self.clear()


class LayerRef(DataObject):
    def __init__(self, data: dict[str, Any], model: AnimeModel, frame_index: int, layer_index: int) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index
        self.layer_index = layer_index

    def cell(self, *, to_poly: bool = False) -> CellRef:
        return CellRef(self.model.cell(self.frame_index, self.layer_index, to_poly=to_poly),
                       self.model,
                       self.frame_index,
                       self.layer_index)

    @property
    def id(self) -> int:
        return self.layer_index

    @property
    def index(self) -> int:
        return self.layer_index

    @property
    def name(self) -> str:
        return self.model.scene.layer_name(self.layer_index)

    def setname(self, name: str) -> "LayerRef":
        self.model.scene.set_layer_name(self.layer_index, name)
        self._data["name"] = self.model.scene.layer_name(self.layer_index)
        return self

    @property
    def visible(self) -> bool:
        return self.model.scene.layer_visible(self.layer_index)

    @visible.setter
    def visible(self, value: bool) -> None:
        self.model.scene.set_layer_visible(self.layer_index, bool(value))
        _cpp.ui.layer.refresh()
        _cpp.ui.widget.refresh()

    def show(self) -> "LayerRef":
        self.visible = True
        return self

    def hide(self) -> "LayerRef":
        self.visible = False
        return self

    def location(self) -> list[LocationPath]:
        locations = _cell_locations_for_layer(self.model, self.layer_index)
        if locations:
            return locations
        return [_location_path(self.model, self.frame_index, self.layer_index, None)]

    @property
    def layerid(self) -> list[int]:
        return _unique_values(location[2] for location in self.location())

    @property
    def layerName(self) -> list[str]:
        return _layer_names_at_locations(self.model, self.location())

    @property
    def frame(self) -> list[int]:
        return _unique_values(location[1] for location in self.location())

    @property
    def assetid(self) -> list[int]:
        return _unique_values(location[3] for location in self.location() if location[3] is not None)

    @property
    def assetName(self) -> list[str]:
        return _asset_names_at_locations(self.model, self.location())

    @property
    def stroke(self) -> LazyList:
        return LazyList(lambda: self.get_stroke())

    def get_stroke(
        self,
        id: int | None = None,
        *,
        index: int | None = None,
        num: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        to_poly: bool = False,
    ) -> StrokeRef | list[StrokeRef]:
        return self.cell(to_poly=to_poly).get_stroke(
            id,
            index=index,
            num=num,
            name=name,
            Name=Name,
            to_poly=to_poly,
        )

    def strokes(self, *, to_poly: bool = False) -> list[StrokeRef]:
        return self.get_stroke(to_poly=to_poly)  # type: ignore[return-value]

    def fillarea(self, num: int | None = None, *, index: int | None = None, to_poly: bool = False) -> FillAreaRef | list[FillAreaRef]:
        return self.cell(to_poly=to_poly).fillarea(num, index=index, to_poly=to_poly)

    def fill_area(self, num: int | None = None, *, index: int | None = None, to_poly: bool = False) -> FillAreaRef | list[FillAreaRef]:
        return self.fillarea(num, index=index, to_poly=to_poly)

    def raster(self) -> RasterRef:
        return self.cell().raster()

    def clear(self) -> "LayerRef":
        self.cell().clear()
        return self

    def remove(self) -> bool:
        return self.model.remove_layer(self.layer_index)

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "LayerRef":
        self.model.add_polyline(points, frame=self.frame_index, layer=self.layer_index, color=color, width=width)
        return self

    def add_stroke(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
        smooth: bool = True,
        smooth_value: int = 50,
    ) -> _cpp.VectorStroke:
        return self.model.add_stroke(
            points,
            frame=self.frame_index,
            layer=self.layer_index,
            color=color,
            width=width,
            smooth=smooth,
            smooth_value=smooth_value,
        )


class FrameRef(DataObject):
    def __init__(self, data: dict[str, Any], model: AnimeModel, frame_index: int) -> None:
        super().__init__(data)
        self.model = model
        self.frame_index = frame_index

    @property
    def id(self) -> int:
        return self.frame_index

    @property
    def index(self) -> int:
        return self.frame_index

    @property
    def name(self) -> str:
        return self.model.scene.frame_name(self.frame_index)

    @property
    def layer(self) -> LazyList:
        return LazyList(lambda: self.get_layer())

    def get_layer(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        index: int | None = None,
        asset_name: str | None = None,
    ) -> LayerRef | list[LayerRef]:
        if index is not None:
            return self._layer_at_index(index)
        layer_name = name if name is not None else Name
        matches = _filter_layers_at_index(self.model, id, layer_name, asset_name, self.frame_index)
        if id is not None:
            if not matches:
                raise IndexError(f"Layer id {id} was not found at frame {self.frame_index + 1}.")
            match = matches[0]
            return LayerRef(match.data or {}, self.model, self.frame_index, match.index)
        if layer_name is not None and len(matches) == 1:
            match = matches[0]
            return LayerRef(match.data or {}, self.model, self.frame_index, match.index)
        return [LayerRef(match.data or {}, self.model, self.frame_index, match.index) for match in matches]

    def _layer_at_index(self, index: int) -> LayerRef:
        structure = self.model.get_structure()
        for layer in structure["layers"]:
            if layer["index"] == index:
                return LayerRef(layer, self.model, self.frame_index, index)
        raise IndexError(f"Layer index {index} was not found.")

    def remove(self) -> bool:
        return self.model.remove_frame(self.frame_index)


class SceneRef(DataObject):
    def __init__(self, data: dict[str, Any]) -> None:
        super().__init__(data)
        raw_scene = data["scene"]
        self.raw_scene = raw_scene
        self.model = register_scene(raw_scene)

    @property
    def sceneName(self) -> str:
        return self.raw_scene.scene_name()

    @property
    def sceneId(self) -> int:
        return self.raw_scene.scene_id()

    @property
    def id(self) -> int:
        return self.sceneId

    @property
    def name(self) -> str:
        return self.sceneName

    def location(self) -> LocationPath:
        return _location_path(self.model, None, None, None)

    @property
    def current(self) -> "CurrentRef":
        return CurrentRef(self)

    @property
    def layerid(self) -> list[int]:
        structure = self.model.get_structure()
        return [layer["index"] for layer in structure["layers"]]

    @property
    def layerName(self) -> list[str]:
        structure = self.model.get_structure()
        return [layer["name"] for layer in structure["layers"]]

    @property
    def frameid(self) -> list[int]:
        structure = self.model.get_structure()
        return [frame["index"] for frame in structure["frames"]]

    @property
    def frame(self) -> LazyList:
        return LazyList(lambda: self.get_frame())

    @property
    def layer(self) -> LazyList:
        return LazyList(lambda: self.get_layer())

    @property
    def asset(self) -> LazyList:
        return LazyList(lambda: self.get_asset())

    def get_frame(
        self,
        *,
        id: int | None = None,
        index: int | None = None,
        name: str | None = None,
        Name: str | None = None,
    ) -> FrameRef | list[FrameRef]:
        structure = self.model.get_structure()
        if index is None and id is None:
            frame_name = name if name is not None else Name
            return [
                FrameRef(structure["frames"][frame_index], self.model, frame_index)
                for frame_index in range(structure["frame_count"])
                if _contains(structure["frames"][frame_index].get("name", ""), frame_name)
            ]
        frame_index = index if index is not None else id
        if frame_index < 0 or frame_index >= structure["frame_count"]:
            raise IndexError(f"Frame {'index ' + str(index) if index is not None else 'id ' + str(id)} was not found.")
        frame_data = structure["frames"][frame_index]
        return FrameRef(frame_data, self.model, frame_index)

    def get_asset(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        index: int | None = None,
    ) -> AssetRef | list[AssetRef]:
        structure = self.model.get_structure()
        assets = structure["assets"]
        if index is not None:
            if index < 0 or index >= len(assets):
                raise IndexError(f"Asset index {index} was not found.")
            return AssetRef(assets[index], self.model, index)

        asset_name = name if name is not None else Name
        matches: list[AssetRef] = []
        for asset in assets:
            asset_index = asset["index"]
            if id is not None and asset_index != id:
                continue
            if not _contains(asset.get("name", ""), asset_name):
                continue
            matches.append(AssetRef(asset, self.model, asset_index))

        if id is not None:
            if not matches:
                raise IndexError(f"Asset id {id} was not found.")
            return matches[0]
        if asset_name is not None and len(matches) == 1:
            return matches[0]
        return matches

    def get_layer(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        index: int | None = None,
        frame_index: int | None = None,
        frame_id: int | None = None,
    ) -> LayerRef | list[LayerRef]:
        selected_frame = self.model.current_frame if frame_id is None and frame_index is None else None
        frame_ref = self.get_frame(
            id=frame_id,
            index=selected_frame if selected_frame is not None else frame_index,
        )
        if isinstance(frame_ref, list):
            raise ValueError("layer() needs frame_id or frame_index when frame() would return multiple frames.")
        return frame_ref.get_layer(
            id=id,
            name=name,
            Name=Name,
            index=index,
        )

    def set_scene_name(self, name: str) -> "SceneRef":
        self.model.set_scene_name(name)
        self._data["sceneName"] = name
        return self

    def setname(self, name: str) -> "SceneRef":
        return self.set_scene_name(name)

    def set_scene_id(self, scene_id: int) -> "SceneRef":
        self.model.set_scene_id(scene_id)
        self._data["sceneId"] = scene_id
        return self

    def remove(self) -> bool:
        _SCENES.pop(self.model.id(), None)
        _SCENES.pop(self.model.scene_name(), None)
        _SCENES_BY_INT.pop(self.model.scene_id(), None)
        if hasattr(_cpp, "unregister_scene"):
            _cpp.unregister_scene(self.raw_scene)
        return True


class CurrentSceneRef(SceneRef):
    pass


class CurrentRef:
    def __init__(self, scene_ref: SceneRef | None = None) -> None:
        self._scene_ref = scene_ref

    def _current_ref(self) -> SceneRef | None:
        if self._scene_ref is not None:
            return self._scene_ref
        current_info = _cpp.get_current()
        if current_info is None:
            return None
        return CurrentSceneRef(dict(current_info))

    @property
    def scene(self) -> SceneRef | None:
        return self._current_ref()

    @property
    def raw_scene(self) -> Any | None:
        scene_ref = self._current_ref()
        return scene_ref.raw_scene if scene_ref is not None else None

    @property
    def model(self) -> AnimeModel | None:
        scene_ref = self._current_ref()
        return scene_ref.model if scene_ref is not None else None

    def _frame_id(self) -> int | None:
        scene_ref = self._current_ref()
        if scene_ref is None:
            return None
        if isinstance(scene_ref, CurrentSceneRef):
            return scene_ref._data.get("frame")
        frame_index = scene_ref.model.current_frame
        return frame_index if frame_index >= 0 else None

    def _layer_id(self) -> int | None:
        scene_ref = self._current_ref()
        if scene_ref is None:
            return None
        if isinstance(scene_ref, CurrentSceneRef):
            return scene_ref._data.get("layer")
        layer_index = scene_ref.model.current_layer
        return layer_index if layer_index >= 0 else None

    def _asset_id(self) -> int | None:
        scene_ref = self._current_ref()
        if scene_ref is None:
            return None
        if isinstance(scene_ref, CurrentSceneRef):
            asset_index = scene_ref._data.get("asset")
        else:
            asset_index = scene_ref.model.scene.current_asset()
            asset_index = asset_index if asset_index >= 0 else None
        if asset_index is not None:
            return asset_index
        return _asset_index_at(scene_ref.model, self._frame_id(), self._layer_id())

    @property
    def current_frame(self) -> int | None:
        return self._frame_id()

    @property
    def current_layer(self) -> int | None:
        return self._layer_id()

    @property
    def current_asset(self) -> int | None:
        return self._asset_id()

    @property
    def frame(self) -> FrameRef | None:
        scene_ref = self._current_ref()
        frame_index = self._frame_id()
        if scene_ref is None or frame_index is None:
            return None
        return scene_ref.get_frame(index=frame_index)

    @property
    def layer(self) -> LayerRef | None:
        scene_ref = self._current_ref()
        frame_index = self._frame_id()
        layer_index = self._layer_id()
        if scene_ref is None or frame_index is None or layer_index is None:
            return None
        return scene_ref.get_layer(index=layer_index, frame_index=frame_index)

    @property
    def asset(self) -> AssetRef | None:
        scene_ref = self._current_ref()
        asset_index = self._asset_id()
        if scene_ref is None or asset_index is None:
            return None
        return scene_ref.get_asset(id=asset_index)

    @property
    def location(self) -> LocationPath | None:
        model = self.model
        if model is None:
            return None
        return _location_path(model, self._frame_id(), self._layer_id(), self._asset_id())

    def __bool__(self) -> bool:
        return self._current_ref() is not None

    def __repr__(self) -> str:
        scene_ref = self._current_ref()
        if scene_ref is None:
            return "CurrentRef(None)"
        return (
            "CurrentRef("
            f"sceneName={scene_ref.sceneName!r}, "
            f"sceneId={scene_ref.sceneId!r}, "
            f"frame={self._frame_id()!r}, "
            f"layer={self._layer_id()!r}, "
            f"asset={self._asset_id()!r}, "
            f"location={self.location!r})"
        )


@dataclass(frozen=True)
class LayerMatch:
    model: AnimeModel
    index: int
    frame_index: int | None = None
    data: dict[str, Any] | None = None

    def cell(self, frame_id: int | None = None) -> "CellHandle":
        frame_index = self.frame_index if frame_id is None else frame_id
        if frame_index is None:
            frame_index = self.model.current_frame
        return CellHandle(self.model, frame_index, self.index)

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        frame_id: int | None = None,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "CellHandle":
        cell = self.cell(frame_id)
        cell.add_polyline(points, color=color, width=width)
        return cell

    def remove(self) -> bool:
        return self.model.remove_layer(self.index)


@dataclass(frozen=True)
class FrameHandle:
    model: AnimeModel
    frame_index: int

    @property
    def layer(self) -> LazyList:
        return LazyList(lambda: self.get_layer())

    def get_layer(
        self,
        *,
        id: int | None = None,
        name: str | None = None,
        Name: str | None = None,
        asset_name: str | None = None,
        frame_id: int | None = None,
    ) -> "CellHandle | list[LayerMatch]":
        selected_frame = self.frame_index if frame_id is None else frame_id
        layer_name = name if name is not None else Name
        matches = _filter_layers_at_index(self.model, id, layer_name, asset_name, selected_frame)
        if id is not None:
            if not matches:
                raise IndexError(f"Layer id {id} was not found at frame {selected_frame + 1}.")
            return matches[0].cell()
        return matches

    def remove(self) -> bool:
        return self.model.remove_frame(self.frame_index)


@dataclass(frozen=True)
class CellHandle:
    model: AnimeModel
    frame_index: int
    layer_index: int

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "CellHandle":
        self.model.add_polyline(points, frame=self.frame_index, layer=self.layer_index, color=color, width=width)
        return self

    def add_stroke(
        self,
        points: Iterable[PointLike],
        *,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
        smooth: bool = True,
        smooth_value: int = 50,
    ) -> _cpp.VectorStroke:
        return self.model.add_stroke(
            points,
            frame=self.frame_index,
            layer=self.layer_index,
            color=color,
            width=width,
            smooth=smooth,
            smooth_value=smooth_value,
        )

    def clear(self) -> "CellHandle":
        self.model.clear_image(self.frame_index, self.layer_index)
        return self

    def remove(self) -> "CellHandle":
        return self.clear()

    def to_dict(self, *, to_poly: bool = False) -> dict[str, Any]:
        return self.model.cell(self.frame_index, self.layer_index, to_poly=to_poly)

    @property
    def asset_index(self) -> int:
        return self.to_dict()["asset_index"]


def _filter_layers(
    model: AnimeModel,
    id: int | None = None,
    Name: str | None = None,
    asset_name: str | None = None,
    frame_id: int | None = None,
) -> list[LayerMatch]:
    structure = model.get_structure()
    frame_index = model.current_frame if frame_id is None else frame_id
    return _filter_layers_at_index(model, id, Name, asset_name, frame_index, structure)


def _filter_layers_at_index(
    model: AnimeModel,
    id: int | None,
    Name: str | None,
    asset_name: str | None,
    frame_index: int,
    structure: dict[str, Any] | None = None,
) -> list[LayerMatch]:
    if structure is None:
        structure = model.get_structure()
    matches: list[LayerMatch] = []

    for layer in structure["layers"]:
        if id is not None and layer["index"] != id:
            continue
        if not _contains(layer.get("name", ""), Name) and not _contains(layer.get("column_name", ""), Name):
            continue

        cell_data = None
        for cell in layer["cells"]:
            if cell["frame_index"] == frame_index:
                cell_data = cell
                break
        if cell_data is None:
            continue
        if not _contains(cell_data.get("asset_name", ""), asset_name):
            continue

        matches.append(LayerMatch(model, layer["index"], frame_index, layer))
    return matches


def register_scene(model: AnimeModel | _cpp.SceneModel) -> AnimeModel:
    wrapped = model if isinstance(model, AnimeModel) else AnimeModel.from_scene(model)
    _SCENES[wrapped.id()] = wrapped
    _SCENES[wrapped.scene_name()] = wrapped
    _SCENES_BY_INT[wrapped.scene_id()] = wrapped
    return wrapped


def create_scene(layer_count: int = 2, frame_count: int = 2) -> AnimeModel:
    model = AnimeModel()
    old_id = model.id()
    model.initialize(layer_count, frame_count)
    if old_id != model.id():
        _SCENES.pop(old_id, None)
    _SCENES[model.id()] = model
    return model


def _scene_info_from_model(model: AnimeModel) -> dict[str, Any]:
    return {
        "scene": model.scene,
        "sceneName": model.scene_name(),
        "sceneId": model.scene_id(),
    }


def _scene_ref_from_model(model: AnimeModel) -> SceneRef:
    return SceneRef(_scene_info_from_model(model))


def _scene_info_matches(data: dict[str, Any], name: str | None, scene_id: int | None) -> bool:
    if name is not None and not _contains(data.get("sceneName", ""), name):
        return False
    if scene_id is not None and data.get("sceneId") != scene_id:
        return False
    return True


def _all_scene_refs() -> list[SceneRef]:
    refs = [SceneRef(dict(info)) for info in _cpp.get_scene()]
    seen = {ref.sceneId for ref in refs}
    for model in _SCENES_BY_INT.values():
        if model.scene_id() not in seen:
            refs.append(_scene_ref_from_model(model))
            seen.add(model.scene_id())
    return refs


def get_scene(*, id: int | None = None, name: str | None = None, Name: str | None = None) -> SceneRef | list[SceneRef]:
    scene_name = name if name is not None else Name
    matches: list[SceneRef] = []
    for info in _cpp.get_scene():
        data = dict(info)
        if _scene_info_matches(data, scene_name, id):
            matches.append(SceneRef(data))

    for model in _SCENES_BY_INT.values():
        data = _scene_info_from_model(model)
        if _scene_info_matches(data, scene_name, id) and data["sceneId"] not in {match.sceneId for match in matches}:
            matches.append(SceneRef(data))

    if id is not None:
        if matches:
            return matches[0]
        raise KeyError(f"Scene was not found: id={id!r}, name={scene_name!r}")
    if scene_name is not None and len(matches) == 1:
        return matches[0]

    return matches


def scenes() -> dict[str, AnimeModel]:
    return dict(_SCENES)


def get_current() -> CurrentRef | None:
    return current if current else None


class _UiRefreshTarget:
    def __init__(self, refresh_function: Any) -> None:
        self._refresh_function = refresh_function

    def refresh(self) -> None:
        self._refresh_function()


class _UiOptionHookTarget:
    def __init__(self, tool_name: str | None, name: str) -> None:
        self._tool_name = tool_name
        self._name = name

    def hook(self, function: Any) -> Any:
        import importlib
        import python_hooks

        if callable(function):
            resolved = function
        else:
            module_name, separator, function_name = str(function).rpartition(".")
            if not separator:
                module_name = "__main__"
                function_name = str(function)
            resolved = getattr(importlib.import_module(module_name), function_name)
            if not callable(resolved):
                raise TypeError(f"ui option hook is not callable: {function}")

        def option_hook(message: dict[str, Any]) -> Any:
            if self._tool_name is not None and message.get("base_tool") != self._tool_name:
                return None
            option = message.get("option", {})
            if option.get("name") != self._name:
                return None
            return resolved(option)

        tool_prefix = self._tool_name or "any"
        option_hook.__name__ = f"ui_{tool_prefix}_{self._name}_hook"
        option_hook.__qualname__ = option_hook.__name__
        return python_hooks.set_hook(option_hook, option=True)


class _UiToolOptionHooks:
    def __init__(self, tool_name: str) -> None:
        self._tool_name = tool_name

    def __getattr__(self, name: str) -> _UiOptionHookTarget:
        if name.startswith("_"):
            raise AttributeError(name)
        return _UiOptionHookTarget(self._tool_name, name)

    def __getitem__(self, name: str) -> _UiOptionHookTarget:
        return _UiOptionHookTarget(self._tool_name, str(name))


class Ui:
    """Facade for operations that affect the live AnimeAn user interface."""

    def __init__(self) -> None:
        self.main = _UiRefreshTarget(_cpp.ui.main.refresh)
        self.children = _UiRefreshTarget(_cpp.ui.children.refresh)
        self.frame = _UiRefreshTarget(_cpp.ui.frame.refresh)
        self.layer = _UiRefreshTarget(_cpp.ui.layer.refresh)
        self.asset = _UiRefreshTarget(_cpp.ui.asset.refresh)
        self.widget = _UiRefreshTarget(_cpp.ui.widget.refresh)
        self.pen = _UiToolOptionHooks("pen")
        self.fill = _UiToolOptionHooks("fill")
        self.eraser = _UiToolOptionHooks("eraser")
        self.delete_line = _UiToolOptionHooks("delete_line")
        self.move = _UiToolOptionHooks("move")

    def refresh(self) -> None:
        _cpp.ui.refresh()

    def freeze(self) -> None:
        _cpp.ui.freeze()

    def unfreeze(self) -> None:
        _cpp.ui.unfreeze()

    def set_current(
        self,
        *,
        frame: int | None = None,
        layer: int | None = None,
        asset: int | None = None,
    ) -> None:
        _cpp.ui.set_current(frame=frame, layer=layer, asset=asset)

    @property
    def current(self) -> CurrentRef | None:
        return get_current()


scene = LazyList(lambda: get_scene())
current = CurrentRef()
ui = Ui()
annimemodel = AnimeModel
animemodel = AnimeModel
model_pybind = ModelPybind()
