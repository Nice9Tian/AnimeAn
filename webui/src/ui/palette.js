// The palette block of the tool options panel: current colour row, page tabs,
// swatch box and the three pickers. Same geometry and maths as
// childrenpanel/palettecontrol.cpp, drawn with DOM for the box and canvas for
// the pickers. Colours travel as #aarrggbb, the form Python stores.
import { label as labelled } from "../i18n/index.js";
import { PRESS_ENDS } from "./panels.js";

const css = (name) => (typeof getComputedStyle === "function" ? getComputedStyle(document.documentElement).getPropertyValue(name).trim() : "");
const STRIP = 15, GAP = 9, RING = 14, READOUT_W = 44, READOUT_H = 24;

export function parseColor(text) {
  const t = String(text ?? "").replace("#", "");
  if (t.length === 8) {
    return { a: parseInt(t.slice(0, 2), 16), r: parseInt(t.slice(2, 4), 16), g: parseInt(t.slice(4, 6), 16), b: parseInt(t.slice(6, 8), 16) };
  }
  if (t.length === 6) {
    return { a: 255, r: parseInt(t.slice(0, 2), 16), g: parseInt(t.slice(2, 4), 16), b: parseInt(t.slice(4, 6), 16) };
  }
  if (t.length === 3) {
    const r = parseInt(t[0], 16), g = parseInt(t[1], 16), b = parseInt(t[2], 16);
    return { a: 255, r: (r << 4) | r, g: (g << 4) | g, b: (b << 4) | b };
  }
  return { a: 255, r: 0, g: 0, b: 0 };
}
const hex2 = (n) => n.toString(16).padStart(2, "0");
export const toHexRgb = (c) => `#${hex2(c.r)}${hex2(c.g)}${hex2(c.b)}`;
export const toHexArgb = (c) => `#${hex2(c.a)}${hex2(c.r)}${hex2(c.g)}${hex2(c.b)}`;

function toColorObj(c) {
  if (typeof c === "string") return parseColor(c);
  return {
    r: Math.max(0, Math.min(255, Math.round(Number(c?.r ?? 0)))),
    g: Math.max(0, Math.min(255, Math.round(Number(c?.g ?? 0)))),
    b: Math.max(0, Math.min(255, Math.round(Number(c?.b ?? 0)))),
    a: Math.max(0, Math.min(255, Math.round(Number(c?.a ?? 255)))),
  };
}

export function colorLightness(c) {
  const { r, g, b } = toColorObj(c);
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  return Math.floor(((max + min) * 257 + 256) / 514);
}

function toHsv16({ r, g, b }) {
  const rf = (r * 257) / 65535, gf = (g * 257) / 65535, bf = (b * 257) / 65535;
  const max = Math.max(rf, gf, bf), min = Math.min(rf, gf, bf), d = max - min;
  const v = Math.round(max * 65535);
  if (d === 0 || max === 0) return { h: -1, s: 0, v };
  const s = Math.round((d / max) * 65535);
  let h = 0;
  if (rf === max) h = (gf - bf) / d;
  else if (gf === max) h = 2 + (bf - rf) / d;
  else h = 4 + (rf - gf) / d;
  h /= 6;
  if (h < 0) h += 1;
  const h16 = Math.round(h * 36000);
  return { h: h16 === 36000 ? 0 : h16, s, v };
}

function fromHsv16(h, s, v, a = 255) {
  const vf = v / 65535, sf = s / 65535;
  let rf = vf, gf = vf, bf = vf;
  if (s > 0 && h >= 0) {
    const sector = (h === 36000 ? 0 : h) / 6000;
    const i = Math.floor(sector);
    const f = sector - i;
    const p = vf * (1 - sf);
    if (i & 1) {
      const q = vf * (1 - sf * f);
      if (i === 1) { rf = q; gf = vf; bf = p; }
      else if (i === 3) { rf = p; gf = q; bf = vf; }
      else if (i === 5) { rf = vf; gf = p; bf = q; }
    } else {
      const t = vf * (1 - sf * (1 - f));
      if (i === 0) { rf = vf; gf = t; bf = p; }
      else if (i === 2) { rf = p; gf = vf; bf = t; }
      else if (i === 4) { rf = t; gf = p; bf = vf; }
    }
  }
  const div257 = (u16) => Math.floor((Math.round(u16) + 128) / 257);
  return {
    r: div257(rf * 65535),
    g: div257(gf * 65535),
    b: div257(bf * 65535),
    a,
  };
}

export function colorLighter(c, factor = 150) {
  const col = toColorObj(c);
  if (factor <= 0) return col;
  if (factor < 100) return colorDarker(col, Math.round(10000 / factor));
  let { h, s, v } = toHsv16(col);
  v = Math.floor((v * factor) / 100);
  if (v > 65535) {
    s = Math.max(0, s - (v - 65535));
    v = 65535;
  }
  return fromHsv16(h, s, v, col.a);
}

export function colorDarker(c, factor = 200) {
  const col = toColorObj(c);
  if (factor <= 0) return col;
  if (factor < 100) return colorLighter(col, Math.round(10000 / factor));
  const { h, s, v } = toHsv16(col);
  return fromHsv16(h, s, Math.round((v * 100) / factor), col.a);
}

export function colorButtonColors(c) {
  const col = toColorObj(c);
  const dark = colorLightness(col) < 128;
  const hover = dark ? colorLighter(col, 135) : colorDarker(col, 108);
  const pressed = dark ? colorLighter(col, 165) : colorDarker(col, 125);
  return {
    background: toHexRgb(col),
    color: dark ? "white" : "black",
    hoverBackground: toHexRgb(hover),
    pressedBackground: toHexRgb(pressed),
  };
}
export { colorLightness as lightness, colorLighter as lighter, colorDarker as darker, colorButtonColors as colorButtonStyle };

function rgbToHsv({ r, g, b }) {
  const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
  let h = -1;
  if (d > 0) {
    if (max === r) h = ((g - b) / d + 6) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h = Math.round(h * 60) % 360;
  }
  return { h, s: max === 0 ? 0 : d / max, v: max / 255 };
}
function hsvToRgb(h, s, v) {
  const c = v * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = v - c;
  const [r, g, b] = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];
  return { r: Math.round((r + m) * 255), g: Math.round((g + m) * 255), b: Math.round((b + m) * 255) };
}

class Hsva {
  constructor() { this.h = 0; this.s = 0; this.v = 0; this.a = 255; }
  // A grey has no hue and a black no saturation: the last meaningful value stands.
  setColor(c) {
    const t = rgbToHsv(c);
    if (t.h >= 0) this.h = t.h;
    if (t.v > 0) this.s = t.s;
    this.v = t.v;
    this.a = c.a;
  }
  color() { return { ...hsvToRgb(Math.max(0, Math.min(359, this.h)), this.s, this.v), a: this.a }; }
}

function checker(ctx, x, y, w, h, cell) {
  ctx.save();
  ctx.beginPath(); ctx.rect(x, y, w, h); ctx.clip();
  ctx.fillStyle = "#fff"; ctx.fillRect(x, y, w, h);
  ctx.fillStyle = "#cbcbcb";
  for (let j = 0; j * cell < h + cell; j++) for (let i = 0; i * cell < w + cell; i++) {
    if ((i + j) % 2) ctx.fillRect(x + i * cell, y + j * cell, cell, cell);
  }
  ctx.restore();
}
function frame(ctx, x, y, w, h) {
  ctx.strokeStyle = css("--divider"); ctx.lineWidth = 1;
  ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
}
function svSquare(ctx, x, y, w, h, hue) {
  const { r, g, b } = hsvToRgb(hue, 1, 1);
  ctx.fillStyle = `rgb(${r},${g},${b})`; ctx.fillRect(x, y, w, h);
  let grad = ctx.createLinearGradient(x, y, x + w, y);
  grad.addColorStop(0, "rgba(255,255,255,1)"); grad.addColorStop(1, "rgba(255,255,255,0)");
  ctx.fillStyle = grad; ctx.fillRect(x, y, w, h);
  grad = ctx.createLinearGradient(x, y, x, y + h);
  grad.addColorStop(0, "rgba(0,0,0,0)"); grad.addColorStop(1, "rgba(0,0,0,1)");
  ctx.fillStyle = grad; ctx.fillRect(x, y, w, h);
  frame(ctx, x, y, w, h);
}
function svMarker(ctx, cx, cy) {
  ctx.lineWidth = 1; ctx.strokeStyle = "rgba(0,0,0,0.63)";
  ctx.beginPath(); ctx.arc(cx, cy, 7, 0, Math.PI * 2); ctx.stroke();
  ctx.lineWidth = 1.5; ctx.strokeStyle = "#fff";
  ctx.beginPath(); ctx.arc(cx, cy, 5.5, 0, Math.PI * 2); ctx.stroke();
}
function trackMarker(ctx, g, pos) {
  const x = g.x + Math.max(0, Math.min(1, pos)) * g.w;
  ctx.fillStyle = css("--surface"); ctx.fillRect(x - 2.5, g.y - 4, 5, g.h + 8);
  ctx.fillStyle = css("--text"); ctx.fillRect(x - 1.5, g.y - 3, 3, g.h + 6);
}
function hueTrack(ctx, g) {
  const grad = ctx.createLinearGradient(g.x, 0, g.x + g.w, 0);
  for (let i = 0; i <= 6; i++) { const c = hsvToRgb((i * 60) % 360, 1, 1); grad.addColorStop(i / 6, `rgb(${c.r},${c.g},${c.b})`); }
  ctx.fillStyle = grad; ctx.fillRect(g.x, g.y, g.w, g.h);
  frame(ctx, g.x, g.y, g.w, g.h);
}
function alphaTrack(ctx, g, c) {
  checker(ctx, g.x, g.y, g.w, g.h, 8);
  const grad = ctx.createLinearGradient(g.x, 0, g.x + g.w, 0);
  grad.addColorStop(0, `rgba(${c.r},${c.g},${c.b},0)`); grad.addColorStop(1, `rgba(${c.r},${c.g},${c.b},1)`);
  ctx.fillStyle = grad; ctx.fillRect(g.x, g.y, g.w, g.h);
  frame(ctx, g.x, g.y, g.w, g.h);
}
const inRect = (r, x, y) => x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
const clamp01 = (t) => Math.max(0, Math.min(1, t));

class Picker {
  constructor(height, cursor = "crosshair") {
    this.hsva = new Hsva();
    this.canvas = document.createElement("canvas");
    this.canvas.className = "pal-picker";
    this.canvas.style.cursor = cursor;
    this.height = height;
    this.zone = -1;
    this.onPreview = () => {};
    this.onCommit = () => {};
    this.canvas.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      if (!this.apply(e.offsetX, e.offsetY, true)) return;
      this.canvas.setPointerCapture(e.pointerId);
      this.dragging = true;
      this.paint();
      this.onPreview(this.hsva.color());
    });
    this.canvas.addEventListener("pointermove", (e) => {
      if (this.dragging && this.apply(e.offsetX, e.offsetY, false)) { this.paint(); this.onPreview(this.hsva.color()); }
    });
    for (const type of PRESS_ENDS) {
      this.canvas.addEventListener(type, () => {
        if (!this.dragging) return;
        this.dragging = false; this.zone = -1;
        // One message per drag: a continuous emit would arm a tool on every move.
        this.onCommit(this.hsva.color());
      });
    }
  }
  setColor(c) { this.hsva.setColor(c); if (this.canvas.isConnected) this.paint(); }
  layout(width) {
    this.width = width;
    const dpr = (typeof window !== "undefined" && window.devicePixelRatio) || 1;
    this.canvas.width = width * dpr; this.canvas.height = this.height * dpr;
    this.canvas.style.width = `${width}px`; this.canvas.style.height = `${this.height}px`;
    this.ctx = this.canvas.getContext("2d");
    this.ctx?.setTransform?.(dpr, 0, 0, dpr, 0, 0);
    this.paint();
  }
  paint() { if (!this.ctx?.clearRect) return; this.ctx.clearRect(0, 0, this.width, this.height); this.draw(this.ctx); }
}

class CirclePicker extends Picker {
  constructor() { super(208); }
  geometry() {
    const side = Math.min(this.width, this.height) - 2;
    const cx = this.width / 2, cy = this.height / 2, outer = side / 2, inner = outer - RING;
    const s = Math.round(Math.max(24, (outer - RING - 4) * Math.SQRT2 - 6));
    return { cx, cy, outer, inner, square: { x: Math.round(cx - s / 2), y: Math.round(cy - s / 2), w: s, h: s } };
  }
  draw(ctx) {
    const { cx, cy, outer, inner, square } = this.geometry();
    const wheel = ctx.createConicGradient(-Math.PI / 2, cx, cy);
    for (let i = 0; i <= 36; i++) { const c = hsvToRgb((i * 10) % 360, 1, 1); wheel.addColorStop(i / 36, `rgb(${c.r},${c.g},${c.b})`); }
    ctx.fillStyle = wheel;
    ctx.beginPath(); ctx.arc(cx, cy, outer, 0, Math.PI * 2); ctx.arc(cx, cy, inner, 0, Math.PI * 2, true); ctx.fill();
    ctx.strokeStyle = css("--divider"); ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(cx, cy, outer - 0.5, 0, Math.PI * 2); ctx.stroke();
    ctx.beginPath(); ctx.arc(cx, cy, inner + 0.5, 0, Math.PI * 2); ctx.stroke();
    const rad = (this.hsva.h * Math.PI) / 180, mid = (outer + inner) / 2;
    const mx = cx + Math.sin(rad) * mid, my = cy - Math.cos(rad) * mid;
    const c = hsvToRgb(this.hsva.h, 1, 1);
    ctx.fillStyle = `rgb(${c.r},${c.g},${c.b})`; ctx.strokeStyle = css("--surface"); ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(mx, my, 5, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    ctx.strokeStyle = css("--text"); ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(mx, my, 6, 0, Math.PI * 2); ctx.stroke();
    svSquare(ctx, square.x, square.y, square.w, square.h, this.hsva.h);
    svMarker(ctx, square.x + this.hsva.s * square.w, square.y + (1 - this.hsva.v) * square.h);
  }
  apply(x, y, begin) {
    const { cx, cy, outer, inner, square } = this.geometry();
    if (begin) {
      const d = Math.hypot(x - cx, y - cy);
      if (inRect(square, x, y)) this.zone = 1;
      else if (d >= inner - 4 && d <= outer + 4) this.zone = 0;
      else { this.zone = -1; return false; }
    }
    if (this.zone === 0) {
      // Hue 0 sits at 12 o'clock and grows clockwise.
      const deg = (Math.atan2(x - cx, -(y - cy)) * 180) / Math.PI;
      this.hsva.h = Math.floor((deg + 360) % 360) % 360;
      return true;
    }
    if (this.zone === 1) {
      this.hsva.s = clamp01((x - square.x) / square.w);
      this.hsva.v = clamp01(1 - (y - square.y) / square.h);
      return true;
    }
    return false;
  }
}

class HsvPicker extends Picker {
  constructor() { super(150 + 2 * (GAP + STRIP)); }
  zones() {
    const sh = Math.max(60, this.height - 2 * STRIP - 2 * GAP);
    return { square: { x: 0, y: 0, w: this.width, h: sh }, hue: { x: 0, y: sh + GAP, w: this.width, h: STRIP }, alpha: { x: 0, y: sh + 2 * GAP + STRIP, w: this.width, h: STRIP } };
  }
  draw(ctx) {
    const { square, hue, alpha } = this.zones();
    svSquare(ctx, square.x, square.y, square.w, square.h, this.hsva.h);
    svMarker(ctx, square.x + this.hsva.s * square.w, square.y + (1 - this.hsva.v) * square.h);
    hueTrack(ctx, hue); trackMarker(ctx, hue, this.hsva.h / 360);
    alphaTrack(ctx, alpha, this.hsva.color()); trackMarker(ctx, alpha, this.hsva.a / 255);
  }
  apply(x, y, begin) {
    const { square, hue, alpha } = this.zones();
    const grown = (r) => ({ x: r.x, y: r.y - 4, w: r.w, h: r.h + 8 });
    if (begin) {
      if (inRect(square, x, y)) this.zone = 0;
      else if (inRect(grown(hue), x, y)) this.zone = 1;
      else if (inRect(grown(alpha), x, y)) this.zone = 2;
      else { this.zone = -1; return false; }
    }
    if (this.zone === 0) { this.hsva.s = clamp01(x / square.w); this.hsva.v = clamp01(1 - y / square.h); return true; }
    if (this.zone === 1) { this.hsva.h = Math.max(0, Math.min(359, Math.round((359 * x) / hue.w))); return true; }
    if (this.zone === 2) { this.hsva.a = Math.max(0, Math.min(255, Math.round((255 * x) / alpha.w))); return true; }
    return false;
  }
}

class RgbPicker extends Picker {
  constructor() { super(4 * READOUT_H + 3 * GAP); }
  row(i) { return { x: 0, y: i * (READOUT_H + GAP), w: this.width, h: READOUT_H }; }
  groove(i) {
    const r = this.row(i), left = 9 + GAP, right = r.w - 1 - READOUT_W - GAP;
    return { x: left, y: r.y + Math.floor((r.h - 1) / 2) - Math.floor(STRIP / 2), w: Math.max(10, right - left), h: STRIP };
  }
  draw(ctx) {
    const c = this.hsva.color();
    const values = [c.r, c.g, c.b, this.hsva.a];
    ["R", "G", "B", "A"].forEach((label, i) => {
      const r = this.row(i), g = this.groove(i);
      ctx.font = 'bold 11px "Segoe UI"'; ctx.fillStyle = css("--text-dim"); ctx.textBaseline = "middle"; ctx.textAlign = "left";
      ctx.fillText(label, 0, r.y + r.h / 2);
      if (i === 3) alphaTrack(ctx, g, c);
      else {
        const from = { ...c }, to = { ...c };
        from["rgb"[i]] = 0; to["rgb"[i]] = 255;
        const grad = ctx.createLinearGradient(g.x, 0, g.x + g.w, 0);
        grad.addColorStop(0, `rgb(${from.r},${from.g},${from.b})`); grad.addColorStop(1, `rgb(${to.r},${to.g},${to.b})`);
        ctx.fillStyle = grad; ctx.fillRect(g.x, g.y, g.w, g.h);
        frame(ctx, g.x, g.y, g.w, g.h);
      }
      trackMarker(ctx, g, values[i] / 255);
      const box = { x: r.w - READOUT_W, y: r.y, w: READOUT_W, h: READOUT_H };
      ctx.fillStyle = css("--surface-alt"); ctx.fillRect(box.x, box.y, box.w, box.h);
      frame(ctx, box.x, box.y, box.w, box.h);
      ctx.font = 'bold 11px "Courier New"'; ctx.fillStyle = css("--text"); ctx.textAlign = "center";
      ctx.fillText(String(values[i]), box.x + box.w / 2, box.y + box.h / 2);
    });
  }
  apply(x, y, begin) {
    if (begin) {
      this.zone = -1;
      for (let i = 0; i < 4; i++) if (inRect(this.row(i), x, y)) { this.zone = i; break; }
      if (this.zone < 0) return false;
    }
    if (this.zone < 0) return false;
    const g = this.groove(this.zone);
    const value = Math.max(0, Math.min(255, Math.round((255 * (x - g.x)) / g.w)));
    if (this.zone === 3) { this.hsva.a = value; return true; }
    const c = this.hsva.color();
    c["rgb"[this.zone]] = value;
    this.hsva.setColor(c);
    return true;
  }
}

function currentRow(root) {
  root.innerHTML = `<div class="pal-chip"></div><div class="pal-hex"></div><div class="pal-rgba"></div>`;
  const [chip, hex, rgba] = root.children;
  return (c) => {
    chip.style.background = c.a < 255
      ? `linear-gradient(${toRgba(c)},${toRgba(c)}),repeating-conic-gradient(#cbcbcb 0 25%,#fff 0 50%) 0 0/16px 16px`
      : toHexRgb(c);
    hex.textContent = toHexRgb(c).toUpperCase();
    rgba.textContent = `R ${c.r}   G ${c.g}   B ${c.b}   A ${c.a}`;
  };
}
const toRgba = (c) => `rgba(${c.r},${c.g},${c.b},${c.a / 255})`;

// Returns { el, update(control), layout(width) }. emit(kind, value):
// "color" with #aarrggbb when a colour is committed, "box" with add:/remove:.
export function createPalette(emit, { pickersOnly = false } = {}) {
  const el = document.createElement("div");
  el.className = "pal";
  const row = document.createElement("div");
  row.className = "pal-row";
  const setRow = currentRow(row);
  const tabs = document.createElement("div");
  tabs.className = "pal-tabs";
  const box = document.createElement("div");
  box.className = "pal-boxes";
  const pickers = [new CirclePicker(), new HsvPicker(), new RgbPicker()];
  el.append(row, tabs, box, ...pickers.map((p) => p.canvas));

  let color = { r: 0, g: 0, b: 0, a: 255 };
  let swatches = [];
  let page = pickersOnly ? 1 : 0;

  const labels = ["Box", "Circle", "HSV", "RGB"];
  (pickersOnly ? labels.slice(1) : labels).forEach((text, n) => {
    const i = pickersOnly ? n + 1 : n;
    const t = document.createElement("div");
    t.className = "pal-tab";
    labelled(t, text);
    t.addEventListener("click", () => show(i));
    // Same integer split as PaletteTabStrip::tabRect, so the dividers land on
    // the same columns at any width.
    const count = pickersOnly ? 3 : 4;
    t.style.left = `calc((100% + 2px) * ${n} / ${count} - 1px)`;
    t.style.width = `calc((100% + 2px) / ${count})`;
    tabs.append(t);
  });
  function show(i) {
    page = i;
    [...tabs.children].forEach((t, k) => t.classList.toggle("sel", k === (pickersOnly ? i - 1 : i)));
    box.style.display = i === 0 ? "" : "none";
    const w = el.clientWidth > 34 ? el.clientWidth - 34 : 262;
    pickers.forEach((p, k) => { p.canvas.style.display = k + 1 === i ? "block" : "none"; if (k + 1 === i) { p.layout(w); p.setColor(color); } });
  }
  function drawBox() {
    box.replaceChildren();
    swatches.forEach((text) => {
      const c = parseColor(text);
      const s = document.createElement("div");
      s.className = "pal-swatch";
      if (c.r === color.r && c.g === color.g && c.b === color.b && c.a === color.a) s.classList.add("cur");
      s.style.background = c.a < 255 ? `linear-gradient(${toRgba(c)},${toRgba(c)}),repeating-conic-gradient(#cbcbcb 0 25%,#fff 0 50%) 0 0/16px 16px` : toHexRgb(c);
      s.addEventListener("click", () => commit(c));
      s.addEventListener("contextmenu", (e) => { e.preventDefault(); swatches = swatches.filter((x) => x !== text); drawBox(); emit("box", "remove:" + toHexArgb(c)); });
      box.append(s);
    });
    const plus = document.createElement("div");
    plus.className = "pal-swatch pal-plus";
    plus.innerHTML = `<svg width="24" height="24" viewBox="0 0 24 24"><path d="M6.5 12.25h11M12 6.75v11" stroke="currentColor" stroke-width="1.5"/></svg>`;
    plus.addEventListener("click", () => emit("newColor", color));
    box.append(plus);
  }
  function preview(c) { color = c; setRow(c); }
  function commit(c) { color = c; setRow(c); drawBox(); pickers.forEach((p) => p.setColor(c)); emit("color", toHexArgb(c)); }
  pickers.forEach((p) => { p.onPreview = preview; p.onCommit = commit; });

  return {
    el,
    update(control) {
      color = parseColor(control.value);
      swatches = (control.swatches ?? []).map(String);
      setRow(color); drawBox();
      pickers.forEach((p) => p.setColor(color));
      show(page);
    },
    // Value first: the rebuild the add triggers reads the current colour back.
    addSwatch(text) { commit(parseColor(text)); if (!swatches.includes(text)) { swatches.push(text); drawBox(); emit("box", "add:" + text); } },
    color: () => color,
  };
}
