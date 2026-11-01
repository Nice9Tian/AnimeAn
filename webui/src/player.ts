import { t } from "./i18n/index.js";
import {
  computeDockWindowLayout,
  syncDockHeaderDom,
  syncDockSplittersDom,
  type DockPreviewCallbacks,
} from "../vf/src/dock.ts";
import type { Value } from "../vf/src/expr.ts";
import {
  allWindowIds,
  childrenOfNode,
  isPartitionedWindow,
  LAYOUT_TYPE_KINDS,
  MOUSE_EVENTS,
  paintEffectForNode,
  parentOfNode,
  visibleEffectForNode,
  walkMenuItems,
  type ComponentInstance,
  type ComputedBox,
  type MenuItemSpec,
  type Project,
  type SceneNode,
} from "../vf/src/model.ts";
import { layoutLine, paintLine, paintShape, type LineLayout } from "../vf/src/paint.ts";
import { applyComponentStylesToDoc, scopeComponentCss } from "../vf/src/project.ts";
import { Runtime, type LogEntry, type RuntimeHooks } from "../vf/src/runtime.ts";
import {
  autoSide,
  parseBends,
  shapeGeometry,
  sideAnchor,
  sideDirection,
  SIDES,
  type Endpoint,
  type Point,
  type Rect,
  type Side,
} from "../vf/src/shapes.ts";
import {
  absRectInScene,
  nodeLocalToWorldInScene,
  rotatePoint,
  worldRotationInScene,
  worldToNodeLocalInScene,
} from "../vf/src/transform.ts";
import { STANDARD_COMPONENT_LIST, STANDARD_COMPONENTS } from "../vf/standard/index.ts";

export interface PlayerInstance {
  runtime: Runtime;
  refreshDockHeaders(): void;
  setExternal(id: string, el: HTMLElement | null): void;
  destroy(): void;
}

interface Mounted {
  wrapper: HTMLElement;
  body: HTMLElement;
  instance?: ComponentInstance;
  hit?: SVGGeometryElement;
  shown?: boolean;
  generation: number;
  lastBox?: ComputedBox;
}

const DOCK_ACTION_TIPS: Record<string, string> = { float: "Float panel", restore: "Dock panel", close: "Close panel" };

type PopupMenuItem = { label: string; disabled?: boolean; action: () => void } | "separator";

const SVG_NS = "http://www.w3.org/2000/svg";
const num = (value: Value | undefined, fallback: number): number =>
  typeof value === "number" && Number.isFinite(value) ? value : fallback;
const str = (value: Value | undefined, fallback = ""): string => (typeof value === "string" ? value : fallback);

const PLAYER_BASE_CSS = `
.vf-player-stage {
  --bg: #f3f4f6;
  --panel: #ffffff;
  --line: #d9dce1;
  --muted: #6b7280;
  --accent-soft: #dbe7ff;
  --preview: #d9480f;
  --error: #c62828;
  --hint: #8a6d1a;
  font-family: "Segoe UI", "Microsoft YaHei UI", system-ui, sans-serif;
  font-size: 13px;
  color: var(--text);
  position: relative;
  width: 100%;
  height: 100%;
  overflow: hidden;
  user-select: auto;
  box-sizing: border-box;
}
.vf-player-stage *,
.vf-player-stage *::before,
.vf-player-stage *::after {
  box-sizing: border-box;
}
.vf-player-stage button,
.vf-player-stage input,
.vf-player-stage textarea,
.vf-player-stage select {
  font: inherit;
  color: inherit;
}
.vf-player-stage .vf-node {
  position: absolute;
}
.vf-player-stage .vf-body {
  position: absolute;
  inset: 0;
}
.vf-player-stage .vf-node[data-vf-type="Container"] > .vf-body:not(:empty),
.vf-player-stage .vf-node[data-vf-type="VBox"] > .vf-body:not(:empty),
.vf-player-stage .vf-node[data-vf-type="HBox"] > .vf-body:not(:empty),
.vf-player-stage .vf-node[data-vf-type="Grid"] > .vf-body:not(:empty),
.vf-player-stage .vf-node[data-vf-type="Form"] > .vf-body:not(:empty),
.vf-player-stage .vf-node[data-vf-type="Stack"] > .vf-body:not(:empty) {
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px dashed #dfe2e7;
  color: #c3c7ce;
  font-size: 12px;
  overflow: hidden;
}
.vf-player-stage .vf-node[data-vf-type="External"] > .vf-body.vf-external-placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px solid #9aa1ad;
  background: repeating-linear-gradient(45deg, #f4f5f7 0 10px, #eceef1 10px 20px);
  color: #6b7280;
  font-size: 12px;
  text-align: center;
  white-space: pre-line;
  overflow: hidden;
}
.vf-player-stage .vf-error {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  height: 100%;
  border: 1px solid var(--error);
  background: #fdecec;
  color: var(--error);
  font-size: 12px;
  overflow: hidden;
  text-align: center;
}
.vf-player-stage .vf-node[data-vf-type="Shape"],
.vf-player-stage .vf-node[data-vf-type="Line"] {
  pointer-events: none;
}
.vf-player-stage .vf-shape-svg .vf-hit {
  pointer-events: all;
}
.vf-player-stage .vf-line-svg .vf-hit {
  pointer-events: stroke;
}
.vf-player-stage .vf-shape-svg,
.vf-player-stage .vf-line-svg {
  position: absolute;
  left: 0;
  top: 0;
  display: block;
  overflow: visible;
}
.vf-player-stage .vf-shape-text {
  position: absolute;
  display: flex;
  align-items: center;
  overflow: hidden;
  line-height: 1.3;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  pointer-events: none;
}
.vf-player-stage .vf-line-label {
  position: absolute;
  transform: translate(-50%, -50%);
  padding: 1px 5px;
  border-radius: 3px;
  background: #fff;
  line-height: 1.3;
  white-space: pre;
}
.vf-player-stage .vf-dock-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 26px;
  padding: 0 4px 0 8px;
  background: #232834;
  border-bottom: 1px solid #2e3440;
  color: #cdd5e0;
  font-size: 11px;
  font-weight: 600;
  user-select: none;
  cursor: grab;
  box-sizing: border-box;
}
.vf-player-stage .vf-dock-header:active {
  cursor: grabbing;
}
.vf-player-stage .vf-dock-header .dock-tabs {
  display: flex;
  align-items: center;
  gap: 2px;
  overflow: hidden;
  flex: 1;
  min-width: 0;
}
.vf-player-stage .vf-dock-header .dock-tab {
  appearance: none;
  border: 0;
  border-radius: 3px 3px 0 0;
  background: transparent;
  color: #8b95a5;
  font-size: 11px;
  font-weight: 600;
  padding: 3px 8px;
  cursor: pointer;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.vf-player-stage .vf-dock-header .dock-tab:hover {
  color: #dbe3ef;
  background: rgba(255, 255, 255, 0.05);
}
.vf-player-stage .vf-dock-header .dock-tab.active {
  color: #e5e9f0;
  background: #2e3545;
  border-bottom: 2px solid #5e81ac;
}
.vf-player-stage .vf-dock-header .dock-actions {
  display: flex;
  align-items: center;
  gap: 2px;
  flex-shrink: 0;
}
.vf-player-stage .vf-dock-header .dock-action-btn {
  appearance: none;
  border: 0;
  border-radius: 3px;
  width: 20px;
  height: 18px;
  padding: 0;
  background: transparent;
  color: #9aa5b4;
  font-size: 11px;
  line-height: 18px;
  text-align: center;
  cursor: pointer;
}
.vf-player-stage .vf-dock-header .dock-action-btn:hover {
  background: #353d4f;
  color: #ffffff;
}
.vf-player-stage .vf-node.dock-floating {
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.45);
  border: 1px solid #4c566a !important;
  border-radius: 4px;
}
.vf-player-stage .dock-float-resizer {
  position: absolute;
  right: 0;
  bottom: 0;
  width: 12px;
  height: 12px;
  cursor: nwse-resize;
  z-index: 5;
  background: linear-gradient(135deg, transparent 50%, rgba(160, 174, 192, 0.55) 50%);
}
.vf-player-stage .dock-splitter {
  position: absolute;
  z-index: 40;
  background: transparent;
  transition: background 0.12s ease;
}
.vf-player-stage .dock-splitter.vertical,
.vf-player-stage .dock-splitter-vertical {
  cursor: col-resize;
}
.vf-player-stage .dock-splitter.horizontal,
.vf-player-stage .dock-splitter-horizontal {
  cursor: row-resize;
}
.vf-player-stage .dock-splitter:hover {
  background: rgba(94, 129, 172, 0.55);
}
.vf-player-stage .dock-drop-overlay {
  position: absolute;
  pointer-events: none;
  z-index: 120;
  border: 2px dashed #5e81ac;
  background: rgba(94, 129, 172, 0.22);
  border-radius: 4px;
  box-sizing: border-box;
}
.vf-player-stage .dock-drag-ghost {
  position: absolute;
  pointer-events: none;
  z-index: 125;
  padding: 3px 8px;
  border-radius: 4px;
  background: rgba(35, 40, 52, 0.92);
  border: 1px solid #5e81ac;
  color: #e5e9f0;
  font-size: 11px;
  font-weight: 600;
  white-space: nowrap;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.35);
}
.vf-popup-menu {
  position: fixed;
  z-index: 1000;
  min-width: 150px;
  padding: 4px;
  border-radius: 6px;
  background: #1e293b;
  border: 1px solid #334155;
  box-shadow: 0 10px 25px rgba(0, 0, 0, 0.45);
  color: #e2e8f0;
  font-family: "Segoe UI", "Microsoft YaHei UI", system-ui, sans-serif;
  font-size: 12px;
}
.vf-popup-menu hr {
  border: 0;
  border-top: 1px solid #334155;
  margin: 4px 2px;
}
.vf-popup-menu button {
  display: block;
  width: 100%;
  text-align: left;
  appearance: none;
  border: 0;
  border-radius: 4px;
  padding: 5px 10px;
  background: transparent;
  color: inherit;
  font: inherit;
  cursor: pointer;
  white-space: pre;
}
.vf-popup-menu button:hover:not(:disabled) {
  background: #334155;
  color: #ffffff;
}
.vf-popup-menu button:disabled {
  opacity: 0.45;
  cursor: default;
}
`;

function injectAllStyles(doc: Document, project: Project): void {
  let baseStyle = doc.getElementById("vf-stage-base-styles") as HTMLStyleElement | null;
  if (!baseStyle) {
    baseStyle = doc.createElement("style");
    baseStyle.id = "vf-stage-base-styles";
    baseStyle.textContent = PLAYER_BASE_CSS;
    doc.head.prepend(baseStyle);
  }
  let css = "";
  for (const std of STANDARD_COMPONENT_LIST) {
    if (project.components.has(std.decl.type)) continue;
    if (std.styleText) css += `${scopeComponentCss(std.decl.type, std.styleText)}\n`;
  }
  for (const [type, component] of project.components) {
    const styleText = (component as { styleText?: string }).styleText;
    if (styleText) css += `${scopeComponentCss(type, styleText)}\n`;
  }
  applyComponentStylesToDoc(doc, css);
}

function showNodeError(body: HTMLElement, message: string): void {
  body.innerHTML = "";
  const box = body.ownerDocument.createElement("div");
  box.className = "vf-error";
  box.textContent = message;
  body.append(box);
}

function matchesMenuShortcut(e: KeyboardEvent, shortcut: string): boolean {
  const parts = shortcut
    .split("+")
    .map((p) => p.trim().toLowerCase())
    .filter(Boolean);
  if (parts.length === 0) return false;
  const needCtrl = parts.includes("ctrl") || parts.includes("control") || parts.includes("cmd");
  const needShift = parts.includes("shift");
  const needAlt = parts.includes("alt");
  if ((e.ctrlKey || e.metaKey) !== needCtrl) return false;
  if (e.shiftKey !== needShift) return false;
  if (e.altKey !== needAlt) return false;
  const keyPart = parts[parts.length - 1];
  return e.key.toLowerCase() === keyPart;
}

export function createPlayer(
  root: HTMLElement,
  project: Project,
  hooks: Partial<RuntimeHooks> & { onDockClosed?(dockId: string): void } = {},
): PlayerInstance {
  const doc = root.ownerDocument ?? document;
  const win = doc.defaultView ?? window;
  injectAllStyles(doc, project);

  const stageEl = doc.createElement("div");
  stageEl.id = "stage-canvas";
  stageEl.className = "vf-player-stage";

  const popupMenuEl = doc.createElement("div");
  popupMenuEl.id = "menu";
  popupMenuEl.className = "vf-popup-menu";
  popupMenuEl.hidden = true;

  root.replaceChildren(stageEl, popupMenuEl);

  const mounted = new Map<string, Mounted>();
  const menuItemButtons = new Map<string, HTMLButtonElement[]>();
  const externalElements = new Map<string, HTMLElement>();
  const outlineCache = new Map<string, Point[]>();
  const lineNodeIds: string[] = [];

  const measureSvg = doc.createElementNS(SVG_NS, "svg");
  measureSvg.setAttribute(
    "style",
    "position:absolute;left:-9999px;top:-9999px;width:0;height:0;visibility:hidden",
  );
  const measurePath = doc.createElementNS(SVG_NS, "path");
  measureSvg.append(measurePath);
  root.append(measureSvg);

  let clockTimer: number | null = null;
  let currentBoxes = new Map<string, ComputedBox>();
  let isApplying = false;

  const stopClockTimer = () => {
    if (clockTimer !== null) {
      win.clearInterval(clockTimer);
      clockTimer = null;
    }
  };

  const syncClockTimer = () => {
    const playing = Boolean(runtime.getState("timeline.playing") || runtime.getState("project.playing"));
    if (playing && clockTimer === null) {
      clockTimer = win.setInterval(() => {
        runtime.setClock(runtime.getClock() + 100);
      }, 100);
    } else if (!playing && clockTimer !== null) {
      stopClockTimer();
    }
  };

  const runtimeHooks: RuntimeHooks = {
    onChange: () => {
      syncClockTimer();
      applyViews(false);
      hooks.onChange?.();
    },
    onLog: (entry: LogEntry) => {
      hooks.onLog?.(entry);
    },
    raiseBusiness: hooks.raiseBusiness,
    raiseAction: hooks.raiseAction,
  };

  const runtime = new Runtime(project, runtimeHooks);

  const node = (id: string): SceneNode | undefined => project.scene.nodes[id];

  const getRootSize = (): { width: number; height: number } => {
    const rootNode = node(project.scene.root);
    const w = stageEl.clientWidth || root.clientWidth || win.innerWidth || (rootNode?.width ?? 1280);
    const h = stageEl.clientHeight || root.clientHeight || win.innerHeight || (rootNode?.height ?? 800);
    return { width: Math.max(1, Math.round(w)), height: Math.max(1, Math.round(h)) };
  };

  const hideMenu = () => {
    popupMenuEl.hidden = true;
  };

  const openMenu = (x: number, y: number, items: PopupMenuItem[]) => {
    popupMenuEl.innerHTML = "";
    for (const item of items) {
      if (item === "separator") {
        popupMenuEl.append(doc.createElement("hr"));
        continue;
      }
      const button = doc.createElement("button");
      button.type = "button";
      button.textContent = item.label;
      if (item.disabled) button.disabled = true;
      button.addEventListener("click", () => {
        hideMenu();
        item.action();
      });
      popupMenuEl.append(button);
    }
    popupMenuEl.hidden = false;
    const rect = popupMenuEl.getBoundingClientRect();
    const maxW = win.innerWidth || 1280;
    const maxH = win.innerHeight || 800;
    popupMenuEl.style.left = `${Math.max(0, Math.min(x, maxW - rect.width - 8))}px`;
    popupMenuEl.style.top = `${Math.max(0, Math.min(y, maxH - rect.height - 8))}px`;
  };

  const triggerMenuItem = (itemId: string) => {
    runtime.dispatch(itemId, "mousedown");
    runtime.dispatch(itemId, "mouseup");
    runtime.dispatch(itemId, "click");
    runtime.dispatch(null, "window.click");
  };

  const flattenMenuSpecsToPopupItems = (specs: MenuItemSpec[], prefix = ""): PopupMenuItem[] => {
    const out: PopupMenuItem[] = [];
    for (const spec of specs) {
      if (spec.kind === "separator") {
        out.push("separator");
        continue;
      }
      const view = runtime.view(spec.id);
      if (!view.visible) continue;
      const text = String(view.props.text ?? spec.text ?? spec.id);
      if (spec.items?.length) {
        out.push(...flattenMenuSpecsToPopupItems(spec.items, `${prefix}${text} ▸ `));
        continue;
      }
      const isRadio = spec.kind === "radio" || Boolean(spec.group);
      const isCheck = isRadio || spec.kind === "check" || Boolean(spec.checkable) || Boolean(spec.dock);
      const checked = Boolean(view.props.checked);
      const mark = isCheck ? (checked ? (isRadio ? "● " : "✓ ") : "   ") : "";
      const sc = spec.shortcut ? `　(${spec.shortcut})` : "";
      out.push({
        label: `${mark}${prefix}${text}${sc}`,
        disabled: !view.enabled,
        action: () => {
          if (view.enabled) triggerMenuItem(spec.id);
        },
      });
    }
    return out;
  };

  const showPopupMenu = (x: number, y: number, specs: MenuItemSpec[]) => {
    const items = flattenMenuSpecsToPopupItems(specs);
    if (items.length > 0) openMenu(x, y, items);
  };

  // ---------- 图形与线条计算 ----------

  const absRect = (id: string): Rect => absRectInScene(project.scene, id, currentBoxes);
  const worldRotation = (id: string): number => worldRotationInScene(project.scene, id);

  const parentOrigin = (id: string): Point => {
    const parentId = parentOfNode(project.scene, id);
    if (!parentId) return { x: 0, y: 0 };
    const r = absRect(parentId);
    return { x: r.x, y: r.y };
  };

  const attachedTarget = (lineId: string, value: Value | undefined): string | null => {
    const target = str(value);
    const n = target ? node(target) : undefined;
    return n && target !== lineId && n.type !== "Line" && target !== project.scene.root ? target : null;
  };

  const outlineOf = (id: string): Point[] => {
    const n = node(id);
    if (!n) return [];
    const props = runtime.view(id).props;
    const kind = str(props.shape, "rectangle");
    const isShape = n.type === "Shape" && kind !== "text";
    const stroke = props.stroke === "none" ? 0 : num(props.strokeWidth, 2);
    const r = absRect(id);
    const key = isShape
      ? JSON.stringify([kind, r.width, r.height, stroke, props.radius, props.sides, props.points, props.ratio])
      : `box|${r.width}|${r.height}`;
    const cached = outlineCache.get(key);
    if (cached) return cached;
    let points: Point[];
    if (isShape && typeof measurePath.getTotalLength === "function") {
      measurePath.setAttribute("d", shapeGeometry(kind, r.width, r.height, stroke, props).body);
      const total = measurePath.getTotalLength();
      const count = Math.max(32, Math.min(800, Math.round(total / 2)));
      points = [];
      for (let i = 0; i < count; i++) {
        const p = measurePath.getPointAtLength((total * i) / count);
        points.push({ x: p.x, y: p.y });
      }
    } else {
      points = [
        { x: 0, y: 0 },
        { x: r.width, y: 0 },
        { x: r.width, y: r.height },
        { x: 0, y: r.height },
      ];
    }
    if (outlineCache.size > 500) outlineCache.clear();
    outlineCache.set(key, points);
    return points;
  };

  const worldOutlineOf = (id: string): Point[] =>
    outlineOf(id).map((pt) => nodeLocalToWorldInScene(project.scene, id, pt, currentBoxes));

  const nearestOnOutline = (id: string, p: Point): { point: Point; distance: number } => {
    const r = absRect(id);
    const pts = worldOutlineOf(id);
    let best = { point: { x: r.x, y: r.y }, distance: Infinity };
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i];
      const b = pts[(i + 1) % pts.length];
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const length2 = dx * dx + dy * dy;
      const t = length2 > 0 ? Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2)) : 0;
      const q = { x: a.x + dx * t, y: a.y + dy * t };
      const d = Math.hypot(q.x - p.x, q.y - p.y);
      if (d < best.distance) best = { point: q, distance: d };
    }
    return best;
  };

  const outlineRayHit = (id: string, direction: Point): Point | null => {
    const r = absRect(id);
    const c = { x: r.x + r.width / 2, y: r.y + r.height / 2 };
    const pts = worldOutlineOf(id);
    let best: Point | null = null;
    let bestT = -1;
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i];
      const b = pts[(i + 1) % pts.length];
      const ex = b.x - a.x;
      const ey = b.y - a.y;
      const denominator = direction.x * ey - direction.y * ex;
      if (Math.abs(denominator) < 1e-9) continue;
      const t = ((a.x - c.x) * ey - (a.y - c.y) * ex) / denominator;
      const u = ((a.x - c.x) * direction.y - (a.y - c.y) * direction.x) / denominator;
      if (t > 0 && u >= 0 && u <= 1 && t > bestT) {
        bestT = t;
        best = { x: c.x + direction.x * t, y: c.y + direction.y * t };
      }
    }
    return best;
  };

  const sideDirectionOf = (id: string, side: Side): Point => {
    const dir = sideDirection(side);
    const rot = worldRotation(id);
    return rot ? rotatePoint(dir, { x: 0, y: 0 }, rot) : dir;
  };

  const sideAnchorOf = (id: string, side: Side): Point => {
    const rotDir = sideDirectionOf(id, side);
    if (node(id)?.type === "Shape") {
      const hit = outlineRayHit(id, rotDir);
      if (hit) return hit;
    }
    const r = absRect(id);
    const localAnchor = sideAnchor({ x: 0, y: 0, width: r.width, height: r.height }, side);
    return nodeLocalToWorldInScene(project.scene, id, localAnchor, currentBoxes);
  };

  const parseAt = (value: Value | undefined): Point | null => {
    if (typeof value !== "string") return null;
    const [x, y] = value.split(",").map(Number);
    return Number.isFinite(x) && Number.isFinite(y) ? { x, y } : null;
  };

  const resolveAttach = (id: string, sideValue: string, at: Point | null, other: Point): Endpoint => {
    const rect = absRect(id);
    const localRect = { x: 0, y: 0, width: rect.width, height: rect.height };
    if (sideValue === "outline" && at) {
      const guess = nodeLocalToWorldInScene(
        project.scene,
        id,
        { x: at.x * rect.width, y: at.y * rect.height },
        currentBoxes,
      );
      const point = nearestOnOutline(id, guess).point;
      const localPt = worldToNodeLocalInScene(project.scene, id, point, currentBoxes);
      const localSide = autoSide(localRect, localPt);
      return { point, direction: sideDirectionOf(id, localSide) };
    }
    const localOther = worldToNodeLocalInScene(project.scene, id, other, currentBoxes);
    const side: Side = SIDES.includes(sideValue as Side) ? (sideValue as Side) : autoSide(localRect, localOther);
    return { point: sideAnchorOf(id, side), direction: sideDirectionOf(id, side) };
  };

  const lineEndpoints = (id: string, props: Record<string, Value>): { start: Endpoint; end: Endpoint } => {
    const origin = parentOrigin(id);
    const raw = (which: "start" | "end"): Point =>
      which === "start"
        ? { x: origin.x + num(props.x1, 0), y: origin.y + num(props.y1, 0) }
        : { x: origin.x + num(props.x2, 160), y: origin.y + num(props.y2, 0) };
    const startTarget = attachedTarget(id, props.startNode);
    const endTarget = attachedTarget(id, props.endNode);
    const center = (target: string): Point => {
      const r = absRect(target);
      return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
    };
    const startRef = startTarget ? center(startTarget) : raw("start");
    const endRef = endTarget ? center(endTarget) : raw("end");
    const resolve = (which: "start" | "end", target: string | null, other: Point): Endpoint => {
      if (!target) return { point: raw(which), direction: null };
      const sideValue = str(which === "start" ? props.startSide : props.endSide, "auto");
      return resolveAttach(target, sideValue, parseAt(which === "start" ? props.startAt : props.endAt), other);
    };
    return { start: resolve("start", startTarget, endRef), end: resolve("end", endTarget, startRef) };
  };

  const lineLayoutOf = (id: string, props: Record<string, Value>): LineLayout => {
    const { start, end } = lineEndpoints(id, props);
    const origin = parentOrigin(id);
    const bends = parseBends(props.bends).map((p) => ({ x: p.x + origin.x, y: p.y + origin.y }));
    return layoutLine(start, end, props, bends);
  };

  const paintGraphic = (id: string, entry: Mounted, props: Record<string, Value>) => {
    const n = node(id);
    if (!n) return;
    if (n.type === "Shape") {
      const box = currentBoxes.get(id);
      const w = box ? box.width : n.width;
      const h = box ? box.height : n.height;
      entry.hit = paintShape(entry.body, w, h, props);
    } else {
      const layout = lineLayoutOf(id, props);
      const origin = parentOrigin(id);
      const left = Math.floor(layout.bbox.x);
      const top = Math.floor(layout.bbox.y);
      const lx = left - origin.x;
      const ly = top - origin.y;
      const lw = Math.ceil(layout.bbox.x + layout.bbox.width) - left;
      const lh = Math.ceil(layout.bbox.y + layout.bbox.height) - top;
      Object.assign(entry.wrapper.style, {
        left: `${lx}px`,
        top: `${ly}px`,
        width: `${lw}px`,
        height: `${lh}px`,
      });
      entry.hit = paintLine(entry.body, layout, { x: left, y: top }, props);
    }
    entry.wrapper.style.opacity = String(Math.max(0, Math.min(1, num(props.opacity, 1))));
  };

  // ---------- 节点构建与显示过渡 ----------

  const registerMenuButton = (itemId: string, btn: HTMLButtonElement) => {
    let list = menuItemButtons.get(itemId);
    if (!list) {
      list = [];
      menuItemButtons.set(itemId, list);
    }
    list.push(btn);
  };

  const syncExternalBody = (id: string, body: HTMLElement) => {
    const extEl = externalElements.get(id);
    if (extEl) {
      body.classList.remove("vf-external-placeholder");
      body.replaceChildren(extEl);
    } else {
      body.classList.add("vf-external-placeholder");
      body.textContent = `外部容器 ${id}\n导出后由业务放入控件`;
    }
  };

  const setShown = (id: string, entry: Mounted, shown: boolean) => {
    if (entry.shown === shown) return;
    const first = entry.shown === undefined;
    entry.shown = shown;
    const generation = ++entry.generation;
    const effectName = visibleEffectForNode(project, id);
    const play = effectName ? project.effects.get(effectName)?.play : undefined;
    const finish = () => {
      if (entry.generation === generation) entry.wrapper.style.display = shown ? "" : "none";
    };
    if (first || !play) return finish();
    entry.wrapper.style.display = "";
    try {
      const result = play(entry.wrapper, { showing: shown });
      const done = result && "finished" in result ? result.finished : result;
      Promise.resolve(done).then(finish, finish);
    } catch {
      finish();
    }
  };

  const buildNode = (id: string, parentEl: HTMLElement) => {
    const n = node(id);
    if (!n) return;
    const wrapper = doc.createElement("div");
    wrapper.className = "vf-node";
    wrapper.dataset.vfId = id;
    wrapper.dataset.vfType = n.type;
    Object.assign(wrapper.style, {
      left: `${n.x ?? 0}px`,
      top: `${n.y ?? 0}px`,
      width: `${n.width}px`,
      height: `${n.height}px`,
      transform: n.rotation ? `rotate(${n.rotation}deg)` : "",
      transformOrigin: "center center",
    });
    const body = doc.createElement("div");
    body.className = "vf-body";
    wrapper.append(body);
    parentEl.append(wrapper);
    const entry: Mounted = { wrapper, body, generation: 0 };
    mounted.set(id, entry);

    if (n.type === "Line") {
      lineNodeIds.push(id);
    }

    if (n.type === "Dock") {
      const header = doc.createElement("div");
      header.className = "vf-dock-header";
      header.style.cssText = `height:${n.headerHeight ?? 26}px;display:flex;align-items:center;padding:0 10px;font-size:12px;font-weight:600;background:#182030;color:#cbd5e1;border-bottom:1px solid #2b3548;box-sizing:border-box;user-select:none;`;
      header.textContent = String(runtime.view(id).props.title ?? n.title ?? id);
      wrapper.prepend(header);
      wrapper.style.background = "#111726";
      wrapper.style.boxShadow = "inset 0 0 0 1px #263042";
      wrapper.style.boxSizing = "border-box";
    } else if (n.type === "Dialog") {
      wrapper.style.background = "#151c2c";
      wrapper.style.borderRadius = "8px";
      wrapper.style.boxShadow = "inset 0 0 0 1px #334155, 0 20px 50px rgba(0,0,0,0.6)";
      wrapper.style.zIndex = "50";
    } else if (n.type === "Container" || n.type in LAYOUT_TYPE_KINDS) {
      if (!childrenOfNode(n).length) body.textContent = id;
    } else if (n.type === "External") {
      syncExternalBody(id, body);
    } else if (n.type === "Shape" || n.type === "Line") {
      paintGraphic(id, entry, runtime.view(id).props);
    } else {
      const component = project.components.get(n.type) ?? STANDARD_COMPONENTS.get(n.type);
      if (!component?.create) {
        showNodeError(body, component?.error ?? `找不到控件 ${n.type}`);
      } else {
        try {
          entry.instance = component.create(body, {
            props: runtime.view(id).props,
            emit: (event, value) => {
              runtime.dispatch(id, event, value);
            },
            mode: "preview",
          });
        } catch (e) {
          showNodeError(body, `${n.type} 运行出错：${(e as Error).message}`);
        }
      }
    }

    for (const name of MOUSE_EVENTS) {
      wrapper.addEventListener(name, (e) => {
        const bubbles = name !== "mouseenter" && name !== "mouseleave";
        if (bubbles && (e.target as Element | null)?.closest(".vf-node") !== wrapper) return;
        runtime.dispatch(id, name);
        if (name === "click" && n.popupMenu?.length) {
          const rect = wrapper.getBoundingClientRect();
          showPopupMenu(rect.left, rect.bottom + 2, n.popupMenu);
        }
      });
    }

    if (n.contextMenu?.length) {
      wrapper.addEventListener("contextmenu", (e) => {
        if ((e.target as Element | null)?.closest(".vf-node") !== wrapper) return;
        e.preventDefault();
        e.stopPropagation();
        showPopupMenu(e.clientX, e.clientY, n.contextMenu!);
      });
    }

    const extraMenuItems: MenuItemSpec[] = [];
    if (n.contextMenu?.length) walkMenuItems(n.contextMenu, (it) => extraMenuItems.push(it));
    if (n.popupMenu?.length) walkMenuItems(n.popupMenu, (it) => extraMenuItems.push(it));
    if (extraMenuItems.length > 0) {
      const hiddenMenuHost = doc.createElement("div");
      hiddenMenuHost.style.display = "none";
      for (const item of extraMenuItems) {
        if (item.kind === "separator" || item.items?.length) continue;
        const btn = doc.createElement("button");
        btn.type = "button";
        btn.dataset.vfMenuItem = item.id;
        btn.dataset.vfMenuBaseText = item.text;
        const isRadio = item.kind === "radio" || Boolean(item.group);
        const isCheck = isRadio || item.kind === "check" || Boolean(item.checkable) || Boolean(item.dock);
        if (isCheck) btn.dataset.vfMenuCheckable = "1";
        if (isRadio) btn.dataset.vfMenuRadio = "1";
        btn.addEventListener("click", () => triggerMenuItem(item.id));
        hiddenMenuHost.append(btn);
        registerMenuButton(item.id, btn);
      }
      wrapper.append(hiddenMenuHost);
    }

    for (const child of childrenOfNode(n)) buildNode(child, wrapper);
  };

  // ---------- 增量更新 ----------

  const rootId = project.scene.root;

  const dockCallbacks: DockPreviewCallbacks = {
    getMode: () => "preview",
    getScene: () => project.scene,
    getDockLayout: () => runtime.getDockLayout(rootId),
    getWindowSize: () => getRootSize(),
    getDockTitle: (did) => t(String(runtime.view(did).props.title ?? node(did)?.title ?? did)),
    onActivateTab: (did) => runtime.dockActivateTab(did),
    onTabify: (targetId, did) => runtime.dockTabify(targetId, did),
    onSplit: (targetId, did, pos, orient) => runtime.dockSplit(targetId, did, pos, orient),
    onMoveToArea: (did, area) => runtime.dockMoveToArea(did, area),
    onFloat: (did, rect) => runtime.dockFloat(did, rect),
    onRestore: (did) => runtime.dockRestore(did),
    onClose: (did) => { runtime.dockSetVisible(did, false); hooks.onDockClosed?.(did); },
    onResizeArea: (area, size) => runtime.dockResizeArea(area, size),
    onResizeSplit: (area, idx, size) => runtime.dockResizeSplit(area, idx, size),
  };

  // dock.ts writes its button tips in one language; the player owns what shows.
  const syncDockHeader = (
    wrapper: HTMLElement,
    header: HTMLElement,
    id: string,
    height: number,
    info: Parameters<typeof syncDockHeaderDom>[4],
  ) => {
    syncDockHeaderDom(wrapper, header, id, height, info, stageEl, dockCallbacks);
    for (const button of header.querySelectorAll<HTMLElement>(".dock-action-btn")) {
      const action = button.dataset.dockAction;
      if (action) button.title = t(DOCK_ACTION_TIPS[action]);
    }
  };

  let dockInfoMap:
    | Map<
        string,
        {
          groupTabs: string[];
          activeTab: string;
          isFloating: boolean;
          area?: "left" | "right" | "top" | "bottom";
        }
      >
    | undefined;

  const updateMenuItemButtons = (itemId: string) => {
    const btns = menuItemButtons.get(itemId);
    if (!btns) return;
    const view = runtime.view(itemId);
    for (const btn of btns) {
      btn.disabled = !view.enabled;
      if (btn.parentElement?.style.display !== "none") {
        btn.style.display = view.visible ? "" : "none";
      }
      const checkable = btn.dataset.vfMenuCheckable === "1";
      const isRadio = btn.dataset.vfMenuRadio === "1";
      const checked = Boolean(view.props.checked);
      const labelText = String(view.props.text ?? btn.dataset.vfMenuBaseText ?? "");
      const prefix = checkable ? (checked ? (isRadio ? "● " : "✓ ") : "") : "";
      btn.textContent = prefix + labelText;
      if (checkable) {
        btn.style.borderColor = checked ? "#38bdf8" : "#334155";
        btn.style.color = checked ? "#f8fafc" : "#e2e8f0";
      }
    }
  };

  const updateNodeBoxAndPlacement = (
    id: string,
    entry: Mounted,
    rootSize: { width: number; height: number },
  ) => {
    const n = node(id);
    if (!n) return;
    const view = runtime.view(id);
    const box = currentBoxes.get(id);

    if (n.type === "FloatBox") {
      const wantFloat = Boolean(view.props.floating);
      if (wantFloat && entry.wrapper.parentElement !== stageEl) {
        stageEl.append(entry.wrapper);
      } else if (!wantFloat) {
        const origPid = parentOfNode(project.scene, id);
        const origParentEl = origPid && origPid !== rootId ? mounted.get(origPid)?.wrapper : stageEl;
        if (origParentEl && entry.wrapper.parentElement !== origParentEl) {
          origParentEl.append(entry.wrapper);
        }
      }
      entry.wrapper.style.zIndex = wantFloat ? "35" : "";
    }

    if (box && n.type !== "Line") {
      const prevBox = entry.lastBox;
      const isDialog = n.type === "Dialog" && id !== rootId;
      const useAbs = n.type === "FloatBox" && Boolean(view.props.floating) && entry.wrapper.parentElement === stageEl;
      const targetLeft = isDialog ? Math.round((rootSize.width - box.width) / 2) : useAbs ? box.absX : box.x;
      const targetTop = isDialog ? Math.round((rootSize.height - box.height) / 2) : useAbs ? box.absY : box.y;
      if (
        !prevBox ||
        prevBox.x !== targetLeft ||
        prevBox.y !== targetTop ||
        prevBox.width !== box.width ||
        prevBox.height !== box.height
      ) {
        Object.assign(entry.wrapper.style, {
          left: `${targetLeft}px`,
          top: `${targetTop}px`,
          width: `${box.width}px`,
          height: `${box.height}px`,
          transform: n.rotation ? `rotate(${n.rotation}deg)` : "",
          transformOrigin: "center center",
        });
        entry.lastBox = { ...box, x: targetLeft, y: targetTop };
        if (n.type === "Shape") {
          paintGraphic(id, entry, view.props);
        }
      }
    }

    const effectiveVis = box ? box.visible : view.visible;
    setShown(id, entry, effectiveVis);

    if (n.type === "Dock") {
      const header = entry.wrapper.querySelector<HTMLElement>(".vf-dock-header");
      const info = dockInfoMap?.get(id);
      if (header) syncDockHeader(entry.wrapper, header, id, n.headerHeight ?? 26, info);
      entry.wrapper.style.zIndex = info?.isFloating ? "30" : "";
    }
  };

  const updateNodeProps = (id: string, entry: Mounted) => {
    const n = node(id);
    if (!n) return;
    const view = runtime.view(id);
    const box = currentBoxes.get(id);

    entry.wrapper.toggleAttribute("data-vf-disabled", !view.enabled);

    if (n.type === "Dock") {
      const header = entry.wrapper.querySelector<HTMLElement>(".vf-dock-header");
      const info = dockInfoMap?.get(id);
      if (header) syncDockHeader(entry.wrapper, header, id, n.headerHeight ?? 26, info);
      entry.wrapper.style.zIndex = info?.isFloating ? "30" : "";
    } else if (n.type === "FloatBox") {
      entry.wrapper.style.zIndex = view.props.floating ? "35" : "";
    }

    if (n.type === "Shape" || n.type === "Line") {
      paintGraphic(id, entry, view.props);
      return;
    }

    try {
      entry.instance?.update(view.props);
    } catch (e) {
      entry.instance = undefined;
      showNodeError(entry.body, `运行出错：${(e as Error).message}`);
    }

    const paintName = paintEffectForNode(project, id);
    if (paintName) {
      const paintEffect = project.effects.get(paintName);
      const w = Math.max(1, box?.width ?? n.width ?? 100);
      const h = Math.max(1, box?.height ?? n.height ?? 100);
      if (paintEffect?.paint) {
        try {
          paintEffect.paint(entry.body, { props: view.props, width: w, height: h });
        } catch (e) {
          showNodeError(entry.body, `绘制效果 ${paintName} 运行出错：${(e as Error).message}`);
        }
      } else {
        let placeholder = entry.body.querySelector<HTMLElement>(".vf-paint-placeholder");
        if (!placeholder) {
          placeholder = doc.createElement("div");
          placeholder.className = "vf-paint-placeholder";
          placeholder.style.cssText =
            "width:100%;height:100%;box-sizing:border-box;border:1px dashed #94a3b8;border-radius:6px;background:rgba(248,250,252,0.85);color:#475569;font-size:12px;display:flex;align-items:center;justify-content:center;text-align:center;padding:6px;";
          entry.body.replaceChildren(placeholder);
        }
        placeholder.textContent = `[绘制效果占位：${paintName}]`;
      }
    }
  };

  function applyViews(forceFull = false): void {
    if (isApplying) return;
    isApplying = true;
    try {
      const rootSize = getRootSize();
      const { dirtyNodes, layoutChanged, boxes } = runtime.refresh(rootSize);
      currentBoxes = boxes;
      const updateAll = forceFull || dirtyNodes === "all";
      const rootNode = node(rootId);

      if ((layoutChanged || updateAll) && rootNode && isPartitionedWindow(rootNode)) {
        const menuH = rootNode.menuBar ? Math.max(0, Math.round(rootNode.menuBar.height ?? 28)) : 0;
        const statusNode = rootNode.statusBar ? project.scene.nodes[rootNode.statusBar] : undefined;
        const statusVis = statusNode && rootNode.statusBar ? runtime.view(rootNode.statusBar).visible : false;
        const statusH = statusNode && statusVis ? Math.round(statusNode.height ?? 28) : 0;
        const midY = menuH;
        const midH = Math.max(0, rootSize.height - menuH - statusH);
        const dockRes = computeDockWindowLayout(
          project.scene,
          rootId,
          rootNode,
          rootSize.width,
          midY,
          midH,
          true,
          (nid) => runtime.view(nid).visible,
          runtime.getDockLayout(rootId),
        );
        dockInfoMap = dockRes.activeTabByDock;
        syncDockSplittersDom(stageEl, dockRes.splitters, dockCallbacks);
      }

      if (layoutChanged || updateAll) {
        for (const [id, entry] of mounted) {
          updateNodeBoxAndPlacement(id, entry, rootSize);
        }
        for (const lineId of lineNodeIds) {
          const lineEntry = mounted.get(lineId);
          if (lineEntry) paintGraphic(lineId, lineEntry, runtime.view(lineId).props);
        }
      }

      if (updateAll) {
        for (const [id, entry] of mounted) {
          updateNodeProps(id, entry);
        }
        for (const itemId of menuItemButtons.keys()) {
          updateMenuItemButtons(itemId);
        }
      } else {
        for (const id of dirtyNodes) {
          const entry = mounted.get(id);
          if (entry) {
            if (!layoutChanged) {
              updateNodeBoxAndPlacement(id, entry, rootSize);
            }
            updateNodeProps(id, entry);
          }
          if (menuItemButtons.has(id)) {
            updateMenuItemButtons(id);
          }
        }
      }
    } finally {
      isApplying = false;
    }
  }

  // ---------- 初始渲染主舞台 ----------

  const rootNode = node(rootId);
  if (rootNode?.menuBar) {
    const bar = doc.createElement("div");
    const mh = rootNode.menuBar.height ?? 28;
    bar.style.cssText = `position:absolute;left:0;top:0;width:100%;height:${mh}px;display:flex;align-items:center;gap:6px;padding:0 10px;background:#111827;border-bottom:1px solid #263042;box-sizing:border-box;z-index:20;font-size:12px;color:#e2e8f0;overflow:hidden;`;
    for (const menu of rootNode.menuBar.menus ?? []) {
      const group = doc.createElement("div");
      group.style.cssText = "display:flex;align-items:center;gap:2px;flex-shrink:0;";
      const titleSpan = doc.createElement("button");
      titleSpan.type = "button";
      titleSpan.dataset.vfMenuTitle = menu.id;
      titleSpan.style.cssText =
        "font-weight:600;color:#cbd5e1;background:#1e293b;border:1px solid #334155;padding:2px 8px;border-radius:4px;font-size:12px;cursor:pointer;";
      titleSpan.textContent = menu.title;
      titleSpan.addEventListener("click", () => {
        if (menu.items?.length) {
          const rect = titleSpan.getBoundingClientRect();
          showPopupMenu(rect.left, rect.bottom + 2, menu.items);
        }
      });
      const hiddenItems = doc.createElement("div");
      hiddenItems.style.display = "none";
      walkMenuItems(menu.items, (item) => {
        if (item.kind === "separator" || item.items?.length) return;
        const btn = doc.createElement("button");
        btn.type = "button";
        btn.dataset.vfMenuItem = item.id;
        btn.dataset.vfMenuBaseText = item.text;
        const isRadio = item.kind === "radio" || Boolean(item.group);
        const isCheck = isRadio || item.kind === "check" || Boolean(item.checkable) || Boolean(item.dock);
        if (isCheck) btn.dataset.vfMenuCheckable = "1";
        if (isRadio) btn.dataset.vfMenuRadio = "1";
        if (item.shortcut) btn.title = `${item.text} (${item.shortcut})`;
        btn.textContent = item.text;
        btn.addEventListener("click", () => triggerMenuItem(item.id));
        hiddenItems.append(btn);
        registerMenuButton(item.id, btn);
      });
      group.append(titleSpan, hiddenItems);
      bar.append(group);
    }
    stageEl.append(bar);
  }

  if (rootNode) {
    for (const child of childrenOfNode(rootNode)) buildNode(child, stageEl);
  }
  for (const winId of allWindowIds(project.scene)) {
    if (winId !== rootId) buildNode(winId, stageEl);
  }

  const onStageClick = (e: MouseEvent) => {
    if (!e.altKey) runtime.dispatch(null, "window.click");
  };
  stageEl.addEventListener("click", onStageClick);

  const onDocPointerDown = (e: PointerEvent) => {
    if (!(e.target as Element | null)?.closest("#menu")) hideMenu();
  };
  doc.addEventListener("pointerdown", onDocPointerDown);

  const onKeyDown = (e: KeyboardEvent) => {
    if (e.key === "Escape") {
      hideMenu();
      const modalWin = runtime.activeModalWindow();
      if (modalWin) {
        runtime.closeWindow(modalWin);
        return;
      }
    }
    const target = e.target;
    const inInput =
      target instanceof HTMLElement && (target.matches("input, textarea, select") || target.isContentEditable);
    if (inInput && !e.ctrlKey && !e.metaKey && !e.altKey) return;
    let matchedId: string | null = null;
    for (const menu of rootNode?.menuBar?.menus ?? []) {
      walkMenuItems(menu.items, (item) => {
        if (matchedId || item.kind === "separator" || !item.shortcut) return;
        if (matchesMenuShortcut(e, item.shortcut)) {
          const view = runtime.view(item.id);
          if (view.visible && view.enabled) matchedId = item.id;
        }
      });
    }
    if (matchedId) {
      e.preventDefault();
      e.stopPropagation();
      triggerMenuItem(matchedId);
    }
  };
  doc.addEventListener("keydown", onKeyDown);

  const onWindowResize = () => {
    applyViews(false);
  };
  win.addEventListener("resize", onWindowResize);

  let resizeObserver: ResizeObserver | null = null;
  if (typeof ResizeObserver !== "undefined") {
    resizeObserver = new ResizeObserver(() => {
      applyViews(false);
    });
    resizeObserver.observe(root);
  }

  runtime.start();
  applyViews(true);

  return {
    runtime,
    refreshDockHeaders(): void {
      for (const [id, entry] of mounted) {
        if (node(id)?.type === "Dock") updateNodeProps(id, entry);
      }
    },
    setExternal(id: string, el: HTMLElement | null): void {
      if (el) externalElements.set(id, el);
      else externalElements.delete(id);
      const entry = mounted.get(id);
      if (entry && node(id)?.type === "External") {
        syncExternalBody(id, entry.body);
      }
    },
    destroy(): void {
      stopClockTimer();
      resizeObserver?.disconnect();
      win.removeEventListener("resize", onWindowResize);
      doc.removeEventListener("pointerdown", onDocPointerDown);
      doc.removeEventListener("keydown", onKeyDown);
      stageEl.removeEventListener("click", onStageClick);
      for (const entry of mounted.values()) {
        try {
          entry.instance?.destroy();
        } catch {
          // 忽略销毁异常
        }
      }
      mounted.clear();
      menuItemButtons.clear();
      measureSvg.remove();
      stageEl.remove();
      popupMenuEl.remove();
    },
  };
}
