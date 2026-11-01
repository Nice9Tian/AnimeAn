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
const MENU_WIDTH = { File: 26, Edit: 28, View: 32, "Auto Mapping": 88, Draw: 32, Theme: 44, Windows: 62 };
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
  let min = 0, max = 0, page = 1, value = 0;
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
  s.querySelector(".sb-a").addEventListener("click", () => set(value - 20));
  s.querySelector(".sb-b").addEventListener("click", () => set(value + 20));
  handle.addEventListener("pointerdown", (e) => {
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
  return { el: s, set(next) { ({ min, max, page, value } = next); draw(); } };
}

// ---- Central area: Drawing / Texture pages around the board canvases ---------
export function createCentral(root, { onPage, onScroll, onPill }) {
  const tabs = tabBar(["Drawing", "Texture"], (i) => { show(i); onPage(i === 0 ? "main" : "child"); });
  const stage = div("central-stage");
  const host = div("board-host");
  const h = scrollBar(false, (v) => onScroll("h", v));
  const v = scrollBar(true, (val) => onScroll("v", val));
  const pill = div("pill");
  pill.innerHTML = `<svg width="12" height="12" viewBox="0 0 24 24"><path d="M6 14 L12 8 L18 14" fill="none" stroke="currentColor" stroke-width="2"/></svg><span>TIMELINE</span>`;
  pill.addEventListener("click", onPill);
  stage.append(host, h.el, v.el, pill);
  root.append(tabs.el, stage);
  function show(i) { tabs.select(i); }
  show(0);
  return { host, h, v, pill, show };
}

// ---- Layer tree ---------------------------------------------------------------
export function createLayers(root, handlers) {
  const tabs = tabBar(["Main Layers", "Child Layers"], (i) => show(i));
  const pages = ["main", "child"].map((view) => {
    const page = div("layer-page");
    const tree = div("layer-tree");
    const add = document.createElement("button");
    const del = document.createElement("button");
    const clear = document.createElement("button");
    add.textContent = "Add Layer"; del.textContent = "Remove Layer"; clear.textContent = "Clear Selection";
    for (const b of [add, del, clear]) { b.type = "button"; b.className = "qbutton"; }
    add.addEventListener("click", () => handlers.onAdd(view));
    del.addEventListener("click", () => handlers.onRemove(view));
    clear.addEventListener("click", () => handlers.onClear(view));
    const row = div("layer-buttons");
    row.append(add, del);
    page.append(tree, row, clear);
    return { page, tree, view };
  });
  root.append(tabs.el, ...pages.map((p) => p.page));
  for (const p of pages) {
    p.tree.addEventListener("contextmenu", (e) => {
      e.preventDefault();
      handlers.onContext(p.view, null, [], e.clientX, e.clientY);
    });
  }
  function show(i) {
    tabs.select(i);
    pages.forEach((p, k) => { p.page.style.display = k === i ? "" : "none"; });
  }
  show(0);

  function paintRows(p, state) {
    p.tree.replaceChildren();
    (state?.rows ?? []).forEach((row, index) => {
      const r = div("layer-row");
      r.classList.toggle("sel", row.group ? false : row.layer === state.selected);
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
      const name = div("layer-name", (row.child ? "↳ " : "") + row.name);
      r.append(name);
      if (!row.group) r.addEventListener("click", () => handlers.onSelect(p.view, row.layer));
      r.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const members = [];
        if (row.group) {
          for (let i = index + 1; i < state.rows.length && state.rows[i].depth > row.depth; i++) {
            if (!state.rows[i].group) members.push(state.rows[i].layer);
          }
        }
        handlers.onContext(p.view, row, members, e.clientX, e.clientY);
      });
      r.draggable = true;
      r.addEventListener("dragstart", (e) => { e.dataTransfer.setData("text/plain", String(index)); });
      r.addEventListener("dragover", (e) => e.preventDefault());
      r.addEventListener("drop", (e) => {
        e.preventDefault();
        const from = Number(e.dataTransfer.getData("text/plain"));
        if (Number.isFinite(from) && from !== index) handlers.onMove(p.view, state.rows[from], row);
      });
      p.tree.append(r);
    });
  }
  return { update(view, state) { const p = pages.find((x) => x.view === view); if (p) paintRows(p, state); } };
}

// ---- Menu bar and popup menus -----------------------------------------------
let openMenu = null;
function closeMenus() { if (openMenu) { openMenu.remove(); openMenu = null; } }
window.addEventListener("pointerdown", (e) => { if (openMenu && !e.target.closest?.(".qmenu")) closeMenus(); }, true);

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

export function createMenuBar(root, { onTrigger }) {
  const bar = div("menubar");
  root.append(bar);
  const GAP = 6, PAD = 3;
  let menus = [];
  const measure = document.createElement("canvas").getContext("2d");
  measure.font = '12px "Segoe UI"';
  function paint() {
    bar.replaceChildren();
    let x = GAP;
    menus.forEach((menu) => {
      const item = div("item", menu.text);
      const w = MENU_WIDTH[menu.text] ?? Math.ceil(measure.measureText(menu.text).width) + 2 * PAD;
      item.style.left = `${x}px`; item.style.width = `${w}px`;
      const open = () => {
        const r = item.getBoundingClientRect();
        popupMenu(menu.children ?? [], r.left, r.bottom, onTrigger);
        bar.querySelectorAll(".item").forEach((n) => n.classList.remove("open"));
        item.classList.add("open");
      };
      item.addEventListener("click", open);
      item.addEventListener("pointerenter", () => { if (openMenu) open(); });
      bar.append(item);
      x += w + GAP;
    });
  }
  return { update(next) { menus = next ?? []; paint(); } };
}
