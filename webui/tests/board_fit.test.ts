// A board that follows another page: its frame is that page's size, shown whole
// and scaled to fit, and the pointer is taken back through the same scale.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { BoardEffects } from "../src/board/effects.ts";
import { fitFrame, OWN_SIZE, toBoardPoint } from "../src/board/fit.ts";
import { attachBoardInput } from "../src/board/input.ts";
import type { InputMessage } from "../src/net.ts";

describe("fitFrame: how a frame sits in the board element", () => {
  it("fills the element when the frame is the element's own size", () => {
    assert.equal(fitFrame(800, 600, 800, 600), OWN_SIZE);
    // The size a page reports is rounded to whole pixels.
    assert.equal(fitFrame(800, 600, 800.4, 599.5), OWN_SIZE);
    assert.equal(fitFrame(800, 600, 801, 600).mirrored, false);
  });

  it("shows a larger frame whole, scaled down and centred", () => {
    // Same shape: no bars.
    assert.deepEqual(fitFrame(800, 600, 400, 300), { mirrored: true, scale: 0.5, offsetX: 0, offsetY: 0 });
    // A wider element: bars left and right.
    assert.deepEqual(fitFrame(800, 600, 600, 300), { mirrored: true, scale: 0.5, offsetX: 100, offsetY: 0 });
    // A taller element: bars above and below.
    assert.deepEqual(fitFrame(800, 600, 400, 500), { mirrored: true, scale: 0.5, offsetX: 0, offsetY: 100 });
  });

  it("shows a smaller frame whole, scaled up and centred", () => {
    assert.deepEqual(fitFrame(400, 300, 1000, 600), { mirrored: true, scale: 2, offsetX: 100, offsetY: 0 });
  });

  it("leaves things alone before there is a frame or an element to measure", () => {
    assert.equal(fitFrame(0, 0, 800, 600), OWN_SIZE);
    assert.equal(fitFrame(800, 600, 0, 0), OWN_SIZE);
    assert.equal(fitFrame(Number.NaN, 600, 800, 600), OWN_SIZE);
  });

  it("takes a point of the element back onto the board", () => {
    const fit = fitFrame(800, 600, 600, 300);
    assert.deepEqual(toBoardPoint(fit, 100, 0), { x: 0, y: 0 }, "the frame's top left corner");
    assert.deepEqual(toBoardPoint(fit, 500, 300), { x: 800, y: 600 }, "the frame's bottom right corner");
    assert.deepEqual(toBoardPoint(fit, 300, 150), { x: 400, y: 300 }, "the middle");
    assert.deepEqual(toBoardPoint(fit, 0, 150), { x: -200, y: 300 }, "in the bar: off the board, to its left");
    assert.deepEqual(toBoardPoint(OWN_SIZE, 12.5, 7), { x: 12.5, y: 7 });
  });
});

describe("Board input on a board that follows another page", () => {
  function boardWith(box: { width: number; height: number }, frame: { w: number; h: number } | null) {
    const listeners = new Map<string, (ev: unknown) => void>();
    const canvas = {
      style: {},
      tabIndex: 0,
      getBoundingClientRect: () => ({ left: 100, top: 50, ...box }),
      setPointerCapture() {},
      focus() {},
      addEventListener: (type: string, fn: (ev: unknown) => void) => listeners.set(type, fn),
      removeEventListener() {},
    } as unknown as HTMLElement;
    const messages: InputMessage[] = [];
    attachBoardInput(canvas, { view: "main", onInput: (msg) => messages.push(msg), getFrameSize: () => frame });
    const press = (clientX: number, clientY: number) => {
      listeners.get("pointerdown")?.({ pointerId: 1, clientX, clientY, button: 0, buttons: 1, timeStamp: 1, preventDefault() {} });
      return messages.at(-1) as { x: number; y: number };
    };
    return { listeners, messages, press };
  }

  it("sends the element's own coordinates when the frame is its own size", () => {
    const { press } = boardWith({ width: 800, height: 600 }, { w: 800, h: 600 });
    const sent = press(100 + 250.5, 50 + 40);
    assert.deepEqual([sent.x, sent.y], [250.5, 40]);
  });

  it("sends board coordinates when the frame is another page's size", () => {
    // An 800 x 600 board shown in a 600 x 300 element: half size, 100 px bars at the sides.
    const { press, listeners, messages } = boardWith({ width: 600, height: 300 }, { w: 800, h: 600 });
    let sent = press(100 + 100, 50 + 0);
    assert.deepEqual([sent.x, sent.y], [0, 0], "the corner of the picture is the corner of the board");
    sent = press(100 + 300, 50 + 150);
    assert.deepEqual([sent.x, sent.y], [400, 300]);

    listeners.get("pointermove")?.({ pointerId: 1, clientX: 100 + 500, clientY: 50 + 300, buttons: 1, button: -1, timeStamp: 2 });
    const move = messages.at(-1) as { kind: string; x: number; y: number };
    assert.deepEqual([move.kind, move.x, move.y], ["move", 800, 600]);

    listeners.get("wheel")?.({ clientX: 100 + 300, clientY: 50 + 0, deltaX: 0, deltaY: -120, deltaMode: 0, preventDefault() {} });
    const wheel = messages.at(-1) as { kind: string; x: number; y: number };
    assert.deepEqual([wheel.kind, wheel.x, wheel.y], ["wheel", 400, 0], "the wheel zooms about the point under the pointer");
  });

  it("sends the element's own coordinates before the first frame", () => {
    const { press } = boardWith({ width: 600, height: 300 }, null);
    const sent = press(100 + 300, 50 + 150);
    assert.deepEqual([sent.x, sent.y], [300, 150]);
  });
});

describe("Board effects on a board that follows another page", () => {
  it("draws in the frame's pixels, not the element's", () => {
    const log: string[] = [];
    const context = () =>
      new Proxy({} as Record<string, unknown>, {
        get: (target, key) => {
          if (key in target) return target[key as string];
          return (...args: unknown[]) => {
            if (key === "drawImage" || key === "setTransform") log.push(`${String(key)}(${args.filter((a) => typeof a !== "object").join(",")})`);
          };
        },
        set: (target, key, value) => {
          target[key as string] = value;
          return true;
        },
      });
    (globalThis as Record<string, unknown>).document = {
      createElement: () => ({ width: 0, height: 0, getContext: () => context(), remove() {} }),
    };
    // The other page's board is 800 x 600; here it is shown in a 400 x 300 element.
    const board = {
      width: 800,
      height: 600,
      clientWidth: 400,
      clientHeight: 300,
      classList: { contains: (name: string) => name === "board-mirror" },
      getClientRects: () => [1],
      after(node: Record<string, unknown>) {
        node.previousSibling = board;
      },
    };
    const effects = new BoardEffects({
      canvas: () => board as unknown as HTMLCanvasElement,
      frameSize: () => ({ w: 800, h: 600 }),
      now: () => 0,
      raf: () => 1,
      cancelRaf() {},
    });
    effects.frame();
    effects.play({ kind: "fade", duration: 300 });
    assert.equal(effects.active, true);
    assert.ok(log.includes("setTransform(1,0,0,1,0,0)"), `one overlay pixel per board pixel, got ${log.join(" ")}`);
    assert.ok(log.includes("drawImage(0,0,800,600)"), `the picture from before covers the whole board, got ${log.join(" ")}`);
  });
});
