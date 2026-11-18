import type { IncomingMessage } from "node:http";
import { isIP } from "node:net";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type ProxyOptions } from "vite";

const rootDir = fileURLToPath(new URL(".", import.meta.url));

// Set by tools/mcp-backend-debug/src/launcher.mjs when it starts Vite with
// --host. The backend listens on 127.0.0.1 only and accepts only
// http://localhost page origins, so a browser on another machine reaches it
// through this proxy; BACKEND_PROXY_PATH must match the launcher's.
const BACKEND_PROXY_PATH = "/__backend";
const publicHost = process.env.ANIMEAN_DEV_HOST || "";
const backendPort = Number(process.env.ANIMEAN_DEV_BACKEND_PORT) || 0;

function hostnameOf(hostOrUrl: string): string | null {
  try {
    const url = new URL(hostOrUrl.includes("://") ? hostOrUrl : `http://${hostOrUrl}`);
    return url.hostname.replace(/^\[|\]$/g, "");
  } catch {
    return null;
  }
}

// Only pages this Vite served on the --host address get through: both the
// Host the request came to and the page's Origin must name that address. A
// same-origin check alone would let a DNS-rebound page in.
function isOwnPage(req: IncomingMessage): boolean {
  const { origin, host } = req.headers;
  if (!origin || !host) return false;
  const want = publicHost.replace(/^\[|\]$/g, "");
  return hostnameOf(host) === want && hostnameOf(origin) === want && new URL(origin).host === host;
}

function backendProxy(port: number): Record<string, ProxyOptions> {
  return {
    [BACKEND_PROXY_PATH]: {
      target: `ws://127.0.0.1:${port}`,
      ws: true,
      rewrite: (path) => path.slice(BACKEND_PROXY_PATH.length) || "/",
      bypass: (req) => (isOwnPage(req) ? undefined : false),
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
    // Vite answers only host names it knows; IP addresses it always accepts.
    ...(publicHost && !isIP(publicHost) ? { allowedHosts: [publicHost] } : {}),
    ...(command === "serve" && publicHost && backendPort > 0 ? { proxy: backendProxy(backendPort) } : {}),
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
