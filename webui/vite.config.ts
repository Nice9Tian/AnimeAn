import type { IncomingMessage } from "node:http";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type ProxyOptions } from "vite";

const rootDir = fileURLToPath(new URL(".", import.meta.url));

// Set by tools/mcp-backend-debug/src/launcher.mjs when it starts Vite. The
// backend listens on 127.0.0.1 only and accepts only http://localhost page
// origins, so a browser on another machine reaches it through this proxy;
// BACKEND_PROXY_PATH must match the launcher's.
const BACKEND_PROXY_PATH = "/__backend";
const backendPort = Number(process.env.ANIMEAN_DEV_BACKEND_PORT) || 0;
const allowedHost = process.env.ANIMEAN_DEV_ALLOWED_HOST || "";

// A page can open a WebSocket to any host, so the proxy only lets through
// pages served by this Vite: the Origin must name the host the request came to.
function isSameOrigin(req: IncomingMessage): boolean {
  const { origin, host } = req.headers;
  if (!origin || !host) return false;
  try {
    return new URL(origin).host === host;
  } catch {
    return false;
  }
}

function backendProxy(port: number): Record<string, ProxyOptions> {
  return {
    [BACKEND_PROXY_PATH]: {
      target: `ws://127.0.0.1:${port}`,
      ws: true,
      rewrite: (path) => path.slice(BACKEND_PROXY_PATH.length) || "/",
      bypass: (req) => (isSameOrigin(req) ? undefined : false),
      // The backend sees the page as a localhost one, which it is: Vite
      // served it from this machine.
      configure: (proxy) => {
        proxy.on("proxyReqWs", (proxyReq) => {
          proxyReq.setHeader("origin", `http://localhost:${port}`);
        });
      },
    },
  };
}

export default defineConfig(({ command }) => ({
  server: {
    port: 1430,
    strictPort: true,
    ...(allowedHost ? { allowedHosts: [allowedHost] } : {}),
    ...(command === "serve" && backendPort > 0 ? { proxy: backendProxy(backendPort) } : {}),
  },
  build: {
    rollupOptions: {
      input: {
        main: resolve(rootDir, "index.html"),
        dialog: resolve(rootDir, "dialog.html"),
        pen_probe: resolve(rootDir, "pen_probe.html"),
      },
    },
  },
}));
