/// <reference types="vite/client" />
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { removeNodeReferences, type Project } from "../vf/src/model.ts";
import { loadProject, type ProjectIO } from "../vf/src/project.ts";
import { BackendClient } from "./net.ts";
import { createPlayer } from "./player.ts";
import { RelayClient, RelayHost } from "./relay.ts";
import { createChrome, createStore, DOCK_FOR_WINDOW, panelFactories, syncDocks } from "./ui/app.js";
import { messageBox } from "./ui/panels2.js";
import { createPanelWindows, setupSatelliteDock, triggerDockClosed } from "./ui/panelwindows.js";
import { watchTimelineDock } from "./ui/timeline.js";
import { raiseBusiness, syncTools, watchToolsScroll } from "./ui/tools.js";
import "./ui/qt.css";
import "./ui/qt3.css";
import "./ui/shell.css";

const rawDesignFiles = import.meta.glob(
  [
    "../design/project.json",
    "../design/ui/*.json",
    "../design/logic/*.yaml",
    "../design/components/**/*",
    "../design/functions/**/*",
    "../design/actions/**/*",
    "../design/effects/**/*",
    "../design/docs/**/*",
  ],
  { query: "?raw", import: "default", eager: true },
) as Record<string, string>;

export function createGlobProjectIO(rawMap: Record<string, string>, prefix = "../design/"): ProjectIO {
  const files = new Map<string, string>();
  for (const [rawKey, content] of Object.entries(rawMap)) {
    const normalized = rawKey.replace(/\\/g, "/");
    const rel = normalized.startsWith(prefix) ? normalized.slice(prefix.length) : normalized.replace(/^\/+/, "");
    files.set(rel, content);
  }

  return {
    async root(): Promise<string> {
      return "/design";
    },
    async readText(path: string): Promise<string | null> {
      const clean = path.replace(/\\/g, "/").replace(/^\/+/, "");
      return files.get(clean) ?? null;
    },
    async listDir(dirPath: string): Promise<Array<{ name: string; isDir: boolean }>> {
      const cleanDir = dirPath.replace(/\\/g, "/").replace(/^\/+|\/+$/g, "");
      const dirPrefix = cleanDir ? `${cleanDir}/` : "";
      const entries = new Map<string, boolean>();
      for (const filePath of files.keys()) {
        if (!filePath.startsWith(dirPrefix)) continue;
        const rest = filePath.slice(dirPrefix.length);
        if (!rest) continue;
        const slashIdx = rest.indexOf("/");
        if (slashIdx === -1) {
          if (!entries.has(rest)) entries.set(rest, false);
        } else {
          const folder = rest.slice(0, slashIdx);
          entries.set(folder, true);
        }
      }
      return [...entries.entries()]
        .sort(([a], [b]) => a.localeCompare(b))
        .map(([name, isDir]) => ({ name, isDir }));
    },
  };
}

const inShell = (): boolean => Boolean((window as unknown as { __TAURI__?: unknown }).__TAURI__);

// A packaged window is an application window, not a browser tab. Reloading one would
// also lose track of the panels out in their own windows, so the browser's keys and
// its right-click menu stay out of it. A browser in development keeps them.
function keepBrowserOut(): void {
  const typing = (target: EventTarget | null) =>
    target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement;
  window.addEventListener("contextmenu", (e) => {
    if (!typing(e.target)) e.preventDefault();
  });
  window.addEventListener("keydown", (e) => {
    const browserKey = e.ctrlKey || e.altKey || /^F\d+$/.test(e.key);
    if (browserKey && !typing(e.target)) e.preventDefault();
  });
}

async function resolveBackendWsUrl(): Promise<string | null> {
  if (inShell()) {
    const endpoint = await invoke<{ url?: string } | string>("backend_endpoint");
    if (typeof endpoint === "string") return endpoint;
    if (endpoint && typeof endpoint.url === "string") return endpoint.url;
    return null;
  }
  const params = new URLSearchParams(window.location.search);
  return params.get("ws");
}

// ?only=<dock id> leaves that dock alone in the window, to work on one panel by itself.
export function keepOnlyDock(project: Project, dockId: string): void {
  const { scene } = project;
  const dock = scene.nodes[dockId];
  if (!dock) throw new Error(`场景里没有 ${dockId}`);
  const kept = new Set([dockId]);
  for (const id of kept) for (const child of scene.nodes[id].children ?? []) kept.add(child);
  kept.add(scene.root);
  const removed = new Set(Object.keys(scene.nodes).filter((id) => !kept.has(id)));
  removeNodeReferences(scene, removed);
  for (const id of removed) delete scene.nodes[id];
  const root = scene.nodes[scene.root];
  if (root) {
    delete root.docks;
    delete root.dockLayout;
    root.children = [dockId];
    root.layout = { kind: "vbox" };
  }
  dock.size = { horizontal: "expanding", vertical: "expanding" };
  delete dock.widthStep;
}

async function bootstrap(): Promise<void> {
  const appEl = document.getElementById("app");
  if (!appEl) throw new Error("找不到 #app 容器");
  if (inShell()) keepBrowserOut();

  const io = createGlobProjectIO(rawDesignFiles);
  const project = await loadProject(io);
  const params = new URLSearchParams(window.location.search);
  const onlyDock = params.get("only");
  const isSatellite = params.get("satellite") === "1";
  if (onlyDock) {
    keepOnlyDock(project, onlyDock);
    appEl.style.top = "0";
    appEl.style.bottom = "0";
  }

  const client = isSatellite ? new RelayClient() : new BackendClient();
  const relayHost = !isSatellite && typeof BroadcastChannel !== "undefined" ? new RelayHost(client) : null;
  const store = createStore(client);
  const factories = panelFactories(store, client, { satellite: isSatellite });
  for (const [type, create] of Object.entries(factories)) {
    const component = project.components.get(type);
    if (!component) throw new Error(`design 里缺少 ${type}`);
    component.create = (el) => {
      el.classList.add("qt-panel");
      create(el);
      return { update() {}, destroy() { el.replaceChildren(); } };
    };
  }

  let onTimelineDockChanged: ((dockId: string) => void) | undefined;
  let panelWindows: ReturnType<typeof createPanelWindows> | undefined;
  const satelliteHooks = isSatellite && onlyDock ? setupSatelliteDock(appEl, onlyDock, client) : null;

  const player = createPlayer(appEl, project, {
    onDockChanged: (dockId) => {
      onTimelineDockChanged?.(dockId);
      panelWindows?.onDockChanged(dockId);
    },
    onDockFloatRequest: satelliteHooks ? (dockId) => satelliteHooks.onDockFloatRequest(dockId) : undefined,
    raiseBusiness: (name, args) => raiseBusiness(store, name, args),
    onDockClosed(dockId: string) {
      if (satelliteHooks) {
        satelliteHooks.onDockClosed(dockId);
        return;
      }
      void triggerDockClosed(store, dockId);
    },
  });

  if (!onlyDock) {
    for (const dock of [...Object.values(DOCK_FOR_WINDOW), "timelineDock"]) {
      player.runtime.dockSetVisible(dock, false);
    }
    panelWindows = createPanelWindows(player.runtime, store, {
      host: relayHost ?? undefined,
      onDockRestored: (dockId: string) => onTimelineDockChanged?.(dockId),
      onDockClosed: (dockId: string) => {
        void triggerDockClosed(store, dockId);
      },
      onExternalChanged: () => {
        (store as { texture?: { updateHome(): void } }).texture?.updateHome();
      },
    });
    (store as { externalDocks?: Set<string> }).externalDocks = panelWindows.externalDocks;
    syncDocks(player.runtime, store, panelWindows.externalDocks);
  }

  const toolState = project.logic.state?.tools as { locked?: object } | undefined;
  syncTools(player.runtime, store, Object.keys(toolState?.locked ?? {}));
  watchToolsScroll(appEl, player.runtime, store);
  if (!onlyDock) {
    onTimelineDockChanged = watchTimelineDock(player.runtime, store);
  }
  const menuEl = document.getElementById("qt-menubar")!;
  const statusEl = document.getElementById("qt-statusbar")!;
  createChrome(menuEl, statusEl, store, { satellite: isSatellite });
  menuEl.hidden = statusEl.hidden = Boolean(onlyDock || isSatellite);

  if (isSatellite) {
    await client.connect("");
    return;
  }

  if (inShell()) {
    void listen("backend-exited", () => {
      void messageBox({
        title: "AnimeAn",
        text: "The drawing engine stopped unexpectedly. Changes since the last save are lost.\nClose AnimeAn and start it again.",
      });
    });
  }

  try {
    const wsUrl = await resolveBackendWsUrl();
    if (wsUrl) {
      await client.connect(wsUrl);
      (store as { boards?: { resendSizes(): void } }).boards?.resendSizes();
      (store as { texture?: { announceHome(): void } }).texture?.announceHome();
    }
  } catch (e) {
    console.error("连接后端失败:", e);
    void messageBox({ title: "AnimeAn", text: `The drawing engine could not be started.\n${e}` });
  }
}

if (typeof document !== "undefined" && document.getElementById("app")) {
  void bootstrap();
}
