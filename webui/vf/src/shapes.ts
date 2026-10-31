// 基础图形和线条：形状的轮廓怎样画、线条怎样走、端头怎样画，以及工具自带的预设。
// 这里只算几何和数据，不碰网页元素，所以也能在命令行和导出时用。
//
// 参照 Figma 的形状工具（矩形、直线、箭头、椭圆、多边形、星形）和 FigJam 的形状库
// （基础图形、流程图图形）以及连接线（直线、折线、曲线，五种端头）。
import type { Value } from "./expr.ts";
import type { ComponentDecl } from "./model.ts";

// ---------- 节点类型的声明 ----------

/** 形状：一个可以填色、描边、写字的图形。 */
export const SHAPE_DECL: ComponentDecl = {
  type: "Shape",
  label: "形状",
  description: "矩形、椭圆、菱形、星形、流程图图形等。可以填色、描边、在里面写字",
  defaultSize: { width: 120, height: 80 },
  props: {
    shape: { type: "string", default: "rectangle", logic: true },
    fill: { type: "string", default: "#ffffff", logic: true },
    stroke: { type: "string", default: "#1f2430", logic: true },
    strokeWidth: { type: "number", default: 2, logic: true },
    strokeStyle: { type: "string", default: "solid", logic: true },
    radius: { type: "number", default: 0, logic: true },
    opacity: { type: "number", default: 1, logic: true },
    sides: { type: "number", default: 5, logic: true },
    points: { type: "number", default: 5, logic: true },
    ratio: { type: "number", default: 0.45, logic: true },
    text: { type: "string", default: "", logic: true },
    textColor: { type: "string", default: "#1f2430", logic: true },
    fontSize: { type: "number", default: 14, logic: true },
    fontWeight: { type: "string", default: "normal", logic: true },
    textAlign: { type: "string", default: "center", logic: true },
  },
  children: false,
};

/** 线条：直线、折线或曲线，两端可以挂在别的节点上，跟着它们走。 */
export const LINE_DECL: ComponentDecl = {
  type: "Line",
  label: "线条",
  description: "直线、箭头、连接线。两端可以挂在形状或控件上，被挂的东西移动时线跟着走",
  defaultSize: { width: 160, height: 0 },
  props: {
    x1: { type: "number", default: 0 },
    y1: { type: "number", default: 0 },
    x2: { type: "number", default: 160 },
    y2: { type: "number", default: 0 },
    startNode: { type: "string", default: "" },
    startSide: { type: "string", default: "auto" },
    endNode: { type: "string", default: "" },
    endSide: { type: "string", default: "auto" },
    startAt: { type: "string", default: "" },
    endAt: { type: "string", default: "" },
    bends: { type: "string", default: "" },
    route: { type: "string", default: "straight", logic: true },
    stroke: { type: "string", default: "#1f2430", logic: true },
    strokeWidth: { type: "number", default: 2, logic: true },
    strokeStyle: { type: "string", default: "solid", logic: true },
    startCap: { type: "string", default: "none", logic: true },
    endCap: { type: "string", default: "none", logic: true },
    opacity: { type: "number", default: 1, logic: true },
    label: { type: "string", default: "", logic: true },
    textColor: { type: "string", default: "#1f2430", logic: true },
    fontSize: { type: "number", default: 12, logic: true },
  },
  children: false,
};

// ---------- 形状 ----------

export interface ShapeKind {
  /** 写在 props.shape 里的名字 */
  id: string;
  label: string;
  group: "basic" | "flowchart";
  /** 文字区域离边框的距离，按宽高的比例 */
  textInset?: [number, number];
}

export const SHAPE_KINDS: ShapeKind[] = [
  { id: "rectangle", label: "矩形", group: "basic" },
  { id: "ellipse", label: "椭圆", group: "basic", textInset: [0.15, 0.15] },
  { id: "diamond", label: "菱形", group: "basic", textInset: [0.25, 0.25] },
  { id: "triangle", label: "三角形", group: "basic", textInset: [0.25, 0.35] },
  { id: "triangleDown", label: "倒三角", group: "basic", textInset: [0.25, 0.1] },
  { id: "polygon", label: "多边形", group: "basic", textInset: [0.2, 0.2] },
  { id: "star", label: "星形", group: "basic", textInset: [0.3, 0.3] },
  { id: "plus", label: "十字", group: "basic", textInset: [0.33, 0.33] },
  { id: "arrowRight", label: "右箭头", group: "basic", textInset: [0.05, 0.28] },
  { id: "arrowLeft", label: "左箭头", group: "basic", textInset: [0.05, 0.28] },
  { id: "arrowBoth", label: "双向箭头", group: "basic", textInset: [0.2, 0.28] },
  { id: "chevron", label: "折角箭头", group: "basic", textInset: [0.2, 0.1] },
  { id: "speechBubble", label: "对话气泡", group: "basic", textInset: [0.06, 0.08] },
  { id: "cloud", label: "云", group: "basic", textInset: [0.18, 0.22] },
  { id: "hexagon", label: "六边形", group: "flowchart", textInset: [0.2, 0.05] },
  { id: "parallelogram", label: "平行四边形", group: "flowchart", textInset: [0.2, 0.05] },
  { id: "parallelogramLeft", label: "左倾平行四边形", group: "flowchart", textInset: [0.2, 0.05] },
  { id: "trapezoid", label: "梯形", group: "flowchart", textInset: [0.2, 0.05] },
  { id: "manualInput", label: "手动输入", group: "flowchart", textInset: [0.05, 0.2] },
  { id: "cylinder", label: "圆柱（数据库）", group: "flowchart", textInset: [0.05, 0.25] },
  { id: "cylinderHorizontal", label: "横向圆柱", group: "flowchart", textInset: [0.2, 0.05] },
  { id: "document", label: "文档", group: "flowchart", textInset: [0.05, 0.1] },
  { id: "multiDocument", label: "多文档", group: "flowchart", textInset: [0.06, 0.15] },
  { id: "predefinedProcess", label: "预定义处理", group: "flowchart", textInset: [0.14, 0.05] },
  { id: "internalStorage", label: "内部存储", group: "flowchart", textInset: [0.18, 0.24] },
  { id: "file", label: "文件", group: "flowchart", textInset: [0.06, 0.2] },
  { id: "or", label: "或", group: "flowchart", textInset: [0.2, 0.2] },
  { id: "summingJunction", label: "求和", group: "flowchart", textInset: [0.2, 0.2] },
  { id: "text", label: "文字", group: "basic", textInset: [0, 0] },
];

export function shapeKind(id: string): ShapeKind {
  return SHAPE_KINDS.find((kind) => kind.id === id) ?? SHAPE_KINDS[0];
}

/** 形状画出来的样子：body 是填色加描边的轮廓，details 是只描边的附加线（圆柱的上沿、预定义处理的两条竖线等）。 */
export interface ShapeGeometry {
  body: string;
  details: string[];
}

const fmt = (n: number) => String(Math.round(n * 100) / 100);

function polygonPath(points: Array<[number, number]>): string {
  return points.map(([x, y], i) => `${i === 0 ? "M" : "L"}${fmt(x)} ${fmt(y)}`).join(" ") + " Z";
}

function roundedRect(x: number, y: number, w: number, h: number, r: number): string {
  const radius = Math.max(0, Math.min(r, w / 2, h / 2));
  if (radius === 0) return polygonPath([[x, y], [x + w, y], [x + w, y + h], [x, y + h]]);
  const R = fmt(radius);
  return [
    `M${fmt(x + radius)} ${fmt(y)}`,
    `H${fmt(x + w - radius)}`,
    `A${R} ${R} 0 0 1 ${fmt(x + w)} ${fmt(y + radius)}`,
    `V${fmt(y + h - radius)}`,
    `A${R} ${R} 0 0 1 ${fmt(x + w - radius)} ${fmt(y + h)}`,
    `H${fmt(x + radius)}`,
    `A${R} ${R} 0 0 1 ${fmt(x)} ${fmt(y + h - radius)}`,
    `V${fmt(y + radius)}`,
    `A${R} ${R} 0 0 1 ${fmt(x + radius)} ${fmt(y)}`,
    "Z",
  ].join(" ");
}

function ellipsePath(cx: number, cy: number, rx: number, ry: number): string {
  return `M${fmt(cx - rx)} ${fmt(cy)} A${fmt(rx)} ${fmt(ry)} 0 1 0 ${fmt(cx + rx)} ${fmt(cy)} A${fmt(rx)} ${fmt(ry)} 0 1 0 ${fmt(cx - rx)} ${fmt(cy)} Z`;
}

/** 正多边形和星形的顶点，内接于宽高给定的椭圆，第一个顶点朝上。 */
function radialPoints(x: number, y: number, w: number, h: number, count: number, inner: number | null): Array<[number, number]> {
  const points: Array<[number, number]> = [];
  const steps = inner === null ? count : count * 2;
  for (let i = 0; i < steps; i++) {
    const angle = -Math.PI / 2 + (i * 2 * Math.PI) / steps;
    const scale = inner !== null && i % 2 === 1 ? inner : 1;
    points.push([x + w / 2 + (Math.cos(angle) * w * scale) / 2, y + h / 2 + (Math.sin(angle) * h * scale) / 2]);
  }
  return points;
}

/** 云的轮廓，在 100×100 的格子里画好，再按实际大小缩放。 */
const CLOUD = [
  [26, 86], [12, 86, 2, 74, 6, 62], [0, 50, 10, 37, 24, 39], [24, 22, 44, 12, 57, 22],
  [66, 8, 88, 12, 90, 32], [101, 36, 103, 56, 93, 64], [100, 79, 86, 89, 74, 86],
] as const;

/**
 * 形状在 w×h 的方框里的轮廓。strokeWidth 用来往里缩半个线宽，描边就不会被方框切掉。
 * props 里用到 radius（矩形圆角）、sides（多边形边数）、points 和 ratio（星形的角数和内径比例）。
 */
export function shapeGeometry(kind: string, w: number, h: number, strokeWidth: number, props: Record<string, Value>): ShapeGeometry {
  const i = Math.max(0, strokeWidth) / 2;
  const x = i;
  const y = i;
  const W = Math.max(1, w - 2 * i);
  const H = Math.max(1, h - 2 * i);
  const num = (key: string, fallback: number) => (typeof props[key] === "number" ? (props[key] as number) : fallback);
  const details: string[] = [];
  const P = (px: number, py: number): [number, number] => [x + px, y + py];

  switch (kind) {
    case "text":
      return { body: "", details };
    case "ellipse":
      return { body: ellipsePath(x + W / 2, y + H / 2, W / 2, H / 2), details };
    case "diamond":
      return { body: polygonPath([P(W / 2, 0), P(W, H / 2), P(W / 2, H), P(0, H / 2)]), details };
    case "triangle":
      return { body: polygonPath([P(W / 2, 0), P(W, H), P(0, H)]), details };
    case "triangleDown":
      return { body: polygonPath([P(0, 0), P(W, 0), P(W / 2, H)]), details };
    case "polygon": {
      const sides = Math.max(3, Math.min(60, Math.round(num("sides", 5))));
      return { body: polygonPath(radialPoints(x, y, W, H, sides, null)), details };
    }
    case "star": {
      const count = Math.max(3, Math.min(60, Math.round(num("points", 5))));
      const ratio = Math.max(0.05, Math.min(1, num("ratio", 0.45)));
      return { body: polygonPath(radialPoints(x, y, W, H, count, ratio)), details };
    }
    case "plus": {
      const t = Math.min(W, H) / 3;
      const cx = W / 2;
      const cy = H / 2;
      return {
        body: polygonPath([
          P(cx - t / 2, 0), P(cx + t / 2, 0), P(cx + t / 2, cy - t / 2), P(W, cy - t / 2), P(W, cy + t / 2), P(cx + t / 2, cy + t / 2),
          P(cx + t / 2, H), P(cx - t / 2, H), P(cx - t / 2, cy + t / 2), P(0, cy + t / 2), P(0, cy - t / 2), P(cx - t / 2, cy - t / 2),
        ]),
        details,
      };
    }
    case "arrowRight":
    case "arrowLeft": {
      const head = Math.min(W * 0.45, H);
      const pts: Array<[number, number]> = [
        [0, H * 0.25], [W - head, H * 0.25], [W - head, 0], [W, H / 2], [W - head, H], [W - head, H * 0.75], [0, H * 0.75],
      ];
      const flip = kind === "arrowLeft";
      return { body: polygonPath(pts.map(([px, py]) => P(flip ? W - px : px, py))), details };
    }
    case "arrowBoth": {
      const head = Math.min(W * 0.3, H);
      return {
        body: polygonPath([
          P(0, H / 2), P(head, 0), P(head, H * 0.25), P(W - head, H * 0.25), P(W - head, 0), P(W, H / 2),
          P(W - head, H), P(W - head, H * 0.75), P(head, H * 0.75), P(head, H),
        ]),
        details,
      };
    }
    case "chevron": {
      const d = Math.min(W / 3, H / 2);
      return { body: polygonPath([P(0, 0), P(W - d, 0), P(W, H / 2), P(W - d, H), P(0, H), P(d, H / 2)]), details };
    }
    case "speechBubble": {
      const bodyH = H * 0.8;
      const r = Math.min(12, W / 4, bodyH / 4);
      const tailX = Math.min(W * 0.2, W - r - 4);
      const R = fmt(r);
      return {
        body: [
          `M${fmt(x + r)} ${fmt(y)}`, `H${fmt(x + W - r)}`, `A${R} ${R} 0 0 1 ${fmt(x + W)} ${fmt(y + r)}`,
          `V${fmt(y + bodyH - r)}`, `A${R} ${R} 0 0 1 ${fmt(x + W - r)} ${fmt(y + bodyH)}`,
          `H${fmt(x + tailX + W * 0.12)}`, `L${fmt(x + tailX)} ${fmt(y + H)}`, `L${fmt(x + tailX + W * 0.02)} ${fmt(y + bodyH)}`,
          `H${fmt(x + r)}`, `A${R} ${R} 0 0 1 ${fmt(x)} ${fmt(y + bodyH - r)}`, `V${fmt(y + r)}`,
          `A${R} ${R} 0 0 1 ${fmt(x + r)} ${fmt(y)}`, "Z",
        ].join(" "),
        details,
      };
    }
    case "cloud": {
      const sx = W / 100;
      const sy = H / 100;
      const parts = CLOUD.map((seg, index) => {
        if (index === 0) return `M${fmt(x + seg[0] * sx)} ${fmt(y + seg[1] * sy)}`;
        const c = seg as readonly number[];
        return `C${fmt(x + c[0] * sx)} ${fmt(y + c[1] * sy)} ${fmt(x + c[2] * sx)} ${fmt(y + c[3] * sy)} ${fmt(x + c[4] * sx)} ${fmt(y + c[5] * sy)}`;
      });
      return { body: parts.join(" ") + " Z", details };
    }
    case "hexagon": {
      const d = Math.min(W / 4, H / 2);
      return { body: polygonPath([P(d, 0), P(W - d, 0), P(W, H / 2), P(W - d, H), P(d, H), P(0, H / 2)]), details };
    }
    case "parallelogram":
    case "parallelogramLeft": {
      const s = Math.min(W * 0.25, H * 0.6);
      const pts: Array<[number, number]> = [[s, 0], [W, 0], [W - s, H], [0, H]];
      const flip = kind === "parallelogramLeft";
      return { body: polygonPath(pts.map(([px, py]) => P(flip ? W - px : px, py))), details };
    }
    case "trapezoid": {
      const s = Math.min(W * 0.2, H * 0.6);
      return { body: polygonPath([P(s, 0), P(W - s, 0), P(W, H), P(0, H)]), details };
    }
    case "manualInput":
      return { body: polygonPath([P(0, H * 0.25), P(W, 0), P(W, H), P(0, H)]), details };
    case "cylinder": {
      const rx = W / 2;
      const ry = Math.min(H * 0.15, W * 0.25);
      details.push(`M${fmt(x)} ${fmt(y + ry)} A${fmt(rx)} ${fmt(ry)} 0 0 0 ${fmt(x + W)} ${fmt(y + ry)}`);
      return {
        body: `M${fmt(x)} ${fmt(y + ry)} A${fmt(rx)} ${fmt(ry)} 0 0 1 ${fmt(x + W)} ${fmt(y + ry)} V${fmt(y + H - ry)} A${fmt(rx)} ${fmt(ry)} 0 0 1 ${fmt(x)} ${fmt(y + H - ry)} Z`,
        details,
      };
    }
    case "cylinderHorizontal": {
      const ry = H / 2;
      const rx = Math.min(W * 0.15, H * 0.25);
      details.push(`M${fmt(x + W - rx)} ${fmt(y)} A${fmt(rx)} ${fmt(ry)} 0 0 0 ${fmt(x + W - rx)} ${fmt(y + H)}`);
      return {
        body: `M${fmt(x + rx)} ${fmt(y)} H${fmt(x + W - rx)} A${fmt(rx)} ${fmt(ry)} 0 0 1 ${fmt(x + W - rx)} ${fmt(y + H)} H${fmt(x + rx)} A${fmt(rx)} ${fmt(ry)} 0 0 1 ${fmt(x + rx)} ${fmt(y)} Z`,
        details,
      };
    }
    case "document": {
      const a = H * 0.1;
      return {
        body: `M${fmt(x)} ${fmt(y)} H${fmt(x + W)} V${fmt(y + H - a)} Q${fmt(x + W * 0.75)} ${fmt(y + H - 3 * a)} ${fmt(x + W * 0.5)} ${fmt(y + H - a)} T${fmt(x)} ${fmt(y + H - a)} Z`,
        details,
      };
    }
    case "multiDocument": {
      const o = Math.min(W, H) * 0.07;
      const a = H * 0.08;
      const fw = W - 2 * o;
      const top = y + 2 * o;
      details.push(
        `M${fmt(x + o)} ${fmt(top)} V${fmt(y + o)} H${fmt(x + W - o)} V${fmt(y + H - 2 * o - a)}`,
        `M${fmt(x + 2 * o)} ${fmt(y + o)} V${fmt(y)} H${fmt(x + W)} V${fmt(y + H - 3 * o - a)} H${fmt(x + W - o)}`,
      );
      return {
        body: `M${fmt(x)} ${fmt(top)} H${fmt(x + fw)} V${fmt(y + H - a)} Q${fmt(x + fw * 0.75)} ${fmt(y + H - 3 * a)} ${fmt(x + fw * 0.5)} ${fmt(y + H - a)} T${fmt(x)} ${fmt(y + H - a)} Z`,
        details,
      };
    }
    case "predefinedProcess": {
      const d = Math.min(W * 0.1, 16);
      details.push(`M${fmt(x + d)} ${fmt(y)} V${fmt(y + H)}`, `M${fmt(x + W - d)} ${fmt(y)} V${fmt(y + H)}`);
      return { body: roundedRect(x, y, W, H, 0), details };
    }
    case "internalStorage": {
      const dx = Math.min(W * 0.15, 20);
      const dy = Math.min(H * 0.2, 20);
      details.push(`M${fmt(x + dx)} ${fmt(y)} V${fmt(y + H)}`, `M${fmt(x)} ${fmt(y + dy)} H${fmt(x + W)}`);
      return { body: roundedRect(x, y, W, H, 0), details };
    }
    case "file": {
      const f = Math.min(W, H) * 0.22;
      details.push(`M${fmt(x + W - f)} ${fmt(y)} V${fmt(y + f)} H${fmt(x + W)}`);
      return { body: polygonPath([P(0, 0), P(W - f, 0), P(W, f), P(W, H), P(0, H)]), details };
    }
    case "or": {
      details.push(`M${fmt(x + W / 2)} ${fmt(y)} V${fmt(y + H)}`, `M${fmt(x)} ${fmt(y + H / 2)} H${fmt(x + W)}`);
      return { body: ellipsePath(x + W / 2, y + H / 2, W / 2, H / 2), details };
    }
    case "summingJunction": {
      const c = Math.SQRT1_2;
      const cx = x + W / 2;
      const cy = y + H / 2;
      const ax = (W / 2) * c;
      const ay = (H / 2) * c;
      details.push(`M${fmt(cx - ax)} ${fmt(cy - ay)} L${fmt(cx + ax)} ${fmt(cy + ay)}`, `M${fmt(cx + ax)} ${fmt(cy - ay)} L${fmt(cx - ax)} ${fmt(cy + ay)}`);
      return { body: ellipsePath(cx, cy, W / 2, H / 2), details };
    }
    default:
      return { body: roundedRect(x, y, W, H, num("radius", 0)), details };
  }
}

/** 形状里文字区域的位置（相对于形状的左上角）。 */
export function shapeTextBox(kind: string, w: number, h: number): { x: number; y: number; width: number; height: number } {
  const [ix, iy] = shapeKind(kind).textInset ?? [0.06, 0.06];
  const padX = kind === "text" ? 0 : Math.max(6, w * ix);
  const padY = kind === "text" ? 0 : Math.max(4, h * iy);
  return { x: padX, y: padY, width: Math.max(0, w - 2 * padX), height: Math.max(0, h - 2 * padY) };
}

/** 虚线和点线的 stroke-dasharray。 */
export function dashArray(style: Value, width: number): string {
  const w = Math.max(1, width);
  if (style === "dashed") return `${fmt(w * 4)} ${fmt(w * 3)}`;
  if (style === "dotted") return `0 ${fmt(w * 2.2)}`;
  return "";
}

// ---------- 线条 ----------

export type Side = "top" | "right" | "bottom" | "left";
export const SIDES: Side[] = ["top", "right", "bottom", "left"];

export interface Point {
  x: number;
  y: number;
}

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

const SIDE_DIRECTION: Record<Side, Point> = {
  top: { x: 0, y: -1 },
  right: { x: 1, y: 0 },
  bottom: { x: 0, y: 1 },
  left: { x: -1, y: 0 },
};

export function sideAnchor(rect: Rect, side: Side): Point {
  if (side === "top") return { x: rect.x + rect.width / 2, y: rect.y };
  if (side === "bottom") return { x: rect.x + rect.width / 2, y: rect.y + rect.height };
  if (side === "left") return { x: rect.x, y: rect.y + rect.height / 2 };
  return { x: rect.x + rect.width, y: rect.y + rect.height / 2 };
}

/** 线挂在一个方框上、另一头在 toward 时，自动选哪一边：朝着另一头的那一边。 */
export function autoSide(rect: Rect, toward: Point): Side {
  const dx = (toward.x - (rect.x + rect.width / 2)) / Math.max(1, rect.width);
  const dy = (toward.y - (rect.y + rect.height / 2)) / Math.max(1, rect.height);
  if (Math.abs(dx) >= Math.abs(dy)) return dx >= 0 ? "right" : "left";
  return dy >= 0 ? "bottom" : "top";
}

/** 线的一端：位置，以及线从这里出发（或者到达这里）时的方向。 */
export interface Endpoint {
  point: Point;
  /** 离开端点、朝线的内部走的方向；没有挂在方框上时为 null */
  direction: Point | null;
}

export interface LineGeometry {
  /** SVG 路径 */
  path: string;
  /** 折线和直线的顶点；曲线是起点、两个控制点和终点 */
  points: Point[];
  /** 起点和终点处线的走向（指向端头外侧），用来画端头 */
  startAngle: number;
  endAngle: number;
}

function dominantDirection(from: Point, to: Point): Point {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  if (Math.abs(dx) >= Math.abs(dy)) return { x: dx >= 0 ? 1 : -1, y: 0 };
  return { x: 0, y: dy >= 0 ? 1 : -1 };
}

function simplify(points: Point[]): Point[] {
  const result: Point[] = [];
  for (const p of points) {
    const last = result[result.length - 1];
    if (last && Math.abs(last.x - p.x) < 0.5 && Math.abs(last.y - p.y) < 0.5) continue;
    result.push(p);
  }
  // 去掉三点共线时中间的那一点
  for (let k = result.length - 2; k > 0; k--) {
    const a = result[k - 1];
    const b = result[k];
    const c = result[k + 1];
    if ((Math.abs(a.x - b.x) < 0.5 && Math.abs(b.x - c.x) < 0.5) || (Math.abs(a.y - b.y) < 0.5 && Math.abs(b.y - c.y) < 0.5)) {
      result.splice(k, 1);
    }
  }
  return result;
}

/** 折线：只走水平和竖直的线段，从起点所在的那一边出去，从终点所在的那一边进来。 */
function elbowPoints(start: Endpoint, end: Endpoint): Point[] {
  const GAP = 20;
  const S = start.point;
  const E = end.point;
  const ds = start.direction ?? dominantDirection(S, E);
  const de = end.direction ?? dominantDirection(E, S);
  const P1 = { x: S.x + ds.x * GAP, y: S.y + ds.y * GAP };
  const P2 = { x: E.x + de.x * GAP, y: E.y + de.y * GAP };
  const startHorizontal = ds.y === 0;
  const endHorizontal = de.y === 0;
  let middle: Point[];
  if (startHorizontal && endHorizontal) {
    // 两头都是横着出去的：在中间竖着拐一下
    let mx = (S.x + E.x) / 2;
    if ((ds.x > 0 && mx < P1.x) || (ds.x < 0 && mx > P1.x)) mx = P1.x;
    if ((de.x > 0 && mx < P2.x) || (de.x < 0 && mx > P2.x)) mx = P2.x;
    middle = [{ x: mx, y: S.y }, { x: mx, y: E.y }];
  } else if (!startHorizontal && !endHorizontal) {
    let my = (S.y + E.y) / 2;
    if ((ds.y > 0 && my < P1.y) || (ds.y < 0 && my > P1.y)) my = P1.y;
    if ((de.y > 0 && my < P2.y) || (de.y < 0 && my > P2.y)) my = P2.y;
    middle = [{ x: S.x, y: my }, { x: E.x, y: my }];
  } else if (startHorizontal) {
    // 横着出去、竖着进来：能一次拐过去就一次，不能就先各自伸出一段
    const direct = { x: E.x, y: S.y };
    const ok = (direct.x - S.x) * ds.x >= GAP && (direct.y - E.y) * de.y >= GAP;
    middle = ok ? [direct] : [P1, { x: P1.x, y: P2.y }, P2];
  } else {
    const direct = { x: S.x, y: E.y };
    const ok = (direct.y - S.y) * ds.y >= GAP && (direct.x - E.x) * de.x >= GAP;
    middle = ok ? [direct] : [P1, { x: P2.x, y: P1.y }, P2];
  }
  return simplify([S, ...middle, E]);
}

/** 折线手动调整过的拐点，写在 props.bends 里：“x,y;x,y”，相对于线的父节点。 */
export function parseBends(text: Value | undefined): Point[] {
  if (typeof text !== "string" || !text.trim()) return [];
  const points: Point[] = [];
  for (const pair of text.split(";")) {
    const [x, y] = pair.split(",").map(Number);
    if (Number.isFinite(x) && Number.isFinite(y)) points.push({ x, y });
  }
  return points;
}

export function formatBends(points: Point[]): string {
  return points.map((p) => `${Math.round(p.x)},${Math.round(p.y)}`).join(";");
}

/**
 * 用手动调整过的拐点走折线。两头可能已经移动过，所以先把挨着两头的那个拐点挪一下，
 * 让第一段和最后一段仍然是水平或竖直的；中间各段保持原样。
 */
function elbowWithBends(start: Endpoint, end: Endpoint, bends: Point[]): Point[] {
  const S = start.point;
  const E = end.point;
  const inner = bends.map((p) => ({ ...p }));
  // 挨着一头的拐点：这一头是横着出去的，拐点就和它同高；竖着出去的，就和它对齐 x
  const align = (p: Point, from: Point, direction: Point | null, preferHorizontal: boolean) => {
    const horizontal = direction ? direction.y === 0 : preferHorizontal;
    if (horizontal) p.y = from.y;
    else p.x = from.x;
  };
  const first = inner[0];
  const last = inner[inner.length - 1];
  const nearlyHorizontal = (p: Point, from: Point) => Math.abs(p.y - from.y) <= Math.abs(p.x - from.x);
  if (inner.length === 1) {
    // 只有一个拐点：它同时连着两头，只能是两头横竖交叉的那个角
    const horizontalFromStart = start.direction ? start.direction.y === 0 : nearlyHorizontal(first, S);
    inner[0] = horizontalFromStart ? { x: E.x, y: S.y } : { x: S.x, y: E.y };
  } else {
    align(first, S, start.direction, nearlyHorizontal(first, S));
    align(last, E, end.direction, nearlyHorizontal(last, E));
  }
  return simplify([S, ...inner, E]);
}

function angleOf(from: Point, to: Point): number {
  return Math.atan2(to.y - from.y, to.x - from.x);
}

/** 曲线通过一组拐点（waypoints = [S, ...bends, E]）：每一段是一条三次贝塞尔曲线，返回 [S, C1, C2, P1, C3, C4, P2, ...]。 */
function curvedPoints(start: Endpoint, end: Endpoint, bends: Point[]): Point[] {
  const S = start.point;
  const E = end.point;
  if (bends.length === 0) {
    const distance = Math.hypot(E.x - S.x, E.y - S.y);
    const k = Math.max(30, distance * 0.4);
    const ds = start.direction ?? dominantDirection(S, E);
    const de = end.direction ?? dominantDirection(E, S);
    const C1 = { x: S.x + ds.x * k, y: S.y + ds.y * k };
    const C2 = { x: E.x + de.x * k, y: E.y + de.y * k };
    return [S, C1, C2, E];
  }
  const waypoints = [S, ...bends, E];
  const tangents: Point[] = waypoints.map((p, i) => {
    if (i === 0) {
      if (start.direction) return start.direction;
      const next = waypoints[1];
      const d = Math.hypot(next.x - p.x, next.y - p.y) || 1;
      return { x: (next.x - p.x) / d, y: (next.y - p.y) / d };
    }
    if (i === waypoints.length - 1) {
      if (end.direction) return { x: -end.direction.x, y: -end.direction.y };
      const prev = waypoints[i - 1];
      const d = Math.hypot(p.x - prev.x, p.y - prev.y) || 1;
      return { x: (p.x - prev.x) / d, y: (p.y - prev.y) / d };
    }
    const prev = waypoints[i - 1];
    const next = waypoints[i + 1];
    const d = Math.hypot(next.x - prev.x, next.y - prev.y) || 1;
    return { x: (next.x - prev.x) / d, y: (next.y - prev.y) / d };
  });
  const result: Point[] = [S];
  for (let i = 0; i < waypoints.length - 1; i++) {
    const a = waypoints[i];
    const b = waypoints[i + 1];
    const dist = Math.hypot(b.x - a.x, b.y - a.y);
    const k = Math.max(12, dist * 0.35);
    const ta = tangents[i];
    const tb = tangents[i + 1];
    const c1 = { x: a.x + ta.x * k, y: a.y + ta.y * k };
    const c2 = { x: b.x - tb.x * k, y: b.y - tb.y * k };
    result.push(c1, c2, b);
  }
  return result;
}

/** 线的路径。route 为 straight、elbow 或 curved；都可以带手动调整过的拐点（绝对坐标）。 */
export function lineGeometry(route: Value, start: Endpoint, end: Endpoint, bends: Point[] = []): LineGeometry {
  const S = start.point;
  const E = end.point;
  if (route === "curved") {
    const pts = curvedPoints(start, end, bends);
    return {
      path: pathFromPoints("curved", pts),
      points: pts,
      startAngle: angleOf(pts[1], pts[0]),
      endAngle: angleOf(pts[pts.length - 2], pts[pts.length - 1]),
    };
  }
  const points = route === "elbow" ? (bends.length > 0 ? elbowWithBends(start, end, bends) : elbowPoints(start, end)) : [S, ...bends, E];
  const safe = points.length >= 2 ? points : [S, { x: S.x + 1, y: S.y }];
  return {
    path: safe.map((p, i) => `${i === 0 ? "M" : "L"}${fmt(p.x)} ${fmt(p.y)}`).join(" "),
    points: safe,
    startAngle: angleOf(safe[1], safe[0]),
    endAngle: angleOf(safe[safe.length - 2], safe[safe.length - 1]),
  };
}

/** 端头的种类：没有、线形箭头、实心箭头、圆点、菱形、短横。 */
export const CAPS: Array<{ id: string; label: string }> = [
  { id: "none", label: "无" },
  { id: "arrow", label: "线形箭头" },
  { id: "triangle", label: "实心箭头" },
  { id: "circle", label: "圆点" },
  { id: "diamond", label: "菱形" },
  { id: "bar", label: "短横" },
];

/** 端头有多长：线要在这里缩回去，免得线头从实心端头里戳出来。 */
export function capInset(cap: Value, strokeWidth: number): number {
  const size = 5 + strokeWidth * 2.5;
  if (cap === "triangle") return size * 0.85;
  if (cap === "circle") return size * 0.6;
  if (cap === "diamond") return size * 1.1;
  return 0;
}

/**
 * 端头的图形。point 是线的端点，angle 是线在这里的走向（指向外侧）。
 * 返回 filled 为真的要填色，否则只描边。
 */
export function capShape(cap: Value, point: Point, angle: number, strokeWidth: number): { path: string; filled: boolean } | null {
  const size = 5 + strokeWidth * 2.5;
  const ux = Math.cos(angle);
  const uy = Math.sin(angle);
  // 往回走 along，往旁边走 across
  const at = (along: number, across: number): string => `${fmt(point.x - ux * along - uy * across)} ${fmt(point.y - uy * along + ux * across)}`;
  switch (cap) {
    case "arrow":
      return { path: `M${at(size, size * 0.6)} L${at(0, 0)} L${at(size, -size * 0.6)}`, filled: false };
    case "triangle":
      return { path: `M${at(0, 0)} L${at(size, size * 0.55)} L${at(size, -size * 0.55)} Z`, filled: true };
    case "circle": {
      const r = size * 0.35;
      const c = { x: point.x - ux * r, y: point.y - uy * r };
      return { path: ellipsePath(c.x, c.y, r, r), filled: true };
    }
    case "diamond":
      return { path: `M${at(0, 0)} L${at(size * 0.55, size * 0.4)} L${at(size * 1.1, 0)} L${at(size * 0.55, -size * 0.4)} Z`, filled: true };
    case "bar":
      return { path: `M${at(0, size * 0.6)} L${at(0, -size * 0.6)}`, filled: false };
    default:
      return null;
  }
}

/** 把线的一端沿着线往里缩 distance，给实心端头腾地方。只处理直线和折线的端点，曲线也近似用这一段的方向。 */
export function shortenEnd(points: Point[], fromStart: boolean, distance: number): Point[] {
  if (distance <= 0 || points.length < 2) return points;
  const copy = points.slice();
  const index = fromStart ? 0 : copy.length - 1;
  const neighbor = copy[fromStart ? 1 : copy.length - 2];
  const p = copy[index];
  const length = Math.hypot(p.x - neighbor.x, p.y - neighbor.y);
  if (length < 1) return points;
  const t = Math.min(distance, length - 0.5) / length;
  copy[index] = { x: p.x + (neighbor.x - p.x) * t, y: p.y + (neighbor.y - p.y) * t };
  return copy;
}

/** 曲线或折线的路径文字，按点重新拼。 */
export function pathFromPoints(route: Value, points: Point[]): string {
  if (route === "curved" && points.length >= 4 && (points.length - 1) % 3 === 0) {
    let d = `M${fmt(points[0].x)} ${fmt(points[0].y)}`;
    for (let i = 1; i + 2 < points.length; i += 3) {
      const c1 = points[i];
      const c2 = points[i + 1];
      const p = points[i + 2];
      d += ` C${fmt(c1.x)} ${fmt(c1.y)} ${fmt(c2.x)} ${fmt(c2.y)} ${fmt(p.x)} ${fmt(p.y)}`;
    }
    return d;
  }
  return points.map((p, i) => `${i === 0 ? "M" : "L"}${fmt(p.x)} ${fmt(p.y)}`).join(" ");
}

/** 直线和曲线每一段的中点，用来放置“拖动添加拐点”的把手。 */
export function lineSegmentMidpoints(route: Value, start: Endpoint, end: Endpoint, bends: Point[] = []): Point[] {
  if (route === "curved") {
    const pts = curvedPoints(start, end, bends);
    const mids: Point[] = [];
    for (let i = 0; i + 3 < pts.length; i += 3) {
      const p0 = pts[i];
      const c1 = pts[i + 1];
      const c2 = pts[i + 2];
      const p1 = pts[i + 3];
      mids.push({
        x: 0.125 * (p0.x + p1.x) + 0.375 * (c1.x + c2.x),
        y: 0.125 * (p0.y + p1.y) + 0.375 * (c1.y + c2.y),
      });
    }
    return mids;
  }
  const waypoints = [start.point, ...bends, end.point];
  const mids: Point[] = [];
  for (let i = 0; i + 1 < waypoints.length; i++) {
    mids.push({
      x: (waypoints[i].x + waypoints[i + 1].x) / 2,
      y: (waypoints[i].y + waypoints[i + 1].y) / 2,
    });
  }
  return mids;
}

export function insertLineBend(bends: Point[], index: number, point: Point): Point[] {
  const next = bends.map((p) => ({ ...p }));
  const clamped = Math.max(0, Math.min(next.length, index));
  next.splice(clamped, 0, { x: point.x, y: point.y });
  return next;
}

export function updateLineBend(bends: Point[], index: number, point: Point): Point[] {
  return bends.map((p, i) => (i === index ? { x: point.x, y: point.y } : { ...p }));
}

export function removeLineBend(bends: Point[], index: number): Point[] {
  return bends.filter((_, i) => i !== index).map((p) => ({ ...p }));
}

export function sideDirection(side: Side): Point {
  return SIDE_DIRECTION[side];
}

/**
 * 拖动折线的第 index 段（从 points[index] 到 points[index + 1]），沿着和它垂直的方向挪 delta。
 * 返回新的拐点（不含两头）。挨着两头的那一段不能直接平移（两头是固定的），
 * 就在这一段中间折一下，多出一小段来，和 FigJam 拖连接线头尾那一段时的做法一样。
 */
export function dragElbowSegment(points: Point[], index: number, delta: number): Point[] {
  const a = points[index];
  const b = points[index + 1];
  if (!a || !b) return points.slice(1, -1);
  const horizontal = Math.abs(a.y - b.y) < 0.5;
  const shift = (p: Point): Point => (horizontal ? { x: p.x, y: p.y + delta } : { x: p.x + delta, y: p.y });
  const last = points.length - 1;
  const touchesStart = index === 0;
  const touchesEnd = index + 1 === last;
  const result = points.map((p) => ({ ...p }));
  if (!touchesStart && !touchesEnd) {
    result[index] = shift(a);
    result[index + 1] = shift(b);
    return result.slice(1, -1);
  }
  const at = (t: number): Point => ({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t });
  if (touchesStart && touchesEnd) {
    // 只有一段：在三分之一和三分之二处各折一下，中间那段平移
    const m1 = at(1 / 3);
    const m2 = at(2 / 3);
    return [m1, shift(m1), shift(m2), m2];
  }
  if (touchesStart) {
    const m = at(0.5);
    return [m, shift(m), shift(b), ...result.slice(index + 2, -1)];
  }
  const m = at(0.5);
  return [...result.slice(1, index), shift(a), shift(m), m];
}

// ---------- 预设 ----------

/** 配色：填充、描边和文字的颜色一起换。参照 FigJam 的调色板。 */
export interface ColorStyle {
  id: string;
  label: string;
  fill: string;
  stroke: string;
  textColor: string;
}

export const COLOR_STYLES: ColorStyle[] = [
  { id: "white", label: "白", fill: "#ffffff", stroke: "#1f2430", textColor: "#1f2430" },
  { id: "gray", label: "灰", fill: "#f1f2f4", stroke: "#8c919a", textColor: "#1f2430" },
  { id: "red", label: "红", fill: "#fde8e8", stroke: "#e5484d", textColor: "#7a1c1f" },
  { id: "orange", label: "橙", fill: "#ffeedd", stroke: "#f76b15", textColor: "#7a3608" },
  { id: "yellow", label: "黄", fill: "#fff6bf", stroke: "#d29b00", textColor: "#5c4300" },
  { id: "green", label: "绿", fill: "#e2f5e6", stroke: "#2f9e44", textColor: "#16461f" },
  { id: "teal", label: "青", fill: "#dcf4f1", stroke: "#0c9488", textColor: "#0b4741" },
  { id: "blue", label: "蓝", fill: "#e2ecff", stroke: "#2563eb", textColor: "#13317a" },
  { id: "violet", label: "紫", fill: "#eee6ff", stroke: "#7c3aed", textColor: "#3b1a78" },
  { id: "pink", label: "粉", fill: "#fde6f2", stroke: "#d6409f", textColor: "#6e1650" },
  { id: "dark", label: "深色", fill: "#2b303b", stroke: "#2b303b", textColor: "#ffffff" },
  { id: "outline", label: "只描边", fill: "none", stroke: "#1f2430", textColor: "#1f2430" },
];

export const STROKE_STYLES: Array<{ id: string; label: string }> = [
  { id: "solid", label: "实线" },
  { id: "dashed", label: "虚线" },
  { id: "dotted", label: "点线" },
];

export const STROKE_WIDTHS: Array<{ value: number; label: string }> = [
  { value: 1, label: "细" },
  { value: 2, label: "中" },
  { value: 4, label: "粗" },
];

export const ROUTES: Array<{ id: string; label: string }> = [
  { id: "straight", label: "直线" },
  { id: "elbow", label: "折线" },
  { id: "curved", label: "曲线" },
];

export const FONT_SIZES: Array<{ value: number; label: string }> = [
  { value: 12, label: "小" },
  { value: 14, label: "中" },
  { value: 18, label: "大" },
  { value: 28, label: "特大" },
];

/** 一个预设：素材栏里的一项，或者画图工具画出来的东西。 */
export interface ShapePreset {
  id: string;
  label: string;
  group: string;
  type: "Shape" | "Line";
  /** 新节点 ID 的前缀 */
  idBase: string;
  width: number;
  height: number;
  props: Record<string, Value>;
}

function shapePreset(id: string, label: string, group: string, props: Record<string, Value>, width = 120, height = 80, idBase = id): ShapePreset {
  return { id, label, group, type: "Shape", idBase, width, height, props };
}

function linePreset(id: string, label: string, props: Record<string, Value>, idBase = "line"): ShapePreset {
  return { id, label, group: "线条", type: "Line", idBase, width: 160, height: 0, props };
}

export const SHAPE_PRESETS: ShapePreset[] = [
  shapePreset("rectangle", "矩形", "基础图形", { shape: "rectangle" }, 120, 80, "rect"),
  shapePreset("roundedRectangle", "圆角矩形", "基础图形", { shape: "rectangle", radius: 16 }, 120, 80, "rect"),
  shapePreset("ellipse", "椭圆", "基础图形", { shape: "ellipse" }, 120, 80),
  shapePreset("circle", "圆", "基础图形", { shape: "ellipse" }, 80, 80, "circle"),
  shapePreset("diamond", "菱形", "基础图形", { shape: "diamond" }, 120, 90),
  shapePreset("triangle", "三角形", "基础图形", { shape: "triangle" }, 100, 90),
  shapePreset("triangleDown", "倒三角", "基础图形", { shape: "triangleDown" }, 100, 90, "triangle"),
  shapePreset("pentagon", "五边形", "基础图形", { shape: "polygon", sides: 5 }, 100, 95, "polygon"),
  shapePreset("octagon", "八边形", "基础图形", { shape: "polygon", sides: 8 }, 100, 100, "polygon"),
  shapePreset("star", "星形", "基础图形", { shape: "star", points: 5, ratio: 0.45 }, 100, 95),
  shapePreset("plus", "十字", "基础图形", { shape: "plus" }, 90, 90),
  shapePreset("arrowRight", "右箭头", "基础图形", { shape: "arrowRight" }, 140, 70, "arrowShape"),
  shapePreset("arrowLeft", "左箭头", "基础图形", { shape: "arrowLeft" }, 140, 70, "arrowShape"),
  shapePreset("arrowBoth", "双向箭头", "基础图形", { shape: "arrowBoth" }, 160, 70, "arrowShape"),
  shapePreset("chevron", "折角箭头", "基础图形", { shape: "chevron" }, 120, 70),
  shapePreset("speechBubble", "对话气泡", "基础图形", { shape: "speechBubble" }, 140, 90, "bubble"),
  shapePreset("cloud", "云", "基础图形", { shape: "cloud" }, 150, 100),
  shapePreset("text", "文字", "基础图形", { shape: "text", fill: "none", stroke: "none", text: "文字", textAlign: "left", fontSize: 16 }, 120, 28),
  shapePreset("terminal", "开始 / 结束", "流程图", { shape: "rectangle", radius: 999 }, 140, 56, "terminal"),
  shapePreset("process", "处理", "流程图", { shape: "rectangle" }, 140, 70, "process"),
  shapePreset("decision", "判断", "流程图", { shape: "diamond" }, 140, 100, "decision"),
  shapePreset("inputOutput", "输入 / 输出", "流程图", { shape: "parallelogram" }, 150, 70, "io"),
  shapePreset("predefinedProcess", "预定义处理", "流程图", { shape: "predefinedProcess" }, 150, 70, "subprocess"),
  shapePreset("database", "数据库", "流程图", { shape: "cylinder" }, 100, 110, "database"),
  shapePreset("document", "文档", "流程图", { shape: "document" }, 130, 90),
  shapePreset("multiDocument", "多文档", "流程图", { shape: "multiDocument" }, 140, 100, "documents"),
  shapePreset("manualInput", "手动输入", "流程图", { shape: "manualInput" }, 140, 80, "input"),
  shapePreset("preparation", "准备", "流程图", { shape: "hexagon" }, 150, 70, "preparation"),
  shapePreset("manualOperation", "手动操作", "流程图", { shape: "trapezoid" }, 150, 70, "manual"),
  shapePreset("internalStorage", "内部存储", "流程图", { shape: "internalStorage" }, 120, 90, "storage"),
  shapePreset("directAccess", "直接访问存储", "流程图", { shape: "cylinderHorizontal" }, 150, 70, "storage"),
  shapePreset("file", "文件", "流程图", { shape: "file" }, 100, 120),
  shapePreset("or", "或", "流程图", { shape: "or" }, 60, 60),
  shapePreset("summingJunction", "求和", "流程图", { shape: "summingJunction" }, 60, 60, "sum"),
  linePreset("line", "直线", { route: "straight", startCap: "none", endCap: "none" }),
  linePreset("arrow", "箭头", { route: "straight", startCap: "none", endCap: "triangle" }, "arrow"),
  linePreset("doubleArrow", "双向箭头", { route: "straight", startCap: "triangle", endCap: "triangle" }, "arrow"),
  linePreset("elbowConnector", "折线连接", { route: "elbow", startCap: "none", endCap: "triangle" }, "connector"),
  linePreset("curvedConnector", "曲线连接", { route: "curved", startCap: "none", endCap: "triangle" }, "connector"),
  linePreset("dashedArrow", "虚线箭头", { route: "straight", startCap: "none", endCap: "arrow", strokeStyle: "dashed" }, "arrow"),
];

export function presetById(id: string): ShapePreset | undefined {
  return SHAPE_PRESETS.find((preset) => preset.id === id);
}

/** 预设里只有样式的那几项：存为预设、记住上次用的样式时只取这些。 */
export const SHAPE_STYLE_KEYS = ["fill", "stroke", "strokeWidth", "strokeStyle", "radius", "opacity", "textColor", "fontSize", "fontWeight", "textAlign", "sides", "points", "ratio"];
export const LINE_STYLE_KEYS = ["route", "stroke", "strokeWidth", "strokeStyle", "startCap", "endCap", "opacity", "textColor", "fontSize"];

// ---------- 纯数学 SVG 路径采样（供命令行与导出 Qt 时不依赖浏览器测量轮廓与中点） ----------

interface PathSeg {
  a: Point;
  b: Point;
  len: number;
}

function vecAngle(ux: number, uy: number, vx: number, vy: number): number {
  const dot = ux * vx + uy * vy;
  const len = Math.hypot(ux, uy) * Math.hypot(vx, vy);
  if (len <= 0) return 0;
  const cos = Math.max(-1, Math.min(1, dot / len));
  const ang = Math.acos(cos);
  return ux * vy - uy * vx < 0 ? -ang : ang;
}

function linearizeSvgPath(d: string): PathSeg[] {
  const tokens = d.match(/[MLHVCSQTAZmlhvcsqtaz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?/g);
  if (!tokens || tokens.length === 0) return [];
  const segs: PathSeg[] = [];
  const addSeg = (x0: number, y0: number, x1: number, y1: number) => {
    const len = Math.hypot(x1 - x0, y1 - y0);
    if (len > 1e-6) segs.push({ a: { x: x0, y: y0 }, b: { x: x1, y: y1 }, len });
  };

  let idx = 0;
  let cmd = "M";
  let curX = 0;
  let curY = 0;
  let startX = 0;
  let startY = 0;
  let prevQx = 0;
  let prevQy = 0;
  let lastCmd = "";

  const hasNum = (): boolean => idx < tokens.length && !/^[A-Za-z]$/.test(tokens[idx]);
  const nextNum = (): number => (idx < tokens.length ? Number(tokens[idx++]) : 0);

  while (idx < tokens.length) {
    if (/^[A-Za-z]$/.test(tokens[idx])) {
      cmd = tokens[idx++];
    }
    const upper = cmd.toUpperCase();
    if (upper === "M") {
      const x = nextNum();
      const y = nextNum();
      curX = startX = x;
      curY = startY = y;
      lastCmd = "M";
      cmd = "L";
      continue;
    }
    if (upper === "L") {
      const x = nextNum();
      const y = nextNum();
      addSeg(curX, curY, x, y);
      curX = x;
      curY = y;
      lastCmd = "L";
      continue;
    }
    if (upper === "H") {
      const x = nextNum();
      addSeg(curX, curY, x, curY);
      curX = x;
      lastCmd = "H";
      continue;
    }
    if (upper === "V") {
      const y = nextNum();
      addSeg(curX, curY, curX, y);
      curY = y;
      lastCmd = "V";
      continue;
    }
    if (upper === "Q") {
      const x1 = nextNum();
      const y1 = nextNum();
      const x = nextNum();
      const y = nextNum();
      const x0 = curX;
      const y0 = curY;
      const steps = 16;
      let px = x0;
      let py = y0;
      for (let s = 1; s <= steps; s++) {
        const t = s / steps;
        const mt = 1 - t;
        const nx = mt * mt * x0 + 2 * mt * t * x1 + t * t * x;
        const ny = mt * mt * y0 + 2 * mt * t * y1 + t * t * y;
        addSeg(px, py, nx, ny);
        px = nx;
        py = ny;
      }
      prevQx = x1;
      prevQy = y1;
      curX = x;
      curY = y;
      lastCmd = "Q";
      continue;
    }
    if (upper === "T") {
      const x = nextNum();
      const y = nextNum();
      const x1 = lastCmd === "Q" || lastCmd === "T" ? 2 * curX - prevQx : curX;
      const y1 = lastCmd === "Q" || lastCmd === "T" ? 2 * curY - prevQy : curY;
      const x0 = curX;
      const y0 = curY;
      const steps = 16;
      let px = x0;
      let py = y0;
      for (let s = 1; s <= steps; s++) {
        const t = s / steps;
        const mt = 1 - t;
        const nx = mt * mt * x0 + 2 * mt * t * x1 + t * t * x;
        const ny = mt * mt * y0 + 2 * mt * t * y1 + t * t * y;
        addSeg(px, py, nx, ny);
        px = nx;
        py = ny;
      }
      prevQx = x1;
      prevQy = y1;
      curX = x;
      curY = y;
      lastCmd = "T";
      continue;
    }
    if (upper === "C") {
      const x1 = nextNum();
      const y1 = nextNum();
      const x2 = nextNum();
      const y2 = nextNum();
      const x = nextNum();
      const y = nextNum();
      const x0 = curX;
      const y0 = curY;
      const steps = 20;
      let px = x0;
      let py = y0;
      for (let s = 1; s <= steps; s++) {
        const t = s / steps;
        const mt = 1 - t;
        const nx = mt * mt * mt * x0 + 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t * x;
        const ny = mt * mt * mt * y0 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y;
        addSeg(px, py, nx, ny);
        px = nx;
        py = ny;
      }
      curX = x;
      curY = y;
      lastCmd = "C";
      continue;
    }
    if (upper === "A") {
      let rx = Math.abs(nextNum());
      let ry = Math.abs(nextNum());
      const phi = (nextNum() * Math.PI) / 180;
      const largeArc = nextNum() !== 0;
      const sweep = nextNum() !== 0;
      const x = nextNum();
      const y = nextNum();
      const x1 = curX;
      const y1 = curY;
      if (Math.hypot(x - x1, y - y1) > 1e-6) {
        if (rx <= 1e-6 || ry <= 1e-6) {
          addSeg(x1, y1, x, y);
        } else {
          const cosPhi = Math.cos(phi);
          const sinPhi = Math.sin(phi);
          const dx2 = (x1 - x) / 2;
          const dy2 = (y1 - y) / 2;
          const x1p = cosPhi * dx2 + sinPhi * dy2;
          const y1p = -sinPhi * dx2 + cosPhi * dy2;
          const lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
          if (lambda > 1) {
            const s = Math.sqrt(lambda);
            rx *= s;
            ry *= s;
          }
          const rx2 = rx * rx;
          const ry2 = ry * ry;
          const x1p2 = x1p * x1p;
          const y1p2 = y1p * y1p;
          const numVal = Math.max(0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
          const denVal = rx2 * y1p2 + ry2 * x1p2;
          const factor = (largeArc === sweep ? -1 : 1) * (denVal > 0 ? Math.sqrt(numVal / denVal) : 0);
          const cxp = factor * ((rx * y1p) / ry);
          const cyp = factor * (-(ry * x1p) / rx);
          const cx = cosPhi * cxp - sinPhi * cyp + (x1 + x) / 2;
          const cy = sinPhi * cxp + cosPhi * cyp + (y1 + y) / 2;
          const theta1 = vecAngle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
          let dTheta = vecAngle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
          if (!sweep && dTheta > 0) dTheta -= Math.PI * 2;
          if (sweep && dTheta < 0) dTheta += Math.PI * 2;
          const steps = Math.max(16, Math.ceil(Math.abs(dTheta) / (Math.PI / 24)));
          let px = x1;
          let py = y1;
          for (let s = 1; s <= steps; s++) {
            const ang = theta1 + (dTheta * s) / steps;
            const nx = s === steps ? x : cx + cosPhi * rx * Math.cos(ang) - sinPhi * ry * Math.sin(ang);
            const ny = s === steps ? y : cy + sinPhi * rx * Math.cos(ang) + cosPhi * ry * Math.sin(ang);
            addSeg(px, py, nx, ny);
            px = nx;
            py = ny;
          }
        }
      }
      curX = x;
      curY = y;
      lastCmd = "A";
      continue;
    }
    if (upper === "Z") {
      addSeg(curX, curY, startX, startY);
      curX = startX;
      curY = startY;
      lastCmd = "Z";
      continue;
    }
    if (hasNum()) nextNum();
  }
  return segs;
}

function pointAtSegLength(segs: PathSeg[], targetDist: number): Point {
  if (segs.length === 0) return { x: 0, y: 0 };
  if (targetDist <= 0) return { ...segs[0].a };
  let acc = 0;
  for (const seg of segs) {
    if (acc + seg.len >= targetDist) {
      const t = seg.len > 0 ? (targetDist - acc) / seg.len : 0;
      return {
        x: seg.a.x + (seg.b.x - seg.a.x) * t,
        y: seg.a.y + (seg.b.y - seg.a.y) * t,
      };
    }
    acc += seg.len;
  }
  return { ...segs[segs.length - 1].b };
}

/** 按弧长均匀采样 SVG 路径上的点（不依赖浏览器 DOM）。 */
export function sampleSvgPath(d: string, count?: number): Point[] {
  const segs = linearizeSvgPath(d);
  if (segs.length === 0) return [];
  const total = segs.reduce((sum, s) => sum + s.len, 0);
  const n = count ?? Math.max(32, Math.min(800, Math.round(total / 2)));
  const points: Point[] = [];
  for (let i = 0; i < n; i++) {
    points.push(pointAtSegLength(segs, (total * i) / n));
  }
  return points;
}

/** 计算 SVG 路径弧长中点坐标（不依赖浏览器 DOM）。 */
export function svgPathMidpoint(d: string, fallback: Point): Point {
  const segs = linearizeSvgPath(d);
  if (segs.length === 0) return fallback;
  const total = segs.reduce((sum, s) => sum + s.len, 0);
  if (total <= 0) return fallback;
  return pointAtSegLength(segs, total / 2);
}
