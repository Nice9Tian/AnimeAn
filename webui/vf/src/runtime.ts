// 逻辑运行层：保存状态，计算条件和属性，按批处理事件。
// 不依赖网页和 Tauri：工具的预览、命令行，以及导出后嵌进 Qt 的 JS 引擎，运行的都是这一份。
import {
  cloneDockLayout,
  dockActivateTab,
  dockFloat,
  dockMoveToArea,
  dockResizeArea,
  dockResizeSplit,
  dockRestore,
  dockSplit,
  dockTabify,
  isDockArea,
  normalizeDockLayout,
  type DockArea,
  type DockFloatRect,
  type DockLayoutSpec,
  type DockSplitOrientation,
  type DockSplitShown,
  dockFindLocation,
} from "./dock.ts";
import { evalExpr, parseAssignment, parseExpr, type Expr, type Value } from "./expr.ts";
import {
  allConditions,
  allMenuItems,
  allStateLeaves,
  allWindowIds,
  businessEvent,
  childrenOfNode,
  computeSceneLayout,
  declFor,
  findCondition,
  findStateLeaf,
  globalSteps,
  isBusinessState,
  isPartitionedWindow,
  layoutOf,
  requestEvent,
  resolveStateImage,
  type ComputedBox,
  type MenuItemSpec,
  type RuntimeProject,
  type StateImageKey,
  type StateLeaf,
  type Step,
} from "./model.ts";

export interface LogEntry {
  title: string;
  lines: string[];
}

export interface NodeView {
  visible: boolean;
  enabled: boolean;
  props: Record<string, Value>;
  /** 状态图片控件当前所处的外观状态（normal / hover / pressed / selected / disabled） */
  visualState?: StateImageKey;
  /** 状态图片控件当前选中的 SVG 文件名 */
  stateImage?: string;
}

export type ActionSimMode = "real" | "success" | "error";

export interface ActionSimConfig {
  mode: ActionSimMode;
  delayMs: number;
  result?: Value;
  errorMessage?: string;
}

export interface PendingBusinessEvent {
  name: string;
  args: Record<string, Value>;
}

export interface RuntimeHooks {
  onChange(): void;
  onLog(entry: LogEntry): void;
  /**
   * 有业务事件要处理时调用。返回 true 表示外面接手了（导出到 Qt 后由界面绑定层决定交给业务还是模拟）；
   * 没有提供，或者返回 false，就执行这个事件的模拟步骤。
   */
  raiseBusiness?(name: string, args: Record<string, Value>): boolean;
  /**
   * 有动作（call）要处理时调用。返回 true 表示外部业务接手了该动作（完成后由业务调 finishActionSuccess / finishActionError）；
   * 没有提供，或者返回 false，就由运行层执行动作脚本或模拟结果。
   */
  raiseAction?(name: string, input: Record<string, Value>): boolean;
}

const SHADOWED_DOM_GLOBALS = [
  "window",
  "document",
  "globalThis",
  "self",
  "top",
  "parent",
  "frames",
  "HTMLElement",
  "SVGElement",
  "Element",
  "Node",
  "navigator",
  "location",
  "history",
  "localStorage",
  "sessionStorage",
  "indexedDB",
  "XMLHttpRequest",
  "fetch",
  "WebSocket",
  "Worker",
  "MutationObserver",
  "ResizeObserver",
  "IntersectionObserver",
  "customElements",
  "requestAnimationFrame",
  "cancelAnimationFrame",
] as const;

const MAX_EMIT_DEPTH = 20;
/** 一次操作引出的业务事件最多连锁这么多个，防止模拟步骤互相触发停不下来。 */
const MAX_BUSINESS_CHAIN = 50;

function cloneValue<T extends Value>(value: T): T {
  if (value === null || typeof value !== "object") return value;
  return JSON.parse(JSON.stringify(value)) as T;
}

function sameStateValue(a: Value | undefined, b: Value | undefined): boolean {
  if (a === b) return true;
  if (a !== undefined && b !== undefined && typeof a === "object" && a !== null && typeof b === "object" && b !== null) {
    return JSON.stringify(a) === JSON.stringify(b);
  }
  return false;
}

function show(value: Value): string {
  if (typeof value === "string") return `'${value}'`;
  if (typeof value === "object" && value !== null) return JSON.stringify(value);
  return String(value);
}

function defaultFor(leaf: StateLeaf): Value {
  if (leaf.initial !== undefined) return cloneValue(leaf.initial);
  const fallback = (leaf as { default?: Value }).default;
  if (fallback !== undefined) return cloneValue(fallback);
  if (leaf.type === "number") return 0;
  if (leaf.type === "boolean") return false;
  if (leaf.type === "enum") return leaf.values?.[0] ?? "";
  if (leaf.type === "array") return [];
  if (leaf.type === "object") return {};
  return "";
}

function readTailValue(base: Value, tail: string[]): Value {
  let cursor: Value = base;
  for (const part of tail) {
    if (cursor === null || cursor === undefined) return null;
    if (Array.isArray(cursor)) {
      if (part === "length") cursor = cursor.length;
      else return null;
    } else if (typeof cursor === "object") {
      const next = (cursor as Record<string, Value>)[part];
      cursor = next === undefined ? null : next;
    } else if (typeof cursor === "string" && part === "length") {
      cursor = cursor.length;
    } else {
      return null;
    }
  }
  return cursor;
}

function doesViewAffectLayout(prev: NodeView | undefined, next: NodeView): boolean {
  if (!prev) return true;
  if (prev.visible !== next.visible) return true;
  const p = prev.props;
  const n = next.props;
  if (
    p.text !== n.text ||
    p.label !== n.label ||
    p.title !== n.title ||
    p.width !== n.width ||
    p.height !== n.height ||
    p.currentIndex !== n.currentIndex ||
    p.currentPage !== n.currentPage ||
    p.floating !== n.floating ||
    p.floatX !== n.floatX ||
    p.floatY !== n.floatY ||
    p.floatWidth !== n.floatWidth ||
    p.floatHeight !== n.floatHeight ||
    p.headerHeight !== n.headerHeight
  ) {
    return true;
  }
  return false;
}

interface FloatBoxRuntimeState {
  floating: boolean;
  floatX?: number;
  floatY?: number;
  floatWidth?: number;
  floatHeight?: number;
}

/** 一批步骤执行时用到的东西。 */
interface Batch {
  /** 状态的工作副本：后面的步骤读到前面步骤写入的新值，全部执行完再一次生效 */
  work: Map<string, Value>;
  /** 窗口开关状态的工作副本 */
  windows: Map<string, { open: boolean; modal: boolean }>;
  /** 停靠面板开关覆盖（当面板未通过 props.visible 绑定状态时使用） */
  docks: Map<string, boolean>;
  /** 菜单项未绑定状态时的勾选覆盖 */
  menuChecked: Map<string, boolean>;
  lines: string[];
  /** 这一批里 emit 的业务事件，等这批生效之后再处理 */
  pending: PendingBusinessEvent[];
  /** 这一批里 call 的动作，等这批生效之后再启动 */
  calls: Array<{ name: string; input: Record<string, Value> }>;
  /** 是不是业务事件的模拟步骤：只有模拟步骤能修改业务管的状态 */
  simulating: boolean;
}

export class Runtime {
  private project: RuntimeProject;
  private hooks: RuntimeHooks;
  /** 状态路径 → 当前值 */
  private state = new Map<string, Value>();
  /** 文字条件的开关 */
  private textConditions = new Map<string, boolean>();
  /** 顶层窗口与对话框的开关状态 */
  private windows = new Map<string, { open: boolean; modal: boolean }>();
  /** 停靠面板未绑定状态时的显隐开关 */
  private docks = new Map<string, boolean>();
  /** 主窗口停靠排列状态 */
  private dockLayouts = new Map<string, DockLayoutSpec>();
  /** 上次 reset/adopt 时画布节点上的 dockLayout 快照（用于判断 scene.json 中的 dockLayout 是否被外部修改或撤销/重做） */
  private lastSceneDockLayouts = new Map<string, string>();
  /** 浮动嵌入框未绑定状态时的运行态属性 */
  private floatBoxes = new Map<string, FloatBoxRuntimeState>();
  /** 上次 reset/adopt 时画布 FloatBox 节点的 props 快照 */
  private lastSceneFloatBoxProps = new Map<string, string>();
  /** 菜单项未绑定状态时的勾选开关 */
  private menuChecked = new Map<string, boolean>();
  /** 预览时钟当前毫秒数 */
  private clockMs = 0;
  /** 动作模拟设置 */
  private actionSims = new Map<string, ActionSimConfig>();
  /** 正在进行中的异步动作集合 */
  private runningActions = new Set<string>();
  /** 当前鼠标悬停中的控件 ID 集合（用于状态图片选图） */
  private hoveredControls = new Set<string>();
  /** 当前鼠标按下中的控件 ID 集合（用于状态图片选图） */
  private pressedControls = new Set<string>();

  /** 静态场景结构索引（避免每次 computeView 遍历全场景） */
  private menuItemById = new Map<string, MenuItemSpec>();
  private parentById = new Map<string, string>();
  private windowById = new Map<string, string>();
  private stackParentById = new Map<string, { parentId: string; children: string[] }>();
  private dependentNodesByNode = new Map<string, Set<string>>();

  /** 增量视图与排版缓存 */
  private viewCache = new Map<string, NodeView>();
  private prevViewCache = new Map<string, NodeView>();
  private depsByPath = new Map<string, Set<string>>();
  private depsByNode = new Map<string, Set<string>>();
  private trackingStack: Array<Set<string>> = [];
  private dirtyViewIds = new Set<string>();
  private pendingViewCheckIds = new Set<string>();
  private allViewsDirty = true;
  private layoutDirty = true;
  private cachedLayoutBoxes: Map<string, ComputedBox> | null = null;
  private lastLayoutRootSize: { width: number; height: number } | undefined = undefined;

  private activePromises = new Set<Promise<unknown>>();
  private exprCache = new Map<string, Expr>();
  private fnCache = new Map<string, (state: Record<string, unknown>, args: Record<string, Value>) => unknown>();
  private businessChain = 0;

  constructor(project: RuntimeProject, hooks: RuntimeHooks = { onChange() {}, onLog() {} }) {
    this.project = project;
    this.hooks = hooks;
    this.reset();
  }

  private rebuildSceneIndexes(): void {
    this.menuItemById.clear();
    this.parentById.clear();
    this.windowById.clear();
    this.stackParentById.clear();
    this.dependentNodesByNode.clear();

    const addNodeDep = (fromId: string, toId: string) => {
      let set = this.dependentNodesByNode.get(fromId);
      if (!set) {
        set = new Set();
        this.dependentNodesByNode.set(fromId, set);
      }
      set.add(toId);
    };

    const scene = this.project.scene;
    for (const { item } of allMenuItems(scene)) {
      this.menuItemById.set(item.id, item);
      if (item.dock) {
        addNodeDep(item.dock, item.id);
      }
    }

    for (const [id, n] of Object.entries(scene.nodes)) {
      for (const cid of childrenOfNode(n)) {
        if (!this.parentById.has(cid)) {
          this.parentById.set(cid, id);
        }
      }
      if (layoutOf(n)?.kind === "stack" && n.children) {
        for (const cid of n.children) {
          this.stackParentById.set(cid, { parentId: id, children: n.children });
          addNodeDep(id, cid);
        }
      }
    }

    const markWin = (winId: string, curId: string) => {
      if (this.windowById.has(curId)) return;
      this.windowById.set(curId, winId);
      const n = scene.nodes[curId];
      if (!n) return;
      for (const cid of childrenOfNode(n)) {
        markWin(winId, cid);
      }
    };
    for (const winId of allWindowIds(scene)) {
      markWin(winId, winId);
    }
  }

  /** 清空全部视图和排版缓存。 */
  invalidateAll(): void {
    this.viewCache.clear();
    this.prevViewCache.clear();
    this.depsByPath.clear();
    this.depsByNode.clear();
    this.dirtyViewIds.clear();
    this.pendingViewCheckIds.clear();
    this.allViewsDirty = true;
    this.layoutDirty = true;
    this.cachedLayoutBoxes = null;
  }

  private setNodeDeps(id: string, nextDeps: Set<string>): void {
    const prevDeps = this.depsByNode.get(id);
    if (prevDeps) {
      for (const p of prevDeps) {
        if (!nextDeps.has(p)) {
          this.depsByPath.get(p)?.delete(id);
        }
      }
    }
    this.depsByNode.set(id, nextDeps);
    for (const p of nextDeps) {
      let set = this.depsByPath.get(p);
      if (!set) {
        set = new Set();
        this.depsByPath.set(p, set);
      }
      set.add(id);
    }
  }

  private invalidateNode(id: string): void {
    this.viewCache.delete(id);
    this.dirtyViewIds.add(id);
    if (this.pendingViewCheckIds.has(id)) return;
    this.pendingViewCheckIds.add(id);
    const depNodes = this.dependentNodesByNode.get(id);
    if (depNodes) {
      for (const childId of depNodes) {
        this.invalidateNode(childId);
      }
    }
  }

  private invalidatePath(path: string): void {
    const nodes = this.depsByPath.get(path);
    if (!nodes) return;
    for (const id of [...nodes]) {
      this.invalidateNode(id);
    }
  }

  /** 把所有状态、窗口和文字条件恢复为初始值。 */
  reset(): void {
    this.rebuildSceneIndexes();
    this.invalidateAll();
    this.state.clear();
    for (const { path, leaf } of allStateLeaves(this.project.logic)) this.state.set(path, defaultFor(leaf));
    this.textConditions.clear();
    for (const { id, def } of allConditions(this.project.logic)) {
      if (def?.type === "text") this.textConditions.set(id, def.preview === true);
    }
    this.windows.clear();
    this.dockLayouts.clear();
    this.lastSceneDockLayouts.clear();
    for (const winId of allWindowIds(this.project.scene)) {
      const n = this.project.scene.nodes[winId];
      const isRoot = winId === this.project.scene.root;
      const open = isRoot ? true : Boolean(n?.initialOpen ?? n?.props?.open ?? false);
      const modal = isRoot ? false : Boolean(n?.modal ?? n?.props?.modal ?? n?.type === "Dialog");
      this.windows.set(winId, { open, modal });
      if (isPartitionedWindow(n)) {
        this.dockLayouts.set(winId, normalizeDockLayout(this.project.scene, n));
        this.lastSceneDockLayouts.set(winId, JSON.stringify(n?.dockLayout ?? null));
      }
    }
    this.docks.clear();
    this.floatBoxes.clear();
    this.lastSceneFloatBoxProps.clear();
    for (const [id, n] of Object.entries(this.project.scene.nodes)) {
      if (n.type === "Dock") this.docks.set(id, n.props?.visible !== false);
      if (n.type === "FloatBox") {
        this.floatBoxes.set(id, {
          floating: Boolean(n.props?.floating ?? false),
          ...(typeof n.props?.floatX === "number" ? { floatX: n.props.floatX } : {}),
          ...(typeof n.props?.floatY === "number" ? { floatY: n.props.floatY } : {}),
          ...(typeof n.props?.floatWidth === "number" ? { floatWidth: n.props.floatWidth } : {}),
          ...(typeof n.props?.floatHeight === "number" ? { floatHeight: n.props.floatHeight } : {}),
        });
        this.lastSceneFloatBoxProps.set(id, JSON.stringify(n.props ?? null));
      }
    }
    this.menuChecked.clear();
    for (const [itemId, item] of this.menuItemById) {
      if (item.checked !== undefined) this.menuChecked.set(itemId, Boolean(item.checked));
    }
    this.clockMs = 0;
    for (const [name, def] of Object.entries(this.project.logic.actions ?? {})) {
      if (!this.actionSims.has(name)) {
        this.actionSims.set(name, {
          mode: "real",
          delayMs: 0,
          result: def?.mockResult,
          errorMessage: def?.mockError,
        });
      }
    }
    this.runningActions.clear();
    this.hoveredControls.clear();
    this.pressedControls.clear();
  }

  /** 换成新加载的项目；状态结构没变的部分保留当前值。 */
  adopt(project: RuntimeProject): void {
    const prevSceneDockLayouts = new Map(this.lastSceneDockLayouts);
    const prevSceneFloatBoxProps = new Map(this.lastSceneFloatBoxProps);
    const oldState = new Map(this.state);
    const oldConditions = new Map(this.textConditions);
    const oldWindows = new Map(this.windows);
    const oldDocks = new Map(this.docks);
    const oldDockLayouts = new Map(this.dockLayouts);
    const oldFloatBoxes = new Map(this.floatBoxes);
    const oldSims = new Map(this.actionSims);
    this.project = project;
    this.exprCache.clear();
    this.fnCache.clear();
    this.reset();
    for (const [path, value] of oldState) {
      const leaf = findStateLeaf(project.logic, path);
      if (leaf && this.accepts(leaf, value)) this.state.set(path, cloneValue(value));
    }
    for (const [id, value] of oldConditions) {
      if (this.textConditions.has(id)) this.textConditions.set(id, value);
    }
    for (const [id, win] of oldWindows) {
      if (this.windows.has(id)) this.windows.set(id, win);
    }
    for (const [id, vis] of oldDocks) {
      if (this.docks.has(id)) this.docks.set(id, vis);
    }
    for (const [winId, layout] of oldDockLayouts) {
      if (!this.dockLayouts.has(winId)) continue;
      const oldWinDockLayout = prevSceneDockLayouts.get(winId) ?? "null";
      const newWinDockLayout = this.lastSceneDockLayouts.get(winId) ?? "null";
      if (oldWinDockLayout === newWinDockLayout) {
        this.dockLayouts.set(winId, normalizeDockLayout(project.scene, { ...project.scene.nodes[winId], dockLayout: layout }));
      }
    }
    for (const [id, fb] of oldFloatBoxes) {
      if (this.floatBoxes.has(id)) {
        const oldProps = prevSceneFloatBoxProps.get(id) ?? "null";
        const newProps = this.lastSceneFloatBoxProps.get(id) ?? "null";
        if (oldProps === newProps) this.floatBoxes.set(id, { ...fb });
      }
    }
    for (const [name, sim] of oldSims) {
      this.actionSims.set(name, sim);
    }
    this.syncActiveTabsWithVisibility();
  }

  /** 触发 app.start，预览开始时调用一次。 */
  start(): void {
    this.dispatch(null, "app.start");
  }

  // ---------- 停靠排列读写与操作 ----------

  getDockLayout(winId = this.project.scene.root): DockLayoutSpec {
    const existing = this.dockLayouts.get(winId);
    if (existing) return cloneDockLayout(existing);
    return normalizeDockLayout(this.project.scene, this.project.scene.nodes[winId]);
  }

  setDockLayout(layout: DockLayoutSpec, winId = this.project.scene.root): void {
    const winNode = this.project.scene.nodes[winId];
    const normalized = normalizeDockLayout(this.project.scene, {
      ...(winNode ?? { type: "Window", width: 1280, height: 800 }),
      dockLayout: layout,
    });
    this.dockLayouts.set(winId, normalized);
    this.lastSceneDockLayouts.set(winId, JSON.stringify(winNode?.dockLayout ?? null));
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.notifyChanged();
  }

  dockTabify(targetDockId: string, dockId: string, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockTabify(cur, this.project.scene, this.project.scene.nodes[winId], targetDockId, dockId);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`叠入页签组 ${targetDockId}`] });
    this.notifyChanged();
  }

  dockActivateTab(dockId: string, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockActivateTab(cur, this.project.scene, this.project.scene.nodes[winId], dockId);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`切换激活页签 ${dockId}`] });
    this.notifyChanged();
  }

  dockFloat(dockId: string, rect?: Partial<DockFloatRect>, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockFloat(cur, this.project.scene, this.project.scene.nodes[winId], dockId, rect);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`浮出为独立面板`] });
    this.notifyChanged();
  }

  dockRestore(dockId: string, targetArea?: DockArea, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockRestore(cur, this.project.scene, this.project.scene.nodes[winId], dockId, targetArea);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`从浮窗恢复停靠`] });
    this.notifyChanged();
  }

  dockMoveToArea(dockId: string, area: DockArea, index?: number, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockMoveToArea(cur, this.project.scene, this.project.scene.nodes[winId], dockId, area, index);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`吸附到停靠区 ${area}`] });
    this.notifyChanged();
  }

  dockSplit(
    targetDockId: string,
    dockId: string,
    position: "before" | "after" = "after",
    orientation?: DockSplitOrientation,
    winId = this.project.scene.root,
  ): void {
    const cur = this.getDockLayout(winId);
    const next = dockSplit(
      cur,
      this.project.scene,
      this.project.scene.nodes[winId],
      targetDockId,
      dockId,
      position,
      orientation,
    );
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠 ${dockId}`, lines: [`与 ${targetDockId} 并排（${position}）`] });
    this.notifyChanged();
  }

  dockResizeArea(area: DockArea, size: number, winId = this.project.scene.root): void {
    const cur = this.getDockLayout(winId);
    const next = dockResizeArea(cur, this.project.scene, this.project.scene.nodes[winId], area, size);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠区 ${area}`, lines: [`调整尺寸为 ${next.areas[area].size}px`] });
    this.notifyChanged();
  }

  /** shown：屏幕上各可见组的像素（分隔条矩形上带着）。给了，size 就按屏幕像素算，见 dock.ts 的 dockResizeSplit。 */
  dockResizeSplit(
    area: DockArea,
    groupIndex: number,
    size: number,
    winId = this.project.scene.root,
    shown?: DockSplitShown[],
  ): void {
    const cur = this.getDockLayout(winId);
    const next = dockResizeSplit(cur, this.project.scene, this.project.scene.nodes[winId], area, groupIndex, size, shown);
    this.dockLayouts.set(winId, next);
    this.layoutDirty = true;
    this.releaseDockedPointerStates();
    this.hooks.onLog({ title: `停靠区 ${area}`, lines: [`调整并排组 #${groupIndex} 尺寸为 ${Math.round(size)}px`] });
    this.notifyChanged();
  }

  /** 当停靠面板显隐变化后，若某个页签组的当前 active 面板已隐藏，则自动切换到同组内仍可见的页签。 */
  private syncActiveTabsWithVisibility(): boolean {
    let anyChanged = false;
    for (const [winId, layout] of this.dockLayouts) {
      let next: DockLayoutSpec | null = null;
      for (const area of ["top", "bottom", "left", "right"] as const) {
        const groups = layout.areas[area]?.groups ?? [];
        for (let gi = 0; gi < groups.length; gi++) {
          const group = groups[gi];
          const activeId = group.active ?? group.tabs[0];
          if (activeId && !this.view(activeId).visible) {
            const firstVisible = group.tabs.find((tid) => this.view(tid).visible);
            if (firstVisible && firstVisible !== group.active) {
              if (!next) next = cloneDockLayout(layout);
              next.areas[area].groups[gi].active = firstVisible;
            }
          }
        }
      }
      if (next) {
        this.dockLayouts.set(winId, next);
        this.layoutDirty = true;
        anyChanged = true;
      }
    }
    return anyChanged;
  }

  dockSetVisible(dockId: string, visible: boolean): void {
    if (!this.project.scene.nodes[dockId]) return;
    const winId = this.windowOf(dockId);
    if (visible) {
      const curLayout = this.getDockLayout(winId);
      this.dockLayouts.set(winId, dockActivateTab(curLayout, this.project.scene, this.project.scene.nodes[winId], dockId));
      this.layoutDirty = true;
    }
    this.runBatch(`停靠面板 ${dockId}`, false, false, (batch) => {
      const dockObj = this.project.logic.objects?.[dockId];
      const visExpr = dockObj?.props?.visible;
      if (typeof visExpr === "string" && findStateLeaf(this.project.logic, visExpr)) {
        this.assign(batch, visExpr, visible);
        return;
      }
      const cur = batch.docks.get(dockId) ?? true;
      if (cur !== visible) {
        batch.docks.set(dockId, visible);
        batch.lines.push(`停靠面板 ${dockId}: ${cur ? "显示 → 隐藏" : "隐藏 → 显示"}`);
      }
    });
  }

  /** 统一的停靠指令入口（供测试步骤与 Qt 侧桥接调用）。 */
  dockOp(op: string, args: Record<string, Value> = {}): void {
    const dockId = String(args.dock ?? "");
    const targetId = String(args.target ?? "");
    if (op === "tabify") {
      this.dockTabify(targetId, dockId);
    } else if (op === "tab" || op === "activateTab") {
      this.dockActivateTab(dockId);
    } else if (op === "float") {
      this.dockFloat(dockId, {
        ...(typeof args.x === "number" ? { x: args.x } : {}),
        ...(typeof args.y === "number" ? { y: args.y } : {}),
        ...(typeof args.width === "number" ? { width: args.width } : {}),
        ...(typeof args.height === "number" ? { height: args.height } : {}),
      });
    } else if (op === "restore") {
      this.dockRestore(dockId, isDockArea(args.area) ? args.area : undefined);
    } else if (op === "move") {
      if (isDockArea(args.area)) {
        this.dockMoveToArea(dockId, args.area, typeof args.index === "number" ? args.index : undefined);
      }
    } else if (op === "split") {
      this.dockSplit(
        targetId,
        dockId,
        args.position === "before" ? "before" : "after",
        args.orientation === "horizontal" || args.orientation === "vertical" ? args.orientation : undefined,
      );
    } else if (op === "resize" || op === "resizeArea") {
      if (isDockArea(args.area)) {
        this.dockResizeArea(args.area, Number(args.size ?? 240));
      }
    } else if (op === "resizeSplit") {
      if (isDockArea(args.area)) {
        this.dockResizeSplit(args.area, Number(args.groupIndex ?? 0), Number(args.size ?? 200));
      }
    } else if (op === "close") {
      this.dockSetVisible(dockId, false);
    } else if (op === "open") {
      this.dockSetVisible(dockId, true);
    } else if (op === "toggle") {
      this.dockSetVisible(dockId, !this.view(dockId).visible);
    }
  }

  // ---------- 读取 ----------

  private parse(source: string | number | boolean): Expr {
    const key = String(source);
    let expr = this.exprCache.get(key);
    if (!expr) {
      expr = parseExpr(key);
      this.exprCache.set(key, expr);
    }
    return expr;
  }

  private recordDep(path: string): void {
    const n = this.trackingStack.length;
    if (n > 0) {
      this.trackingStack[n - 1].add(path);
    }
  }

  private resolve(
    path: string,
    state: Map<string, Value>,
    stack: string[],
    scope: Record<string, Value> = {},
  ): Value {
    if (path in scope) return scope[path];
    for (const key of Object.keys(scope)) {
      if (path.startsWith(key + ".")) {
        return readTailValue(scope[key], path.slice(key.length + 1).split("."));
      }
    }
    const track = state === this.state && this.trackingStack.length > 0;
    if (track) this.recordDep(path);
    if (state.has(path)) return state.get(path)!;
    if (path === "clock" || path === "clock.ms") {
      if (track) this.recordDep("clock");
      return this.clockMs;
    }
    if (path === "clock.sec") {
      if (track) this.recordDep("clock");
      return Math.floor(this.clockMs / 1000);
    }
    // 支持读取数组或字符串状态的 .length 等尾部属性，例如 layers.items.length
    const parts = path.split(".");
    for (let i = parts.length - 1; i >= 1; i--) {
      const prefix = parts.slice(0, i).join(".");
      if (state.has(prefix)) {
        if (track) this.recordDep(prefix);
        return readTailValue(state.get(prefix)!, parts.slice(i));
      }
    }
    const condition = findCondition(this.project.logic, path);
    if (!condition) throw new Error(`未知的状态路径或条件代号“${path}”`);
    if (condition.type === "text") return this.textConditions.get(path) ?? false;
    if (stack.includes(path)) throw new Error(`条件循环引用：${[...stack, path].join(" → ")}`);
    return Boolean(
      evalExpr(this.parse(condition.expr ?? "false"), (p) => this.resolve(p, state, [...stack, path], scope)),
    );
  }

  private evaluate(
    source: string | number | boolean,
    state = this.state,
    scope: Record<string, Value> = {},
  ): Value {
    return evalExpr(this.parse(source), (path) => this.resolve(path, state, [], scope));
  }

  private evaluateStructured(
    raw: Value | Record<string, Value | string>,
    state: Map<string, Value>,
    scope: Record<string, Value>,
  ): Value {
    if (raw === null || typeof raw === "boolean" || typeof raw === "number") return raw;
    if (typeof raw === "string") return this.evaluate(raw, state, scope);
    if (Array.isArray(raw)) return raw.map((item) => this.evaluateStructured(item, state, scope));
    const result: Record<string, Value> = {};
    for (const [k, v] of Object.entries(raw)) {
      result[k] = this.evaluateStructured(v as Value, state, scope);
    }
    return result;
  }

  /** 预览工具条用：全部条件及其当前值。 */
  conditions(): Array<{ id: string; text: string; kind: "text" | "symbol"; value: boolean }> {
    return allConditions(this.project.logic).map(({ id, def }) => {
      let value = false;
      try {
        value = Boolean(this.resolve(id, this.state, []));
      } catch {
        // 写错的条件已经在问题栏报告，这里按假处理
      }
      return { id, text: def?.text ?? "", kind: def?.type === "text" ? "text" : "symbol", value };
    });
  }

  /** 预览工具条用：全部动作及其模拟设置。 */
  actions(): Array<{ name: string; text: string; sim: ActionSimConfig }> {
    return Object.entries(this.project.logic.actions ?? {}).map(([name, def]) => ({
      name,
      text: def?.text ?? "",
      sim: this.getActionSimulation(name),
    }));
  }

  getActionSimulation(name: string): ActionSimConfig {
    const existing = this.actionSims.get(name);
    if (existing) return { ...existing };
    const def = this.project.logic.actions?.[name];
    return {
      mode: "real",
      delayMs: 0,
      result: def?.mockResult,
      errorMessage: def?.mockError,
    };
  }

  setActionSimulation(name: string, config: Partial<ActionSimConfig>): void {
    const cur = this.getActionSimulation(name);
    this.actionSims.set(name, { ...cur, ...config });
    this.notifyChanged();
  }

  isWindowOpen(id: string): boolean {
    if (id === this.project.scene.root) return true;
    return this.windows.get(id)?.open ?? false;
  }

  isWindowModal(id: string): boolean {
    return this.windows.get(id)?.modal ?? false;
  }

  activeModalWindow(): string | null {
    let active: string | null = null;
    for (const [id, win] of this.windows) {
      if (id !== this.project.scene.root && win.open && win.modal) active = id;
    }
    return active;
  }

  private parentOf(nodeId: string): string | null {
    return this.parentById.get(nodeId) ?? null;
  }

  private windowOf(nodeId: string): string {
    return this.windowById.get(nodeId) ?? this.project.scene.root;
  }

  private isInsideSubtree(rootId: string, targetId: string): boolean {
    for (let cur: string | null = targetId; cur; cur = this.parentOf(cur)) {
      if (cur === rootId) return true;
    }
    return false;
  }

  getClock(): number {
    return this.clockMs;
  }

  setClock(ms: number): void {
    const next = Math.max(0, Math.round(Number(ms) || 0));
    const prev = this.clockMs;
    this.clockMs = next;
    this.invalidatePath("clock");
    const tickSteps = globalSteps(this.project.logic, "clock.tick");
    this.runBatch(`预览时钟 ${prev}ms → ${next}ms`, false, false, (batch) => {
      if (batch.work.has("clock")) batch.work.set("clock", next);
      if (batch.work.has("clock.ms")) batch.work.set("clock.ms", next);
      batch.lines.push(`clock: ${prev}ms → ${next}ms`);
      if (tickSteps) {
        this.runSteps(tickSteps, batch, 0, {
          value: next,
          $value: next,
          param: { ms: next, prevMs: prev, deltaMs: next - prev },
          $event: { ms: next, prevMs: prev, deltaMs: next - prev },
          ms: next,
          prevMs: prev,
          deltaMs: next - prev,
        });
      }
    });
    this.notifyChanged();
  }

  stateSnapshot(): Array<[string, Value]> {
    return [...this.state].map(([k, v]) => [k, cloneValue(v)]);
  }

  getState(path: string): Value | undefined {
    const val = this.state.get(path);
    return val === undefined ? undefined : cloneValue(val);
  }

  /**
   * 连续读很多控件时共用一份结果：由于 view(id) 已经按状态路径做持久缓存，这里直接调用 read()。
   */
  readSnapshot<T>(read: () => T): T {
    return read();
  }

  /**
   * 取出自上次调用以来受影响的节点 ID 集合（首次或全量重置后返回 "all"）。
   */
  consumeDirtyNodes(): Set<string> | "all" {
    if (this.allViewsDirty) {
      this.allViewsDirty = false;
      this.dirtyViewIds.clear();
      return "all";
    }
    const out = new Set(this.dirtyViewIds);
    this.dirtyViewIds.clear();
    return out;
  }

  /**
   * 增量计算布局：仅在尺寸、可见性、文字、栈页或停靠排列变化时重算 computeSceneLayout。
   */
  computeLayout(rootSize?: { width: number; height: number }): {
    boxes: Map<string, ComputedBox>;
    layoutChanged: boolean;
  } {
    if (this.pendingViewCheckIds.size > 0) {
      for (const id of [...this.pendingViewCheckIds]) {
        this.view(id);
      }
    }
    const sizeChanged =
      this.lastLayoutRootSize?.width !== rootSize?.width ||
      this.lastLayoutRootSize?.height !== rootSize?.height;
    if (!this.layoutDirty && !sizeChanged && this.cachedLayoutBoxes) {
      return { boxes: this.cachedLayoutBoxes, layoutChanged: false };
    }
    this.lastLayoutRootSize = rootSize ? { width: rootSize.width, height: rootSize.height } : undefined;
    this.cachedLayoutBoxes = computeSceneLayout(this.project.scene, {
      rootSize,
      isVisible: (nid) => this.view(nid).visible,
      getProp: (nid, prop) => this.view(nid).props[prop] ?? null,
      getDockLayout: (wid) => this.getDockLayout(wid),
    });
    this.layoutDirty = false;
    return { boxes: this.cachedLayoutBoxes, layoutChanged: true };
  }

  /**
   * 执行一次增量刷新：更新所有受影响节点的 view，并在需要时更新排版。
   */
  refresh(rootSize?: { width: number; height: number }): {
    dirtyNodes: Set<string> | "all";
    layoutChanged: boolean;
    boxes: Map<string, ComputedBox>;
  } {
    const dirtyNodes = this.consumeDirtyNodes();
    if (dirtyNodes === "all") {
      for (const id of Object.keys(this.project.scene.nodes)) {
        this.view(id);
      }
      for (const id of this.menuItemById.keys()) {
        this.view(id);
      }
    } else {
      for (const id of dirtyNodes) {
        this.view(id);
      }
    }
    const { boxes, layoutChanged } = this.computeLayout(rootSize);
    return { dirtyNodes, layoutChanged, boxes };
  }

  view(id: string): NodeView {
    const cached = this.viewCache.get(id);
    if (cached) return cached;

    const deps = new Set<string>();
    this.trackingStack.push(deps);
    let next: NodeView;
    try {
      next = this.computeView(id);
    } finally {
      this.trackingStack.pop();
      this.setNodeDeps(id, deps);
    }

    const prev = this.prevViewCache.get(id);
    if (doesViewAffectLayout(prev, next)) {
      this.layoutDirty = true;
    }
    this.viewCache.set(id, next);
    this.prevViewCache.set(id, next);
    this.pendingViewCheckIds.delete(id);
    return next;
  }

  private computeView(id: string): NodeView {
    const menuItem = !this.project.scene.nodes[id] ? this.menuItemById.get(id) : undefined;
    if (menuItem) {
      const object = this.project.logic.objects?.[id];
      const props: Record<string, Value> = {
        text: menuItem.text,
        checked: menuItem.dock ? this.view(menuItem.dock).visible : (this.menuChecked.get(id) ?? Boolean(menuItem.checked ?? false)),
        enabled: true,
      };
      let visible = true;
      for (const [key, expr] of Object.entries(object?.props ?? {})) {
        try {
          const val = this.evaluate(expr);
          if (key === "visible") visible = Boolean(val);
          else props[key] = val;
        } catch {
          // 忽略错误表达式
        }
      }
      return { visible, enabled: props.enabled !== false, props };
    }

    const node = this.project.scene.nodes[id];
    const decl = node ? declFor(this.project, node.type) : null;
    const object = this.project.logic.objects?.[id];
    const props: Record<string, Value> = {};
    const declared = decl?.props ?? {};
    for (const key of Object.keys(declared)) {
      const value = declared[key].default;
      if (value !== undefined) props[key] = cloneValue(value);
    }
    if (node?.title !== undefined && (node.type === "Dock" || node.type === "Dialog" || node.type === "FloatBox")) {
      props.title = node.title;
    }
    if (node?.props) {
      for (const [k, v] of Object.entries(node.props)) {
        if (k !== "visible") props[k] = cloneValue(v);
      }
    }
    if (node?.type === "FloatBox" && this.floatBoxes.has(id)) {
      const fb = this.floatBoxes.get(id)!;
      props.floating = fb.floating;
      if (fb.floatX !== undefined) props.floatX = fb.floatX;
      if (fb.floatY !== undefined) props.floatY = fb.floatY;
      if (fb.floatWidth !== undefined) props.floatWidth = fb.floatWidth;
      if (fb.floatHeight !== undefined) props.floatHeight = fb.floatHeight;
    }
    let visible = true;
    if (this.windows.has(id) && id !== this.project.scene.root) {
      visible = this.windows.get(id)!.open;
    } else if (node?.type === "Dock" && this.docks.has(id)) {
      visible = this.docks.get(id)!;
    } else if (node?.props?.visible !== undefined) {
      visible = Boolean(node.props.visible);
    }
    const computed = object?.props ?? {};
    for (const key of Object.keys(computed)) {
      try {
        const value = this.evaluate(computed[key]);
        if (key === "visible") visible = Boolean(value);
        else props[key] = cloneValue(value);
      } catch {
        // 表达式有错时保留静态值，错误已在问题栏报告
      }
    }
    if (object?.bind) {
      this.recordDep(object.bind);
      if (this.state.has(object.bind)) {
        props.value = cloneValue(this.state.get(object.bind)!);
      }
    }
    if (node?.type === "SpinBox") {
      const min = Number(props.min ?? 0);
      const max = Number(props.max ?? 9999);
      const val = Number(props.value ?? 0);
      if (Number.isFinite(val)) {
        props.value = Math.max(min, Math.min(max, val));
      }
    }
    if (visible) {
      const stackParent = this.stackParentById.get(id);
      if (stackParent) {
        const pView = this.view(stackParent.parentId);
        const activeIdx = Number(pView.props.currentIndex ?? 0);
        const activePage = String(pView.props.currentPage ?? "");
        const idx = stackParent.children.indexOf(id);
        const isActive = activePage ? id === activePage : idx === activeIdx;
        if (!isActive) visible = false;
      }
    }
    const enabled = props.enabled !== false;
    const selected = Boolean(props.selected ?? props.checked ?? (props.variant === "primary"));
    const resolvedState = resolveStateImage(decl, {
      enabled,
      pressed: this.pressedControls.has(id) || props.pressed === true,
      hovered: this.hoveredControls.has(id) || props.hovered === true,
      selected,
    });
    return {
      visible,
      enabled,
      props,
      ...(resolvedState ? { visualState: resolvedState.state, stateImage: resolvedState.file } : {}),
    };
  }

  /** 所有变更都从这里通知界面：先清掉已经不可能被鼠标碰到的控件的悬停和按下，view() 只读不改。 */
  private notifyChanged(): void {
    for (const set of [this.hoveredControls, this.pressedControls]) {
      for (const id of [...set]) {
        if (!this.canTakePointer(id)) {
          set.delete(id);
          this.invalidateNode(id);
        }
      }
    }
    this.hooks.onChange();
  }

  /** 叠页签组里显示的那个面板；激活页签已关闭时是组里第一个没关闭的。 */
  private isShownDockTab(dockId: string): boolean {
    const layout = this.dockLayouts.get(this.windowOf(dockId));
    const loc = layout ? dockFindLocation(layout, dockId) : null;
    if (loc?.kind !== "area") return true;
    const { active, tabs } = loc.group;
    const shown = active && tabs.includes(active) && this.view(active).visible ? active : tabs.find((tid) => this.view(tid).visible);
    return shown === dockId;
  }

  private releasePointerOn(nodeId: string, event: string): void {
    const wasHovered = event === "mouseleave" && this.hoveredControls.delete(nodeId);
    const wasPressed = (event === "mouseleave" || event === "mouseup") && this.pressedControls.delete(nodeId);
    if (wasHovered || wasPressed) {
      this.invalidateNode(nodeId);
      this.notifyChanged();
    }
  }

  /** 控件此刻显示在界面上并且可用。view().visible 只看控件自己，这里还看所属窗口、容器、停靠页签和打开的模态框。 */
  private canTakePointer(id: string): boolean {
    const scene = this.project.scene;
    if (!this.view(id).enabled) return false;
    const modalWin = this.activeModalWindow();
    if (modalWin && !this.isInsideSubtree(modalWin, id)) return false;
    for (let cur: string | null = id; cur && cur !== scene.root; cur = this.parentOf(cur)) {
      const node = scene.nodes[cur];
      const curView = this.view(cur);
      if (!node || !curView.visible) return false;
      if (node.type === "Dock" && !this.isShownDockTab(cur)) return false;
      // 浮出的 FloatBox 脱离原来的容器单独显示，原父面板切走或关闭都不影响它
      if (node.type === "FloatBox" && curView.props.floating) return true;
    }
    return true;
  }

  /** 面板重排后控件换了位置，不一定还在鼠标底下，而界面不一定收到移出事件，所以停靠面板里的悬停和按下一律清掉。 */
  private releaseDockedPointerStates(): void {
    const scene = this.project.scene;
    for (const set of [this.hoveredControls, this.pressedControls]) {
      for (const id of [...set]) {
        for (let cur: string | null = id; cur; cur = this.parentOf(cur)) {
          const node = scene.nodes[cur];
          // 已浮出的 FloatBox 脱离了原面板，不跟着重排
          if (node?.type === "FloatBox" && this.view(cur).props.floating) break;
          if (node?.type === "Dock") {
            set.delete(id);
            this.invalidateNode(id);
            break;
          }
        }
      }
    }
  }

  // ---------- 事件 ----------

  toggleTextCondition(id: string): void {
    const next = !(this.textConditions.get(id) ?? false);
    this.textConditions.set(id, next);
    this.invalidatePath(id);
    this.hooks.onLog({ title: `条件 ${id}`, lines: [`手动切换：${next ? "否 → 是" : "是 → 否"}`] });
    this.notifyChanged();
  }

  /** nodeId 为 null 表示全局事件。 */
  dispatch(nodeId: string | null, event: string, value?: Value): void {
    const { logic, scene } = this.project;

    if (nodeId !== null) {
      const modalWin = this.activeModalWindow();
      if (modalWin && !this.isInsideSubtree(modalWin, nodeId)) {
        // 事件本身被拦下，但鼠标确实离开、松开了，界面状态要跟上
        this.releasePointerOn(nodeId, event);
        if (event !== "mouseenter" && event !== "mouseleave") {
          this.hooks.onLog({ title: `${nodeId}.${event}`, lines: [`已忽略：模态窗口 ${modalWin} 正在打开`] });
        }
        return;
      }
    }

    const menuItem = nodeId && !scene.nodes[nodeId] ? this.menuItemById.get(nodeId) : undefined;
    if (menuItem && nodeId) {
      const object = logic.objects?.[nodeId];
      const steps = object?.events?.[event];
      if (!steps && event !== "click") return;
      const view = this.view(nodeId);
      if (!view.visible || !view.enabled) {
        this.hooks.onLog({ title: `${nodeId}.${event}`, lines: [`已忽略：菜单项${view.visible ? "不可用" : "不可见"}`] });
        return;
      }
      this.runBatch(`${nodeId}.${event}`, false, false, (batch) => {
        let nextChecked = Boolean(view.props.checked);
        if (menuItem.group || menuItem.kind === "radio") {
          nextChecked = true;
          batch.menuChecked.set(nodeId, true);
          if (menuItem.group) {
            for (const [otherId, item] of this.menuItemById) {
              if (item.group === menuItem.group && otherId !== nodeId) {
                batch.menuChecked.set(otherId, false);
              }
            }
          }
        } else if ((menuItem.checkable || menuItem.kind === "check") && !menuItem.dock) {
          nextChecked = !nextChecked;
          batch.menuChecked.set(nodeId, nextChecked);
        }
        if (steps) {
          this.runSteps(steps, batch, 0, this.scopeForValue(value !== undefined ? value : nextChecked));
        } else if (menuItem.emit) {
          this.runSteps([{ emit: menuItem.emit }], batch, 0, {});
        } else if (menuItem.dock) {
          this.toggleDockInBatch(batch, menuItem.dock);
        }
      });
      return;
    }

    const node = nodeId ? scene.nodes[nodeId] : undefined;
    if (nodeId && node?.type === "FloatBox") {
      const view = this.view(nodeId);
      if (view.visible && view.enabled) {
        if (event === "toggle" || (event === "change" && typeof value === "boolean")) {
          const cur = this.floatBoxes.get(nodeId) ?? { floating: Boolean(view.props.floating) };
          const nextFloat = typeof value === "boolean" ? value : !cur.floating;
          if (cur.floating !== nextFloat) {
            this.floatBoxes.set(nodeId, { ...cur, floating: nextFloat });
            this.invalidateNode(nodeId);
            this.layoutDirty = true;
            this.hooks.onLog({
              title: `${nodeId}.${event}`,
              lines: [`浮动嵌入框 ${nodeId}: ${nextFloat ? "嵌入 → 浮动" : "浮动 → 嵌入"}`],
            });
            this.notifyChanged();
          }
        } else if (event === "move" && value && typeof value === "object" && !Array.isArray(value)) {
          const cur = this.floatBoxes.get(nodeId) ?? { floating: Boolean(view.props.floating) };
          const nx = typeof value.x === "number" ? Math.round(value.x) : cur.floatX;
          const ny = typeof value.y === "number" ? Math.round(value.y) : cur.floatY;
          const nw = typeof value.width === "number" ? Math.round(value.width) : cur.floatWidth;
          const nh = typeof value.height === "number" ? Math.round(value.height) : cur.floatHeight;
          this.floatBoxes.set(nodeId, {
            ...cur,
            floating: true,
            ...(nx !== undefined ? { floatX: nx } : {}),
            ...(ny !== undefined ? { floatY: ny } : {}),
            ...(nw !== undefined ? { floatWidth: nw } : {}),
            ...(nh !== undefined ? { floatHeight: nh } : {}),
          });
          this.invalidateNode(nodeId);
          this.layoutDirty = true;
          this.notifyChanged();
        }
      }
    }
    if (nodeId) {
      if (event === "mouseleave" || event === "mouseup") {
        this.releasePointerOn(nodeId, event);
      } else if (event === "mouseenter" || event === "mousedown") {
        if (this.canTakePointer(nodeId)) {
          if (event === "mouseenter") this.hoveredControls.add(nodeId);
          else this.pressedControls.add(nodeId);
          this.invalidateNode(nodeId);
          this.notifyChanged();
        }
      } else if (event === "change" && (value === "pressed" || value === "released")) {
        const decl = node ? declFor(this.project, node.type) : null;
        if (decl?.stateImages) {
          if (this.canTakePointer(nodeId)) {
            if (value === "pressed") this.pressedControls.add(nodeId);
            else this.pressedControls.delete(nodeId);
            this.invalidateNode(nodeId);
            this.notifyChanged();
          }
          if (!logic.objects?.[nodeId]?.bind && !logic.objects?.[nodeId]?.events?.change) return;
        }
      }
    }

    const object = nodeId ? logic.objects?.[nodeId] : undefined;
    const steps = nodeId ? object?.events?.[event] : globalSteps(logic, event);
    const bindTarget = nodeId && (event === "change" || (node?.type === "FloatBox" && event === "toggle")) && value !== undefined ? object?.bind : undefined;
    if (!steps && !bindTarget) return;

    const title = nodeId ? `${nodeId}.${event}` : `全局事件 ${event}`;
    if (nodeId) {
      const winId = this.windowOf(nodeId);
      if (winId !== scene.root && !this.view(winId).visible) {
        if (event !== "mouseenter" && event !== "mouseleave") {
          this.hooks.onLog({ title, lines: [`已忽略：所属窗口 ${winId} 未打开`] });
        }
        return;
      }
      const view = this.view(nodeId);
      if (!view.visible || !view.enabled) {
        // 鼠标移入移出很频繁，被忽略时不记录，免得刷屏
        if (event !== "mouseenter" && event !== "mouseleave") {
          this.hooks.onLog({ title, lines: [`已忽略：控件${view.visible ? "不可用" : "不可见"}`] });
        }
        return;
      }
      if ((node?.type === "SpinBox" || node?.type === "Slider" || node?.type === "ScrollBar") && event === "change" && typeof value === "number" && Number.isFinite(value)) {
        const min = Number(view.props.min ?? 0);
        const max = Number(view.props.max ?? (node.type === "SpinBox" ? 9999 : 100));
        value = Math.max(min, Math.min(max, value));
      } else if (node?.type === "ComboBox" && event === "change" && typeof value === "string") {
        const options = String(view.props.options ?? "")
          .split(",")
          .map((s) => s.trim())
          .filter(Boolean);
        if (!options.includes(value)) return;
      } else if ((node?.type === "List" || node?.type === "Tree") && (event === "change" || event === "select") && typeof value === "number") {
        const items = Array.isArray(view.props.items) ? view.props.items : [];
        if (!Number.isInteger(value) || value < 0 || value >= items.length) return;
      }
    }

    const scope = this.scopeForValue(value);
    // window.click 每次点击都会触发，没有引起变化时不记录
    this.runBatch(title, false, event === "window.click", (batch) => {
      if (bindTarget) this.assign(batch, bindTarget, value!);
      if (steps) this.runSteps(steps, batch, 0, scope);
    });
  }

  private scopeForValue(value?: Value): Record<string, Value> {
    if (value === undefined) return {};
    const scope: Record<string, Value> = { value, $value: value, $event: value };
    if (typeof value === "object" && value !== null && !Array.isArray(value)) {
      scope.param = value;
      for (const [k, v] of Object.entries(value)) scope[k] = v;
    }
    return scope;
  }

  private toggleDockInBatch(batch: Batch, dockId: string): void {
    const dockObj = this.project.logic.objects?.[dockId];
    const visExpr = dockObj?.props?.visible;
    let nextVis = true;
    if (typeof visExpr === "string" && findStateLeaf(this.project.logic, visExpr)) {
      const cur = Boolean(batch.work.get(visExpr));
      nextVis = !cur;
      this.assign(batch, visExpr, nextVis);
    } else {
      const cur = batch.docks.get(dockId) ?? true;
      nextVis = !cur;
      batch.docks.set(dockId, nextVis);
      batch.lines.push(`停靠面板 ${dockId}: ${cur ? "显示 → 隐藏" : "隐藏 → 显示"}`);
    }
    if (nextVis) {
      const winId = this.windowOf(dockId);
      const curLayout = this.getDockLayout(winId);
      this.dockLayouts.set(winId, dockActivateTab(curLayout, this.project.scene, this.project.scene.nodes[winId], dockId));
      this.layoutDirty = true;
    }
  }

  /** 外部（如用户按 Esc 或点击窗口标题栏关闭按钮）关闭窗口时，将窗口开关同步回运行层。 */
  closeWindow(id: string): void {
    const win = this.windows.get(id);
    if (!win || !win.open) return;
    this.runBatch(`关闭窗口 ${id}`, false, false, (batch) => {
      batch.windows.set(id, { open: false, modal: win.modal });
      batch.lines.push(`关闭窗口 ${id}`);
      const steps = this.project.logic.objects?.[id]?.events?.["close"];
      if (steps) this.runSteps(steps, batch, 0, {});
    });
  }

  /** 等待所有正在执行的异步动作或延迟模拟完成。 */
  async waitForIdle(): Promise<void> {
    while (this.activePromises.size > 0) {
      await Promise.all([...this.activePromises]);
    }
  }

  /** 执行一个业务事件的模拟步骤，就像业务没有接上时那样。 */
  simulate(name: string, args: Record<string, Value> = {}): void {
    const def = businessEvent(this.project.logic, name);
    if (!def) return;
    const scope: Record<string, Value> = { param: args, ...args };
    this.runBatch(`业务事件 ${name}（模拟）`, true, false, (batch) => {
      if (def.simulate) this.runSteps(def.simulate, batch, 0, scope);
    });
  }

  /**
   * 业务触发请求事件（业务 → 界面）。成功返回 null，否则返回原因。
   */
  request(name: string, args: Record<string, Value> = {}): string | null {
    const def = requestEvent(this.project.logic, name);
    if (!def) return `“${name}”不是请求事件`;
    for (const [paramName, paramType] of Object.entries(def.params ?? {})) {
      const v = args[paramName];
      if (v === undefined) return `请求事件 ${name} 缺少参数 ${paramName}`;
      if (typeof v !== paramType) return `请求事件 ${name} 的参数 ${paramName} 需要 ${paramType}，得到 ${show(v)}`;
    }
    const scope: Record<string, Value> = { param: args, ...args };
    this.runBatch(`请求事件 ${name}`, false, false, (batch) => {
      if (def.steps) this.runSteps(def.steps, batch, 0, scope);
    });
    return null;
  }

  /**
   * 业务把自己管的状态写回界面。成功返回 null，否则返回原因。
   * 只能写标了 owner: business 的状态。
   */
  setBusinessState(path: string, value: Value): string | null {
    const leaf = findStateLeaf(this.project.logic, path);
    if (!leaf) return `“${path}”不是状态路径`;
    if (!isBusinessState(leaf)) return `“${path}”是界面自己管的状态，业务不能写`;
    if (!this.accepts(leaf, value)) return `${path} 需要 ${this.expected(leaf)}，得到 ${show(value)}`;
    const before = this.state.get(path);
    if (sameStateValue(before, value)) return null;
    this.state.set(path, cloneValue(value));
    this.invalidatePath(path);
    this.hooks.onLog({ title: "业务写回状态", lines: [`${path}: ${show(before ?? null)} → ${show(value)}`] });
    this.notifyChanged();
    return null;
  }

  /** 执行一批步骤，让结果一次生效，再处理这一批里 emit 的业务事件和 call 的动作。 */
  private runBatch(title: string, simulating: boolean, quietWhenEmpty: boolean, body: (batch: Batch) => void): void {
    const work = new Map<string, Value>();
    for (const [k, v] of this.state) work.set(k, cloneValue(v));
    const windows = new Map<string, { open: boolean; modal: boolean }>();
    for (const [k, v] of this.windows) windows.set(k, { ...v });
    const docks = new Map<string, boolean>(this.docks);
    const menuChecked = new Map<string, boolean>(this.menuChecked);

    const batch: Batch = { work, windows, docks, menuChecked, lines: [], pending: [], calls: [], simulating };
    body(batch);

    let changed = false;
    const changedStatePaths: string[] = [];
    const changedWinIds: string[] = [];
    const changedDockIds: string[] = [];
    const changedMenuIds: string[] = [];

    for (const [path, next] of batch.work) {
      const before = this.state.get(path);
      if (!sameStateValue(before, next)) {
        batch.lines.push(`${path}: ${show(before ?? null)} → ${show(next)}`);
        changedStatePaths.push(path);
        changed = true;
      }
    }
    for (const [id, next] of batch.windows) {
      const before = this.windows.get(id);
      if (!before || before.open !== next.open || before.modal !== next.modal) {
        changedWinIds.push(id);
        changed = true;
      }
    }
    for (const [id, next] of batch.docks) {
      if (this.docks.get(id) !== next) {
        changedDockIds.push(id);
        changed = true;
      }
    }
    for (const [id, next] of batch.menuChecked) {
      if (this.menuChecked.get(id) !== next) {
        changedMenuIds.push(id);
        changed = true;
      }
    }

    this.state = batch.work;
    this.windows = batch.windows;
    this.docks = batch.docks;
    this.menuChecked = batch.menuChecked;

    for (const path of changedStatePaths) this.invalidatePath(path);
    for (const id of changedWinIds) {
      this.invalidateNode(id);
      this.layoutDirty = true;
    }
    for (const id of changedDockIds) {
      this.invalidateNode(id);
      this.layoutDirty = true;
    }
    for (const id of changedMenuIds) this.invalidateNode(id);

    if (this.syncActiveTabsWithVisibility()) {
      changed = true;
    }
    if (batch.lines.length > 0 || !quietWhenEmpty) {
      if (batch.lines.length === 0) batch.lines.push("（没有状态变化）");
      this.hooks.onLog({ title, lines: batch.lines });
    }
    if (changed) this.notifyChanged();

    for (const item of batch.pending) this.raise(item.name, item.args);
    for (const call of batch.calls) this.executeAction(call.name, call.input);
  }

  private raise(name: string, args: Record<string, Value>): void {
    if (this.hooks.raiseBusiness?.(name, args)) return;
    if (this.businessChain >= MAX_BUSINESS_CHAIN) {
      this.hooks.onLog({
        title: `业务事件 ${name}`,
        lines: [`错误：业务事件连锁超过 ${MAX_BUSINESS_CHAIN} 个，可能是模拟步骤互相触发`],
      });
      return;
    }
    this.businessChain++;
    try {
      this.simulate(name, args);
    } finally {
      this.businessChain--;
    }
  }

  finishActionSuccess(name: string, resultVal?: Value): void {
    const def = this.project.logic.actions?.[name];
    if (!def) return;
    this.runningActions.delete(name);
    const res = resultVal === undefined ? (def.mockResult ?? null) : resultVal;
    const scope: Record<string, Value> = { result: res };
    this.runBatch(`动作 ${name} 完成`, false, false, (batch) => {
      if (def.onSuccess) this.runSteps(def.onSuccess, batch, 0, scope);
    });
  }

  finishActionError(name: string, message: string): void {
    const def = this.project.logic.actions?.[name];
    if (!def) return;
    this.runningActions.delete(name);
    const errObj: Record<string, Value> = { message };
    const scope: Record<string, Value> = { error: errObj, "error.message": message };
    this.runBatch(`动作 ${name} 失败`, false, false, (batch) => {
      batch.lines.push(`失败：${message}`);
      if (def.onError) this.runSteps(def.onError, batch, 0, scope);
    });
  }

  runActionDefault(name: string, input: Record<string, Value>): void {
    const def = this.project.logic.actions?.[name];
    if (!def) return;
    const sim = this.getActionSimulation(name);
    const source = this.project.actionSources?.[name];
    if (!source) {
      this.finishActionSuccess(name, sim.result !== undefined ? sim.result : (def.mockResult ?? null));
      return;
    }
    try {
      const fn = this.compileFunction(`action:${name}`, source);
      const out = fn(input as Record<string, unknown>, input);
      if (out && typeof (out as Promise<unknown>).then === "function") {
        this.runningActions.add(name);
        const tracked = (out as Promise<Value>).then(
          (res) => this.finishActionSuccess(name, res ?? null),
          (err) => this.finishActionError(name, (err as Error)?.message ?? String(err)),
        );
        const cleanup: Promise<void> = tracked.then(
          () => {
            this.activePromises.delete(cleanup);
          },
          () => {
            this.activePromises.delete(cleanup);
          },
        );
        this.activePromises.add(cleanup);
      } else {
        this.finishActionSuccess(name, (out as Value) ?? null);
      }
    } catch (e) {
      this.finishActionError(name, (e as Error).message);
    }
  }

  private executeAction(name: string, input: Record<string, Value>): void {
    const def = this.project.logic.actions?.[name];
    if (!def) {
      this.hooks.onLog({ title: `动作 ${name}`, lines: [`错误：动作“${name}”没有定义`] });
      return;
    }
    if (this.runningActions.has(name)) {
      this.hooks.onLog({ title: `动作 ${name}`, lines: ["已忽略：该动作正在进行中"] });
      return;
    }
    const sim = this.getActionSimulation(name);

    const runNow = () => {
      if (sim.mode === "error") {
        this.finishActionError(name, sim.errorMessage ?? def.mockError ?? "模拟失败");
        return;
      }
      if (sim.mode === "success") {
        this.finishActionSuccess(name, sim.result !== undefined ? sim.result : (def.mockResult ?? null));
        return;
      }
      if (this.hooks.raiseAction?.(name, input)) {
        this.runningActions.add(name);
        return;
      }
      this.runActionDefault(name, input);
    };

    if (sim.delayMs > 0 && typeof setTimeout === "function") {
      this.runningActions.add(name);
      this.hooks.onLog({ title: `动作 ${name}`, lines: [`开始（延迟 ${sim.delayMs} ms）`] });
      const timerPromise = new Promise<void>((resolve) => {
        setTimeout(() => {
          try {
            runNow();
          } finally {
            resolve();
          }
        }, sim.delayMs);
      });
      const cleanup: Promise<void> = timerPromise.then(
        () => {
          this.activePromises.delete(cleanup);
        },
        () => {
          this.activePromises.delete(cleanup);
        },
      );
      this.activePromises.add(cleanup);
    } else {
      runNow();
    }
  }

  private compileFunction(
    cacheKey: string,
    source: string,
  ): (state: Record<string, unknown>, args: Record<string, Value>) => unknown {
    let fn = this.fnCache.get(cacheKey);
    if (!fn) {
      const transformed = `"use strict";\n` + source.replace(/^\s*export\s+default\s+/m, "return ");
      let factory: (...shadowed: undefined[]) => (state: Record<string, unknown>, args: Record<string, Value>) => unknown;
      try {
        factory = new Function(...SHADOWED_DOM_GLOBALS, transformed) as typeof factory;
      } catch (err) {
        // Qt QJSEngine (QV4) 不支持 async/await 语法糖，将其降级为 Promise.resolve().then(...)
        if (err instanceof SyntaxError && /\basync\b/.test(transformed)) {
          const downleveled = transformed
            .replace(
              /\breturn\s+async\s+function\s*([A-Za-z0-9_$]*)\s*\(([^)]*)\)\s*\{([\s\S]*)\}\s*$/,
              "return function $1($2) { return Promise.resolve().then(() => {$3}); }",
            )
            .replace(
              /\breturn\s+async\s*\(([^)]*)\)\s*=>\s*\{([\s\S]*)\}\s*$/,
              "return ($1) => Promise.resolve().then(() => {$2})",
            )
            .replace(
              /\breturn\s+async\s*\(([^)]*)\)\s*=>\s*([\s\S]*)$/,
              "return ($1) => Promise.resolve().then(() => ($2))",
            )
            .replace(/\bawait\s+/g, "");
          factory = new Function(...SHADOWED_DOM_GLOBALS, downleveled) as typeof factory;
        } else {
          throw err;
        }
      }
      const exported = factory(...SHADOWED_DOM_GLOBALS.map(() => undefined));
      if (typeof exported !== "function") {
        throw new Error(`函数文件没有导出一个函数`);
      }
      fn = exported;
      this.fnCache.set(cacheKey, fn);
    }
    return fn;
  }

  private buildStateObject(work: Map<string, Value>): Record<string, unknown> {
    const obj: Record<string, unknown> = { clock: this.clockMs };
    for (const [path, val] of work) {
      const cloned = cloneValue(val);
      obj[path] = cloned;
      const parts = path.split(".");
      let cursor = obj;
      for (let i = 0; i < parts.length - 1; i++) {
        const part = parts[i];
        if (typeof cursor[part] !== "object" || cursor[part] === null || Array.isArray(cursor[part])) {
          cursor[part] = {};
        }
        cursor = cursor[part] as Record<string, unknown>;
      }
      cursor[parts[parts.length - 1]] = cloneValue(val);
    }
    return obj;
  }

  private applyFnResult(batch: Batch, result: unknown): void {
    if (!result || typeof result !== "object" || Array.isArray(result)) return;
    const walk = (obj: Record<string, unknown>, prefix: string) => {
      for (const [k, v] of Object.entries(obj)) {
        const path = prefix ? `${prefix}.${k}` : k;
        const leaf = findStateLeaf(this.project.logic, path);
        if (leaf) {
          this.assign(batch, path, v as Value);
        } else if (v && typeof v === "object" && !Array.isArray(v)) {
          walk(v as Record<string, unknown>, path);
        } else {
          this.assign(batch, path, v as Value);
        }
      }
    };
    walk(result as Record<string, unknown>, "");
  }

  private runSteps(steps: Step[], batch: Batch, depth: number, scope: Record<string, Value>): void {
    if (!Array.isArray(steps)) return;
    for (const step of steps) {
      try {
        if ("set" in step) {
          const parsed = parseAssignment(step.set);
          const nextValue = evalExpr(parsed.value, (path) => this.resolve(path, batch.work, [], scope));
          if (parsed.index !== undefined) {
            const idx = Number(evalExpr(parsed.index, (path) => this.resolve(path, batch.work, [], scope)));
            this.assignIndexed(batch, parsed.target, idx, parsed.field, nextValue);
          } else {
            this.assign(batch, parsed.target, nextValue);
          }
        } else if ("push" in step) {
          const spec = typeof step.push === "string" ? { to: step.push, item: (step as { value?: Value }).value ?? null } : step.push;
          const itemVal = this.evaluateStructured(spec.item, batch.work, scope);
          const cur = batch.work.get(spec.to);
          const arr = Array.isArray(cur) ? cloneValue(cur) : [];
          if (spec.index !== undefined) {
            const idx = Math.max(0, Math.min(arr.length, Number(this.evaluate(spec.index, batch.work, scope))));
            arr.splice(idx, 0, itemVal);
          } else {
            arr.push(itemVal);
          }
          this.assign(batch, spec.to, arr);
        } else if ("remove" in step) {
          const spec = typeof step.remove === "string" ? { from: step.remove, index: (step as { index?: string | number }).index ?? 0 } : step.remove;
          const idx = Number(this.evaluate(spec.index, batch.work, scope));
          const cur = batch.work.get(spec.from);
          if (Array.isArray(cur)) {
            if (Number.isInteger(idx) && idx >= 0 && idx < cur.length) {
              const arr = cloneValue(cur);
              arr.splice(idx, 1);
              this.assign(batch, spec.from, arr);
            }
          } else {
            this.assign(batch, spec.from, []);
          }
        } else if ("move" in step) {
          const spec =
            typeof step.move === "string"
              ? {
                  in: step.move,
                  from: (step as { from?: string | number }).from ?? 0,
                  to: (step as { to?: string | number }).to ?? 0,
                }
              : step.move;
          const fromIdx = Number(this.evaluate(spec.from, batch.work, scope));
          const toIdx = Number(this.evaluate(spec.to, batch.work, scope));
          const cur = batch.work.get(spec.in);
          if (Array.isArray(cur)) {
            if (
              Number.isInteger(fromIdx) &&
              Number.isInteger(toIdx) &&
              fromIdx >= 0 &&
              fromIdx < cur.length &&
              toIdx >= 0 &&
              toIdx < cur.length
            ) {
              const arr = cloneValue(cur);
              const [moved] = arr.splice(fromIdx, 1);
              arr.splice(toIdx, 0, moved);
              this.assign(batch, spec.in, arr);
            }
          } else {
            this.assign(batch, spec.in, []);
          }
        } else if ("fn" in step) {
          const spec = typeof step.fn === "string" ? { name: step.fn, args: (step as { args?: Record<string, Value | string> }).args } : step.fn;
          const source = this.project.functions?.[spec.name];
          if (!source) {
            batch.lines.push(`错误：找不到函数“${spec.name}”`);
          } else {
            const evalArgs: Record<string, Value> = { ...scope };
            for (const [k, v] of Object.entries(spec.args ?? {})) {
              evalArgs[k] = this.evaluateStructured(v, batch.work, scope);
            }
            const fn = this.compileFunction(`fn:${spec.name}`, source);
            const stateObj = this.buildStateObject(batch.work);
            const result = fn(stateObj, evalArgs);
            this.applyFnResult(batch, result);
          }
        } else if ("openWindow" in step) {
          const spec = typeof step.openWindow === "string" ? { id: step.openWindow } : step.openWindow;
          const node = this.project.scene.nodes[spec.id];
          if (!node) {
            batch.lines.push(`错误：窗口“${spec.id}”不存在`);
          } else {
            const modal = spec.modal !== undefined ? Boolean(spec.modal) : Boolean(node.modal ?? node.props?.modal ?? node.type === "Dialog");
            batch.windows.set(spec.id, { open: true, modal });
            batch.lines.push(`打开窗口 ${spec.id}${modal ? "（模态）" : ""}`);
          }
        } else if ("closeWindow" in step) {
          const id = step.closeWindow;
          const prev = batch.windows.get(id);
          if (!this.project.scene.nodes[id]) {
            batch.lines.push(`错误：窗口“${id}”不存在`);
          } else {
            batch.windows.set(id, { open: false, modal: prev?.modal ?? true });
            batch.lines.push(`关闭窗口 ${id}`);
          }
        } else if ("call" in step) {
          const name = step.call;
          const def = this.project.logic.actions?.[name];
          if (!def) {
            batch.lines.push(`错误：动作“${name}”没有在 actions 中定义`);
          } else {
            const input: Record<string, Value> = {};
            for (const [k, expr] of Object.entries(def.input ?? {})) {
              input[k] = this.evaluate(expr, batch.work, scope);
            }
            batch.calls.push({ name, input });
            batch.lines.push(`调用动作 ${name}`);
          }
        } else if ("emit" in step) {
          const spec = typeof step.emit === "string" ? { name: step.emit, args: (step as { args?: Record<string, Value | string> }).args } : step.emit;
          const evalArgs: Record<string, Value> = {};
          for (const [k, v] of Object.entries(spec.args ?? {})) {
            evalArgs[k] = this.evaluateStructured(v, batch.work, scope);
          }
          const handler = globalSteps(this.project.logic, spec.name);
          const reqDef = requestEvent(this.project.logic, spec.name);
          if (businessEvent(this.project.logic, spec.name)) {
            batch.pending.push({ name: spec.name, args: evalArgs });
            batch.lines.push(`业务事件 ${spec.name}：这批步骤生效后处理`);
          } else if (reqDef) {
            if (depth >= MAX_EMIT_DEPTH) {
              batch.lines.push(`错误：全局事件嵌套超过 ${MAX_EMIT_DEPTH} 层，可能是互相触发`);
            } else {
              batch.lines.push(`触发请求事件 ${spec.name}`);
              this.runSteps(reqDef.steps, batch, depth + 1, { ...scope, param: evalArgs, ...evalArgs });
            }
          } else if (!handler) {
            batch.lines.push(`错误：全局事件“${spec.name}”没有定义`);
          } else if (batch.simulating) {
            batch.lines.push(`错误：模拟步骤里不能触发普通全局事件“${spec.name}”`);
          } else if (depth >= MAX_EMIT_DEPTH) {
            batch.lines.push(`错误：全局事件嵌套超过 ${MAX_EMIT_DEPTH} 层，可能是互相触发`);
          } else {
            batch.lines.push(`触发全局事件 ${spec.name}`);
            this.runSteps(handler, batch, depth + 1, { ...scope, param: evalArgs, ...evalArgs });
          }
        } else if ("if" in step) {
          const branch = this.evaluate(step.if, batch.work, scope) ? step.then : step.else;
          if (branch) this.runSteps(branch, batch, depth, scope);
        }
      } catch (e) {
        batch.lines.push(`错误：${(e as Error).message}`);
      }
    }
  }

  private accepts(leaf: StateLeaf, value: Value): boolean {
    if (leaf.type === "enum") return typeof value === "string" && (leaf.values ?? []).includes(value);
    if (leaf.type === "array") return Array.isArray(value);
    if (leaf.type === "object") return typeof value === "object" && value !== null && !Array.isArray(value);
    return typeof value === leaf.type;
  }

  private expected(leaf: StateLeaf): string {
    return leaf.type === "enum" ? `[${(leaf.values ?? []).join(", ")}] 之一` : leaf.type;
  }

  private assign(batch: Batch, target: string, value: Value): void {
    const leaf = findStateLeaf(this.project.logic, target);
    if (!leaf) {
      batch.lines.push(`错误：“${target}”不是状态路径`);
    } else if (isBusinessState(leaf) !== batch.simulating) {
      batch.lines.push(
        batch.simulating
          ? `错误：模拟步骤只能修改业务管的状态，“${target}”是界面自己管的`
          : `错误：状态“${target}”归业务管，只能在业务事件的模拟步骤里修改`,
      );
    } else if (!this.accepts(leaf, value)) {
      batch.lines.push(`错误：${target} 需要 ${this.expected(leaf)}，得到 ${show(value)}`);
    } else {
      batch.work.set(target, cloneValue(value));
    }
  }

  private assignIndexed(batch: Batch, target: string, idx: number, field: string | undefined, value: Value): void {
    const leaf = findStateLeaf(this.project.logic, target);
    if (!leaf) {
      batch.lines.push(`错误：“${target}”不是状态路径`);
      return;
    }
    if (leaf.type !== "array") {
      batch.lines.push(`错误：“${target}”不是数组状态，不能按位置赋值`);
      return;
    }
    if (isBusinessState(leaf) !== batch.simulating) {
      batch.lines.push(
        batch.simulating
          ? `错误：模拟步骤只能修改业务管的状态，“${target}”是界面自己管的`
          : `错误：状态“${target}”归业务管，只能在业务事件的模拟步骤里修改`,
      );
      return;
    }
    const cur = batch.work.get(target);
    if (!Array.isArray(cur) || !Number.isInteger(idx) || idx < 0 || idx >= cur.length) {
      return;
    }
    const nextArr = cloneValue(cur);
    if (field) {
      const item = nextArr[idx];
      if (item && typeof item === "object" && !Array.isArray(item)) {
        nextArr[idx] = { ...(item as Record<string, Value>), [field]: cloneValue(value) };
      }
    } else {
      nextArr[idx] = cloneValue(value);
    }
    batch.work.set(target, nextArr);
  }
}
