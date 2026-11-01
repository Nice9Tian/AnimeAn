// Qt 停靠（QMainWindow + QDockWidget 规则）：
// 1. 同一份停靠排列描述（DockLayoutSpec）既可存入项目的窗口节点，也在运行时与 Qt 导出侧共用；
// 2. 支持四个停靠区（left / right / top / bottom）、区内并排（split）、叠成页签（tabify）、拖出成浮窗（floating）、
//    从浮窗恢复（restore）、关闭后从菜单重新打开、拖动分隔条调整停靠区或并排面板大小；
// 3. 提供预览模式下的 DOM 交互（拖动标题栏或页签吸附/并排/叠页签/浮出、分隔条拖拽改大小、浮窗拖拽与缩放）。

import { dockMinHeightForWidth, prefHeight, prefWidth, type ComputedBox, type Scene, type SceneNode } from "./model.ts";

export type DockArea = "left" | "right" | "top" | "bottom";
export type DockSplitOrientation = "horizontal" | "vertical";

export const DOCK_AREAS: readonly DockArea[] = ["top", "bottom", "left", "right"];

export interface DockFloatRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface DockGroupSpec {
  /** 叠在该槽位的停靠面板 ID 列表（长度 >= 2 表示叠成页签） */
  tabs: string[];
  /** 当前激活显示的页签面板 ID */
  active?: string;
  /** 在并排（split）方向上的像素尺寸（拖动并排分隔条时更新） */
  size?: number;
}

export interface DockAreaSpec {
  /** 停靠区厚度（left/right 为宽度 px，top/bottom 为高度 px） */
  size?: number;
  /** 区内多组并排方向：left/right 默认为 "vertical"（上下并排）或 "horizontal"；top/bottom 默认为 "horizontal"（左右并排） */
  splitOrientation?: DockSplitOrientation;
  /** 该停靠区内的停靠组列表 */
  groups: DockGroupSpec[];
}

export interface DockFloatingSpec {
  dockId: string;
  /** 浮出前所在的停靠区，用于 restore 恢复 */
  lastArea: DockArea;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface DockLayoutSpec {
  areas: Record<DockArea, DockAreaSpec>;
  floating?: DockFloatingSpec[];
}

export interface DockSplitterRect {
  kind: "area" | "split";
  area: DockArea;
  groupIndex?: number;
  orientation: "horizontal" | "vertical";
  x: number;
  y: number;
  width: number;
  height: number;
  currentSize: number;
}

export interface ComputedDockWindowLayout {
  dockBoxes: Map<string, ComputedBox>;
  centralBox: { x: number; y: number; width: number; height: number };
  splitters: DockSplitterRect[];
  activeTabByDock: Map<string, { groupTabs: string[]; activeTab: string; isFloating: boolean; area?: DockArea }>;
}

export function isDockArea(val: unknown): val is DockArea {
  return val === "left" || val === "right" || val === "top" || val === "bottom";
}

export function defaultSplitOrientation(area: DockArea): DockSplitOrientation {
  return area === "left" || area === "right" ? "vertical" : "horizontal";
}

export function cloneDockLayout(layout: DockLayoutSpec): DockLayoutSpec {
  return {
    areas: {
      top: {
        ...(layout.areas.top?.size !== undefined ? { size: layout.areas.top.size } : {}),
        ...(layout.areas.top?.splitOrientation ? { splitOrientation: layout.areas.top.splitOrientation } : {}),
        groups: (layout.areas.top?.groups ?? []).map((g) => ({
          tabs: [...g.tabs],
          ...(g.active ? { active: g.active } : {}),
          ...(g.size !== undefined ? { size: g.size } : {}),
        })),
      },
      bottom: {
        ...(layout.areas.bottom?.size !== undefined ? { size: layout.areas.bottom.size } : {}),
        ...(layout.areas.bottom?.splitOrientation ? { splitOrientation: layout.areas.bottom.splitOrientation } : {}),
        groups: (layout.areas.bottom?.groups ?? []).map((g) => ({
          tabs: [...g.tabs],
          ...(g.active ? { active: g.active } : {}),
          ...(g.size !== undefined ? { size: g.size } : {}),
        })),
      },
      left: {
        ...(layout.areas.left?.size !== undefined ? { size: layout.areas.left.size } : {}),
        ...(layout.areas.left?.splitOrientation ? { splitOrientation: layout.areas.left.splitOrientation } : {}),
        groups: (layout.areas.left?.groups ?? []).map((g) => ({
          tabs: [...g.tabs],
          ...(g.active ? { active: g.active } : {}),
          ...(g.size !== undefined ? { size: g.size } : {}),
        })),
      },
      right: {
        ...(layout.areas.right?.size !== undefined ? { size: layout.areas.right.size } : {}),
        ...(layout.areas.right?.splitOrientation ? { splitOrientation: layout.areas.right.splitOrientation } : {}),
        groups: (layout.areas.right?.groups ?? []).map((g) => ({
          tabs: [...g.tabs],
          ...(g.active ? { active: g.active } : {}),
          ...(g.size !== undefined ? { size: g.size } : {}),
        })),
      },
    },
    floating: (layout.floating ?? []).map((f) => ({ ...f })),
  };
}

/** 提取 DockLayoutSpec 中引用的全部停靠面板 ID（保持出现顺序且去重）。 */
export function dockIdsOfLayout(layout: DockLayoutSpec | undefined): string[] {
  if (!layout) return [];
  const list: string[] = [];
  const add = (id: string | undefined) => {
    if (id && !list.includes(id)) list.push(id);
  };
  for (const area of DOCK_AREAS) {
    for (const g of layout.areas?.[area]?.groups ?? []) {
      for (const tid of g.tabs ?? []) add(tid);
    }
  }
  for (const f of layout.floating ?? []) add(f.dockId);
  return list;
}

/** 从 DockLayoutSpec 中移除被删除的节点 ID。 */
export function removeDockLayoutReferences(layout: DockLayoutSpec | undefined, removed: ReadonlySet<string>): void {
  if (!layout) return;
  for (const area of DOCK_AREAS) {
    const areaSpec = layout.areas?.[area];
    if (!areaSpec?.groups) continue;
    const nextGroups: DockGroupSpec[] = [];
    for (const g of areaSpec.groups) {
      const tabs = (g.tabs ?? []).filter((tid) => !removed.has(tid));
      if (tabs.length === 0) continue;
      const active = g.active && tabs.includes(g.active) ? g.active : tabs[0];
      nextGroups.push({
        tabs,
        active,
        ...(g.size !== undefined ? { size: g.size } : {}),
      });
    }
    areaSpec.groups = nextGroups;
  }
  if (layout.floating) {
    layout.floating = layout.floating.filter((f) => !removed.has(f.dockId));
  }
}

/** 将 DockLayoutSpec 中的 oldId 重命名为 nextId。 */
export function renameDockLayoutReferences(layout: DockLayoutSpec | undefined, oldId: string, nextId: string): void {
  if (!layout) return;
  for (const area of DOCK_AREAS) {
    for (const g of layout.areas?.[area]?.groups ?? []) {
      if (g.tabs) g.tabs = g.tabs.map((tid) => (tid === oldId ? nextId : tid));
      if (g.active === oldId) g.active = nextId;
    }
  }
  if (layout.floating) {
    for (const f of layout.floating) {
      if (f.dockId === oldId) f.dockId = nextId;
    }
  }
}

/** 按映射函数重写 DockLayoutSpec 中的面板 ID。 */
export function remapDockLayoutReferences(
  layout: DockLayoutSpec | undefined,
  mapId: (oldId: string) => string | undefined,
): void {
  if (!layout) return;
  for (const area of DOCK_AREAS) {
    const areaSpec = layout.areas?.[area];
    if (!areaSpec?.groups) continue;
    const nextGroups: DockGroupSpec[] = [];
    for (const g of areaSpec.groups) {
      const tabs = (g.tabs ?? []).map(mapId).filter((tid): tid is string => Boolean(tid));
      if (tabs.length === 0) continue;
      const mappedActive = g.active ? mapId(g.active) : undefined;
      nextGroups.push({
        tabs,
        active: mappedActive && tabs.includes(mappedActive) ? mappedActive : tabs[0],
        ...(g.size !== undefined ? { size: g.size } : {}),
      });
    }
    areaSpec.groups = nextGroups;
  }
  if (layout.floating) {
    const nextFloating: DockFloatingSpec[] = [];
    for (const f of layout.floating) {
      const mapped = mapId(f.dockId);
      if (mapped) nextFloating.push({ ...f, dockId: mapped });
    }
    layout.floating = nextFloating;
  }
}

/** 收集主窗口下的所有有效 Dock 节点 ID。 */
export function windowDockIds(scene: Scene, winNode: SceneNode | undefined): string[] {
  if (!winNode) return [];
  const raw = [
    ...(winNode.docks ?? []),
    ...dockIdsOfLayout(winNode.dockLayout),
    ...(winNode.children ?? []).filter((cid) => scene.nodes[cid]?.type === "Dock"),
  ];
  const out: string[] = [];
  for (const id of raw) {
    if (scene.nodes[id]?.type === "Dock" && !out.includes(id)) out.push(id);
  }
  return out;
}

/**
 * 将窗口节点上的 dockLayout（或回退自 docks + dockArea）归一化为完整的 DockLayoutSpec。
 */
export function normalizeDockLayout(scene: Scene, winNode: SceneNode | undefined): DockLayoutSpec {
  const validIds = windowDockIds(scene, winNode);
  const validSet = new Set(validIds);
  const seen = new Set<string>();

  const areas: Record<DockArea, DockAreaSpec> = {
    top: { groups: [] },
    bottom: { groups: [] },
    left: { groups: [] },
    right: { groups: [] },
  };
  const floating: DockFloatingSpec[] = [];

  const rawLayout = winNode?.dockLayout;
  if (rawLayout?.areas) {
    for (const area of DOCK_AREAS) {
      const rawArea = rawLayout.areas[area];
      if (!rawArea) continue;
      if (typeof rawArea.size === "number" && Number.isFinite(rawArea.size) && rawArea.size > 0) {
        areas[area].size = Math.round(rawArea.size);
      }
      if (rawArea.splitOrientation === "horizontal" || rawArea.splitOrientation === "vertical") {
        areas[area].splitOrientation = rawArea.splitOrientation;
      }
      for (const rawGroup of rawArea.groups ?? []) {
        const tabs: string[] = [];
        for (const tid of rawGroup.tabs ?? []) {
          if (validSet.has(tid) && !seen.has(tid)) {
            seen.add(tid);
            tabs.push(tid);
          }
        }
        if (tabs.length === 0) continue;
        const active = rawGroup.active && tabs.includes(rawGroup.active) ? rawGroup.active : tabs[0];
        const group: DockGroupSpec = { tabs, active };
        if (typeof rawGroup.size === "number" && Number.isFinite(rawGroup.size) && rawGroup.size > 0) {
          group.size = Math.round(rawGroup.size);
        }
        areas[area].groups.push(group);
      }
    }
  }

  if (Array.isArray(rawLayout?.floating)) {
    for (const rf of rawLayout.floating) {
      if (!rf || !validSet.has(rf.dockId) || seen.has(rf.dockId)) continue;
      seen.add(rf.dockId);
      const dNode = scene.nodes[rf.dockId]!;
      const fallbackArea: DockArea = isDockArea(dNode.dockArea) ? dNode.dockArea : "left";
      floating.push({
        dockId: rf.dockId,
        lastArea: isDockArea(rf.lastArea) ? rf.lastArea : fallbackArea,
        x: Math.round(Number.isFinite(rf.x) ? rf.x : 80),
        y: Math.round(Number.isFinite(rf.y) ? rf.y : 90),
        width: Math.max(80, Math.round(Number.isFinite(rf.width) ? rf.width : prefWidth(dNode))),
        height: Math.max(60, Math.round(Number.isFinite(rf.height) ? rf.height : prefHeight(dNode))),
      });
    }
  }

  // 未出现在 dockLayout 中的 Dock 节点按各自的 dockArea 追加
  for (const did of validIds) {
    if (seen.has(did)) continue;
    seen.add(did);
    const dNode = scene.nodes[did]!;
    const area: DockArea = isDockArea(dNode.dockArea) ? dNode.dockArea : "left";
    areas[area].groups.push({ tabs: [did], active: did });
  }

  // 补齐每个非空停靠区的默认厚度 size，并清理空区/单组区的多余字段
  const out: DockLayoutSpec = { areas, floating };
  for (const area of DOCK_AREAS) {
    const areaSpec = areas[area];
    if (areaSpec.groups.length === 0) {
      delete areaSpec.size;
      delete areaSpec.splitOrientation;
      continue;
    }
    if (areaSpec.size === undefined) {
      const firstId = areaSpec.groups[0].active ?? areaSpec.groups[0].tabs[0];
      const firstNode = firstId ? scene.nodes[firstId] : undefined;
      if (firstNode) {
        areaSpec.size = area === "left" || area === "right" ? prefWidth(firstNode) : prefHeight(firstNode);
      }
    }
    if (areaSpec.groups.length === 1) {
      delete areaSpec.splitOrientation;
      delete areaSpec.groups[0].size;
    } else {
      const orient = areaSpec.splitOrientation ?? defaultSplitOrientation(area);
      areaSpec.splitOrientation = orient;
      const refSpan = computeReferenceAreaSplitSpan(out, scene, winNode, area);
      const isPerpendicularSplit = orient === defaultSplitOrientation(area);
      const curSum = areaSpec.groups.reduce((acc, g) => acc + (g.size ?? 0), 0);
      if (areaSpec.groups.some((g) => g.size === undefined) || (isPerpendicularSplit && Math.abs(curSum - refSpan) > 1)) {
        const groupEntries = areaSpec.groups.map((group) => {
          const activeId = group.active && group.tabs.includes(group.active) ? group.active : group.tabs[0];
          const activeDock = (activeId ? scene.nodes[activeId] : undefined) ?? ({ type: "Dock", width: 240, height: 280 } as SceneNode);
          return { group, activeDock };
        });
        const distributed = distributeGroupAxisSizes(refSpan, groupEntries, orient);
        for (let gi = 0; gi < areaSpec.groups.length; gi++) {
          areaSpec.groups[gi].size = distributed[gi];
        }
      }
      if (!isPerpendicularSplit) {
        areaSpec.size = areaSpec.groups.reduce((acc, g) => acc + (g.size ?? 0), 0);
      }
    }
  }

  return out;
}

export type DockLocation =
  | { kind: "area"; area: DockArea; groupIndex: number; tabIndex: number; group: DockGroupSpec }
  | { kind: "floating"; index: number; item: DockFloatingSpec };

export function dockFindLocation(layout: DockLayoutSpec, dockId: string): DockLocation | null {
  for (const area of DOCK_AREAS) {
    const groups = layout.areas[area]?.groups ?? [];
    for (let gi = 0; gi < groups.length; gi++) {
      const g = groups[gi];
      const ti = g.tabs.indexOf(dockId);
      if (ti >= 0) {
        return { kind: "area", area, groupIndex: gi, tabIndex: ti, group: g };
      }
    }
  }
  const floating = layout.floating ?? [];
  for (let fi = 0; fi < floating.length; fi++) {
    if (floating[fi].dockId === dockId) {
      return { kind: "floating", index: fi, item: floating[fi] };
    }
  }
  return null;
}

/**
 * 将 DockLayoutSpec 写回窗口节点的 dockLayout，并同步更新 winNode.docks 顺序与各 Dock 节点的 dockArea / 尺寸，
 * 确保 layoutedit 与 dock.ts 共用并维护同一份停靠描述。
 */
export function syncSceneNodesFromDockLayout(
  scene: Scene,
  winNode: SceneNode | undefined,
  layout?: DockLayoutSpec,
): DockLayoutSpec {
  const normalized = normalizeDockLayout(
    scene,
    layout
      ? {
          ...winNode,
          type: "Window",
          width: winNode?.width ?? 1280,
          height: winNode?.height ?? 800,
          dockLayout: layout,
        }
      : winNode,
  );
  if (!winNode) return normalized;
  winNode.dockLayout = cloneDockLayout(normalized);
  for (const area of DOCK_AREAS) {
    const areaSpec = normalized.areas[area];
    const orient = areaSpec.splitOrientation ?? defaultSplitOrientation(area);
    for (const g of areaSpec.groups) {
      for (const tid of g.tabs) {
        const d = scene.nodes[tid];
        if (!d || d.type !== "Dock") continue;
        d.dockArea = area;
        if (area === "left" || area === "right") {
          if (orient === "vertical" && areaSpec.size !== undefined) {
            d.width = areaSpec.size;
          } else if (orient === "horizontal" && g.size !== undefined) {
            d.width = g.size;
          }
        } else {
          if (orient === "horizontal" && areaSpec.size !== undefined) {
            d.height = areaSpec.size;
          } else if (orient === "vertical" && g.size !== undefined) {
            d.height = g.size;
          }
        }
      }
    }
  }
  for (const f of normalized.floating ?? []) {
    const d = scene.nodes[f.dockId];
    if (!d || d.type !== "Dock") continue;
    d.dockArea = f.lastArea;
  }
  winNode.docks = dockIdsOfLayout(normalized);
  return winNode.dockLayout;
}

function detachDockFromLayout(layout: DockLayoutSpec, dockId: string): { lastArea?: DockArea } {
  const loc = dockFindLocation(layout, dockId);
  if (!loc) return {};
  if (loc.kind === "floating") {
    const lastArea = loc.item.lastArea;
    layout.floating = (layout.floating ?? []).filter((f) => f.dockId !== dockId);
    return { lastArea };
  }
  const areaSpec = layout.areas[loc.area];
  const group = areaSpec.groups[loc.groupIndex];
  group.tabs = group.tabs.filter((id) => id !== dockId);
  if (group.tabs.length === 0) {
    areaSpec.groups.splice(loc.groupIndex, 1);
    if (areaSpec.groups.length === 0) {
      delete areaSpec.size;
      delete areaSpec.splitOrientation;
    } else if (areaSpec.groups.length === 1) {
      delete areaSpec.splitOrientation;
      delete areaSpec.groups[0].size;
    }
  } else if (group.active === dockId || !group.active || !group.tabs.includes(group.active)) {
    group.active = group.tabs[Math.min(loc.tabIndex, group.tabs.length - 1)];
  }
  return { lastArea: loc.area };
}

/** 将 dockId 叠入 targetDockId 所在的页签组，并激活 dockId。 */
export function dockTabify(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  targetDockId: string,
  dockId: string,
): DockLayoutSpec {
  const next = normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: cloneDockLayout(layout) });
  if (targetDockId === dockId) return next;
  const targetLocBefore = dockFindLocation(next, targetDockId);
  if (!targetLocBefore) return next;
  if (targetLocBefore.kind === "floating") {
    // 若目标面板当前处于浮窗，先恢复到其停靠区再叠页签
    const restored = dockRestore(next, scene, winNode, targetDockId);
    return dockTabify(restored, scene, winNode, targetDockId, dockId);
  }
  detachDockFromLayout(next, dockId);
  const targetLoc = dockFindLocation(next, targetDockId);
  if (!targetLoc || targetLoc.kind !== "area") return next;
  if (!targetLoc.group.tabs.includes(dockId)) {
    targetLoc.group.tabs.push(dockId);
  }
  targetLoc.group.active = dockId;
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 切换页签组内的当前激活页签。 */
export function dockActivateTab(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  dockId: string,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const loc = dockFindLocation(next, dockId);
  if (loc && loc.kind === "area") {
    loc.group.active = dockId;
  }
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 将停靠面板拖出/切换为浮窗（或更新浮窗位置和尺寸）。 */
export function dockFloat(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  dockId: string,
  rect?: Partial<DockFloatRect>,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const dNode = scene.nodes[dockId];
  if (!dNode || dNode.type !== "Dock") return next;
  const existingLoc = dockFindLocation(next, dockId);
  const prevFloat = existingLoc?.kind === "floating" ? existingLoc.item : undefined;
  const { lastArea } = detachDockFromLayout(next, dockId);
  const resolvedArea: DockArea =
    lastArea ?? (isDockArea(dNode.dockArea) ? dNode.dockArea : "left");

  const defW = prevFloat?.width ?? (resolvedArea === "left" || resolvedArea === "right" ? prefWidth(dNode) : Math.min(480, prefWidth(dNode)));
  const defH = prevFloat?.height ?? (resolvedArea === "top" || resolvedArea === "bottom" ? Math.max(120, prefHeight(dNode)) : Math.min(440, prefHeight(dNode)));

  const item: DockFloatingSpec = {
    dockId,
    lastArea: resolvedArea,
    x: Math.round(rect?.x ?? prevFloat?.x ?? 80),
    y: Math.round(rect?.y ?? prevFloat?.y ?? 90),
    width: Math.max(80, Math.round(rect?.width ?? defW)),
    height: Math.max(60, Math.round(rect?.height ?? defH)),
  };
  next.floating = [...(next.floating ?? []), item];
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 将浮窗恢复回原来的停靠区（或指定停靠区）。 */
export function dockRestore(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  dockId: string,
  targetArea?: DockArea,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const dNode = scene.nodes[dockId];
  if (!dNode || dNode.type !== "Dock") return next;
  const { lastArea } = detachDockFromLayout(next, dockId);
  const area: DockArea = targetArea ?? lastArea ?? (isDockArea(dNode.dockArea) ? dNode.dockArea : "left");
  next.areas[area].groups.push({ tabs: [dockId], active: dockId });
  if (next.areas[area].size === undefined) {
    next.areas[area].size = area === "left" || area === "right" ? prefWidth(dNode) : prefHeight(dNode);
  }
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 将面板移动/吸附到指定停靠区。 */
export function dockMoveToArea(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  dockId: string,
  area: DockArea,
  index?: number,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const dNode = scene.nodes[dockId];
  if (!dNode || dNode.type !== "Dock") return next;
  detachDockFromLayout(next, dockId);
  const groups = next.areas[area].groups;
  const newGroup: DockGroupSpec = { tabs: [dockId], active: dockId };
  if (index !== undefined && index >= 0 && index <= groups.length) {
    groups.splice(index, 0, newGroup);
  } else {
    groups.push(newGroup);
  }
  if (next.areas[area].size === undefined) {
    next.areas[area].size = area === "left" || area === "right" ? prefWidth(dNode) : prefHeight(dNode);
  }
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 将 dockId 与 targetDockId 在同一停靠区内并排放置（split）。 */
export function dockSplit(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  targetDockId: string,
  dockId: string,
  position: "before" | "after" = "after",
  orientation?: DockSplitOrientation,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  if (targetDockId === dockId) return next;
  const targetLocBefore = dockFindLocation(next, targetDockId);
  if (!targetLocBefore || targetLocBefore.kind !== "area") return next;
  const area = targetLocBefore.area;
  detachDockFromLayout(next, dockId);
  const targetLoc = dockFindLocation(next, targetDockId);
  if (!targetLoc || targetLoc.kind !== "area") {
    return dockMoveToArea(next, scene, winNode, dockId, area);
  }
  const areaSpec = next.areas[area];
  areaSpec.splitOrientation = orientation ?? areaSpec.splitOrientation ?? defaultSplitOrientation(area);
  const insertIdx = position === "before" ? targetLoc.groupIndex : targetLoc.groupIndex + 1;
  areaSpec.groups.splice(insertIdx, 0, { tabs: [dockId], active: dockId });
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

/** 拖动停靠区与中央区之间的分隔条，调整停靠区厚度（宽度或高度）。 */
export function dockResizeArea(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  area: DockArea,
  size: number,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const minSize = area === "left" || area === "right" ? 80 : 48;
  const clamped = Math.max(minSize, Math.min(900, Math.round(size)));
  const areaSpec = next.areas[area];
  areaSpec.size = clamped;
  // 沿厚度方向并排时，停靠区厚度是各组之和，要按比例缩放各组，否则归一化后又回到原来的和
  const orient = areaSpec.splitOrientation ?? defaultSplitOrientation(area);
  if (areaSpec.groups.length >= 2 && orient !== defaultSplitOrientation(area)) {
    const groupEntries = areaSpec.groups.map((group) => {
      const activeId = group.active && group.tabs.includes(group.active) ? group.active : group.tabs[0];
      const activeDock = (activeId ? scene.nodes[activeId] : undefined) ?? ({ type: "Dock", width: 240, height: 280 } as SceneNode);
      return { group, activeDock };
    });
    const scaled = distributeGroupAxisSizes(clamped, groupEntries, orient);
    areaSpec.groups.forEach((group, gi) => {
      group.size = scaled[gi];
    });
  }
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

function computeReferenceAreaSplitSpan(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  area: DockArea,
): number {
  const W = winNode?.width && winNode.width > 0 ? winNode.width : 1280;
  const H = winNode?.height && winNode.height > 0 ? winNode.height : 800;
  const menuH = winNode?.menuBar ? Math.max(0, Math.round(winNode.menuBar.height ?? 28)) : 0;
  const statusNode = winNode?.statusBar ? scene.nodes[winNode.statusBar] : undefined;
  const statusH = statusNode ? Math.round(statusNode.height ?? 28) : 0;
  const midH = Math.max(0, H - menuH - statusH);

  const areaSpec = layout.areas[area];
  const orient = areaSpec?.splitOrientation ?? defaultSplitOrientation(area);
  if (orient === "horizontal") {
    if (area === "top" || area === "bottom") return W;
    const firstId = areaSpec?.groups[0]?.active ?? areaSpec?.groups[0]?.tabs[0];
    const firstNode = firstId ? scene.nodes[firstId] : undefined;
    return Math.max(80, Math.round(areaSpec?.size ?? (firstNode ? prefWidth(firstNode) : 240)));
  }

  let topH = 0;
  if ((layout.areas.top?.groups.length ?? 0) > 0) {
    const firstTopId = layout.areas.top.groups[0].active ?? layout.areas.top.groups[0].tabs[0];
    const firstTopNode = firstTopId ? scene.nodes[firstTopId] : undefined;
    topH = Math.max(40, Math.round(layout.areas.top.size ?? (firstTopNode ? prefHeight(firstTopNode) : 68)));
  }
  let bottomH = 0;
  if ((layout.areas.bottom?.groups.length ?? 0) > 0) {
    const firstBotId = layout.areas.bottom.groups[0].active ?? layout.areas.bottom.groups[0].tabs[0];
    const firstBotNode = firstBotId ? scene.nodes[firstBotId] : undefined;
    bottomH = Math.max(40, Math.round(layout.areas.bottom.size ?? (firstBotNode ? prefHeight(firstBotNode) : 96)));
  }
  if (area === "top") return topH || 68;
  if (area === "bottom") return bottomH || 96;
  return Math.max(80, midH - topH - bottomH);
}

/** 拖动同一停靠区内并排组之间的分隔条，仅在相邻的第 groupIndex 与第 groupIndex+1 组之间重新分配尺寸，保持其余组不变。 */
export function dockResizeSplit(
  layout: DockLayoutSpec,
  scene: Scene,
  winNode: SceneNode | undefined,
  area: DockArea,
  groupIndex: number,
  size: number,
): DockLayoutSpec {
  const next = cloneDockLayout(layout);
  const areaSpec = next.areas[area];
  const groups = areaSpec?.groups ?? [];
  if (groupIndex < 0 || groupIndex >= groups.length) {
    return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
  }
  const orient = areaSpec.splitOrientation ?? defaultSplitOrientation(area);
  const minEach = orient === "horizontal" ? 60 : 48;
  if (groups.length === 1 || groupIndex === groups.length - 1) {
    groups[groupIndex].size = Math.max(minEach, Math.round(size));
    return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
  }
  const refSpan = computeReferenceAreaSplitSpan(next, scene, winNode, area);
  const groupEntries = groups.map((group) => {
    const activeId = group.active && group.tabs.includes(group.active) ? group.active : group.tabs[0];
    const activeDock = (activeId ? scene.nodes[activeId] : undefined) ?? ({ type: "Dock", width: 240, height: 280 } as SceneNode);
    return { group, activeDock };
  });
  const curSizes = distributeGroupAxisSizes(refSpan, groupEntries, orient);
  const pairSum = Math.max(minEach * 2, curSizes[groupIndex] + curSizes[groupIndex + 1]);
  const newA = Math.max(minEach, Math.min(pairSum - minEach, Math.round(size)));
  const newB = Math.max(minEach, pairSum - newA);
  curSizes[groupIndex] = newA;
  curSizes[groupIndex + 1] = newB;
  for (let i = 0; i < groups.length; i++) {
    groups[i].size = curSizes[i];
  }
  return normalizeDockLayout(scene, { ...winNode, type: "Window", width: winNode?.width ?? 1280, height: winNode?.height ?? 800, dockLayout: next });
}

function distributeGroupAxisSizes(
  totalSpan: number,
  groups: Array<{ group: DockGroupSpec; activeDock: SceneNode }>,
  axis: "horizontal" | "vertical",
): number[] {
  const n = groups.length;
  if (n === 0) return [];
  if (n === 1) return [Math.max(0, totalSpan)];
  const minEach = axis === "horizontal" ? 60 : 48;
  const rawSizes = groups.map(({ group, activeDock }) => {
    if (group.size !== undefined && group.size > 0) return group.size;
    return axis === "horizontal" ? prefWidth(activeDock) : prefHeight(activeDock);
  });

  const sumRaw = rawSizes.reduce((a, b) => a + b, 0) || 1;
  const out: number[] = [];
  let used = 0;
  for (let i = 0; i < n; i++) {
    if (i === n - 1) {
      out.push(Math.max(minEach, totalSpan - used));
    } else {
      const maxAllow = Math.max(minEach, totalSpan - used - minEach * (n - 1 - i));
      const s = Math.max(minEach, Math.min(maxAllow, Math.round((totalSpan * rawSizes[i]) / sumRaw)));
      out.push(s);
      used += s;
    }
  }
  return out;
}

/** 窗口放不下全部停靠区时，给中央区留下的最小尺寸。 */
const MIN_CENTER_WIDTH = 120;
const MIN_CENTER_HEIGHT = 80;

interface VisibleDockGroup {
  group: DockGroupSpec;
  groupIndex: number;
  visibleTabs: string[];
  activeId: string;
  activeDock: SceneNode;
}

interface EdgeHeights {
  orient: DockSplitOrientation;
  /** 横向并排时各组的宽度 */
  widths: number[];
  /** 横向并排时只有一个共用厚度；竖向并排时是各组高度 */
  heights: number[];
  /** 各高度的内容下限，收窄时不低于它 */
  floors: number[];
}

/**
 * 根据 DockLayoutSpec 计算主窗口所有停靠面板、中央区及分隔条的几何框。
 */
export function computeDockWindowLayout(
  scene: Scene,
  winId: string,
  winNode: SceneNode,
  W: number,
  midY: number,
  midH: number,
  winVis: boolean,
  isVisible: (id: string) => boolean,
  dockLayoutOverride?: DockLayoutSpec,
): ComputedDockWindowLayout {
  const layout = dockLayoutOverride
    ? normalizeDockLayout(scene, { ...winNode, dockLayout: dockLayoutOverride })
    : normalizeDockLayout(scene, winNode);

  const dockBoxes = new Map<string, ComputedBox>();
  const splitters: DockSplitterRect[] = [];
  const activeTabByDock = new Map<string, { groupTabs: string[]; activeTab: string; isFloating: boolean; area?: DockArea }>();

  const resolveAreaVisibleGroups = (area: DockArea) => {
    const areaSpec = layout.areas[area];
    const visGroups: VisibleDockGroup[] = [];
    areaSpec.groups.forEach((group, groupIndex) => {
      const visibleTabs = group.tabs.filter((tid) => scene.nodes[tid] && isVisible(tid));
      if (visibleTabs.length === 0) {
        for (const tid of group.tabs) {
          const d = scene.nodes[tid];
          if (!d) continue;
          dockBoxes.set(tid, {
            x: 0,
            y: midY,
            width: prefWidth(d),
            height: prefHeight(d),
            absX: 0,
            absY: midY,
            visible: false,
            windowId: winId,
          });
          activeTabByDock.set(tid, { groupTabs: [], activeTab: tid, isFloating: false, area });
        }
        return;
      }
      const activeId = group.active && visibleTabs.includes(group.active) ? group.active : visibleTabs[0];
      const activeDock = scene.nodes[activeId]!;
      visGroups.push({ group, groupIndex, visibleTabs, activeId, activeDock });
    });
    return visGroups;
  };

  const assignGroupBoxes = (
    area: DockArea,
    groupEntry: { group: DockGroupSpec; visibleTabs: string[]; activeId: string },
    gx: number,
    gy: number,
    gw: number,
    gh: number,
  ) => {
    for (const tid of groupEntry.group.tabs) {
      const d = scene.nodes[tid];
      if (!d) continue;
      const isActive = tid === groupEntry.activeId && isVisible(tid);
      dockBoxes.set(tid, {
        x: gx,
        y: gy,
        width: gw,
        height: gh,
        absX: gx,
        absY: gy,
        visible: winVis && isActive,
        windowId: winId,
      });
      activeTabByDock.set(tid, {
        groupTabs: groupEntry.visibleTabs,
        activeTab: groupEntry.activeId,
        isFloating: false,
        area,
      });
    }
  };

  // 顶部、底部的高度：横向并排时共用一个厚度；竖向并排时各组独立，厚度是各组之和。
  // 只有换行布局的面板按内容下限撑高，其他面板完全跟随停靠排列。
  const resolveEdgeHeights = (area: "top" | "bottom", groups: VisibleDockGroup[]): EdgeHeights => {
    const orient = layout.areas[area].splitOrientation ?? "horizontal";
    if (orient === "horizontal") {
      const widths = distributeGroupAxisSizes(W, groups, "horizontal");
      const base = Math.max(40, Math.round(layout.areas[area].size ?? prefHeight(groups[0].activeDock)));
      const floor = Math.max(0, ...groups.map((g, idx) => dockMinHeightForWidth(scene, g.activeDock, widths[idx], isVisible)));
      return { orient, widths, heights: [Math.max(base, floor)], floors: [floor] };
    }
    const floors = groups.map((g) => dockMinHeightForWidth(scene, g.activeDock, W, isVisible));
    const heights = groups.map((g, idx) =>
      Math.max(40, Math.round(g.group.size ?? layout.areas[area].size ?? prefHeight(g.activeDock)), floors[idx]),
    );
    return { orient, widths: [], heights, floors };
  };

  const topGroups = resolveAreaVisibleGroups("top");
  const bottomGroups = resolveAreaVisibleGroups("bottom");
  const topEdge = topGroups.length > 0 ? resolveEdgeHeights("top", topGroups) : null;
  const bottomEdge = bottomGroups.length > 0 ? resolveEdgeHeights("bottom", bottomGroups) : null;

  // 窗口放不下时只收窄实际尺寸，排列描述里的尺寸不变，窗口放大后恢复
  const edgeSum = (edge: EdgeHeights | null) => (edge ? edge.heights.reduce((acc, h) => acc + h, 0) : 0);
  let edgeExcess = edgeSum(topEdge) + edgeSum(bottomEdge) - Math.max(0, midH - MIN_CENTER_HEIGHT);
  for (const edge of [bottomEdge, topEdge]) {
    if (!edge) continue;
    for (let i = edge.heights.length - 1; i >= 0 && edgeExcess > 0; i--) {
      const cut = Math.min(edgeExcess, Math.max(0, edge.heights[i] - Math.max(40, edge.floors[i])));
      edge.heights[i] -= cut;
      edgeExcess -= cut;
    }
  }

  // 1. Top 停靠区
  let topH = 0;
  if (topEdge) {
    if (topEdge.orient === "horizontal") {
      topH = topEdge.heights[0];
      let curX = 0;
      topGroups.forEach((g, idx) => {
        const gw = topEdge.widths[idx];
        assignGroupBoxes("top", g, curX, midY, gw, topH);
        curX += gw;
        if (idx < topGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "top",
            groupIndex: g.groupIndex,
            orientation: "vertical",
            x: curX - 3,
            y: midY,
            width: 6,
            height: topH,
            currentSize: gw,
          });
        }
      });
    } else {
      let curY = midY;
      topGroups.forEach((g, idx) => {
        const gh = topEdge.heights[idx];
        assignGroupBoxes("top", g, 0, curY, W, gh);
        curY += gh;
        topH += gh;
        if (idx < topGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "top",
            groupIndex: g.groupIndex,
            orientation: "horizontal",
            x: 0,
            y: curY - 3,
            width: W,
            height: 6,
            currentSize: gh,
          });
        }
      });
    }
    splitters.push({
      kind: "area",
      area: "top",
      orientation: "horizontal",
      x: 0,
      y: midY + topH - 3,
      width: W,
      height: 6,
      currentSize: topH,
    });
  }

  // 2. Bottom 停靠区
  let bottomH = 0;
  if (bottomEdge) {
    if (bottomEdge.orient === "horizontal") {
      bottomH = bottomEdge.heights[0];
      const bY = midY + midH - bottomH;
      let curX = 0;
      bottomGroups.forEach((g, idx) => {
        const gw = bottomEdge.widths[idx];
        assignGroupBoxes("bottom", g, curX, bY, gw, bottomH);
        curX += gw;
        if (idx < bottomGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "bottom",
            groupIndex: g.groupIndex,
            orientation: "vertical",
            x: curX - 3,
            y: bY,
            width: 6,
            height: bottomH,
            currentSize: gw,
          });
        }
      });
    } else {
      bottomH = edgeSum(bottomEdge);
      let curY = midY + midH - bottomH;
      bottomGroups.forEach((g, idx) => {
        const gh = bottomEdge.heights[idx];
        assignGroupBoxes("bottom", g, 0, curY, W, gh);
        curY += gh;
        if (idx < bottomGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "bottom",
            groupIndex: g.groupIndex,
            orientation: "horizontal",
            x: 0,
            y: curY - 3,
            width: W,
            height: 6,
            currentSize: gh,
          });
        }
      });
    }
    splitters.push({
      kind: "area",
      area: "bottom",
      orientation: "horizontal",
      x: 0,
      y: midY + midH - bottomH - 3,
      width: W,
      height: 6,
      currentSize: bottomH,
    });
  }

  const centerY = midY + topH;
  const centerH = Math.max(0, midH - topH - bottomH);

  // 左右停靠区的宽度：竖向并排时共用一个厚度；横向并排时各组独立，厚度是各组之和
  const resolveSideWidths = (area: "left" | "right", groups: VisibleDockGroup[]): number[] => {
    const orient = layout.areas[area].splitOrientation ?? "vertical";
    if (orient === "vertical") {
      return [Math.max(60, Math.round(layout.areas[area].size ?? prefWidth(groups[0].activeDock)))];
    }
    return groups.map((g) => Math.max(60, Math.round(g.group.size ?? layout.areas[area].size ?? prefWidth(g.activeDock))));
  };

  const leftGroups = resolveAreaVisibleGroups("left");
  const rightGroups = resolveAreaVisibleGroups("right");
  const leftWidths = leftGroups.length > 0 ? resolveSideWidths("left", leftGroups) : [];
  const rightWidths = rightGroups.length > 0 ? resolveSideWidths("right", rightGroups) : [];
  const sumOf = (list: number[]) => list.reduce((acc, v) => acc + v, 0);
  let sideExcess = sumOf(leftWidths) + sumOf(rightWidths) - Math.max(0, W - MIN_CENTER_WIDTH);
  for (const list of [rightWidths, leftWidths]) {
    for (let i = list.length - 1; i >= 0 && sideExcess > 0; i--) {
      const cut = Math.min(sideExcess, Math.max(0, list[i] - 60));
      list[i] -= cut;
      sideExcess -= cut;
    }
  }

  // 3. Left 停靠区
  let leftW = 0;
  if (leftGroups.length > 0) {
    const orient = layout.areas.left.splitOrientation ?? "vertical";
    if (orient === "vertical") {
      leftW = leftWidths[0];
      const heights = distributeGroupAxisSizes(centerH, leftGroups, "vertical");
      let curY = centerY;
      leftGroups.forEach((g, idx) => {
        const gh = heights[idx];
        assignGroupBoxes("left", g, 0, curY, leftW, gh);
        curY += gh;
        if (idx < leftGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "left",
            groupIndex: g.groupIndex,
            orientation: "horizontal",
            x: 0,
            y: curY - 3,
            width: leftW,
            height: 6,
            currentSize: gh,
          });
        }
      });
    } else {
      leftGroups.forEach((g, idx) => {
        const gw = leftWidths[idx];
        assignGroupBoxes("left", g, leftW, centerY, gw, centerH);
        leftW += gw;
        if (idx < leftGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "left",
            groupIndex: g.groupIndex,
            orientation: "vertical",
            x: leftW - 3,
            y: centerY,
            width: 6,
            height: centerH,
            currentSize: gw,
          });
        }
      });
    }
    splitters.push({
      kind: "area",
      area: "left",
      orientation: "vertical",
      x: leftW - 3,
      y: centerY,
      width: 6,
      height: centerH,
      currentSize: leftW,
    });
  }

  // 4. Right 停靠区
  let rightW = 0;
  if (rightGroups.length > 0) {
    const orient = layout.areas.right.splitOrientation ?? "vertical";
    if (orient === "vertical") {
      rightW = rightWidths[0];
      const rX = W - rightW;
      const heights = distributeGroupAxisSizes(centerH, rightGroups, "vertical");
      let curY = centerY;
      rightGroups.forEach((g, idx) => {
        const gh = heights[idx];
        assignGroupBoxes("right", g, rX, curY, rightW, gh);
        curY += gh;
        if (idx < rightGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "right",
            groupIndex: g.groupIndex,
            orientation: "horizontal",
            x: rX,
            y: curY - 3,
            width: rightW,
            height: 6,
            currentSize: gh,
          });
        }
      });
    } else {
      rightW = sumOf(rightWidths);
      let rCurX = W - rightW;
      rightGroups.forEach((g, idx) => {
        const gw = rightWidths[idx];
        assignGroupBoxes("right", g, rCurX, centerY, gw, centerH);
        rCurX += gw;
        if (idx < rightGroups.length - 1) {
          splitters.push({
            kind: "split",
            area: "right",
            groupIndex: g.groupIndex,
            orientation: "vertical",
            x: rCurX - 3,
            y: centerY,
            width: 6,
            height: centerH,
            currentSize: gw,
          });
        }
      });
    }
    splitters.push({
      kind: "area",
      area: "right",
      orientation: "vertical",
      x: W - rightW - 3,
      y: centerY,
      width: 6,
      height: centerH,
      currentSize: rightW,
    });
  }

  // 5. 浮动停靠面板
  for (const f of layout.floating ?? []) {
    const d = scene.nodes[f.dockId];
    if (!d) continue;
    const vis = winVis && isVisible(f.dockId);
    dockBoxes.set(f.dockId, {
      x: f.x,
      y: f.y,
      width: f.width,
      height: f.height,
      absX: f.x,
      absY: f.y,
      visible: vis,
      windowId: winId,
    });
    activeTabByDock.set(f.dockId, {
      groupTabs: vis ? [f.dockId] : [],
      activeTab: f.dockId,
      isFloating: true,
      area: f.lastArea,
    });
  }

  const centralBox = {
    x: leftW,
    y: centerY,
    width: Math.max(0, W - leftW - rightW),
    height: centerH,
  };

  return { dockBoxes, centralBox, splitters, activeTabByDock };
}

/**
 * 把排列描述换算成当前窗口尺寸下的实际尺寸：换行面板撑高、窗口放不下时收窄、并排比例随窗口缩放后，
 * 描述里的尺寸与实际不同。和 Qt 实测比较时用这一份；没有可见面板的组和停靠区不带尺寸。
 */
export function effectiveDockLayout(
  layout: DockLayoutSpec,
  boxOf: (dockId: string) => { width: number; height: number; visible: boolean } | undefined,
): DockLayoutSpec {
  const out = cloneDockLayout(layout);
  for (const area of DOCK_AREAS) {
    const areaSpec = out.areas[area];
    const orient = areaSpec.splitOrientation ?? defaultSplitOrientation(area);
    const horizontalThickness = area === "left" || area === "right";
    const alongThickness = orient !== defaultSplitOrientation(area);
    const shown: Array<{ group: DockGroupSpec; shownTab: string; box: { width: number; height: number } }> = [];
    for (const group of areaSpec.groups) {
      delete group.size;
      const shownTab = group.tabs.find((tid) => boxOf(tid)?.visible);
      if (shownTab) shown.push({ group, shownTab, box: boxOf(shownTab)! });
    }
    let thickness = 0;
    for (const { group, shownTab, box } of shown) {
      // 描述里的激活页签可能已关闭，这时显示的是组里另一个页签
      group.active = shownTab;
      const groupThickness = horizontalThickness ? box.width : box.height;
      thickness = alongThickness ? thickness + groupThickness : Math.max(thickness, groupThickness);
      if (shown.length >= 2) group.size = orient === "horizontal" ? box.width : box.height;
    }
    if (thickness > 0) areaSpec.size = thickness;
    else delete areaSpec.size;
  }
  return out;
}

// ---------- 预览模式下的 DOM 停靠交互控制 ----------

export interface DockPreviewCallbacks {
  getMode: () => "edit" | "preview";
  getScene: () => Scene;
  getDockLayout?: () => DockLayoutSpec;
  getWindowSize: () => { width: number; height: number };
  getDockTitle: (dockId: string) => string;
  onActivateTab: (dockId: string) => void;
  onTabify: (targetDockId: string, dockId: string) => void;
  onSplit: (targetDockId: string, dockId: string, position: "before" | "after", orientation?: DockSplitOrientation) => void;
  onMoveToArea: (dockId: string, area: DockArea) => void;
  onFloat: (dockId: string, rect?: Partial<DockFloatRect>, commit?: boolean) => void;
  onRestore: (dockId: string) => void;
  onClose: (dockId: string) => void;
  onResizeArea: (area: DockArea, size: number, commit?: boolean) => void;
  onResizeSplit: (area: DockArea, groupIndex: number, size: number, commit?: boolean) => void;
}

/**
 * 渲染单个 Dock 面板的标题栏内容（含叠页签栏、浮出/恢复按钮、关闭按钮、拖拽手柄）及浮窗缩放手柄。
 */
export function syncDockHeaderDom(
  dockEl: HTMLElement,
  headerEl: HTMLElement,
  dockId: string,
  headerHeight: number,
  info: { groupTabs: string[]; activeTab: string; isFloating: boolean; area?: DockArea } | undefined,
  windowFrameEl: HTMLElement,
  callbacks: DockPreviewCallbacks,
): void {
  const isPreview = callbacks.getMode() === "preview";
  const isFloating = Boolean(info?.isFloating);
  const groupTabs = info?.groupTabs && info.groupTabs.length > 0 ? info.groupTabs : [dockId];

  dockEl.classList.toggle("dock-floating", isFloating);
  headerEl.style.height = `${headerHeight}px`;

  if (!isPreview) {
    headerEl.textContent = callbacks.getDockTitle(dockId);
    dockEl.querySelector(".dock-float-resizer")?.remove();
    return;
  }

  const sig = JSON.stringify({
    tabs: groupTabs.map((tid) => [tid, callbacks.getDockTitle(tid)]),
    active: info?.activeTab ?? dockId,
    floating: isFloating,
  });
  if (headerEl.dataset.dockHeaderSig !== sig) {
    headerEl.dataset.dockHeaderSig = sig;
    headerEl.replaceChildren();

    if (groupTabs.length >= 2) {
      const tabBar = document.createElement("div");
      tabBar.className = "dock-tabs";
      for (const tid of groupTabs) {
        const tabBtn = document.createElement("button");
        tabBtn.type = "button";
        tabBtn.className = "dock-tab" + (tid === (info?.activeTab ?? dockId) ? " active" : "");
        tabBtn.dataset.dockTab = tid;
        tabBtn.textContent = callbacks.getDockTitle(tid);
        tabBtn.addEventListener("click", (e) => {
          e.stopPropagation();
          callbacks.onActivateTab(tid);
        });
        tabBtn.addEventListener("pointerdown", (e) => {
          if (e.button !== 0) return;
          e.stopPropagation();
          startDockDragSession(e, tid, dockEl, windowFrameEl, callbacks);
        });
        tabBar.append(tabBtn);
      }
      headerEl.append(tabBar);
    } else {
      const titleSpan = document.createElement("span");
      titleSpan.className = "dock-title";
      titleSpan.textContent = callbacks.getDockTitle(dockId);
      headerEl.append(titleSpan);
    }

    const actions = document.createElement("div");
    actions.className = "dock-actions";

    const floatBtn = document.createElement("button");
    floatBtn.type = "button";
    floatBtn.className = "dock-action-btn dock-btn-float";
    floatBtn.dataset.dockAction = isFloating ? "restore" : "float";
    floatBtn.title = isFloating ? "恢复停靠" : "浮出面板";
    floatBtn.textContent = isFloating ? "↙" : "⧉";
    floatBtn.addEventListener("pointerdown", (e) => e.stopPropagation());
    floatBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      if (isFloating) callbacks.onRestore(dockId);
      else callbacks.onFloat(dockId);
    });

    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "dock-action-btn dock-btn-close";
    closeBtn.dataset.dockAction = "close";
    closeBtn.title = "关闭面板";
    closeBtn.textContent = "×";
    closeBtn.addEventListener("pointerdown", (e) => e.stopPropagation());
    closeBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      callbacks.onClose(dockId);
    });

    actions.append(floatBtn, closeBtn);
    headerEl.append(actions);
  }

  if (!headerEl.dataset.dragBound) {
    headerEl.dataset.dragBound = "1";
    headerEl.addEventListener("pointerdown", (e) => {
      if (callbacks.getMode() !== "preview" || e.button !== 0) return;
      const target = e.target as HTMLElement | null;
      if (target?.closest(".dock-action-btn") || target?.closest(".dock-tab")) return;
      e.stopPropagation();
      startDockDragSession(e, dockId, dockEl, windowFrameEl, callbacks);
    });
    headerEl.addEventListener("dblclick", (e) => {
      if (callbacks.getMode() !== "preview") return;
      const target = e.target as HTMLElement | null;
      if (target?.closest(".dock-action-btn")) return;
      e.stopPropagation();
      if (dockEl.classList.contains("dock-floating")) callbacks.onRestore(dockId);
      else callbacks.onFloat(dockId);
    });
  }

  // 浮窗右下角缩放手柄
  let resizer = dockEl.querySelector<HTMLElement>(".dock-float-resizer");
  if (isFloating) {
    if (!resizer) {
      resizer = document.createElement("div");
      resizer.className = "dock-float-resizer";
      resizer.addEventListener("pointerdown", (e) => {
        if (callbacks.getMode() !== "preview" || e.button !== 0) return;
        e.stopPropagation();
        e.preventDefault();
        const ctx = createFrameDragContext(windowFrameEl, e, resizer!);
        const startPt = ctx.toStagePoint(e);
        const startW = dockEl.offsetWidth;
        const startH = dockEl.offsetHeight;
        const startLeft = parseFloat(dockEl.style.left) || 0;
        const startTop = parseFloat(dockEl.style.top) || 0;
        let latestRect: Partial<DockFloatRect> | null = null;
        const onMove = (me: PointerEvent, fromOuter = false) => {
          const pt = ctx.toStagePoint(me, fromOuter);
          const nw = Math.max(120, Math.round(startW + (pt.x - startPt.x)));
          const nh = Math.max(80, Math.round(startH + (pt.y - startPt.y)));
          latestRect = { x: startLeft, y: startTop, width: nw, height: nh };
          callbacks.onFloat(dockId, latestRect, false);
        };
        const onUp = () => {
          cleanup();
          if (latestRect) callbacks.onFloat(dockId, latestRect, true);
        };
        const cleanup = ctx.bindDrag(onMove, onUp);
      });
      dockEl.append(resizer);
    }
  } else {
    resizer?.remove();
  }
}

/**
 * 统一处理直接挂载在页面或隔离在 #stage-iframe 内的拖拽坐标换算与跨窗口指针监听：
 * - 支持外层 #stage-frame CSS transform: scale(zoom) 与内层容器自身缩放；
 * - 同时监听 iframe 内部 window 与外层 window 的 pointermove / pointerup；
 * - 自动识别事件 clientX/clientY 是来自 iframe 内部视口坐标系还是外层主窗口坐标系。
 */
function createFrameDragContext(
  windowFrameEl: HTMLElement,
  downEvent: PointerEvent,
  targetEl?: HTMLElement,
) {
  const ownerDoc = windowFrameEl.ownerDocument ?? document;
  const ownerWin = ownerDoc.defaultView ?? window;
  const frameEl = ownerWin !== window ? (ownerWin.frameElement as HTMLElement | null) : null;

  const isOuterEvent = (pe: PointerEvent, checkTarget?: HTMLElement): boolean => {
    if (!frameEl || ownerWin === window) return false;
    if (pe.view === window) return true;
    if (pe.view === ownerWin) return false;
    const fr = frameEl.getBoundingClientRect();
    const outerScale = frameEl.offsetWidth > 0 && fr.width > 0 ? fr.width / frameEl.offsetWidth : 1;
    if (checkTarget && (Math.abs(outerScale - 1) > 0.01 || Math.abs(fr.left) > 1 || Math.abs(fr.top) > 1)) {
      const tr = checkTarget.getBoundingClientRect();
      const inInner =
        pe.clientX >= tr.left - 4 &&
        pe.clientX <= tr.right + 4 &&
        pe.clientY >= tr.top - 4 &&
        pe.clientY <= tr.bottom + 4;
      const oLeft = fr.left + tr.left * outerScale;
      const oRight = fr.left + tr.right * outerScale;
      const oTop = fr.top + tr.top * outerScale;
      const oBottom = fr.top + tr.bottom * outerScale;
      const inOuter =
        pe.clientX >= oLeft - 4 &&
        pe.clientX <= oRight + 4 &&
        pe.clientY >= oTop - 4 &&
        pe.clientY <= oBottom + 4;
      if (inOuter && !inInner) return true;
      if (inInner && !inOuter) return false;
    }
    return false;
  };

  const downUsesOuter = isOuterEvent(downEvent, targetEl);

  const toStagePoint = (pe: PointerEvent, fromOuter = false): { x: number; y: number } => {
    const winRect = windowFrameEl.getBoundingClientRect();
    const innerScale =
      windowFrameEl.offsetWidth > 0 && winRect.width > 0 ? winRect.width / windowFrameEl.offsetWidth : 1;
    let cx = pe.clientX;
    let cy = pe.clientY;
    if (frameEl && (downUsesOuter || fromOuter || pe.view === window)) {
      const fr = frameEl.getBoundingClientRect();
      const outerScale = frameEl.offsetWidth > 0 && fr.width > 0 ? fr.width / frameEl.offsetWidth : 1;
      cx = (cx - fr.left) / outerScale;
      cy = (cy - fr.top) / outerScale;
    }
    return {
      x: (cx - winRect.left) / innerScale,
      y: (cy - winRect.top) / innerScale,
    };
  };

  const bindDrag = (
    onMove: (e: PointerEvent, fromOuter: boolean) => void,
    onUp: (e: PointerEvent, fromOuter: boolean) => void,
  ) => {
    const moveInner = (e: PointerEvent) => onMove(e, false);
    const upInner = (e: PointerEvent) => onUp(e, false);
    const moveOuter = (e: PointerEvent) => onMove(e, true);
    const upOuter = (e: PointerEvent) => onUp(e, true);
    ownerWin.addEventListener("pointermove", moveInner);
    ownerWin.addEventListener("pointerup", upInner);
    if (ownerWin !== window) {
      window.addEventListener("pointermove", moveOuter);
      window.addEventListener("pointerup", upOuter);
    }
    return () => {
      ownerWin.removeEventListener("pointermove", moveInner);
      ownerWin.removeEventListener("pointerup", upInner);
      if (ownerWin !== window) {
        window.removeEventListener("pointermove", moveOuter);
        window.removeEventListener("pointerup", upOuter);
      }
    };
  };

  return { toStagePoint, bindDrag };
}

/**
 * 在主窗口容器上渲染停靠区与并排面板的分隔条（支持拖动改大小）。
 */
export function syncDockSplittersDom(
  windowFrameEl: HTMLElement,
  splitters: DockSplitterRect[],
  callbacks: DockPreviewCallbacks,
): void {
  const existing = windowFrameEl.querySelectorAll(".dock-splitter");
  existing.forEach((el) => el.remove());
  if (callbacks.getMode() !== "preview") return;

  for (const sp of splitters) {
    const bar = document.createElement("div");
    bar.className = `dock-splitter ${sp.orientation} dock-splitter-${sp.orientation}`;
    bar.dataset.splitterKind = sp.kind;
    bar.dataset.splitterArea = sp.area;
    if (sp.groupIndex !== undefined) bar.dataset.splitterIndex = String(sp.groupIndex);
    Object.assign(bar.style, {
      left: `${sp.x}px`,
      top: `${sp.y}px`,
      width: `${sp.width}px`,
      height: `${sp.height}px`,
    });

    bar.addEventListener("pointerdown", (e) => {
      if (callbacks.getMode() !== "preview" || e.button !== 0) return;
      e.stopPropagation();
      e.preventDefault();
      const ctx = createFrameDragContext(windowFrameEl, e, bar);
      const startPt = ctx.toStagePoint(e, false);
      const startSize = sp.currentSize;
      bar.classList.add("dragging");
      let latestSize: number | null = null;

      const onMove = (me: PointerEvent, fromOuter = false) => {
        const pt = ctx.toStagePoint(me, fromOuter);
        const dx = Math.round(pt.x - startPt.x);
        const dy = Math.round(pt.y - startPt.y);
        if (sp.kind === "area") {
          let nextSize = startSize;
          if (sp.area === "left") nextSize = startSize + dx;
          else if (sp.area === "right") nextSize = startSize - dx;
          else if (sp.area === "top") nextSize = startSize + dy;
          else if (sp.area === "bottom") nextSize = startSize - dy;
          latestSize = nextSize;
          callbacks.onResizeArea(sp.area, nextSize, false);
        } else if (sp.kind === "split" && sp.groupIndex !== undefined) {
          const delta = sp.orientation === "vertical" ? dx : dy;
          latestSize = startSize + delta;
          callbacks.onResizeSplit(sp.area, sp.groupIndex, latestSize, false);
        }
      };
      const onUp = () => {
        bar.classList.remove("dragging");
        cleanup();
        if (latestSize !== null) {
          if (sp.kind === "area") {
            callbacks.onResizeArea(sp.area, latestSize, true);
          } else if (sp.kind === "split" && sp.groupIndex !== undefined) {
            callbacks.onResizeSplit(sp.area, sp.groupIndex, latestSize, true);
          }
        }
      };
      const cleanup = ctx.bindDrag(onMove, onUp);
    });

    windowFrameEl.append(bar);
  }
}

interface DropAction {
  type: "area" | "tabify" | "split" | "float";
  area?: DockArea;
  targetDockId?: string;
  splitPos?: "before" | "after";
  splitOrient?: DockSplitOrientation;
  previewRect: DockFloatRect;
}

function startDockDragSession(
  downEvent: PointerEvent,
  dragDockId: string,
  dockEl: HTMLElement,
  windowFrameEl: HTMLElement,
  callbacks: DockPreviewCallbacks,
): void {
  const ctx = createFrameDragContext(windowFrameEl, downEvent, dockEl);
  const startPt = ctx.toStagePoint(downEvent);
  const dockLeft = parseFloat(dockEl.style.left) || 0;
  const dockTop = parseFloat(dockEl.style.top) || 0;
  const offsetX = startPt.x - dockLeft;
  const offsetY = startPt.y - dockTop;
  const isAlreadyFloating = dockEl.classList.contains("dock-floating");
  const floatW = isAlreadyFloating
    ? Math.max(80, Math.round(dockEl.offsetWidth || 240))
    : Math.min(320, Math.max(180, Math.round(dockEl.offsetWidth || 240)));
  const floatH = isAlreadyFloating
    ? Math.max(60, Math.round(dockEl.offsetHeight || 280))
    : Math.min(440, Math.max(120, Math.round(dockEl.offsetHeight || 280)));

  let dragging = false;
  let overlayEl: HTMLElement | null = null;
  let ghostEl: HTMLElement | null = null;
  let currentDrop: DropAction | null = null;

  const computeDropAction = (relX: number, relY: number): DropAction => {
    const W = windowFrameEl.offsetWidth;
    const H = windowFrameEl.offsetHeight;
    const scene = callbacks.getScene();
    const curLayout = callbacks.getDockLayout
      ? callbacks.getDockLayout()
      : normalizeDockLayout(scene, scene.nodes[scene.root]);
    const dragLoc = dockFindLocation(curLayout, dragDockId);

    // 1. 检查是否悬停在窗口四边吸附热区（距离边缘 28px 内）
    const edgeMargin = 28;
    if (relX >= 0 && relX <= W && relY >= 0 && relY <= H) {
      if (relX <= edgeMargin) {
        return {
          type: "area",
          area: "left",
          previewRect: { x: 0, y: 28, width: 220, height: Math.max(80, H - 56) },
        };
      }
      if (relX >= W - edgeMargin) {
        return {
          type: "area",
          area: "right",
          previewRect: { x: Math.max(0, W - 240), y: 28, width: 240, height: Math.max(80, H - 56) },
        };
      }
      if (relY <= edgeMargin + 28) {
        return {
          type: "area",
          area: "top",
          previewRect: { x: 0, y: 28, width: W, height: 72 },
        };
      }
      if (relY >= H - edgeMargin - 28) {
        return {
          type: "area",
          area: "bottom",
          previewRect: { x: 0, y: Math.max(28, H - 124), width: W, height: 96 },
        };
      }
    }

    // 2. 检查是否悬停在可见已停靠面板上（中心叠页签，上下/左右边缘并排；含多页签组把当前激活页签拖到本组边缘拆出分栏）
    const otherDocks = Array.from(
      windowFrameEl.querySelectorAll<HTMLElement>('.node[data-type="Dock"], .vf-node[data-vf-type="Dock"]'),
    );
    for (const otherEl of otherDocks) {
      const otherId = otherEl.dataset.id ?? otherEl.dataset.vfId;
      if (!otherId) continue;
      if (otherEl.style.display === "none" || otherEl.classList.contains("dock-floating")) continue;
      let effectiveTargetId = otherId;
      if (otherId === dragDockId) {
        const peerId = dragLoc?.kind === "area" ? dragLoc.group.tabs.find((tid) => tid !== dragDockId) : undefined;
        if (!peerId) continue;
        effectiveTargetId = peerId;
      }
      const ox = parseFloat(otherEl.style.left) || 0;
      const oy = parseFloat(otherEl.style.top) || 0;
      const ow = otherEl.offsetWidth;
      const oh = otherEl.offsetHeight;
      if (relX >= ox && relX <= ox + ow && relY >= oy && relY <= oy + oh) {
        const otherLoc = dockFindLocation(curLayout, effectiveTargetId);
        const sameTabGroup =
          dragLoc?.kind === "area" &&
          otherLoc?.kind === "area" &&
          dragLoc.area === otherLoc.area &&
          dragLoc.groupIndex === otherLoc.groupIndex;
        const dNode = scene.nodes[otherId];
        const hdrH = dNode?.headerHeight ?? 26;
        // 在同一个页签组的标题栏内微拖页签按钮时，保持叠页签状态，不误拆成上下分栏
        if (sameTabGroup && relY <= oy + hdrH + 8) {
          return {
            type: "tabify",
            targetDockId: effectiveTargetId,
            previewRect: { x: ox, y: oy, width: ow, height: oh },
          };
        }
        const localX = (relX - ox) / Math.max(1, ow);
        const localY = (relY - oy) / Math.max(1, oh);
        const area: DockArea =
          otherLoc?.kind === "area" ? otherLoc.area : isDockArea(dNode?.dockArea) ? dNode.dockArea : "left";
        const primaryVert = (curLayout.areas[area]?.splitOrientation ?? defaultSplitOrientation(area)) === "vertical";
        if (primaryVert) {
          if (localY < 0.25) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "before",
              splitOrient: "vertical",
              previewRect: { x: ox, y: oy, width: ow, height: Math.round(oh / 2) },
            };
          }
          if (localY > 0.75) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "after",
              splitOrient: "vertical",
              previewRect: { x: ox, y: oy + Math.round(oh / 2), width: ow, height: Math.round(oh / 2) },
            };
          }
          if (localX < 0.22) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "before",
              splitOrient: "horizontal",
              previewRect: { x: ox, y: oy, width: Math.round(ow / 2), height: oh },
            };
          }
          if (localX > 0.78) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "after",
              splitOrient: "horizontal",
              previewRect: { x: ox + Math.round(ow / 2), y: oy, width: Math.round(ow / 2), height: oh },
            };
          }
        } else {
          if (localX < 0.25) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "before",
              splitOrient: "horizontal",
              previewRect: { x: ox, y: oy, width: Math.round(ow / 2), height: oh },
            };
          }
          if (localX > 0.75) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "after",
              splitOrient: "horizontal",
              previewRect: { x: ox + Math.round(ow / 2), y: oy, width: Math.round(ow / 2), height: oh },
            };
          }
          if (localY < 0.22) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "before",
              splitOrient: "vertical",
              previewRect: { x: ox, y: oy, width: ow, height: Math.round(oh / 2) },
            };
          }
          if (localY > 0.78) {
            return {
              type: "split",
              targetDockId: effectiveTargetId,
              splitPos: "after",
              splitOrient: "vertical",
              previewRect: { x: ox, y: oy + Math.round(oh / 2), width: ow, height: Math.round(oh / 2) },
            };
          }
        }
        return {
          type: "tabify",
          targetDockId: effectiveTargetId,
          previewRect: { x: ox, y: oy, width: ow, height: oh },
        };
      }
    }

    // 3. 其余位置：作为浮窗停留在指针位置
    const fx = Math.max(0, Math.min(Math.max(0, W - 80), Math.round(relX - offsetX)));
    const fy = Math.max(28, Math.min(Math.max(28, H - 60), Math.round(relY - offsetY)));
    return {
      type: "float",
      previewRect: { x: fx, y: fy, width: floatW, height: floatH },
    };
  };

  const onMove = (me: PointerEvent, fromOuter = false) => {
    const pt = ctx.toStagePoint(me, fromOuter);
    const dist = Math.hypot(pt.x - startPt.x, pt.y - startPt.y);
    if (!dragging) {
      if (dist < 4) return;
      dragging = true;
      overlayEl = document.createElement("div");
      overlayEl.className = "dock-drop-overlay";
      ghostEl = document.createElement("div");
      ghostEl.className = "dock-drag-ghost";
      ghostEl.textContent = callbacks.getDockTitle(dragDockId);
      windowFrameEl.append(overlayEl, ghostEl);
    }
    const relX = Math.round(pt.x);
    const relY = Math.round(pt.y);
    currentDrop = computeDropAction(relX, relY);
    if (overlayEl && currentDrop) {
      const r = currentDrop.previewRect;
      overlayEl.dataset.dropType = currentDrop.type;
      Object.assign(overlayEl.style, {
        left: `${r.x}px`,
        top: `${r.y}px`,
        width: `${r.width}px`,
        height: `${r.height}px`,
      });
    }
    if (ghostEl) {
      if (currentDrop) ghostEl.dataset.dropType = currentDrop.type;
      Object.assign(ghostEl.style, {
        left: `${Math.max(0, Math.min(windowFrameEl.offsetWidth - 80, relX + 10))}px`,
        top: `${Math.max(0, Math.min(windowFrameEl.offsetHeight - 28, relY + 10))}px`,
      });
    }
  };

  const onUp = () => {
    cleanup();
    overlayEl?.remove();
    ghostEl?.remove();
    if (!dragging || !currentDrop) return;
    if (currentDrop.type === "area" && currentDrop.area) {
      callbacks.onMoveToArea(dragDockId, currentDrop.area);
    } else if (currentDrop.type === "tabify" && currentDrop.targetDockId) {
      callbacks.onTabify(currentDrop.targetDockId, dragDockId);
    } else if (currentDrop.type === "split" && currentDrop.targetDockId && currentDrop.splitPos) {
      callbacks.onSplit(currentDrop.targetDockId, dragDockId, currentDrop.splitPos, currentDrop.splitOrient);
    } else if (currentDrop.type === "float") {
      callbacks.onFloat(dragDockId, currentDrop.previewRect, true);
    }
  };

  const cleanup = ctx.bindDrag(onMove, onUp);
}
