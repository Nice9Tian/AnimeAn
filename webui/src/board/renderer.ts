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
  style: number;
  rgba: string;
  width: number;
  cap: CanvasLineCap;
  join: CanvasLineJoin;
  miter: number;
  cosmetic: boolean;
  dash: number[];
  offset: number;
}

type GradientStop = [number, string];

type BrushGradientSpec =
  | {
      kind: "L";
      x1: number;
      y1: number;
      x2: number;
      y2: number;
      stops: GradientStop[];
    }
  | {
      kind: "R";
      cx: number;
      cy: number;
      r: number;
      fx: number;
      fy: number;
      fr: number;
      stops: GradientStop[];
    };

interface BrushState {
  style: number;
  rgba: string;
  gradient: BrushGradientSpec | null;
}

interface ClipSpec {
  op: number;
  rule: CanvasFillRule;
  cmds: number[];
  pts: number[];
}

interface DrawState {
  transform: [number, number, number, number, number, number];
  pen: PenState;
  brush: BrushState;
  opacity: number;
  composite: GlobalCompositeOperation;
  clips: ClipSpec[];
}

function defaultDrawState(): DrawState {
  return {
    transform: [1, 0, 0, 1, 0, 0],
    pen: {
      style: 1,
      rgba: "#000000ff",
      width: 1,
      cap: "butt",
      join: "miter",
      miter: 2,
      cosmetic: false,
      dash: [],
      offset: 0,
    },
    brush: {
      style: 0,
      rgba: "#00000000",
      gradient: null,
    },
    opacity: 1,
    composite: "source-over",
    clips: [],
  };
}

function cloneDrawState(s: DrawState): DrawState {
  return {
    transform: [...s.transform] as [number, number, number, number, number, number],
    pen: {
      ...s.pen,
      dash: [...s.pen.dash],
    },
    brush: {
      style: s.brush.style,
      rgba: s.brush.rgba,
      gradient: s.brush.gradient
        ? s.brush.gradient.kind === "L"
          ? { ...s.brush.gradient, stops: s.brush.gradient.stops.map((st) => [st[0], st[1]]) }
          : { ...s.brush.gradient, stops: s.brush.gradient.stops.map((st) => [st[0], st[1]]) }
        : null,
    },
    opacity: s.opacity,
    composite: s.composite,
    clips: s.clips.map((c) => ({
      op: c.op,
      rule: c.rule,
      cmds: [...c.cmds],
      pts: [...c.pts],
    })),
  };
}

function mapLineCap(cap: unknown): CanvasLineCap {
  if (cap === "round" || cap === 32 || cap === 0x20 || cap === 2) return "round";
  if (cap === "square" || cap === 16 || cap === 0x10 || cap === 1) return "square";
  return "butt";
}

function mapLineJoin(join: unknown): CanvasLineJoin {
  if (join === "round" || join === 128 || join === 0x80 || join === 2) return "round";
  if (join === "bevel" || join === 64 || join === 0x40 || join === 1) return "bevel";
  return "miter";
}

function mapCompositeMode(mode: unknown): GlobalCompositeOperation {
  if (typeof mode === "string") return mode as GlobalCompositeOperation;
  switch (Number(mode)) {
    case 0:
      return "source-over";
    case 1:
      return "destination-over";
    case 3:
      return "copy";
    case 5:
      return "source-in";
    case 6:
      return "destination-in";
    case 7:
      return "source-out";
    case 8:
      return "destination-out";
    case 9:
      return "source-atop";
    case 10:
      return "destination-atop";
    case 11:
      return "xor";
    case 12:
      return "lighter";
    case 13:
      return "multiply";
    case 14:
      return "screen";
    case 15:
      return "overlay";
    case 16:
      return "darken";
    case 17:
      return "lighten";
    case 18:
      return "color-dodge";
    case 19:
      return "color-burn";
    case 20:
      return "hard-light";
    case 21:
      return "soft-light";
    case 22:
      return "difference";
    case 23:
      return "exclusion";
    default:
      return "source-over";
  }
}

function defaultDashForStyle(style: number): number[] {
  switch (style) {
    case 2: // Qt::DashLine
      return [4, 2];
    case 3: // Qt::DotLine
      return [1, 2];
    case 4: // Qt::DashDotLine
      return [4, 2, 1, 2];
    case 5: // Qt::DashDotDotLine
      return [4, 2, 1, 2, 1, 2];
    default:
      return [];
  }
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
  // 无 DOM/OffscreenCanvas 环境下的轻量虚拟画布
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
      if (this.canvas.style) {
        this.canvas.style.width = `${w}px`;
        this.canvas.style.height = `${h}px`;
      }
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
      // 全量帧：直接在主画布重放，并同步更新前缀缓存画布供后续 keep > 0 的帧复用
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
      // 增量帧：确保 cacheCanvas 持有 0..keep 的稳定前缀，主画布先贴缓存再只重放后缀 keep..fullOps.length
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

    this.client?.sendAck(msg.view, msg.frame);
    this.onAck?.(msg.view, msg.frame);
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
    ctx.strokeStyle = pen.rgba;
    ctx.lineWidth = effWidth;
    ctx.lineCap = pen.cap;
    ctx.lineJoin = pen.join;
    ctx.miterLimit = pen.miter;
    const rawDash = pen.dash.length > 0 ? pen.dash : defaultDashForStyle(pen.style);
    if (pen.style === 1 || rawDash.length === 0) {
      ctx.setLineDash([]);
      ctx.lineDashOffset = 0;
    } else {
      ctx.setLineDash(rawDash.map((d) => d * effWidth));
      ctx.lineDashOffset = pen.offset * effWidth;
    }
  }

  private applyBrushToContext(ctx: Canvas2DContextLike, brush: BrushState): void {
    if (brush.gradient) {
      const g = brush.gradient;
      if (g.kind === "L") {
        const grad = ctx.createLinearGradient(g.x1, g.y1, g.x2, g.y2);
        for (const [pos, color] of g.stops) {
          grad.addColorStop(pos, color);
        }
        ctx.fillStyle = grad;
      } else {
        const grad = ctx.createRadialGradient(g.fx, g.fy, g.fr, g.cx, g.cy, g.r);
        for (const [pos, color] of g.stops) {
          grad.addColorStop(pos, color);
        }
        ctx.fillStyle = grad;
      }
    } else {
      ctx.fillStyle = brush.rgba;
    }
  }

  private applyFullState(ctx: Canvas2DContextLike, state: DrawState, dpr: number): void {
    ctx.restore();
    ctx.save();
    this.applyTransform(ctx, state.transform, dpr);
    ctx.globalAlpha = state.opacity;
    ctx.globalCompositeOperation = state.composite;
    this.applyPenToContext(ctx, state.pen);
    this.applyBrushToContext(ctx, state.brush);
    for (const clip of state.clips) {
      this.tracePath(ctx, clip.cmds, clip.pts);
      ctx.clip(clip.rule);
    }
  }

  private tracePath(
    ctx: Canvas2DContextLike,
    cmds: number[],
    pts: number[],
    transform?: [number, number, number, number, number, number],
  ): void {
    ctx.beginPath();
    let pi = 0;
    const mapX = (x: number, y: number): number =>
      transform ? transform[0] * x + transform[2] * y + transform[4] : x;
    const mapY = (x: number, y: number): number =>
      transform ? transform[1] * x + transform[3] * y + transform[5] : y;

    for (let ci = 0; ci < cmds.length; ci++) {
      const cmd = cmds[ci];
      if (cmd === 0) {
        const x = Number(pts[pi++] ?? 0);
        const y = Number(pts[pi++] ?? 0);
        ctx.moveTo(mapX(x, y), mapY(x, y));
      } else if (cmd === 1) {
        const x = Number(pts[pi++] ?? 0);
        const y = Number(pts[pi++] ?? 0);
        ctx.lineTo(mapX(x, y), mapY(x, y));
      } else if (cmd === 2) {
        const c1x = Number(pts[pi++] ?? 0);
        const c1y = Number(pts[pi++] ?? 0);
        const c2x = Number(pts[pi++] ?? 0);
        const c2y = Number(pts[pi++] ?? 0);
        const ex = Number(pts[pi++] ?? 0);
        const ey = Number(pts[pi++] ?? 0);
        ctx.bezierCurveTo(
          mapX(c1x, c1y),
          mapY(c1x, c1y),
          mapX(c2x, c2y),
          mapY(c2x, c2y),
          mapX(ex, ey),
          mapY(ex, ey),
        );
        if (cmds[ci + 1] === 3) ci++;
        if (cmds[ci + 1] === 3) ci++;
      } else if (cmd === 3) {
        // 已经随 CurveToElement(2) 一并消费
      } else if (cmd === 4) {
        ctx.closePath();
      }
    }
  }

  private strokeWithPen(
    ctx: Canvas2DContextLike,
    state: DrawState,
    dpr: number,
    traceInTransformedSpace: (t?: [number, number, number, number, number, number]) => void,
  ): void {
    if (state.pen.style === 0) return;
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
          const style = Number(op[1] ?? 1);
          const rgba = String(op[2] ?? "#000000ff");
          const rawWidth = Number(op[3] ?? 1);
          const cap = mapLineCap(op[4]);
          const join = mapLineJoin(op[5]);
          const miter = Number(op[6] ?? 2);
          const cosmetic = Boolean(op[7]) || rawWidth === 0;
          const dash = Array.isArray(op[8]) ? (op[8] as unknown[]).map((n) => Number(n) || 0) : [];
          const offset = Number(op[9] ?? 0);
          state.pen = {
            style,
            rgba,
            width: rawWidth > 0 ? rawWidth : 1,
            cap,
            join,
            miter,
            cosmetic,
            dash,
            offset,
          };
          this.applyPenToContext(ctx, state.pen);
          break;
        }

        case "B": {
          const style = Number(op[1] ?? 0);
          const rgba = String(op[2] ?? "#00000000");
          state.brush = {
            style,
            rgba,
            gradient: style === 15 || style === 16 || style === 17 ? state.brush.gradient : null,
          };
          this.applyBrushToContext(ctx, state.brush);
          break;
        }

        case "G": {
          const kind = String(op[1] ?? "L");
          if (kind === "L") {
            const x1 = Number(op[2] ?? 0);
            const y1 = Number(op[3] ?? 0);
            const x2 = Number(op[4] ?? 0);
            const y2 = Number(op[5] ?? 0);
            const rawStops = Array.isArray(op[6]) ? (op[6] as unknown[]) : [];
            const stops: GradientStop[] = rawStops.map((st) => {
              const pair = Array.isArray(st) ? st : [0, "#000000ff"];
              return [Number(pair[0] ?? 0), String(pair[1] ?? "#000000ff")];
            });
            state.brush = {
              style: 15,
              rgba: state.brush.rgba,
              gradient: { kind: "L", x1, y1, x2, y2, stops },
            };
          } else if (kind === "R") {
            const cx = Number(op[2] ?? 0);
            const cy = Number(op[3] ?? 0);
            const r = Number(op[4] ?? 0);
            const fx = Number(op[5] ?? cx);
            const fy = Number(op[6] ?? cy);
            const fr = Number(op[7] ?? 0);
            const rawStops = Array.isArray(op[8]) ? (op[8] as unknown[]) : [];
            const stops: GradientStop[] = rawStops.map((st) => {
              const pair = Array.isArray(st) ? st : [0, "#000000ff"];
              return [Number(pair[0] ?? 0), String(pair[1] ?? "#000000ff")];
            });
            state.brush = {
              style: 16,
              rgba: state.brush.rgba,
              gradient: { kind: "R", cx, cy, r, fx, fy, fr, stops },
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
          const clipOp = Number(op[1] ?? 0);
          if (clipOp === 0) {
            state.clips = [];
            this.applyFullState(ctx, state, dpr);
          } else {
            const rule: CanvasFillRule = Number(op[2] ?? 0) === 0 ? "evenodd" : "nonzero";
            const cmds = Array.isArray(op[3]) ? (op[3] as unknown[]).map((n) => Number(n) || 0) : [];
            const pts = Array.isArray(op[4]) ? (op[4] as unknown[]).map((n) => Number(n) || 0) : [];
            if (clipOp === 1) {
              state.clips = [{ op: 1, rule, cmds, pts }];
              this.applyFullState(ctx, state, dpr);
            } else {
              state.clips.push({ op: 2, rule, cmds, pts });
              this.tracePath(ctx, cmds, pts);
              ctx.clip(rule);
            }
          }
          break;
        }

        case "R": {
          const x = Number(op[1] ?? 0);
          const y = Number(op[2] ?? 0);
          const w = Number(op[3] ?? 0);
          const h = Number(op[4] ?? 0);
          if (state.brush.style !== 0) {
            ctx.fillRect(x, y, w, h);
          }
          if (state.pen.style !== 0) {
            if (!state.pen.cosmetic) {
              ctx.strokeRect(x, y, w, h);
            } else {
              this.strokeWithPen(ctx, state, dpr, (t) => {
                this.tracePath(
                  ctx,
                  [0, 1, 1, 1, 4],
                  [x, y, x + w, y, x + w, y + h, x, y + h],
                  t,
                );
              });
            }
          }
          break;
        }

        case "S": {
          if (state.pen.style === 0) break;
          const coords: number[] = [];
          for (let k = 1; k < op.length; k++) {
            coords.push(Number(op[k] ?? 0));
          }
          this.strokeWithPen(ctx, state, dpr, (t) => {
            ctx.beginPath();
            const mapX = (px: number, py: number): number =>
              t ? t[0] * px + t[2] * py + t[4] : px;
            const mapY = (px: number, py: number): number =>
              t ? t[1] * px + t[3] * py + t[5] : py;
            for (let k = 0; k + 3 < coords.length; k += 4) {
              const x1 = coords[k];
              const y1 = coords[k + 1];
              const x2 = coords[k + 2];
              const y2 = coords[k + 3];
              ctx.moveTo(mapX(x1, y1), mapY(x1, y1));
              ctx.lineTo(mapX(x2, y2), mapY(x2, y2));
            }
          });
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
          const rule: CanvasFillRule = Number(op[1] ?? 0) === 0 ? "evenodd" : "nonzero";
          const cmds = Array.isArray(op[2]) ? (op[2] as unknown[]).map((n) => Number(n) || 0) : [];
          const pts = Array.isArray(op[3]) ? (op[3] as unknown[]).map((n) => Number(n) || 0) : [];
          if (state.brush.style !== 0) {
            this.tracePath(ctx, cmds, pts);
            ctx.fill(rule);
          }
          if (state.pen.style !== 0) {
            this.strokeWithPen(ctx, state, dpr, (t) => {
              this.tracePath(ctx, cmds, pts, t);
            });
          }
          break;
        }
      }
    }
  }
}
