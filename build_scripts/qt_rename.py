#!/usr/bin/env python3
"""Replace Qt core types and helpers with AnimeCore / standard library equivalents.

Usage:
    # Convert the default 12 target files into .scratch/trial/ (preserving relative paths):
    py build_scripts/qt_rename.py

    # Convert specific files into a custom output directory:
    py build_scripts/qt_rename.py --out-dir .scratch/trial backend/main.cpp theme.h theme.cpp

    # Convert specific files in place (overwriting the originals):
    py build_scripts/qt_rename.py --in-place theme.h theme.cpp

    # Convert a single file to stdout:
    py build_scripts/qt_rename.py --stdout theme.cpp
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import Iterable

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

DEFAULT_FILES: list[str] = [
    "backend/session/backendsession.h",
    "backend/session/backendsession.cpp",
    "backend/business/animeanbusiness.h",
    "backend/business/animeanbusiness.cpp",
    "openglwidget.h",
    "openglwidget.cpp",
    "pythonbind/python_bindings.h",
    "pythonbind/python_bindings.cpp",
    "theme.h",
    "theme.cpp",
    "backend/main.cpp",
    "backend/board/tests/board_headless_test.cpp",
]

# Mapping from Qt include headers to replacement headers.
INCLUDE_MAP: dict[str, list[str]] = {
    "QString": ['"algorithm/core/string.h"'],
    "QStringList": ['"algorithm/core/string.h"'],
    "QChar": ['"algorithm/core/string.h"'],
    "QLatin1Char": ['"algorithm/core/string.h"'],
    "QLatin1String": ['"algorithm/core/string.h"'],
    "QByteArray": ["<string>"],
    "QVector": ['"algorithm/core/containers.h"'],
    "QList": ['"algorithm/core/containers.h"'],
    "QHash": ['"algorithm/core/containers.h"'],
    "QSet": ['"algorithm/core/containers.h"'],
    "QMap": ['"algorithm/core/containers.h"'],
    "QQueue": ['"algorithm/core/containers.h"'],
    "QPair": ["<utility>"],
    "QPoint": ['"algorithm/core/geometry.h"'],
    "QPointF": ['"algorithm/core/geometry.h"'],
    "QRect": ['"algorithm/core/geometry.h"'],
    "QRectF": ['"algorithm/core/geometry.h"'],
    "QSize": ['"algorithm/core/geometry.h"'],
    "QSizeF": ['"algorithm/core/geometry.h"'],
    "QLineF": ['"algorithm/core/geometry.h"'],
    "QPolygonF": ['"algorithm/core/containers.h"', '"algorithm/core/geometry.h"'],
    "QTransform": ['"algorithm/core/geometry.h"'],
    "QColor": ['"algorithm/core/color.h"'],
    "QImage": ['"algorithm/core/image.h"'],
    "QPainterPath": ['"algorithm/core/path.h"'],
    "QJsonObject": ['"algorithm/core/json.h"'],
    "QJsonArray": ['"algorithm/core/json.h"'],
    "QJsonValue": ['"algorithm/core/json.h"'],
    "QJsonDocument": ['"algorithm/core/json.h"'],
    "QJsonParseError": ['"algorithm/core/json.h"'],
    "QtGlobal": ["<cstdint>", '"algorithm/core/geometry.h"'],
}

# Ordered token replacements (matched on word boundaries outside string/comment literals).
QUALIFIED_RENAMES: list[tuple[str, str]] = [
    ("Qt::CaseInsensitive", "CaseInsensitive"),
    ("Qt::CaseSensitive", "CaseSensitive"),
    ("Qt::KeepEmptyParts", "KeepEmptyParts"),
    ("Qt::SkipEmptyParts", "SkipEmptyParts"),
    ("Qt::OddEvenFill", "OddEvenFill"),
    ("Qt::WindingFill", "WindingFill"),
]

IDENTIFIER_RENAMES: dict[str, str] = {
    "QString": "String",
    "QStringLiteral": "String",
    "QStringList": "StringList",
    "QChar": "Char",
    "QLatin1Char": "Latin1Char",
    "QLatin1String": "Latin1String",
    "QVector": "Vector",
    "QList": "Vector",
    "QHash": "Hash",
    "QSet": "Set",
    "QMap": "Map",
    "QQueue": "Queue",
    "QPair": "std::pair",
    "qMakePair": "std::make_pair",
    "QPointF": "PointF",
    "QPoint": "Point",
    "QRectF": "RectF",
    "QRect": "Rect",
    "QSizeF": "SizeF",
    "QSize": "Size",
    "QLineF": "LineF",
    "QPolygonF": "Vector<PointF>",
    "QColor": "Color",
    "QImage": "Image",
    "QTransform": "Transform",
    "QPainterPath": "Path",
    "QJsonObject": "JsonObject",
    "QJsonArray": "JsonArray",
    "QJsonValue": "JsonValue",
    "QJsonDocument": "JsonDocument",
    "QJsonParseError": "JsonParseError",
    "QByteArray": "std::string",
    "QByteArrayLiteral": "std::string",
    "qreal": "double",
    "qint64": "std::int64_t",
    "quint64": "std::uint64_t",
    "qint32": "std::int32_t",
    "quint32": "std::uint32_t",
    "qint16": "std::int16_t",
    "quint16": "std::uint16_t",
    "qint8": "std::int8_t",
    "quint8": "std::uint8_t",
    "qulonglong": "unsigned long long",
    "qlonglong": "long long",
    "qsizetype": "std::ptrdiff_t",
    "uint": "unsigned int",
    "uchar": "unsigned char",
    "ushort": "unsigned short",
    "ulong": "unsigned long",
    "qFuzzyCompare": "fuzzyCompare",
    "qFuzzyIsNull": "fuzzyIsNull",
    "qBound": "bound",
    "qAbs": "std::abs",
    "qDegreesToRadians": "degreesToRadians",
    "qRadiansToDegrees": "radiansToDegrees",
}

_INCLUDE_RE = re.compile(r"^(\s*#\s*include\s*)<([A-Za-z0-9_]+)>(\s*(?://.*)?)$")

# Tokenizer that separates comments, string/char literals, preprocessor #include lines,
# qualified Qt:: tokens, and normal identifiers so we never corrupt string literals or comments.
_TOKEN_RE = re.compile(
    r"(?P<line_comment>//[^\r\n]*)"
    r"|(?P<block_comment>/\*[\s\S]*?\*/)"
    r"|(?P<raw_string>R\"([^()\\\s]{0,16})\([\s\S]*?\)\4\")"
    r"|(?P<string>\"(?:\\.|[^\"\\\r\n])*\")"
    r"|(?P<char>'(?:\\.|[^'\\\r\n])*')"
    r"|(?P<include>^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"][^\r\n]*)"
    r"|(?P<qualified>\bQt::[A-Za-z_]\w*\b)"
    r"|(?P<ident>(?<!::)\b[A-Za-z_]\w*\b)",
    re.MULTILINE,
)

_QUALIFIED_LOOKUP: dict[str, str] = dict(QUALIFIED_RENAMES)


def _rewrite_includes(lines: list[str]) -> tuple[list[str], bool]:
    """Replace mapped Qt #include directives and ensure AnimeCore headers are available."""
    seen_includes: set[str] = set()
    for line in lines:
        m_any = re.match(r"^\s*#\s*include\s*([<\"][^>\"]+[>\"])", line)
        if m_any:
            seen_includes.add(m_any.group(1))

    out_lines: list[str] = []
    replaced_any = False

    for line in lines:
        m = _INCLUDE_RE.match(line)
        if m and m.group(2) in INCLUDE_MAP:
            replaced_any = True
            for target in INCLUDE_MAP[m.group(2)]:
                if target not in seen_includes:
                    seen_includes.add(target)
                    out_lines.append(f"#include {target}")
        else:
            out_lines.append(line)

    return out_lines, replaced_any


def _ensure_core_preamble(text: str) -> str:
    """Ensure core headers, qtbridge.h, <cstdint>, <string>, and using namespace AnimeCore are present."""
    required_headers = [
        '"algorithm/core/qtbridge.h"',
        '"algorithm/core/color.h"',
        '"algorithm/core/containers.h"',
        '"algorithm/core/geometry.h"',
        '"algorithm/core/image.h"',
        '"algorithm/core/json.h"',
        '"algorithm/core/path.h"',
        '"algorithm/core/string.h"',
        "<cstdint>",
        "<string>",
        "<utility>",
    ]

    lines = text.splitlines()
    existing = set()
    for line in lines:
        m = re.match(r"^\s*#\s*include\s*([<\"][^>\"]+[>\"])", line)
        if m:
            existing.add(m.group(1))

    missing = [f"#include {h}" for h in required_headers if h not in existing]
    has_using = bool(re.search(r"^\s*using\s+namespace\s+AnimeCore\s*;", text, re.MULTILINE))

    # Find first #include (to insert qtbridge.h and core headers before any Qt/Core includes)
    # and last top-level #include (to insert using namespace AnimeCore;).
    first_include_idx = -1
    last_include_idx = 0
    in_ifdef = 0
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if re.match(r"^#\s*if", stripped):
            # Ignore header guard at very top of .h file
            if idx <= 3 and re.match(r"^#\s*ifndef\s+\w+_H\b", stripped):
                continue
            in_ifdef += 1
        elif re.match(r"^#\s*endif\b", stripped):
            if in_ifdef > 0:
                in_ifdef -= 1
        elif in_ifdef == 0 and re.match(r"^#\s*include\b", stripped):
            if first_include_idx == -1:
                first_include_idx = idx
            last_include_idx = idx + 1
        elif in_ifdef == 0 and re.match(r"^#\s*define\b", stripped) and first_include_idx == -1:
            last_include_idx = idx + 1

    if first_include_idx == -1:
        first_include_idx = last_include_idx

    if missing:
        lines = lines[:first_include_idx] + missing + lines[first_include_idx:]
        last_include_idx += len(missing)

    if not has_using:
        lines = lines[:last_include_idx] + ["using namespace AnimeCore;"] + lines[last_include_idx:]

    return "\r\n".join(lines) + ("\r\n" if text.endswith(("\n", "\r\n")) else "")


def transform_source(text: str) -> str:
    """Transform Qt core type/helper names in C++ source text to AnimeCore equivalents."""
    lines = text.splitlines()
    lines, _ = _rewrite_includes(lines)
    joined = "\r\n".join(lines) + ("\r\n" if text.endswith(("\n", "\r\n")) else "")

    def _replace_match(match: re.Match[str]) -> str:
        qual = match.group("qualified")
        if qual is not None:
            return _QUALIFIED_LOOKUP.get(qual, qual)
        ident = match.group("ident")
        if ident is not None:
            return IDENTIFIER_RENAMES.get(ident, ident)
        return match.group(0)

    transformed = _TOKEN_RE.sub(_replace_match, joined)
    return _ensure_core_preamble(transformed)


def convert_files(
    files: Iterable[str],
    out_dir: pathlib.Path | None,
    in_place: bool = False,
    to_stdout: bool = False,
) -> None:
    for rel_or_abs in files:
        src_path = pathlib.Path(rel_or_abs)
        if not src_path.is_absolute():
            src_path = REPO_ROOT / src_path
        raw = src_path.read_text(encoding="utf-8")
        converted = transform_source(raw)

        if to_stdout:
            sys.stdout.write(converted)
            continue

        if in_place:
            dst_path = src_path
        else:
            assert out_dir is not None
            try:
                rel = src_path.resolve().relative_to(REPO_ROOT)
            except ValueError:
                rel = pathlib.Path(src_path.name)
            dst_path = out_dir / rel

        dst_path.parent.mkdir(parents=True, exist_ok=True)
        with open(dst_path, "w", encoding="utf-8", newline="") as f:
            f.write(converted)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Rename Qt core types/methods in C++ files to AnimeCore equivalents."
    )
    parser.add_argument(
        "files",
        nargs="*",
        default=DEFAULT_FILES,
        help="Input C++ files (default: the 12 backend/paint/pythonbind target files).",
    )
    parser.add_argument(
        "--out-dir",
        "-d",
        default=".scratch/trial",
        help="Output directory when not using --in-place (default: .scratch/trial).",
    )
    parser.add_argument(
        "--in-place",
        "-i",
        action="store_true",
        help="Modify input files in place instead of writing to --out-dir.",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Write transformed result to stdout.",
    )
    args = parser.parse_args(argv)

    out_dir = None if (args.in_place or args.stdout) else (REPO_ROOT / args.out_dir)
    convert_files(args.files, out_dir=out_dir, in_place=args.in_place, to_stdout=args.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
