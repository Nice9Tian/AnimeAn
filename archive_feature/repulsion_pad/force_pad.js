// Extracted from webui/src/ui/panels2.js when Repulsion Pad was archived.
// Imports assume placement back inside webui/src/ui/ (or re-merging into panels2.js).
import { tabBar } from "./panels.js";
import { onThemeChange, cosmeticStroke, flattenCubic, hintedTracking } from "./timeline.js";

const TAB_WIDTH = { "Repulsion Pad": 97 };
const kHandleRadius = 9, kMargin = 14, kTextBand = 20, kMoveThrottleMs = 16;
const kAccent = "rgb(61,142,201)";
const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}

function singleTab(root, title) {
  const tabs = tabBar([title], () => {}, TAB_WIDTH);
  tabs.select(0);
  root.append(tabs.el);
}

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
  // A drag cut short would strand the listener in an open session; a centred
  // release is the explicit cancel.
  const cancel = () => {
    if (!dragging) return;
    dragging = false;
    value = { x: 0, y: 0 };
    onRelease?.(0, 0);
    paint();
  };
  canvas.addEventListener("pointercancel", cancel);
  canvas.addEventListener("lostpointercapture", cancel);

  function measure() {
    const dpr = window.devicePixelRatio || 1;
    const wasShown = width > 0;
    width = page.clientWidth; height = page.clientHeight;
    canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
    canvas.style.width = `${width}px`; canvas.style.height = `${height}px`;
    if (wasShown && !width) cancel();
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
