from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable, Sequence

import animean_python as _cpp


PointLike = Sequence[float] | dict[str, float]
ColorLike = Sequence[int] | dict[str, int]
RectLike = Sequence[float] | dict[str, float]

_SCENES: dict[str, "AnimeModel"] = {}


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

    def initialize(self, layer_count: int = 2, frame_count: int = 2) -> "AnimeModel":
        old_id = self.id()
        self.scene.initialize_scene(layer_count, frame_count)
        if old_id != self.id():
            _SCENES.pop(old_id, None)
        register_scene(self)
        return self

    def set_current(self, *, frame: int | None = None, layer: int | None = None) -> "AnimeModel":
        if frame is not None:
            self.scene.set_current_frame(frame)
        if layer is not None:
            self.scene.set_current_layer(layer)
        return self

    def get_structure(self) -> dict[str, Any]:
        return self.scene.get_structure()

    def frame(self, frame_num: int) -> "FrameHandle":
        return FrameHandle(self, frame_num - 1)

    def layer(
        self,
        num_name: int | str | None = None,
        layer_name: str | None = None,
        asset_name: str | None = None,
        frame_num: int | None = None,
    ) -> list["LayerMatch"]:
        return _filter_layers(self, num_name, layer_name, asset_name, frame_num)

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


@dataclass(frozen=True)
class LayerMatch:
    model: AnimeModel
    index: int
    frame_index: int | None = None
    data: dict[str, Any] | None = None

    def cell(self, frame_num: int | None = None) -> "CellHandle":
        frame_index = self.frame_index if frame_num is None else frame_num - 1
        if frame_index is None:
            frame_index = self.model.current_frame
        return CellHandle(self.model, frame_index, self.index)

    def add_polyline(
        self,
        points: Iterable[PointLike],
        *,
        frame: int | None = None,
        color: ColorLike = (0, 0, 0, 255),
        width: float = 3.0,
    ) -> "CellHandle":
        cell = self.cell(frame)
        cell.add_polyline(points, color=color, width=width)
        return cell


@dataclass(frozen=True)
class FrameHandle:
    model: AnimeModel
    frame_index: int

    def layer(
        self,
        num_name: int | str | None = None,
        layer_name: str | None = None,
        asset_name: str | None = None,
        frame_num: int | None = None,
    ) -> "CellHandle | list[LayerMatch]":
        selected_frame = self.frame_index if frame_num is None else frame_num - 1
        matches = _filter_layers_at_index(self.model, num_name, layer_name, asset_name, selected_frame)
        if isinstance(num_name, int):
            if not matches:
                raise IndexError(f"Layer {num_name} was not found at frame {selected_frame + 1}.")
            return matches[0].cell()
        return matches


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

    def to_dict(self, *, to_poly: bool = False) -> dict[str, Any]:
        return self.model.cell(self.frame_index, self.layer_index, to_poly=to_poly)

    @property
    def asset_index(self) -> int:
        return self.to_dict()["asset_index"]


def _filter_layers(
    model: AnimeModel,
    num_name: int | str | None = None,
    layer_name: str | None = None,
    asset_name: str | None = None,
    frame_num: int | None = None,
) -> list[LayerMatch]:
    structure = model.get_structure()
    frame_index = model.current_frame if frame_num is None else frame_num - 1
    return _filter_layers_at_index(model, num_name, layer_name, asset_name, frame_index, structure)


def _filter_layers_at_index(
    model: AnimeModel,
    num_name: int | str | None,
    layer_name: str | None,
    asset_name: str | None,
    frame_index: int,
    structure: dict[str, Any] | None = None,
) -> list[LayerMatch]:
    if structure is None:
        structure = model.get_structure()
    matches: list[LayerMatch] = []

    for layer in structure["layers"]:
        if isinstance(num_name, int) and layer["num"] != num_name:
            continue
        if isinstance(num_name, str) and not (
            _contains(layer.get("name", ""), num_name) or _contains(layer.get("column_name", ""), num_name)
        ):
            continue
        if not _contains(layer.get("name", ""), layer_name) and not _contains(layer.get("column_name", ""), layer_name):
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
    return wrapped


def create_scene(layer_count: int = 2, frame_count: int = 2) -> AnimeModel:
    model = AnimeModel()
    old_id = model.id()
    model.initialize(layer_count, frame_count)
    if old_id != model.id():
        _SCENES.pop(old_id, None)
    _SCENES[model.id()] = model
    return model


def scene(scene_id: str) -> AnimeModel:
    return _SCENES[scene_id]


def scenes() -> dict[str, AnimeModel]:
    return dict(_SCENES)


annimemodel = AnimeModel
animemodel = AnimeModel
model_pybind = ModelPybind()
