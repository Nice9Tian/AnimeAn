// The tool options panel: builds the grid Python describes, the same control
// set as childrenpanel/tooloptpanel.cpp. onOption receives
// { hook, name, type, value, row, startColumn, endColumn }. A colour swatch
// opens no dialog of its own: onDialog asks Python for one (dialogs.py), and the
// colour comes back as that control's option.
import { label as labelled } from "../i18n/index.js";
import { colorButtonColors, colorDarker, colorLighter, colorLightness, createPalette, parseColor, toHexArgb, toHexRgb } from "./palette.js";
export { colorButtonColors, colorButtonColors as colorButtonStyle, colorDarker, colorDarker as darker, colorLighter, colorLighter as lighter, colorLightness, colorLightness as lightness };

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
  const reading = document.createTextNode("");
  label.append(labelled(document.createElement("span"), title), reading);
  const track = div("qslider");
  track.innerHTML = `<div class="groove"></div><div class="sub"></div><div class="handle"></div>`;
  const [, sub, handle] = track.children;
  wrap.append(label, track);
  const HANDLE = 16;
  const draw = () => {
    reading.nodeValue = `: ${value}`;
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
  const trackX = (e) => e.clientX - track.getBoundingClientRect().left;
  // How far off its centre the handle was pressed: pressing it must not move it.
  let grab = 0;
  track.addEventListener("pointerdown", (e) => {
    track.setPointerCapture(e.pointerId);
    track.classList.add("pressed");
    grab = e.target === handle ? trackX(e) - (handle.offsetLeft + HANDLE / 2) : 0;
    set(fromX(trackX(e) - grab));
  });
  track.addEventListener("pointermove", (e) => {
    if (track.hasPointerCapture(e.pointerId)) set(fromX(trackX(e) - grab));
  });
  track.addEventListener("lostpointercapture", () => track.classList.remove("pressed"));
  new ResizeObserver(draw).observe(track);
  queueMicrotask(draw);
  return wrap;
}

function list(control, send) {
  const wrap = div("opt-list");
  wrap.append(labelled(div("opt-title"), control.title ?? ""));
  const box = div("qlist");
  const options = control.options ?? [];
  box.style.maxHeight = `${Math.max(62, options.length * 28 + 8)}px`;
  let current = String(control.value ?? "");
  if (!options.some((o) => String(o.value) === current) && options.length) current = String(options[0].value);
  const items = options.map((o) => {
    const row = labelled(div("qlist-item"), o.title);
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
  label.append(input, document.createTextNode(" "), labelled(document.createElement("span"), control.title ?? ""));
  return label;
}

function buttonTint(state) {
  if (!state || typeof state !== "object") return null;
  if (state.color) return parseColor(state.color);
  if (state.r !== undefined || state.g !== undefined || state.b !== undefined) {
    return { r: Number(state.r ?? 0), g: Number(state.g ?? 0), b: Number(state.b ?? 0), a: Number(state.a ?? 255) };
  }
  return null;
}

function button(control, send) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "qbutton opt-button";
  labelled(b, control.title ?? "");
  const tint = buttonTint(control.state);
  if (tint) {
    const styles = colorButtonColors(tint);
    let hovered = false, pressed = false;
    const syncBg = () => {
      const bg = pressed ? styles.pressedBackground : hovered ? styles.hoverBackground : styles.background;
      b.style.background = bg;
      b.style.backgroundColor = bg;
    };
    b.style.color = styles.color;
    b.dataset.hoverBg = styles.hoverBackground;
    b.dataset.pressedBg = styles.pressedBackground;
    syncBg();
    b.addEventListener("pointerenter", () => { hovered = true; syncBg(); });
    b.addEventListener("pointerleave", () => { hovered = false; pressed = false; syncBg(); });
    b.addEventListener("pointerdown", (e) => { if (!e.button) { pressed = true; syncBg(); } });
    b.addEventListener("pointerup", () => { pressed = false; syncBg(); });
    b.addEventListener("pointercancel", () => { pressed = false; syncBg(); });
  }
  b.addEventListener("click", () => send("button", control.value ?? ""));
  return b;
}

function color(control, ask) {
  const wrap = div("opt-color");
  if (control.title) wrap.append(labelled(div("opt-title-inline"), control.title));
  const current = parseColor(control.value);
  const swatch = document.createElement("button");
  swatch.type = "button";
  swatch.className = "opt-swatch";
  const rgb = toHexRgb(current);
  swatch.style.background = rgb;
  swatch.style.backgroundColor = rgb;
  swatch.title = toHexArgb(current);
  swatch.addEventListener("click", () => ask(toHexArgb(current)));
  wrap.append(swatch);
  return wrap;
}

/**
 * @param {any} root
 * @param {{ onOption: Function, onDialog?: Function, onSubWindow: Function, onBeforeBuild?: Function, onAfterBuild?: Function }} handlers
 */
export function createToolOptions(root, { onOption, onDialog, onSubWindow, onBeforeBuild = undefined, onAfterBuild = undefined }) {
  const grid = div("opt-grid");
  root.append(grid);
  let visibility = [];
  let subWindows = [];
  let lastLayout = null;
  const values = new Map();

  function applyVisibility() {
    for (const { el, watch, allowed } of visibility) {
      el.style.display = allowed.includes(String(values.get(watch) ?? "")) ? "" : "none";
    }
  }

  function build(layout) {
    lastLayout = layout;
    onBeforeBuild?.(layout);
    grid.replaceChildren();
    visibility = [];
    subWindows = [];
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
      const askColor = (value) => onDialog?.({
        kind: "color", hook: control.hook ?? control.name, name: control.name, title: control.title, value,
        row, startColumn: start, endColumn: end,
      });
      const box = (value) => onOption({ hook: "palette_box", name: control.name, type: "palette", value, row, startColumn: start, endColumn: end });
      let el = null;
      switch (control.type) {
        case "button": el = button(control, send); break;
        case "list": el = list(control, send); break;
        case "slider": el = slider(control, send); break;
        case "check": case "checkbox": el = check(control, send); break;
        case "color": el = color(control, askColor); break;
        case "palette": {
          // The + asks palette_box for New Color, starting on the colour shown.
          const palette = createPalette((kind, value) => {
            if (kind === "color") send("palette", value);
            else if (kind === "box") box(value);
            else if (kind === "newColor") box(`new:${toHexArgb(value)}`);
          });
          el = palette.el;
          queueMicrotask(() => palette.update(control));
          break;
        }
        case "subwindow": el = onSubWindow(control); break;
      }
      if (!el) continue;
      if (control.type === "subwindow") subWindows.push({ control, el });
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
    // The embedded board takes what height the panel has left (ToolOptPanel's greedy row).
    const boardRow = controls.some((c) => c.type === "subwindow" && (c.row ?? 0) === rows - 1);
    grid.classList.toggle("has-subwindow", boardRow);
    grid.style.gridTemplateRows = boardRow ? `repeat(${rows - 1}, auto) minmax(min-content, 1fr)` : "";
    applyVisibility();
    onAfterBuild?.(layout);
  }

  return {
    build,
    // The board came or went: only its cell changes, the other controls keep what
    // the user set on them since the layout was built.
    refreshSubWindows() {
      for (const entry of subWindows) {
        const next = onSubWindow(entry.control);
        if (!next) continue;
        next.style.gridRow = entry.el.style.gridRow;
        next.style.gridColumn = entry.el.style.gridColumn;
        for (const rule of visibility) if (rule.el === entry.el) rule.el = next;
        entry.el.replaceWith(next);
        entry.el = next;
      }
      applyVisibility();
    },
    hasSubWindow(name) {
      return (lastLayout?.controls ?? []).some(
        (c) => c.type === "subwindow" && (!name || !c.name || c.name === name),
      );
    },
  };
}
