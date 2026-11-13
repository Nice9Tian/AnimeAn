import http from "node:http";

const LOOPBACK_HOSTS = new Set(["127.0.0.1", "localhost", "[::1]"]);

/** @param {string|undefined} hostHeader */
export function isLoopbackHost(hostHeader) {
  if (!hostHeader) return false;
  const host = String(hostHeader).trim().toLowerCase().replace(/:\d+$/, "");
  return LOOPBACK_HOSTS.has(host);
}

/**
 * Start a minimal Streamable HTTP MCP transport bound to 127.0.0.1:<port>.
 *
 * Exposes:
 * - `POST /mcp` : JSON-RPC 2.0 request/notification or batch
 * - `GET /health` : Health probe returning server status
 */
export async function startHttpTransport({
  handler,
  port = 19440,
  host = "127.0.0.1",
  serverName = "animean-backend-debug",
}) {
  const server = http.createServer(async (req, res) => {
    const urlPath = (req.url || "/").split("?")[0];

    // `eval_python` runs code in the backend, so no web page may reach this
    // server: no CORS headers (MCP clients are not browsers), requests a
    // browser makes on a page's behalf carry an Origin and are refused, and a
    // Host other than loopback (DNS rebinding) is refused too.
    if (req.headers.origin !== undefined || !isLoopbackHost(req.headers.host)) {
      res.writeHead(403, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify({ error: "Forbidden: browser origins and non-loopback hosts are not accepted" }));
      return;
    }

    if (req.method === "GET" && urlPath === "/health") {
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify({ ok: true, server: serverName, transport: "streamable-http" }));
      return;
    }

    if (req.method === "POST" && urlPath === "/mcp") {
      const chunks = [];
      let totalBytes = 0;
      const MAX_BODY = 16 * 1024 * 1024;
      for await (const chunk of req) {
        totalBytes += chunk.length;
        if (totalBytes > MAX_BODY) {
          res.writeHead(413, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ error: "Payload too large" }));
          return;
        }
        chunks.push(chunk);
      }
      const rawBody = Buffer.concat(chunks).toString("utf-8");
      let parsed;
      try {
        parsed = JSON.parse(rawBody);
      } catch {
        res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
        res.end(
          JSON.stringify({
            jsonrpc: "2.0",
            id: null,
            error: { code: -32700, message: "Parse error" },
          })
        );
        return;
      }

      const reply = await handler.handlePayload(parsed);
      if (reply === null || reply === undefined) {
        res.writeHead(202);
        res.end();
        return;
      }
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify(reply));
      return;
    }

    res.writeHead(404, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify({ error: "Not found" }));
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, host, () => {
      server.off("error", reject);
      resolve();
    });
  });

  const addr = server.address();
  const actualPort = typeof addr === "object" && addr ? addr.port : port;
  return {
    server,
    port: actualPort,
    url: `http://${host}:${actualPort}/mcp`,
    healthUrl: `http://${host}:${actualPort}/health`,
    close: () =>
      new Promise((resolve) => {
        server.close(() => resolve());
      }),
  };
}
