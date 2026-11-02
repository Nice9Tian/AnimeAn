// The timeline, drawn to match childrenpanel/timelinewindow.cpp: the transport
// bar, the frame strip, the side title bar. It holds no model or playback policy:
// it reports what the user did and redraws what update() is told.
//
// The bar is DOM (real cells, a real <input>). The strip is a canvas and a direct
// port of TimelineStrip::paintEvent, because Qt paints it without antialiasing and
// a sheet can run to thousands of cells.
import { scrollBar } from "./panels.js";

// ---- design metrics, same names as the anonymous namespace in the .cpp ---------
const kBarHeight = 34, kButtonWidth = 33, kPlayWidth = 38, kGlyphPx = 17, kSmallGlyphPx = 15;
const kDividerGap = 6, kFrameFieldWidth = 42, kFrameFieldHeight = 22, kFrameFieldGap = 8, kFamilyGap = 8;
const kKeyCellWidth = 126, kKeyCellHeight = 74, kHoldFraction = 0.2, kPreviewAspect = 4 / 3;
const kLaneThickness = 18, kLaneDot = 16, kLaneLine = 3, kMinLaneThickness = 11, kMinLaneDotRadius = 4, kMinLaneLine = 2;
const kScrollThickness = 14, kScrollIntoViewMargin = 10, kStripColumnWidth = 126, kStripCommandHeight = 28, kStripCommandWidth = 30;
const kStripCommandCount = 4, kMinStripThickness = 44, kMinStripColumn = kStripCommandCount * kStripCommandWidth + kScrollThickness;
const kSideTitleHeight = 22, kCollapsedColumnWidth = 132;
const kDockTitleHeight = 18;  // .vf-dock-header in shell.css; the Qt dock has none above the bar

const kCadences = [
  { title: "1s with 4", shortTitle: "1s×4", fps: 6 },
  { title: "1s with 3", shortTitle: "1s×3", fps: 8 },
  { title: "1s with 2", shortTitle: "1s×2", fps: 12 },
  { title: "1s with 1", shortTitle: "1s×1", fps: 24 },
];

export function fpsForText(text, fallback) {
  const trimmed = String(text).trim();
  for (const c of kCadences) if (trimmed === c.title || trimmed === c.shortTitle) return c.fps;
  const match = /^(\d{1,3})\s*(?:fps)?$/i.exec(trimmed);
  if (match) {
    const typed = Number(match[1]);
    if (typed >= 1 && typed <= 120) return typed;
  }
  return fallback;
}
export function textForFps(fps) {
  const c = kCadences.find((x) => x.fps === fps);
  return c ? `${c.title}  (${fps} fps)` : `${fps} fps`;
}
export function shortTextForFps(fps) {
  return kCadences.find((x) => x.fps === fps)?.shortTitle ?? `${fps} fps`;
}

// Qt sums hinted advances, and the layout below depends on them: these are the
// widths QFontMetrics reports at labelFont(10), and Consolas Bold is 6px per
// character at 11px and 5px at 10px once hinted.
const LABEL_WIDTH = { GUIDE: 32, LINE: 23, FILL: 20, AM: 19, LAYER: 32 };
const MONO11_ADVANCE = 6, MONO10_ADVANCE = 5;

// The same story for the application font: at 9pt Qt lays Segoe UI out on whole-pixel
// advances, the browser on fractional ones, and a long row ends up 7px apart. These
// are QFontMetrics::horizontalAdvance per character; the sum is what Qt measures,
// and hintedTracking() is the letter-spacing that makes the browser land on it.
const SEGOE12 = {
  a: 6, b: 7, c: 6, d: 7, e: 6, f: 4, g: 7, h: 7, i: 3, j: 3, k: 6, l: 3, m: 11, n: 7, o: 7, p: 7, q: 7, r: 4, s: 5, t: 4, u: 7, v: 6, w: 9, x: 5, y: 6, z: 5,
  A: 8, B: 7, C: 8, D: 8, E: 6, F: 6, G: 8, H: 9, I: 3, J: 4, K: 7, L: 6, M: 11, N: 9, O: 9, P: 7, Q: 9, R: 7, S: 6, T: 7, U: 8, V: 7, W: 11, X: 7, Y: 7, Z: 7,
  0: 6, 1: 6, 2: 6, 3: 6, 4: 6, 5: 6, 6: 6, 7: 6, 8: 6, 9: 6, " ": 3, ".": 3, ",": 3, "-": 5, _: 5, ">": 8, "·": 3,
};
const tracker = document.createElement("canvas").getContext("2d");
export function hintedTracking(text) {
  if (!text) return "0px";
  tracker.font = '12px "Segoe UI"';
  let width = 0;
  for (const ch of text) width += SEGOE12[ch] ?? Math.round(tracker.measureText(ch).width);
  return `${(width - tracker.measureText(text).width) / text.length}px`;
}
function tracked(cls, text) {
  const d = div(cls, text);
  d.style.letterSpacing = hintedTracking(text);
  return d;
}
export { tracked as trackedDiv };

// ---- glyphs: glyphPath() on its 24x24 sheet -------------------------------------
const rect = (x, y, w, h) => `M${x} ${y}h${w}v${h}h${-w}z`;
const GLYPH = {
  Prev: "M17 5L17 19L7 12ZM4 5L4 19",
  Pause: "M9 5L9 19M15 5L15 19",
  Play: "M7 4L20 12L7 20Z",
  // QPainterPath::arcTo(circle, 0, 310): counter-clockwise from 3 o'clock.
  Loop: "M20 11A8 8 0 1 0 17.142 17.128M20 5L20 11L14 11",
  Next: "M7 5L7 19L17 12ZM20 5L20 19",
  ChevronDown: "M6 9L12 15L18 9",
  CollapseUp: "M6 14L12 8L18 14M6 19L12 13L18 19",
  CollapseDown: "M6 10L12 16L18 10M6 5L12 11L18 5",
  Close: "M5 5L19 19M19 5L5 19",
  Float: `M3 15L3 4L16 4L16 7${rect(8, 8, 13, 12)}M8 12L21 12`,
  ToVertical: `${rect(3, 11, 12, 9)}M6 7C10 2.8 17 3.8 20 8M20 8L16.6 7.5M20 8L19.5 4.6`,
  ToHorizontal: `${rect(11, 8, 9, 12)}M7 5C2.8 9 3.8 16 8 19M8 19L7.5 15.6M8 19L11.4 18.5`,
  AddFrame: "M5 3L13 3L18 8L18 21L5 21ZM13 3L13 8L18 8M11.5 11.5L11.5 17.5M8.5 14.5L14.5 14.5",
  AddHold: `${rect(4, 4, 4, 16)}M14 8L14 18M9 13L19 13`,
  Duplicate: `${rect(8, 3, 12, 12)}${rect(4, 9, 12, 12)}`,
  DeleteFrame: "M4 7L20 7M9 7L9 4L15 4L15 7M6 7L7 20L17 20L18 7M10 11L10 17M14 11L14 17",
};

// ---- QCosmeticStroker -----------------------------------------------------------
// An antialiased pen no wider than one device pixel does not go through the
// stroker: QRasterPaintEngine hands it to QCosmeticStroker, a two-pixel line walker
// whose diagonals look nothing like a 1px polygon. The 15px glyphs (pen 0.94), the
// 13px close, the 11px chevron and every 1px line of the Repulsion Pad take this
// route, so the walker is ported here. opacity is (int)(256 * width * scale).
const toF26 = (v) => Math.trunc(v * 64);
const fixedDiv = (a, b) => Math.trunc((a * 65536) / b);

function cosmeticLine(rx1, ry1, rx2, ry2, capBegin, capEnd, opacity, plot) {
  const a0 = toF26(rx1), b0 = toF26(ry1), a1 = toF26(rx2), b1 = toF26(ry2);
  const dx = a1 - a0, dy = b1 - b0;
  const steep = Math.abs(dx) < Math.abs(dy);
  if (!steep && !dx) return;
  // Walk along the major axis; m is that axis, n the other.
  let m0 = steep ? b0 : a0, m1 = steep ? b1 : a1, n0 = steep ? a0 : b0;
  const inc = steep ? fixedDiv(dx, dy) : fixedDiv(dy, dx);
  if (m0 > m1) {
    [m0, m1] = [m1, m0];
    n0 = steep ? a1 : b1;
    [capBegin, capEnd] = [capEnd, capBegin];
  }
  let n = (n0 - 32) * 1024;
  n -= ((((m0 & 63) - 32) * inc) >> 6);
  if (capBegin) { m0 -= 32; n -= inc >> 1; }
  if (capEnd) m1 += 32;
  let m = m0 >> 6;
  const end = m1 >> 6;
  const same = m === end;
  const alphaStart = same ? m1 - m0 : 64 - (m0 & 63);
  const alphaEnd = same ? 0 : m1 & 63;
  const put = (scale) => {
    const alpha = (n >> 8) & 255, near = n >> 16;
    const first = scale < 0 ? 255 - alpha : ((255 - alpha) * scale) >> 6;
    const second = scale < 0 ? alpha : (alpha * scale) >> 6;
    if (steep) { plot(near, m, (first * opacity) >> 8); plot(near + 1, m, (second * opacity) >> 8); }
    else { plot(m, near, (first * opacity) >> 8); plot(m, near + 1, (second * opacity) >> 8); }
  };
  put(alphaStart);
  n += inc; m++;
  for (; m < end; m++) { put(-1); n += inc; }
  if (alphaEnd) put(alphaEnd);
}

function flattenCubic(p0, p1, p2, p3, out, level = 6) {
  const dx = p3[0] - p0[0], dy = p3[1] - p0[1];
  const d1 = Math.abs((p1[0] - p3[0]) * dy - (p1[1] - p3[1]) * dx), d2 = Math.abs((p2[0] - p3[0]) * dy - (p2[1] - p3[1]) * dx);
  const len = Math.abs(dx) + Math.abs(dy);
  if (level === 0 || (d1 + d2) <= 0.5 * len) { out.push(p3); return; }
  const mid = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
  const a = mid(p0, p1), b = mid(p1, p2), c = mid(p2, p3), e = mid(a, b), f = mid(b, c), g = mid(e, f);
  flattenCubic(p0, a, e, g, out, level - 1);
  flattenCubic(g, f, c, p3, out, level - 1);
}
export { flattenCubic };

// subpaths: lists of [x, y] in device pixels, a closed one ending on its start.
// caps is false for a flat cap. plot(x, y, coverage) gets 0..255.
export function cosmeticStroke(subpaths, opacity, caps, plot) {
  for (const points of subpaths) {
    const last = points[points.length - 1];
    const closed = points.length > 2 && points[0][0] === last[0] && points[0][1] === last[1];
    for (let i = 0; i + 1 < points.length; i++) {
      const [x0, y0] = points[i], [x1, y1] = points[i + 1];
      cosmeticLine(x0, y0, x1, y1, caps && !closed && i === 0, caps && !closed && i === points.length - 2, opacity, plot);
    }
  }
}

function subpathsOf(d, tx, ty, s) {
  const tokens = d.match(/[A-Za-z]|-?\d*\.?\d+/g);
  const map = (x, y) => [tx + x * s, ty + y * s];
  const out = [];
  let i = 0, cmd = "", x = 0, y = 0, start = [0, 0], current = null;
  const num = () => Number(tokens[i++]);
  while (i < tokens.length) {
    if (/[A-Za-z]/.test(tokens[i])) cmd = tokens[i++];
    switch (cmd) {
      case "M": x = num(); y = num(); start = [x, y]; current = [map(x, y)]; out.push(current); break;
      case "L": x = num(); y = num(); current.push(map(x, y)); break;
      case "h": x += num(); current.push(map(x, y)); break;
      case "v": y += num(); current.push(map(x, y)); break;
      case "C": {
        const c1 = map(num(), num()), c2 = map(num(), num());
        x = num(); y = num();
        flattenCubic(current[current.length - 1], c1, c2, map(x, y), current);
        break;
      }
      case "z": case "Z": [x, y] = start; current.push(map(x, y)); break;
    }
  }
  return out;
}

const maskCache = new Map();
function cosmeticMask(glyph, boxW, boxH, px) {
  const key = `${glyph}/${boxW}/${boxH}/${px}`;
  if (maskCache.has(key)) return maskCache.get(key);
  const s = px / 24;
  const alpha = new Float64Array(boxW * boxH);
  cosmeticStroke(subpathsOf(GLYPH[glyph], (boxW - px) / 2, (boxH - px) / 2, s), Math.trunc(256 * 1.5 * s), true, (x, y, coverage) => {
    if (x < 0 || y < 0 || x >= boxW || y >= boxH) return;
    const c = coverage / 255;
    alpha[y * boxW + x] += c * (1 - alpha[y * boxW + x]);
  });
  const canvas = document.createElement("canvas");
  canvas.width = boxW; canvas.height = boxH;
  const g = canvas.getContext("2d");
  const image = g.createImageData(boxW, boxH);
  alpha.forEach((a, i) => { image.data[4 * i + 3] = Math.round(a * 255); });
  g.putImageData(image, 0, 0);
  const url = canvas.toDataURL();
  maskCache.set(key, url);
  return url;
}

// One glyph in a box exactly as drawGlyph places it. The sheet is scaled and
// translated inside an SVG the size of the box, so a half-pixel centring offset
// stays a half pixel instead of being snapped by layout.
function glyphSvg(glyph, boxW, boxH, px) {
  const tx = (boxW - px) / 2, ty = (boxH - px) / 2, s = px / 24;
  if (glyph !== "Onion" && 1.5 * s * (window.devicePixelRatio || 1) <= 1) {
    const url = cosmeticMask(glyph, boxW, boxH, px);
    return `<div class="tl-mask" style="width:${boxW}px;height:${boxH}px;-webkit-mask-image:url(${url});mask-image:url(${url})"></div>`;
  }
  const pen = `stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"`;
  let body;
  if (glyph === "Onion") {
    // Three overlapping sheets, the nearest one solid.
    const circle = (cx, alpha, stroked) => `<circle cx="${cx}" cy="12" r="7.2" fill="currentColor" fill-opacity="${alpha}" ${stroked ? pen : ""}/>`;
    body = circle(8.4, 0.16, true) + circle(11.8, 0.36, true) + circle(15.6, 1, false);
  } else {
    body = `<path d="${GLYPH[glyph]}" fill="none" ${pen}/>`;
  }
  return `<svg width="${boxW}" height="${boxH}" xmlns="http://www.w3.org/2000/svg"><g transform="translate(${tx} ${ty}) scale(${s})">${body}</g></svg>`;
}

function glyphFor(command, state) {
  switch (command) {
    case "Orientation": return state.vertical ? "ToHorizontal" : "ToVertical";
    case "Collapse": return state.collapsed ? "CollapseDown" : "CollapseUp";
    default: return command;
  }
}

function tooltipFor(command, state) {
  const offBoard = "Onion skin is a main board feature - point the timeline back at the main board to use it";
  const onionClass = (text) => (!state.onionAvailable ? offBoard : state.onion ? text : "Turn onion skin on first");
  switch (command) {
    case "AddFrame": return "Add frame at the end of the sheet";
    case "AddHold": return "Hold the current frame - the new row right after it shows the same drawing; editing either one changes both";
    case "Duplicate": return "Duplicate frame - copies the current drawing to a new frame";
    case "DeleteFrame": return "Delete frame";
    case "Onion": return state.onionAvailable ? "Onion skin" : offBoard;
    case "OnionLines": return onionClass("Show painting lines in the onion ghosts");
    case "OnionFills": return onionClass("Show fill areas in the onion ghosts");
    case "OnionAmLayers": return onionClass("Show auto-mapping layers in the onion ghosts");
    case "GuideLines": return onionClass("Show guide lines in the onion ghosts");
    case "Prev": return "Previous frame";
    case "Pause": return "Pause";
    case "Play": return "Play";
    case "Loop": return "Loop";
    case "Next": return "Next frame";
    case "Rate": return "Playback rate";
    case "Orientation": return state.vertical ? "Dock the strip under the canvas" : "Dock the strip beside the canvas";
    case "Float": return state.floating ? "Dock the timeline again" : "Float the timeline as a window";
    case "Collapse": return state.collapsed ? "Show the frame strip" : "Hide the frame strip";
    case "Close": return "Close the timeline window";
  }
  return "";
}

// QFont::PercentageSpacing over hinted advances, reached from the browser's unhinted
// ones: each label gets the tracking that makes it as wide as Qt measured it.
const measurer = document.createElement("canvas").getContext("2d");
function labelDiv(cls, text, width, nudge) {
  const d = document.createElement("div");
  d.className = cls;
  d.textContent = text;
  measurer.font = 'bold 10px "Segoe UI"';
  d.style.letterSpacing = `${(width - measurer.measureText(text).width) / text.length}px`;
  if (nudge) d.classList.add("tl-nudge");
  return d;
}

const paddedFrame = (frame) => String(frame + 1).padStart(3, "0");

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}

const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// Repaint hook for the canvas panels: CSS variables reach DOM on their own, a
// painted surface has to be told the theme changed.
export function onThemeChange(repaint) {
  new MutationObserver(repaint).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
}

// ---- QToolTip -------------------------------------------------------------------
// The stylesheet rule from theme.cpp, after Qt's wake-up delay. text(event)
// is asked at show time because the strip's tip depends on where the pointer is.
let tipEl = null, tipTimer = 0;
function hideTip() { clearTimeout(tipTimer); tipTimer = 0; if (tipEl) { tipEl.remove(); tipEl = null; } }
export function qtTooltip(target, text) {
  let last = null;
  const show = () => {
    const value = text(last);
    if (!value) return;
    hideTip();
    tipEl = div("qtooltip", value);
    tipEl.style.left = `${last.clientX + 2}px`;
    tipEl.style.top = `${last.clientY + 20}px`;
    document.body.append(tipEl);
    const r = tipEl.getBoundingClientRect();
    if (r.right > innerWidth) tipEl.style.left = `${Math.max(0, innerWidth - r.width)}px`;
    if (r.bottom > innerHeight) tipEl.style.top = `${Math.max(0, last.clientY - r.height - 4)}px`;
  };
  target.addEventListener("pointermove", (e) => {
    last = e;
    if (tipEl) { if (text(e) !== tipEl.textContent) hideTip(); return; }
    clearTimeout(tipTimer);
    tipTimer = setTimeout(show, 700);
  });
  for (const type of ["pointerleave", "pointerdown"]) target.addEventListener(type, hideTip);
}

// ---- the menu the rate cell opens -----------------------------------------------
const CHECK = `<svg width="14" height="14" xmlns="http://www.w3.org/2000/svg"><rect x="0.5" y="0.5" width="13" height="13" fill="url(#qcheck-fill)" stroke="var(--check-outline)"/>`
  + `<defs><linearGradient id="qcheck-fill" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--check-top)"/><stop offset="0.15" stop-color="var(--surface)"/><stop offset="1" stop-color="var(--surface)"/></linearGradient></defs>`;
const CHECK_MARK = `<path transform="translate(-0.3 1)" d="M4.12 6.11L6.5 10.31L10.31 2.69" fill="none" stroke="var(--check-mark)" stroke-width="1.69"/>`;

let rateMenu = null;
function closeRateMenu() {
  if (!rateMenu) return;
  rateMenu.remove();
  rateMenu = null;
  window.removeEventListener("pointerdown", outsideRateMenu, true);
  window.removeEventListener("keydown", rateMenuKey, true);
}
function outsideRateMenu(e) { if (rateMenu && !rateMenu.contains(e.target)) closeRateMenu(); }
function rateMenuKey(e) { if (e.key === "Escape") { e.stopPropagation(); closeRateMenu(); } }

function showRateMenu(anchor, fps, onPick) {
  closeRateMenu();
  const menu = div("rate-menu");
  for (const c of kCadences) {
    const row = div("rate-item");
    row.innerHTML = `<span class="rate-check">${CHECK}${fps === c.fps ? CHECK_MARK : ""}</svg></span>`;
    row.append(tracked("rate-text", `${c.title}  -  ${c.fps} fps`));
    row.addEventListener("click", () => { closeRateMenu(); onPick(c.fps); });
    menu.append(row);
  }
  menu.append(div("rate-sep"));
  const typed = div("rate-typed");
  const field = document.createElement("input");
  field.className = "rate-field";
  field.value = String(fps);
  field.addEventListener("input", () => { field.value = field.value.replace(/\D/g, "").slice(0, 3); });
  field.addEventListener("keydown", (e) => {
    if (e.key !== "Enter") return;
    const next = fpsForText(field.value, fps);
    closeRateMenu();
    onPick(next);
  });
  typed.append(tracked("rate-note", "or type"), field, tracked("rate-note", "fps · 1-120"));
  menu.append(typed);
  document.body.append(menu);
  const height = menu.getBoundingClientRect().height;
  menu.style.left = `${anchor.left}px`;
  menu.style.top = `${anchor.top - height - 4}px`;
  rateMenu = menu;
  window.addEventListener("pointerdown", outsideRateMenu, true);
  window.addEventListener("keydown", rateMenuKey, true);
}

// ---- transport ------------------------------------------------------------------
const FRAME_COMMANDS = ["AddFrame", "AddHold", "Duplicate", "DeleteFrame"];
const SMALL_GLYPHS = new Set(["Orientation", "Collapse", "Close", "Float"]);
const TWO_LINE = { OnionAmLayers: ["AM", "LAYER"], GuideLines: ["GUIDE", "LINE"] };
// What is happening right now rather than a setting: these take the focus colour.
const LIVE_COMMANDS = new Set(["Play", "Onion"]);
const ONE_LINE = { OnionLines: "LINE", OnionFills: "FILL" };

function createBar(trigger) {
  const el = div("tl-bar");
  const rule = div("tl-bar-rule");
  el.append(rule);
  let state = null, rows = 1, items = [];
  const cells = new Map();
  const dividers = [div("tl-divider"), div("tl-divider")];
  el.append(...dividers);

  const field = document.createElement("input");
  field.className = "tl-field";
  field.maxLength = 4;
  field.spellcheck = false;
  field.autocomplete = "off";
  field.addEventListener("input", () => { field.value = field.value.replace(/\D/g, ""); });
  const finish = () => {
    const typed = parseInt(field.value, 10);
    if (typed >= 1) trigger("FrameTyped", typed - 1);
    field.value = paddedFrame(state.current);
  };
  field.addEventListener("keydown", (e) => { if (e.key === "Enter") field.blur(); });
  field.addEventListener("blur", finish);
  qtTooltip(field, () => "Current frame - type a number to jump");
  el.append(field);

  function cellFor(command) {
    let c = cells.get(command);
    if (c) return c;
    c = div("tl-cell");
    if (ONE_LINE[command]) c.append(labelDiv("tl-label", ONE_LINE[command], LABEL_WIDTH[ONE_LINE[command]], true));
    else if (TWO_LINE[command]) {
      const [a, b] = TWO_LINE[command];
      const nudge = command === "OnionAmLayers";
      c.append(labelDiv("tl-label tl-upper", a, LABEL_WIDTH[a], nudge), labelDiv("tl-label tl-lower", b, LABEL_WIDTH[b], nudge));
    } else if (command === "Rate") {
      const text = div("tl-rate-text");
      const chevron = div("tl-rate-chevron");
      chevron.innerHTML = glyphSvg("ChevronDown", 11, kBarHeight, 11);
      c.append(text, chevron);
      c._text = text;
    }
    c.addEventListener("click", () => {
      const item = items.find((i) => i.command === command);
      if (!item || !item.enabled) return;
      if (command === "Rate") showRateMenu(c.getBoundingClientRect(), state.fps, (fps) => trigger("Rate", fps));
      else trigger(command);
    });
    qtTooltip(c, () => tooltipFor(command, state));
    cells.set(command, c);
    el.append(c);
    return c;
  }

  function relayout() {
    if (!state) return;
    const width = el.clientWidth;
    if (!width) return;
    items = [];
    const guideWidth = LABEL_WIDTH.GUIDE + 14;
    const classWidth = Math.max(LABEL_WIDTH.LINE, LABEL_WIDTH.FILL) + 14;
    const amWidth = Math.max(LABEL_WIDTH.AM, LABEL_WIDTH.LAYER) + 14;
    const rateWidth = shortTextForFps(state.fps).length * MONO11_ADVANCE + 9 + 7 + 11 + 9;
    const add = (command, x, y, w, enabled, active) => items.push({ command, x, y, w, enabled, active });

    const collapseAtLeft = state.vertical && state.leftAligned && !state.floating;
    const showFrameCommands = !state.vertical;
    const leftWidth = (collapseAtLeft ? kButtonWidth : 0) + (showFrameCommands ? kStripCommandCount * kButtonWidth : 0);
    const chrome = ["Close"];
    if (!collapseAtLeft && !state.floating) chrome.push("Collapse");
    chrome.push("Orientation", "Float");
    const chromeWidth = chrome.length * kButtonWidth;

    const dividerWidth = 1 + 2 * kDividerGap;
    const onionWidth = kButtonWidth + 2 * classWidth + amWidth + guideWidth;
    const playbackWidth = kButtonWidth * 4 + kPlayWidth;
    const rateFieldWidth = rateWidth + kFrameFieldWidth + 2 * kFrameFieldGap;
    const assemblyWidth = onionWidth + dividerWidth + playbackWidth + dividerWidth + rateFieldWidth;

    // Twice the WIDER side, not the sum: the assembly is centred on the whole bar.
    const oneRowWidth = 2 * Math.max(leftWidth, chromeWidth) + assemblyWidth + 2 * kFamilyGap;
    rows = width >= oneRowWidth ? 1 : 2;
    el.style.height = `${kBarHeight * rows}px`;
    rule.style.display = rows > 1 ? "" : "none";
    const h = kBarHeight;

    let x = 0;
    if (collapseAtLeft) { add("Collapse", x, 0, kButtonWidth, true, false); x += kButtonWidth; }
    if (showFrameCommands) for (const command of FRAME_COMMANDS) { add(command, x, 0, kButtonWidth, true, false); x += kButtonWidth; }

    let right = width;
    for (const command of chrome) { right -= kButtonWidth; add(command, right, 0, kButtonWidth, true, false); }

    // Centred on the bar, not on what is left of it: playback stays put when the
    // side groups change.
    const assemblyY = rows === 1 ? 0 : kBarHeight;
    let cx = Math.max(0, Math.trunc((width - assemblyWidth) / 2));
    if (rows === 1) cx = Math.max(x, Math.min(cx, width - chromeWidth - kFamilyGap - assemblyWidth));

    const live = state.onionAvailable && state.onion;
    add("Onion", cx, assemblyY, kButtonWidth, state.onionAvailable, live); cx += kButtonWidth;
    add("OnionLines", cx, assemblyY, classWidth, live, live && state.onionLines); cx += classWidth;
    add("OnionFills", cx, assemblyY, classWidth, live, live && state.onionFills); cx += classWidth;
    add("OnionAmLayers", cx, assemblyY, amWidth, live, live && state.onionAmLayers); cx += amWidth;
    add("GuideLines", cx, assemblyY, guideWidth, live, live && state.guideLines); cx += guideWidth;
    placeDivider(dividers[0], cx + kDividerGap, assemblyY + Math.trunc((h - 20) / 2));
    cx += dividerWidth;
    add("Prev", cx, assemblyY, kButtonWidth, true, false); cx += kButtonWidth;
    add("Pause", cx, assemblyY, kButtonWidth, state.playing, false); cx += kButtonWidth;
    add("Play", cx, assemblyY, kPlayWidth, !state.playing, state.playing); cx += kPlayWidth;
    add("Loop", cx, assemblyY, kButtonWidth, true, state.loop); cx += kButtonWidth;
    add("Next", cx, assemblyY, kButtonWidth, true, false); cx += kButtonWidth;
    placeDivider(dividers[1], cx + kDividerGap, assemblyY + Math.trunc((h - 20) / 2));
    cx += dividerWidth;
    add("Rate", cx, assemblyY, rateWidth, true, false); cx += rateWidth;
    field.style.left = `${cx + kFrameFieldGap}px`;
    field.style.top = `${assemblyY + Math.trunc((h - kFrameFieldHeight) / 2)}px`;
    paint();
  }

  function placeDivider(d, x, y) { d.style.left = `${x}px`; d.style.top = `${y}px`; }

  function paint() {
    const seen = new Set();
    for (const item of items) {
      const c = cellFor(item.command);
      seen.add(item.command);
      c.style.display = "";
      c.style.left = `${item.x}px`; c.style.top = `${item.y}px`; c.style.width = `${item.w}px`;
      c.classList.toggle("off", !item.enabled);
      c.classList.toggle("on", item.active);
      c.classList.toggle("live", LIVE_COMMANDS.has(item.command) && item.active);
      if (item.command === "Rate") {
        c._text.textContent = shortTextForFps(state.fps);
      } else if (!ONE_LINE[item.command] && !TWO_LINE[item.command]) {
        const px = item.command === "Play" ? 19 : SMALL_GLYPHS.has(item.command) ? kSmallGlyphPx : kGlyphPx;
        const glyph = item.command === "Onion" ? "Onion" : glyphFor(item.command, state);
        const key = `${glyph}/${item.w}/${px}`;
        if (c._glyph !== key) { c.innerHTML = glyphSvg(glyph, item.w, kBarHeight, px); c._glyph = key; }
      }
    }
    for (const [command, c] of cells) if (!seen.has(command)) c.style.display = "none";
    if (document.activeElement !== field) field.value = paddedFrame(state.current);
  }

  new ResizeObserver(relayout).observe(el);
  return {
    el,
    setState(next) { state = next; relayout(); },
    height: () => kBarHeight * rows,
  };
}

// ---- side title bar -------------------------------------------------------------
function createSideTitle(trigger) {
  const el = div("tl-side");
  el.append(labelDiv("tl-label tl-side-text", "ANIMATION", 64));
  const close = div("tl-side-close");
  close.innerHTML = glyphSvg("Close", kSideTitleHeight - 2, kSideTitleHeight - 2, 13);
  close.addEventListener("click", () => trigger("Close"));
  qtTooltip(close, () => "Close the timeline window");
  el.append(close);
  return el;
}

// ---- strip ----------------------------------------------------------------------
// QPainter draws this widget aliased, so a filled disc is the pixels whose centres
// fall inside it, not an antialiased arc.
function aliasedDisc(ctx, cx, cy, r, dpr) {
  if (dpr !== 1) {
    ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
    return;
  }
  for (let y = Math.floor(cy - r); y <= Math.ceil(cy + r); y++) {
    const dy = y + 0.5 - cy, room = r * r - dy * dy;
    if (room < 0) continue;
    // A span on the boundary is out, except that a scan line owns its lower edge.
    if (room === 0) { if (dy > 0) ctx.fillRect(Math.floor(cx), y, 1, 1); continue; }
    const half = Math.sqrt(room);
    const x0 = Math.floor(cx - half - 0.5) + 1, x1 = Math.ceil(cx + half - 0.5) - 1;
    if (x1 >= x0) ctx.fillRect(x0, y, x1 - x0 + 1, 1);
  }
}

function outline(ctx, x, y, w, h, t) {
  ctx.fillRect(x, y, w, t);
  ctx.fillRect(x, y + h - t, w, t);
  ctx.fillRect(x, y + t, t, h - 2 * t);
  ctx.fillRect(x + w - t, y + t, t, h - 2 * t);
}
// A 2px aliased pen leaves the two outer corners on the left side unpainted.
function thickOutline(ctx, x, y, w, h) {
  ctx.fillRect(x + 1, y, w - 1, 1);
  ctx.fillRect(x, y + 1, w, 1);
  ctx.fillRect(x, y + h - 2, w, 1);
  ctx.fillRect(x + 1, y + h - 1, w - 1, 1);
  ctx.fillRect(x, y + 2, 2, h - 4);
  ctx.fillRect(x + w - 2, y + 2, 2, h - 4);
}

const center = (x, w) => Math.trunc((2 * x + w - 1) / 2);

function createStrip(trigger) {
  const el = div("tl-strip");
  const canvas = document.createElement("canvas");
  canvas.className = "tl-canvas";
  const onScrollBar = (value) => { scroll = Math.trunc(value); relayout(); paint(); };
  const hBar = scrollBar(false, onScrollBar), vBar = scrollBar(true, onScrollBar);
  vBar.el.style.top = `${kStripCommandHeight}px`;
  el.append(canvas, hBar.el, vBar.el);
  // Opaque, so the browser keeps LCD text like the rest of the page.
  const ctx = canvas.getContext("2d", { alpha: false });

  let state = null, vertical = false, cells = [];
  let width = 0, height = 0, dpr = 1;
  let scroll = 0, extent = 0, keyExtent = 0, holdExtent = 0, cellThickness = 0;
  let laneThickness = 0, laneDotRadius = 0, laneLine = 0;
  let hoverCommand = -1, pressedCommand = -1, pressedCell = -1, dragging = false, pressX = 0, pressY = 0;
  const thumbs = new Map();

  const cellOrigin = () => (vertical ? kStripCommandHeight : 0);
  const commandRect = (i) => ({ x: i * kStripCommandWidth, y: 0, w: kStripCommandWidth, h: kStripCommandHeight });
  const contains = (r, x, y) => x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  const nameFor = (frame) => {
    const f = state.frames[frame];
    return f?.name ? String(f.name) : String((f?.index ?? frame) + 1);
  };
  const viewport = () => (vertical ? height - cellOrigin() : width);

  function relayout() {
    cells = [];
    if (!state) return;
    const count = Math.max(1, state.frames.length);
    const scaleLane = (scale) => {
      laneThickness = Math.max(kMinLaneThickness, Math.round(kLaneThickness * scale));
      laneDotRadius = Math.max(kMinLaneDotRadius, (kLaneDot / 2) * scale);
      laneLine = Math.max(kMinLaneLine, kLaneLine * scale);
    };
    let offset = 0;
    if (vertical) {
      const rowWidth = Math.max(1, width - kScrollThickness);
      const scale = rowWidth / kStripColumnWidth;
      scaleLane(scale);
      cellThickness = rowWidth;
      keyExtent = Math.max(16, Math.round(kKeyCellHeight * scale));
      holdExtent = Math.max(5, Math.round(keyExtent * kHoldFraction));
      for (let frame = 0; frame < count; frame++) {
        const hold = !!state.frames[frame]?.held;
        const rowHeight = hold ? holdExtent : keyExtent;
        const y = kStripCommandHeight + offset - scroll;
        cells.push({ frame, hold, rect: { x: 0, y, w: rowWidth, h: rowHeight }, lane: { x: rowWidth - laneThickness - 2, y, w: laneThickness, h: rowHeight } });
        offset += rowHeight;
      }
    } else {
      const cellHeight = Math.max(1, height - kScrollThickness);
      const scale = cellHeight / kKeyCellHeight;
      scaleLane(scale);
      cellThickness = cellHeight;
      keyExtent = Math.max(28, Math.round(kKeyCellWidth * scale));
      holdExtent = Math.max(8, Math.round(keyExtent * kHoldFraction));
      for (let frame = 0; frame < count; frame++) {
        const hold = !!state.frames[frame]?.held;
        const cellWidth = hold ? holdExtent : keyExtent;
        const x = offset - scroll;
        cells.push({ frame, hold, rect: { x, y: 0, w: cellWidth, h: cellHeight }, lane: { x, y: 0, w: cellWidth, h: laneThickness } });
        offset += cellWidth;
      }
    }
    extent = offset;
    clampScroll();
  }

  function clampScroll() {
    const maximum = Math.max(0, extent - Math.max(0, viewport()));
    const clamped = Math.min(Math.max(0, scroll), maximum);
    if (clamped === scroll) return;
    const delta = scroll - clamped;
    scroll = clamped;
    for (const cell of cells) {
      const key = vertical ? "y" : "x";
      cell.rect[key] += delta;
      cell.lane[key] += delta;
    }
  }

  // Scrolls the current cell back into view; never during a drag, because the
  // pointer is measured against the cells it was pressed on.
  function ensureCurrentVisible() {
    if (dragging || !state || state.current < 0 || state.current >= cells.length) return;
    const view = viewport();
    if (view <= 0) return;
    const origin = cellOrigin();
    const r = cells[state.current].rect;
    const near = vertical ? r.y : r.x;
    const far = vertical ? r.y + r.h - 1 : r.x + r.w - 1;
    let delta = 0;
    if (near - kScrollIntoViewMargin < origin) delta = near - kScrollIntoViewMargin - origin;
    else if (far + kScrollIntoViewMargin > origin + view - 1) delta = far + kScrollIntoViewMargin - (origin + view - 1);
    if (delta === 0) return;
    scroll += delta;
    relayout();
  }

  function thumbnailFor(frame) {
    const t = state.frames[frame]?.thumbnail;
    if (!t) return null;
    if (typeof t !== "string") return t;
    let image = thumbs.get(t);
    if (!image) {
      image = new Image();
      image.onload = paint;
      image.src = t;
      thumbs.set(t, image);
    }
    return image.complete && image.naturalWidth ? image : null;
  }

  // The preview is the PAGE, so it keeps 4:3 whatever the cell does.
  function previewOf(cell) {
    let ph = vertical ? cell.rect.h - 8 : cell.rect.h;
    const room = vertical ? cell.rect.w - laneThickness - 2 - 4 : cell.rect.w - 4;
    const pw = Math.min(room, Math.round(ph * kPreviewAspect));
    if (pw <= 0 || ph <= 0) return null;
    ph = Math.min(ph, Math.round(pw / kPreviewAspect));
    return vertical
      ? { x: cell.rect.x + Math.trunc((cell.rect.w - laneThickness - 2 - pw) / 2), y: cell.rect.y + 4, w: pw, h: ph }
      : { x: cell.rect.x + Math.trunc((cell.rect.w - pw) / 2), y: cell.rect.y + Math.trunc((cell.rect.h - ph) / 2), w: pw, h: ph };
  }

  function drawGlyph(box, glyph, color, px) {
    ctx.save();
    const s = px / 24;
    ctx.translate(box.x + (box.w - px) / 2, box.y + (box.h - px) / 2);
    ctx.scale(s, s);
    ctx.strokeStyle = color; ctx.lineWidth = 1.5; ctx.lineCap = "round"; ctx.lineJoin = "round";
    ctx.stroke(new Path2D(GLYPH[glyph]));
    ctx.restore();
  }

  function paint() {
    if (!state || !width || !height) return;
    const c = {
      ground: cssVar("--window"), surface: cssVar("--surface"), divider: cssVar("--divider"), accent: cssVar("--accent"), onAccent: cssVar("--on-accent"),
      dim: cssVar("--text-dim"), text: cssVar("--text"), hold: cssVar("--strip-hold"),
      wash: cssVar("--accent-wash"), past: cssVar("--onion-past"), ahead: cssVar("--onion-ahead"),
      pastLine: cssVar("--onion-past-line"), aheadLine: cssVar("--onion-ahead-line"),
    };
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = c.ground;
    ctx.fillRect(0, 0, width, height);

    if (vertical) {
      ctx.fillStyle = c.surface;
      ctx.fillRect(0, 0, width, kStripCommandHeight);
      ctx.fillStyle = c.divider;
      ctx.fillRect(0, kStripCommandHeight - 1, width, 1);
      for (let i = 0; i < kStripCommandCount; i++) {
        const box = commandRect(i), hover = i === hoverCommand;
        if (hover) { ctx.fillStyle = c.wash; ctx.fillRect(box.x, box.y, box.w, box.h); }
        drawGlyph(box, FRAME_COMMANDS[i], hover ? c.text : c.dim, kGlyphPx);
      }
      ctx.save();
      ctx.beginPath();
      ctx.rect(0, cellOrigin(), width, height - cellOrigin());
      ctx.clip();
    }

    const current = state.current;
    const lanes = state.lanes;
    let firstPast = Infinity, lastAhead = -Infinity;
    if (state.onionAvailable) {
      for (const frame of lanes) {
        if (frame < current) firstPast = Math.min(firstPast, frame);
        else if (frame > current) lastAhead = Math.max(lastAhead, frame);
      }
    }

    ctx.font = 'bold 10px Consolas, monospace';
    ctx.textBaseline = "alphabetic";
    ctx.textAlign = "left";

    const onScreen = [];
    let previewSize = null;
    for (const cell of cells) {
      const r = cell.rect;
      if (r.x >= width || r.y >= height || r.x + r.w <= 0 || r.y + r.h <= 0) continue;
      ctx.fillStyle = cell.hold ? c.hold : c.ground;
      ctx.fillRect(r.x, r.y, r.w, r.h);
      ctx.fillStyle = c.divider;
      if (vertical) ctx.fillRect(r.x, r.y + r.h - 1, r.w, 1);
      else ctx.fillRect(r.x + r.w - 1, r.y, 1, r.h);

      if (!cell.hold) {
        const preview = previewOf(cell);
        if (preview) {
          onScreen.push(cell.frame);
          previewSize ??= { width: preview.w, height: preview.h };
          ctx.fillStyle = "#fff";
          ctx.fillRect(preview.x, preview.y, preview.w, preview.h);
          const image = thumbnailFor(cell.frame);
          if (image) ctx.drawImage(image, preview.x, preview.y, preview.w, preview.h);
          ctx.fillStyle = c.divider;
          outline(ctx, preview.x, preview.y, preview.w, preview.h, 1);

          // The name is printed ON the drawing, under the lane so it never eats a click.
          const currentCell = cell.frame === current;
          const name = nameFor(cell.frame);
          const chipWidth = Math.min(preview.w - 4, name.length * MONO10_ADVANCE + 10);
          const chip = { x: preview.x + 2, y: preview.y + (vertical ? 1 : laneThickness + 1), w: Math.max(12, chipWidth), h: 13 };
          ctx.fillStyle = currentCell ? c.accent : c.surface;
          ctx.fillRect(chip.x, chip.y, chip.w, chip.h);
          let shown = name;
          const fit = Math.floor((chip.w - 4) / MONO10_ADVANCE);
          if (name.length > fit) shown = fit > 1 ? name.slice(0, fit - 1) + "…" : "…";
          ctx.fillStyle = currentCell ? c.onAccent : c.dim;
          ctx.fillText(shown, chip.x + Math.trunc((chip.w - shown.length * MONO10_ADVANCE) / 2), chip.y + 10);
        }
      } else {
        // A hold is MARKED, not drawn.
        const mx = center(r.x, r.w), my = center(r.y, r.h);
        ctx.fillStyle = c.dim;
        thickOutline(ctx, mx - 5, my - 5, 10, 10);
      }

      // The lane: a line while the ghost run passes through, a dot where it is anchored.
      let lineColor = null;
      if (cell.frame < current && cell.frame >= firstPast) lineColor = c.pastLine;
      else if (cell.frame > current && cell.frame <= lastAhead) lineColor = c.aheadLine;
      const laneX = center(cell.lane.x, cell.lane.w) + 0.5, laneY = center(cell.lane.y, cell.lane.h) + 0.5;
      if (lineColor) {
        ctx.fillStyle = lineColor;
        const mid = vertical ? laneX : laneY;
        const a = Math.floor(mid - laneLine / 2 - 0.5) + 1, b = Math.floor(mid + laneLine / 2 - 0.5);
        if (vertical) ctx.fillRect(a, cell.lane.y, b - a + 1, cell.lane.h);
        else ctx.fillRect(cell.lane.x, a, cell.lane.w, b - a + 1);
      }
      if (!cell.hold && state.onionAvailable && lanes.has(cell.frame)) {
        ctx.fillStyle = cell.frame < current ? c.past : cell.frame > current ? c.ahead : c.dim;
        aliasedDisc(ctx, laneX, laneY, laneDotRadius, dpr);
      }

      if (cell.frame === current) {
        ctx.fillStyle = c.accent;
        thickOutline(ctx, r.x, r.y, r.w, r.h);
      }
    }
    if (vertical) ctx.restore();

    const view = viewport(), overflowing = extent > view && view > 0;
    hBar.el.hidden = vertical || !overflowing;
    vBar.el.hidden = !vertical || !overflowing;
    if (overflowing) (vertical ? vBar : hBar).set({ min: 0, max: extent - view, page: view, value: scroll });
    // A collapsed strip is out of the page: it shows no frames, so it asks for none.
    if (el.isConnected !== false) trigger("Preview", onScreen, previewSize);
  }

  function cellAt(x, y) {
    for (let i = 0; i < cells.length; i++) if (contains(cells[i].rect, x, y)) return i;
    return -1;
  }
  // Past the last centre is a slot of its own: the end of the run.
  function dropTargetAt(x, y) {
    for (let i = 0; i < cells.length; i++) {
      const r = cells[i].rect;
      if (vertical ? y < center(r.y, r.h) : x < center(r.x, r.w)) return i;
    }
    return cells.length;
  }
  const local = (e) => {
    const b = canvas.getBoundingClientRect();
    return [e.clientX - b.left, e.clientY - b.top];
  };

  canvas.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const [x, y] = local(e);
    pressX = x; pressY = y;
    dragging = false; pressedCell = -1; pressedCommand = -1;
    canvas.setPointerCapture(e.pointerId);
    if (vertical && y < kStripCommandHeight) {
      for (let i = 0; i < kStripCommandCount; i++) if (contains(commandRect(i), x, y)) { pressedCommand = i; break; }
      return;
    }
    pressedCell = cellAt(x, y);
  });
  canvas.addEventListener("pointermove", (e) => {
    const [x, y] = local(e);
    if (pressedCell >= 0 && !dragging && Math.abs(x - pressX) + Math.abs(y - pressY) >= 6) dragging = true;
    let hover = -1;
    if (vertical) for (let i = 0; i < kStripCommandCount; i++) if (contains(commandRect(i), x, y)) hover = i;
    if (hover !== hoverCommand) { hoverCommand = hover; paint(); }
  });
  const cancelPress = () => { pressedCommand = -1; pressedCell = -1; dragging = false; };
  canvas.addEventListener("pointercancel", cancelPress);
  canvas.addEventListener("lostpointercapture", cancelPress);
  canvas.addEventListener("pointerup", (e) => {
    if (e.button !== 0) return;
    const [x, y] = local(e);
    const command = pressedCommand, cellIndex = pressedCell, dragged = dragging;
    cancelPress();
    if (command >= 0) { trigger(FRAME_COMMANDS[command]); return; }
    if (cellIndex < 0 || cellIndex >= cells.length) return;
    const cell = cells[cellIndex];
    if (dragged) {
      // dropTargetAt answers in pre-removal slots; the model moves by take+insert.
      let target = dropTargetAt(x, y);
      if (cell.frame < target) target--;
      if (target !== cell.frame) trigger("MoveFrame", cell.frame, target);
      return;
    }
    if (!cell.hold && state.onionAvailable && contains(cell.lane, x, y)) {
      trigger("LaneToggled", cell.frame, !state.lanes.has(cell.frame));
      return;
    }
    trigger("SelectFrame", cell.frame);
  });
  canvas.addEventListener("pointerleave", () => { if (hoverCommand !== -1) { hoverCommand = -1; paint(); } });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    // Qt scrolls angleDelta/2, 60px a notch; a browser notch reports 100.
    const delta = (e.deltaY !== 0 ? e.deltaY : e.deltaX) * (e.deltaMode === 1 ? 20 : 0.6);
    scroll = Math.trunc(scroll + delta);
    relayout();
    paint();
  }, { passive: false });

  qtTooltip(canvas, (e) => {
    if (!state || !e) return "";
    const [x, y] = local(e);
    if (vertical) for (let i = 0; i < kStripCommandCount; i++) if (contains(commandRect(i), x, y)) return tooltipFor(FRAME_COMMANDS[i], state);
    const index = cellAt(x, y);
    if (index < 0) return "";
    const cell = cells[index];
    if (cell.hold) return `Held: shows the same drawing as frame ${nameFor(cell.frame - 1)}. Editing either one changes both.`;
    if (state.onionAvailable && contains(cell.lane, x, y)) {
      return state.lanes.has(cell.frame) ? `Remove frame ${nameFor(cell.frame)} from the onion skin` : `Ghost frame ${nameFor(cell.frame)} under the current one`;
    }
    return `Frame ${nameFor(cell.frame)}`;
  });

  // Read synchronously as well as on resize: the first update has to know the
  // viewport, or the playhead would not be scrolled into it.
  function measure() {
    const nextWidth = el.clientWidth, nextHeight = el.clientHeight;
    dpr = window.devicePixelRatio || 1;
    if (nextWidth === width && nextHeight === height) return;
    width = nextWidth; height = nextHeight;
    canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
    canvas.style.width = `${width}px`; canvas.style.height = `${height}px`;
  }
  new ResizeObserver(() => { measure(); relayout(); paint(); }).observe(el);
  onThemeChange(paint);

  return {
    el,
    setVertical(next) {
      if (vertical === next) return;
      vertical = next;
      scroll = 0;
    },
    setState(next) {
      // Followed only when it moves, or any report would drag a scrolled sheet back.
      const moved = next.current !== state?.current;
      state = next;
      measure();
      // A URL no frame uses any more is not worth keeping decoded.
      const used = new Set(state.frames.map((f) => f.thumbnail));
      for (const key of thumbs.keys()) if (!used.has(key)) thumbs.delete(key);
      relayout();
      if (moved && !hBar.held() && !vBar.held()) ensureCurrentVisible();
      paint();
    },
  };
}

// ---- the timeline ---------------------------------------------------------------
// Playback reports every frame, and a button lifted out of the page under a held
// mouse loses its click: children move only when the arrangement really changed.
export function arrange(root, wanted) {
  const placed = [...root.children];
  if (wanted.length !== placed.length || wanted.some((node, i) => node !== placed[i])) root.replaceChildren(...wanted);
}

// TimelineWindow::applyDockExtent: collapsing hands the strip's height to the board.
// Only on the mode change: at any other moment the size is what the user dragged it
// to. A dock beside the timeline shares its height, so it is left alone then.
function targetDockArea(value) {
  if (value?.dockArea === "left" || value?.dockArea === "right" || value?.dockArea === "bottom") return value.dockArea;
  if (value?.vertical !== undefined) return value.vertical ? (value.leftAligned ? "left" : "right") : "bottom";
  return null;
}

export function watchTimelineDock(runtime, store) {
  const panel = store.timeline;
  if (!panel) return;
  let shrunkTo = 0, expanded = 0, shrunkArea = "";
  const findArea = (areas) => ["bottom", "top", "left", "right"].find((name) => areas[name]?.groups.some((g) => g.tabs.includes("timelineDock")));
  const fit = () => {
    const layout = runtime.getDockLayout();
    const isFloating = Boolean(layout.floating?.some((f) => f.dockId === "timelineDock")) || Boolean(runtime.externalDocks?.has?.("timelineDock"));
    let area = findArea(layout.areas);
    const value = store.get("timeline");
    const targetArea = targetDockArea(value);
    if (shrunkTo && (value?.floating || (targetArea && targetArea !== shrunkArea) || (area && area !== shrunkArea))) {
      runtime.dockResizeArea(shrunkArea, expanded);
      shrunkTo = 0;
      shrunkArea = "";
    }
    if (value?.floating) {
      if (!isFloating) {
        if (targetArea && area && area !== targetArea) runtime.dockMoveToArea?.("timelineDock", targetArea);
        runtime.dockFloat?.("timelineDock");
      }
      return;
    }
    if (isFloating && value) {
      if (runtime.dockRestore) runtime.dockRestore("timelineDock", targetArea ?? undefined);
      else if (targetArea) runtime.dockMoveToArea?.("timelineDock", targetArea);
    } else if (targetArea && area && area !== targetArea) {
      runtime.dockMoveToArea?.("timelineDock", targetArea);
    }
    const { areas } = runtime.getDockLayout();
    area = findArea(areas);
    if (!area) return;
    const vertical = area === "left" || area === "right";
    const extent = panel.extent();
    const size = vertical ? (extent.width ?? kCollapsedColumnWidth) : kDockTitleHeight + extent.height;

    if (!value?.collapsed || value.floating) {
      if (shrunkTo) runtime.dockResizeArea(area, Math.max(size, expanded));
      shrunkTo = 0;
      shrunkArea = "";
      return;
    }
    if (size === shrunkTo && area === shrunkArea) return;
    const beside = areas[area].groups.some((g) => g.tabs.some((id) => id !== "timelineDock" && runtime.view(id).visible));
    if (beside && !shrunkTo) return;
    if (!shrunkTo) expanded = areas[area].size ?? 0;
    shrunkTo = size;
    shrunkArea = area;
    runtime.dockResizeArea(area, size);

  };
  store.on("timeline", fit);
  // A narrow window wraps the bar onto a second row.
  new ResizeObserver(fit).observe(panel.bar);
  return (dockId) => {
    if (dockId !== "timelineDock") return;
    const layout = runtime.getDockLayout();
    const floating = Boolean(layout.floating?.some((f) => f.dockId === "timelineDock")) || Boolean(runtime.externalDocks?.has?.("timelineDock"));
    const value = store.get("timeline");
    if (floating) {
      if (!value?.floating) store.call?.("timeline.setFloating", { floating: true });
      return;
    }
    let area = findArea(layout.areas);
    if (area === "top") {
      runtime.dockMoveToArea?.("timelineDock", "bottom");
      area = "bottom";
    }
    const prevArea = targetDockArea(value) ?? "bottom";
    if (area && area !== prevArea) {
      store.call?.("timeline.setDockArea", { dockArea: area, floating: false });
    } else if (value?.floating) {
      store.call?.("timeline.setFloating", { floating: false });
    }
  };
}

function normalise(s) {
  return {
    frames: s.frames ?? [],
    current: s.current ?? 0,
    fps: s.fps ?? 12,
    playing: !!s.playing,
    loop: s.loop ?? true,
    onion: !!s.onion,
    onionLines: s.onionLines ?? true,
    onionFills: s.onionFills ?? true,
    onionAmLayers: s.onionAmLayers ?? true,
    guideLines: !!s.guideLines,
    onionAvailable: s.onionAvailable ?? true,
    lanes: new Set(s.lanes ?? []),
    collapsed: !!s.collapsed,
    floating: !!s.floating,
    // A floating timeline is horizontal whichever side it came from.
    vertical: !!s.vertical && !s.floating,
    leftAligned: !!s.leftAligned,
  };
}

// createTimeline(root, handlers, { bottomChrome })
//   root: the dock's box; fill it. See extent() for what size Qt would give it.
//   bottomChrome: where the transport goes while the strip is docked to a side
//   (PaintViewContainer::setBottomChrome). Without it the bar stays in root.
// handlers: on<TimelineCommand>, onRate(fps), onSelectFrame(frame),
//   onMoveFrame(from, to), onFrameTyped(frame), onLaneToggled(frame, on). The toggle
//   commands receive the value they ask for. All are optional.
export function createTimeline(root, handlers = {}, options = {}) {
  const { bottomChrome = null } = options;
  root.classList.add("tl-root");
  let state = normalise({});

  function trigger(command, ...args) {
    switch (command) {
      case "Onion": args = [!state.onion]; break;
      case "OnionLines": args = [!state.onionLines]; break;
      case "OnionFills": args = [!state.onionFills]; break;
      case "OnionAmLayers": args = [!state.onionAmLayers]; break;
      case "GuideLines": args = [!state.guideLines]; break;
      case "Collapse": args = [!state.collapsed]; break;
      case "Float": args = [!state.floating]; break;
      case "Orientation": args = [!state.vertical]; break;
      case "Loop":
        // The one flag the window flips itself before reporting it.
        state.loop = !state.loop;
        args = [state.loop];
        bar.setState(state);
        break;
      case "FrameTyped":
        args = [Math.min(Math.max(0, args[0]), Math.max(0, state.frames.length - 1))];
        break;
    }
    handlers[`on${command}`]?.(...args);
  }

  const bar = createBar(trigger);
  const side = createSideTitle(trigger);
  const strip = createStrip(trigger);

  function home() {
    const stripVisible = !(state.collapsed && !state.floating);
    root.classList.toggle("floating", state.floating);
    const wanted = [];
    if (state.vertical) {
      wanted.push(side);
      if (bottomChrome) { if (bar.el.parentElement !== bottomChrome) bottomChrome.append(bar.el); }
    } else {
      wanted.push(bar.el);
    }
    if (stripVisible) wanted.push(strip.el);
    if (state.vertical && !bottomChrome) wanted.push(bar.el);
    arrange(root, wanted);
  }

  return {
    update(next) {
      state = normalise(next ?? {});
      strip.setVertical(state.vertical);
      home();
      bar.setState(state);
      strip.setState(state);
    },
    // The transport, for a shell that wants to place it itself.
    bar: bar.el,
    // The size applyDockExtent asks for in this mode: { width } docked left or
    // right, { height } docked at the bottom.
    extent() {
      if (state.vertical) return { width: kCollapsedColumnWidth };
      const strip = state.collapsed && !state.floating ? 0 : kKeyCellHeight + kScrollThickness;
      return { height: bar.height() + strip };
    },
    minimum: { thickness: kMinStripThickness, column: kMinStripColumn },
  };
}
