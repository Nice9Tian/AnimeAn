// The panels that start hidden, drawn to match the Qt build: History, Assets, the
// Repulsion Pad, Python Debug, the New Project dialog and the texture board's
// frame. Each takes plain data and reports what the user did.
import { tabBar, scrollArea, scrollBar, createMenuBar } from "./panels.js";
import { onThemeChange, cosmeticStroke, flattenCubic, hintedTracking, trackedDiv } from "./timeline.js";

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}
function pushButton(text, onClick) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "qbutton";
  b.textContent = text;
  if (onClick) b.addEventListener("click", onClick);
  return b;
}
const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// Hinted tab widths from the Qt build, same idea as TAB_WIDTH in panels.js.
const TAB_WIDTH = { History: 60, Assets: 55, "Repulsion Pad": 97, "Python Debug": 98 };
function singleTab(root, title) {
  const tabs = tabBar([title], () => {});
  tabs.select(0);
  tabs.el.firstChild.style.width = `${TAB_WIDTH[title]}px`;
  root.append(tabs.el);
}

// ---- QListWidget ----------------------------------------------------------------
function listWidget(onRow) {
  const area = scrollArea("qlistwidget");
  area.view.tabIndex = 0;
  let current = -1, count = 0;
  area.view.addEventListener("keydown", (e) => {
    const step = e.key === "ArrowDown" ? 1 : e.key === "ArrowUp" ? -1 : 0;
    if (!step || !count) return;
    e.preventDefault();
    const next = Math.max(0, Math.min(count - 1, (current < 0 ? -step : current) + step));
    if (next !== current) onRow(next);
  });
  return {
    el: area.frame,
    // rows: [{ text, dim }], selected: a row or -1.
    paint(rows, selected, decorate) {
      current = selected;
      count = rows.length;
      area.view.replaceChildren(...rows.map((row, i) => {
        const item = trackedDiv("qitem", row.text);
        item.classList.toggle("sel", i === selected);
        item.classList.toggle("dim", !!row.dim);
        item.addEventListener("click", () => { if (i !== current) onRow(i); });
        decorate?.(item, i);
        return item;
      }));
      area.sync();
      const shown = area.view.children[selected];
      shown?.scrollIntoView({ block: "nearest" });
    },
  };
}

// ---- History --------------------------------------------------------------------
// update({ labels: string[], current }). Rows are numbered as Qt numbers them; the
// redo tail, the rows past current, reads as disabled text. onSelect(row).
export function createHistory(root, { onSelect } = {}) {
  singleTab(root, "History");
  const page = div("history-page");
  const list = listWidget((row) => onSelect?.(row));
  page.append(list.el);
  root.append(page);
  return {
    update({ labels = [], current = -1 } = {}) {
      list.paint(labels.map((label, i) => ({ text: `${i + 1}. ${label}`, dim: i > current })), current);
    },
  };
}

// ---- Assets ---------------------------------------------------------------------
// update({ names: string[], selected }). Rows drag out (DragOnly, copy action) with
// the row in text/plain. onSelect(row), onAdd(), onClear().
export function createAssets(root, { onSelect, onAdd, onClear } = {}) {
  singleTab(root, "Assets");
  const page = div("assets-page");
  const list = listWidget((row) => onSelect?.(row));
  const row = div("assets-buttons");
  row.append(pushButton("Add Asset", () => onAdd?.()), pushButton("Clear Selection", () => onClear?.()));
  page.append(list.el, row);
  root.append(page);
  const dragOut = (item, i) => {
    item.draggable = true;
    item.addEventListener("dragstart", (e) => {
      e.dataTransfer.effectAllowed = "copy";
      e.dataTransfer.setData("application/x-animean-asset", String(i));
    });
  };
  return {
    update({ names = [], selected = -1 } = {}) {
      list.paint(names.map((text) => ({ text })), selected, dragOut);
    },
  };
}

// ---- Repulsion Pad --------------------------------------------------------------
const kHandleRadius = 9, kMargin = 14, kTextBand = 20, kMoveThrottleMs = 16;
const kAccent = "rgb(61,142,201)";

// update({ value: { x, y }, springBack }): a programmatic set clamps into the disc
// and raises nothing. onPress / onMove / onRelease receive (x, y) in the unit disc.
export function createForcePad(root, { onPress, onMove, onRelease } = {}) {
  singleTab(root, "Repulsion Pad");
  const page = div("pad-page");
  const canvas = document.createElement("canvas");
  page.append(canvas);
  root.append(page);
  const ctx = canvas.getContext("2d", { alpha: false });
  let width = 0, height = 0, value = { x: 0, y: 0 }, dragging = false, springBack = false, lastMove = 0;

  const padRect = () => {
    const side = Math.min(width - 2 * kMargin, height - 2 * kMargin - kTextBand);
    return { left: (width - side) / 2, top: (height - kTextBand - side) / 2, side };
  };
  const fromWidget = (px, py) => {
    const r = padRect(), half = r.side / 2;
    if (half <= 0) return { x: 0, y: 0 };
    let nx = (px - (r.left + half)) / half, ny = (py - (r.top + half)) / half;
    const length = Math.hypot(nx, ny);
    if (length > 1) { nx /= length; ny /= length; }
    return { x: nx, y: ny };
  };

  function paint() {
    if (!width || !height) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = cssVar("--window");
    ctx.fillRect(0, 0, width, height);
    const r = padRect(), half = r.side / 2, cx = r.left + half, cy = r.top + half;
    const mid = cssVar("--divider"), text = cssVar("--text");

    // A 1px antialiased pen takes QPainter's cosmetic route at 1:1; past that it is
    // an ordinary stroke.
    const thin = (subpaths, color) => {
      if (dpr !== 1) {
        ctx.strokeStyle = color; ctx.lineWidth = 1;
        ctx.beginPath();
        for (const points of subpaths) points.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
        ctx.stroke();
        return;
      }
      ctx.save();
      ctx.fillStyle = color;
      cosmeticStroke(subpaths, 256, true, (x, y, coverage) => {
        ctx.globalAlpha = coverage / 255;
        ctx.fillRect(x, y, 1, 1);
      });
      ctx.restore();
    };
    const circle = (x, y, radius) => {
      // qt_curves_for_arc: four cubics clockwise from 3 o'clock.
      const k = radius * 0.5522847498;
      const points = [[x + radius, y]];
      flattenCubic([x + radius, y], [x + radius, y + k], [x + k, y + radius], [x, y + radius], points);
      flattenCubic([x, y + radius], [x - k, y + radius], [x - radius, y + k], [x - radius, y], points);
      flattenCubic([x - radius, y], [x - radius, y - k], [x - k, y - radius], [x, y - radius], points);
      flattenCubic([x, y - radius], [x + k, y - radius], [x + radius, y - k], [x + radius, y], points);
      return points;
    };

    ctx.fillStyle = cssVar("--surface");
    ctx.beginPath(); ctx.arc(cx, cy, half, 0, Math.PI * 2); ctx.fill();
    thin([circle(cx, cy, half)], mid);
    const ticks = [[[r.left, cy], [r.left + r.side, cy]], [[cx, r.top], [cx, r.top + r.side]]];
    for (const step of [-1, 1]) {
      const offset = (step * r.side) / 4;
      ticks.push([[cx + offset, cy - 3], [cx + offset, cy + 3]], [[cx - 3, cy + offset], [cx + 3, cy + offset]]);
    }
    thin(ticks, mid);

    const hx = cx + value.x * half, hy = cy + value.y * half;
    if (value.x !== 0 || value.y !== 0) {
      ctx.strokeStyle = kAccent; ctx.lineWidth = 1.5; ctx.lineCap = "square";
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(hx, hy); ctx.stroke();
    }
    ctx.fillStyle = dragging ? kAccent : cssVar("--pad-handle");
    ctx.beginPath(); ctx.arc(hx, hy, kHandleRadius, 0, Math.PI * 2); ctx.fill();
    thin([circle(hx, hy, kHandleRadius)], text);

    ctx.fillStyle = text;
    ctx.font = '12px "Segoe UI"';
    ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
    const readout = `x ${value.x.toFixed(2)}   y ${value.y.toFixed(2)}`;
    ctx.letterSpacing = hintedTracking(readout);
    ctx.fillText(readout, width / 2 - 1, r.top + r.side + 2 + 13);
  }

  const position = (e) => { const b = canvas.getBoundingClientRect(); return fromWidget(e.clientX - b.left, e.clientY - b.top); };
  canvas.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    // The second press of a double click is one hesitant click, not a new gesture.
    if (e.detail > 1) return;
    canvas.setPointerCapture(e.pointerId);
    dragging = true;
    lastMove = performance.now();
    value = position(e);
    paint();
    onPress?.(value.x, value.y);
  });
  canvas.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    value = position(e);
    paint();
    // Throttle the report, not the picture.
    const now = performance.now();
    if (now - lastMove >= kMoveThrottleMs) { lastMove = now; onMove?.(value.x, value.y); }
  });
  canvas.addEventListener("pointerup", (e) => {
    if (e.button !== 0 || !dragging) return;
    dragging = false;
    const released = position(e);
    value = released;
    onRelease?.(released.x, released.y);
    if (springBack) value = { x: 0, y: 0 };
    paint();
  });

  function measure() {
    const dpr = window.devicePixelRatio || 1;
    const wasShown = width > 0;
    width = page.clientWidth; height = page.clientHeight;
    canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
    canvas.style.width = `${width}px`; canvas.style.height = `${height}px`;
    // A panel hidden mid-drag would strand the listener in an open session; a
    // centred release is the explicit cancel.
    if (wasShown && !width && dragging) { dragging = false; value = { x: 0, y: 0 }; onRelease?.(0, 0); }
  }
  new ResizeObserver(() => { measure(); paint(); }).observe(page);
  onThemeChange(paint);

  return {
    update(data = {}) {
      measure();
      if (data.springBack !== undefined) springBack = !!data.springBack;
      if (data.value) {
        let { x, y } = data.value;
        const length = Math.hypot(x, y);
        if (length > 1) { x /= length; y /= length; }
        value = { x, y };
        paint();
      }
    },
  };
}

// ---- Python Debug ---------------------------------------------------------------
// update({ output, command }): output is the whole text. append(text) adds lines and
// scrolls to the end. onRun(text) on Run or Return.
export function createPythonDebug(root, { onRun } = {}) {
  singleTab(root, "Python Debug");
  const page = div("debug-page");
  const area = scrollArea("qplaintext");
  const text = div("qplaintext-doc");
  let lines = [];
  const placeholder = div("qplaintext-hint", "Python debug output");
  area.view.append(text, placeholder);
  const row = div("debug-row");
  const field = document.createElement("input");
  field.className = "qlineedit";
  field.spellcheck = false;
  const setCommand = (value) => { field.value = value; field.style.letterSpacing = hintedTracking(value); };
  setCommand("bind_test.py");
  field.addEventListener("input", () => { field.style.letterSpacing = hintedTracking(field.value); });
  const run = () => onRun?.(field.value);
  field.addEventListener("keydown", (e) => { if (e.key === "Enter") run(); });
  row.append(trackedDiv("debug-label", "Command"), field, pushButton("Run", run));
  page.append(area.frame, row);
  root.append(page);
  // One element per block, as a QPlainTextEdit has, so each carries its own tracking.
  const block = (line) => {
    const d = trackedDiv("qplaintext-line", line);
    if (!line) d.textContent = "\u00a0";
    return d;
  };
  const refresh = () => { placeholder.style.display = lines.length ? "none" : ""; area.sync(); };
  const toEnd = () => { area.view.scrollTop = area.view.scrollHeight; };
  refresh();
  return {
    update({ output, command } = {}) {
      if (output !== undefined) {
        lines = output ? output.split("\n") : [];
        text.replaceChildren(...lines.map(block));
        refresh(); toEnd();
      }
      if (command !== undefined) setCommand(command);
    },
    append(line) {
      for (const part of String(line).split("\n")) { lines.push(part); text.append(block(part)); }
      refresh(); toEnd();
    },
  };
}

// ---- New Project dialog ---------------------------------------------------------
const PRESETS = [
  ["720p  -  1280 x 720", 1280, 720],
  ["1080p  -  1920 x 1080", 1920, 1080],
  ["4K UHD  -  3840 x 2160", 3840, 2160],
  ["Square  -  1080 x 1080", 1080, 1080],
  ["Vertical  -  1080 x 1920", 1080, 1920],
  ["A4 at 150dpi  -  1240 x 1754", 1240, 1754],
];
const SPIN_ARROW_UP = `<svg width="9" height="5"><path d="M4.5 0 L9 5 L0 5 z" fill="currentColor"/></svg>`;
const SPIN_ARROW_DOWN = `<svg width="9" height="5"><path d="M0 0 L9 0 L4.5 5 z" fill="currentColor"/></svg>`;
const COMBO_ARROW = `<svg width="9" height="5"><path d="M0 0 L9 0 L4.5 5 z" fill="currentColor"/></svg>`;

function spinBox(min, max, suffix, onChange) {
  const box = div("qspin");
  const input = document.createElement("input");
  input.className = "qspin-edit";
  input.spellcheck = false;
  const up = div("qspin-up"), down = div("qspin-down");
  up.innerHTML = SPIN_ARROW_UP; down.innerHTML = SPIN_ARROW_DOWN;
  box.append(input, div("qspin-column"), up, down);
  let value = min;
  const show = () => { input.value = `${value}${suffix}`; input.style.letterSpacing = hintedTracking(input.value); };
  const set = (next) => {
    next = Math.max(min, Math.min(max, next));
    if (next !== value) { value = next; onChange(); }
    show();
  };
  const digits = () => String(value).length;
  input.addEventListener("input", () => {
    const typed = parseInt(input.value.replace(/\D/g, ""), 10);
    if (typed >= min && typed <= max && typed !== value) { value = typed; onChange(); }
  });
  input.addEventListener("blur", show);
  input.addEventListener("keydown", (e) => {
    const step = e.key === "ArrowUp" ? 1 : e.key === "ArrowDown" ? -1 : 0;
    if (step) { e.preventDefault(); set(value + step); input.setSelectionRange(0, digits()); }
  });
  input.addEventListener("wheel", (e) => { e.preventDefault(); set(value + (e.deltaY < 0 ? 1 : -1)); }, { passive: false });
  // QAbstractSpinBox auto-repeat: a held arrow steps after a pause, then fast.
  const repeat = (button, step) => {
    let timer = 0;
    const stop = () => { clearTimeout(timer); timer = 0; box.classList.remove("armed"); };
    const tick = () => { set(value + step); timer = setTimeout(tick, 60); };
    button.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      input.focus();
      button.setPointerCapture(e.pointerId);
      set(value + step);
      timer = setTimeout(tick, 500);
    });
    button.addEventListener("pointerup", stop);
    button.addEventListener("pointercancel", stop);
  };
  repeat(up, 1); repeat(down, -1);
  return {
    el: box,
    input,
    value: () => value,
    setValue(next) { value = Math.max(min, Math.min(max, next)); show(); },
    selectDigits() { input.focus({ preventScroll: true }); input.setSelectionRange(0, digits()); },
  };
}

function comboBox(items, onPick) {
  const box = div("qcombo");
  const text = div("qcombo-text");
  const arrow = div("qcombo-arrow");
  arrow.innerHTML = COMBO_ARROW;
  box.append(text, arrow);
  let current = 0, popup = null;
  const close = () => {
    if (!popup) return;
    popup.remove();
    popup = null;
    window.removeEventListener("pointerdown", outside, true);
  };
  const outside = (e) => { if (popup && !popup.contains(e.target) && !box.contains(e.target)) close(); };
  box.addEventListener("click", () => {
    if (popup) { close(); return; }
    const r = box.getBoundingClientRect();
    popup = div("qcombo-popup");
    items.forEach((label, i) => {
      const row = trackedDiv("qcombo-item", label);
      row.classList.toggle("sel", i === current);
      row.addEventListener("pointerenter", () => {
        for (const other of popup.children) other.classList.toggle("sel", other === row);
      });
      row.addEventListener("click", () => { close(); if (i !== current) { setIndex(i); onPick(i); } });
      popup.append(row);
    });
    document.body.append(popup);
    popup.style.left = `${r.left}px`;
    // The current row lands on the box, as with a menu-style QComboBox popup.
    popup.style.top = `${r.top - 24 * current}px`;
    window.addEventListener("pointerdown", outside, true);
  });
  function setIndex(i) { current = i; text.textContent = items[i]; text.style.letterSpacing = hintedTracking(items[i]); }
  return { el: box, close, setIndex };
}

// update({ width, height, open }). The frame is drawn here, not by the OS. onCreate
// receives { width, height }; onCancel(). Enter and Escape work as in a QDialog.
export function createNewProjectDialog(root, { onCreate, onCancel } = {}) {
  const shade = div("qdialog-shade");
  const dialog = div("qdialog");
  const title = div("qdialog-title");
  title.append(div("qdialog-caption", "New Project"));
  const close = div("qdialog-close");
  close.innerHTML = `<svg width="10" height="10"><path d="M0.5 0.5 L9.5 9.5 M9.5 0.5 L0.5 9.5" stroke="currentColor" fill="none"/></svg>`;
  title.append(close);
  const body = div("qdialog-body");
  dialog.append(title, body);
  shade.append(dialog);
  root.append(shade);

  let syncing = false;
  const applyPreset = (index) => {
    if (syncing || index >= PRESETS.length) return;
    syncing = true;
    width.setValue(PRESETS[index][1]);
    height.setValue(PRESETS[index][2]);
    syncing = false;
  };
  const syncPreset = () => {
    if (syncing) return;
    syncing = true;
    const match = PRESETS.findIndex((p) => p[1] === width.value() && p[2] === height.value());
    preset.setIndex(match < 0 ? PRESETS.length : match);
    syncing = false;
  };

  const preset = comboBox([...PRESETS.map((p) => p[0]), "Custom"], applyPreset);
  const width = spinBox(16, 16384, " px", syncPreset);
  const height = spinBox(16, 16384, " px", syncPreset);
  const place = (el, x, y, w) => { el.style.left = `${x}px`; el.style.top = `${y}px`; if (w) el.style.width = `${w}px`; body.append(el); };
  const label = (text, y, h) => {
    const l = trackedDiv("qdialog-label", text);
    // A QFormLayout label sits a pixel lower beside the taller spin box.
    l.style.height = `${h}px`; l.style.lineHeight = `${h === 27 ? 29 : h}px`;
    place(l, 11, y);
  };

  place(trackedDiv("qdialog-caption-text", "Canvas size for the new document, in pixels."), 11, 11, 212);
  label("Preset", 49, 24); place(preset.el, 53, 49, 170);
  label("Width", 79, 27); place(width.el, 53, 79, 170);
  label("Height", 112, 27); place(height.el, 53, 112, 170);
  const create = pushButton("Create", () => accept());
  create.classList.add("qdefault");
  const cancel = pushButton("Cancel", () => reject());
  place(create, 57, 145, 80);
  place(cancel, 143, 145, 80);

  const hide = () => { preset.close(); shade.style.display = "none"; };
  const accept = () => { hide(); onCreate?.({ width: width.value(), height: height.value() }); };
  const reject = () => { hide(); onCancel?.(); };
  close.addEventListener("click", reject);
  dialog.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); accept(); }
    if (e.key === "Escape") { e.preventDefault(); reject(); }
  });
  // The frame is ours, so moving it is too.
  title.addEventListener("pointerdown", (e) => {
    if (e.target.closest(".qdialog-close")) return;
    title.setPointerCapture(e.pointerId);
    const r = dialog.getBoundingClientRect(), host = shade.getBoundingClientRect();
    const dx = e.clientX - r.left, dy = e.clientY - r.top;
    const move = (m) => { dialog.style.left = `${m.clientX - dx - host.left}px`; dialog.style.top = `${m.clientY - dy - host.top}px`; dialog.style.transform = "none"; };
    const up = () => { title.removeEventListener("pointermove", move); title.removeEventListener("pointerup", up); };
    title.addEventListener("pointermove", move);
    title.addEventListener("pointerup", up);
  });

  shade.style.display = "none";
  return {
    update({ width: w = 1280, height: h = 720, open = true } = {}) {
      width.setValue(w > 0 ? w : 1280);
      height.setValue(h > 0 ? h : 720);
      syncPreset();
      shade.style.display = open ? "" : "none";
      if (open) width.selectDigits();
    },
  };
}

// ---- SubControlFrame -------------------------------------------------------------
const kTitlePadding = 6, kAffordance = 14;

function affordanceSvg(floating) {
  const box = { x: 1.5, y: 4.5, w: 8, h: 8 };
  const cx = box.x + box.w / 2, cy = box.y + box.h / 2;
  const corner = [kAffordance - 1 - 1, 1];
  const [fx, fy] = floating ? corner : [cx, cy];
  const [tx, ty] = floating ? [cx, cy] : corner;
  const dx = tx - fx, dy = ty - fy, length = Math.hypot(dx, dy);
  const ux = dx / length, uy = dy / length, nx = -uy, ny = ux;
  const a = [tx - ux * 3.5 + nx * 2.2, ty - uy * 3.5 + ny * 2.2], b = [tx - ux * 3.5 - nx * 2.2, ty - uy * 3.5 - ny * 2.2];
  const d = `M${box.x} ${box.y}h${box.w}v${box.h}h${-box.w}zM${fx} ${fy}L${tx} ${ty}M${tx} ${ty}L${a[0]} ${a[1]}M${tx} ${ty}L${b[0]} ${b[1]}`;
  return `<svg width="${kAffordance}" height="${kAffordance}" xmlns="http://www.w3.org/2000/svg"><path d="${d}" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
}

const GRIP_DOTS = [[14, 5], [11, 8], [14, 8], [8, 11], [11, 11], [14, 11], [5, 14], [8, 14], [11, 14], [14, 14]];

// The named frame a tool panel can adopt or float. update({ title, floating,
// placeholder }); put content in .body, or leave it empty to show the placeholder.
// preview(rect|null) draws the drop wash in client pixels. onAffordance() on the
// corner glyph; onDragStart/onDragMove/onDragEnd(x, y) past a 4px threshold on the bar.
export function createSubControlFrame(root, { onAffordance, onDragStart, onDragMove, onDragEnd } = {}) {
  root.classList.add("subframe");
  const bar = div("subframe-title");
  const text = div("subframe-text");
  const glyph = div("subframe-glyph");
  bar.append(text, glyph);
  const body = div("subframe-body");
  const placeholder = div("subframe-placeholder");
  const grip = div("subframe-grip");
  grip.innerHTML = `<svg width="17" height="17">${GRIP_DOTS.map(([x, y]) => `<rect x="${x}" y="${y}" width="1" height="1" fill="var(--grip-dark)"/><rect x="${x + 1}" y="${y + 1}" width="1" height="1" fill="var(--grip-light)"/>`).join("")}</svg>`;
  root.append(bar, body, placeholder, grip);

  let floating = false, pressed = false, dragging = false, pressX = 0, pressY = 0;
  glyph.addEventListener("click", () => onAffordance?.());
  bar.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || glyph.contains(e.target)) return;
    bar.setPointerCapture(e.pointerId);
    pressed = true; dragging = false; pressX = e.clientX; pressY = e.clientY;
  });
  bar.addEventListener("pointermove", (e) => {
    if (!pressed) return;
    if (!dragging) {
      if (Math.abs(e.clientX - pressX) + Math.abs(e.clientY - pressY) < 4) return;
      dragging = true;
      onDragStart?.(pressX, pressY);
    }
    onDragMove?.(e.clientX, e.clientY);
  });
  bar.addEventListener("pointerup", (e) => {
    if (dragging) onDragEnd?.(e.clientX, e.clientY);
    pressed = false; dragging = false;
  });

  let wash = null;
  const refresh = () => { placeholder.style.display = body.childElementCount ? "none" : ""; };
  new MutationObserver(refresh).observe(body, { childList: true });
  refresh();

  return {
    body,
    update({ title = "", floating: nextFloating = false, placeholder: note = "" } = {}) {
      floating = nextFloating;
      text.textContent = title;
      placeholder.textContent = note;
      placeholder.style.letterSpacing = hintedTracking(note);
      glyph.innerHTML = affordanceSvg(floating);
      bar.title = floating ? "Drag onto a panel to dock this control" : "Drag out to float this control";
      grip.style.display = floating ? "" : "none";
      root.classList.toggle("floating", floating);
    },
    preview(rect) {
      if (!rect) { wash?.remove(); wash = null; return; }
      if (!wash) { wash = div("subframe-wash"); document.body.append(wash); }
      Object.assign(wash.style, { left: `${rect.x}px`, top: `${rect.y}px`, width: `${rect.width}px`, height: `${rect.height}px` });
    },
  };
}

// ---- TexturePanel ----------------------------------------------------------------
const SETTING_MENU = {
  text: "Setting",
  children: [
    { kind: "action", text: "Changable Timeline", checked: false, id: "changable_timeline" },
    { kind: "action", text: "Changable Texture", checked: true, id: "changable_texture" },
    { kind: "separator" },
    { kind: "submenu", text: "Background", children: [
      { kind: "action", text: "White", checked: true, id: "background:white" },
      { kind: "action", text: "Black", checked: false, id: "background:black" },
      { kind: "action", text: "Transparent", checked: false, id: "background:transparent" },
    ] },
  ],
};

// The texture board and what configures it, as one reparentable body: menu bar,
// optional script-button row, the board's host with its scroll bars.
// update({ menus, buttons: [{ name, title, checkable, checked }] }): menus replaces
// the bar and defaults to Setting alone, the shell adds File and the script's View.
// onTrigger(item), onScroll(axis, value), onButton(name, on). Returns { host, h, v }
// so the owner can drop its board canvas in and drive the scroll bars.
export function createTexturePanel(root, { onTrigger, onScroll, onButton } = {}) {
  root.classList.add("texture-panel");
  const menuHost = div("texture-menu");
  const menu = createMenuBar(menuHost, { onTrigger: (item) => onTrigger?.(item) });
  const options = div("texture-options");
  const stage = div("texture-stage");
  const host = div("board-host");
  const h = scrollBar(false, (v) => onScroll?.("h", v));
  const v = scrollBar(true, (value) => onScroll?.("v", value));
  stage.append(host, h.el, v.el);
  root.append(menuHost, options, stage);
  return {
    host, h, v,
    update({ menus, buttons = [] } = {}) {
      menu.update(menus ?? [SETTING_MENU]);
      options.replaceChildren(...buttons.map((button) => {
        const b = pushButton(button.title ?? button.name);
        let on = !!button.checked;
        b.classList.toggle("checked", on);
        b.addEventListener("click", () => {
          if (button.checkable) { on = !on; b.classList.toggle("checked", on); }
          onButton?.(button.name, button.checkable ? on : true);
        });
        return b;
      }));
      options.style.display = buttons.length ? "" : "none";
    },
  };
}

// ---- QMessageBox -----------------------------------------------------------------
export function messageBox({ title = "", text = "", buttons = ["OK"] } = {}) {
  const list = Array.isArray(buttons) && buttons.length ? buttons.map(String) : ["OK"];
  if (typeof document === "undefined" || !document.body) return Promise.resolve(null);
  return new Promise((resolve) => {
    const shade = div("qdialog-shade");
    const dialog = div("qdialog");
    Object.assign(dialog.style, { width: "auto", minWidth: "260px", maxWidth: "420px" });
    const head = div("qdialog-title");
    head.append(div("qdialog-caption", String(title ?? "")));
    const close = div("qdialog-close");
    close.innerHTML = `<svg width="10" height="10"><path d="M0.5 0.5 L9.5 9.5 M9.5 0.5 L0.5 9.5" stroke="currentColor" fill="none"/></svg>`;
    head.append(close);
    const body = div("qdialog-body");
    Object.assign(body.style, {
      width: "auto",
      height: "auto",
      padding: "12px",
      display: "flex",
      flexDirection: "column",
      gap: "12px",
      boxSizing: "border-box",
    });
    const msg = div("qdialog-caption-text", String(text ?? ""));
    Object.assign(msg.style, { position: "static", whiteSpace: "pre-wrap", overflowWrap: "anywhere" });
    const row = div("qdialog-buttons");
    Object.assign(row.style, { position: "static", display: "flex", justifyContent: "flex-end", gap: "6px" });

    let done = false;
    const finish = (result) => {
      if (done) return;
      done = true;
      if (typeof window !== "undefined") window.removeEventListener("keydown", onKey, true);
      shade.remove();
      resolve(result);
    };
    const onKey = (e) => {
      if (e.key === "Enter") { e.preventDefault(); finish(list[0]); }
      else if (e.key === "Escape") { e.preventDefault(); finish(null); }
    };

    const btnEls = list.map((label, i) => {
      const b = pushButton(label, () => finish(label));
      b.style.minWidth = "72px";
      if (i === 0) b.classList.add("qdefault");
      row.append(b);
      return b;
    });

    close.addEventListener("click", () => finish(null));
    dialog.tabIndex = -1;
    dialog.addEventListener("keydown", onKey);
    if (typeof window !== "undefined") window.addEventListener("keydown", onKey, true);
    head.addEventListener("pointerdown", (e) => {
      if (e.target?.closest?.(".qdialog-close")) return;
      head.setPointerCapture?.(e.pointerId);
      const r = dialog.getBoundingClientRect(), host = shade.getBoundingClientRect();
      const dx = e.clientX - r.left, dy = e.clientY - r.top;
      const move = (m) => {
        dialog.style.left = `${m.clientX - dx - host.left}px`;
        dialog.style.top = `${m.clientY - dy - host.top}px`;
        dialog.style.transform = "none";
      };
      const up = () => { head.removeEventListener("pointermove", move); head.removeEventListener("pointerup", up); };
      head.addEventListener("pointermove", move);
      head.addEventListener("pointerup", up);
    });

    body.append(msg, row);
    dialog.append(head, body);
    shade.append(dialog);
    document.body.append(shade);
    (btnEls[0] ?? dialog).focus?.({ preventScroll: true });
  });
}
