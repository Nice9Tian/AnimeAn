// The tool options panel: builds the grid Python describes, the same control
// set as childrenpanel/tooloptpanel.cpp. onOption receives
// { hook, name, type, value, row, startColumn, endColumn }.
import { createPalette, parseColor, toHexRgb } from "./palette.js";

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}

function slider(control, send) {
  const wrap = div("opt-slider");
  const title = control.title ?? control.name;
  const min = control.min ?? 0, max = control.max ?? 100;
  let value = Number(control.value ?? 0);
  const label = div("opt-slider-label");
  const track = div("qslider");
  track.innerHTML = `<div class="groove"></div><div class="sub"></div><div class="handle"></div>`;
  const [, sub, handle] = track.children;
  wrap.append(label, track);
  const HANDLE = 16;
  const draw = () => {
    label.textContent = `${title}: ${value}`;
    const span = Math.max(1, track.clientWidth - HANDLE);
    const pos = Math.round(((value - min) / Math.max(1, max - min)) * span);
    handle.style.left = `${pos}px`;
    sub.style.width = `${pos + HANDLE / 2}px`;
  };
  const fromX = (x) => {
    const span = Math.max(1, track.clientWidth - HANDLE);
    const t = Math.max(0, Math.min(1, (x - HANDLE / 2) / span));
    return Math.round(min + t * (max - min));
  };
  const set = (next) => { if (next !== value) { value = next; draw(); send("slider", value); } };
  track.addEventListener("pointerdown", (e) => {
    track.setPointerCapture(e.pointerId);
    track.classList.add("pressed");
    set(fromX(e.offsetX));
  });
  track.addEventListener("pointermove", (e) => {
    if (track.hasPointerCapture(e.pointerId)) set(fromX(e.clientX - track.getBoundingClientRect().left));
  });
  track.addEventListener("pointerup", () => track.classList.remove("pressed"));
  new ResizeObserver(draw).observe(track);
  queueMicrotask(draw);
  return wrap;
}

function list(control, send) {
  const wrap = div("opt-list");
  wrap.append(div("opt-title", control.title ?? ""));
  const box = div("qlist");
  const options = control.options ?? [];
  box.style.maxHeight = `${Math.max(62, options.length * 28 + 8)}px`;
  let current = String(control.value ?? "");
  if (!options.some((o) => String(o.value) === current) && options.length) current = String(options[0].value);
  const items = options.map((o) => {
    const row = div("qlist-item", o.title);
    row.addEventListener("click", () => {
      if (String(o.value) === current) return;
      current = String(o.value);
      paint();
      send(control.type ?? "list", o.value);
    });
    box.append(row);
    return [row, String(o.value)];
  });
  const paint = () => items.forEach(([row, v]) => row.classList.toggle("sel", v === current));
  paint();
  wrap.append(box);
  return wrap;
}

function check(control, send) {
  const label = document.createElement("label");
  label.className = "qcheck";
  const input = document.createElement("input");
  input.type = "checkbox";
  const v = String(control.value ?? "").toLowerCase();
  input.checked = v === "on" || v === "true" || control.value === true;
  input.addEventListener("change", () => send("check", input.checked ? "on" : "off"));
  label.append(input, document.createTextNode(" " + (control.title ?? "")));
  return label;
}

function button(control, send) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "qbutton opt-button";
  b.textContent = control.title ?? "";
  const tint = control.state?.color;
  if (tint) b.style.background = String(tint);
  b.addEventListener("click", () => send("button", control.value ?? ""));
  return b;
}

function color(control, send) {
  const wrap = div("opt-color");
  if (control.title) wrap.append(div("opt-title-inline", control.title));
  const c = parseColor(control.value);
  const input = document.createElement("input");
  input.type = "color";
  input.className = "opt-swatch";
  input.value = toHexRgb(c);
  input.addEventListener("change", () => {
    const alpha = c.a.toString(16).padStart(2, "0");
    send("color", `#${alpha}${input.value.slice(1)}`);
  });
  wrap.append(input);
  return wrap;
}

export function createToolOptions(root, { onOption, onPalette, onNewColor, onSubWindow }) {
  const grid = div("opt-grid");
  root.append(grid);
  let visibility = [];
  const values = new Map();

  function applyVisibility() {
    for (const { el, watch, allowed } of visibility) {
      el.style.display = allowed.includes(String(values.get(watch) ?? "")) ? "" : "none";
    }
  }

  function build(layout) {
    grid.replaceChildren();
    visibility = [];
    values.clear();
    const controls = layout?.controls ?? [];
    grid.style.rowGap = `${layout?.row_spacing ?? 8}px`;
    grid.style.columnGap = `${layout?.column_spacing ?? 6}px`;
    const next = new Map();
    let rows = 0, columns = 1;
    const placed = [];
    for (const control of controls) {
      const row = control.row ?? 0;
      const start = control.start_column ?? control.column ?? (next.get(row) ?? 0);
      const end = control.end_column ?? start;
      next.set(row, end + 1);
      const send = (type, value) => {
        if (control.name) { values.set(control.name, String(value)); applyVisibility(); }
        onOption({ hook: control.hook ?? control.name, name: control.name, type, value, row, startColumn: start, endColumn: end });
      };
      let el = null;
      switch (control.type) {
        case "button": el = button(control, send); break;
        case "list": el = list(control, send); break;
        case "slider": el = slider(control, send); break;
        case "check": case "checkbox": el = check(control, send); break;
        case "color": el = color(control, send); break;
        case "palette": {
          const palette = createPalette((kind, value) => {
            if (kind === "color") send("color", value);
            else if (kind === "box") onPalette(value, control);
            else if (kind === "newColor") onNewColor(palette);
          });
          el = palette.el;
          queueMicrotask(() => palette.update(control));
          break;
        }
        case "subwindow": el = onSubWindow(control); break;
      }
      if (!el) continue;
      if (control.name) values.set(control.name, String(control.value ?? ""));
      el.style.gridRow = String(row + 1);
      el.style.gridColumn = `${start + 1} / ${end + 2}`;
      grid.append(el);
      rows = Math.max(rows, row + 1);
      columns = Math.max(columns, end + 1);
      placed.push(el);
      const when = control.visible_when;
      if (when?.name) {
        const allowed = (when.values ?? []).map(String);
        if (when.value !== undefined) allowed.push(String(when.value));
        visibility.push({ el, watch: when.name, allowed });
      }
    }
    grid.style.gridTemplateColumns = `repeat(${columns}, minmax(0, 1fr))`;
    applyVisibility();
  }

  return { build };
}
