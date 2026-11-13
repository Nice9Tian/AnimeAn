// Board effects: the short animation a tool plays when its result lands.
//
// Which effect, when and how long is Python's (pyfile/board_effects.py); the
// backend moves its geometry onto the board (`board.effect`,
// backend/session/backendsession.cpp) and sends it as a one-off `event`. This
// file only draws it: when the effect arrives the board still shows the
// picture before the change, so that picture is kept, and once the next frame
// has landed underneath, the kept picture is laid over it and taken away bit
// by bit - spreading from a point, along a line, or fading out. The renderers
// are untouched; the effect is a canvas of its own above the board's.
//
// Geometry arrives in board CSS px. Paths are the flat format of the `S` op:
// [0, x, y, 1, x, y, 2, c1x, c1y, c2x, c2y, x, y, 3].

export interface BoardEffectSpec {
  view?: string;
  kind?: string;
  duration?: number;
  center?: [number, number];
  radius?: number;
  clip?: number[];
  path?: number[];
  width?: number;
  margin?: number;
}

export interface EffectCanvasLike {
  width: number;
  height: number;
  clientWidth?: number;
  clientHeight?: number;
}

// The board waits this long for the frame that carries the change. Python asks
// for an effect only when the board will change, at the end of its work, so
// the frame follows at once; past the wait the effect is dropped (the frame
// never came), and a later, unrelated frame cannot start it.
export const FRAME_WAIT_MS = 1000;

const clamp01 = (v: number) => (v < 0 ? 0 : v > 1 ? 1 : v);

export function easeInOutCubic(t: number): number {
  const v = clamp01(t);
  return v < 0.5 ? 4 * v * v * v : 1 - Math.pow(-2 * v + 2, 3) / 2;
}

// How far along the effect is, 0..1. A line is drawn at an even pace, the way
// a hand draws it; a spread and a fade ease in and out.
export function effectProgress(kind: string, elapsed: number, duration: number): number {
  const t = duration > 0 ? clamp01(elapsed / duration) : 1;
  return kind === "stroke" ? t : easeInOutCubic(t);
}

// Walks a flat path, handing each segment to `seg` as line or cubic.
function walk(
  flat: ReadonlyArray<number>,
  seg: (kind: "move" | "line" | "cubic" | "close", pts: number[]) => void,
): void {
  let i = 0;
  while (i < flat.length) {
    const op = flat[i];
    const n = op === 0 || op === 1 ? 2 : op === 2 ? 6 : op === 3 ? 0 : -1;
    if (n < 0 || i + n >= flat.length) return;
    const pts = flat.slice(i + 1, i + 1 + n) as number[];
    if (pts.some((v) => !Number.isFinite(v))) return;
    seg(op === 0 ? "move" : op === 1 ? "line" : op === 2 ? "cubic" : "close", pts);
    i += 1 + n;
  }
}

// Length of a flat path; curves by 16 chords each, which is plenty for a
// reveal (the dash only has to end near the right place).
export function flatPathLength(flat: ReadonlyArray<number>): number {
  let x = 0;
  let y = 0;
  let sx = 0;
  let sy = 0;
  let total = 0;
  walk(flat, (kind, p) => {
    if (kind === "move") {
      x = sx = p[0];
      y = sy = p[1];
    } else if (kind === "line") {
      total += Math.hypot(p[0] - x, p[1] - y);
      x = p[0];
      y = p[1];
    } else if (kind === "cubic") {
      let px = x;
      let py = y;
      for (let k = 1; k <= 16; k++) {
        const t = k / 16;
        const u = 1 - t;
        const qx = u * u * u * x + 3 * u * u * t * p[0] + 3 * u * t * t * p[2] + t * t * t * p[4];
        const qy = u * u * u * y + 3 * u * u * t * p[1] + 3 * u * t * t * p[3] + t * t * t * p[5];
        total += Math.hypot(qx - px, qy - py);
        px = qx;
        py = qy;
      }
      x = p[4];
      y = p[5];
    } else {
      total += Math.hypot(sx - x, sy - y);
      x = sx;
      y = sy;
    }
  });
  return total;
}

export interface PathSink {
  moveTo(x: number, y: number): void;
  lineTo(x: number, y: number): void;
  bezierCurveTo(a: number, b: number, c: number, d: number, e: number, f: number): void;
  closePath(): void;
}

export function traceFlatPath(sink: PathSink, flat: ReadonlyArray<number>): void {
  walk(flat, (kind, p) => {
    if (kind === "move") sink.moveTo(p[0], p[1]);
    else if (kind === "line") sink.lineTo(p[0], p[1]);
    else if (kind === "cubic") sink.bezierCurveTo(p[0], p[1], p[2], p[3], p[4], p[5]);
    else sink.closePath();
  });
}

// Whether a spec carries what its kind needs to draw anything at all.
export function effectDrawable(spec: BoardEffectSpec): boolean {
  if (spec.kind === "radial") {
    return Array.isArray(spec.center) && Number.isFinite(spec.center[0]) && Number.isFinite(spec.center[1])
      && Number.isFinite(spec.radius) && (spec.radius ?? 0) > 0;
  }
  if (spec.kind === "stroke") {
    return Array.isArray(spec.path) && spec.path.length >= 6 && Number.isFinite(spec.width) && flatPathLength(spec.path) > 0;
  }
  return spec.kind === "fade";
}

type Ctx2D = CanvasRenderingContext2D;

function makePath(flat: ReadonlyArray<number>): Path2D {
  const path = new Path2D();
  traceFlatPath(path, flat);
  return path;
}

// Lays the kept picture over the board where the change is not uncovered yet,
// at progress `p` (0 = all of it kept, 1 = none). The context is already
// scaled to CSS px; `snapshot` is the board's own pixels, drawn to w x h.
export function paintEffect(
  ctx: Ctx2D,
  spec: BoardEffectSpec,
  snapshot: CanvasImageSource,
  p: number,
  w: number,
  h: number,
  paths: { clip?: Path2D; path?: Path2D; length?: number },
): void {
  ctx.save();
  ctx.clearRect(0, 0, w, h);
  if (p >= 1) {
    ctx.restore();
    return;
  }
  if (spec.kind === "fade") {
    if (paths.clip) ctx.clip(paths.clip, "evenodd");
    ctx.globalAlpha = 1 - p;
    ctx.drawImage(snapshot, 0, 0, w, h);
    ctx.restore();
    return;
  }
  // The area the change may touch, as a mask...
  ctx.fillStyle = "#000";
  ctx.strokeStyle = "#000";
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  if (spec.kind === "radial") {
    if (paths.clip) {
      ctx.fill(paths.clip, "evenodd");
      // The region's antialiased rim belongs to it too.
      ctx.lineWidth = 2;
      ctx.stroke(paths.clip);
    } else {
      ctx.fillRect(0, 0, w, h);
    }
  } else if (paths.path) {
    ctx.lineWidth = (spec.width ?? 1) + 2 * (spec.margin ?? 0);
    ctx.stroke(paths.path);
  }
  // ...less what is already uncovered...
  ctx.globalCompositeOperation = "destination-out";
  if (spec.kind === "radial" && spec.center && p > 0) {
    ctx.beginPath();
    ctx.arc(spec.center[0], spec.center[1], Math.max(0, (spec.radius ?? 0) * p), 0, Math.PI * 2);
    ctx.fill();
  } else if (spec.kind === "stroke" && paths.path && p > 0) {
    const length = paths.length ?? 0;
    ctx.lineWidth = (spec.width ?? 1) + 2 * (spec.margin ?? 0) + 2;
    ctx.setLineDash([length * p, length + ctx.lineWidth * 2]);
    ctx.stroke(paths.path);
    ctx.setLineDash([]);
  }
  // ...filled with the picture from before.
  ctx.globalCompositeOperation = "source-in";
  ctx.drawImage(snapshot, 0, 0, w, h);
  ctx.restore();
}

export interface BoardEffectsOptions {
  // The board's canvas right now (a renderer switch replaces it).
  canvas: () => (HTMLCanvasElement & EffectCanvasLike) | null;
  // The board size of the frame on show. The effect's geometry is in these
  // pixels; without it the canvas element's own size is taken, which is the
  // same thing unless the board follows another page (board/fit.ts).
  frameSize?: () => { w: number; h: number } | null | undefined;
  now?: () => number;
  raf?: (fn: (t: number) => void) => number;
  cancelRaf?: (id: number) => void;
  reducedMotion?: () => boolean;
}

// One per board. `play` on the effect, `frame` after each board frame is
// drawn, `stop` when the hand comes back (press, wheel, resize).
export class BoardEffects {
  private opts: BoardEffectsOptions;
  private overlay: HTMLCanvasElement | null = null;
  private snapshot: HTMLCanvasElement | null = null;
  private spec: BoardEffectSpec | null = null;
  private paths: { clip?: Path2D; path?: Path2D; length?: number } = {};
  private armedAt = 0;
  private startedAt = -1;
  private rafId = 0;
  private sizeKey = "";
  private now: () => number;
  private raf: (fn: (t: number) => void) => number;
  private cancelRaf: (id: number) => void;
  framesSeen = 0;

  constructor(opts: BoardEffectsOptions) {
    this.opts = opts;
    this.now = opts.now ?? (() => performance.now());
    this.raf = opts.raf ?? ((fn) => requestAnimationFrame(fn));
    this.cancelRaf = opts.cancelRaf ?? ((id) => cancelAnimationFrame(id));
  }

  get active(): boolean {
    return this.spec !== null;
  }

  play(spec: BoardEffectSpec): void {
    this.stop();
    if (!effectDrawable(spec)) return;
    if (this.opts.reducedMotion?.()) return;
    // Nothing on the board yet (a page just connected): there is no "before".
    if (this.framesSeen === 0) return;
    const board = this.opts.canvas();
    if (!board || !board.width || !board.height) return;
    if (typeof board.getClientRects === "function" && board.getClientRects().length === 0) return;
    const { w, h } = this.boardSize(board);
    const snapshot = document.createElement("canvas");
    snapshot.width = board.width;
    snapshot.height = board.height;
    const sctx = snapshot.getContext("2d");
    if (!sctx) return;
    sctx.drawImage(board, 0, 0);
    this.snapshot = snapshot;
    this.spec = spec;
    this.paths = {
      clip: spec.clip?.length ? makePath(spec.clip) : undefined,
      path: spec.path?.length ? makePath(spec.path) : undefined,
      length: spec.path?.length ? flatPathLength(spec.path) : undefined,
    };
    this.sizeKey = `${board.width}x${board.height}`;
    this.armedAt = this.now();
    this.startedAt = -1;
    const overlay = this.ensureOverlay(board);
    overlay.width = board.width;
    overlay.height = board.height;
    // Laid over at once: the frame with the change may be drawn before the
    // next animation frame, and must not flash through.
    this.paint(0, w, h);
    this.schedule();
  }

  // After the board has drawn a frame. The first one after `play` carries the
  // change, so the clock starts there.
  frame(): void {
    this.framesSeen++;
    if (!this.spec) return;
    const board = this.opts.canvas();
    if (!board || `${board.width}x${board.height}` !== this.sizeKey) {
      this.stop();
      return;
    }
    if (this.startedAt < 0) this.startedAt = this.now();
  }

  stop(): void {
    if (this.rafId) this.cancelRaf(this.rafId);
    this.rafId = 0;
    this.spec = null;
    this.snapshot = null;
    this.paths = {};
    this.startedAt = -1;
    if (this.overlay) {
      const ctx = this.overlay.getContext("2d");
      ctx?.setTransform(1, 0, 0, 1, 0, 0);
      ctx?.clearRect(0, 0, this.overlay.width, this.overlay.height);
    }
  }

  destroy(): void {
    this.stop();
    this.overlay?.remove();
    this.overlay = null;
  }

  private ensureOverlay(board: HTMLCanvasElement): HTMLCanvasElement {
    let overlay = this.overlay;
    if (!overlay) {
      overlay = document.createElement("canvas");
      overlay.className = "board-effects";
      this.overlay = overlay;
    }
    // Right above the board's canvas, whichever canvas that is now.
    if (overlay.previousSibling !== board) board.after(overlay);
    // Laid out as the board is: scaled to fit when the board is (shell.css).
    overlay.classList?.toggle("board-mirror", Boolean(board.classList?.contains("board-mirror")));
    return overlay;
  }

  private boardSize(board: HTMLCanvasElement & EffectCanvasLike): { w: number; h: number } {
    const frame = this.opts.frameSize?.();
    if (frame && frame.w > 0 && frame.h > 0) return { w: frame.w, h: frame.h };
    return { w: board.clientWidth || board.width, h: board.clientHeight || board.height };
  }

  private schedule(): void {
    this.rafId = this.raf(() => this.tick());
  }

  private tick(): void {
    this.rafId = 0;
    const spec = this.spec;
    const board = this.opts.canvas();
    if (!spec || !board) return;
    const now = this.now();
    if (this.startedAt < 0) {
      if (now - this.armedAt > FRAME_WAIT_MS) {
        this.stop();
        return;
      }
      this.schedule();
      return;
    }
    const elapsed = now - this.startedAt;
    const duration = Math.max(0, Number(spec.duration) || 0);
    const p = effectProgress(spec.kind ?? "", elapsed, duration);
    const { w, h } = this.boardSize(board);
    this.paint(p, w, h);
    if (elapsed >= duration) {
      this.stop();
      return;
    }
    this.schedule();
  }

  private paint(p: number, w: number, h: number): void {
    const overlay = this.overlay;
    const spec = this.spec;
    if (!overlay || !spec || !this.snapshot) return;
    const ctx = overlay.getContext("2d");
    if (!ctx) return;
    const sx = w > 0 ? overlay.width / w : 1;
    const sy = h > 0 ? overlay.height / h : 1;
    ctx.setTransform(sx, 0, 0, sy, 0, 0);
    paintEffect(ctx, spec, this.snapshot, p, w, h, this.paths);
  }
}
