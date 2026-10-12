"""Draw the paper's figures into ../assets/src/.

    py paper/figures/make_figures.py

Two kinds of figure:

1. Pictures taken from the 2026-10-06 seminar deck (the T-shirt sketch, the
   annotated screenshot, the parameter, flow-field and outline diagrams, the
   motion sheet). Their word-free bitmaps are kept in ../assets/base/; this
   script writes the words on them in Chinese at the places recorded in
   labels.json (pixel coordinates of each bitmap, the anchor rule of the deck:
   first letter l/m/r across, second letter t/a/m/b down).
2. One figure computed here from pyfile/auto_mapping.py itself: a pattern
   mapped through a main-board axis that folds over (front runs, back runs and
   the crease between them).
"""
import json
import math
import os
import sys
import types
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
PAPER = HERE.parent
BASE = PAPER / "assets" / "base"
SRC = PAPER / "assets" / "src"
ROOT = PAPER.parent
FONT = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def put_label(draw, item, size):
    """One label: text at (x, y) under the deck's anchor rule, on an optional plate."""
    f = font(size, item.get("bold", False))
    lines = item["text"].split("\n")
    pad = round(size * 0.32)
    gap = round(size * 0.28)
    widths = [draw.textlength(line, font=f) for line in lines]
    w = max(widths) + 2 * pad
    h = len(lines) * size + (len(lines) - 1) * gap + 2 * pad
    across, down = item["anchor"][0], item["anchor"][1]
    x = item["x"] - {"l": 0, "m": w / 2, "r": w}[across]
    y = item["y"] - {"m": h / 2, "a": pad, "t": pad, "b": h - pad, "s": h - pad}[down]
    if item.get("plate"):
        draw.rectangle([x, y, x + w, y + h], fill="#FFFFFF", outline="#000000",
                       width=max(1, round(size / 14)))
    for n, line in enumerate(lines):
        if item.get("plate") or across == "m":
            lx = x + (w - widths[n]) / 2
        elif across == "r":
            lx = x + w - pad - widths[n]
        else:
            lx = x + pad
        draw.text((lx, y + pad + n * (size + gap) - size * 0.12), line, font=f, fill=item["colour"])


def labelled(name, out, size, crop=None, extend=None):
    """The bitmap `name` with its labels; `crop` = (l, t, r, b) in pixels, taken after drawing."""
    img = Image.open(BASE / name).convert("RGB")
    if extend:                                  # white margin (l, t, r, b) added before drawing
        l, t, r, b = extend
        wide = Image.new("RGB", (img.width + l + r, img.height + t + b), "#FFFFFF")
        wide.paste(img, (l, t))
        img, shift = wide, (l, t)
    else:
        shift = (0, 0)
    draw = ImageDraw.Draw(img)
    for item in LABELS[Path(name).stem]:
        item = dict(item, x=item["x"] + shift[0], y=item["y"] + shift[1])
        put_label(draw, item, size)
    if crop:
        img = img.crop(crop)
    if out is None:
        return img
    img.save(SRC / out, optimize=True)
    print("wrote", out, img.size)
    return img


def side_by_side(img, split, gap=40, trim=None):
    """The upper and lower panel of a picture (cut at y = split) set left and right."""
    top, bottom = img.crop((0, 0, img.width, split)), img.crop((0, split, img.width, img.height))
    if trim:                                    # (left, right) pixels of white cut from each panel
        top = top.crop((trim[0], 0, top.width - trim[1], top.height))
        bottom = bottom.crop((trim[0], 0, bottom.width - trim[1], bottom.height))
    height = max(top.height, bottom.height)
    out = Image.new("RGB", (top.width + gap + bottom.width, height), "#FFFFFF")
    out.paste(top, (0, (height - top.height) // 2))
    out.paste(bottom, (top.width + gap, (height - bottom.height) // 2))
    return out


def deck_figures():
    shirt = labelled("pic-shirt.png", None, 40)
    shirt = side_by_side(shirt, 720, trim=(150, 0))
    shirt.save(SRC / "shirt_axes.png", optimize=True)
    print("wrote shirt_axes.png", shirt.size)
    labelled("fig-app.png", "app_screens.png", 30, crop=(0, 20, 1800, 1010))
    params = labelled("fig-params.png", None, 36, crop=(40, 0, 1200, 1400))
    params = side_by_side(params, 640)
    params.save(SRC / "axis_mapping.png", optimize=True)
    print("wrote axis_mapping.png", params.size)
    labelled("fig-flow.png", "flow_field.png", 26, crop=(60, 0, 1440, 640))
    labelled("fig-outline.png", "outline_modes.png", 26, crop=(60, 0, 1440, 640))
    sheet = Image.open(BASE / "motion-sheet.jpg").convert("RGB")
    sheet.thumbnail((1250, 1250))               # the pipeline places PNG only
    sheet.save(SRC / "motion_sheet.png", optimize=True)
    print("wrote motion_sheet.png", sheet.size)


# ---------------------------------------------------------------------------
# the fold figure, computed with the repository's own mapper
# ---------------------------------------------------------------------------
def arc_from(p, heading, radius, turn, n=40):
    """Points of a circular arc leaving p in direction `heading` (radians), turning by `turn`."""
    left = 1.0 if turn > 0 else -1.0
    cx = p[0] - left * radius * math.sin(heading)
    cy = p[1] + left * radius * math.cos(heading)
    pts = []
    for k in range(1, n + 1):
        a = heading + turn * k / n
        pts.append((cx + left * radius * math.sin(a), cy - left * radius * math.cos(a)))
    return pts, heading + turn


def pleat_axis(length=760.0):
    """A main-board H axis that runs right, folds back over itself and runs right again."""
    pts = [(-380.0, 0.0), (-100.0, 0.0)]
    heading = 0.0
    turn, radius, back = math.radians(165.0), 40.0, 110.0
    arc, heading = arc_from(pts[-1], heading, radius, turn)
    pts += arc
    pts.append((pts[-1][0] + back * math.cos(heading), pts[-1][1] + back * math.sin(heading)))
    arc, heading = arc_from(pts[-1], heading, radius, -turn)
    pts += arc
    used = 280.0 + 2.0 * radius * turn + back
    pts.append((pts[-1][0] + (length - used), pts[-1][1]))
    return pts


def fold_figure():
    sys.path.insert(0, str(ROOT / "pyfile"))
    sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
    import auto_mapping as am
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams["font.family"] = ["Microsoft YaHei", "sans-serif"]
    plt.rcParams["axes.unicode_minus"] = False

    child_h = [(-380.0, 0.0), (380.0, 0.0)]
    child_v = [(-250.0, -90.0), (-250.0, 90.0)]
    main_h = pleat_axis()
    main_v = [(-250.0, -90.0), (-250.0, 90.0)]
    info = {}
    mp, _ = am.build_mapper(child_h, child_v, main_h, main_v, info)
    assert mp is not None

    pattern = []                                   # a lattice with one diagonal in every other cell
    xs = [-360.0 + 45.0 * k for k in range(17)]
    ys = [-67.5 + 45.0 * k for k in range(4)]
    for y in ys:
        pattern.append([(xs[0], y), (xs[-1], y)])
    for x in xs:
        pattern.append([(x, ys[0]), (x, ys[-1])])
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            if (i + j) % 2 == 0:
                pattern.append([(xs[i] + 10.0, ys[j] + 10.0), (xs[i] + 35.0, ys[j] + 35.0)])

    front, back = "#1b1b1b", "#68708c"             # the lining colour is the tool's default
    fig, axes = plt.subplots(1, 2, figsize=(9.4, 3.05), dpi=220)
    creases = {}
    counts = {1: 0, -1: 0}
    for line in pattern:
        runs = am._split_by_fold(mp, am._densify(line))
        for k, (run, side) in enumerate(runs):
            counts[side] += 1
            colour = front if side > 0 else back
            width = 1.3 if side > 0 else 1.1
            axes[0].plot([p[0] for p in run], [p[1] for p in run], color=colour, lw=width,
                         solid_capstyle="round")
            image = [q for q, _ in am._adaptive_map_polyline(mp, run)]
            axes[1].plot([p[0] for p in image], [p[1] for p in image], color=colour, lw=width,
                         zorder=3 if side > 0 else 2, solid_capstyle="round")
            if k + 1 < len(runs):                   # the shared point of two runs lies on a crease
                cut = run[-1]
                creases.setdefault(round(cut[0]), []).append(mp(cut))
    for key, pts in sorted(creases.items()):
        if len(pts) < 3:
            continue
        pts.sort(key=lambda p: p[1])
        axes[1].plot([p[0] for p in pts], [p[1] for p in pts], color="#d03030", lw=1.5,
                     ls=(0, (4, 3)), zorder=4)
    titles = ("(a) 纹理板：平铺的图案与两条直轴线", "(b) 主画板：H 轴折返时的输出")
    for ax, (h, v), name in zip(axes, ((child_h, child_v), (main_h, main_v)), titles):
        ax.plot([p[0] for p in h], [p[1] for p in h], color="#2E6EDC", lw=2.0, zorder=5)
        ax.plot([p[0] for p in v], [p[1] for p in v], color="#28A05A", lw=2.0, zorder=5)
        ax.text(h[-1][0] + 8.0, h[-1][1], "H", color="#2E6EDC", fontsize=10, va="center")
        ax.text(v[-1][0], v[-1][1] + 6.0, "V", color="#28A05A", fontsize=10, ha="center", va="bottom")
        ax.set_aspect("equal")
        ax.set_xlim(-395.0, 410.0)
        ax.axis("off")
        ax.set_title(name, fontsize=9.5, loc="left", pad=2.0)
    top = max(p[1] for p in main_h) + 118.0
    axes[0].set_ylim(-100.0 - (top - 118.0) / 2.0, 118.0 + (top - 118.0) / 2.0)
    axes[1].set_ylim(-100.0, top)
    handles = [plt.Line2D([], [], color=front, lw=1.6, label="正面"),
               plt.Line2D([], [], color=back, lw=1.6, label="背面（衬里色）"),
               plt.Line2D([], [], color="#d03030", lw=1.5, ls=(0, (4, 3)), label="折痕")]
    fig.legend(handles=handles, loc="lower center", ncol=3, frameon=False, fontsize=9)
    fig.subplots_adjust(left=0.01, right=0.99, top=0.93, bottom=0.10, wspace=0.03)
    fig.savefig(SRC / "fold_layers.png")
    plt.close(fig)
    print("wrote fold_layers.png  front runs %d, back runs %d, creases %s, mirrored=%s"
          % (counts[1], counts[-1], sorted(k for k, v in creases.items() if len(v) >= 3),
             info.get("mirrored")))


LABELS = json.loads((HERE / "labels.json").read_text(encoding="utf-8"))

if __name__ == "__main__":
    SRC.mkdir(parents=True, exist_ok=True)
    deck_figures()
    fold_figure()
