/// <reference types="vite/client" />
import { invoke } from "@tauri-apps/api/core";
import { removeNodeReferences, type Project } from "../vf/src/model.ts";
import { loadProject, type ProjectIO } from "../vf/src/project.ts";
import { BackendClient } from "./net.ts";
import { createPlayer } from "./player.ts";
import { createChrome, createStore, DOCK_FOR_WINDOW, panelFactories, syncDocks } from "./ui/app.js";
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

async function resolveBackendWsUrl(): Promise<string | null> {
  const w = window as unknown as { __TAURI__?: unknown };
  if (w.__TAURI__) {
    const endpoint = await invoke<{ url?: string } | string>("backend_endpoint");
    if (typeof endpoint === "string") return endpoint;
    if (endpoint && typeof endpoint.url === "string") return endpoint.url;
    return null;
  }
  const params = new URLSearchParams(window.location.search);
  return params.get("ws");
}

// ?only=<dock id> leaves that dock alone in the window, to work on one panel by itself.
function keepOnlyDock(project: Project, dockId: string): void {
  const { scene } = project;
  const dock = scene.nodes[dockId];
  if (!dock) throw new Error(`场景里没有 ${dockId}`);
  const kept = new Set([dockId]);
  for (const id of kept) for (const child of scene.nodes[id].children ?? []) kept.add(child);
  kept.add(scene.root);
  const removed = new Set(Object.keys(scene.nodes).filter((id) => !kept.has(id)));
  removeNodeReferences(scene, removed);
  for (const id of removed) delete scene.nodes[id];
}

async function bootstrap(): Promise<void> {
  const appEl = document.getElementById("app");
  if (!appEl) throw new Error("找不到 #app 容器");

  const io = createGlobProjectIO(rawDesignFiles);
  const project = await loadProject(io);
  const onlyDock = new URLSearchParams(window.location.search).get("only");
  if (onlyDock) keepOnlyDock(project, onlyDock);
  const client = new BackendClient();
  const store = createStore(client);
  const factories = panelFactories(store, client);
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
  const player = createPlayer(appEl, project, {
    onDockChanged: (dockId) => onTimelineDockChanged?.(dockId),
    raiseBusiness: (name, args) => raiseBusiness(store, name, args),
    onDockClosed(dockId: string) {
      const name = Object.entries(DOCK_FOR_WINDOW).find(([, dock]) => dock === dockId)?.[0];
      if (name) void store.call("menu.trigger", { id: `windows.${name}` });
    },
  });
  for (const dock of [...Object.values(DOCK_FOR_WINDOW), "timelineDock"]) player.runtime.dockSetVisible(dock, false);
  syncDocks(player.runtime, store);
  const toolState = project.logic.state?.tools as { locked?: object } | undefined;
  syncTools(player.runtime, store, Object.keys(toolState?.locked ?? {}));
  watchToolsScroll(appEl, player.runtime, store);
  onTimelineDockChanged = watchTimelineDock(player.runtime, store);
  const menuEl = document.getElementById("qt-menubar")!;
  const statusEl = document.getElementById("qt-statusbar")!;
  createChrome(menuEl, statusEl, store);
  menuEl.hidden = statusEl.hidden = Boolean(onlyDock);

  const wsUrl = await resolveBackendWsUrl();
  if (wsUrl) {
    try {
      await client.connect(wsUrl);
      (store as { boards?: { resendSizes(): void } }).boards?.resendSizes();
    } catch (e) {
      console.error("连接后端失败:", e);
    }
  }
}

if (typeof document !== "undefined" && document.getElementById("app")) {
  void bootstrap();
}
