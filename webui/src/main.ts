/// <reference types="vite/client" />
import { invoke } from "@tauri-apps/api/core";
import { loadProject, type ProjectIO } from "../vf/src/project.ts";
import { attachBoardInput } from "./board/input.ts";
import { BoardRenderer } from "./board/renderer.ts";
import { attachBridge, DEFAULT_STATE_MAPPING } from "./bridge.ts";
import { BackendClient } from "./net.ts";
import { createPlayer } from "./player.ts";

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

async function bootstrap(): Promise<void> {
  const appEl = document.getElementById("app");
  if (!appEl) throw new Error("找不到 #app 容器");

  const io = createGlobProjectIO(rawDesignFiles);
  const project = await loadProject(io);
  const client = new BackendClient();
  const player = createPlayer(appEl, project);
  attachBridge(player.runtime, client, DEFAULT_STATE_MAPPING);

  const canvas = document.createElement("canvas");
  canvas.style.cssText = "display:block;width:100%;height:100%;";
  new BoardRenderer(canvas, { view: "main", client });
  attachBoardInput(canvas, { view: "main", client });
  player.setExternal("canvasHost", canvas);

  const wsUrl = await resolveBackendWsUrl();
  if (wsUrl) {
    try {
      await client.connect(wsUrl);
    } catch (e) {
      console.error("连接后端失败:", e);
    }
  }
}

if (typeof document !== "undefined" && document.getElementById("app")) {
  void bootstrap();
}
