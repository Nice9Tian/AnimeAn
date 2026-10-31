import type { BackendClient, BoardMessage, ImageMeta } from "../net.ts";

export interface Canvas2DContextLike {
  save(): void;
  restore(): void;
  setTransform(a: number, b: number, c: number, d: number, e: number, f: number): void;
  clearRect(x: number, y: number, w: number, h: number): void;
  fillRect(x: number, y: number, w: number, h: number): void;
  strokeRect(x: number, y: number, w: number, h: number): void;
  beginPath(): void;
  moveTo(x: number, y: number): void;
  lineTo(x: number, y: number): void;
  bezierCurveTo(cp1x: number, cp1y: number, cp2x: number, cp2y: number, x: number, y: number): void;
  rect(x: number, y: number, w: number, h: number): void;
  closePath(): void;
  fill(fillRule?: CanvasFillRule): void;
  stroke(): void;
  clip(fillRule?: CanvasFillRule): void;
  setLineDash(segments: number[]): void;
  createLinearGradient(x0: number, y0: number, x1: number, y1: number): CanvasGradient;
  createRadialGradient(x0: number, y0: number, r0: number, x1: number, y1: number, r1: number): CanvasGradient;
  drawImage(
    image: unknown,
    sx: number,
    sy: number,
    sw?: number,
    sh?: number,
    dx?: number,
    dy?: number,
    dw?: number,
    dh?: number,
  ): void;
  putImageData?(imagedata: ImageData, dx: number, dy: number): void;
  fillStyle: string | CanvasGradient | CanvasPattern;
  strokeStyle: string | CanvasGradient | CanvasPattern;
  lineWidth: number;
  lineCap: CanvasLineCap;
  lineJoin: CanvasLineJoin;
  miterLimit: number;
  lineDashOffset: number;
  globalAlpha: number;
  globalCompositeOperation: GlobalCompositeOperation;
  imageSmoothingEnabled?: boolean;
}

export interface CanvasLike {
  width: number;
  height: number;
  style?: { width?: string; height?: string };
  getContext(contextId: "2d"): Canvas2DContextLike | null;
}

export interface BoardRendererOptions {
  view?: string;
  client?: BackendClient;
  getDpr?: () => number;
  createCacheCanvas?: (width: number, height: number) => CanvasLike;
  onAck?: (view: string, frame: number) => void;
}

interface PenState {
  r: number;
  g: number;
  b: number;
  a: number;
  color: string;
  width: number;
  cap: CanvasLineCap;
  join: CanvasLineJoin;
  miter: number;
  cosmetic: boolean;
  dash: number[] | null;
}

type GradientStop = [number, string];

type BrushGradientSpec =
  | {
      kind: "linear";
      x1: number;
      y1: number;
      x2: number;
      y2: number;
      stops: GradientStop[];
    }
  | {
      kind: "radial";
      fx: number;
      fy: number;
      fr: number;
      cx: number;
      cy: number;
      cr: number;
      stops: GradientStop[];
    };

interface BrushState {
  r: number;
  g: number;
  b: number;
  a: number;
  color: string;
  gradient: BrushGradientSpec | null;
}

interface ClipSpec {
  path: number[];
  rule: CanvasFillRule;
  transform: [number, number, number, number, number, number];
}

interface DrawState {
  transform: [number, number, number, number, number, number];
  pen: PenState;
  brush: BrushState;
  opacity: number;
  composite: GlobalCompositeOperation;
  clip: ClipSpec | null;
  smooth: boolean;
}

function clampByte(n: unknown): number {
  const v = Math.round(Number(n) || 0);
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

export function formatRgbaColor(r: number, g: number, b: number, a: number): string {
  if (a >= 255) {
    return `rgb(${r},${g},${b})`;
  }
  if (a <= 0) {
    return "rgba(0,0,0,0)";
  }
  return `rgba(${r},${g},${b},${a / 255})`;
}

function defaultDrawState(): DrawState {
  return {
    transform: [1, 0, 0, 1, 0, 0],
    pen: {
      r: 0,
      g: 0,
      b: 0,
      a: 255,
      color: "rgb(0,0,0)",
      width: 1,
      cap: "butt",
      join: "miter",
      miter: 2,
      cosmetic: false,
      dash: null,
    },
    brush: {
      r: 0,
      g: 0,
      b: 0,
      a: 0,
      color: "rgba(0,0,0,0)",
      gradient: null,
    },
    opacity: 1,
    composite: "source-over",
    clip: null,
    smooth: false,
  };
}

function cloneDrawState(s: DrawState): DrawState {
  return {
    transform: [...s.transform] as [number, number, number, number, number, number],
    pen: {
      ...s.pen,
      dash: s.pen.dash ? [...s.pen.dash] : null,
    },
    brush: {
      ...s.brush,
      gradient: s.brush.gradient
        ? s.brush.gradient.kind === "linear"
          ? { ...s.brush.gradient, stops: s.brush.gradient.stops.map((st) => [st[0], st[1]]) }
          : { ...s.brush.gradient, stops: s.brush.gradient.stops.map((st) => [st[0], st[1]]) }
        : null,
    },
    opacity: s.opacity,
    composite: s.composite,
    clip: s.clip
      ? {
          path: [...s.clip.path],
          rule: s.clip.rule,
          transform: [...s.clip.transform] as [number, number, number, number, number, number],
        }
      : null,
    smooth: s.smooth,
  };
}

function mapLineCap(cap: unknown): CanvasLineCap {
  const n = Number(cap);
  if (n === 1 || cap === "square") return "square";
  if (n === 2 || cap === "round") return "round";
  return "butt";
}

function mapLineJoin(join: unknown): CanvasLineJoin {
  const n = Number(join);
  if (n === 1 || join === "bevel") return "bevel";
  if (n === 2 || join === "round") return "round";
  return "miter";
}

function mapCompositeMode(mode: unknown): GlobalCompositeOperation {
  if (typeof mode === "string" && mode.length > 0) {
    if (mode === "source") return "copy";
    return mode as GlobalCompositeOperation;
  }
  return "source-over";
}

function parseGradientStops(rawStops: unknown): GradientStop[] {
  if (!Array.isArray(rawStops)) return [];
  return rawStops.map((st) => {
    if (!Array.isArray(st)) return [0, "rgba(0,0,0,0)"];
    const pos = Math.max(0, Math.min(1, Number(st[0] ?? 0)));
    if (typeof st[1] === "string") {
      return [pos, st[1]];
    }
    const r = clampByte(st[1]);
    const g = clampByte(st[2]);
    const b = clampByte(st[3]);
    const a = st.length > 4 ? clampByte(st[4]) : 255;
    return [pos, formatRgbaColor(r, g, b, a)];
  });
}

function createDefaultCacheCanvas(width: number, height: number): CanvasLike {
  if (typeof OffscreenCanvas !== "undefined") {
    return new OffscreenCanvas(width, height) as unknown as CanvasLike;
  }
  if (typeof document !== "undefined") {
    const c = document.createElement("canvas");
    c.width = width;
    c.height = height;
    return c as unknown as CanvasLike;
  }
  const noopCtx: Canvas2DContextLike = {
    save() {},
    restore() {},
    setTransform() {},
    clearRect() {},
    fillRect() {},
    strokeRect() {},
    beginPath() {},
    moveTo() {},
    lineTo() {},
    bezierCurveTo() {},
    rect() {},
    closePath() {},
    fill() {},
    stroke() {},
    clip() {},
    setLineDash() {},
    createLinearGradient() {
      return { addColorStop() {} } as unknown as CanvasGradient;
    },
    createRadialGradient() {
      return { addColorStop() {} } as unknown as CanvasGradient;
    },
    drawImage() {},
    fillStyle: "#000000",
    strokeStyle: "#000000",
    lineWidth: 1,
    lineCap: "butt",
    lineJoin: "miter",
    miterLimit: 2,
    lineDashOffset: 0,
    globalAlpha: 1,
    globalCompositeOperation: "source-over",
    imageSmoothingEnabled: false,
  };
  return {
    width,
    height,
    getContext: () => noopCtx,
  };
}

export class BoardRenderer {
  private canvas: CanvasLike;
  private ctx: Canvas2DContextLike;
  private view: string;
  private client?: BackendClient;
  private getDpr: () => number;
  private createCacheCanvas: (width: number, height: number) => CanvasLike;
  private onAck?: (view: string, frame: number) => void;

  private fullOps: unknown[][] = [];
  private cacheCanvas: CanvasLike | null = null;
  private cacheCtx: Canvas2DContextLike | null = null;
  private cachedCount = 0;
  private cachedState: DrawState = defaultDrawState();
  private logicalW = 0;
  private logicalH = 0;
  private currentDpr = 1;

  private images = new Map<string, unknown>();
  private unsubBoard?: () => void;
  private unsubImage?: () => void;

  constructor(canvas: CanvasLike, options: BoardRendererOptions = {}) {
    const ctx = canvas.getContext("2d");
    if (!ctx) {
      throw new Error("无法获取 Canvas 2D 上下文");
    }
    this.canvas = canvas;
    this.ctx = ctx;
    this.view = options.view ?? "main";
    this.client = options.client;
    this.getDpr =
      options.getDpr ?? (() => (typeof window !== "undefined" && window.devicePixelRatio ? window.devicePixelRatio : 1));
    this.createCacheCanvas = options.createCacheCanvas ?? createDefaultCacheCanvas;
    this.onAck = options.onAck;

    if (this.client) {
      this.unsubImage = this.client.onImage((meta, data) => {
        this.putImage(meta, data);
      });
      this.unsubBoard = this.client.onBoard(this.view, (msg) => {
        this.renderFrame(msg);
      });
    }
  }

  getOps(): ReadonlyArray<unknown[]> {
    return this.fullOps;
  }

  getImage(key: string): unknown {
    return this.images.get(key);
  }

  putImage(meta: ImageMeta, data: Uint8Array): void {
    const w = Math.max(1, Math.round(meta.w));
    const h = Math.max(1, Math.round(meta.h));
    if (typeof ImageData !== "undefined" && (typeof OffscreenCanvas !== "undefined" || typeof document !== "undefined")) {
      const holder = this.createCacheCanvas(w, h);
      holder.width = w;
      holder.height = h;
      const hCtx = holder.getContext("2d");
      if (hCtx && typeof hCtx.putImageData === "function") {
        const clamped = new Uint8ClampedArray(w * h * 4);
        clamped.set(data.subarray(0, Math.min(data.length, w * h * 4)));
        const imgData = new ImageData(clamped, w, h);
        hCtx.putImageData(imgData, 0, 0);
        this.images.set(meta.key, holder);
        return;
      }
    }
    this.images.set(meta.key, { key: meta.key, width: w, height: h, data });
  }

  renderFrame(msg: BoardMessage): void {
    try {
      this.drawFrame(msg);
    } finally {
      // Without this ack the backend stops sending frames for this board.
      this.client?.sendAck(msg.view, msg.frame);
      this.onAck?.(msg.view, msg.frame);
    }
  }

  private drawFrame(msg: BoardMessage): void {
    const dpr = Math.max(0.1, Number(this.getDpr()) || 1);
    const w = Math.max(1, Math.round(msg.w));
    const h = Math.max(1, Math.round(msg.h));
    const pw = Math.max(1, Math.round(w * dpr));
    const ph = Math.max(1, Math.round(h * dpr));

    const sizeChanged = w !== this.logicalW || h !== this.logicalH || dpr !== this.currentDpr;
    if (sizeChanged) {
      this.logicalW = w;
      this.logicalH = h;
      this.currentDpr = dpr;
      this.canvas.width = pw;
      this.canvas.height = ph;
      this.cacheCanvas = this.createCacheCanvas(pw, ph);
      this.cacheCanvas.width = pw;
      this.cacheCanvas.height = ph;
      this.cacheCtx = this.cacheCanvas.getContext("2d");
      this.cachedCount = 0;
      this.cachedState = defaultDrawState();
    } else if (!this.cacheCanvas || !this.cacheCtx) {
      this.cacheCanvas = this.createCacheCanvas(pw, ph);
      this.cacheCanvas.width = pw;
      this.cacheCanvas.height = ph;
      this.cacheCtx = this.cacheCanvas.getContext("2d");
      this.cachedCount = 0;
      this.cachedState = defaultDrawState();
    }

    const keep = Math.max(0, Math.min(Math.round(msg.keep ?? 0), this.fullOps.length));
    const incomingOps = Array.isArray(msg.ops) ? msg.ops : [];
    this.fullOps = this.fullOps.slice(0, keep).concat(incomingOps);

    if (keep === 0) {
      const mainState = defaultDrawState();
      this.resetContext(this.ctx, pw, ph, dpr, mainState);
      this.executeOps(this.ctx, this.fullOps, 0, this.fullOps.length, mainState, dpr);

      if (this.cacheCtx) {
        const cState = defaultDrawState();
        this.resetContext(this.cacheCtx, pw, ph, dpr, cState);
        this.executeOps(this.cacheCtx, this.fullOps, 0, this.fullOps.length, cState, dpr);
        this.cachedCount = this.fullOps.length;
        this.cachedState = cloneDrawState(cState);
      }
    } else {
      if (this.cacheCtx && this.cacheCanvas) {
        if (this.cachedCount > keep) {
          const cState = defaultDrawState();
          this.resetContext(this.cacheCtx, pw, ph, dpr, cState);
          this.executeOps(this.cacheCtx, this.fullOps, 0, keep, cState, dpr);
          this.cachedCount = keep;
          this.cachedState = cloneDrawState(cState);
        } else if (this.cachedCount < keep) {
          const cState = cloneDrawState(this.cachedState);
          if (this.cachedCount === 0) {
            this.resetContext(this.cacheCtx, pw, ph, dpr, cState);
          } else {
            this.applyFullState(this.cacheCtx, cState, dpr);
          }
          this.executeOps(this.cacheCtx, this.fullOps, this.cachedCount, keep, cState, dpr);
          this.cachedCount = keep;
          this.cachedState = cloneDrawState(cState);
        }

        const suffixState = cloneDrawState(this.cachedState);
        this.ctx.restore();
        this.ctx.save();
        this.ctx.setTransform(1, 0, 0, 1, 0, 0);
        this.ctx.globalAlpha = 1;
        this.ctx.globalCompositeOperation = "source-over";
        this.ctx.clearRect(0, 0, pw, ph);
        this.ctx.drawImage(this.cacheCanvas, 0, 0);
        this.applyFullState(this.ctx, suffixState, dpr);
        this.executeOps(this.ctx, this.fullOps, keep, this.fullOps.length, suffixState, dpr);
      } else {
        const mainState = defaultDrawState();
        this.resetContext(this.ctx, pw, ph, dpr, mainState);
        this.executeOps(this.ctx, this.fullOps, 0, this.fullOps.length, mainState, dpr);
      }
    }
  }

  destroy(): void {
    this.unsubBoard?.();
    this.unsubImage?.();
    this.unsubBoard = undefined;
    this.unsubImage = undefined;
    this.images.clear();
    this.fullOps = [];
  }

  private resetContext(
    ctx: Canvas2DContextLike,
    pw: number,
    ph: number,
    dpr: number,
    state: DrawState,
  ): void {
    ctx.restore();
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    ctx.clearRect(0, 0, pw, ph);
    this.applyFullState(ctx, state, dpr);
  }

  private applyTransform(ctx: Canvas2DContextLike, t: [number, number, number, number, number, number], dpr: number): void {
    ctx.setTransform(t[0] * dpr, t[1] * dpr, t[2] * dpr, t[3] * dpr, t[4] * dpr, t[5] * dpr);
  }

  private applyPenToContext(ctx: Canvas2DContextLike, pen: PenState): void {
    const effWidth = pen.width > 0 ? pen.width : 1;
    ctx.strokeStyle = pen.color;
    ctx.lineWidth = effWidth;
    ctx.lineCap = pen.cap;
    ctx.lineJoin = pen.join;
    ctx.miterLimit = pen.miter;
    if (pen.dash && pen.dash.length > 0) {
      ctx.setLineDash(pen.dash.map((d) => d * effWidth));
    } else {
      ctx.setLineDash([]);
    }
    ctx.lineDashOffset = 0;
  }

  private applyBrushToContext(ctx: Canvas2DContextLike, brush: BrushState): void {
    if (brush.gradient) {
      const g = brush.gradient;
      if (g.kind === "linear") {
        const grad = ctx.createLinearGradient(g.x1, g.y1, g.x2, g.y2);
        for (const [pos, color] of g.stops) {
          grad.addColorStop(pos, color);
        }
        ctx.fillStyle = grad;
      } else {
        const grad = ctx.createRadialGradient(g.fx, g.fy, g.fr, g.cx, g.cy, g.cr);
        for (const [pos, color] of g.stops) {
          grad.addColorStop(pos, color);
        }
        ctx.fillStyle = grad;
      }
    } else {
      ctx.fillStyle = brush.color;
    }
  }

  private applyFullState(ctx: Canvas2DContextLike, state: DrawState, dpr: number): void {
    ctx.restore();
    ctx.save();
    if (state.clip) {
      this.applyTransform(ctx, state.clip.transform, dpr);
      this.tracePath(ctx, state.clip.path);
      ctx.clip(state.clip.rule);
    }
    this.applyTransform(ctx, state.transform, dpr);
    ctx.globalAlpha = state.opacity;
    ctx.globalCompositeOperation = state.composite;
    ctx.imageSmoothingEnabled = state.smooth;
    this.applyPenToContext(ctx, state.pen);
    this.applyBrushToContext(ctx, state.brush);
  }

  private tracePath(
    ctx: Canvas2DContextLike,
    path: number[],
    transform?: [number, number, number, number, number, number],
  ): void {
    ctx.beginPath();
    const mapX = (x: number, y: number): number =>
      transform ? transform[0] * x + transform[2] * y + transform[4] : x;
    const mapY = (x: number, y: number): number =>
      transform ? transform[1] * x + transform[3] * y + transform[5] : y;

    let i = 0;
    const len = path.length;
    while (i < len) {
      const cmd = path[i++];
      if (cmd === 0) {
        const x = Number(path[i++] ?? 0);
        const y = Number(path[i++] ?? 0);
        ctx.moveTo(mapX(x, y), mapY(x, y));
      } else if (cmd === 1) {
        const x = Number(path[i++] ?? 0);
        const y = Number(path[i++] ?? 0);
        ctx.lineTo(mapX(x, y), mapY(x, y));
      } else if (cmd === 2) {
        const c1x = Number(path[i++] ?? 0);
        const c1y = Number(path[i++] ?? 0);
        const c2x = Number(path[i++] ?? 0);
        const c2y = Number(path[i++] ?? 0);
        const ex = Number(path[i++] ?? 0);
        const ey = Number(path[i++] ?? 0);
        ctx.bezierCurveTo(
          mapX(c1x, c1y),
          mapY(c1x, c1y),
          mapX(c2x, c2y),
          mapY(c2x, c2y),
          mapX(ex, ey),
          mapY(ex, ey),
        );
      } else if (cmd === 3) {
        ctx.closePath();
      } else {
        break;
      }
    }
  }

  private strokeWithPen(
    ctx: Canvas2DContextLike,
    state: DrawState,
    dpr: number,
    traceInTransformedSpace: (t?: [number, number, number, number, number, number]) => void,
  ): void {
    if (state.pen.a <= 0) return;
    if (state.pen.cosmetic) {
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      traceInTransformedSpace(state.transform);
      ctx.stroke();
      this.applyTransform(ctx, state.transform, dpr);
    } else {
      traceInTransformedSpace(undefined);
      ctx.stroke();
    }
  }

  private executeOps(
    ctx: Canvas2DContextLike,
    ops: unknown[][],
    start: number,
    end: number,
    state: DrawState,
    dpr: number,
  ): void {
    for (let i = start; i < end; i++) {
      const op = ops[i];
      if (!Array.isArray(op) || op.length === 0) continue;
      const code = op[0];

      switch (code) {
        case "T": {
          state.transform = [
            Number(op[1] ?? 1),
            Number(op[2] ?? 0),
            Number(op[3] ?? 0),
            Number(op[4] ?? 1),
            Number(op[5] ?? 0),
            Number(op[6] ?? 0),
          ];
          this.applyTransform(ctx, state.transform, dpr);
          break;
        }

        case "P": {
          const r = clampByte(op[1]);
          const g = clampByte(op[2]);
          const b = clampByte(op[3]);
          const a = clampByte(op[4]);
          const rawWidth = Number(op[5] ?? 1);
          const cap = mapLineCap(op[6]);
          const join = mapLineJoin(op[7]);
          const miter = Number(op[8] ?? 2);
          const cosmetic = Boolean(op[9]);
          const dash = Array.isArray(op[10]) ? (op[10] as unknown[]).map((n) => Number(n) || 0) : null;
          state.pen = {
            r,
            g,
            b,
            a,
            color: formatRgbaColor(r, g, b, a),
            width: rawWidth > 0 ? rawWidth : 1,
            cap,
            join,
            miter,
            cosmetic,
            dash,
          };
          this.applyPenToContext(ctx, state.pen);
          break;
        }

        case "B": {
          const r = clampByte(op[1]);
          const g = clampByte(op[2]);
          const b = clampByte(op[3]);
          const a = clampByte(op[4]);
          state.brush = {
            r,
            g,
            b,
            a,
            color: formatRgbaColor(r, g, b, a),
            gradient: null,
          };
          this.applyBrushToContext(ctx, state.brush);
          break;
        }

        case "G": {
          const kind = String(op[1] ?? "linear");
          const coords = Array.isArray(op[2]) ? (op[2] as unknown[]).map((n) => Number(n) || 0) : [];
          const stops = parseGradientStops(op[3]);
          if (kind === "radial") {
            const fx = coords[0] ?? 0;
            const fy = coords[1] ?? 0;
            const fr = coords.length >= 6 ? (coords[2] ?? 0) : 0;
            const cx = coords.length >= 6 ? (coords[3] ?? fx) : fx;
            const cy = coords.length >= 6 ? (coords[4] ?? fy) : fy;
            const cr = coords.length >= 6 ? (coords[5] ?? 0) : (coords[2] ?? 0);
            state.brush = {
              r: 0,
              g: 0,
              b: 0,
              a: 255,
              color: state.brush.color,
              gradient: { kind: "radial", fx, fy, fr, cx, cy, cr, stops },
            };
          } else {
            const x1 = coords[0] ?? 0;
            const y1 = coords[1] ?? 0;
            const x2 = coords[2] ?? 0;
            const y2 = coords[3] ?? 0;
            state.brush = {
              r: 0,
              g: 0,
              b: 0,
              a: 255,
              color: state.brush.color,
              gradient: { kind: "linear", x1, y1, x2, y2, stops },
            };
          }
          this.applyBrushToContext(ctx, state.brush);
          break;
        }

        case "O": {
          state.opacity = Math.max(0, Math.min(1, Number(op[1] ?? 1)));
          ctx.globalAlpha = state.opacity;
          break;
        }

        case "M": {
          state.composite = mapCompositeMode(op[1]);
          ctx.globalCompositeOperation = state.composite;
          break;
        }

        case "K": {
          if (!Array.isArray(op[1])) {
            state.clip = null;
            this.applyFullState(ctx, state, dpr);
          } else {
            const path = (op[1] as unknown[]).map((n) => Number(n) || 0);
            const rule: CanvasFillRule = Number(op[2] ?? 0) === 0 ? "evenodd" : "nonzero";
            state.clip = {
              path,
              rule,
              transform: [...state.transform] as [number, number, number, number, number, number],
            };
            this.applyFullState(ctx, state, dpr);
          }
          break;
        }

        case "R": {
          const x = Number(op[1] ?? 0);
          const y = Number(op[2] ?? 0);
          const w = Number(op[3] ?? 0);
          const h = Number(op[4] ?? 0);
          if (state.brush.a > 0 || state.brush.gradient !== null) {
            ctx.fillRect(x, y, w, h);
          }
          if (state.pen.a > 0) {
            if (!state.pen.cosmetic) {
              ctx.strokeRect(x, y, w, h);
            } else {
              this.strokeWithPen(ctx, state, dpr, (t) => {
                this.tracePath(
                  ctx,
                  [0, x, y, 1, x + w, y, 1, x + w, y + h, 1, x, y + h, 3],
                  t,
                );
              });
            }
          }
          break;
        }

        case "S": {
          const path = Array.isArray(op[1]) ? (op[1] as unknown[]).map((n) => Number(n) || 0) : [];
          const rule: CanvasFillRule = Number(op[2] ?? 0) === 0 ? "evenodd" : "nonzero";
          const hasBrush = state.brush.a > 0 || state.brush.gradient !== null;
          const hasPen = state.pen.a > 0;
          if (hasBrush) {
            this.tracePath(ctx, path);
            ctx.fill(rule);
          }
          if (hasPen) {
            if (!state.pen.cosmetic) {
              if (!hasBrush) {
                this.tracePath(ctx, path);
              }
              ctx.stroke();
            } else {
              this.strokeWithPen(ctx, state, dpr, (t) => {
                this.tracePath(ctx, path, t);
              });
            }
          }
          break;
        }

        case "I": {
          const key = String(op[1] ?? "");
          const sx = Number(op[2] ?? 0);
          const sy = Number(op[3] ?? 0);
          const sw = Number(op[4] ?? 0);
          const sh = Number(op[5] ?? 0);
          const dx = Number(op[6] ?? 0);
          const dy = Number(op[7] ?? 0);
          const dw = Number(op[8] ?? 0);
          const dh = Number(op[9] ?? 0);
          const img = this.images.get(key);
          if (img) {
            ctx.drawImage(img, sx, sy, sw, sh, dx, dy, dw, dh);
          }
          break;
        }

        case "H": {
          state.smooth = Boolean(op[1]);
          ctx.imageSmoothingEnabled = state.smooth;
          break;
        }
      }
    }
  }
}
