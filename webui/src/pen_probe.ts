export interface ProbeSampleInput {
  x?: number;
  y?: number;
  timeMs: number;
  pressure?: number;
  tiltX?: number;
  tiltY?: number;
}

export interface ProbeSample {
  x: number;
  y: number;
  timeMs: number;
  pressure: number;
  tiltX: number;
  tiltY: number;
}

export interface StrokeStats {
  index: number;
  pointerType: string;
  sampleCount: number;
  durationMs: number;
  avgHz: number;
  medianHz: number;
  pressureMin: number;
  pressureMax: number;
  tiltMax: number;
  cancelled: boolean;
}

export interface BucketSummary {
  pointerType: string;
  strokeCount: number;
  sampleCount: number;
  avgSamplesPerStroke: number;
  durationMs: number;
  avgHz: number;
  medianHz: number;
  pressureMin: number;
  pressureMax: number;
  tiltMax: number;
  cancelCount: number;
  cancelled: boolean;
  recentStrokes: StrokeStats[];
}

export interface ProbeReport {
  probe: "web" | "qt";
  recentLimit: number;
  summary: {
    pen: BucketSummary;
    mouse: BucketSummary;
  };
  recentStrokes: StrokeStats[];
  strokes: StrokeStats[];
}

export interface PointerEventLike {
  pointerId?: number;
  pointerType?: string;
  type?: string;
  clientX?: number;
  clientY?: number;
  offsetX?: number;
  offsetY?: number;
  pressure?: number;
  buttons?: number;
  tiltX?: number;
  tiltY?: number;
  timeStamp?: number;
  getCoalescedEvents?: () => PointerEventLike[];
  preventDefault?: () => void;
}

export function round2(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.round(value * 100) / 100;
}

export function round4(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.round(value * 10000) / 10000;
}

export function computeMedian(values: readonly number[]): number {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  if (sorted.length % 2 === 1) {
    return sorted[mid];
  }
  return (sorted[mid - 1] + sorted[mid]) / 2;
}

export interface ComputeStrokeOptions {
  index?: number;
  pointerType?: string;
  cancelled?: boolean;
}

export function computeStrokeStats(
  samples: readonly ProbeSampleInput[],
  options: ComputeStrokeOptions = {},
): StrokeStats {
  const index = options.index ?? 1;
  const pointerType = options.pointerType || "pen";
  const cancelled = Boolean(options.cancelled);
  const sampleCount = samples.length;

  if (sampleCount === 0) {
    return {
      index,
      pointerType,
      sampleCount: 0,
      durationMs: 0,
      avgHz: 0,
      medianHz: 0,
      pressureMin: 0,
      pressureMax: 0,
      tiltMax: 0,
      cancelled,
    };
  }

  const firstTime = Number(samples[0].timeMs) || 0;
  const lastTime = Number(samples[sampleCount - 1].timeMs) || 0;
  const rawDuration = sampleCount >= 2 ? Math.max(0, lastTime - firstTime) : 0;
  const durationMs = round2(rawDuration);
  const avgHz =
    sampleCount >= 2 && rawDuration > 0
      ? round2(((sampleCount - 1) * 1000) / rawDuration)
      : 0;

  const instantRates: number[] = [];
  for (let i = 1; i < sampleCount; i++) {
    const t0 = Number(samples[i - 1].timeMs) || 0;
    const t1 = Number(samples[i].timeMs) || 0;
    const dt = t1 - t0;
    if (dt > 0) {
      instantRates.push(1000 / dt);
    }
  }
  const medianHz = instantRates.length > 0 ? round2(computeMedian(instantRates)) : 0;

  let pMin = Infinity;
  let pMax = -Infinity;
  let tMax = 0;

  for (const s of samples) {
    const p = typeof s.pressure === "number" && Number.isFinite(s.pressure) ? s.pressure : 0;
    if (p < pMin) pMin = p;
    if (p > pMax) pMax = p;

    const tx = typeof s.tiltX === "number" && Number.isFinite(s.tiltX) ? s.tiltX : 0;
    const ty = typeof s.tiltY === "number" && Number.isFinite(s.tiltY) ? s.tiltY : 0;
    const tilt = Math.hypot(tx, ty);
    if (tilt > tMax) tMax = tilt;
  }

  return {
    index,
    pointerType,
    sampleCount,
    durationMs,
    avgHz,
    medianHz,
    pressureMin: pMin === Infinity ? 0 : round4(pMin),
    pressureMax: pMax === -Infinity ? 0 : round4(pMax),
    tiltMax: round2(tMax),
    cancelled,
  };
}

export function summarizeBucket(
  strokes: readonly StrokeStats[],
  pointerType: string,
  recentLimit = 10,
): BucketSummary {
  const matching = strokes.filter((s) => s.pointerType === pointerType);
  const strokeCount = matching.length;
  if (strokeCount === 0) {
    return {
      pointerType,
      strokeCount: 0,
      sampleCount: 0,
      avgSamplesPerStroke: 0,
      durationMs: 0,
      avgHz: 0,
      medianHz: 0,
      pressureMin: 0,
      pressureMax: 0,
      tiltMax: 0,
      cancelCount: 0,
      cancelled: false,
      recentStrokes: [],
    };
  }

  let sampleCount = 0;
  let totalIntervals = 0;
  let totalDuration = 0;
  let pMin = Infinity;
  let pMax = -Infinity;
  let tMax = 0;
  let cancelCount = 0;
  const medians: number[] = [];

  for (const s of matching) {
    sampleCount += s.sampleCount;
    totalIntervals += Math.max(0, s.sampleCount - 1);
    totalDuration += s.durationMs;
    if (s.sampleCount > 0) {
      if (s.pressureMin < pMin) pMin = s.pressureMin;
      if (s.pressureMax > pMax) pMax = s.pressureMax;
      if (s.tiltMax > tMax) tMax = s.tiltMax;
    }
    if (s.medianHz > 0) {
      medians.push(s.medianHz);
    }
    if (s.cancelled) {
      cancelCount += 1;
    }
  }

  const durationMs = round2(totalDuration);
  const avgHz = totalDuration > 0 ? round2((totalIntervals * 1000) / totalDuration) : 0;
  const medianHz = medians.length > 0 ? round2(computeMedian(medians)) : 0;

  return {
    pointerType,
    strokeCount,
    sampleCount,
    avgSamplesPerStroke: round2(sampleCount / strokeCount),
    durationMs,
    avgHz,
    medianHz,
    pressureMin: pMin === Infinity ? 0 : round4(pMin),
    pressureMax: pMax === -Infinity ? 0 : round4(pMax),
    tiltMax: round2(tMax),
    cancelCount,
    cancelled: cancelCount > 0,
    recentStrokes: matching.slice(-recentLimit),
  };
}

export function extractPointerSamples(
  event: PointerEventLike,
  rect?: { left: number; top: number },
): ProbeSample[] {
  const coalesced =
    typeof event.getCoalescedEvents === "function" ? event.getCoalescedEvents() : [];
  const rawList = Array.isArray(coalesced) && coalesced.length > 0 ? coalesced : [event];
  const left = rect ? rect.left : 0;
  const top = rect ? rect.top : 0;

  const out: ProbeSample[] = [];
  for (const ev of rawList) {
    const x =
      typeof ev.clientX === "number"
        ? ev.clientX - left
        : typeof event.clientX === "number"
          ? event.clientX - left
          : Number(ev.offsetX ?? event.offsetX ?? 0);
    const y =
      typeof ev.clientY === "number"
        ? ev.clientY - top
        : typeof event.clientY === "number"
          ? event.clientY - top
          : Number(ev.offsetY ?? event.offsetY ?? 0);
    const timeMs =
      typeof ev.timeStamp === "number"
        ? ev.timeStamp
        : typeof event.timeStamp === "number"
          ? event.timeStamp
          : 0;
    const pressure =
      typeof ev.pressure === "number"
        ? ev.pressure
        : typeof event.pressure === "number"
          ? event.pressure
          : 0;
    const tiltX =
      typeof ev.tiltX === "number"
        ? ev.tiltX
        : typeof event.tiltX === "number"
          ? event.tiltX
          : 0;
    const tiltY =
      typeof ev.tiltY === "number"
        ? ev.tiltY
        : typeof event.tiltY === "number"
          ? event.tiltY
          : 0;
    out.push({ x, y, timeMs, pressure, tiltX, tiltY });
  }
  return out;
}

export class PenProbeSession {
  private strokes: StrokeStats[] = [];

  recordStroke(
    samples: readonly ProbeSampleInput[],
    options: { pointerType?: string; cancelled?: boolean } = {},
  ): StrokeStats {
    const stats = computeStrokeStats(samples, {
      index: this.strokes.length + 1,
      pointerType: options.pointerType || "pen",
      cancelled: options.cancelled,
    });
    this.strokes.push(stats);
    return stats;
  }

  getStrokes(): StrokeStats[] {
    return [...this.strokes];
  }

  getReport(recentLimit = 10): ProbeReport {
    return {
      probe: "web",
      recentLimit,
      summary: {
        pen: summarizeBucket(this.strokes, "pen", recentLimit),
        mouse: summarizeBucket(this.strokes, "mouse", recentLimit),
      },
      recentStrokes: this.strokes.slice(-recentLimit),
      strokes: [...this.strokes],
    };
  }

  toJSON(recentLimit = 10): string {
    return JSON.stringify(this.getReport(recentLimit), null, 2);
  }

  reset(): void {
    this.strokes = [];
  }
}

export function formatStrokeLine(s: StrokeStats): string {
  return (
    `#${s.index} [${s.pointerType}] ` +
    `samples=${s.sampleCount} ` +
    `dur=${s.durationMs.toFixed(1)}ms ` +
    `avg=${s.avgHz.toFixed(1)}Hz ` +
    `med=${s.medianHz.toFixed(1)}Hz ` +
    `p=${s.pressureMin.toFixed(3)}..${s.pressureMax.toFixed(3)} ` +
    `tiltMax=${s.tiltMax.toFixed(1)}° ` +
    `cancel=${s.cancelled ? "YES" : "no"}`
  );
}

export function formatBucketLine(label: string, b: BucketSummary): string {
  return (
    `${label}(${b.strokeCount}笔): ` +
    `samples=${b.sampleCount} (avg=${b.avgSamplesPerStroke.toFixed(1)}/笔) ` +
    `dur=${b.durationMs.toFixed(1)}ms ` +
    `avg=${b.avgHz.toFixed(1)}Hz ` +
    `med=${b.medianHz.toFixed(1)}Hz ` +
    `p=${b.pressureMin.toFixed(3)}..${b.pressureMax.toFixed(3)} ` +
    `tiltMax=${b.tiltMax.toFixed(1)}° ` +
    `cancel=${b.cancelCount}`
  );
}

export function formatReadoutLine(report: ProbeReport): string {
  const last =
    report.strokes.length > 0 ? report.strokes[report.strokes.length - 1] : null;
  if (!last) {
    return "等待落笔（pen / mouse 分开统计）…";
  }
  return (
    `最新 ${formatStrokeLine(last)} | ` +
    `${formatBucketLine("笔合计", report.summary.pen)} | ` +
    `${formatBucketLine("鼠合计", report.summary.mouse)}`
  );
}

export function formatDetailsText(report: ProbeReport): string {
  const lines: string[] = [];
  lines.push(formatBucketLine("合计[pen]", report.summary.pen));
  for (const s of report.summary.pen.recentStrokes) {
    lines.push("  " + formatStrokeLine(s));
  }
  lines.push(formatBucketLine("合计[mouse]", report.summary.mouse));
  for (const s of report.summary.mouse.recentStrokes) {
    lines.push("  " + formatStrokeLine(s));
  }
  return lines.join("\n");
}

export interface PenProbeControllerOptions {
  session?: PenProbeSession;
  onUpdate?: (report: ProbeReport) => void;
}

export interface PenProbeController {
  session: PenProbeSession;
  clear(): void;
  destroy(): void;
}

export function attachPenProbe(
  canvas: HTMLElement,
  options: PenProbeControllerOptions = {},
): PenProbeController {
  const session = options.session ?? new PenProbeSession();

  if (canvas.style) {
    canvas.style.touchAction = "none";
    canvas.style.userSelect = "none";
  }

  let ctx: CanvasRenderingContext2D | null = null;
  if ("getContext" in canvas && typeof (canvas as HTMLCanvasElement).getContext === "function") {
    ctx = (canvas as HTMLCanvasElement).getContext("2d");
  }

  const drawnStrokes: Array<{ pointerType: string; points: ProbeSample[] }> = [];
  let activeStroke: {
    pointerId: number;
    pointerType: string;
    samples: ProbeSample[];
    usedRawUpdate: boolean;
  } | null = null;

  const redrawAll = () => {
    if (!ctx) return;
    const htmlCanvas = canvas as HTMLCanvasElement;
    const dpr =
      typeof window !== "undefined" && window.devicePixelRatio ? window.devicePixelRatio : 1;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, htmlCanvas.width, htmlCanvas.height);
    ctx.scale(dpr, dpr);
    ctx.lineCap = "round";
    ctx.lineJoin = "round";

    const drawPoints = (pointerType: string, pts: readonly ProbeSample[]) => {
      if (!ctx || pts.length === 0) return;
      ctx.strokeStyle = pointerType === "pen" ? "#111111" : "#1f5aa6";
      ctx.fillStyle = ctx.strokeStyle;
      if (pts.length === 1) {
        const r = Math.max(1, (pts[0].pressure || 0.5) * 3);
        ctx.beginPath();
        ctx.arc(pts[0].x, pts[0].y, r, 0, Math.PI * 2);
        ctx.fill();
        return;
      }
      for (let i = 1; i < pts.length; i++) {
        const p0 = pts[i - 1];
        const p1 = pts[i];
        ctx.lineWidth = Math.max(1, (p1.pressure || 0.5) * 6);
        ctx.beginPath();
        ctx.moveTo(p0.x, p0.y);
        ctx.lineTo(p1.x, p1.y);
        ctx.stroke();
      }
    };

    for (const s of drawnStrokes) {
      drawPoints(s.pointerType, s.points);
    }
    if (activeStroke) {
      drawPoints(activeStroke.pointerType, activeStroke.samples);
    }
    ctx.restore();
  };

  const getRect = () => {
    if (typeof canvas.getBoundingClientRect === "function") {
      return canvas.getBoundingClientRect();
    }
    return { left: 0, top: 0 };
  };

  const appendNewSamples = (newSamples: readonly ProbeSample[]) => {
    if (!activeStroke || newSamples.length === 0) return;
    const prevLen = activeStroke.samples.length;
    activeStroke.samples.push(...newSamples);
    if (!ctx) return;

    const dpr =
      typeof window !== "undefined" && window.devicePixelRatio ? window.devicePixelRatio : 1;
    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.strokeStyle = activeStroke.pointerType === "pen" ? "#111111" : "#1f5aa6";
    ctx.fillStyle = ctx.strokeStyle;

    if (prevLen === 0 && activeStroke.samples.length === 1) {
      const p = activeStroke.samples[0];
      const r = Math.max(1, (p.pressure || 0.5) * 3);
      ctx.beginPath();
      ctx.arc(p.x, p.y, r, 0, Math.PI * 2);
      ctx.fill();
    } else {
      const startIdx = Math.max(1, prevLen);
      for (let i = startIdx; i < activeStroke.samples.length; i++) {
        const p0 = activeStroke.samples[i - 1];
        const p1 = activeStroke.samples[i];
        ctx.lineWidth = Math.max(1, (p1.pressure || 0.5) * 6);
        ctx.beginPath();
        ctx.moveTo(p0.x, p0.y);
        ctx.lineTo(p1.x, p1.y);
        ctx.stroke();
      }
    }
    ctx.restore();
  };

  const onPointerDown = (rawEv: Event) => {
    const e = rawEv as unknown as PointerEventLike;
    if (activeStroke !== null) return;
    e.preventDefault?.();
    const pointerId = e.pointerId ?? 0;
    try {
      canvas.setPointerCapture?.(pointerId);
    } catch {
      // Ignore synthetic events without active pointerId
    }
    activeStroke = {
      pointerId,
      pointerType: e.pointerType || "mouse",
      samples: [],
      usedRawUpdate: false,
    };
    appendNewSamples(extractPointerSamples(e, getRect()));
  };

  const onPointerRawUpdate = (rawEv: Event) => {
    const e = rawEv as unknown as PointerEventLike;
    if (!activeStroke || (e.pointerId ?? 0) !== activeStroke.pointerId) return;
    activeStroke.usedRawUpdate = true;
    appendNewSamples(extractPointerSamples(e, getRect()));
  };

  const onPointerMove = (rawEv: Event) => {
    const e = rawEv as unknown as PointerEventLike;
    if (!activeStroke || (e.pointerId ?? 0) !== activeStroke.pointerId) return;
    if (activeStroke.usedRawUpdate) return;
    appendNewSamples(extractPointerSamples(e, getRect()));
  };

  const finishStroke = (rawEv: Event, cancelled: boolean) => {
    const e = rawEv as unknown as PointerEventLike;
    if (!activeStroke || (e.pointerId ?? 0) !== activeStroke.pointerId) return;
    const pointerId = activeStroke.pointerId;
    try {
      if (canvas.hasPointerCapture?.(pointerId)) {
        canvas.releasePointerCapture?.(pointerId);
      }
    } catch {
      // Ignore
    }
    const completed = activeStroke;
    activeStroke = null;
    drawnStrokes.push({
      pointerType: completed.pointerType,
      points: completed.samples,
    });
    session.recordStroke(completed.samples, {
      pointerType: completed.pointerType,
      cancelled,
    });
    options.onUpdate?.(session.getReport(10));
  };

  const onPointerUp = (e: Event) => finishStroke(e, false);
  const onPointerCancel = (e: Event) => finishStroke(e, true);
  const onContextMenu = (e: Event) => {
    e.preventDefault?.();
  };

  canvas.addEventListener("pointerdown", onPointerDown);
  canvas.addEventListener("pointerrawupdate", onPointerRawUpdate);
  canvas.addEventListener("pointermove", onPointerMove);
  canvas.addEventListener("pointerup", onPointerUp);
  canvas.addEventListener("pointercancel", onPointerCancel);
  canvas.addEventListener("contextmenu", onContextMenu);

  return {
    session,
    clear() {
      activeStroke = null;
      drawnStrokes.length = 0;
      session.reset();
      redrawAll();
      options.onUpdate?.(session.getReport(10));
    },
    destroy() {
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("pointerrawupdate", onPointerRawUpdate);
      canvas.removeEventListener("pointermove", onPointerMove);
      canvas.removeEventListener("pointerup", onPointerUp);
      canvas.removeEventListener("pointercancel", onPointerCancel);
      canvas.removeEventListener("contextmenu", onContextMenu);
    },
  };
}

export async function copyProbeReportJson(
  session: PenProbeSession,
  clipboard: { writeText?: (text: string) => Promise<void> } | null | undefined,
  fallbackTextarea?: HTMLTextAreaElement | null,
  statusEl?: HTMLElement | null,
): Promise<boolean> {
  const json = session.toJSON(10);
  if (clipboard && typeof clipboard.writeText === "function") {
    try {
      await clipboard.writeText(json);
      if (fallbackTextarea) {
        fallbackTextarea.value = json;
        fallbackTextarea.style.display = "none";
      }
      if (statusEl) {
        statusEl.textContent = "已复制 JSON 到剪贴板";
      }
      return true;
    } catch {
      // Fall back to textarea below
    }
  }
  if (fallbackTextarea) {
    fallbackTextarea.value = json;
    fallbackTextarea.style.display = "block";
    fallbackTextarea.focus?.();
    fallbackTextarea.select?.();
  }
  if (statusEl) {
    statusEl.textContent = "剪贴板写入失败，已显示在文本框中";
  }
  return false;
}

function initBrowserPage(): void {
  if (typeof document === "undefined" || typeof document.getElementById !== "function") return;
  const canvas = document.getElementById("probe-canvas") as HTMLCanvasElement | null;
  if (!canvas) return;

  const readoutEl = document.getElementById("probe-readout");
  const detailsEl = document.getElementById("probe-details");
  const copyBtn = document.getElementById("probe-copy-btn");
  const clearBtn = document.getElementById("probe-clear-btn");
  const statusEl = document.getElementById("probe-copy-status");
  const jsonOutputEl = document.getElementById(
    "probe-json-output",
  ) as HTMLTextAreaElement | null;

  window.addEventListener("contextmenu", (e) => e.preventDefault());

  const resizeCanvas = () => {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.floor(window.innerWidth * dpr));
    const h = Math.max(1, Math.floor(window.innerHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
  };
  resizeCanvas();

  const renderReport = (report: ProbeReport) => {
    if (readoutEl) {
      readoutEl.textContent = formatReadoutLine(report);
    }
    if (detailsEl) {
      detailsEl.textContent = formatDetailsText(report);
    }
    if (jsonOutputEl && jsonOutputEl.style.display === "block") {
      jsonOutputEl.value = JSON.stringify(report, null, 2);
    }
  };

  const controller = attachPenProbe(canvas, {
    onUpdate: renderReport,
  });

  window.addEventListener("resize", () => {
    resizeCanvas();
  });

  renderReport(controller.session.getReport(10));

  copyBtn?.addEventListener("click", () => {
    void copyProbeReportJson(
      controller.session,
      typeof navigator !== "undefined" ? navigator.clipboard : null,
      jsonOutputEl,
      statusEl,
    );
  });

  clearBtn?.addEventListener("click", () => {
    controller.clear();
    if (statusEl) statusEl.textContent = "";
    if (jsonOutputEl) {
      jsonOutputEl.value = "";
      jsonOutputEl.style.display = "none";
    }
  });

  window.addEventListener("keydown", (e) => {
    if (e.key === "c" || e.key === "C") {
      if (e.ctrlKey || e.metaKey) return;
      void copyProbeReportJson(
        controller.session,
        typeof navigator !== "undefined" ? navigator.clipboard : null,
        jsonOutputEl,
        statusEl,
      );
    }
  });
}

initBrowserPage();
