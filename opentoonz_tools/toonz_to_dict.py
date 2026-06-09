#!/usr/bin/env python3
"""Parse OpenToonz/Toonz TOStream files into Python dictionaries.

The scene file format used by OpenToonz is an XML-like text stream written by
TOStream. This parser keeps the original tag structure, attributes, repeated
children, and text values while returning plain Python data.
"""

from __future__ import annotations

import argparse
import json
import lzma
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


try:
    import lz4.frame as lz4_frame
except ImportError:  # pragma: no cover - optional dependency
    lz4_frame = None


class ToonzParseError(ValueError):
    """Raised when a TOStream document cannot be parsed."""


class PliParseError(ValueError):
    """Raised when a Paperless Image (.pli) file cannot be parsed."""


@dataclass
class Tag:
    name: str
    attributes: dict[str, str] = field(default_factory=dict)
    self_closing: bool = False
    closing: bool = False


@dataclass
class Node:
    name: str
    attributes: dict[str, str] = field(default_factory=dict)
    children: list["Node"] = field(default_factory=list)
    text: str = ""

    def to_dict(self, *, coerce_scalars: bool = True) -> dict[str, Any]:
        result: dict[str, Any] = {}
        if self.attributes:
            result["@attributes"] = dict(self.attributes)

        text = self.text.strip()
        if text:
            result["#text"] = _coerce_tokens(text) if coerce_scalars else text

        for child in self.children:
            child_value = child.to_dict(coerce_scalars=coerce_scalars)
            existing = result.get(child.name)
            if existing is None:
                result[child.name] = child_value
            elif isinstance(existing, list):
                existing.append(child_value)
            else:
                result[child.name] = [existing, child_value]

        return result


class ToonzParser:
    def __init__(self, text: str):
        self.text = text
        self.pos = 0
        self.length = len(text)

    def parse(self, *, coerce_scalars: bool = True) -> dict[str, Any]:
        roots: list[Node] = []
        stack: list[Node] = []

        while self._skip_ws():
            if self._peek() != "<":
                chunk = self._read_until("<")
                if stack:
                    stack[-1].text += chunk
                elif chunk.strip():
                    raise self._error("unexpected text outside root tag")
                continue

            tag = self._read_tag()
            if not tag.name:
                continue
            if tag.closing:
                if not stack:
                    raise self._error(f"unexpected closing tag </{tag.name}>")
                node = stack.pop()
                if node.name != tag.name:
                    raise self._error(
                        f"tag mismatch: expected </{node.name}>, got </{tag.name}>"
                    )
                if stack:
                    stack[-1].children.append(node)
                else:
                    roots.append(node)
                continue

            node = Node(tag.name, tag.attributes)
            if tag.self_closing:
                if stack:
                    stack[-1].children.append(node)
                else:
                    roots.append(node)
            else:
                stack.append(node)

        if stack:
            raise self._error(f"unclosed tag <{stack[-1].name}>")
        if not roots:
            raise ToonzParseError("empty TOStream document")
        if len(roots) == 1:
            return {roots[0].name: roots[0].to_dict(coerce_scalars=coerce_scalars)}
        return {
            "#roots": [
                {node.name: node.to_dict(coerce_scalars=coerce_scalars)}
                for node in roots
            ]
        }

    def _read_tag(self) -> Tag:
        self._expect("<")
        if self._consume("!--"):
            end = self.text.find("-->", self.pos)
            if end < 0:
                raise self._error("unterminated comment")
            self.pos = end + 3
            return Tag("", self_closing=True)
        self._skip_ws()

        closing = self._consume("/")
        self._skip_ws()
        name = self._read_ident()
        attrs: dict[str, str] = {}

        while True:
            self._skip_ws()
            if self._consume(">"):
                return Tag(name, attrs, closing=closing)
            if self._consume("/"):
                self._skip_ws()
                self._expect(">")
                return Tag(name, attrs, self_closing=True)
            attr_name = self._read_ident()
            self._skip_ws()
            if self._consume("="):
                self._skip_ws()
                attrs[attr_name] = self._read_quoted_value()
            else:
                attrs[attr_name] = ""

    def _read_ident(self) -> str:
        start = self.pos
        if self.pos >= self.length or not self.text[self.pos].isalnum():
            raise self._error("expected identifier")
        self.pos += 1
        while self.pos < self.length:
            ch = self.text[self.pos]
            if ch.isalnum() or ch in "_.-":
                self.pos += 1
            else:
                break
        return self.text[start : self.pos]

    def _read_quoted_value(self) -> str:
        if self.pos >= self.length or self.text[self.pos] not in "\"'":
            raise self._error("expected quoted value")
        quote = self.text[self.pos]
        self.pos += 1
        value: list[str] = []
        while self.pos < self.length:
            ch = self.text[self.pos]
            self.pos += 1
            if ch == quote:
                return "".join(value)
            if ch == "\\":
                if self.pos >= self.length:
                    raise self._error("unexpected EOF in escape sequence")
                value.append(self.text[self.pos])
                self.pos += 1
            else:
                value.append(ch)
        raise self._error("unterminated quoted value")

    def _read_until(self, token: str) -> str:
        end = self.text.find(token, self.pos)
        if end < 0:
            end = self.length
        value = self.text[self.pos : end]
        self.pos = end
        return value

    def _skip_ws(self) -> bool:
        while self.pos < self.length and self.text[self.pos].isspace():
            self.pos += 1
        return self.pos < self.length

    def _peek(self) -> str:
        return self.text[self.pos] if self.pos < self.length else ""

    def _consume(self, token: str) -> bool:
        if self.text.startswith(token, self.pos):
            self.pos += len(token)
            return True
        return False

    def _expect(self, token: str) -> None:
        if not self._consume(token):
            raise self._error(f"expected {token!r}")

    def _error(self, message: str) -> ToonzParseError:
        line = self.text.count("\n", 0, self.pos) + 1
        col = self.pos - self.text.rfind("\n", 0, self.pos)
        return ToonzParseError(f"{message} at line {line}, column {col}")


def parse_toonz_file(path: str | Path, *, coerce_scalars: bool = True) -> dict[str, Any]:
    """Read a Toonz file and return its nested dict representation."""

    data = _read_toonz_bytes(Path(path))
    text = data.decode("utf-8", errors="replace")
    return ToonzParser(text).parse(coerce_scalars=coerce_scalars)


def parse_toonz_text(text: str, *, coerce_scalars: bool = True) -> dict[str, Any]:
    """Parse TOStream text that has already been decoded."""

    return ToonzParser(text).parse(coerce_scalars=coerce_scalars)


def parse_file(path: str | Path, *, coerce_scalars: bool = True) -> dict[str, Any]:
    """Parse a supported OpenToonz file.

    `.pli` files are parsed as vector levels and include stroke geometry.
    Other files are parsed as TOStream text.
    """

    path = Path(path)
    if path.suffix.lower() == ".pli":
        return parse_pli_file(path)
    return parse_toonz_file(path, coerce_scalars=coerce_scalars)


def _read_toonz_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"TABc"):
        return _decompress_lz4_tabc(raw)
    if raw.startswith(b"TNZC"):
        return _decompress_lz4_tnzc(raw)
    if raw.startswith(b"\xfd7zXZ\x00"):
        return lzma.decompress(raw)
    return raw


def _decompress_lz4_tabc(raw: bytes) -> bytes:
    if lz4_frame is None:
        raise ToonzParseError(
            "compressed TOStream file requires the optional 'lz4' Python package"
        )
    if len(raw) < 16:
        raise ToonzParseError("corrupted TABc file")
    marker = raw[4:8]
    endian = "<" if marker == b"\r\x0c\x0b\n" else ">"
    if marker not in {b"\r\x0c\x0b\n", b"\n\x0b\x0c\r"}:
        raise ToonzParseError("bad TABc endian marker")
    expected_size, compressed_size = struct.unpack(endian + "II", raw[8:16])
    payload = raw[16 : 16 + compressed_size]
    data = lz4_frame.decompress(payload)
    if len(data) != expected_size:
        raise ToonzParseError("decompressed size mismatch")
    return data


def _decompress_lz4_tnzc(raw: bytes) -> bytes:
    if lz4_frame is None:
        raise ToonzParseError(
            "compressed TOStream file requires the optional 'lz4' Python package"
        )
    if len(raw) < 20:
        raise ToonzParseError("corrupted TNZC file")
    expected_size, compressed_size = struct.unpack("=QQ", raw[4:20])
    payload = raw[20 : 20 + compressed_size]
    data = lz4_frame.decompress(payload)
    if len(data) != expected_size:
        raise ToonzParseError("decompressed size mismatch")
    return data


def _coerce_tokens(text: str) -> Any:
    tokens = list(_split_tstream_tokens(text))
    coerced = [_coerce_scalar(token) for token in tokens]
    if not coerced:
        return ""
    if len(coerced) == 1:
        return coerced[0]
    return coerced


def _split_tstream_tokens(text: str) -> Iterable[str]:
    pos = 0
    length = len(text)
    while pos < length:
        while pos < length and text[pos].isspace():
            pos += 1
        if pos >= length:
            return
        if text[pos] in "\"'":
            quote = text[pos]
            pos += 1
            value: list[str] = []
            while pos < length:
                ch = text[pos]
                pos += 1
                if ch == quote:
                    break
                if ch == "\\" and pos < length:
                    value.append(text[pos])
                    pos += 1
                else:
                    value.append(ch)
            yield "".join(value)
        else:
            start = pos
            while pos < length and not text[pos].isspace():
                pos += 1
            yield text[start:pos]


def _coerce_scalar(value: str) -> Any:
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    try:
        if value and all(ch not in value for ch in ".eE"):
            return int(value)
        return float(value)
    except ValueError:
        return value


class PliTagType:
    END = 0
    SET_DATA_8 = 1
    SET_DATA_16 = 2
    SET_DATA_32 = 3
    THICK_QUADRATIC_CHAIN = 10
    GROUP = 14
    IMAGE = 16
    COLOR = 17
    INTERSECTION_DATA = 21
    IMAGE_BEGIN = 22
    THICK_QUADRATIC_LOOP = 23
    OUTLINE_OPTIONS = 24
    PRECISION_SCALE = 25
    AUTOCLOSE_TOLERANCE = 26


PLI_TAG_NAMES = {
    PliTagType.END: "END_CNTRL",
    PliTagType.SET_DATA_8: "SET_DATA_8_CNTRL",
    PliTagType.SET_DATA_16: "SET_DATA_16_CNTRL",
    PliTagType.SET_DATA_32: "SET_DATA_32_CNTRL",
    PliTagType.THICK_QUADRATIC_CHAIN: "THICK_QUADRATIC_CHAIN_GOBJ",
    PliTagType.GROUP: "GROUP_GOBJ",
    PliTagType.IMAGE: "IMAGE_GOBJ",
    PliTagType.COLOR: "COLOR_NGOBJ",
    PliTagType.INTERSECTION_DATA: "INTERSECTION_DATA_GOBJ",
    PliTagType.IMAGE_BEGIN: "IMAGE_BEGIN_GOBJ",
    PliTagType.THICK_QUADRATIC_LOOP: "THICK_QUADRATIC_LOOP_GOBJ",
    PliTagType.OUTLINE_OPTIONS: "OUTLINE_OPTIONS_GOBJ",
    PliTagType.PRECISION_SCALE: "PRECISION_SCALE_GOBJ",
    PliTagType.AUTOCLOSE_TOLERANCE: "AUTOCLOSE_TOLERANCE_GOBJ",
}


GROUP_TYPES = {
    0: "NONE",
    1: "STROKE",
    2: "SKETCH_STROKE",
    3: "LOOP",
    4: "FILL_SEED",
    5: "PALETTE",
}


COLOR_ATTRIBUTES = {
    0: "ATTRIBUTE_NONE",
    1: "EVENODD_LOOP_FILL",
    2: "DIRECTION_LOOP_FILL",
    3: "STROKE_COLOR",
    4: "LEFT_STROKE_COLOR",
    5: "RIGHT_STROKE_COLOR",
}


@dataclass
class PliObject:
    offset: int
    type_id: int
    type_name: str
    value: dict[str, Any]


@dataclass
class ThickPoint:
    x: float
    y: float
    thick: float

    def to_dict(self) -> dict[str, float]:
        return {"x": self.x, "y": self.y, "thick": self.thick}


@dataclass
class QuadraticSegment:
    p0: ThickPoint
    p1: ThickPoint
    p2: ThickPoint

    def to_dict(self) -> dict[str, dict[str, float]]:
        return {"p0": self.p0.to_dict(), "p1": self.p1.to_dict(), "p2": self.p2.to_dict()}


@dataclass
class Stroke:
    style_id: int | None
    is_loop: bool
    max_thickness: float
    quadratics: list[QuadraticSegment]
    outline_options: dict[str, Any] | None = None
    source_offset: int | None = None

    @property
    def quadratic_count(self) -> int:
        return len(self.quadratics)

    def to_dict(self) -> dict[str, Any]:
        return {
            "style_id": self.style_id,
            "is_loop": self.is_loop,
            "max_thickness": self.max_thickness,
            "quadratic_count": self.quadratic_count,
            "quadratics": [quadratic.to_dict() for quadratic in self.quadratics],
            "outline_options": self.outline_options,
            "source_offset": self.source_offset,
        }


@dataclass
class VectorFrame:
    frame: str
    strokes: list[Stroke]

    def to_dict(self) -> dict[str, Any]:
        return {"frame": self.frame, "strokes": [stroke.to_dict() for stroke in self.strokes]}


@dataclass
class VectorLevel:
    version: dict[str, int]
    creator: str
    frame_count: int
    autoclose_tolerance: float
    precision_scale: int
    frames: list[VectorFrame]

    def to_dict(self) -> dict[str, Any]:
        return {
            "version": dict(self.version),
            "creator": self.creator,
            "frame_count": self.frame_count,
            "autoclose_tolerance": self.autoclose_tolerance,
            "precision_scale": self.precision_scale,
            "frames": [frame.to_dict() for frame in self.frames],
        }

    def get_frame(self, frame_id: str | int) -> VectorFrame | None:
        frame_id = str(frame_id)
        for frame in self.frames:
            if frame.frame == frame_id:
                return frame
        return None

    def iter_strokes(self, frame_id: str | int | None = None) -> Iterable[Stroke]:
        if frame_id is None:
            for frame in self.frames:
                yield from frame.strokes
            return
        frame = self.get_frame(frame_id)
        if frame:
            yield from frame.strokes


class PliParser:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.irix_endian = False
        self.dynamic_size = 2
        self.precision_scale = 128
        self.thick_ratio = 1.0
        self.max_thickness = 0.0
        self.major = 0
        self.minor = 0
        self.objects: dict[int, PliObject] = {}
        self.images: list[PliObject] = []

    def parse_vector_level(self) -> VectorLevel:
        self._read_header()
        while self.pos < len(self.data):
            offset = self.pos
            type_id, payload = self._read_tag()
            if type_id == PliTagType.END:
                break
            obj = self._decode_tag(offset, type_id, payload)
            if obj:
                self.objects[offset] = obj
                if type_id == PliTagType.IMAGE:
                    self.images.append(obj)

        return VectorLevel(
            version={"major": self.major, "minor": self.minor},
            creator=self.creator,
            frame_count=self.frame_count,
            autoclose_tolerance=self.autoclose_tolerance,
            precision_scale=self.precision_scale,
            frames=[self._image_to_frame(image) for image in self.images],
        )

    def parse(self) -> dict[str, Any]:
        return {"pli": self.parse_vector_level().to_dict()}

    def _read_header(self) -> None:
        magic_le = self._read_u32(force_endian="<")
        if magic_le == 0x4D494C50:
            self.irix_endian = False
        elif magic_le == 0x504C494D:
            self.irix_endian = True
        else:
            raise PliParseError("bad .pli magic number")

        self.major = self._read_u8()
        self.minor = self._read_u8()
        self.creator = self._read_string() if self._version_at_least(5, 8) else ""
        self.file_length = self._read_u32()
        self.frame_count = self._read_u16()
        if self._version_at_least(5, 7):
            self.thick_ratio = 0
        else:
            self.max_thickness = self._read_u8()
            self.thick_ratio = self.max_thickness / 255.0

        if self.major < 5:
            self.autoclose_tolerance = 0.0
            return

        sign = self._read_u8() if self._version_at_least(6, 5) else 2
        integer = self._read_u8()
        decimal = self._read_u8()
        self.autoclose_tolerance = (sign - 1) * (integer + 0.01 * decimal)

    def _read_tag(self) -> tuple[int, bytes]:
        first = self._read_u8()
        if first == 0xFF:
            raw_type = self._read_u16()
            length_id = raw_type >> 14
            type_id = raw_type & 0x3FFF
        else:
            length_id = first >> 6
            type_id = first & 0x3F

        if length_id == 0:
            length = 0
        elif length_id == 1:
            length = self._read_u8()
        elif length_id == 2:
            length = self._read_u16()
        else:
            length = self._read_u32()

        payload = self._read_bytes(length)
        return type_id, payload

    def _decode_tag(self, offset: int, type_id: int, payload: bytes) -> PliObject | None:
        if type_id == PliTagType.SET_DATA_8:
            self.dynamic_size = 1
            return None
        if type_id == PliTagType.SET_DATA_16:
            self.dynamic_size = 2
            return None
        if type_id == PliTagType.SET_DATA_32:
            self.dynamic_size = 4
            return None
        if type_id == PliTagType.IMAGE_BEGIN:
            self.dynamic_size = 3
            return None

        value: dict[str, Any]
        if type_id in (
            PliTagType.THICK_QUADRATIC_CHAIN,
            PliTagType.THICK_QUADRATIC_LOOP,
        ):
            value = self._decode_thick_quadratic_chain(
                payload, is_loop=type_id == PliTagType.THICK_QUADRATIC_LOOP
            )
        elif type_id == PliTagType.GROUP:
            value = self._decode_group(payload)
        elif type_id == PliTagType.IMAGE:
            value = self._decode_image(payload)
        elif type_id == PliTagType.COLOR:
            value = self._decode_color(payload)
        elif type_id == PliTagType.OUTLINE_OPTIONS:
            value = self._decode_outline_options(payload)
        elif type_id == PliTagType.PRECISION_SCALE:
            value = self._decode_precision_scale(payload)
            self.precision_scale = value["precision_scale"]
        elif type_id == PliTagType.AUTOCLOSE_TOLERANCE:
            value = self._decode_autoclose_tolerance(payload)
        elif type_id == PliTagType.INTERSECTION_DATA:
            value = {"note": "region intersection data is skipped by this parser"}
        else:
            value = {"raw_length": len(payload)}

        return PliObject(offset, type_id, PLI_TAG_NAMES.get(type_id, str(type_id)), value)

    def _decode_thick_quadratic_chain(
        self, payload: bytes, *, is_loop: bool
    ) -> dict[str, Any]:
        off = 0
        new_thickness = self._version_at_least(5, 7)
        if new_thickness:
            max_thickness = payload[off]
            off += 1
            thick_ratio = max_thickness / 255.0
        else:
            max_thickness = self.max_thickness
            thick_ratio = self.thick_ratio

        scale = 1.0 / float(self.precision_scale)
        x, off = self._read_dyn_signed_from(payload, off)
        y, off = self._read_dyn_signed_from(payload, off)
        thick0 = payload[off] * thick_ratio
        off += 1
        current = {"x": scale * x, "y": scale * y, "thick": thick0}

        per_quad = 4 * self.dynamic_size + (2 if new_thickness else 3)
        count = (len(payload) - off) // per_quad
        quadratics = []
        for _ in range(count):
            p0 = dict(current)
            dx1, off = self._read_dyn_signed_from(payload, off)
            dy1, off = self._read_dyn_signed_from(payload, off)
            if new_thickness:
                thick1 = payload[off] * thick_ratio
                off += 1
            else:
                thick1, off = self._read_old_thickness(payload, off, thick_ratio)
            dx2, off = self._read_dyn_signed_from(payload, off)
            dy2, off = self._read_dyn_signed_from(payload, off)
            thick2 = payload[off] * thick_ratio
            off += 1

            p1 = {
                "x": p0["x"] + scale * dx1,
                "y": p0["y"] + scale * dy1,
                "thick": thick1,
            }
            p2 = {
                "x": p1["x"] + scale * dx2,
                "y": p1["y"] + scale * dy2,
                "thick": thick2,
            }
            quadratics.append({"p0": p0, "p1": p1, "p2": p2})
            current = p2

        return {
            "is_loop": is_loop,
            "max_thickness": max_thickness,
            "quadratic_count": len(quadratics),
            "quadratics": [
                QuadraticSegment(
                    ThickPoint(**quadratic["p0"]),
                    ThickPoint(**quadratic["p1"]),
                    ThickPoint(**quadratic["p2"]),
                )
                for quadratic in quadratics
            ],
        }

    def _decode_group(self, payload: bytes) -> dict[str, Any]:
        if not payload:
            return {"group_type": 0, "group_type_name": "NONE", "object_offsets": []}
        group_type = payload[0]
        offsets = self._read_dyn_list(payload, 1)
        return {
            "group_type": group_type,
            "group_type_name": GROUP_TYPES.get(group_type, str(group_type)),
            "object_offsets": offsets,
        }

    def _decode_image(self, payload: bytes) -> dict[str, Any]:
        off = 0
        frame, off = self._read_u16_from(payload, off)
        suffix = ""
        if self.major >= 150:
            suffix_len, off = self._read_u32_from(payload, off)
            suffix = payload[off : off + suffix_len].decode("utf-8", errors="replace")
            off += suffix_len
        elif self._version_at_least(6, 6):
            letter = payload[off]
            off += 1
            suffix = chr(letter) if letter else ""
        return {"frame": f"{frame}{suffix}", "object_offsets": self._read_dyn_list(payload, off)}

    def _decode_color(self, payload: bytes) -> dict[str, Any]:
        if len(payload) < 2:
            return {"colors": []}
        colors = self._read_dyn_list(payload, 2)
        return {
            "style_type": payload[0],
            "attribute": payload[1],
            "attribute_name": COLOR_ATTRIBUTES.get(payload[1], str(payload[1])),
            "colors": colors,
        }

    def _decode_outline_options(self, payload: bytes) -> dict[str, Any]:
        off = 2
        miter_lower, off = self._read_dyn_signed_from(payload, off)
        miter_upper, off = self._read_dyn_signed_from(payload, off)
        return {
            "cap_style": payload[0] if len(payload) > 0 else None,
            "join_style": payload[1] if len(payload) > 1 else None,
            "miter_lower": 0.001 * miter_lower,
            "miter_upper": 0.001 * miter_upper,
        }

    def _decode_precision_scale(self, payload: bytes) -> dict[str, Any]:
        value, _ = self._read_dyn_signed_from(payload, 0)
        return {"precision_scale": value}

    def _decode_autoclose_tolerance(self, payload: bytes) -> dict[str, Any]:
        value, _ = self._read_dyn_signed_from(payload, 0)
        return {"autoclose_tolerance": value}

    def _image_to_frame(self, image: PliObject) -> VectorFrame:
        state = {"style_id": None, "outline_options": None}
        strokes: list[Stroke] = []
        for object_offset in image.value["object_offsets"]:
            self._collect_strokes(object_offset, strokes, state)
        return VectorFrame(frame=image.value["frame"], strokes=strokes)

    def _collect_strokes(
        self, object_offset: int, strokes: list[Stroke], state: dict[str, Any]
    ) -> None:
        obj = self.objects.get(object_offset)
        if obj is None:
            return
        if obj.type_id == PliTagType.COLOR and obj.value.get("colors"):
            state["style_id"] = obj.value["colors"][0]
        elif obj.type_id == PliTagType.OUTLINE_OPTIONS:
            state["outline_options"] = obj.value
        elif obj.type_id == PliTagType.GROUP:
            local_state = dict(state)
            for child_offset in obj.value["object_offsets"]:
                self._collect_strokes(child_offset, strokes, local_state)
        elif obj.type_id in (
            PliTagType.THICK_QUADRATIC_CHAIN,
            PliTagType.THICK_QUADRATIC_LOOP,
        ):
            strokes.append(
                Stroke(
                    style_id=state.get("style_id"),
                    is_loop=obj.value["is_loop"],
                    max_thickness=obj.value["max_thickness"],
                    quadratics=obj.value["quadratics"],
                    outline_options=state.get("outline_options"),
                    source_offset=object_offset,
                )
            )

    def _read_dyn_list(self, payload: bytes, off: int) -> list[int]:
        values = []
        while off < len(payload):
            value, off = self._read_dyn_unsigned_from(payload, off)
            values.append(value)
        return values

    def _read_dyn_unsigned_from(self, payload: bytes, off: int) -> tuple[int, int]:
        chunk = payload[off : off + self.dynamic_size]
        if len(chunk) != self.dynamic_size:
            raise PliParseError("unexpected EOF in dynamic integer")
        return int.from_bytes(chunk, self._byteorder(), signed=False), off + self.dynamic_size

    def _read_dyn_signed_from(self, payload: bytes, off: int) -> tuple[int, int]:
        raw, off = self._read_dyn_unsigned_from(payload, off)
        sign_bit = 1 << (self.dynamic_size * 8 - 1)
        value = raw & (sign_bit - 1)
        return (-value if raw & sign_bit else value), off

    def _read_old_thickness(
        self, payload: bytes, off: int, thick_ratio: float
    ) -> tuple[float, int]:
        raw = int.from_bytes(payload[off : off + 2], self._byteorder(), signed=False)
        value = -(raw & 0x7FFF) if raw & 0x8000 else raw & 0x7FFF
        return value * thick_ratio, off + 2

    def _read_u8(self) -> int:
        return self._read_bytes(1)[0]

    def _read_u16(self) -> int:
        value, self.pos = self._read_u16_from(self.data, self.pos)
        return value

    def _read_u32(self, *, force_endian: str | None = None) -> int:
        endian = force_endian or ("<" if not self.irix_endian else ">")
        value = struct.unpack_from(endian + "I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def _read_string(self) -> str:
        length = self._read_u16()
        return self._read_bytes(length).decode("utf-8", errors="replace")

    def _read_bytes(self, size: int) -> bytes:
        if self.pos + size > len(self.data):
            raise PliParseError("unexpected EOF")
        chunk = self.data[self.pos : self.pos + size]
        self.pos += size
        return chunk

    def _read_u16_from(self, payload: bytes, off: int) -> tuple[int, int]:
        return int.from_bytes(payload[off : off + 2], self._byteorder()), off + 2

    def _read_u32_from(self, payload: bytes, off: int) -> tuple[int, int]:
        return int.from_bytes(payload[off : off + 4], self._byteorder()), off + 4

    def _byteorder(self) -> str:
        return "big" if self.irix_endian else "little"

    def _version_at_least(self, major: int, minor: int) -> bool:
        return self.major > major or (self.major == major and self.minor >= minor)


def parse_pli_file(path: str | Path) -> dict[str, Any]:
    """Read an OpenToonz vector level (.pli) and return frame stroke data."""

    return {"pli": read_vector_level(path).to_dict()}


def read_vector_level(path: str | Path) -> VectorLevel:
    """Read a `.pli` Vector Level and return structured frame/stroke objects."""

    return PliParser(Path(path).read_bytes()).parse_vector_level()


def read_vector_level_strokes(
    path: str | Path, frame_id: str | int | None = None
) -> list[Stroke]:
    """Return strokes from a `.pli` Vector Level.

    If `frame_id` is omitted, strokes from all frames are returned in file order.
    If `frame_id` is provided, only strokes from that frame are returned.
    """

    return list(read_vector_level(path).iter_strokes(frame_id))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Parse an OpenToonz .tnz/TOStream file and print JSON."
    )
    parser.add_argument("path", help="Path to a .tnz, TOStream file, or .pli vector level")
    parser.add_argument(
        "--no-coerce",
        action="store_true",
        help="Keep text token values as strings instead of int/float/bool",
    )
    parser.add_argument(
        "--compact", action="store_true", help="Print compact one-line JSON"
    )
    args = parser.parse_args()

    parsed = parse_file(args.path, coerce_scalars=not args.no_coerce)
    json_kwargs = {"ensure_ascii": False}
    if not args.compact:
        json_kwargs["indent"] = 2
    print(json.dumps(parsed, **json_kwargs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
