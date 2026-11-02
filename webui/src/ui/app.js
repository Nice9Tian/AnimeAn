// Joins the drawn panels to the backend: keeps the latest value of every state
// key, hands each panel what it shows, and turns what the user does into calls.
import { BoardRenderer } from "../board/renderer.ts";
import { GlBoardRenderer } from "../board/glrenderer.ts";
import { attachBoardInput } from "../board/input.ts";
import { cursorCss } from "../board/cursor.ts";
import { createCentral, createLayers, createMenuBar, popupMenu, scrollArea } from "./panels.js";
import { createToolOptions } from "./options.js";
import { createPalette, toHexArgb } from "./palette.js";
import { createAssets, createForcePad, createHistory, createNewProjectDialog, createPythonDebug } from "./panels2.js";
import { createTimeline } from "./timeline.js";

function fail(error) {
  console.error(error);
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
  const call = (method, params = {}) => client.call(method, params).catch(fail);
  const cursors = new Map();
  client.onImage((meta, data) => {
    if (!meta.key.startsWith("cur_")) return;
    const canvas = document.createElement("canvas");
    canvas.width = meta.w;
    canvas.height = meta.h;
    const pixels = new ImageData(new Uint8ClampedArray(data), meta.w, meta.h);
    canvas.getContext("2d").putImageData(pixels, 0, 0);
    cursors.set(meta.key, canvas.toDataURL());
  });
  return { on, call, get: (key) => values.get(key), cursorUrl: (key) => cursors.get(key) };
}

const activeView = (store) => store.get("window")?.activeView ?? "main";

// Which scene dock shows which backend window.
export const DOCK_FOR_WINDOW = {
  assets: "assetsDock",
  history: "historyDock",
  repulsion_pad: "forcePadDock",
  python_debug: "pythonDebugDock",
};

// Panels keyed by the component type the scene names.
export function panelFactories(store, client) {
  const { on, call } = store;
  return {
    QtToolOptions(el) {
      el.classList.add("dock-page", "toolopt-gap");
      const tabs = document.createElement("div");
      tabs.className = "tabbar-row";
      tabs.innerHTML = `<div class="tab sel" style="width:64px">Options</div>`;
      const body = scrollArea("opt-body");
      el.append(tabs, body.frame);
      const panel = createToolOptions(body.view, {
        onOption: (option) => call("tool.option", option),
        onPalette: (action, control) => call("tool.option", {
          hook: "palette_box", name: control.name, type: "palette", value: action,
          row: control.row ?? 0, startColumn: control.start_column ?? 0, endColumn: control.end_column ?? 0,
        }),
        onNewColor: (palette) => openNewColor(palette),
        onSubWindow: () => subWindowNote(),
      });
      on("toolOptions", (value) => panel.build(value));
    },

    QtLayers(el) {
      const panel = createLayers(el, {
        onAdd: (view) => call("layer.add", { view }),
        onRemove: (view) => {
          const selected = store.get(`layers/${view}`)?.selected ?? -1;
          if (selected >= 0) call("layer.delete", { view, layerIndex: selected });
        },
        onClear: (view) => call("layer.unselect", { view }),
        onSelect: (view, layerIndex) => call("layer.select", { view, layerIndex }),
        onVisible: (view, layerIndex, visible) => call("layer.setVisible", { view, layerIndex, visible }),
        onCollapse: (view, groupId, collapsed) => call("layer.setCollapsed", { view, groupId, collapsed }),
        onMove: (view, from, to) => {
          if (from.layer !== undefined && to.layer !== undefined) call("layer.move", { view, fromIndex: from.layer, toIndex: to.layer });
        },
        onContext: async (view, row, members, x, y) => {
          const target = {
            view,
            groupId: row?.group ? row.group_id : 0,
            groupName: row?.group ? row.name : "",
            layer: row && !row.group ? row.layer : -1,
            members,
          };
          const reply = await call("layer.contextMenu", target);
          if (!reply?.items?.length) return;
          popupMenu(reply.items, x, y, (item) => call("layer.contextAction", { ...target, id: item.id, title: item.text }));
        },
      });
      on("layers/main", (value) => panel.update("main", value));
      on("layers/child", (value) => panel.update("child", value));
    },

    QtAssets(el) {
      const panel = createAssets(el, {
        onSelect: (assetIndex) => call("asset.select", { assetIndex, view: activeView(store) }),
        onAdd: () => call("asset.add", { view: activeView(store) }),
        onClear: () => call("asset.unselect", { view: activeView(store) }),
      });
      const show = () => {
        const state = store.get(`assets/${activeView(store)}`);
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
      const panel = createPythonDebug(el, {
        onRun: async (command) => {
          const reply = await call("python.run", { command });
          if (reply?.output) panel.append(reply.output);
        },
      });
      on("window", (value) => panel.update({ output: (value?.pythonDebug ?? []).join("\n") }));
    },

    QtTimeline(el) {
      const panel = createTimeline(el, {
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
        onOrientation: (vertical) => call("timeline.setDockArea", { dockArea: vertical ? "right" : "bottom" }),
        onSelectFrame: (frameIndex) => call("frame.select", { frameIndex }),
        onMoveFrame: (fromIndex, toIndex) => call("frame.move", { fromIndex, toIndex }),
        onFrameTyped: (frameIndex) => call("frame.select", { frameIndex }),
        onLaneToggled: (frame, on) => call("onion.toggleFrame", { frame, on }),
      });
      store.timeline = panel;
      on("timeline", (value) => {
        if (!value) return;
        panel.update({
          frames: (value.frames ?? []).map((f) => ({ index: f.index, name: f.name, held: f.hold })),
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
      });
    },

    QtCentral(el) {
      el.classList.add("central");
      const panel = createCentral(el, {
        onPage: (view) => { boards.show(view); call("view.setActive", { viewName: view }); },
        onScroll: (axis, value) => call("board.scroll", { view: boards.current, axis, value }),
        onPill: () => call("menu.trigger", { id: "windows.timeline" }),
      });
      const boards = createBoards(panel.host, client, store);
      store.boards = boards;
      on("windows", (value) => { panel.pill.style.display = value?.timeline?.visible ? "none" : ""; });
      on("event", (event) => {
        if (event?.name === "textureView.surface") { panel.show(1); boards.show("child"); }
        if (event?.name === "window.select" && event.args?.name === "paint") {
          const child = event.args.page === "texture";
          panel.show(child ? 1 : 0);
          boards.show(child ? "child" : "main");
        }
      });
      const syncScroll = (view) => (value) => {
        if (view !== boards.current || !value) return;
        panel.h.set(value.h);
        panel.v.set(value.v);
      };
      on("viewport/main", syncScroll("main"));
      on("viewport/child", syncScroll("child"));
    },
  };
}

function subWindowNote() {
  const note = document.createElement("div");
  note.className = "opt-title";
  note.textContent = "Texture view: on the Texture page";
  return note;
}

function openNewColor(palette) {
  const shade = document.createElement("div");
  shade.className = "qdialog-shade";
  const dialog = document.createElement("div");
  dialog.className = "qdialog newcolor-dialog";
  const inner = createPalette(() => {}, { pickersOnly: true });
  const add = document.createElement("button");
  add.type = "button";
  add.className = "qbutton";
  add.textContent = "Add";
  add.addEventListener("click", () => { palette.addSwatch(toHexArgb(inner.color())); shade.remove(); });
  shade.addEventListener("pointerdown", (e) => { if (e.target === shade) shade.remove(); });
  dialog.append(inner.el, add);
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

function createBoards(host, client, store) {
  const slots = { main: { view: "main" }, child: { view: "child" } };
  const mount = (slot, kind) => {
    const { canvas, renderer, kind: started } = startRenderer(kind, { view: slot.view, client });
    const hadFocus = slot.canvas && document.activeElement === slot.canvas;
    if (slot.canvas) slot.canvas.replaceWith(canvas);
    else host.append(canvas);
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
    rendererKind: savedRendererKind(),
    resendSizes: () =>
      Object.values(slots).forEach((slot) => {
        slot.input.resend?.();
        client?.call?.("board.capabilities", { view: slot.view, fieldDisplay: boards.rendererKind === "webgl" })?.catch(() => {});
      }),
    show(view) {
      boards.current = view;
      slots.main.canvas.style.visibility = view === "main" ? "visible" : "hidden";
      slots.child.canvas.style.visibility = view === "child" ? "visible" : "hidden";
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
  boards.show("main");
  return boards;
}

// Keeps scene docks in step with the backend's window list.
export function syncDocks(runtime, store) {
  store.on("windows", (value) => {
    for (const [name, dock] of Object.entries(DOCK_FOR_WINDOW)) {
      runtime.dockSetVisible(dock, Boolean(value?.[name]?.visible));
    }
    runtime.dockSetVisible("timelineDock", Boolean(value?.timeline?.visible));
  });
}

// The chrome outside the docked scene: menu bar and status bar.
export function createChrome(menuEl, statusEl, store) {
  const RENDERER_ITEM = "settings.renderer.";
  const trigger = (item) => {
    if (item.id?.startsWith(RENDERER_ITEM)) {
      store.boards?.setRenderer(item.id.slice(RENDERER_ITEM.length));
      paintMenus();
      return undefined;
    }
    return store.call("menu.trigger", {
      id: item.id,
      checked: item.kind === "check" ? !item.checked : item.kind === "radio" ? true : undefined,
    });
  };
  const bar = createMenuBar(menuEl, { onTrigger: trigger });
  // The renderer belongs to this page, not the document, so its menu is added
  // here rather than coming from the backend's menu state.
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
      }],
    };
  };
  const paintMenus = () => bar.update([...(store.get("menus") ?? []), settingsMenu()]);
  store.on("menus", paintMenus);
  const text = document.createElement("div");
  text.className = "status-text";
  const grip = document.createElement("div");
  grip.className = "size-grip";
  statusEl.append(text, grip);
  store.on("window", (value) => {
    text.textContent = value?.status ?? "";
    if (value?.title) window.__TAURI__?.core.invoke("set_window_title", { title: value.title });
  });
  store.on("theme", (value) => {
    document.documentElement.dataset.theme = value?.mode === "dark" ? "dark" : "light";
  });
  const dialogRoot = document.createElement("div");
  document.body.append(dialogRoot);
  const canvasDialog = createNewProjectDialog(dialogRoot, {
    onCreate: (size) => store.call("project.new", size),
    onCancel: () => {},
  });
  canvasDialog.update({ open: false });
  // The Qt build opens on this dialog too (MainWindow::promptForNewCanvasOnStartup).
  let asked = false;
  store.on("window", (value) => {
    if (asked || !value?.canvasWidth) return;
    asked = true;
    canvasDialog.update({ width: value.canvasWidth, height: value.canvasHeight, open: true });
  });

  // Native pickers live in the shell; the page only says what to pick.
  const pick = (mode, title, filters, multiple = false) =>
    window.__TAURI__?.core.invoke("pick_files", { mode, title, filters, multiple }) ?? Promise.resolve([]);
  const PROJECT = [{ name: "AnimeAn Projects", extensions: ["anproj"] }];
  const TEXTURE = [{ name: "AnimeAn Texture Views", extensions: ["textureview"] }];
  const IMPORTS = {
    "file.import.raster": ["raster", "Import Raster", [{ name: "Images", extensions: ["png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"] }]],
    "file.import.opentoonz": ["opentoonz", "Import OpenToonz Lines", [{ name: "OpenToonz Vector Levels", extensions: ["pli"] }]],
    "file.import.csp": ["csp", "Import Clip Studio Paint", [{ name: "Clip Studio Paint", extensions: ["clip"] }]],
  };
  const TEXTURE_ACTIONS = {
    "file.texture.open": ["open", "open", "Open Texture View", TEXTURE],
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
        const [path] = await pick("save", "Save Project As", PROJECT);
        if (path) store.call("project.saveAs", { path });
        break;
      }
      case "menu.action": {
        if (IMPORTS[args.id]) {
          const [kind, title, filters] = IMPORTS[args.id];
          const paths = await pick("open", title, filters);
          if (paths.length) store.call("file.import", { kind, paths });
        } else if (TEXTURE_ACTIONS[args.id]) {
          const [kind, mode, title, filters] = TEXTURE_ACTIONS[args.id];
          const [path] = await pick(mode, title, filters);
          if (path) store.call("file.texture", { kind, path });
        } else if (args.id?.startsWith("file.texture.")) {
          store.call("file.texture", { kind: args.id.slice("file.texture.".length) });
        }
        break;
      }
    }
  });

  // Menu shortcuts: the menus state carries them, so Python-added ones work too.
  const shortcuts = new Map();
  const collect = (items) => {
    for (const item of items ?? []) {
      if (item.shortcut && item.enabled !== false) shortcuts.set(normalizeKey(item.shortcut), item.id);
      collect(item.children);
    }
  };
  store.on("menus", (value) => { shortcuts.clear(); collect(value); });
  window.addEventListener("keydown", (e) => {
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
    const id = shortcuts.get(keyName(e));
    if (id) { e.preventDefault(); store.call("menu.trigger", { id }); }
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
  head.textContent = title ?? name ?? "";
  const body = document.createElement("div");
  const close = document.createElement("button");
  close.type = "button";
  close.className = "qbutton settings-close";
  close.textContent = "Close";
  close.addEventListener("click", () => shade.remove());
  dialog.append(head, body, close);
  shade.append(dialog);
  document.body.append(shade);
  createToolOptions(body, {
    onOption: (option) => store.call("settings.option", { ...option, menu: name }),
    onPalette: () => {},
    onNewColor: () => {},
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
