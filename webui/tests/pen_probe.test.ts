import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  attachPenProbe,
  computeMedian,
  computeStrokeStats,
  copyProbeReportJson,
  extractPointerSamples,
  formatReadoutLine,
  PenProbeSession,
  summarizeBucket,
} from "../src/pen_probe.ts";

describe("Pen Probe statistics and event collection", () => {
  it("computes median for empty, odd, and even arrays", () => {
    assert.equal(computeMedian([]), 0);
    assert.equal(computeMedian([200]), 200);
    assert.equal(computeMedian([300, 100, 200]), 200);
    assert.equal(computeMedian([400, 100, 200, 300]), 250);
  });

  it("computes per-stroke stats: sampleCount, duration, avgHz, medianHz, pressure, tiltMax, cancelled", () => {
    const empty = computeStrokeStats([], { index: 1, pointerType: "pen" });
    assert.equal(empty.sampleCount, 0);
    assert.equal(empty.durationMs, 0);
    assert.equal(empty.avgHz, 0);
    assert.equal(empty.medianHz, 0);
    assert.equal(empty.pressureMin, 0);
    assert.equal(empty.pressureMax, 0);
    assert.equal(empty.tiltMax, 0);
    assert.equal(empty.cancelled, false);

    // 5 samples at t = 0, 5, 10, 15, 25 ms -> intervals: 5, 5, 5, 10 ms (rates: 200, 200, 200, 100 Hz)
    // Total duration = 25 ms, 4 intervals -> avgHz = 4 * 1000 / 25 = 160 Hz, medianHz = 200 Hz
    const stats = computeStrokeStats(
      [
        { timeMs: 100, pressure: 0.2, tiltX: 3, tiltY: 4 },
        { timeMs: 105, pressure: 0.4, tiltX: 6, tiltY: 8 },
        { timeMs: 110, pressure: 0.85, tiltX: -15, tiltY: 0 },
        { timeMs: 115, pressure: 0.6, tiltX: 0, tiltY: -20 },
        { timeMs: 125, pressure: 0.15, tiltX: 9, tiltY: 12 },
      ],
      { index: 3, pointerType: "pen", cancelled: true },
    );

    assert.equal(stats.index, 3);
    assert.equal(stats.pointerType, "pen");
    assert.equal(stats.sampleCount, 5);
    assert.equal(stats.durationMs, 25);
    assert.equal(stats.avgHz, 160);
    assert.equal(stats.medianHz, 200);
    assert.equal(stats.pressureMin, 0.15);
    assert.equal(stats.pressureMax, 0.85);
    assert.equal(stats.tiltMax, 20);
    assert.equal(stats.cancelled, true);
  });

  it("separates pen and mouse statistics and keeps recent 10 strokes", () => {
    const session = new PenProbeSession();

    for (let i = 0; i < 12; i++) {
      session.recordStroke(
        [
          { timeMs: 0, pressure: 0.1 + i * 0.01, tiltX: 10, tiltY: 0 },
          { timeMs: 5, pressure: 0.5, tiltX: 10 + i, tiltY: 0 },
          { timeMs: 10, pressure: 0.8, tiltX: 5, tiltY: 0 },
        ],
        { pointerType: "pen", cancelled: i === 4 },
      );
    }

    session.recordStroke(
      [
        { timeMs: 0, pressure: 0.5, tiltX: 0, tiltY: 0 },
        { timeMs: 8, pressure: 0.5, tiltX: 0, tiltY: 0 },
      ],
      { pointerType: "mouse", cancelled: false },
    );

    const report = session.getReport(10);
    assert.equal(report.strokes.length, 13);
    assert.equal(report.recentStrokes.length, 10);

    // Pen bucket
    assert.equal(report.summary.pen.strokeCount, 12);
    assert.equal(report.summary.pen.sampleCount, 36);
    assert.equal(report.summary.pen.avgSamplesPerStroke, 3);
    assert.equal(report.summary.pen.durationMs, 120);
    assert.equal(report.summary.pen.avgHz, 200);
    assert.equal(report.summary.pen.medianHz, 200);
    assert.equal(report.summary.pen.pressureMin, 0.1);
    assert.equal(report.summary.pen.pressureMax, 0.8);
    assert.equal(report.summary.pen.tiltMax, 21);
    assert.equal(report.summary.pen.cancelCount, 1);
    assert.equal(report.summary.pen.cancelled, true);
    assert.equal(report.summary.pen.recentStrokes.length, 10);
    assert.equal(report.summary.pen.recentStrokes[0].index, 3);
    assert.equal(report.summary.pen.recentStrokes[9].index, 12);

    // Mouse bucket
    assert.equal(report.summary.mouse.strokeCount, 1);
    assert.equal(report.summary.mouse.sampleCount, 2);
    assert.equal(report.summary.mouse.durationMs, 8);
    assert.equal(report.summary.mouse.avgHz, 125);
    assert.equal(report.summary.mouse.medianHz, 125);
    assert.equal(report.summary.mouse.cancelCount, 0);
    assert.equal(report.summary.mouse.cancelled, false);
    assert.equal(report.summary.mouse.recentStrokes.length, 1);

    const parsed = JSON.parse(session.toJSON(10));
    assert.equal(parsed.probe, "web");
    assert.equal(parsed.summary.pen.strokeCount, 12);
    assert.equal(parsed.summary.mouse.strokeCount, 1);
    assert.ok(formatReadoutLine(report).includes("笔合计(12笔)"));
  });

  it("extracts coalesced samples and listens to pointerrawupdate/pointermove/pointercancel", () => {
    const listeners = new Map<string, (ev: unknown) => void>();
    const fakeCanvas = {
      style: { touchAction: "", userSelect: "" },
      getBoundingClientRect: () => ({ left: 10, top: 20, width: 800, height: 600 }),
      setPointerCapture: () => {},
      hasPointerCapture: () => true,
      releasePointerCapture: () => {},
      addEventListener: (type: string, fn: (ev: unknown) => void) => {
        listeners.set(type, fn);
      },
      removeEventListener: (type: string) => {
        listeners.delete(type);
      },
    } as unknown as HTMLElement;

    const extracted = extractPointerSamples(
      {
        clientX: 50,
        clientY: 60,
        pressure: 0.5,
        tiltX: 5,
        tiltY: 12,
        timeStamp: 100,
        getCoalescedEvents: () => [
          { clientX: 30, clientY: 40, pressure: 0.3, tiltX: 3, tiltY: 4, timeStamp: 96 },
          { clientX: 50, clientY: 60, pressure: 0.5, tiltX: 5, tiltY: 12, timeStamp: 100 },
        ],
      },
      { left: 10, top: 20 },
    );
    assert.equal(extracted.length, 2);
    assert.deepEqual(extracted[0], {
      x: 20,
      y: 20,
      timeMs: 96,
      pressure: 0.3,
      tiltX: 3,
      tiltY: 4,
    });

    const ctrl = attachPenProbe(fakeCanvas);
    assert.equal(fakeCanvas.style.touchAction, "none");

    let contextPrevented = false;
    listeners.get("contextmenu")?.({
      preventDefault() {
        contextPrevented = true;
      },
    });
    assert.equal(contextPrevented, true);

    // Stroke 1: pen using pointerrawupdate (pointermove should not double-count)
    listeners.get("pointerdown")?.({
      pointerId: 1,
      pointerType: "pen",
      clientX: 20,
      clientY: 30,
      pressure: 0.25,
      tiltX: 10,
      tiltY: 0,
      timeStamp: 1000,
    });
    listeners.get("pointerrawupdate")?.({
      pointerId: 1,
      pointerType: "pen",
      getCoalescedEvents: () => [
        { clientX: 25, clientY: 35, pressure: 0.5, tiltX: 15, tiltY: 0, timeStamp: 1004 },
        { clientX: 30, clientY: 40, pressure: 0.75, tiltX: 20, tiltY: 0, timeStamp: 1008 },
      ],
    });
    // pointermove in same stroke is ignored when pointerrawupdate is active
    listeners.get("pointermove")?.({
      pointerId: 1,
      pointerType: "pen",
      clientX: 30,
      clientY: 40,
      pressure: 0.75,
      timeStamp: 1008,
    });
    listeners.get("pointerup")?.({
      pointerId: 1,
      pointerType: "pen",
      clientX: 30,
      clientY: 40,
      pressure: 0,
      timeStamp: 1012,
    });

    // Stroke 2: mouse using pointermove and ending with pointercancel
    listeners.get("pointerdown")?.({
      pointerId: 2,
      pointerType: "mouse",
      clientX: 100,
      clientY: 100,
      pressure: 0.5,
      timeStamp: 2000,
    });
    listeners.get("pointermove")?.({
      pointerId: 2,
      pointerType: "mouse",
      clientX: 120,
      clientY: 120,
      pressure: 0.5,
      timeStamp: 2010,
    });
    listeners.get("pointercancel")?.({
      pointerId: 2,
      pointerType: "mouse",
      clientX: 120,
      clientY: 120,
      pressure: 0,
      timeStamp: 2015,
    });

    const report = ctrl.session.getReport(10);
    assert.equal(report.summary.pen.strokeCount, 1);
    assert.equal(report.summary.pen.sampleCount, 3);
    assert.equal(report.summary.pen.durationMs, 8);
    assert.equal(report.summary.pen.avgHz, 250);
    assert.equal(report.summary.pen.pressureMin, 0.25);
    assert.equal(report.summary.pen.pressureMax, 0.75);
    assert.equal(report.summary.pen.tiltMax, 20);
    assert.equal(report.summary.pen.cancelled, false);

    assert.equal(report.summary.mouse.strokeCount, 1);
    assert.equal(report.summary.mouse.sampleCount, 2);
    assert.equal(report.summary.mouse.cancelled, true);

    ctrl.clear();
    assert.equal(ctrl.session.getStrokes().length, 0);
    ctrl.destroy();
    assert.equal(listeners.size, 0);
  });

  it("copies JSON to clipboard and falls back to textarea on failure", async () => {
    const session = new PenProbeSession();
    session.recordStroke([{ timeMs: 0, pressure: 0.5 }, { timeMs: 10, pressure: 0.6 }], {
      pointerType: "pen",
    });

    let copiedText = "";
    const fakeTextarea = {
      value: "",
      style: { display: "none" },
      focus() {},
      select() {},
    } as unknown as HTMLTextAreaElement;
    const fakeStatus = { textContent: "" } as unknown as HTMLElement;

    const ok1 = await copyProbeReportJson(
      session,
      {
        writeText: async (t: string) => {
          copiedText = t;
        },
      },
      fakeTextarea,
      fakeStatus,
    );
    assert.equal(ok1, true);
    assert.ok(copiedText.includes('"probe": "web"'));
    assert.equal(fakeTextarea.style.display, "none");

    const ok2 = await copyProbeReportJson(
      session,
      {
        writeText: async () => {
          throw new Error("denied");
        },
      },
      fakeTextarea,
      fakeStatus,
    );
    assert.equal(ok2, false);
    assert.equal(fakeTextarea.style.display, "block");
    assert.ok(fakeTextarea.value.includes('"probe": "web"'));

    const emptyBucket = summarizeBucket([], "pen", 10);
    assert.equal(emptyBucket.strokeCount, 0);
  });
});
