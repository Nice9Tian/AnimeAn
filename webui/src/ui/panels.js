// Panels of the main window, drawn to match the Qt build. Each takes plain
// data and reports what the user did; the wiring to the backend is elsewhere.

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}

// Qt sums hinted glyph advances, the browser unhinted ones, so the two disagree
// by a pixel or two per label. These are the widths the Qt build lays out at
// 9pt Segoe UI; a label outside the table falls back to measuring.
const MENU_WIDTH = { File: 26, Edit: 28, Setting: 46, View: 32, "Auto Mapping": 88, Draw: 32, Theme: 44, Windows: 62 };
const TAB_WIDTH = { Painting: 66, Mapping: 70, Drawing: 66, Texture: 60, Options: 64, "Main Layers": 85, "Child Layers": 86 };

// ---- tabs as the stylesheet in parentwindow.cpp draws them ------------------
export function tabBar(labels, onSelect) {
  const bar = div("tabbar-row");
  const measure = document.createElement("canvas").getContext("2d");
  measure.font = '12px "Segoe UI"';
  const tabs = labels.map((text, i) => {
    const t = div("tab", text);
    t.style.width = `${TAB_WIDTH[text] ?? Math.ceil(measure.measureText(text).width) + 22}px`;
    t.addEventListener("click", () => onSelect(i));
    bar.append(t);
    return t;
  });
  return { el: bar, select: (i) => tabs.forEach((t, k) => t.classList.toggle("sel", k === i)) };
}

// ---- Scrollbar --------------------------------------------------------------
export function scrollBar(vertical, onValue) {
  const s = div("sb " + (vertical ? "sb-v" : "sb-h"));
  const a = vertical ? "M3.5 9h7L7 5z" : "M9 3.5v7L5 7z";
  const b = vertical ? "M3.5 5h7L7 9z" : "M5 3.5v7L9 7z";
  s.innerHTML = `<div class="sb-btn sb-a"><svg width="14" height="14"><path d="${a}" fill="var(--sb-arrow)"/></svg></div>`
    + `<div class="sb-btn sb-b"><svg width="14" height="14"><path d="${b}" fill="var(--sb-arrow)"/></svg></div><div class="sb-handle"></div>`;
  const handle = s.querySelector(".sb-handle");
  let min = 0, max = 0, page = 1, value = 0, enabled = true;
  const BTN = 14, MIN_HANDLE = 20;
  const metrics = () => {
    const length = (vertical ? s.clientHeight : s.clientWidth) - 2 * BTN;
    const span = max - min + page;
    // QCommonStyle's integer arithmetic, so the handle lands on the same pixels.
    const size = span > 0 ? Math.min(length, Math.max(MIN_HANDLE, Math.floor((length * page) / span))) : length;
    return { length, size, free: Math.max(0, length - size) };
  };
  const draw = () => {
    const { size, free } = metrics();
    const pos = max > min ? Math.floor(((value - min) * free) / (max - min)) : 0;
    handle.style[vertical ? "top" : "left"] = `${BTN + pos}px`;
    handle.style[vertical ? "height" : "width"] = `${size}px`;
  };
  const set = (next) => {
    next = Math.max(min, Math.min(max, next));
    if (next !== value) { value = next; draw(); onValue(value); }
  };
  s.querySelector(".sb-a").addEventListener("click", () => { if (enabled) set(value - 20); });
  s.querySelector(".sb-b").addEventListener("click", () => { if (enabled) set(value + 20); });
  handle.addEventListener("pointerdown", (e) => {
    if (!enabled) return;
    handle.setPointerCapture(e.pointerId);
    const start = vertical ? e.clientY : e.clientX, from = value;
    const move = (m) => {
      const { free } = metrics();
      const delta = (vertical ? m.clientY : m.clientX) - start;
      set(from + (free > 0 ? (delta / free) * (max - min) : 0));
    };
    const up = () => { handle.removeEventListener("pointermove", move); handle.removeEventListener("pointerup", up); };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", up);
    e.stopPropagation();
  });
  new ResizeObserver(draw).observe(s);
  return {
    el: s,
    set(next = {}) {
      ({ min = 0, max = 0, page = 1, value = 0, enabled = true } = next);
      s.classList.toggle("disabled", !enabled);
      draw();
    },
    held: () => handle.matches(":active"),
    enabled: () => enabled,
  };
}

// ---- QAbstractScrollArea --------------------------------------------------------
// A framed view with the Fusion scroll bar the rest of the app uses, not the
// browser's. Native scrolling stays, so the wheel and the keyboard just work.
export function scrollArea(cls) {
  const frame = div(`qframe ${cls}`);
  const view = div("qframe-view");
  const bar = scrollBar(true, (value) => { view.scrollTop = value; });
  frame.append(view, bar.el);
  const sync = () => {
    const page = view.clientHeight, max = view.scrollHeight - page;
    const needed = max > 0;
    frame.classList.toggle("scrolls", needed);
    if (needed) bar.set({ min: 0, max, page, value: view.scrollTop });
  };
  view.addEventListener("scroll", sync);
  new ResizeObserver(sync).observe(view);
  new MutationObserver(sync).observe(view, { childList: true, subtree: true });
  return { frame, view, sync };
}

// ---- Central area: Drawing / Texture pages around the board canvases ---------
export function isUnboundedViewport(hOrViewport, vEnabled) {
  if (hOrViewport && typeof hOrViewport === "object" && ("h" in hOrViewport || "v" in hOrViewport)) {
    return hOrViewport.h?.enabled === false && hOrViewport.v?.enabled === false;
  }
  return hOrViewport === false && vEnabled === false;
}

export function createCentral(root, { onPage, onScroll, onPill, bottomChrome = div("central-bottom") }) {
  let current = 0;
  const tabs = tabBar(["Drawing", "Texture"], (i) => { show(i); onPage(i === 0 ? "main" : "child"); });
  const stage = div("central-stage");
  const host = div("board-host");
  const h = scrollBar(false, (v) => onScroll("h", v));
  const v = scrollBar(true, (val) => onScroll("v", val));
  const syncBars = () => {
    const hide = isUnboundedViewport(h.enabled(), v.enabled());
    h.el.style.display = hide ? "none" : "";
    v.el.style.display = hide ? "none" : "";
    host.style.right = hide ? "0" : "";
    host.style.bottom = hide ? "0" : "";
  };
  const setH = h.set, setV = v.set;
  h.set = (next) => { setH(next); syncBars(); };
  v.set = (next) => { setV(next); syncBars(); };
  const pill = div("pill");
  pill.innerHTML = `<svg width="12" height="12" viewBox="0 0 24 24"><path d="M6 14 L12 8 L18 14" fill="none" stroke="currentColor" stroke-width="2"/></svg><span>TIMELINE</span>`;
  pill.addEventListener("click", onPill);
  stage.append(host, h.el, v.el, pill);
  const texturePage = div("central-texture");
  const textureSlot = div("central-texture-slot");
  const texturePlaceholder = div("central-placeholder", "The texture view is open in its own control.");
  texturePage.append(textureSlot, texturePlaceholder);
  const syncTexture = () => {
    const hasContent = textureSlot.childElementCount > 0;
    textureSlot.style.display = hasContent ? "" : "none";
    texturePlaceholder.style.display = hasContent ? "none" : "";
  };
  new MutationObserver(syncTexture).observe(textureSlot, { childList: true });
  syncTexture();
  root.append(tabs.el, stage, texturePage, bottomChrome);
  function show(i) {
    current = i === 1 ? 1 : 0;
    tabs.select(current);
    stage.style.display = current === 0 ? "" : "none";
    texturePage.style.display = current === 1 ? "" : "none";
  }
  show(0);
  return {
    host,
    h,
    v,
    pill,
    bottomChrome,
    show,
    current: () => (current === 1 ? "texture" : "drawing"),
    texturePage,
    textureSlot,
    texturePlaceholder,
    setTextureContent(el) {
      if (el) textureSlot.append(el);
      else textureSlot.replaceChildren();
      syncTexture();
    },
  };
}

// ---- Layer tree ---------------------------------------------------------------
export const LAYER_DRAG_MIME = "application/x-animean-layer";
export const ASSET_DRAG_MIME = "application/x-animean-asset";

// A group row carries no layer of its own; selecting it resolves to the first
// member layer in its subtree (mainwindow.cpp firstLayer).
export function firstGroupLayer(rows, index) {
  const head = rows?.[index];
  if (!head) return -1;
  if (!head.group) return head.layer >= 0 ? head.layer : -1;
  for (let i = index + 1; i < rows.length && rows[i].depth > head.depth; i++) {
    if (!rows[i].group && rows[i].layer >= 0) return rows[i].layer;
  }
  return -1;
}

// Highlights the outermost collapsed ancestor when the current layer sits
// inside a collapsed group, or the clicked group row while its first member
// stays the current layer (mainwindow.cpp refreshLayerList).
export function selectedRowIndex(rows, selected, pickedGroupId = 0) {
  if (!rows?.length || selected === undefined || selected < 0) return -1;
  let index = rows.findIndex((r) => !r.group && r.layer === selected);
  if (index < 0) return -1;
  if (pickedGroupId > 0) {
    const groupIndex = rows.findIndex((r) => r.group && r.group_id === pickedGroupId);
    if (groupIndex >= 0 && firstGroupLayer(rows, groupIndex) === selected) index = groupIndex;
  }
  let display = index;
  for (let i = index - 1, d = rows[index].depth; i >= 0 && d > 0; i--) {
    if (rows[i].depth < d) {
      d = rows[i].depth;
      if (rows[i].group && rows[i].collapsed) display = i;
    }
  }
  return display;
}

export function hasDragType(dt, mime) {
  if (!dt) return false;
  if (dt.types && Array.from(dt.types).includes(mime)) return true;
  return typeof dt.getData === "function" && dt.getData(mime) !== "";
}

export function resolveAssetDrop(raw, view, assetTargetView) {
  if (assetTargetView && view !== assetTargetView) return -1;
  const text = String(raw ?? "").trim();
  if (!text) return -1;
  const colon = text.indexOf(":");
  if (colon >= 0) {
    if (text.slice(0, colon) !== view) return -1;
    const idx = Number(text.slice(colon + 1));
    return Number.isInteger(idx) && idx >= 0 ? idx : -1;
  }
  const idx = Number(text);
  return Number.isInteger(idx) && idx >= 0 ? idx : -1;
}

export function layerDropZone(e, el, row, fromIndex = -1, toIndex = -1) {
  if (e?.dropZone === "above" || e?.dropZone === "below" || e?.dropZone === "inside") {
    return !row?.group && e.dropZone === "inside" ? "below" : e.dropZone;
  }
  const rect = el?.getBoundingClientRect?.();
  const hasMetrics = (rect && (rect.height > 0 || rect.top !== 0)) || typeof e?.offsetY === "number";
  if (!hasMetrics) {
    if (row?.group) return "inside";
    return fromIndex >= 0 && toIndex >= 0 && fromIndex < toIndex ? "below" : "above";
  }
  const h = (rect && rect.height > 0 ? rect.height : el?.clientHeight) || 24;
  const y = typeof e?.offsetY === "number"
    ? e.offsetY
    : (typeof e?.clientY === "number" && rect ? e.clientY - rect.top : h * 0.5);
  const ratio = Math.max(0, Math.min(1, y / h));
  if (row?.group) {
    if (ratio < 0.25) return "above";
    if (ratio > 0.75) return "below";
    return "inside";
  }
  return ratio < 0.5 ? "above" : "below";
}

export function applyLayerDrop(rows, fromIndex, toIndex, zone = "below") {
  if (!Array.isArray(rows) || fromIndex < 0 || fromIndex >= rows.length) return null;
  const src = rows[fromIndex];
  if (!src || src.child) return null;

  const baseDepth = src.depth ?? 0;
  let blockEnd = fromIndex + 1;
  while (blockEnd < rows.length && (rows[blockEnd].depth ?? 0) > baseDepth) {
    blockEnd++;
  }

  let resolvedTo = toIndex;
  let resolvedZone = zone;
  if (resolvedTo >= 0 && resolvedTo < rows.length && rows[resolvedTo]?.child) {
    const childDepth = rows[resolvedTo].depth ?? 0;
    while (resolvedTo > 0 && (rows[resolvedTo].child || (rows[resolvedTo].depth ?? 0) >= childDepth)) {
      resolvedTo--;
    }
    resolvedZone = "below";
  }

  if (resolvedTo >= fromIndex && resolvedTo < blockEnd) return null;

  let insertAt = rows.length;
  let targetDepth = 0;
  if (resolvedTo < 0 || resolvedTo >= rows.length) {
    insertAt = rows.length;
    targetDepth = 0;
  } else {
    const dst = rows[resolvedTo];
    const dstDepth = dst.depth ?? 0;
    if (resolvedZone === "inside" && dst.group) {
      let groupEnd = resolvedTo + 1;
      while (groupEnd < rows.length && (rows[groupEnd].depth ?? 0) > dstDepth) {
        groupEnd++;
      }
      insertAt = groupEnd;
      targetDepth = dstDepth + 1;
    } else if (resolvedZone === "above") {
      insertAt = resolvedTo;
      targetDepth = dstDepth;
    } else {
      let afterDst = resolvedTo + 1;
      while (afterDst < rows.length && (rows[afterDst].depth ?? 0) > dstDepth) {
        afterDst++;
      }
      insertAt = afterDst;
      targetDepth = dstDepth;
    }
  }

  const deltaDepth = targetDepth - baseDepth;
  const movedBlock = rows.slice(fromIndex, blockEnd).map((r) => ({
    ...r,
    depth: Math.max(0, (r.depth ?? 0) + deltaDepth),
  }));
  const remaining = rows.slice(0, fromIndex).concat(rows.slice(blockEnd));
  const adjInsert = insertAt > fromIndex ? insertAt - (blockEnd - fromIndex) : insertAt;
  const nextRows = remaining.slice(0, adjInsert).concat(movedBlock, remaining.slice(adjInsert));

  for (let i = nextRows.length - 1; i >= 0; i--) {
    if (!nextRows[i].group) continue;
    const hasContent = i + 1 < nextRows.length && (nextRows[i + 1].depth ?? 0) > (nextRows[i].depth ?? 0);
    if (!hasContent) nextRows.splice(i, 1);
  }

  const movedLayer = src.group ? -1 : (src.layer ?? -1);
  return { rows: nextRows, movedLayer, movedGroupId: src.group ? (src.group_id ?? 0) : 0 };
}

export function createLayers(root, handlers) {
  const tabs = tabBar(["Main Layers", "Child Layers"], (i) => show(i));
  const pages = ["main", "child"].map((view) => {
    const page = div("layer-page");
    const list = scrollArea("layer-tree");
    const add = document.createElement("button");
    const del = document.createElement("button");
    const clear = document.createElement("button");
    add.textContent = "Add Layer"; del.textContent = "Remove Layer"; clear.textContent = "Clear Selection";
    for (const b of [add, del, clear]) { b.type = "button"; b.className = "qbutton"; }
    const p = { frame: null, tree: list.view, view, current: null, pickedGroup: 0 };
    add.addEventListener("click", () => handlers.onAdd(view));
    del.addEventListener("click", () => handlers.onRemove(view, p.current));
    clear.addEventListener("click", () => { p.pickedGroup = 0; p.current = null; handlers.onClear(view); });
    const row = div("layer-buttons");
    row.append(add, del);
    page.append(list.frame, row, clear);
    const area = scrollArea("layer-scroll");
    area.view.append(page);
    p.frame = area.frame;
    return p;
  });
  root.append(tabs.el, ...pages.map((p) => p.frame));
  const targetView = () => handlers.assetTarget?.();
  const acceptAsset = (e, p) => {
    if (!hasDragType(e.dataTransfer, ASSET_DRAG_MIME)) return false;
    const target = targetView();
    if (target && p.view !== target) return true;
    const raw = e.dataTransfer?.getData?.(ASSET_DRAG_MIME);
    if (raw && resolveAssetDrop(raw, p.view, target) < 0) return true;
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = "copy";
    return true;
  };
  const dropAsset = (e, p) => {
    if (!hasDragType(e.dataTransfer, ASSET_DRAG_MIME)) return false;
    e.stopPropagation();
    const raw = e.dataTransfer?.getData?.(ASSET_DRAG_MIME) ?? "";
    const assetIndex = resolveAssetDrop(raw, p.view, targetView());
    if (assetIndex < 0) return true;
    e.preventDefault();
    handlers.onDropAsset?.(p.view, assetIndex);
    return true;
  };
  for (const p of pages) {
    p.tree.addEventListener("contextmenu", (e) => {
      e.preventDefault();
      handlers.onContext(p.view, null, [], e.clientX, e.clientY);
    });
    p.tree.addEventListener("dragover", (e) => {
      if (acceptAsset(e, p)) return;
      if (hasDragType(e.dataTransfer, LAYER_DRAG_MIME)) e.preventDefault();
    });
    p.tree.addEventListener("drop", (e) => {
      if (dropAsset(e, p)) return;
      if (!hasDragType(e.dataTransfer, LAYER_DRAG_MIME)) return;
      e.preventDefault();
      const from = Number(e.dataTransfer.getData(LAYER_DRAG_MIME));
      const res = applyLayerDrop(p.rows ?? [], from, -1, "below");
      if (!res) return;
      handlers.onApplyStructure?.(p.view, res.rows, res.movedLayer);
    });
  }
  function show(i) {
    tabs.select(i);
    pages.forEach((p, k) => { p.frame.style.display = k === i ? "" : "none"; });
  }
  show(0);

  function paintRows(p, state) {
    p.tree.replaceChildren();
    const rows = state?.rows ?? [];
    p.rows = rows;
    const sel = selectedRowIndex(rows, state?.selected ?? -1, p.pickedGroup);
    p.current = sel >= 0 ? rows[sel] : null;
    p.pickedGroup = p.current?.group ? p.current.group_id : 0;
    let hiddenBelow = null;
    rows.forEach((row, index) => {
      // Rows under a collapsed group stay in the state; only the tree hides them.
      if (hiddenBelow !== null && row.depth > hiddenBelow) return;
      hiddenBelow = row.group && row.collapsed ? row.depth : null;
      const r = div("layer-row");
      r.classList.toggle("sel", index === sel);
      r.classList.toggle("child", !!row.child);
      r.style.paddingLeft = `${row.depth * 20 + 4}px`;
      if (row.group) {
        const arrow = div("layer-arrow");
        arrow.innerHTML = row.collapsed
          ? `<svg width="9" height="9"><path d="M2 0v9l5-4.5z" fill="var(--sb-arrow)"/></svg>`
          : `<svg width="9" height="9"><path d="M0 2h9l-4.5 5z" fill="var(--sb-arrow)"/></svg>`;
        arrow.addEventListener("click", (e) => { e.stopPropagation(); handlers.onCollapse(p.view, row.group_id, !row.collapsed); });
        r.append(arrow);
      } else {
        const check = document.createElement("input");
        check.type = "checkbox";
        check.className = "qcheckbox";
        check.checked = row.visible !== false;
        check.addEventListener("click", (e) => e.stopPropagation());
        check.addEventListener("change", () => handlers.onVisible(p.view, row.layer, check.checked));
        r.append(check);
      }
      const name = div("layer-name", row.name);
      r.append(name);
      r.addEventListener("click", () => {
        p.pickedGroup = row.group ? row.group_id : 0;
        p.current = row;
        Array.from(p.tree.children).forEach((el) => el.classList.toggle("sel", el === r));
        const layer = row.group ? firstGroupLayer(rows, index) : row.layer;
        if (layer >= 0) handlers.onSelect(p.view, layer);
      });
      r.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const members = [];
        if (row.group) {
          for (let i = index + 1; i < rows.length && rows[i].depth > row.depth; i++) {
            if (!rows[i].group) members.push(rows[i].layer);
          }
        }
        handlers.onContext(p.view, row, members, e.clientX, e.clientY);
      });
      r.draggable = !row.child;
      r.addEventListener("dragstart", (e) => {
        if (row.child) { e.preventDefault(); return; }
        e.dataTransfer.setData(LAYER_DRAG_MIME, String(index));
      });
      r.addEventListener("dragover", (e) => {
        if (acceptAsset(e, p)) return;
        if (hasDragType(e.dataTransfer, LAYER_DRAG_MIME)) e.preventDefault();
      });
      r.addEventListener("drop", (e) => {
        if (dropAsset(e, p)) return;
        if (!hasDragType(e.dataTransfer, LAYER_DRAG_MIME)) return;
        e.preventDefault();
        e.stopPropagation();
        const from = Number(e.dataTransfer.getData(LAYER_DRAG_MIME));
        if (!Number.isFinite(from) || from === index) return;
        const zone = layerDropZone(e, r, row, from, index);
        const res = applyLayerDrop(rows, from, index, zone);
        if (!res) return;
        if (handlers.onApplyStructure) {
          handlers.onApplyStructure(p.view, res.rows, res.movedLayer);
        } else {
          handlers.onMove?.(p.view, rows[from], row);
        }
      });
      p.tree.append(r);
    });
  }
  return { update(view, state) { const p = pages.find((x) => x.view === view); if (p) paintRows(p, state); } };
}

// ---- Menu bar and popup menus -----------------------------------------------
let openMenu = null;
function closeMenus() { if (openMenu) { openMenu.remove(); openMenu = null; } }
if (typeof window !== "undefined") window.addEventListener("pointerdown", (e) => { if (openMenu && !e.target.closest?.(".qmenu")) closeMenus(); }, true);

export function popupMenu(items, x, y, onTrigger, parent) {
  const m = div("qmenu");
  for (const item of items) {
    if (item.kind === "separator") { m.append(div("qmenu-sep")); continue; }
    const row = div("qmenu-item");
    row.classList.toggle("disabled", item.enabled === false);
    const mark = div("qmenu-mark", item.checked ? "✓" : "");
    const text = div("qmenu-text", item.text ?? "");
    const key = div("qmenu-key", item.shortcut ?? "");
    row.append(mark, text, key);
    if (item.kind === "submenu") {
      row.append(div("qmenu-arrow", "▸"));
      row.addEventListener("pointerenter", () => {
        for (const s of m.querySelectorAll(":scope > .qmenu")) s.remove();
        const r = row.getBoundingClientRect(), base = m.getBoundingClientRect();
        const sub = popupMenu(item.children ?? [], r.right - base.left - 2, r.top - base.top, onTrigger, m);
        sub.style.position = "absolute";
      });
    } else if (item.enabled !== false) {
      row.addEventListener("pointerenter", () => { for (const s of m.querySelectorAll(":scope > .qmenu")) s.remove(); });
      row.addEventListener("click", () => { closeMenus(); onTrigger(item); });
    }
    m.append(row);
  }
  m.style.left = `${x}px`; m.style.top = `${y}px`;
  (parent ?? document.body).append(m);
  if (!parent) { closeMenus(); openMenu = m; }
  return m;
}

export function createMenuBar(root, { onTrigger, onOpen }) {
  const bar = div("menubar");
  root.append(bar);
  const GAP = 6, PAD = 3;
  let menus = [];
  const measure = document.createElement("canvas").getContext("2d");
  measure.font = '12px "Segoe UI"';
  function showPopup(item, index, fallbackMenu) {
    const current = menus.find((m) => (m.id && fallbackMenu.id ? m.id === fallbackMenu.id : m.text === fallbackMenu.text)) ?? menus[index] ?? fallbackMenu;
    const r = item.getBoundingClientRect?.() ?? { left: 0, bottom: 0 };
    popupMenu(current?.children ?? [], r.left, r.bottom, onTrigger);
    bar.querySelectorAll(".item").forEach((n) => n.classList.remove("open"));
    item.classList.add("open");
  }
  function paint() {
    bar.replaceChildren();
    let x = GAP;
    menus.forEach((menu, index) => {
      const item = div("item", menu.text);
      const w = MENU_WIDTH[menu.text] ?? Math.ceil(measure.measureText(menu.text).width) + 2 * PAD;
      item.style.left = `${x}px`; item.style.width = `${w}px`;
      const open = () => {
        const res = onOpen?.(menu);
        if (res && typeof res.then === "function") {
          res.then(() => {
            const matched = Array.from(bar.querySelectorAll(".item")).find((n) => n.textContent === menu.text) ?? item;
            showPopup(matched, index, menu);
          });
          return;
        }
        showPopup(item, index, menu);
      };
      item.addEventListener("click", open);
      item.addEventListener("pointerenter", () => { if (openMenu) open(); });
      bar.append(item);
      x += w + GAP;
    });
  }
  return {
    update(next) {
      const nextMenus = next ?? [];
      // A new element under the pointer fires pointerenter, which reopens the menu,
      // which refreshes it again: the bar is rebuilt only when its titles change.
      const sameTitles = nextMenus.length === menus.length && nextMenus.every((m, i) => m.text === menus[i].text);
      menus = nextMenus;
      if (!sameTitles) paint();
    },
  };
}
