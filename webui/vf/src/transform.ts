// 几何变换与编排操作：锁定判断、旋转、单选与多选整体缩放、对齐与等距分布、线条拐点操作。
// 纯数据计算，不依赖浏览器 DOM，可在编辑器界面和命令行测试中共用。
import type { Value } from "./expr.ts";
import {
  childrenOfNode,
  computeSceneLayout,
  isLayoutPositionedNode,
  isPartitionedWindow,
  layoutOf,
  parentOfNode,
  syncResizedNodeDimensions,
  type ComputedBox,
  type Scene,
  type SceneNode,
} from "./model.ts";
import {
  formatBends,
  insertLineBend,
  lineSegmentMidpoints,
  parseBends,
  removeLineBend,
  updateLineBend,
  type Point,
  type Rect,
} from "./shapes.ts";

export { insertLineBend, lineSegmentMidpoints, removeLineBend, updateLineBend };

export const MIN_NODE_SIZE = 8;

// ---------- 旋转基础几何 ----------

/** 把角度规整到 (-180, 180] 的整数度数。 */
export function normalizeRotation(deg: number): number {
  if (!Number.isFinite(deg)) return 0;
  let a = Math.round(deg) % 360;
  if (a > 180) a -= 360;
  if (a <= -180) a += 360;
  return a === 0 ? 0 : a;
}

/**
 * 绕中心点拖动旋转手柄时计算新角度。
 * snap15 为真（按住 Shift）时按 15 度取整。
 */
export function computeRotationAngle(
  center: Point,
  pointer: Point,
  startPointer?: Point,
  initialRotation = 0,
  snap15 = false,
): number {
  const currentAngle = (Math.atan2(pointer.y - center.y, pointer.x - center.x) * 180) / Math.PI;
  if (!startPointer) {
    let next = currentAngle + 90;
    if (snap15) next = Math.round(next / 15) * 15;
    return normalizeRotation(next);
  }
  const startAngle = (Math.atan2(startPointer.y - center.y, startPointer.x - center.x) * 180) / Math.PI;
  let next = initialRotation + (currentAngle - startAngle);
  if (snap15) next = Math.round(next / 15) * 15;
  return normalizeRotation(next);
}

/** 给节点写入旋转角度；0 度时删掉字段以保持 scene.json 干净。 */
export function setNodeRotation(node: SceneNode, deg: number): number {
  const angle = normalizeRotation(deg);
  if (angle === 0) delete node.rotation;
  else node.rotation = angle;
  return angle;
}

/** 把点 p 绕 center 顺时针旋转 deg 度。 */
export function rotatePoint(p: Point, center: Point, deg: number): Point {
  if (!deg) return { x: p.x, y: p.y };
  const rad = (deg * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);
  const dx = p.x - center.x;
  const dy = p.y - center.y;
  return {
    x: center.x + dx * cos - dy * sin,
    y: center.y + dx * sin + dy * cos,
  };
}

/** 判断点 pt 是否落在顺时针旋转了 deg 度的矩形 rect 内。 */
export function pointInRotatedRect(pt: Point, rect: Rect, deg = 0): boolean {
  const center = { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
  const local = deg ? rotatePoint(pt, center, -deg) : pt;
  return local.x >= rect.x && local.x <= rect.x + rect.width && local.y >= rect.y && local.y <= rect.y + rect.height;
}

/** 矩形 rect 绕中心顺时针旋转 deg 度后的四个顶点。 */
export function rotatedRectCorners(rect: Rect, deg = 0): [Point, Point, Point, Point] {
  const c = { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
  const raw: [Point, Point, Point, Point] = [
    { x: rect.x, y: rect.y },
    { x: rect.x + rect.width, y: rect.y },
    { x: rect.x + rect.width, y: rect.y + rect.height },
    { x: rect.x, y: rect.y + rect.height },
  ];
  if (!deg) return raw;
  return [
    rotatePoint(raw[0], c, deg),
    rotatePoint(raw[1], c, deg),
    rotatePoint(raw[2], c, deg),
    rotatePoint(raw[3], c, deg),
  ];
}

/** 矩形 rect 绕中心顺时针旋转 deg 度后的轴对齐外包框（AABB）。 */
export function rotatedRectBounds(rect: Rect, deg = 0): Rect {
  if (!deg) return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
  const corners = rotatedRectCorners(rect, deg);
  const xs = corners.map((p) => p.x);
  const ys = corners.map((p) => p.y);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minY = Math.min(...ys);
  const maxY = Math.max(...ys);
  return { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
}

/** 判断轴对齐框 box（如框选框）是否与旋转了 deg 度的矩形 rect 相交（分离轴定理 SAT）。 */
export function rectIntersectsRotatedRect(box: Rect, rect: Rect, deg = 0): boolean {
  if (!deg) {
    return (
      rect.x < box.x + box.width &&
      rect.x + rect.width > box.x &&
      rect.y < box.y + box.height &&
      rect.y + rect.height > box.y
    );
  }
  const polyA: Point[] = [
    { x: box.x, y: box.y },
    { x: box.x + box.width, y: box.y },
    { x: box.x + box.width, y: box.y + box.height },
    { x: box.x, y: box.y + box.height },
  ];
  const polyB = rotatedRectCorners(rect, deg);
  const axes: Point[] = [
    { x: 1, y: 0 },
    { x: 0, y: 1 },
    { x: polyB[1].x - polyB[0].x, y: polyB[1].y - polyB[0].y },
    { x: polyB[3].x - polyB[0].x, y: polyB[3].y - polyB[0].y },
  ];
  for (const axis of axes) {
    const len = Math.hypot(axis.x, axis.y);
    if (len < 1e-9) continue;
    const ux = axis.x / len;
    const uy = axis.y / len;
    let minA = Infinity;
    let maxA = -Infinity;
    for (const p of polyA) {
      const proj = p.x * ux + p.y * uy;
      if (proj < minA) minA = proj;
      if (proj > maxA) maxA = proj;
    }
    let minB = Infinity;
    let maxB = -Infinity;
    for (const p of polyB) {
      const proj = p.x * ux + p.y * uy;
      if (proj < minB) minB = proj;
      if (proj > maxB) maxB = proj;
    }
    if (maxA <= minB || maxB <= minA) return false;
  }
  return true;
}

// ---------- 节点层级与绝对坐标（支持父容器旋转与自动排版/分区计算） ----------

export function parentOfInScene(scene: Scene, id: string): string | null {
  return parentOfNode(scene, id);
}

/** 从节点自身往外到根窗口之前的祖先链 `[id, parentId, ...]`。 */
export function ancestorChainInScene(scene: Scene, id: string): string[] {
  const chain: string[] = [];
  for (let cursor: string | null = id; cursor && cursor !== scene.root; cursor = parentOfInScene(scene, cursor)) {
    if (chain.includes(cursor)) break;
    chain.push(cursor);
  }
  return chain;
}

/** 节点在画布世界坐标系中的累计旋转角度（含祖先容器的旋转）。 */
export function worldRotationInScene(scene: Scene, id: string): number {
  let total = 0;
  for (const curr of ancestorChainInScene(scene, id)) {
    total += scene.nodes[curr]?.rotation ?? 0;
  }
  return normalizeRotation(total);
}

function needsSceneLayoutForChain(scene: Scene, chain: string[]): boolean {
  for (const curr of chain) {
    const n = scene.nodes[curr];
    if (!n) continue;
    const parentId = parentOfInScene(scene, curr);
    if (!parentId && curr !== scene.root) return true;
    if (parentId) {
      const parent = scene.nodes[parentId];
      if (parent && (layoutOf(parent) !== null || isPartitionedWindow(parent) || parent.type === "Dock")) {
        return true;
      }
    }
  }
  return false;
}

function localBoxInParent(scene: Scene, curr: string, boxes?: Map<string, ComputedBox>): Rect {
  const n = scene.nodes[curr];
  if (!n) return { x: 0, y: 0, width: 0, height: 0 };
  if (curr === scene.root) return { x: 0, y: 0, width: n.width, height: n.height };
  const parentId = parentOfInScene(scene, curr);
  if (!parentId) {
    const box = boxes?.get(curr);
    const w = box ? box.width : n.width;
    const h = box ? box.height : n.height;
    const rootNode = scene.nodes[scene.root];
    const cx = n.x ?? (rootNode ? Math.round((rootNode.width - w) / 2) : 0);
    const cy = n.y ?? (rootNode ? Math.round((rootNode.height - h) / 2) : 0);
    return { x: cx, y: cy, width: w, height: h };
  }
  const parent = scene.nodes[parentId];
  const managedByLayout = Boolean(
    parent && (layoutOf(parent) !== null || isPartitionedWindow(parent) || parent.type === "Dock"),
  );
  if (managedByLayout && boxes) {
    const box = boxes.get(curr);
    if (box) return { x: box.x, y: box.y, width: box.width, height: box.height };
  }
  return { x: n.x ?? 0, y: n.y ?? 0, width: n.width, height: n.height };
}

/** 把节点 id 内部局部坐标 `(0..width, 0..height)` 中的点转换到画布世界坐标。 */
export function nodeLocalToWorldInScene(
  scene: Scene,
  id: string,
  localPt: Point,
  layoutBoxes?: Map<string, ComputedBox>,
): Point {
  let pt = { x: localPt.x, y: localPt.y };
  const chain = ancestorChainInScene(scene, id);
  const boxes = layoutBoxes ?? (needsSceneLayoutForChain(scene, chain) ? computeSceneLayout(scene) : undefined);
  for (const curr of chain) {
    const n = scene.nodes[curr];
    if (!n) continue;
    const box = localBoxInParent(scene, curr, boxes);
    if (n.rotation) {
      pt = rotatePoint(pt, { x: box.width / 2, y: box.height / 2 }, n.rotation);
    }
    pt = { x: pt.x + box.x, y: pt.y + box.y };
  }
  return pt;
}

/** 把画布世界坐标中的点转换到节点 id 内部未旋转局部坐标 `(0..width, 0..height)`。 */
export function worldToNodeLocalInScene(
  scene: Scene,
  id: string,
  worldPt: Point,
  layoutBoxes?: Map<string, ComputedBox>,
): Point {
  let pt = { x: worldPt.x, y: worldPt.y };
  const chain = ancestorChainInScene(scene, id);
  const boxes = layoutBoxes ?? (needsSceneLayoutForChain(scene, chain) ? computeSceneLayout(scene) : undefined);
  for (const curr of [...chain].reverse()) {
    const n = scene.nodes[curr];
    if (!n) continue;
    const box = localBoxInParent(scene, curr, boxes);
    pt = { x: pt.x - box.x, y: pt.y - box.y };
    if (n.rotation) {
      pt = rotatePoint(pt, { x: box.width / 2, y: box.height / 2 }, -n.rotation);
    }
  }
  return pt;
}

/** 把世界坐标系中的位移 `(worldDx, worldDy)` 转换到节点 id 所在父容器的局部坐标系增量。 */
export function worldToParentDeltaInScene(scene: Scene, id: string, worldDx: number, worldDy: number): Point {
  const parentId = parentOfInScene(scene, id);
  const parentRot = parentId && parentId !== scene.root ? worldRotationInScene(scene, parentId) : 0;
  if (!parentRot) return { x: worldDx, y: worldDy };
  return rotatePoint({ x: worldDx, y: worldDy }, { x: 0, y: 0 }, -parentRot);
}

/** 把节点在画布世界坐标系中的基准框（以世界中心和节点宽高表示）转换为父容器局部坐标 `(x, y, width, height)`。 */
export function worldRectToNodeLocalInScene(
  scene: Scene,
  id: string,
  worldRect: Rect,
  layoutBoxes?: Map<string, ComputedBox>,
): Rect {
  const parentId = parentOfInScene(scene, id);
  const worldCenter = {
    x: worldRect.x + worldRect.width / 2,
    y: worldRect.y + worldRect.height / 2,
  };
  const localCenter =
    parentId && parentId !== scene.root
      ? worldToNodeLocalInScene(scene, parentId, worldCenter, layoutBoxes)
      : worldCenter;
  return {
    x: Math.round(localCenter.x - worldRect.width / 2),
    y: Math.round(localCenter.y - worldRect.height / 2),
    width: Math.round(worldRect.width),
    height: Math.round(worldRect.height),
  };
}

/** 节点在世界坐标系中的未旋转基准框（以世界中心定位，宽高为节点在当前布局中的有效宽高）。 */
export function absRectInScene(
  scene: Scene,
  id: string,
  layoutBoxes?: Map<string, ComputedBox>,
): Rect {
  const n = scene.nodes[id];
  if (!n) return { x: 0, y: 0, width: 0, height: 0 };
  if (id === scene.root) return { x: 0, y: 0, width: n.width, height: n.height };
  const chain = ancestorChainInScene(scene, id);
  const boxes = layoutBoxes ?? (needsSceneLayoutForChain(scene, chain) ? computeSceneLayout(scene) : undefined);
  const box = localBoxInParent(scene, id, boxes);
  const center = nodeLocalToWorldInScene(scene, id, { x: box.width / 2, y: box.height / 2 }, boxes);
  return {
    x: center.x - box.width / 2,
    y: center.y - box.height / 2,
    width: box.width,
    height: box.height,
  };
}

/** 节点在世界坐标系中计入旋转后的视觉外包框（AABB），供吸附、框选、多选外框、对齐分布使用。 */
export function visualBoundsInScene(
  scene: Scene,
  id: string,
  layoutBoxes?: Map<string, ComputedBox>,
): Rect {
  const rect = absRectInScene(scene, id, layoutBoxes);
  if (id === scene.root) return rect;
  const rot = worldRotationInScene(scene, id);
  return rotatedRectBounds(rect, rot);
}

export function parentOriginInScene(scene: Scene, id: string, layoutBoxes?: Map<string, ComputedBox>): Point {
  const parentId = parentOfInScene(scene, id);
  if (!parentId || parentId === scene.root) return { x: 0, y: 0 };
  const r = absRectInScene(scene, parentId, layoutBoxes);
  return { x: r.x, y: r.y };
}

export function unionRectOf(rects: Rect[]): Rect | null {
  let box: Rect | null = null;
  for (const r of rects) {
    if (!box) box = { ...r };
    else {
      const right = Math.max(box.x + box.width, r.x + r.width);
      const bottom = Math.max(box.y + box.height, r.y + r.height);
      box.x = Math.min(box.x, r.x);
      box.y = Math.min(box.y, r.y);
      box.width = right - box.x;
      box.height = bottom - box.y;
    }
  }
  return box;
}

// ---------- 锁定 ----------

/** 节点自己或任一祖先被锁定时，视为已锁定。根窗口不可锁定。 */
export function isNodeLocked(scene: Scene, id: string): boolean {
  for (let cursor: string | null = id; cursor && cursor !== scene.root; cursor = parentOfInScene(scene, cursor)) {
    if (scene.nodes[cursor]?.locked) return true;
  }
  return false;
}

/** 检查节点自身、祖先或其子树中任一后代是否处于锁定状态。 */
export function hasLockedInSubtree(scene: Scene, id: string): boolean {
  if (isNodeLocked(scene, id)) return true;
  const visit = (curr: string): boolean => {
    const n = scene.nodes[curr];
    if (!n) return false;
    if (n.locked) return true;
    for (const child of childrenOfNode(n)) {
      if (visit(child)) return true;
    }
    return false;
  };
  return visit(id);
}

/** 过滤出未锁定且不是根窗口的节点。 */
export function filterUnlocked(scene: Scene, ids: string[]): string[] {
  return ids.filter((id) => id !== scene.root && Boolean(scene.nodes[id]) && !isNodeLocked(scene, id));
}

/** 查找导致节点 id 被继承锁定的最近祖先容器 ID；若祖先均未锁定则返回 null。 */
export function lockedAncestorInScene(scene: Scene, id: string): string | null {
  for (let cursor = parentOfInScene(scene, id); cursor && cursor !== scene.root; cursor = parentOfInScene(scene, cursor)) {
    if (scene.nodes[cursor]?.locked) return cursor;
  }
  return null;
}

/**
 * 设置一组节点的锁定状态，返回是否有变化。
 * 解锁（locked === false）时，仅解除目标节点自身的锁定标记，绝不静默解开父容器和兄弟节点。
 */
export function setNodesLocked(scene: Scene, ids: string[], locked: boolean): boolean {
  let changed = false;
  for (const id of ids) {
    if (id === scene.root) continue;
    const n = scene.nodes[id];
    if (!n) continue;
    if (locked) {
      if (!n.locked) {
        n.locked = true;
        changed = true;
      }
    } else if (n.locked) {
      delete n.locked;
      changed = true;
    }
  }
  return changed;
}

/**
 * 切换一组节点的锁定状态：按实际生效锁定状态（含父容器继承锁定）判断，
 * 只要有一个未锁定就全部锁定，否则全部解锁。返回切换后的锁定状态。
 */
export function toggleNodesLocked(scene: Scene, ids: string[]): boolean {
  const targets = ids.filter((id) => id !== scene.root && Boolean(scene.nodes[id]));
  if (targets.length === 0) return false;
  const shouldLock = targets.some((id) => !isNodeLocked(scene, id));
  setNodesLocked(scene, targets, shouldLock);
  return shouldLock;
}

// ---------- 缩放与多选整体缩放 ----------

/**
 * 拖动八个方向手柄计算新的外框。
 * keepAspect 为真且拖的是四角手柄时保持原长宽比。
 * rotationDeg 非 0 时，先把屏幕增量 (dx, dy) 逆旋转到节点局部坐标系，并保持对侧边/角在世界坐标中固定不漂移。
 */
export function computeResizeBox(
  orig: Rect,
  handle: string,
  dx: number,
  dy: number,
  keepAspect = false,
  minSize = MIN_NODE_SIZE,
  rotationDeg = 0,
): Rect {
  let ldx = dx;
  let ldy = dy;
  if (rotationDeg) {
    const rad = (-rotationDeg * Math.PI) / 180;
    ldx = dx * Math.cos(rad) - dy * Math.sin(rad);
    ldy = dx * Math.sin(rad) + dy * Math.cos(rad);
  }
  let width = orig.width;
  let height = orig.height;
  if (handle.includes("e")) width = orig.width + ldx;
  if (handle.includes("w")) width = orig.width - ldx;
  if (handle.includes("s")) height = orig.height + ldy;
  if (handle.includes("n")) height = orig.height - ldy;
  if (keepAspect && handle.length === 2) {
    const ratio = orig.width / Math.max(1, orig.height);
    if (Math.abs(width / Math.max(1, orig.width)) >= Math.abs(height / Math.max(1, orig.height))) {
      height = Math.round((width * orig.height) / Math.max(1, orig.width));
    } else {
      width = Math.round(height * ratio);
    }
  }
  width = Math.max(minSize, width);
  height = Math.max(minSize, height);
  if (!rotationDeg) {
    return {
      x: orig.x + (handle.includes("w") ? orig.width - width : 0),
      y: orig.y + (handle.includes("n") ? orig.height - height : 0),
      width,
      height,
    };
  }
  const dW = width - orig.width;
  const dH = height - orig.height;
  const localShift = {
    x: handle.includes("e") ? dW / 2 : handle.includes("w") ? -dW / 2 : 0,
    y: handle.includes("s") ? dH / 2 : handle.includes("n") ? -dH / 2 : 0,
  };
  const worldShift = rotatePoint(localShift, { x: 0, y: 0 }, rotationDeg);
  const origCenter = { x: orig.x + orig.width / 2, y: orig.y + orig.height / 2 };
  const newCenter = { x: origCenter.x + worldShift.x, y: origCenter.y + worldShift.y };
  return {
    x: newCenter.x - width / 2,
    y: newCenter.y - height / 2,
    width,
    height,
  };
}

export interface MultiResizeItem {
  id: string;
  abs: Rect;
  parentOrigin: Point;
  isLine?: boolean;
  lineProps?: Record<string, Value>;
}

export interface ScaledNodeResult {
  x: number;
  y: number;
  width: number;
  height: number;
  props?: Record<string, Value>;
}

const numVal = (v: Value | undefined, fallback: number): number =>
  typeof v === "number" && Number.isFinite(v) ? v : fallback;

/**
 * 多选整体缩放：按整体外框从 startBounds 到 nextBounds 的比例，
 * 缩放每个选中节点的位置和宽高（以及未挂接线条的端点与拐点）。
 */
export function scaleGroupItems(
  items: MultiResizeItem[],
  startBounds: Rect,
  nextBounds: Rect,
  minSize = MIN_NODE_SIZE,
  scene?: Scene,
): Map<string, ScaledNodeResult> {
  const sx = startBounds.width > 0 ? nextBounds.width / startBounds.width : 1;
  const sy = startBounds.height > 0 ? nextBounds.height / startBounds.height : 1;
  const mapX = (ax: number) => nextBounds.x + (ax - startBounds.x) * sx;
  const mapY = (ay: number) => nextBounds.y + (ay - startBounds.y) * sy;

  const out = new Map<string, ScaledNodeResult>();
  for (const item of items) {
    const nx = mapX(item.abs.x);
    const ny = mapY(item.abs.y);
    const nw = Math.max(minSize, Math.round(item.abs.width * sx));
    const nh = Math.max(minSize, Math.round(item.abs.height * sy));
    const local = scene
      ? worldRectToNodeLocalInScene(scene, item.id, { x: nx, y: ny, width: nw, height: nh })
      : {
          x: Math.round(nx - item.parentOrigin.x),
          y: Math.round(ny - item.parentOrigin.y),
          width: nw,
          height: nh,
        };
    const result: ScaledNodeResult = {
      x: local.x,
      y: local.y,
      width: local.width,
      height: local.height,
    };
    if (item.isLine && item.lineProps) {
      const p = { ...item.lineProps };
      const ox = item.parentOrigin.x;
      const oy = item.parentOrigin.y;
      const mapLinePt = (lx: number, ly: number): Point => {
        const parentId = scene ? parentOfInScene(scene, item.id) : null;
        if (scene && parentId && parentId !== scene.root) {
          const wpt = nodeLocalToWorldInScene(scene, parentId, { x: lx, y: ly });
          const nextW = { x: mapX(wpt.x), y: mapY(wpt.y) };
          return worldToNodeLocalInScene(scene, parentId, nextW);
        }
        return { x: mapX(ox + lx) - ox, y: mapY(oy + ly) - oy };
      };
      if (!p.startNode) {
        const pt = mapLinePt(numVal(p.x1, 0), numVal(p.y1, 0));
        p.x1 = Math.round(pt.x);
        p.y1 = Math.round(pt.y);
      }
      if (!p.endNode) {
        const pt = mapLinePt(numVal(p.x2, 160), numVal(p.y2, 0));
        p.x2 = Math.round(pt.x);
        p.y2 = Math.round(pt.y);
      }
      const bends = parseBends(p.bends);
      if (bends.length > 0) {
        p.bends = formatBends(bends.map((b) => mapLinePt(b.x, b.y)));
      }
      result.props = p;
    }
    out.set(item.id, result);
  }
  return out;
}

/**
 * 直接在 Scene 上对多选节点执行整体缩放，返回缩放后的整体外框。
 * 拖动四角手柄（handle.length === 2）时默认保持整体长宽比，按住 Shift 可自由变形。
 */
export function applyGroupResize(
  scene: Scene,
  ids: string[],
  handle: string,
  dx: number,
  dy: number,
  keepAspect = handle.length === 2,
  minSize = MIN_NODE_SIZE,
): Rect | null {
  const unlocked = filterUnlocked(scene, ids);
  if (unlocked.length === 0) return null;
  const items: MultiResizeItem[] = unlocked.map((id) => {
    const n = scene.nodes[id];
    return {
      id,
      abs: absRectInScene(scene, id),
      parentOrigin: parentOriginInScene(scene, id),
      isLine: n.type === "Line",
      lineProps: n.type === "Line" ? { ...n.props } : undefined,
    };
  });
  const startBounds = unionRectOf(unlocked.map((id) => visualBoundsInScene(scene, id)));
  if (!startBounds) return null;
  const nextBounds = computeResizeBox(startBounds, handle, dx, dy, keepAspect, minSize);
  const scaled = scaleGroupItems(items, startBounds, nextBounds, minSize, scene);
  for (const [id, res] of scaled) {
    const n = scene.nodes[id];
    if (!n) continue;
    if (isLayoutPositionedNode(scene, id)) {
      delete n.x;
      delete n.y;
    } else {
      n.x = res.x;
      n.y = res.y;
    }
    syncResizedNodeDimensions(n, res.width, res.height, minSize, scene, id);
    if (res.props) n.props = res.props;
  }
  return nextBounds;
}

// ---------- 对齐和分布 ----------

export type AlignKind = "left" | "centerH" | "right" | "top" | "centerV" | "bottom";
export type DistributeAxis = "horizontal" | "vertical";

export interface AlignItem {
  id: string;
  rect: Rect;
  parentOrigin?: Point;
}

/**
 * 计算一组方框对齐后的位置（返回相对于各自 parentOrigin 的 x、y）。
 * 未给 referenceBounds 时，以这组方框的合围外框为基准。
 */
export function alignRects(
  items: AlignItem[],
  kind: AlignKind,
  referenceBounds?: Rect,
): Map<string, { x: number; y: number }> {
  const result = new Map<string, { x: number; y: number }>();
  if (items.length === 0) return result;
  const bounds = referenceBounds ?? unionRectOf(items.map((item) => item.rect))!;
  for (const item of items) {
    const origin = item.parentOrigin ?? { x: 0, y: 0 };
    let absX = item.rect.x;
    let absY = item.rect.y;
    switch (kind) {
      case "left":
        absX = bounds.x;
        break;
      case "centerH":
        absX = bounds.x + bounds.width / 2 - item.rect.width / 2;
        break;
      case "right":
        absX = bounds.x + bounds.width - item.rect.width;
        break;
      case "top":
        absY = bounds.y;
        break;
      case "centerV":
        absY = bounds.y + bounds.height / 2 - item.rect.height / 2;
        break;
      case "bottom":
        absY = bounds.y + bounds.height - item.rect.height;
        break;
    }
    result.set(item.id, {
      x: Math.round(absX - origin.x),
      y: Math.round(absY - origin.y),
    });
  }
  return result;
}

/**
 * 计算一组方框（至少 3 个）在水平或垂直方向等距分布后的位置。
 * 最左/最上与最右/最下的外沿保持不动，中间各节点的间距相等。
 */
export function distributeRects(
  items: AlignItem[],
  axis: DistributeAxis,
): Map<string, { x: number; y: number }> {
  const result = new Map<string, { x: number; y: number }>();
  if (items.length < 3) {
    for (const item of items) {
      const origin = item.parentOrigin ?? { x: 0, y: 0 };
      result.set(item.id, { x: Math.round(item.rect.x - origin.x), y: Math.round(item.rect.y - origin.y) });
    }
    return result;
  }
  const startOf = (r: Rect) => (axis === "horizontal" ? r.x : r.y);
  const sizeOf = (r: Rect) => (axis === "horizontal" ? r.width : r.height);
  const centerOf = (r: Rect) => startOf(r) + sizeOf(r) / 2;

  const sorted = [...items].sort((a, b) => centerOf(a.rect) - centerOf(b.rect) || startOf(a.rect) - startOf(b.rect));
  const minStart = Math.min(...sorted.map((item) => startOf(item.rect)));
  const maxEnd = Math.max(...sorted.map((item) => startOf(item.rect) + sizeOf(item.rect)));
  const totalSize = sorted.reduce((sum, item) => sum + sizeOf(item.rect), 0);
  const gap = (maxEnd - minStart - totalSize) / (sorted.length - 1);

  let cursor = minStart;
  for (const item of sorted) {
    const origin = item.parentOrigin ?? { x: 0, y: 0 };
    const absX = axis === "horizontal" ? cursor : item.rect.x;
    const absY = axis === "vertical" ? cursor : item.rect.y;
    result.set(item.id, {
      x: Math.round(absX - origin.x),
      y: Math.round(absY - origin.y),
    });
    cursor += sizeOf(item.rect) + gap;
  }
  return result;
}

/**
 * 在 Scene 上对齐选中的节点（要求至少 2 个未锁定、非线条且非排版自动定位的节点，按旋转后的视觉外框对齐）。
 */
export function alignSceneNodes(scene: Scene, ids: string[], kind: AlignKind): boolean {
  const unlocked = filterUnlocked(scene, ids).filter(
    (id) => scene.nodes[id]?.type !== "Line" && !isLayoutPositionedNode(scene, id),
  );
  if (unlocked.length < 2) return false;
  const visualMap = new Map<string, Rect>();
  const items: AlignItem[] = unlocked.map((id) => {
    const v = visualBoundsInScene(scene, id);
    visualMap.set(id, v);
    return { id, rect: v };
  });
  const next = alignRects(items, kind);
  let changed = false;
  for (const [id, pos] of next) {
    const n = scene.nodes[id];
    const oldV = visualMap.get(id);
    if (!n || !oldV) continue;
    const wdx = pos.x - oldV.x;
    const wdy = pos.y - oldV.y;
    const pDelta = worldToParentDeltaInScene(scene, id, wdx, wdy);
    const nx = Math.round((n.x ?? 0) + pDelta.x);
    const ny = Math.round((n.y ?? 0) + pDelta.y);
    if ((n.x ?? 0) !== nx || (n.y ?? 0) !== ny) {
      n.x = nx;
      n.y = ny;
      changed = true;
    }
  }
  return changed;
}

/** 在 Scene 上等距分布选中的节点（至少 3 个未锁定、非线条且非排版自动定位的节点，按旋转后的视觉外框等距分布）。 */
export function distributeSceneNodes(scene: Scene, ids: string[], axis: DistributeAxis): boolean {
  const unlocked = filterUnlocked(scene, ids).filter(
    (id) => scene.nodes[id]?.type !== "Line" && !isLayoutPositionedNode(scene, id),
  );
  if (unlocked.length < 3) return false;
  const visualMap = new Map<string, Rect>();
  const items: AlignItem[] = unlocked.map((id) => {
    const v = visualBoundsInScene(scene, id);
    visualMap.set(id, v);
    return { id, rect: v };
  });
  const next = distributeRects(items, axis);
  let changed = false;
  for (const [id, pos] of next) {
    const n = scene.nodes[id];
    const oldV = visualMap.get(id);
    if (!n || !oldV) continue;
    const wdx = pos.x - oldV.x;
    const wdy = pos.y - oldV.y;
    const pDelta = worldToParentDeltaInScene(scene, id, wdx, wdy);
    const nx = Math.round((n.x ?? 0) + pDelta.x);
    const ny = Math.round((n.y ?? 0) + pDelta.y);
    if ((n.x ?? 0) !== nx || (n.y ?? 0) !== ny) {
      n.x = nx;
      n.y = ny;
      changed = true;
    }
  }
  return changed;
}

// ---------- 直线和曲线拐点 ----------

/** 在线条的 props.bends 中第 segmentIndex 段插入一个拐点（相对于父节点的坐标）。 */
export function addBendToLine(
  props: Record<string, Value> | undefined,
  segmentIndex: number,
  point: Point,
  origin: Point = { x: 0, y: 0 },
): Record<string, Value> {
  const current = parseBends(props?.bends);
  const next = insertLineBend(current, segmentIndex, { x: point.x - origin.x, y: point.y - origin.y });
  return { ...(props ?? {}), bends: formatBends(next) };
}

/** 移动线条的 props.bends 中第 bendIndex 个拐点。 */
export function moveBendOnLine(
  props: Record<string, Value> | undefined,
  bendIndex: number,
  point: Point,
  origin: Point = { x: 0, y: 0 },
): Record<string, Value> {
  const current = parseBends(props?.bends);
  const next = updateLineBend(current, bendIndex, { x: point.x - origin.x, y: point.y - origin.y });
  return { ...(props ?? {}), bends: formatBends(next) };
}

/** 删除线条的 props.bends 中第 bendIndex 个拐点。 */
export function deleteBendFromLine(props: Record<string, Value> | undefined, bendIndex: number): Record<string, Value> {
  const current = parseBends(props?.bends);
  const next = removeLineBend(current, bendIndex);
  const copy: Record<string, Value> = { ...(props ?? {}) };
  if (next.length > 0) copy.bends = formatBends(next);
  else delete copy.bends;
  return copy;
}
