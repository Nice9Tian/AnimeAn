import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  attachBoardInput,
  domButtonsToQt,
  domButtonToQt,
  domModifiersToQt,
  domWheelToQt,
  QT_ALT_MODIFIER,
  QT_BACK_BUTTON,
  QT_CTRL_MODIFIER,
  QT_FORWARD_BUTTON,
  QT_LEFT_BUTTON,
  QT_MIDDLE_BUTTON,
  QT_NO_BUTTON,
  QT_RIGHT_BUTTON,
  QT_SHIFT_MODIFIER,
} from "../src/board/input.ts";
import type { InputMessage } from "../src/net.ts";

describe("Board Input conversions & event binding", () => {
  it("converts DOM mouse buttons and button bitmasks to Qt::MouseButton", () => {
    assert.equal(domButtonToQt(0), QT_LEFT_BUTTON);
    assert.equal(domButtonToQt(1), QT_MIDDLE_BUTTON);
    assert.equal(domButtonToQt(2), QT_RIGHT_BUTTON);
    assert.equal(domButtonToQt(3), QT_BACK_BUTTON);
    assert.equal(domButtonToQt(4), QT_FORWARD_BUTTON);
    assert.equal(domButtonToQt(-1), QT_NO_BUTTON);

    assert.equal(domButtonsToQt(1), QT_LEFT_BUTTON);
    assert.equal(domButtonsToQt(2), QT_RIGHT_BUTTON);
    assert.equal(domButtonsToQt(4), QT_MIDDLE_BUTTON);
    assert.equal(domButtonsToQt(1 | 2 | 4), QT_LEFT_BUTTON | QT_RIGHT_BUTTON | QT_MIDDLE_BUTTON);
  });

  it("converts DOM keyboard modifiers to Qt::KeyboardModifier bitmask", () => {
    assert.equal(domModifiersToQt({}), 0);
    assert.equal(domModifiersToQt({ shiftKey: true }), QT_SHIFT_MODIFIER);
    assert.equal(domModifiersToQt({ ctrlKey: true }), QT_CTRL_MODIFIER);
    assert.equal(domModifiersToQt({ altKey: true }), QT_ALT_MODIFIER);
    assert.equal(
      domModifiersToQt({ shiftKey: true, ctrlKey: true, altKey: true }),
      QT_SHIFT_MODIFIER | QT_CTRL_MODIFIER | QT_ALT_MODIFIER,
    );
  });

  it("converts DOM wheel deltas to QWheelEvent::angleDelta units", () => {
    assert.deepEqual(domWheelToQt({ deltaX: 0, deltaY: -120, deltaMode: 0 }), { dx: 0, dy: 120 });
    assert.deepEqual(domWheelToQt({ deltaX: 0, deltaY: -3, deltaMode: 1 }), { dx: 0, dy: 120 });
    assert.deepEqual(domWheelToQt({ deltaX: 1, deltaY: 0, deltaMode: 2 }), { dx: -120, dy: 0 });
  });

  it("captures pointer, expands coalesced move events, and emits press/move/release/wheel/enter/leave/focus", () => {
    const listeners = new Map<string, (ev: unknown) => void>();
    const capturedIds: number[] = [];
    const releasedIds: number[] = [];

    const fakeCanvas = {
      style: { touchAction: "", userSelect: "" },
      tabIndex: -1,
      getBoundingClientRect: () => ({ left: 100, top: 50, width: 500, height: 400 }),
      setPointerCapture: (id: number) => capturedIds.push(id),
      hasPointerCapture: (id: number) => capturedIds.includes(id),
      releasePointerCapture: (id: number) => releasedIds.push(id),
      focus: () => {},
      addEventListener: (type: string, fn: (ev: unknown) => void) => {
        listeners.set(type, fn);
      },
      removeEventListener: (type: string) => {
        listeners.delete(type);
      },
    } as unknown as HTMLElement;

    const messages: InputMessage[] = [];
    const handle = attachBoardInput(fakeCanvas, {
      view: "main",
      onInput: (msg) => messages.push(msg),
    });

    assert.equal(fakeCanvas.style.touchAction, "none");
    assert.equal(fakeCanvas.tabIndex, 0);

    // 1. pointerdown（右键 + Ctrl+Shift）
    listeners.get("pointerdown")?.({
      pointerId: 7,
      clientX: 110.5,
      clientY: 70.25,
      button: 2,
      buttons: 2,
      shiftKey: true,
      ctrlKey: true,
      altKey: false,
      pressure: 0.8,
      timeStamp: 1001,
      preventDefault() {},
    });
    assert.deepEqual(capturedIds, [7]);
    assert.deepEqual(messages[0], {
      t: "input",
      view: "main",
      kind: "press",
      x: 10.5,
      y: 20.25,
      button: QT_RIGHT_BUTTON,
      buttons: QT_RIGHT_BUTTON,
      modifiers: QT_SHIFT_MODIFIER | QT_CTRL_MODIFIER,
      pressure: 0.8,
      time: 1001,
    });

    // 2. pointermove 带 getCoalescedEvents
    listeners.get("pointermove")?.({
      pointerId: 7,
      buttons: 1,
      shiftKey: false,
      ctrlKey: false,
      altKey: true,
      getCoalescedEvents: () => [
        { clientX: 120, clientY: 80, buttons: 1, pressure: 0.4, timeStamp: 1005 },
        { clientX: 125, clientY: 85, buttons: 1, pressure: 0.6, timeStamp: 1009 },
      ],
    });
    assert.equal(messages.length, 3);
    assert.deepEqual(messages[1], {
      t: "input",
      view: "main",
      kind: "move",
      x: 20,
      y: 30,
      button: 0,
      buttons: QT_LEFT_BUTTON,
      modifiers: QT_ALT_MODIFIER,
      pressure: 0.4,
      time: 1005,
    });
    assert.deepEqual(messages[2], {
      t: "input",
      view: "main",
      kind: "move",
      x: 25,
      y: 35,
      button: 0,
      buttons: QT_LEFT_BUTTON,
      modifiers: QT_ALT_MODIFIER,
      pressure: 0.6,
      time: 1009,
    });

    // 3. pointerup
    listeners.get("pointerup")?.({
      pointerId: 7,
      clientX: 125,
      clientY: 85,
      button: 0,
      buttons: 0,
      shiftKey: false,
      ctrlKey: false,
      altKey: false,
      timeStamp: 1012,
    });
    assert.deepEqual(releasedIds, [7]);
    assert.deepEqual(messages[3], {
      t: "input",
      view: "main",
      kind: "release",
      x: 25,
      y: 35,
      button: QT_LEFT_BUTTON,
      buttons: 0,
      modifiers: 0,
      pressure: 0,
      time: 1012,
    });

    // 4. wheel
    let wheelPrevented = false;
    listeners.get("wheel")?.({
      clientX: 200,
      clientY: 250,
      deltaX: 0,
      deltaY: -120,
      deltaMode: 0,
      shiftKey: false,
      ctrlKey: true,
      altKey: false,
      preventDefault() {
        wheelPrevented = true;
      },
    });
    assert.equal(wheelPrevented, true);
    assert.deepEqual(messages[4], {
      t: "input",
      view: "main",
      kind: "wheel",
      x: 100,
      y: 200,
      dx: 0,
      dy: 120,
      modifiers: QT_CTRL_MODIFIER,
    });

    // 5. enter / leave / focus / blur
    listeners.get("pointerenter")?.({ clientX: 150, clientY: 90 });
    listeners.get("pointerleave")?.({});
    listeners.get("focus")?.({});
    listeners.get("blur")?.({});
    assert.deepEqual(messages[5], { t: "input", view: "main", kind: "enter", x: 50, y: 40 });
    assert.deepEqual(messages[6], { t: "input", view: "main", kind: "leave" });
    assert.deepEqual(messages[7], { t: "input", view: "main", kind: "focus", focused: true });
    assert.deepEqual(messages[8], { t: "input", view: "main", kind: "focus", focused: false });

    // 6. resend() 重发当前尺寸
    handle.resend();
    assert.deepEqual(messages[9], {
      t: "input",
      view: "main",
      kind: "resize",
      w: 500,
      h: 400,
      dpr: 1,
    });

    // 7. 按住左键时再按右键、再松开左键：浏览器只发 pointermove（button 是变化的那个键），
    //    转成按下和松开，右键才能在拖动中取消，先松开左键也能结束拖动
    const chord = (button: number, buttons: number) => listeners.get("pointermove")?.({
      pointerId: 7, clientX: 130, clientY: 90, button, buttons,
      shiftKey: false, ctrlKey: false, altKey: false, timeStamp: 1020,
      getCoalescedEvents: () => [],
    });
    const before = messages.length;
    chord(2, 3);
    assert.deepEqual(messages.slice(before + 1).map((m: any) => [m.kind, m.button, m.buttons]),
                     [["press", QT_RIGHT_BUTTON, QT_LEFT_BUTTON | QT_RIGHT_BUTTON]]);
    const mid = messages.length;
    chord(0, 2);
    assert.deepEqual(messages.slice(mid + 1).map((m: any) => [m.kind, m.button, m.buttons]),
                     [["release", QT_LEFT_BUTTON, QT_RIGHT_BUTTON]]);
    const plain = messages.length;
    chord(-1, 2);
    assert.equal(messages.length, plain + 1);      // a plain move is only a move

    handle.destroy();
    assert.equal(listeners.size, 0);
  });

  it("re-sends current size via resend() after ResizeObserver fires before connection", () => {
    const prevRO = globalThis.ResizeObserver;
    let roCallback: ((entries: Array<{ contentRect: { width: number; height: number } }>) => void) | null = null;

    globalThis.ResizeObserver = class MockResizeObserver {
      constructor(cb: (entries: Array<{ contentRect: { width: number; height: number } }>) => void) {
        roCallback = cb;
      }
      observe() {
        // 模拟连接建立前 ResizeObserver 立即触发了 1280×720
        roCallback?.([{ contentRect: { width: 1280, height: 720 } }]);
      }
      unobserve() {}
      disconnect() {}
    } as unknown as typeof ResizeObserver;

    try {
      let connected = false;
      const sentToBackend: InputMessage[] = [];

      const fakeCanvas = {
        style: { touchAction: "", userSelect: "" },
        tabIndex: -1,
        getBoundingClientRect: () => ({ left: 0, top: 0, width: 1280, height: 720 }),
        addEventListener: () => {},
        removeEventListener: () => {},
      } as unknown as HTMLElement;

      const handle = attachBoardInput(fakeCanvas, {
        view: "main",
        getDpr: () => 1.5,
        onInput: (msg) => {
          if (connected) sentToBackend.push(msg);
        },
      });

      // 连接前触发的 resize 被丢弃
      assert.equal(sentToBackend.length, 0);

      // 连接成功后调用 resend()，重发当前尺寸 1280×720
      connected = true;
      handle.resend();
      assert.equal(sentToBackend.length, 1);
      assert.deepEqual(sentToBackend[0], {
        t: "input",
        view: "main",
        kind: "resize",
        w: 1280,
        h: 720,
        dpr: 1.5,
      });

      handle.destroy();
    } finally {
      globalThis.ResizeObserver = prevRO;
    }
  });
});
