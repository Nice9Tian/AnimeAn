import type { BackendClient, InputMessage } from "../net.ts";

export const QT_NO_BUTTON = 0;
export const QT_LEFT_BUTTON = 1;
export const QT_RIGHT_BUTTON = 2;
export const QT_MIDDLE_BUTTON = 4;
export const QT_BACK_BUTTON = 8;
export const QT_FORWARD_BUTTON = 16;

export const QT_SHIFT_MODIFIER = 0x02000000;
export const QT_CTRL_MODIFIER = 0x04000000;
export const QT_ALT_MODIFIER = 0x08000000;

/**
 * 将 DOM MouseEvent.button (0,1,2,3,4) 换算为 Qt::MouseButton 位值。
 */
export function domButtonToQt(button: number): number {
  switch (button) {
    case 0:
      return QT_LEFT_BUTTON;
    case 1:
      return QT_MIDDLE_BUTTON;
    case 2:
      return QT_RIGHT_BUTTON;
    case 3:
      return QT_BACK_BUTTON;
    case 4:
      return QT_FORWARD_BUTTON;
    default:
      return QT_NO_BUTTON;
  }
}

/**
 * 将 DOM MouseEvent.buttons 掩码换算为 Qt::MouseButtons 掩码。
 * DOM buttons: 1=Left, 2=Right, 4=Middle, 8=Back, 16=Forward，与 Qt::MouseButton 位定义一致。
 */
export function domButtonsToQt(buttons: number): number {
  return (Number(buttons) || 0) & 0x1f;
}

/**
 * 将 DOM 键盘修饰键状态换算为 Qt::KeyboardModifiers 位掩码。
 */
export function domModifiersToQt(e: {
  shiftKey?: boolean;
  ctrlKey?: boolean;
  altKey?: boolean;
}): number {
  let mask = 0;
  if (e.shiftKey) mask |= QT_SHIFT_MODIFIER;
  if (e.ctrlKey) mask |= QT_CTRL_MODIFIER;
  if (e.altKey) mask |= QT_ALT_MODIFIER;
  return mask;
}

/**
 * 将 DOM WheelEvent 换算为与 QWheelEvent::angleDelta 同号同量纲的 (dx, dy)（一格 = 120）。
 */
export function domWheelToQt(e: {
  deltaX?: number;
  deltaY?: number;
  deltaMode?: number;
}): { dx: number; dy: number } {
  const mode = e.deltaMode ?? 0;
  const factor = mode === 1 ? 40 : mode === 2 ? 120 : 1;
  const dx = Math.round(-(e.deltaX ?? 0) * factor) || 0;
  const dy = Math.round(-(e.deltaY ?? 0) * factor) || 0;
  return { dx, dy };
}

export interface BoardInputOptions {
  view?: string;
  client?: BackendClient;
  onInput?: (msg: InputMessage) => void;
  getDpr?: () => number;
}

export interface BoardInputHandle {
  resend(): void;
  destroy(): void;
}

export function attachBoardInput(
  canvas: HTMLElement,
  options: BoardInputOptions = {},
): BoardInputHandle {
  const view = options.view ?? "main";
  const getDpr =
    options.getDpr ??
    (() => (typeof window !== "undefined" && window.devicePixelRatio ? window.devicePixelRatio : 1));

  const emit = (msg: InputMessage) => {
    options.client?.sendInput(msg);
    options.onInput?.(msg);
  };

  if (canvas.style) {
    canvas.style.touchAction = "none";
    canvas.style.userSelect = "none";
  }
  if (typeof canvas.tabIndex === "number" && canvas.tabIndex < 0) {
    canvas.tabIndex = 0;
  }

  const localCoords = (e: { clientX?: number; clientY?: number; offsetX?: number; offsetY?: number }) => {
    if (typeof canvas.getBoundingClientRect === "function" && typeof e.clientX === "number" && typeof e.clientY === "number") {
      const rect = canvas.getBoundingClientRect();
      return {
        x: e.clientX - rect.left,
        y: e.clientY - rect.top,
      };
    }
    return {
      x: Number(e.offsetX ?? 0),
      y: Number(e.offsetY ?? 0),
    };
  };

  const eventTime = (e: { timeStamp?: number }): number => {
    return Math.round(Number(e.timeStamp ?? Date.now()));
  };

  const eventPressure = (e: { pressure?: number; buttons?: number }, defaultWhenPressed = 0.5): number => {
    if (typeof e.pressure === "number" && e.pressure > 0) return e.pressure;
    return (e.buttons ?? 0) !== 0 ? defaultWhenPressed : 0;
  };

  let rawUpdateActive = false;

  const emitMoveSamples = (e: PointerEvent) => {
    const coalesced =
      typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
    const samples = coalesced.length > 0 ? coalesced : [e];
    const modifiers = domModifiersToQt(e);
    const buttons = domButtonsToQt(e.buttons);
    for (const sample of samples) {
      const { x, y } = localCoords(sample);
      emit({
        t: "input",
        view,
        kind: "move",
        x,
        y,
        button: QT_NO_BUTTON,
        buttons,
        modifiers,
        pressure: eventPressure(sample),
        time: eventTime(sample),
      });
    }
  };

  const onPointerDown = (e: PointerEvent) => {
    e.preventDefault?.();
    try {
      canvas.setPointerCapture?.(e.pointerId);
    } catch {
      // 忽略虚拟事件无 pointerId 捕获的情况
    }
    canvas.focus?.({ preventScroll: true });
    const { x, y } = localCoords(e);
    const button = domButtonToQt(e.button);
    const buttons = domButtonsToQt(e.buttons) || button;
    emit({
      t: "input",
      view,
      kind: "press",
      x,
      y,
      button,
      buttons,
      modifiers: domModifiersToQt(e),
      pressure: eventPressure(e, 0.5),
      time: eventTime(e),
    });
  };

  const onPointerRawUpdate = (e: Event) => {
    rawUpdateActive = true;
    emitMoveSamples(e as PointerEvent);
  };

  // A second button pressed or released while another is held comes as a
  // pointermove with `button` set (a chorded change): there is no
  // pointerdown / pointerup for it. Reported as the press or release it is,
  // so a right click during a left drag reaches the board, and a left button
  // released while the right one is still down ends the drag.
  const emitChordedButton = (e: PointerEvent) => {
    if (typeof e.button !== "number" || e.button < 0) return;
    const button = domButtonToQt(e.button);
    const buttons = domButtonsToQt(e.buttons);
    const { x, y } = localCoords(e);
    emit({
      t: "input",
      view,
      kind: buttons & button ? "press" : "release",
      x,
      y,
      button,
      buttons,
      modifiers: domModifiersToQt(e),
      pressure: eventPressure(e, buttons & button ? 0.5 : 0),
      time: eventTime(e),
    });
  };

  const onPointerMove = (e: PointerEvent) => {
    if (!rawUpdateActive) emitMoveSamples(e);
    emitChordedButton(e);
  };

  const onPointerUpOrCancel = (e: PointerEvent) => {
    try {
      if (canvas.hasPointerCapture?.(e.pointerId)) {
        canvas.releasePointerCapture?.(e.pointerId);
      }
    } catch {
      // 忽略
    }
    const { x, y } = localCoords(e);
    emit({
      t: "input",
      view,
      kind: "release",
      x,
      y,
      button: domButtonToQt(e.button),
      buttons: domButtonsToQt(e.buttons),
      modifiers: domModifiersToQt(e),
      pressure: 0,
      time: eventTime(e),
    });
  };

  const onPointerEnter = (e: PointerEvent) => {
    const { x, y } = localCoords(e);
    emit({
      t: "input",
      view,
      kind: "enter",
      x,
      y,
    });
  };

  const onPointerLeave = () => {
    emit({
      t: "input",
      view,
      kind: "leave",
    });
  };

  const onWheel = (e: WheelEvent) => {
    e.preventDefault?.();
    const { x, y } = localCoords(e);
    const { dx, dy } = domWheelToQt(e);
    emit({
      t: "input",
      view,
      kind: "wheel",
      x,
      y,
      dx,
      dy,
      modifiers: domModifiersToQt(e),
    });
  };

  const onContextMenu = (e: Event) => {
    e.preventDefault?.();
  };

  const onFocus = () => {
    emit({
      t: "input",
      view,
      kind: "focus",
      focused: true,
    });
  };

  const onBlur = () => {
    emit({
      t: "input",
      view,
      kind: "focus",
      focused: false,
    });
  };

  const supportsRawUpdate =
    typeof window !== "undefined" && "onpointerrawupdate" in window;

  canvas.addEventListener("pointerdown", onPointerDown);
  if (supportsRawUpdate) {
    canvas.addEventListener("pointerrawupdate", onPointerRawUpdate);
  }
  canvas.addEventListener("pointermove", onPointerMove);
  canvas.addEventListener("pointerup", onPointerUpOrCancel);
  canvas.addEventListener("pointercancel", onPointerUpOrCancel);
  canvas.addEventListener("pointerenter", onPointerEnter);
  canvas.addEventListener("pointerleave", onPointerLeave);
  canvas.addEventListener("wheel", onWheel, { passive: false });
  canvas.addEventListener("contextmenu", onContextMenu);
  canvas.addEventListener("dragstart", onContextMenu);
  canvas.addEventListener("focus", onFocus);
  canvas.addEventListener("blur", onBlur);

  let lastW = 0;
  let lastH = 0;

  const sendResize = (w: number, h: number) => {
    if (w > 0 && h > 0) {
      emit({
        t: "input",
        view,
        kind: "resize",
        w,
        h,
        dpr: getDpr(),
      });
    }
  };

  let resizeObserver: ResizeObserver | null = null;
  if (typeof ResizeObserver !== "undefined") {
    resizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const w = Math.round(entry.contentRect.width);
        const h = Math.round(entry.contentRect.height);
        if (w > 0 && h > 0) {
          lastW = w;
          lastH = h;
          sendResize(w, h);
        }
      }
    });
    resizeObserver.observe(canvas);
  }

  return {
    resend() {
      let w = lastW;
      let h = lastH;
      if (w <= 0 || h <= 0) {
        if (typeof canvas.getBoundingClientRect === "function") {
          const rect = canvas.getBoundingClientRect();
          w = Math.round(rect.width);
          h = Math.round(rect.height);
        }
        if (w <= 0 || h <= 0) {
          w = Math.round(Number(canvas.clientWidth) || 0);
          h = Math.round(Number(canvas.clientHeight) || 0);
        }
      }
      sendResize(w, h);
    },
    destroy() {
      resizeObserver?.disconnect();
      canvas.removeEventListener("pointerdown", onPointerDown);
      if (supportsRawUpdate) {
        canvas.removeEventListener("pointerrawupdate", onPointerRawUpdate);
      }
      canvas.removeEventListener("pointermove", onPointerMove);
      canvas.removeEventListener("pointerup", onPointerUpOrCancel);
      canvas.removeEventListener("pointercancel", onPointerUpOrCancel);
      canvas.removeEventListener("pointerenter", onPointerEnter);
      canvas.removeEventListener("pointerleave", onPointerLeave);
      canvas.removeEventListener("wheel", onWheel);
      canvas.removeEventListener("contextmenu", onContextMenu);
      canvas.removeEventListener("dragstart", onContextMenu);
      canvas.removeEventListener("focus", onFocus);
      canvas.removeEventListener("blur", onBlur);
    },
  };
}
