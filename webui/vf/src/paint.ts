// 把形状和线条画成 SVG。只管画，不管它们在画布上的位置怎么算（那在 main.ts 里）。
import type { Value } from "./expr.ts";
import {
  capInset,
  capShape,
  dashArray,
  lineGeometry,
  pathFromPoints,
  shapeGeometry,
  shapeTextBox,
  shortenEnd,
  svgPathMidpoint,
  type Endpoint,
  type LineGeometry,
  type Point,
  type Rect,
  type ShapePreset,
} from "./shapes.ts";

const SVG_NS = "http://www.w3.org/2000/svg";

function svg<K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string | number>): SVGElementTagNameMap[K] {
  const el = document.createElementNS(SVG_NS, tag);
  for (const [key, value] of Object.entries(attrs)) el.setAttribute(key, String(value));
  return el;
}

const num = (value: Value | undefined, fallback: number): number => (typeof value === "number" && Number.isFinite(value) ? value : fallback);
const str = (value: Value | undefined, fallback: string): string => (typeof value === "string" && value !== "" ? value : fallback);

/** 填充、描边的颜色：none 表示不画。 */
function paint(value: Value | undefined, fallback: string): string {
  const color = str(value, fallback);
  return color === "none" || color === "transparent" ? "none" : color;
}

/**
 * 在 body 里画一个形状，返回用来判断“鼠标是不是点在形状上”的那条路径。
 * 文字画成 HTML，放在形状的文字区域里，这样能换行、能用系统字体。
 */
export function paintShape(body: HTMLElement, width: number, height: number, props: Record<string, Value>): SVGGeometryElement {
  const kind = str(props.shape, "rectangle");
  const stroke = paint(props.stroke, "#1f2430");
  const strokeWidth = stroke === "none" ? 0 : Math.max(0, num(props.strokeWidth, 2));
  const geometry = shapeGeometry(kind, width, height, strokeWidth, props);
  const root = svg("svg", { width, height, class: "vf-shape-svg" });
  const dash = dashArray(props.strokeStyle ?? "solid", strokeWidth);

  let hit: SVGGeometryElement;
  if (geometry.body) {
    const path = svg("path", {
      d: geometry.body,
      fill: paint(props.fill, "#ffffff"),
      stroke,
      "stroke-width": strokeWidth,
      "stroke-linejoin": "round",
      "stroke-linecap": props.strokeStyle === "dotted" ? "round" : "butt",
    });
    if (dash) path.setAttribute("stroke-dasharray", dash);
    path.classList.add("vf-hit");
    root.append(path);
    hit = path;
  } else {
    // 纯文字没有轮廓：放一个透明的方块，让它也能被点中
    hit = svg("rect", { x: 0, y: 0, width, height, fill: "transparent", class: "vf-hit" });
    root.append(hit);
  }
  for (const d of geometry.details) {
    const detail = svg("path", { d, fill: "none", stroke, "stroke-width": strokeWidth, "stroke-linecap": "round" });
    if (dash) detail.setAttribute("stroke-dasharray", dash);
    root.append(detail);
  }

  const box = shapeTextBox(kind, width, height);
  const text = document.createElement("div");
  text.className = "vf-shape-text";
  const align = str(props.textAlign, "center");
  Object.assign(text.style, {
    left: `${box.x}px`,
    top: `${box.y}px`,
    width: `${box.width}px`,
    height: `${box.height}px`,
    color: str(props.textColor, "#1f2430"),
    fontSize: `${num(props.fontSize, 14)}px`,
    fontWeight: str(props.fontWeight, "normal"),
    textAlign: align,
    justifyContent: align === "left" ? "flex-start" : align === "right" ? "flex-end" : "center",
  });
  text.textContent = str(props.text, "");
  body.replaceChildren(root, text);
  return hit;
}

/** 线条的位置：两端，路径，以及包住它的方框（设计窗口里的绝对坐标）。 */
export interface LineLayout {
  start: Endpoint;
  end: Endpoint;
  geometry: LineGeometry;
  bbox: Rect;
}

/** 两端定了以后，算出路径和外框。外框留出端头和文字的余量。 */
export function layoutLine(start: Endpoint, end: Endpoint, props: Record<string, Value>, bends: Point[] = []): LineLayout {
  const geometry = lineGeometry(props.route ?? "straight", start, end, bends);
  const xs = geometry.points.map((p) => p.x);
  const ys = geometry.points.map((p) => p.y);
  const margin = 10 + num(props.strokeWidth, 2) * 3 + (str(props.label, "") ? num(props.fontSize, 12) : 0);
  const x = Math.min(...xs) - margin;
  const y = Math.min(...ys) - margin;
  return {
    start,
    end,
    geometry,
    bbox: { x, y, width: Math.max(...xs) + margin - x, height: Math.max(...ys) + margin - y },
  };
}

/**
 * 在 body 里画一条线。layout 用的是设计窗口的绝对坐标，origin 是 body 左上角的绝对坐标。
 * 返回一条加粗的透明路径，用来判断“鼠标是不是点在线上”。
 */
export function paintLine(body: HTMLElement, layout: LineLayout, origin: { x: number; y: number }, props: Record<string, Value>): SVGGeometryElement {
  const { geometry, bbox } = layout;
  const stroke = paint(props.stroke, "#1f2430");
  const strokeWidth = Math.max(0.5, num(props.strokeWidth, 2));
  const route = props.route ?? "straight";
  const root = svg("svg", { width: bbox.width, height: bbox.height, class: "vf-line-svg" });
  // 路径用绝对坐标写，整组挪到 body 的位置
  const group = svg("g", { transform: `translate(${-origin.x} ${-origin.y})` });
  root.append(group);

  // 实心端头处，线要缩回去一点，免得线头戳出来
  let points = geometry.points;
  points = shortenEnd(points, true, capInset(props.startCap, strokeWidth));
  points = shortenEnd(points, false, capInset(props.endCap, strokeWidth));
  const main = svg("path", {
    d: pathFromPoints(route, points),
    fill: "none",
    stroke,
    "stroke-width": strokeWidth,
    "stroke-linejoin": "round",
    "stroke-linecap": props.strokeStyle === "dotted" ? "round" : "butt",
  });
  const dash = dashArray(props.strokeStyle ?? "solid", strokeWidth);
  if (dash) main.setAttribute("stroke-dasharray", dash);
  group.append(main);

  const first = geometry.points[0];
  const last = geometry.points[geometry.points.length - 1];
  for (const [cap, point, angle] of [
    [props.startCap, first, geometry.startAngle],
    [props.endCap, last, geometry.endAngle],
  ] as const) {
    const shape = capShape(cap ?? "none", point, angle, strokeWidth);
    if (!shape) continue;
    group.append(
      svg("path", {
        d: shape.path,
        fill: shape.filled ? stroke : "none",
        stroke,
        "stroke-width": strokeWidth,
        "stroke-linejoin": "round",
        "stroke-linecap": "round",
      }),
    );
  }

  const hit = svg("path", {
    d: geometry.path,
    fill: "none",
    stroke: "transparent",
    "stroke-width": Math.max(12, strokeWidth + 10),
    class: "vf-hit",
  });
  group.append(hit);

  const children: Element[] = [root];
  const labelText = str(props.label, "");
  if (labelText) {
    // 文字放在路径长度一半的地方
    body.replaceChildren(root);
    void main;
    const mid = svgPathMidpoint(pathFromPoints(route, points), { x: (first.x + last.x) / 2, y: (first.y + last.y) / 2 });
    const label = document.createElement("div");
    label.className = "vf-line-label";
    label.textContent = labelText;
    Object.assign(label.style, {
      left: `${mid.x - origin.x}px`,
      top: `${mid.y - origin.y}px`,
      color: str(props.textColor, "#1f2430"),
      fontSize: `${num(props.fontSize, 12)}px`,
    });
    children.push(label);
  }
  body.replaceChildren(...children);
  return hit;
}

/** 素材栏里预设的小图标。 */
export function presetIcon(preset: ShapePreset, size = 28): SVGSVGElement {
  const props = { ...preset.props };
  if (preset.type === "Line") {
    const root = svg("svg", { width: size, height: size, viewBox: `0 0 ${size} ${size}` });
    // 直线类画成横线；折线和曲线从左下拐到右上
    let start: Endpoint = { point: { x: 3, y: size / 2 }, direction: null };
    let end: Endpoint = { point: { x: size - 3, y: size / 2 }, direction: null };
    if (props.route === "elbow" || props.route === "curved") {
      start = { point: { x: 3, y: size - 7 }, direction: { x: 1, y: 0 } };
      end = { point: { x: size - 7, y: 3 }, direction: { x: 0, y: 1 } };
    }
    const iconProps = { ...props, strokeWidth: 1, label: "" };
    const layout = layoutLine(start, end, iconProps);
    const holder = document.createElement("div");
    paintLine(holder, layout, { x: 0, y: 0 }, iconProps);
    const drawn = holder.querySelector("g");
    if (drawn) {
      drawn.removeAttribute("transform");
      root.append(drawn);
    }
    return root;
  }
  const ratio = preset.width / Math.max(1, preset.height);
  const w = ratio >= 1 ? size - 2 : (size - 2) * ratio;
  const h = ratio >= 1 ? (size - 2) / ratio : size - 2;
  const root = svg("svg", { width: size, height: size, viewBox: `0 0 ${size} ${size}` });
  const group = svg("g", { transform: `translate(${(size - w) / 2} ${(size - h) / 2})` });
  if (props.shape === "text") {
    const t = svg("text", { x: w / 2, y: h / 2 + 5, "text-anchor": "middle", "font-size": 14, fill: "#1f2430" });
    t.textContent = "T";
    group.append(t);
  } else {
    const iconProps = { ...props, radius: Math.min(num(props.radius, 0), 6) };
    const geometry = shapeGeometry(String(props.shape ?? "rectangle"), w, h, 1.25, iconProps);
    const fill = paint(props.fill, "#ffffff");
    group.append(svg("path", { d: geometry.body, fill: fill === "none" ? "none" : fill, stroke: paint(props.stroke, "#3b4250"), "stroke-width": 1.25, "stroke-linejoin": "round" }));
    for (const d of geometry.details) group.append(svg("path", { d, fill: "none", stroke: paint(props.stroke, "#3b4250"), "stroke-width": 1.25 }));
  }
  root.append(group);
  return root;
}
