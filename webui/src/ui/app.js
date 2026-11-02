// Joins the drawn panels to the backend: keeps the latest value of every state
// key, hands each panel what it shows, and turns what the user does into calls.
import { BoardRenderer } from "../board/renderer.ts";
import { GlBoardRenderer } from "../board/glrenderer.ts";
import { attachBoardInput } from "../board/input.ts";
import { cursorCss } from "../board/cursor.ts";
import { label, language, LANGUAGES, onLanguage, setLanguage, t } from "../i18n/index.js";
import { askForMenu, createCentral, createLayers, createMenuBar, popupMenu, scrollArea, tabBar } from "./panels.js";
import { createToolOptions } from "./options.js";
import { createPalette, toHexArgb } from "./palette.js";
import { createAssets, createForcePad, createHistory, createNewProjectDialog, createPythonDebug, createSubControlFrame, createTexturePanel, messageBox } from "./panels2.js";
import { createTimeline } from "./timeline.js";
import { asksForCanvasOnStartup } from "./startup.js";

export function warningTitleForCall(method, params = {}) {
  const view = params?.view ?? "main";
  if (method === "project.open") return view === "child" ? "Open Texture View" : "Open Project";
  if (method === "project.save" || method === "project.saveAs") {
    return view === "child" ? "Save Texture View" : "Save Project";
  }
  if (method === "menu.trigger") {
    if (params?.id === "file.save") return "Save Project";
    if (params?.id === "file.texture.save") return "Save Texture View";
    return null;
  }
  if (method === "file.import") {
    const kind = String(params?.kind ?? params?.action ?? "").trim().toLowerCase().replace(/^(file\.)?import\./, "");
    if (kind === "raster" || kind === "image") return "Import Raster";
    if (kind === "opentoonz" || kind === "pli") return "Import OpenToonz Lines";
    if (kind === "csp" || kind === "clip") return "Import Clip Studio Paint";
    return null;
  }
  if (method === "file.texture") {
    const kind = String(params?.kind ?? params?.action ?? "")
      .trim()
      .toLowerCase()
      .replace(/^(file\.)?texture\./, "")
      .replace(/^import\./, "");
    if (kind === "open" || kind === "opentextureview") return "Open Texture View";
    if (kind === "save" || kind === "savetextureview" || kind === "saveas" || kind === "save_as" || kind === "savetextureviewas") {
      return "Save Texture View";
    }
    if (kind === "exportimage" || kind === "export_image" || kind === "export" || kind === "exporttextureimage" || kind === "png") {
      return "Export Texture View Image";
    }
    if (kind === "raster" || kind === "image") return "Import Raster";
    if (kind === "opentoonz" || kind === "pli") return "Import OpenToonz Lines";
    if (kind === "csp" || kind === "clip") return "Import Clip Studio Paint";
  }
  return null;
}

function fileNameOf(path) {
  const s = String(path ?? "").trim();
  if (!s) return "";
  const i = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"));
  return i >= 0 ? s.slice(i + 1) : s;
}

function migrateExtension(name, ext) {
  if (!name) return "";
  const dot = name.lastIndexOf(".");
  if (dot <= 0) return `${name}.${ext}`;
  if (name.slice(dot + 1).toLowerCase() === "json") return `${name.slice(0, dot)}.${ext}`;
  return name;
}

export function defaultSaveName(kind, windowState = {}) {
  if (kind === "project" || kind === "saveProjectAs") {
    const name = fileNameOf(windowState?.currentFilePath);
    return name ? migrateExtension(name, "anproj") : "";
  }
  if (kind === "texture" || kind === "saveAs" || kind === "save") {
    const name = fileNameOf(windowState?.childFilePath);
    return name ? migrateExtension(name, "textureview") : "texture.textureview";
  }
  if (kind === "exportImage") {
    const name = fileNameOf(windowState?.childFilePath);
    if (!name) return "texture.png";
    const dot = name.lastIndexOf(".");
    const base = dot > 0 ? name.slice(0, dot) : name;
    return `${base || "texture"}.png`;
  }
  return "";
}

export function isAllowedWhenFrozen(target, pythonDebugEl) {
  if (!target || typeof target !== "object") return false;
  if (pythonDebugEl && (target === pythonDebugEl || pythonDebugEl.contains?.(target))) return true;
  if (typeof target.closest === "function") {
    return Boolean(target.closest('[data-vf-id="pythonDebugDock"], .debug-page, [data-python-debug="true"]'));
  }
  return false;
}

function fail(error, title) {
  console.error(error);
  if (title) {
    const text = error instanceof Error ? error.message : String(error ?? "");
    messageBox({ title, text });
  }
}

export function createStore(client) {
  const values = new Map();
  const listeners = new Map();
  const on = (key, fn) => {
    if (!listeners.has(key)) {
      listeners.set(key, []);
      client.onState(key, (value) => {
        values.set(key, value);
        for (const l of listeners.get(key)) l(value);
      });
    }
    listeners.get(key).push(fn);
    if (values.has(key)) fn(values.get(key));
  };
  const call = (method, params = {}) => client.call(method, params).catch((error) => fail(error, warningTitleForCall(method, params)));
  const cursors = new Map();
  const images = new Map();
  const imageListeners = new Set();
  client.onImage((meta, data) => {
    if (!meta.key.startsWith("cur_") && !meta.key.startsWith("thumb_")) return;
    const canvas = document.createElement("canvas");
    canvas.width = meta.w;
    canvas.height = meta.h;
    const pixels = new ImageData(new Uint8ClampedArray(data), meta.w, meta.h);
    canvas.getContext("2d").putImageData(pixels, 0, 0);
    const url = canvas.toDataURL();
    if (meta.key.startsWith("cur_")) cursors.set(meta.key, url);
    images.set(meta.key, url);
    for (const fn of imageListeners) fn(meta.key, url);
  });
  return {
    on,
    call,
    get: (key) => values.get(key),
    cursorUrl: (key) => cursors.get(key),
    imageUrl: (key) => images.get(key),
    forgetImage: (key) => images.delete(key),
    onImage: (fn) => { imageListeners.add(fn); return () => imageListeners.delete(fn); },
  };
}

const activeView = (store) => store.get("window")?.activeView ?? "main";
const assetView = (store) => store.get("window")?.assetView ?? store.assetView ?? activeView(store);

// Which scene dock shows which backend window.
export const DOCK_FOR_WINDOW = {
  tools: "toolsDock",
  tool_options: "toolOptionsDock",
  layers: "layersDock",
  assets: "assetsDock",
  history: "historyDock",
  repulsion_pad: "forcePadDock",
  python_debug: "pythonDebugDock",
};

// Panels keyed by the component type the scene names.
export function panelFactories(store, client, { satellite = false } = {}) {
  const { on, call } = store;
  // The transport sits under the canvas while the strip is docked to a side. The
  // scene decides which panel is built first, so neither can ask the other for it.
  const bottomChrome = document.createElement("div");
  bottomChrome.className = "central-bottom";

  const parkedHost = document.createElement("div");
  parkedHost.style.display = "none";

  const textureRoot = document.createElement("div");
  const triggerTextureMenu = (item) => {
    if (!item?.id) return;
    call("menu.trigger", {
      id: item.id,
      title: item.text,
      checked: item.kind === "check" ? !item.checked : item.kind === "radio" ? true : undefined,
    });
  };
  const texturePanel = createTexturePanel(textureRoot, {
    onTrigger: triggerTextureMenu,
    onScroll: (axis, value) => call("board.scroll", { view: "child", axis, value }),
    onButton: (name, onVal) =>
      Promise.resolve(call("texture.viewButton", { view: "child", name, on: onVal })).then((reply) => {
        if (Array.isArray(reply?.viewButtons)) texturePanel.update({ buttons: reply.viewButtons });
      }),
  });
  parkedHost.append(textureRoot);

  const subframeRoot = document.createElement("div");
  subframeRoot.style.display = "none";
  let placement = "parked"; // "parked" | "embedded" | "floating"
  let hostName = null;      // "options" | "tools" | null
  let lastHost = null;      // "options" | "tools" | null
  let floatingPos = { x: 180, y: 120 };
  let floatingSize = { width: 320, height: 260 };
  let dragOffset = { x: 0, y: 0 };
  let currentHome = "";
  let centralPanel = null;
  let optionsPanel = null;
  let optionsEl = null;
  let optionsExtraSlot = null;
  let toolsSlot = null;
  let boards = null;

  const getToolsEl = () =>
    store.toolsHostEl ??
    (typeof document !== "undefined" && typeof document.querySelector === "function"
      ? document.querySelector('[data-vf-id="toolsDock"], [data-vf-id="toolsScroll"]')
      : null);

  const isHostExternal = (targetHost) => {
    const dockId = targetHost === "options" ? "toolOptionsDock" : targetHost === "tools" ? "toolsDock" : "";
    return Boolean(dockId && store.externalDocks?.has?.(dockId));
  };

  // A panel out in its own window has no room for the board here; a hidden one still does.
  const canHost = (targetHost) =>
    !isHostExternal(targetHost) && Boolean(targetHost === "options" ? optionsEl : getToolsEl());

  const isHostVisible = (targetHost) => {
    if (isHostExternal(targetHost)) return false;
    const wins = store.get("windows");
    if (targetHost === "options") {
      if (!optionsEl || optionsEl.style?.display === "none") return false;
      return wins?.tool_options?.visible !== false;
    }
    if (targetHost === "tools") {
      const tEl = getToolsEl();
      if (!tEl || tEl.style?.display === "none") return false;
      return wins?.tools?.visible !== false;
    }
    return false;
  };

  const isFrameLive = () => {
    if (placement === "floating") return subframeRoot.style?.display !== "none";
    if (placement === "embedded") return isHostVisible(hostName);
    return false;
  };

  const placeholderNote = () =>
    centralPanel && centralPanel.current() === "texture"
      ? "Open in the main view"
      : "The texture board is parked";

  const hitRect = (el, x, y) => {
    if (!el || typeof el.getBoundingClientRect !== "function") return null;
    const r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return null;
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom ? r : null;
  };

  const findDropHost = (x, y) => {
    if (isHostVisible("options") && hitRect(optionsEl, x, y)) return "options";
    const tEl = getToolsEl();
    if (isHostVisible("tools") && hitRect(tEl, x, y)) return "tools";
    return null;
  };

  const dropPreviewRect = (targetHost) => {
    const el = targetHost === "options" ? optionsEl : getToolsEl();
    if (!el || typeof el.getBoundingClientRect !== "function") return null;
    const r = el.getBoundingClientRect();
    const h = Math.min(Math.max(120, Math.round(r.height * 0.45)), Math.max(40, r.height - 16));
    return {
      x: Math.round(r.left + 6),
      y: Math.round(Math.max(r.top + 6, r.bottom - h - 6)),
      width: Math.max(0, Math.round(r.width - 12)),
      height: Math.max(0, h),
    };
  };

  const subframe = createSubControlFrame(subframeRoot, {
    onAffordance: () => toggleFloating(),
    onDragStart: (pressX, pressY) => {
      const r = subframeRoot.getBoundingClientRect();
      const w = r.width > 0 ? r.width : floatingSize.width;
      const h = r.height > 0 ? r.height : floatingSize.height;
      dragOffset = {
        x: r.width > 0 ? pressX - r.left : Math.round(w * 0.5),
        y: r.height > 0 ? pressY - r.top : 10,
      };
      if (placement !== "floating") {
        floatFrame({ x: pressX - dragOffset.x, y: pressY - dragOffset.y });
      }
    },
    onDragMove: (x, y) => {
      floatingPos = { x: Math.round(x - dragOffset.x), y: Math.round(y - dragOffset.y) };
      subframeRoot.style.left = `${floatingPos.x}px`;
      subframeRoot.style.top = `${floatingPos.y}px`;
      const candidate = findDropHost(x, y);
      subframe.preview(candidate ? dropPreviewRect(candidate) : null);
    },
    onDragEnd: (x, y) => {
      subframe.preview(null);
      const candidate = x === null ? null : findDropHost(x, y);
      if (candidate) embedIn(candidate);
    },
    onResize: (w, h) => {
      floatingSize = { width: w, height: h };
      if (currentHome === "control") boards?.resendChildSize?.();
    },
  });
  parkedHost.append(subframeRoot);

  // The first placement happens while the scene is built, before there is a
  // connection; main.ts says it again once there is one.
  function announceTextureHome() {
    if (satellite || client.connected === false) return;
    Promise.resolve(call("texture.setHome", { home: currentHome, subControlVisible: isFrameLive() })).then((reply) => {
      if (Array.isArray(reply?.viewButtons)) {
        texturePanel.update({ buttons: reply.viewButtons });
      }
    });
  }

  function updateTextureHome() {
    // Its panel left for a window of its own: the board stays here, in the open.
    if (placement === "embedded" && isHostExternal(hostName)) {
      floatFrame();
      return;
    }
    const centralWantsIt = Boolean(centralPanel && centralPanel.current() === "texture");
    const controlWantsIt = !centralWantsIt && isFrameLive();
    const wanted = centralWantsIt ? "central" : controlWantsIt ? "control" : "parked";
    subframe.update({
      title: "Texture",
      floating: placement === "floating",
      placeholder: placeholderNote(),
    });
    if (wanted !== currentHome) {
      currentHome = wanted;
      if (wanted === "central" && centralPanel) {
        subframe.setContent(null);
        centralPanel.setTextureContent(textureRoot);
      } else if (wanted === "control") {
        centralPanel?.setTextureContent(null);
        subframe.setContent(textureRoot);
      } else {
        centralPanel?.setTextureContent(null);
        subframe.setContent(null);
        parkedHost.append(textureRoot);
      }
      announceTextureHome();
      if (wanted !== "parked") {
        boards?.resendChildSize?.();
      }
    } else {
      subframe.syncPlaceholder?.();
    }
  }

  function floatFrame(pos) {
    const wasInOptions = placement === "embedded" && hostName === "options";
    if (placement === "embedded" && hostName) lastHost = hostName;
    placement = "floating";
    hostName = null;
    if (pos) {
      floatingPos = { x: Math.round(pos.x), y: Math.round(pos.y) };
    } else if (typeof window !== "undefined" && window.innerWidth > 0) {
      // Back where it was last dropped, which may be a panel at the window's edge.
      floatingPos = {
        x: Math.max(0, Math.min(floatingPos.x, window.innerWidth - floatingSize.width)),
        y: Math.max(0, Math.min(floatingPos.y, window.innerHeight - floatingSize.height)),
      };
    }
    subframeRoot.style.display = "";
    subframeRoot.style.left = `${floatingPos.x}px`;
    subframeRoot.style.top = `${floatingPos.y}px`;
    subframeRoot.style.width = `${floatingSize.width}px`;
    subframeRoot.style.height = `${floatingSize.height}px`;
    const floatContainer =
      (typeof document !== "undefined" && typeof document.getElementById === "function" && document.getElementById("app")) ||
      (typeof document !== "undefined" ? document.body : null);
    floatContainer?.append(subframeRoot);
    if (wasInOptions && optionsPanel?.hasSubWindow("texture_view")) {
      optionsPanel.refreshSubWindows();
    }
    updateTextureHome();
  }

  function embedIn(targetHost, gridSlotEl = null) {
    placement = "embedded";
    hostName = targetHost;
    lastHost = targetHost;
    subframeRoot.style.display = "";
    subframeRoot.style.left = "";
    subframeRoot.style.top = "";
    subframeRoot.style.width = "";
    subframeRoot.style.height = "";
    if (gridSlotEl) {
      gridSlotEl.replaceChildren(subframeRoot);
    } else if (targetHost === "options") {
      if (optionsPanel?.hasSubWindow("texture_view")) {
        optionsPanel.refreshSubWindows();
        return;
      }
      optionsExtraSlot?.replaceChildren(subframeRoot);
    } else if (targetHost === "tools") {
      const tEl = getToolsEl();
      if (tEl) {
        if (!toolsSlot || toolsSlot.parentElement !== tEl) {
          toolsSlot = document.createElement("div");
          toolsSlot.className = "tools-subcontrol-slot";
          tEl.append(toolsSlot);
        }
        toolsSlot.replaceChildren(subframeRoot);
      }
    }
    updateTextureHome();
  }

  // Putting the frame away also forgets where it came from: the next surface floats
  // it rather than re-entering a panel the user has finished with.
  function stowFrame() {
    lastHost = null;
    placement = "parked";
    hostName = null;
    subframeRoot.style.display = "none";
    parkedHost.append(subframeRoot);
  }

  function park() {
    stowFrame();
    updateTextureHome();
  }

  function toggleFloating() {
    if (placement === "floating") {
      if (lastHost && canHost(lastHost)) embedIn(lastHost);
      else park();
      return;
    }
    floatFrame();
  }

  function surfaceSubControl() {
    if (placement === "parked") {
      floatFrame();
    } else if (placement === "embedded") {
      if (!isHostVisible(hostName)) {
        const winId = hostName === "options" ? "windows.tool_options" : "windows.tools";
        if (canHost(hostName)) {
          call("menu.trigger", { id: winId, checked: true });
        } else {
          floatFrame();
        }
      }
    } else {
      subframeRoot.style.display = "";
      subframeRoot.parentElement?.append(subframeRoot);
    }
    updateTextureHome();
    if (currentHome === "control") {
      call("view.setActive", { view: "child" });
    }
  }

  function surfaceTextureView() {
    if (!isFrameLive() && centralPanel) {
      centralPanel.show(1);
      boards?.show("child");
    }
    updateTextureHome();
  }

  on("textureMenus", (value) => {
    const menus = Array.isArray(value) ? value : value?.menus;
    const buttons = Array.isArray(value?.buttons) ? value.buttons : undefined;
    texturePanel.update({ menus, ...(buttons !== undefined ? { buttons } : {}) });
  });
  on("viewport/child", (value) => {
    if (!value) return;
    texturePanel.h.set(value.h);
    texturePanel.v.set(value.v);
  });
  on("windows", (value) => {
    if (Array.isArray(value?.textureViewButtons)) {
      texturePanel.update({ buttons: value.textureViewButtons });
    }
    updateTextureHome();
  });
  if (!satellite) {
    on("event", (event) => {
      if (event?.name === "textureSubControl.surface") {
        surfaceSubControl();
      } else if (event?.name === "textureView.surface") {
        surfaceTextureView();
      } else if (event?.name === "window.select" && event.args?.name === "paint") {
        const child = event.args.page === "texture";
        centralPanel?.show(child ? 1 : 0);
        boards?.show(child ? "child" : "main");
        updateTextureHome();
      }
    });
  }

  store.texture = {
    panel: texturePanel,
    frame: subframe,
    get home() { return currentHome; },
    get placement() { return placement; },
    get host() { return hostName; },
    get lastHost() { return lastHost; },
    isFloating: () => placement === "floating",
    isEmbedded: () => placement === "embedded",
    isLive: isFrameLive,
    floatFrame,
    embedIn,
    park,
    toggleFloating,
    surface: surfaceSubControl,
    updateHome: updateTextureHome,
    announceHome: announceTextureHome,
  };

  return {
    QtToolOptions(el) {
      el.classList.add("dock-page", "toolopt-gap");
      optionsEl = el;
      const tabs = tabBar(["Options"], () => {});
      tabs.select(0);
      const body = scrollArea("opt-body");
      optionsExtraSlot = document.createElement("div");
      optionsExtraSlot.className = "opt-subcontrol-slot";
      el.append(tabs.el, body.frame);
      const panel = createToolOptions(body.view, {
        onOption: (option) => call("tool.option", option),
        onNewColor: (palette) => openNewColor(palette),
        onBeforeBuild: () => {
          if (placement === "embedded" && hostName === "options") stowFrame();
        },
        onSubWindow: () => {
          if (satellite) return subWindowFloatingNote("Texture is in the main window");
          if (placement !== "floating" && isHostExternal("options")) floatFrame();
          if (placement === "floating") return subWindowFloatingNote();
          const slot = document.createElement("div");
          slot.className = "opt-subwindow-cell";
          embedIn("options", slot);
          return slot;
        },
        onAfterBuild: () => {
          body.view.append(optionsExtraSlot);
          updateTextureHome();
        },
      });
      optionsPanel = panel;
      on("toolOptions", (value) => panel.build(value));
    },

    QtLayers(el) {
      const panel = createLayers(el, {
        onAdd: (view) => call("layer.add", { view }),
        onRemove: (view, current) => {
          if (current?.group && current.group_id > 0) {
            call("layer.deleteGroup", { view, groupId: current.group_id });
            return;
          }
          const layerIndex = current !== undefined ? (current?.layer ?? -1) : (store.get(`layers/${view}`)?.selected ?? -1);
          if (layerIndex >= 0) call("layer.delete", { view, layerIndex });
        },
        onClear: (view) => call("layer.unselect", { view }),
        onSelect: (view, layerIndex) => call("layer.select", { view, layerIndex }),
        onVisible: (view, layerIndex, visible) => call("layer.setVisible", { view, layerIndex, visible }),
        onCollapse: (view, groupId, collapsed) => call("layer.setCollapsed", { view, groupId, collapsed }),
        onApplyStructure: (view, rows, movedLayer) => call("layer.applyStructure", { view, rows, movedLayer }),
        onMove: (view, from, to) => {
          if (from.layer !== undefined && to.layer !== undefined) call("layer.move", { view, fromIndex: from.layer, toIndex: to.layer });
        },
        assetTarget: () => assetView(store),
        onDropAsset: (view, assetIndex) => {
          if (view === assetView(store) && assetIndex >= 0) {
            call("asset.addLayer", { view, assetIndex });
          }
        },
        onContext: async (view, row, members, x, y) => {
          const target = {
            view,
            groupId: row?.group ? row.group_id : 0,
            groupName: row?.group ? row.name : "",
            layer: row && !row.group ? row.layer : -1,
            members,
          };
          const wanted = askForMenu();
          const reply = await call("layer.contextMenu", target);
          if (!reply?.items?.length || !wanted()) return;
          popupMenu(reply.items, x, y, (item) => call("layer.contextAction", { ...target, id: item.id, title: item.text }));
        },
      });
      on("layers/main", (value) => panel.update("main", value));
      on("layers/child", (value) => panel.update("child", value));
    },

    QtAssets(el) {
      const panel = createAssets(el, {
        onSelect: (assetIndex) => call("asset.select", { assetIndex, view: assetView(store) }),
        onAdd: () => call("asset.add", { view: assetView(store) }),
        onClear: () => call("asset.unselect", { view: assetView(store) }),
      });
      const show = () => {
        const view = assetView(store);
        store.assetView = view;
        const state = store.get(`assets/${view}`);
        panel.update({ names: state?.names ?? [], selected: state?.selected ?? -1 });
      };
      on("assets/main", show);
      on("assets/child", show);
      on("window", show);
    },

    QtHistory(el) {
      const panel = createHistory(el, { onSelect: (row) => call("history.goTo", { row }) });
      on("history", (value) => panel.update({ labels: value?.labels ?? [], current: value?.currentIndex ?? -1 }));
    },

    QtForcePad(el) {
      const send = (phase) => (x, y) => call("pad.event", { pad: "force_pad", phase, x, y });
      const panel = createForcePad(el, { onPress: send("press"), onMove: send("move"), onRelease: send("release") });
      on("pad", (value) => panel.update({ value: { x: value?.x ?? 0, y: value?.y ?? 0 } }));
    },

    QtPythonDebug(el) {
      if (el.dataset) el.dataset.pythonDebug = "true";
      store.pythonDebugEl = el;
      const panel = createPythonDebug(el, {
        onRun: async (command) => {
          const reply = await call("python.run", { command });
          if (reply?.output) panel.append(reply.output);
        },
      });
      on("window", (value) => panel.update({ output: (value?.pythonDebug ?? []).join("\n") }));
    },

    QtTimeline(el) {
      let sideArea = "right";
      const handlers = ({
        onAddFrame: () => call("frame.add"),
        onAddHold: () => call("frame.addHold"),
        onDuplicate: () => call("frame.duplicate"),
        onDeleteFrame: () => call("frame.delete"),
        onOnion: (on) => call("onion.setEnabled", { on }),
        onOnionLines: (on) => call("onion.setLines", { on }),
        onOnionFills: (on) => call("onion.setFills", { on }),
        onOnionAmLayers: (on) => call("onion.setAmLayers", { on }),
        onGuideLines: (on) => call("onion.setGuideLines", { on }),
        onPrev: () => call("frame.step", { delta: -1 }),
        onNext: () => call("frame.step", { delta: 1 }),
        onPlay: () => call("playback.start"),
        onPause: () => call("playback.stop"),
        onLoop: (on) => call("playback.setLoop", { loop: on }),
        onRate: (fps) => call("frame.setFps", { fps }),
        onClose: () => call("menu.trigger", { id: "windows.timeline" }),
        onCollapse: (collapsed) => call("timeline.setCollapsed", { collapsed }),
        onFloat: (floating) => call("timeline.setFloating", { floating }),
        onOrientation: (vertical) => call("timeline.setDockArea", { dockArea: vertical ? sideArea : "bottom", floating: false }),
        onSelectFrame: (frameIndex) => call("frame.select", { frameIndex }),
        onMoveFrame: (fromIndex, toIndex) => call("frame.move", { fromIndex, toIndex }),
        onFrameTyped: (frameIndex) => call("frame.select", { frameIndex }),
        onLaneToggled: (frame, on) => call("onion.toggleFrame", { frame, on }),
        onPreview: (frames, size) => {
          shownThumbs = { frames, size };
          refreshThumbnails();
        },
      });
      const panel = createTimeline(el, handlers, { bottomChrome });
      store.timeline = panel;
      const thumbKeys = new Map();
      const thumbsAsked = new Set();
      let shownThumbs = { frames: [], size: null };
      let thumbsFor = "";
      const applyTimeline = (value = store.get("timeline")) => {
        if (!value) return;
        if (value.dockArea === "left" || value.dockArea === "right") sideArea = value.dockArea;
        else if (value.vertical) sideArea = value.leftAligned ? "left" : "right";
        panel.update({
          frames: (value.frames ?? []).map((f, i) => {
            const idx = f.index ?? i;
            const key = thumbKeys.get(idx);
            const thumbnail = f.thumbnail ?? (key ? store.imageUrl?.(key) : undefined);
            return { index: idx, name: f.name, held: f.hold, ...(thumbnail ? { thumbnail } : {}) };
          }),
          current: value.currentFrame,
          fps: value.fps,
          playing: value.playing,
          loop: value.loop,
          onion: value.onionEnabled,
          onionLines: value.onionLines,
          onionFills: value.onionFills,
          onionAmLayers: value.onionAmLayers,
          guideLines: value.onionGuideLines,
          onionAvailable: value.onionAvailable,
          lanes: value.onionFrames ?? [],
          vertical: Boolean(value.vertical),
          leftAligned: Boolean(value.leftAligned),
          collapsed: Boolean(value.collapsed),
          floating: Boolean(value.floating),
        });
      };
      // TimelineStrip::thumbnail: a frame on screen is rendered once per drawing and size.
      const refreshThumbnails = async () => {
        const value = store.get("timeline");
        if (!value || !shownThumbs.size) return;
        const { width, height } = shownThumbs.size;
        const owner = [value.view, store.get("thumbnails")?.revision, width, height].join();
        if (owner !== thumbsFor) {
          thumbsFor = owner;
          thumbsAsked.clear();
        }
        const frames = shownThumbs.frames.filter((frame) => !thumbsAsked.has(frame));
        if (!frames.length) return;
        for (const frame of frames) thumbsAsked.add(frame);
        const reply = await call("timeline.thumbnails", { frames, width, height });
        if (owner !== thumbsFor || !reply?.thumbnails) return;
        for (const [frame, key] of Object.entries(reply.thumbnails)) {
          const old = thumbKeys.get(Number(frame));
          if (old && old !== key) store.forgetImage?.(old);
          thumbKeys.set(Number(frame), key);
        }
        applyTimeline();
      };
      store.onImage?.((key) => {
        if (key.startsWith("thumb_")) applyTimeline();
      });
      on("timeline", (value) => {
        if (value) applyTimeline(value);
      });
      on("thumbnails", () => refreshThumbnails());
    },

    QtCentral(el) {
      el.classList.add("central");
      const panel = createCentral(el, {
        bottomChrome,
        onPage: (view) => {
          boards?.show(view);
          updateTextureHome();
          call("view.setActive", { viewName: view });
        },
        onScroll: (axis, value) => call("board.scroll", { view: "main", axis, value }),
        onPill: () => call("menu.trigger", { id: "windows.timeline" }),
      });
      centralPanel = panel;
      boards = createBoards(panel.host, texturePanel.host, client, store);
      store.boards = boards;
      store.boardsHost = panel.host;
      on("windows", (value) => {
        const visible = Boolean(value?.timeline?.visible);
        panel.pill.style.display = visible ? "none" : "";
        bottomChrome.hidden = !visible;
      });
      on("viewport/main", (value) => {
        if (!value) return;
        panel.h.set(value.h);
        panel.v.set(value.v);
      });
      updateTextureHome();
    },
  };
}

function subWindowFloatingNote(text = "Texture is floating — drag it back") {
  const note = document.createElement("div");
  note.className = "opt-title";
  return label(note, text);
}

export function openNewColor(palette) {
  const shade = document.createElement("div");
  shade.className = "qdialog-shade";
  const dialog = document.createElement("div");
  dialog.className = "qdialog newcolor-dialog";
  const inner = createPalette(() => {}, { pickersOnly: true });
  const row = document.createElement("div");
  row.className = "qdialog-buttons";
  Object.assign(row.style, { display: "flex", justifyContent: "flex-end", gap: "6px" });
  const add = document.createElement("button");
  add.type = "button";
  add.className = "qbutton qdefault";
  label(add, "Add");
  const cancel = document.createElement("button");
  cancel.type = "button";
  cancel.className = "qbutton";
  label(cancel, "Cancel");
  const close = () => shade.remove();
  const accept = () => { palette.addSwatch(toHexArgb(inner.color())); close(); };
  add.addEventListener("click", accept);
  cancel.addEventListener("click", close);
  shade.addEventListener("pointerdown", (e) => { if (e.target === shade) close(); });
  dialog.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); accept(); }
    if (e.key === "Escape") { e.preventDefault(); close(); }
  });
  row.append(add, cancel);
  dialog.append(inner.el, row);
  shade.append(dialog);
  document.body.append(shade);
  inner.update({ value: toHexArgb(palette.color()), swatches: [] });
}

// The board renderer is a per-machine choice, so it lives in the page's own
// storage rather than in the document or the backend.
const RENDERER_KEY = "animean.boardRenderer";

function savedRendererKind() {
  try {
    return localStorage.getItem(RENDERER_KEY) === "canvas2d" ? "canvas2d" : "webgl";
  } catch {
    return "webgl";
  }
}

function saveRendererKind(kind) {
  try {
    localStorage.setItem(RENDERER_KEY, kind);
  } catch (error) {
    fail(error);
  }
}

// Asking opens a real context, and the browser only allows a handful.
let webglSupported;
function hasWebgl() {
  webglSupported ??= GlBoardRenderer.isSupported();
  return webglSupported;
}

function boardCanvas() {
  const canvas = document.createElement("canvas");
  canvas.className = "board-canvas";
  return canvas;
}

// A canvas that has handed out one kind of context can never give the other,
// so a failed WebGL start and every switch begin on a fresh canvas.
function startRenderer(kind, options) {
  if (kind === "webgl" && hasWebgl()) {
    const canvas = boardCanvas();
    try {
      return { canvas, renderer: new GlBoardRenderer(canvas, options), kind };
    } catch (error) {
      fail(error);
    }
  }
  const canvas = boardCanvas();
  return { canvas, renderer: new BoardRenderer(canvas, options), kind: "canvas2d" };
}

function createBoards(mainHost, childHost, client, store) {
  const slots = {
    main: { view: "main", host: mainHost },
    child: { view: "child", host: childHost ?? mainHost },
  };
  const mount = (slot, kind) => {
    const { canvas, renderer, kind: started } = startRenderer(kind, { view: slot.view, client });
    const hadFocus = slot.canvas && document.activeElement === slot.canvas;
    if (slot.canvas) slot.canvas.replaceWith(canvas);
    else slot.host.append(canvas);
    if (slot.renderer) {
      renderer.adopt(slot.renderer);
      slot.renderer.destroy();
      slot.input.destroy();
    }
    Object.assign(slot, { canvas, renderer, input: attachBoardInput(canvas, { view: slot.view, client }) });
    if (hadFocus) canvas.focus({ preventScroll: true });
    paintCursor(slot);
    client?.call?.("board.capabilities", { view: slot.view, fieldDisplay: started === "webgl" })?.catch(() => {});
    return started;
  };
  const paintCursor = (slot) => {
    const value = store.get(`cursor/${slot.view}`);
    slot.canvas.style.cursor = cursorCss(value, value?.key ? store.cursorUrl(value.key) : null);
  };
  const boards = {
    current: "main",
    slots,
    rendererKind: savedRendererKind(),
    resendSizes: () =>
      Object.values(slots).forEach((slot) => {
        slot.input.resend?.();
        client?.call?.("board.capabilities", { view: slot.view, fieldDisplay: boards.rendererKind === "webgl" })?.catch(() => {});
      }),
    resendChildSize: () => slots.child.input.resend?.(),
    show(view) {
      boards.current = view;
      slots.main.canvas.style.visibility = "visible";
      slots.child.canvas.style.visibility = "visible";
    },
    setRenderer(kind) {
      boards.rendererKind = mount(slots.main, kind);
      mount(slots.child, kind);
      saveRendererKind(boards.rendererKind);
      boards.show(boards.current);
    },
  };
  boards.rendererKind = mount(slots.main, boards.rendererKind);
  mount(slots.child, boards.rendererKind);
  for (const slot of Object.values(slots)) store.on(`cursor/${slot.view}`, () => paintCursor(slot));
  // As in Qt (MainWindow::setActivePaintView), the board that becomes active takes
  // the keyboard, so its frame lights up and the focus is on the board the paint
  // window's page names. A dialog keeps the keys it has.
  let activeSeen = null;
  const dialogOpen = () =>
    [...document.querySelectorAll(".qdialog-shade")].some((s) => typeof s.getClientRects !== "function" || s.getClientRects().length > 0);
  store.on("window", (value) => {
    const view = value?.activeView;
    if (!slots[view] || view === activeSeen) return;
    const first = activeSeen === null;
    activeSeen = view;
    const canvas = slots[view].canvas;
    if (first || typeof document === "undefined" || document.activeElement === canvas) return;
    if (typeof canvas.getClientRects === "function" && canvas.getClientRects().length === 0) return;
    if (dialogOpen()) return;
    canvas.focus?.({ preventScroll: true });
  });
  boards.show("main");
  return boards;
}

// Keeps scene docks in step with the backend's window list.
export function syncDocks(runtime, store, isExternal) {
  const skip = (dock) => (typeof isExternal === "function" ? isExternal(dock) : isExternal?.has?.(dock)) || Boolean(runtime?.externalDocks?.has?.(dock));
  store.on("windows", (value) => {
    for (const [name, dock] of Object.entries(DOCK_FOR_WINDOW)) {
      if (skip(dock)) continue;
      runtime.dockSetVisible(dock, Boolean(value?.[name]?.visible));
    }
    if (!skip("timelineDock")) runtime.dockSetVisible("timelineDock", Boolean(value?.timeline?.visible));
  });
}

// The chrome outside the docked scene: menu bar and status bar.
export function createChrome(menuEl, statusEl, store, options = {}) {
  const isSatellite = Boolean(options?.satellite);
  let frozen = false;
  const RENDERER_ITEM = "settings.renderer.";
  const LANGUAGE_ITEM = "settings.language.";
  const trigger = (item) => {
    if (frozen) return undefined;
    if (item.id?.startsWith(RENDERER_ITEM)) {
      store.boards?.setRenderer(item.id.slice(RENDERER_ITEM.length));
      paintMenus();
      return undefined;
    }
    if (item.id?.startsWith(LANGUAGE_ITEM)) {
      setLanguage(item.id.slice(LANGUAGE_ITEM.length));
      return undefined;
    }
    return store.call("menu.trigger", {
      id: item.id,
      title: item.text,
      checked: item.kind === "check" ? !item.checked : item.kind === "radio" ? true : undefined,
    });
  };
  const bar = isSatellite || !menuEl ? null : createMenuBar(menuEl, {
    onTrigger: trigger,
    onOpen: async () => {
      if (frozen) return;
      const reply = await store.call("menu.refresh", {});
      if (Array.isArray(reply?.menus)) {
        bar.update([...reply.menus, settingsMenu()]);
      }
    },
  });
  // The renderer and the language belong to this page, not the document, so
  // their menus are added here rather than coming from the backend's menu state.
  const settingsMenu = () => {
    const kind = store.boards?.rendererKind ?? savedRendererKind();
    return {
      id: "settings",
      text: "Settings",
      kind: "submenu",
      children: [{
        id: "settings.renderer",
        text: "Board Renderer",
        kind: "submenu",
        children: [
          { id: `${RENDERER_ITEM}webgl`, text: "WebGL", kind: "radio", checked: kind === "webgl", enabled: hasWebgl() },
          { id: `${RENDERER_ITEM}canvas2d`, text: "Canvas 2D", kind: "radio", checked: kind === "canvas2d" },
        ],
      }, {
        id: "settings.language",
        text: "Language",
        kind: "submenu",
        children: LANGUAGES.map(({ id, name }) => ({ id: `${LANGUAGE_ITEM}${id}`, text: name, kind: "radio", checked: id === language() })),
      }],
    };
  };
  const paintMenus = () => bar?.update([...(store.get("menus") ?? []), settingsMenu()]);
  if (!isSatellite) store.on("menus", paintMenus);
  onLanguage(paintMenus);
  const text = document.createElement("div");
  text.className = "status-text";
  const grip = document.createElement("div");
  grip.className = "size-grip";
  if (!isSatellite && statusEl) statusEl.append(text, grip);
  // The line on show is kept in English, so a change of language can repaint it.
  let status = "";
  const showStatus = (source) => {
    status = source ?? "";
    text.textContent = t(status);
  };
  const showTitle = (title) => {
    if (title) window.__TAURI__?.core.invoke("set_window_title", { title: t(title) });
  };
  store.on("window", (value) => {
    if (isSatellite) return;
    if (!frozen) showStatus(value?.status);
    showTitle(value?.title);
  });
  onLanguage(() => {
    if (isSatellite) return;
    showStatus(status);
    showTitle(store.get("window")?.title);
  });
  // To 3D writes its page from Python, so Python has to hear the language too.
  // A panel in a window of its own shares the main window's backend and language.
  const tellPython = () => {
    if (!isSatellite) store.call("tool.option", { hook: "ui_language", name: "ui_language", type: "language", value: language() });
  };
  onLanguage(tellPython);
  store.on("freeze", (value) => {
    const next = Boolean(value?.frozen ?? value);
    if (next === frozen) return;
    frozen = next;
    showStatus(frozen ? "Python busy..." : "Python ready");
    if (menuEl) {
      menuEl.inert = frozen;
      if (menuEl.style) menuEl.style.pointerEvents = frozen ? "none" : "";
    }
    if (store.boardsHost?.style) {
      store.boardsHost.style.pointerEvents = frozen ? "none" : "";
    }
    if (store.texture?.panel?.host?.style) {
      store.texture.panel.host.style.pointerEvents = frozen ? "none" : "";
    }
    if (typeof document !== "undefined" && document.querySelectorAll) {
      for (const el of document.querySelectorAll(".qt-panel, .vf-dock, .settings-dialog, .subframe")) {
        if (isAllowedWhenFrozen(el, store.pythonDebugEl) || (store.pythonDebugEl && el.contains?.(store.pythonDebugEl))) {
          el.inert = false;
          if (el.style) el.style.pointerEvents = "";
          continue;
        }
        el.inert = frozen;
        if (el.style) el.style.pointerEvents = frozen ? "none" : "";
      }
    }
  });
  const blockIfFrozen = (e) => {
    if (!frozen) return;
    if (isAllowedWhenFrozen(e.target, store.pythonDebugEl)) return;
    e.stopImmediatePropagation?.();
    e.stopPropagation?.();
    e.preventDefault?.();
  };
  if (typeof window !== "undefined" && window.addEventListener) {
    for (const type of ["pointerdown", "pointermove", "pointerup", "mousedown", "mousemove", "mouseup", "click", "dblclick", "contextmenu", "wheel", "keydown", "keyup"]) {
      window.addEventListener(type, blockIfFrozen, true);
    }
  }
  store.on("theme", (value) => {
    document.documentElement.dataset.theme = value?.mode === "dark" ? "dark" : "light";
  });

  // Menu shortcuts: the menus state carries them, so Python-added ones work too.
  const shortcuts = new Map();
  const collect = (items) => {
    for (const item of items ?? []) {
      if (item.shortcut && item.enabled !== false) shortcuts.set(normalizeKey(item.shortcut), item);
      collect(item.children);
    }
  };
  const rebuildShortcuts = () => {
    shortcuts.clear();
    collect(store.get("menus"));
    collect(store.get("textureMenus"));
    // Redo has a second key that the menu does not print.
    if (shortcuts.has("ctrl+y")) shortcuts.set("ctrl+shift+z", shortcuts.get("ctrl+y"));
  };
  store.on("menus", rebuildShortcuts);
  store.on("textureMenus", rebuildShortcuts);
  window.addEventListener("keydown", (e) => {
    if (frozen) return;
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
    const item = shortcuts.get(keyName(e));
    if (item) { e.preventDefault(); trigger(item); }
  });

  const dialogRoot = document.createElement("div");
  if (isSatellite) return;
  document.body.append(dialogRoot);
  const canvasDialog = createNewProjectDialog(dialogRoot, {
    onCreate: (size) => store.call("project.new", size),
    onCancel: () => {},
  });
  canvasDialog.update({ open: false });
  // The Qt build opens on this dialog too (MainWindow::promptForNewCanvasOnStartup);
  // a backend already holding a project is not asked.
  let asked = false;
  store.on("window", (value) => {
    if (asked || !value?.canvasWidth) return;
    asked = true;
    tellPython();
    if (asksForCanvasOnStartup(value)) {
      canvasDialog.update({ width: value.canvasWidth, height: value.canvasHeight, open: true });
    }
  });

  // Native pickers live in the shell; the page only says what to pick.
  const pick = (mode, title, filters, multiple = false, defaultName = "") =>
    window.__TAURI__?.core.invoke("pick_files", {
      mode,
      title: t(title),
      filters: filters.map((filter) => ({ ...filter, name: t(filter.name) })),
      multiple,
      defaultName,
    }) ?? Promise.resolve([]);
  const PROJECT = [{ name: "AnimeAn Projects", extensions: ["anproj"] }];
  const TEXTURE = [{ name: "AnimeAn Texture Views", extensions: ["textureview"] }];
  const IMPORTS = {
    "file.import.raster": ["raster", "Import Raster", [{ name: "Images", extensions: ["png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"] }]],
    "file.import.opentoonz": ["opentoonz", "Import OpenToonz Lines", [{ name: "OpenToonz Vector Levels", extensions: ["pli"] }]],
    "file.import.csp": ["csp", "Import Clip Studio Paint", [{ name: "Clip Studio Paint", extensions: ["clip"] }]],
    "file.texture.import.raster": ["raster", "Import Raster", [{ name: "Images", extensions: ["png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"] }], "child"],
    "file.texture.import.opentoonz": ["opentoonz", "Import OpenToonz Lines", [{ name: "OpenToonz Vector Levels", extensions: ["pli"] }], "child"],
    "file.texture.import.csp": ["csp", "Import Clip Studio Paint", [{ name: "Clip Studio Paint", extensions: ["clip"] }], "child"],
  };
  const TEXTURE_ACTIONS = {
    "file.texture.open": ["open", "open", "Open Texture View", TEXTURE],
    "file.texture.save": ["saveAs", "save", "Save Texture View As", TEXTURE],
    "file.texture.saveAs": ["saveAs", "save", "Save Texture View As", TEXTURE],
    "file.texture.exportImage": ["exportImage", "save", "Export Texture View Image", [{ name: "PNG Image", extensions: ["png"] }]],
  };
  store.on("event", async (event) => {
    const args = event?.args ?? {};
    switch (event?.name) {
      case "contextMenu":
        popupMenu(args.items ?? [], args.x, args.y, trigger);
        break;
      case "settings.open":
        openSettings(store, args);
        break;
      case "dialog.canvasSize":
        canvasDialog.update({ width: args.width, height: args.height, open: true });
        break;
      case "dialog.openProject": {
        const [path] = await pick("open", "Open Project", PROJECT);
        if (path) store.call("project.open", { path });
        break;
      }
      case "dialog.saveProjectAs": {
        const [path] = await pick("save", "Save Project As", PROJECT, false, defaultSaveName("project", store.get("window")));
        if (!path) break;
        const probe = await store.call("project.saveAs", { path, probe: true });
        if (probe?.needsConfirmOverwrite) {
          const choice = await messageBox({
            title: "Save Project As",
            text: `${probe.fileName} already exists.\nDo you want to replace it?`,
            buttons: ["Yes", "No"],
          });
          if (choice !== "Yes") break;
        }
        await store.call("project.saveAs", { path, overwrite: true });
        break;
      }
      case "menu.action": {
        if (IMPORTS[args.id]) {
          const [kind, title, filters, view] = IMPORTS[args.id];
          const paths = await pick("open", title, filters);
          if (!paths.length) break;
          const baseParams = view ? { kind, paths, view } : { kind, paths };
          if (kind === "opentoonz" || kind === "csp") {
            const probe = await store.call("file.import", { ...baseParams, probe: true });
            if (!probe) break;
            let fitToCanvas = false;
            if (probe.needsScale) {
              const b = probe.sourceBounds ?? { x: 0, y: 0, width: 0, height: 0 };
              const choice = await messageBox({
                title,
                text: `The imported drawing bounds are (${b.x}, ${b.y}) ${b.width} x ${b.height}, but the current canvas is ${probe.canvasWidth} x ${probe.canvasHeight}.\nFit it to the current canvas?`,
                buttons: ["Yes", "No"],
              });
              fitToCanvas = choice === "Yes";
            }
            await store.call("file.import", { ...baseParams, fitToCanvas });
          } else {
            await store.call("file.import", baseParams);
          }
        } else if (TEXTURE_ACTIONS[args.id]) {
          const [kind, mode, title, filters] = TEXTURE_ACTIONS[args.id];
          const defaultName = mode === "save" ? defaultSaveName(kind, store.get("window")) : "";
          const [path] = await pick(mode, title, filters, false, defaultName);
          if (!path) break;
          if (kind === "saveAs" || kind === "save") {
            const probe = await store.call("file.texture", { kind, path, probe: true });
            if (probe?.needsConfirmOverwrite) {
              const choice = await messageBox({
                title: "Save Texture View As",
                text: `${probe.fileName} already exists.\nDo you want to replace it?`,
                buttons: ["Yes", "No"],
              });
              if (choice !== "Yes") break;
            }
            await store.call("file.texture", { kind, path, overwrite: true });
          } else {
            await store.call("file.texture", { kind, path });
          }
        } else if (args.id?.startsWith("file.texture.")) {
          store.call("file.texture", { kind: args.id.slice("file.texture.".length) });
        }
        break;
      }
    }
  });
}

// A script's settings window: the same declarative controls as the options panel.
function openSettings(store, { name, title, layout }) {
  const shade = document.createElement("div");
  shade.className = "qdialog-shade";
  const dialog = document.createElement("div");
  dialog.className = "qdialog settings-dialog";
  const head = document.createElement("div");
  head.className = "qdialog-title";
  label(head, title ?? name ?? "");
  const body = document.createElement("div");
  const close = document.createElement("button");
  close.type = "button";
  close.className = "qbutton settings-close";
  label(close, "Close");
  close.addEventListener("click", () => shade.remove());
  dialog.append(head, body, close);
  shade.append(dialog);
  document.body.append(shade);
  createToolOptions(body, {
    onOption: (option) => store.call("settings.option", { ...option, menu: name }),
    onNewColor: (palette) => openNewColor(palette),
    onSubWindow: () => null,
  }).build(layout);
}

function normalizeKey(text) {
  return text.toLowerCase().replace(/\s+/g, "");
}

function keyName(e) {
  const parts = [];
  if (e.ctrlKey) parts.push("ctrl");
  if (e.altKey) parts.push("alt");
  if (e.shiftKey) parts.push("shift");
  parts.push(e.key.length === 1 ? e.key.toLowerCase() : e.key.toLowerCase());
  return parts.join("+");
}
