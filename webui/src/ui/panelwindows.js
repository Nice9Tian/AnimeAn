import { DOCK_FOR_WINDOW } from "./app.js";

export const WINDOW_FOR_DOCK = {
  ...Object.fromEntries(Object.entries(DOCK_FOR_WINDOW).map(([win, dock]) => [dock, win])),
  timelineDock: "timeline",
};

export function minSizeForDock(dockId, runtime) {
  const node = runtime?.project?.scene?.nodes?.[dockId];
  const minW = Math.max(80, Number(node?.minWidth ?? (dockId === "timelineDock" ? 134 : 80)));
  const minH = Math.max(60, Number(node?.minHeight ?? 60));
  return { minWidth: minW, minHeight: minH };
}

export function triggerDockClosed(store, dockId) {
  const name = WINDOW_FOR_DOCK[dockId];
  if (name) {
    return store.call("menu.trigger", { id: `windows.${name}` });
  }
  return undefined;
}

function sendControlMessage(target, action, dockId) {
  if (!target) return;
  if (typeof target.sendPanelAction === "function") {
    target.sendPanelAction(action, dockId);
    return;
  }
  if (typeof target.postMessage === "function") {
    target.postMessage({ t: action, dockId });
  }
}

export function setupSatelliteDock(rootEl, dockId, relayOrChannel, options = {}) {
  const getTauri = () => options.tauri ?? (typeof window !== "undefined" ? window.__TAURI__ : undefined);
  const win = options.window ?? (typeof window !== "undefined" ? window : undefined);

  const isHeaderDragEvent = (e) => {
    const target = e?.target;
    if (!target || typeof target.closest !== "function") return false;
    const header = target.closest(".vf-dock-header");
    if (!header) return false;
    if (target.closest("button, .dock-btn, .dock-tab")) return false;
    if (typeof e.button === "number" && e.button !== 0) return false;
    return true;
  };

  const onDockFloatRequest = (did = dockId) => {
    sendControlMessage(relayOrChannel, "restore", did || dockId);
  };

  const onPointerDownCapture = (e) => {
    if (!isHeaderDragEvent(e)) return;
    e.stopImmediatePropagation?.();
    e.stopPropagation?.();
  };

  const onMouseDownCapture = (e) => {
    if (!isHeaderDragEvent(e)) return;
    e.stopImmediatePropagation?.();
    e.stopPropagation?.();
    if (Number(e.detail) >= 2) {
      onDockFloatRequest(dockId);
      return;
    }
    const tauri = getTauri();
    tauri?.window?.getCurrentWindow?.()?.startDragging?.();
  };

  const onPageHide = () => {
    sendControlMessage(relayOrChannel, "closed", dockId);
  };

  if (rootEl && typeof rootEl.addEventListener === "function") {
    rootEl.addEventListener("pointerdown", onPointerDownCapture, true);
    rootEl.addEventListener("mousedown", onMouseDownCapture, true);
  }
  if (win && typeof win.addEventListener === "function") {
    win.addEventListener("pagehide", onPageHide);
  }

  return {
    onDockFloatRequest,
    onDockClosed(did = dockId) {
      sendControlMessage(relayOrChannel, "close", did || dockId);
    },
    destroy() {
      if (rootEl && typeof rootEl.removeEventListener === "function") {
        rootEl.removeEventListener("pointerdown", onPointerDownCapture, true);
        rootEl.removeEventListener("mousedown", onMouseDownCapture, true);
      }
      if (win && typeof win.removeEventListener === "function") {
        win.removeEventListener("pagehide", onPageHide);
      }
    },
  };
}

export function createPanelWindows(runtime, store, options = {}) {
  const externalDocks = runtime?.externalDocks instanceof Set ? runtime.externalDocks : new Set();
  if (runtime && typeof runtime === "object") {
    runtime.externalDocks = externalDocks;
  }
  if (store && typeof store === "object") {
    store.externalDocks = externalDocks;
  }

  const openWindows = new Set();
  const closingByHost = new Set();
  const floatingRects = new Map();

  const getTauri = () => options.tauri ?? (typeof window !== "undefined" ? window.__TAURI__ : undefined);
  const getScreenOffset = () => {
    const w = options.window ?? (typeof window !== "undefined" ? window : undefined);
    return {
      screenX: Number(w?.screenX ?? 0) || 0,
      screenY: Number(w?.screenY ?? 0) || 0,
    };
  };

  const invokeTauri = (cmd, args) => {
    const tauri = getTauri();
    const invoke = options.invoke ?? tauri?.core?.invoke;
    if (typeof invoke === "function") {
      return Promise.resolve(invoke(cmd, args));
    }
    return Promise.resolve();
  };

  const isBackendWindowVisible = (dockId) => {
    const winName = WINDOW_FOR_DOCK[dockId];
    if (!winName) return true;
    const wins = store.get?.("windows");
    if (!wins || typeof wins !== "object" || !(winName in wins)) return true;
    const entry = wins[winName];
    return typeof entry === "boolean" ? entry : Boolean(entry?.visible);
  };

  const getFloatingRect = (dockId) => {
    const layout = runtime.getDockLayout?.();
    const fromLayout = layout?.floating?.find((f) => f.dockId === dockId);
    if (fromLayout) {
      const rect = {
        x: Math.round(Number(fromLayout.x) || 0),
        y: Math.round(Number(fromLayout.y) || 0),
        width: Math.max(80, Math.round(Number(fromLayout.width) || 240)),
        height: Math.max(60, Math.round(Number(fromLayout.height) || 280)),
      };
      floatingRects.set(dockId, rect);
      return rect;
    }
    return floatingRects.get(dockId) ?? { x: 80, y: 90, width: 240, height: 280 };
  };

  const openWindow = (dockId, rectInput) => {
    if (!getTauri()) return;
    const rect = rectInput ?? getFloatingRect(dockId);
    floatingRects.set(dockId, rect);
    const { screenX, screenY } = getScreenOffset();
    const { minWidth, minHeight } = minSizeForDock(dockId, runtime);
    const label = `panel-${dockId}`;
    const url = `index.html?only=${encodeURIComponent(dockId)}&satellite=1`;
    closingByHost.delete(dockId);
    openWindows.add(dockId);
    void invokeTauri("open_panel_window", {
      label,
      url,
      x: Math.round(screenX + (Number(rect.x) || 0)),
      y: Math.round(screenY + (Number(rect.y) || 0)),
      width: Math.max(minWidth, Math.round(Number(rect.width) || 240)),
      height: Math.max(minHeight, Math.round(Number(rect.height) || 280)),
      minWidth,
      minHeight,
    });
  };

  const closeWindow = (dockId, hostInitiated = true) => {
    if (!openWindows.has(dockId)) return;
    openWindows.delete(dockId);
    if (hostInitiated) {
      closingByHost.add(dockId);
    }
    if (!getTauri()) return;
    void invokeTauri("close_panel_window", { label: `panel-${dockId}` });
  };

  const externalizeDock = (dockId, entry) => {
    if (!getTauri()) return;
    const rect = {
      x: Math.round(Number(entry?.x ?? 80)),
      y: Math.round(Number(entry?.y ?? 90)),
      width: Math.max(80, Math.round(Number(entry?.width ?? 240))),
      height: Math.max(60, Math.round(Number(entry?.height ?? 280))),
    };
    floatingRects.set(dockId, rect);
    const wasExternal = externalDocks.has(dockId);
    externalDocks.add(dockId);
    runtime.dockSetVisible?.(dockId, false);
    if (isBackendWindowVisible(dockId) && !openWindows.has(dockId)) {
      openWindow(dockId, rect);
    }
    if (!wasExternal) {
      options.onExternalChanged?.(dockId);
    }
  };

  const onDockChanged = (dockId, commit = true) => {
    if (!getTauri() || commit === false || !dockId) return;
    const layout = runtime.getDockLayout?.();
    const entry = layout?.floating?.find((f) => f.dockId === dockId);
    if (entry) {
      externalizeDock(dockId, entry);
      return;
    }
    if (externalDocks.has(dockId)) {
      externalDocks.delete(dockId);
      closeWindow(dockId, true);
      if (isBackendWindowVisible(dockId)) {
        runtime.dockSetVisible?.(dockId, true);
      }
      options.onExternalChanged?.(dockId);
    }
  };

  const handleCloseDock = (dockId) => {
    if (typeof options.onDockClosed === "function") {
      options.onDockClosed(dockId);
    } else {
      triggerDockClosed(store, dockId);
    }
  };

  const handleAction = (action, dockId) => {
    if (!getTauri() || !dockId) return;
    if (action === "restore") {
      const wasExternal = externalDocks.delete(dockId);
      closeWindow(dockId, true);
      runtime.dockRestore?.(dockId);
      if (isBackendWindowVisible(dockId)) {
        runtime.dockSetVisible?.(dockId, true);
      }
      if (wasExternal) {
        options.onExternalChanged?.(dockId);
      }
      options.onDockRestored?.(dockId);
      return;
    }
    if (action === "close") {
      closeWindow(dockId, true);
      runtime.dockSetVisible?.(dockId, false);
      handleCloseDock(dockId);
      return;
    }
    if (action === "closed") {
      if (closingByHost.has(dockId)) {
        closingByHost.delete(dockId);
        return;
      }
      if (!openWindows.has(dockId) && !externalDocks.has(dockId)) return;
      openWindows.delete(dockId);
      runtime.dockSetVisible?.(dockId, false);
      handleCloseDock(dockId);
    }
  };

  let unsubHost;
  if (options.host && typeof options.host.onPanelAction === "function") {
    unsubHost = options.host.onPanelAction(handleAction);
  }
  const channel = options.channel;
  let onChannelMsg;
  if (channel && !unsubHost) {
    onChannelMsg = (ev) => {
      const msg = ev && typeof ev === "object" && "data" in ev ? ev.data : ev;
      if (!msg || typeof msg !== "object") return;
      if (msg.t === "restore" || msg.t === "close" || msg.t === "closed") {
        handleAction(msg.t, String(msg.dockId ?? ""));
      } else if (msg.t === "panel" && (msg.action === "restore" || msg.action === "close" || msg.action === "closed")) {
        handleAction(msg.action, String(msg.dockId ?? ""));
      }
    };
    if (typeof channel.addEventListener === "function") {
      channel.addEventListener("message", onChannelMsg);
    } else {
      const prev = channel.onmessage;
      channel.onmessage = (ev) => {
        prev?.(ev);
        onChannelMsg(ev);
      };
    }
  }

  store.on?.("windows", (value) => {
    if (!getTauri() || !value || typeof value !== "object") return;
    for (const [dockId, winName] of Object.entries(WINDOW_FOR_DOCK)) {
      if (!externalDocks.has(dockId)) continue;
      if (!(winName in value)) continue;
      const raw = value[winName];
      const visible = typeof raw === "boolean" ? raw : Boolean(raw?.visible);
      runtime.dockSetVisible?.(dockId, false);
      if (!visible) {
        closeWindow(dockId, true);
      } else if (!openWindows.has(dockId)) {
        openWindow(dockId, getFloatingRect(dockId));
      }
    }
  });

  store.on?.("timeline", (value) => {
    if (!getTauri() || !value || typeof value !== "object") return;
    if (value.floating) {
      let layout = runtime.getDockLayout?.();
      let entry = layout?.floating?.find((f) => f.dockId === "timelineDock");
      if (!entry) {
        runtime.dockFloat?.("timelineDock");
        layout = runtime.getDockLayout?.();
        entry = layout?.floating?.find((f) => f.dockId === "timelineDock");
      }
      externalizeDock("timelineDock", entry);
    } else if (externalDocks.has("timelineDock")) {
      externalDocks.delete("timelineDock");
      closeWindow("timelineDock", true);
      const layout = runtime.getDockLayout?.();
      if (layout?.floating?.some((f) => f.dockId === "timelineDock")) {
        runtime.dockRestore?.("timelineDock");
      }
      if (isBackendWindowVisible("timelineDock")) {
        runtime.dockSetVisible?.("timelineDock", true);
      }
      options.onExternalChanged?.("timelineDock");
    }
  });

  return {
    externalDocks,
    openWindows,
    isExternal: (dockId) => externalDocks.has(dockId),
    onDockChanged,
    handleAction,
    destroy() {
      unsubHost?.();
      if (channel && onChannelMsg && typeof channel.removeEventListener === "function") {
        channel.removeEventListener("message", onChannelMsg);
      }
    },
  };
}
